#ifndef VOXEL_MESH_MAP_H
#define VOXEL_MESH_MAP_H

#include "../engine/voxel_engine.h"
#include "../util/macros.h"

#include <unordered_map>
#include <vector>

namespace zylann::voxel {

// Stores meshes and colliders in an infinite sparse grid of chunks (aka chunks).
template <typename ChunkMesh_T>
class VoxelMeshMap {
public:
	VoxelMeshMap() : _last_accessed_chunk(nullptr) {}

	~VoxelMeshMap() {
		clear();
	}

	struct NoAction {
		inline void operator()(ChunkMesh_T &chunk) {}
	};

	template <typename Action_T>
	void remove_chunk(Vector3i bpos, Action_T pre_delete) {
		if (_last_accessed_chunk && _last_accessed_chunk->position == bpos) {
			_last_accessed_chunk = nullptr;
		}
		auto it = _chunks_map.find(bpos);
		if (it != _chunks_map.end()) {
			const unsigned int i = it->second.index;
#ifdef DEBUG_ENABLED
			CRASH_COND(i >= _chunks.size());
#endif
			ChunkMesh_T *chunk = _chunks[i];
			ERR_FAIL_COND(chunk == nullptr);
			pre_delete(*chunk);
			queue_free_chunk_mesh(chunk);
			remove_chunk_internal(it, i);
		}
	}

	ChunkMesh_T *get_chunk(Vector3i bpos) {
		if (_last_accessed_chunk && _last_accessed_chunk->position == bpos) {
			return _last_accessed_chunk;
		}
		auto it = _chunks_map.find(bpos);
		if (it != _chunks_map.end()) {
#ifdef DEBUG_ENABLED
			const unsigned int i = it->second.index;
			CRASH_COND(i >= _chunks.size());
			ChunkMesh_T *chunk = _chunks[i];
			CRASH_COND(chunk == nullptr); // The map should not contain null chunks
			CRASH_COND(it->second.chunk == nullptr);
#endif
			_last_accessed_chunk = it->second.chunk;
			return _last_accessed_chunk;
		}
		return nullptr;
	}

	const ChunkMesh_T *get_chunk(Vector3i bpos) const {
		if (_last_accessed_chunk != nullptr && _last_accessed_chunk->position == bpos) {
			return _last_accessed_chunk;
		}
		auto it = _chunks_map.find(bpos);
		if (it != _chunks_map.end()) {
#ifdef DEBUG_ENABLED
			const unsigned int i = it->second.index;
			CRASH_COND(i >= _chunks.size());
			ChunkMesh_T *chunk = _chunks[i];
			CRASH_COND(chunk == nullptr); // The map should not contain null chunks
			CRASH_COND(it->second.chunk == nullptr);
#endif
			// This function can't cache _last_accessed_chunk, because it's const, so repeated accesses are hashing
			// again...
			return it->second.chunk;
		}
		return nullptr;
	}

	void set_chunk(Vector3i bpos, ChunkMesh_T *chunk) {
		ERR_FAIL_COND(chunk == nullptr);
		CRASH_COND(bpos != chunk->position);
		if (_last_accessed_chunk == nullptr || _last_accessed_chunk->position == bpos) {
			_last_accessed_chunk = chunk;
		}
#ifdef DEBUG_ENABLED
		CRASH_COND(has_chunk(bpos));
#endif
		unsigned int i = _chunks.size();
		_chunks.push_back(chunk);
		_chunks_map.insert({ bpos, { chunk, i } });
	}

	bool has_chunk(Vector3i pos) const {
		//(_last_accessed_chunk != nullptr && _last_accessed_chunk->pos == pos) ||
		return _chunks_map.find(pos) != _chunks_map.end();
	}

	void clear() {
		for (auto it = _chunks.begin(); it != _chunks.end(); ++it) {
			ChunkMesh_T *chunk = *it;
			if (chunk == nullptr) {
				ERR_PRINT("Unexpected nullptr in VoxelMap::clear()");
			} else {
				memdelete(chunk);
			}
		}
		_chunks.clear();
		_chunks_map.clear();
		_last_accessed_chunk = nullptr;
	}

	unsigned int get_chunk_count() const {
#ifdef DEBUG_ENABLED
		const unsigned int chunks_map_size = _chunks_map.size();
		CRASH_COND(_chunks.size() != chunks_map_size);
#endif
		return _chunks.size();
	}

	template <typename Op_T>
	inline void for_each_chunk(Op_T op) {
		for (auto it = _chunks.begin(); it != _chunks.end(); ++it) {
			ChunkMesh_T *chunk = *it;
#ifdef DEBUG_ENABLED
			CRASH_COND(chunk == nullptr);
#endif
			op(*chunk);
		}
	}

	template <typename Op_T>
	inline void for_each_chunk(Op_T op) const {
		for (auto it = _chunks.begin(); it != _chunks.end(); ++it) {
			const ChunkMesh_T *chunk = *it;
#ifdef DEBUG_ENABLED
			CRASH_COND(chunk == nullptr);
#endif
			op(*chunk);
		}
	}

private:
	struct MapItem {
		ChunkMesh_T *chunk;
		// Index of the chunk within the vector storage
		unsigned int index;
	};

	void remove_chunk_internal(typename std::unordered_map<Vector3i, MapItem>::iterator rm_it, unsigned int index) {
		// TODO `erase` can occasionally be very slow (milliseconds) if the map contains lots of items.
		// This might be caused by internal rehashing/resizing.
		// We should look for a faster container, or reduce the number of entries.

		// This function assumes the chunk is already freed
		_chunks_map.erase(rm_it);

		ChunkMesh_T *moved_chunk = _chunks.back();
#ifdef DEBUG_ENABLED
		CRASH_COND(index >= _chunks.size());
#endif
		_chunks[index] = moved_chunk;
		_chunks.pop_back();

		if (index < _chunks.size()) {
			auto moved_chunk_index_it = _chunks_map.find(moved_chunk->position);
			CRASH_COND(moved_chunk_index_it == _chunks_map.end());
			moved_chunk_index_it->second.index = index;
		}
	}

	static void queue_free_chunk_mesh(ChunkMesh_T *chunk) {
		// We spread this out because of physics
		// TODO Could it be enough to do both render and physic deallocation with the task in ~ChunkMesh_T()?
		struct FreeChunkMeshTask : public zylann::ITimeSpreadTask {
			void run(TimeSpreadTaskContext &ctx) override {
				memdelete(chunk);
			}
			ChunkMesh_T *chunk = nullptr;
		};
		ERR_FAIL_COND(chunk == nullptr);
		FreeChunkMeshTask *task = memnew(FreeChunkMeshTask);
		task->chunk = chunk;
		VoxelEngine::get_singleton().push_main_thread_time_spread_task(task);
	}

private:
	// Chunks stored with a spatial hash in all 3D directions.
	std::unordered_map<Vector3i, MapItem> _chunks_map;
	// Chunks are stored in a vector to allow faster iteration over all of them.
	// Use cases for this include updating the transform of the meshes
	std::vector<ChunkMesh_T *> _chunks;

	// Voxel access will most frequently be in contiguous areas, so the same chunks are accessed.
	// To prevent too much hashing, this reference is checked before.
	mutable ChunkMesh_T *_last_accessed_chunk;
};

} // namespace zylann::voxel

#endif // VOXEL_CHUNK_MESH_MAP_H
