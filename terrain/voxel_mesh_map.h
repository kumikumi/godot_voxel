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
	VoxelMeshMap() : _last_accessed_block(nullptr) {}

	~VoxelMeshMap() {
		clear();
	}

	struct NoAction {
		inline void operator()(ChunkMesh_T &block) {}
	};

	template <typename Action_T>
	void remove_chunk(Vector3i bpos, Action_T pre_delete) {
		if (_last_accessed_block && _last_accessed_block->position == bpos) {
			_last_accessed_block = nullptr;
		}
		auto it = _chunks_map.find(bpos);
		if (it != _chunks_map.end()) {
			const unsigned int i = it->second.index;
#ifdef DEBUG_ENABLED
			CRASH_COND(i >= _chunks.size());
#endif
			ChunkMesh_T *block = _chunks[i];
			ERR_FAIL_COND(block == nullptr);
			pre_delete(*block);
			queue_free_chunk_mesh(block);
			remove_chunk_internal(it, i);
		}
	}

	ChunkMesh_T *get_chunk(Vector3i bpos) {
		if (_last_accessed_block && _last_accessed_block->position == bpos) {
			return _last_accessed_block;
		}
		auto it = _chunks_map.find(bpos);
		if (it != _chunks_map.end()) {
#ifdef DEBUG_ENABLED
			const unsigned int i = it->second.index;
			CRASH_COND(i >= _chunks.size());
			ChunkMesh_T *block = _chunks[i];
			CRASH_COND(block == nullptr); // The map should not contain null chunks
			CRASH_COND(it->second.block == nullptr);
#endif
			_last_accessed_block = it->second.block;
			return _last_accessed_block;
		}
		return nullptr;
	}

	const ChunkMesh_T *get_chunk(Vector3i bpos) const {
		if (_last_accessed_block != nullptr && _last_accessed_block->position == bpos) {
			return _last_accessed_block;
		}
		auto it = _chunks_map.find(bpos);
		if (it != _chunks_map.end()) {
#ifdef DEBUG_ENABLED
			const unsigned int i = it->second.index;
			CRASH_COND(i >= _chunks.size());
			ChunkMesh_T *block = _chunks[i];
			CRASH_COND(block == nullptr); // The map should not contain null chunks
			CRASH_COND(it->second.block == nullptr);
#endif
			// This function can't cache _last_accessed_chunk, because it's const, so repeated accesses are hashing
			// again...
			return it->second.block;
		}
		return nullptr;
	}

	void set_chunk(Vector3i bpos, ChunkMesh_T *block) {
		ERR_FAIL_COND(block == nullptr);
		CRASH_COND(bpos != block->position);
		if (_last_accessed_block == nullptr || _last_accessed_block->position == bpos) {
			_last_accessed_block = block;
		}
#ifdef DEBUG_ENABLED
		CRASH_COND(has_block(bpos));
#endif
		unsigned int i = _chunks.size();
		_chunks.push_back(block);
		_chunks_map.insert({ bpos, { block, i } });
	}

	bool has_block(Vector3i pos) const {
		//(_last_accessed_chunk != nullptr && _last_accessed_chunk->pos == pos) ||
		return _chunks_map.find(pos) != _chunks_map.end();
	}

	void clear() {
		for (auto it = _chunks.begin(); it != _chunks.end(); ++it) {
			ChunkMesh_T *block = *it;
			if (block == nullptr) {
				ERR_PRINT("Unexpected nullptr in VoxelMap::clear()");
			} else {
				memdelete(block);
			}
		}
		_chunks.clear();
		_chunks_map.clear();
		_last_accessed_block = nullptr;
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
			ChunkMesh_T *block = *it;
#ifdef DEBUG_ENABLED
			CRASH_COND(block == nullptr);
#endif
			op(*block);
		}
	}

	template <typename Op_T>
	inline void for_each_chunk(Op_T op) const {
		for (auto it = _chunks.begin(); it != _chunks.end(); ++it) {
			const ChunkMesh_T *block = *it;
#ifdef DEBUG_ENABLED
			CRASH_COND(block == nullptr);
#endif
			op(*block);
		}
	}

private:
	struct MapItem {
		ChunkMesh_T *block;
		// Index of the chunk within the vector storage
		unsigned int index;
	};

	void remove_chunk_internal(typename std::unordered_map<Vector3i, MapItem>::iterator rm_it, unsigned int index) {
		// TODO `erase` can occasionally be very slow (milliseconds) if the map contains lots of items.
		// This might be caused by internal rehashing/resizing.
		// We should look for a faster container, or reduce the number of entries.

		// This function assumes the chunk is already freed
		_chunks_map.erase(rm_it);

		ChunkMesh_T *moved_block = _chunks.back();
#ifdef DEBUG_ENABLED
		CRASH_COND(index >= _chunks.size());
#endif
		_chunks[index] = moved_block;
		_chunks.pop_back();

		if (index < _chunks.size()) {
			auto moved_chunk_index_it = _chunks_map.find(moved_block->position);
			CRASH_COND(moved_chunk_index_it == _chunks_map.end());
			moved_chunk_index_it->second.index = index;
		}
	}

	static void queue_free_chunk_mesh(ChunkMesh_T *block) {
		// We spread this out because of physics
		// TODO Could it be enough to do both render and physic deallocation with the task in ~ChunkMesh_T()?
		struct FreeChunkMeshTask : public zylann::ITimeSpreadTask {
			void run(TimeSpreadTaskContext &ctx) override {
				memdelete(block);
			}
			ChunkMesh_T *block = nullptr;
		};
		ERR_FAIL_COND(block == nullptr);
		FreeChunkMeshTask *task = memnew(FreeChunkMeshTask);
		task->block = block;
		VoxelEngine::get_singleton().push_main_thread_time_spread_task(task);
	}

private:
	// Blocks stored with a spatial hash in all 3D directions.
	std::unordered_map<Vector3i, MapItem> _chunks_map;
	// Blocks are stored in a vector to allow faster iteration over all of them.
	// Use cases for this include updating the transform of the meshes
	std::vector<ChunkMesh_T *> _chunks;

	// Voxel access will most frequently be in contiguous areas, so the same chunks are accessed.
	// To prevent too much hashing, this reference is checked before.
	mutable ChunkMesh_T *_last_accessed_block;
};

} // namespace zylann::voxel

#endif // VOXEL_CHUNK_MESH_MAP_H
