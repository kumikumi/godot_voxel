#ifndef VOXEL_LOD_TERRAIN_UPDATE_DATA_H
#define VOXEL_LOD_TERRAIN_UPDATE_DATA_H

#include "../../constants/voxel_constants.h"
#include "../../engine/detail_rendering.h"
#include "../../generators/voxel_generator.h"
#include "../../storage/voxel_data.h"
#include "../../streams/voxel_stream.h"
#include "../../util/fixed_array.h"
#include "../voxel_mesh_map.h"
#include "lod_octree.h"

#include <map>
#include <unordered_set>

namespace zylann {

class AsyncDependencyTracker;

namespace voxel {

// Settings and states needed for the multi-threaded part of the update loop of VoxelLodTerrain.
// See `VoxelLodTerrainUpdateTask` for more info.
struct VoxelLodTerrainUpdateData {
	struct OctreeItem {
		LodOctree octree;
	};

	struct TransitionUpdate {
		Vector3i chunk_position;
		uint8_t transition_mask;
	};

	struct ChunkLocation {
		Vector3i position;
		uint8_t lod;
	};

	// struct BlockToSave {
	// 	std::shared_ptr<VoxelBufferInternal> voxels;
	// 	Vector3i position;
	// 	uint8_t lod;
	// };

	// These values don't change during the update task.
	struct Settings {
		// Area within which voxels can exist.
		// Note, these bounds might not be exactly represented. This volume is chunk-based, so the result will be
		// approximated to the closest chunk.
		// Box3i bounds_in_voxels;
		// unsigned int lod_count = 0;

		// Distance between a viewer and the end of LOD0
		float lod_distance = 0.f;
		unsigned int view_distance_voxels = 512;
		// bool full_load_mode = false;
		bool run_stream_in_editor = true;
		// If true, try to generate chunks and store them in the data map before posting mesh requests.
		// If false, everything will generate non-edited voxels on the fly instead.
		// Not really exposed for now, will wait for it to be really needed. It might never be.
		bool cache_generated_chunks = false;
		bool collision_enabled = true;
		bool detail_textures_use_gpu = false;
		bool generator_use_gpu = false;
		uint8_t detail_texture_generator_override_begin_lod_index = 0;
		unsigned int chunk_mesh_size_po2 = 4;
		DetailRenderingSettings detail_texture_settings;
		Ref<VoxelGenerator> detail_texture_generator_override;
	};

	enum MeshState {
		MESH_NEVER_UPDATED = 0, // TODO Redundant with MESH_NEED_UPDATE?
		MESH_UP_TO_DATE,
		MESH_NEED_UPDATE, // The mesh is out of date but was not yet scheduled for update
		MESH_UPDATE_NOT_SENT, // The mesh is out of date and was scheduled for update, but no request have been sent
							  // yet
		MESH_UPDATE_SENT // The mesh is out of date, and an update request was sent, pending response
	};

	enum DetailTextureState { //
		DETAIL_TEXTURE_IDLE = 0,
		DETAIL_TEXTURE_NEED_UPDATE,
		DETAIL_TEXTURE_PENDING
	};

	struct ChunkMeshState {
		std::atomic<MeshState> state;
		std::atomic<DetailTextureState> detail_texture_state;
		uint8_t transition_mask;
		bool active;
		// bool pending_transition_update;

		ChunkMeshState() :
				state(MESH_NEVER_UPDATED),
				detail_texture_state(DETAIL_TEXTURE_IDLE),
				transition_mask(0),
				active(false) {}
	};

	// Version of the mesh map designed to be mainly used for the threaded update task.
	// It contains states used to determine when to actually load/unload meshes.
	struct MeshMapState {
		// Values in this map are expected to have stable addresses.
		std::unordered_map<Vector3i, ChunkMeshState> map;
		// Locked for writing when chunks get inserted or removed from the map.
		// If you need to lock more than one Lod, always do so in increasing order, to avoid deadlocks.
		// IMPORTANT:
		// - The update task will add and remove chunks from this map.
		// - Threads outside the update task must never add or remove chunks to the map (even with locking),
		//   unless the task is not running in parallel.
		RWLock map_lock;
	};

