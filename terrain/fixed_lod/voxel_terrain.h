#ifndef VOXEL_TERRAIN_H
#define VOXEL_TERRAIN_H

#include "../../constants/voxel_constants.h"
#include "../../engine/meshing_dependency.h"
#include "../../storage/voxel_data.h"
#include "../../util/godot/memory.h"
#include "../../util/math/box3i.h"
#include "../voxel_chunk_enter_info.h"
#include "../voxel_mesh_map.h"
#include "../voxel_node.h"
#include "voxel_chunk_mesh_vt.h"
#include "voxel_terrain_multiplayer_synchronizer.h"

namespace zylann {

class AsyncDependencyTracker;

namespace voxel {

class VoxelTool;
class VoxelInstancer;
class VoxelSaveCompletionTracker;
class VoxelTerrainMultiplayerSynchronizer;

// Infinite paged terrain made of voxel chunks all with the same level of detail.
// Voxels are polygonized around the viewer by distance in a large cubic space.
// Data is streamed using a VoxelStream.
class VoxelTerrain : public VoxelNode {
	GDCLASS(VoxelTerrain, VoxelNode)
public:
	static const unsigned int MAX_VIEW_DISTANCE_FOR_LARGE_VOLUME = 512;

	VoxelTerrain();
	~VoxelTerrain();

	void set_stream(Ref<VoxelStream> p_stream) override;
	Ref<VoxelStream> get_stream() const override;

	void set_generator(Ref<VoxelGenerator> p_generator) override;
	Ref<VoxelGenerator> get_generator() const override;

	void set_mesher(Ref<VoxelMesher> mesher) override;
	Ref<VoxelMesher> get_mesher() const override;

	unsigned int get_chunk_size_pow2() const;
	inline unsigned int get_chunk_size() const {
		return 1 << get_chunk_size_pow2();
	}
	// void set_chunk_size_po2(unsigned int p_chunk_size_po2);

	unsigned int get_chunk_mesh_size_pow2() const;
	inline unsigned int get_chunk_mesh_size() const {
		return 1 << get_chunk_mesh_size_pow2();
	}
	void set_chunk_mesh_size(unsigned int p_chunk_size);

	void post_edit_voxel(Vector3i pos);
	void post_edit_area(Box3i box_in_voxels, bool update_mesh);

	void set_generate_collisions(bool enabled);
	bool get_generate_collisions() const {
		return _generate_collisions;
	}

	void set_collision_layer(int layer);
	int get_collision_layer() const;

	void set_collision_mask(int mask);
	int get_collision_mask() const;

	void set_collision_margin(float margin);
	float get_collision_margin() const;

	unsigned int get_max_view_distance() const;
	void set_max_view_distance(unsigned int distance_in_voxels);

	void set_chunk_enter_notification_enabled(bool enable);
	bool is_chunk_enter_notification_enabled() const;

	void set_area_edit_notification_enabled(bool enable);
	bool is_area_edit_notification_enabled() const;

	void set_automatic_loading_enabled(bool enable);
	bool is_automatic_loading_enabled() const;

	void set_material_override(Ref<Material> material);
	Ref<Material> get_material_override() const;

	void set_generator_use_gpu(bool enabled);
	bool get_generator_use_gpu() const;

	VoxelData &get_storage() const {
		ZN_ASSERT(_data != nullptr);
		return *_data;
	}

	std::shared_ptr<VoxelData> get_storage_shared() const {
		return _data;
	}

	Ref<VoxelTool> get_voxel_tool() override;

	// Creates or overrides whatever chunk data there is at the given position.
	// The use case is multiplayer, client-side.
	// If no local viewer is actually in range, the data will not be applied and the function returns `false`.
	bool try_set_chunk_data(Vector3i position, std::shared_ptr<VoxelBufferInternal> &voxel_data);

	bool has_chunk(Vector3i position) const;

	void set_run_stream_in_editor(bool enable);
	bool is_stream_running_in_editor() const;

	void set_bounds(Box3i box);
	Box3i get_bounds() const;

	void restart_stream() override;
	void remesh_all_chunks() override;

	// Asks to generate (or re-generate) a chunk at the given position asynchronously.
	// If the chunk already exists once the chunk is generated, it will be cancelled.
	// If the chunk is out of range of any viewer, it will be cancelled.
	void generate_chunk_async(Vector3i chunk_position);

	struct Stats {
		int updated_chunks = 0;
		int dropped_chunk_loads = 0;
		int dropped_chunk_meshes = 0;
		uint32_t time_detect_required_chunks = 0;
		uint32_t time_request_chunks_to_load = 0;
		uint32_t time_process_load_responses = 0;
		uint32_t time_request_chunks_to_update = 0;
	};

	const Stats &get_stats() const;

	// struct ChunkToSave {
	// 	std::shared_ptr<VoxelBufferInternal> voxels;
	// 	Vector3i position;
	// };

	// Internal

	void set_instancer(VoxelInstancer *instancer);
	void get_meshed_chunk_positions(std::vector<Vector3i> &out_positions) const;
	Array get_chunk_mesh_surface(Vector3i chunk_pos) const;

	VolumeID get_volume_id() const override {
		return _volume_id;
	}

	std::shared_ptr<StreamingDependency> get_streaming_dependency() const override {
		return _streaming_dependency;
	}

	void get_viewers_in_area(std::vector<ViewerID> &out_viewer_ids, Box3i voxel_box) const;

	void set_multiplayer_synchronizer(VoxelTerrainMultiplayerSynchronizer *synchronizer);
	const VoxelTerrainMultiplayerSynchronizer *get_multiplayer_synchronizer() const;

#ifdef TOOLS_ENABLED
	void get_configuration_warnings(PackedStringArray &warnings) const override;
#endif // TOOLS_ENABLED

protected:
	void _notification(int p_what);

	void _on_gi_mode_changed() override;
	void _on_shadow_casting_changed() override;

private:
	void process();
	void process_viewers();
	void process_viewer_data_box_change(
			ViewerID viewer_id, Box3i prev_data_box, Box3i new_data_box, bool can_load_chunks);
	// void process_received_chunks();
	void process_meshing();
	void apply_mesh_update(const VoxelEngine::ChunkMeshOutput &ob);
	void apply_chunk_response(VoxelEngine::ChunkDataOutput &ob);

	void _on_stream_params_changed();
	// void _set_chunk_size_po2(int p_chunk_size_po2);
	// void make_all_view_dirty();
	void start_updater();
	void stop_updater();
	void start_streamer();
	void stop_streamer();
	void reset_map();

	// void view_chunk(Vector3i bpos, uint32_t viewer_id, bool require_notification);
	void view_chunk_mesh(Vector3i bpos, bool mesh_flag, bool collision_flag);
	// void unview_chunk(Vector3i bpos);
	void unview_chunk_mesh(Vector3i bpos, bool mesh_flag, bool collision_flag);
	// void unload_chunk(Vector3i bpos);
	void unload_chunk_mesh(Vector3i bpos);
	// void make_chunk_dirty(Vector3i bpos);
	void try_schedule_mesh_update(VoxelChunkMeshVT &block);
	void try_schedule_mesh_update_from_data(const Box3i &box_in_voxels);

	void save_all_modified_chunks(bool with_copy, std::shared_ptr<AsyncDependencyTracker> tracker);
	void get_viewer_pos_and_direction(Vector3 &out_pos, Vector3 &out_direction) const;
	void send_data_load_requests();
	void consume_chunk_data_save_requests(
			BufferedTaskScheduler &task_scheduler, std::shared_ptr<AsyncDependencyTracker> saving_tracker);

	void emit_chunk_data_loaded(Vector3i bpos);
	void emit_chunk_data_unloaded(Vector3i bpos);

	void emit_chunk_mesh_entered(Vector3i bpos);
	void emit_chunk_mesh_exited(Vector3i bpos);

	bool try_get_paired_viewer_index(ViewerID id, size_t &out_i) const;

	void notify_chunk_enter(const VoxelChunkData &block, Vector3i bpos, ViewerID viewer_id);

	bool is_area_meshed(const Box3i &box_in_voxels) const;

#ifdef ZN_GODOT
	// Called each time a data chunk enters a viewer's area.
	// This can be either when the chunk exists and the viewer gets close enough, or when it gets loaded.
	// This only happens if data chunk enter notifications are enabled.
	GDVIRTUAL1(_on_chunk_entered, VoxelChunkEnterInfo *);