	// Each LOD works in a set of coordinates spanning 2x more voxels the higher their index is
	struct Lod {
		// Keeping track of asynchronously loading chunks so we don't try to redundantly load them
		std::unordered_set<Vector3i> loading_chunks;
		BinaryMutex loading_chunks_mutex;

		// These are relative to this LOD, in chunk coordinates
		Vector3i last_viewer_chunk_pos;
		int last_view_distance_chunks = 0;

		MeshMapState mesh_map_state;

		std::vector<Vector3i> chunks_pending_update;
		Vector3i last_viewer_chunk_mesh_pos;
		int last_view_distance_chunk_meshes = 0;

		// Deferred outputs to main thread
		std::vector<Vector3i> chunk_meshes_to_unload;
		std::vector<TransitionUpdate> chunk_meshes_to_update_transitions;
		std::vector<Vector3i> chunk_meshes_to_activate;
		std::vector<Vector3i> chunk_meshes_to_deactivate;

		inline bool has_loading_chunk(const Vector3i &pos) const {
			return loading_chunks.find(pos) != loading_chunks.end();
		}
	};

	struct AsyncEdit {
		IThreadedTask *task;
		Box3i box;
		std::shared_ptr<AsyncDependencyTracker> task_tracker;
	};

	struct RunningAsyncEdit {
		std::shared_ptr<AsyncDependencyTracker> tracker;
		Box3i box;
	};

	struct Stats {
		uint32_t blocked_lods = 0;
		uint32_t time_detect_required_chunks = 0;
		uint32_t time_io_requests = 0;
		uint32_t time_mesh_requests = 0;
		uint32_t time_total = 0;
	};

	// Data modified by the update task
	struct State {
		// This terrain type is a sparse grid of octrees.
		// Indexed by a grid coordinate whose step is the size of the highest-LOD chunk.
		// Not using a pointer because Map storage is stable.
		// TODO Optimization: could be replaced with a grid data structure
		std::map<Vector3i, OctreeItem> lod_octrees;
		Box3i last_octree_region_box;
		Vector3i local_viewer_pos_previous_octree_update;
		bool had_blocked_octree_nodes_previous_update = false;
		bool force_update_octrees_next_update = false;

		FixedArray<Lod, constants::MAX_LOD> lods;

		// This is the entry point for notifying data changes, which will cause mesh updates.
		// Contains chunks that were edited and need their LOD counterparts to be updated.
		// Scheduling is only done at LOD0 because it is the only editable LOD.
		std::vector<Vector3i> chunks_pending_lodding_lod0;
		BinaryMutex chunks_pending_lodding_lod0_mutex;

		std::vector<AsyncEdit> pending_async_edits;
		BinaryMutex pending_async_edits_mutex;
		std::vector<RunningAsyncEdit> running_async_edits;

		// Areas where generated stuff has changed. Similar to an edit, but non-destructive.
		std::vector<Box3i> changed_generated_areas;
		BinaryMutex changed_generated_areas_mutex;

		Stats stats;
	};

	// Set to true when the update task is finished
	std::atomic_bool task_is_complete = { true };
	// Will be locked as long as the update task is running.
	BinaryMutex completion_mutex;

	Settings settings;
	State state;

	// After this call, no locking is necessary, as no other thread should be using the data.
	// However it can stall for longer, so prefer using it when doing structural changes, such as changing LOD count,
	// LOD distances, or the way the update logic runs.
	void wait_for_end_of_task() {
		MutexLock lock(completion_mutex);
	}
};

} // namespace voxel
} // namespace zylann

#endif // VOXEL_LOD_TERRAIN_UPDATE_DATA_H