	// Called each time voxels are edited within a region.
	GDVIRTUAL2(_on_area_edited, Vector3i, Vector3i);
#elif defined(ZN_GODOT_EXTENSION)
	// TODO GDX: Defining custom virtual functions is not supported...
#endif

	static void _bind_methods();

	// Bindings
	Vector3i _b_voxel_to_chunk(Vector3 pos) const;
	Vector3i _b_chunk_to_voxel(Vector3i pos) const;
	// void _force_load_chunks_binding(Vector3 center, Vector3 extents) { force_load_chunks(center, extents); }
	Ref<VoxelSaveCompletionTracker> _b_save_modified_chunks();
	void _b_save_chunk(Vector3i p_chunk_pos);
	void _b_set_bounds(AABB aabb);
	AABB _b_get_bounds() const;
	bool _b_try_set_chunk_data(Vector3i position, Ref<gd::VoxelBuffer> voxel_data);
	Dictionary _b_get_statistics() const;
	PackedInt32Array _b_get_viewer_network_peer_ids_in_area(Vector3i area_origin, Vector3i area_size) const;
	void _b_rpc_receive_block(PackedByteArray data);
	void _b_rpc_receive_area(PackedByteArray data);
	bool _b_is_area_meshed(AABB aabb) const;

	VolumeID _volume_id;

	// Paired viewers are VoxelViewers which intersect with the boundaries of the volume
	struct PairedViewer {
		struct State {
			Vector3i local_position_voxels;
			Box3i data_box; // In chunk coordinates
			Box3i mesh_box;
			int view_distance_voxels = 0;
			bool requires_collisions = false;
			bool requires_meshes = false;
		};
		ViewerID id;
		State state;
		State prev_state;
	};

	std::vector<PairedViewer> _paired_viewers;

	// Voxel storage. Using a shared_ptr so threaded tasks can use it safely.
	std::shared_ptr<VoxelData> _data;

	// Mesh storage
	VoxelMeshMap<VoxelChunkMeshVT> _mesh_map;
	uint32_t _chunk_mesh_size_po2 = constants::DEFAULT_CHUNK_SIZE_PO2;

	unsigned int _max_view_distance_voxels = 128;

	// TODO Terrains only need to handle the visible portion of voxels, which reduces the bounds chunks to handle.
	// Therefore, could a simple grid be better to use than a hashmap?

	struct LoadingChunk {
		RefCount viewers;
		// TODO Optimize allocations here
		std::vector<ViewerID> viewers_to_notify;
	};

	// Chunks currently being loaded.
	std::unordered_map<Vector3i, LoadingChunk> _loading_chunks;
	// Chunks that should be loaded on the next process call.
	// The order in that list does not matter.
	std::vector<Vector3i> _chunks_pending_load;
	// Chunk meshes that should be updated on the next process call.
	// The order in that list does not matter.
	std::vector<Vector3i> _chunks_pending_update;
	// Chunks that should be saved on the next process call.
	// The order in that list does not matter.
	std::vector<VoxelData::ChunkToSave> _chunks_to_save;

	Ref<VoxelMesher> _mesher;

	// Data stored with a shared pointer so it can be sent to asynchronous tasks, and these tasks can be cancelled by
	// setting a bool to false and re-instantiating the structure
	std::shared_ptr<StreamingDependency> _streaming_dependency;
	std::shared_ptr<MeshingDependency> _meshing_dependency;

	bool _generate_collisions = true;
	unsigned int _collision_layer = 1;
	unsigned int _collision_mask = 1;
	float _collision_margin = constants::DEFAULT_COLLISION_MARGIN;
	bool _run_stream_in_editor = true;
	// bool _stream_enabled = false;
	bool _chunk_enter_notification_enabled = false;
	bool _area_edit_notification_enabled = false;
	// If enabled, VoxelViewers will cause chunks to automatically load around them.
	bool _automatic_loading_enabled = true;
	bool _generator_use_gpu = false;

	Ref<Material> _material_override;

	GodotObjectUniquePtr<VoxelChunkEnterInfo> _chunk_enter_info_obj;

	// References to external nodes.
	VoxelInstancer *_instancer = nullptr;
	VoxelTerrainMultiplayerSynchronizer *_multiplayer_synchronizer = nullptr;

	Stats _stats;
};

} // namespace voxel
} // namespace zylann

#endif // VOXEL_TERRAIN_H
