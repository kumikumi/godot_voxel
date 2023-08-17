#ifndef VOXEL_DATA_GRID_H
#define VOXEL_DATA_GRID_H

#include "voxel_data_map.h"
#include "voxel_spatial_lock.h"

namespace zylann::voxel {

// Stores chunks of voxel data in a finite grid.
// This is used as temporary storage for some operations, to avoid holding exclusive locks on maps for too long.
// TODO Have a readonly version to enforce no writes?
class VoxelDataGrid {
public:
	// Rebuilds the grid and caches chunks intersecting the specified voxel box.
	// WARNING: the given box is in voxels RELATIVE to the passed map. It that map is not LOD0, you may downscale the
	// box if you expect LOD0 coordinates.
	inline void reference_area(const VoxelDataMap &map, Box3i voxel_box, VoxelSpatialLock *sl) {
		const Box3i chunks_box = voxel_box.downscaled(map.get_chunk_size());
		reference_area_chunk_coords(map, chunks_box, sl);
	}

	inline void reference_area_chunk_coords(const VoxelDataMap &map, Box3i chunks_box, VoxelSpatialLock *sl) {
		ZN_PROFILE_SCOPE();
		create(chunks_box.size, map.get_chunk_size());
		_offset_in_chunks = chunks_box.pos;
		if (sl != nullptr) {
			sl->lock_read(chunks_box);
		}
		chunks_box.for_each_cell_zxy([&map, this](const Vector3i pos) {
			const VoxelChunkData *block = map.get_chunk(pos);
			// TODO Might need to invoke the generator at some level for present chunks without voxels,
			// or make sure all chunks contain voxel data
			if (block != nullptr && block->has_voxels()) {
				set_chunk(pos, block->get_voxels_shared());
			} else {
				set_chunk(pos, nullptr);
			}
		});
		if (sl != nullptr) {
			sl->unlock_read(chunks_box);
		}
		_spatial_lock = sl;
	}

	inline bool has_any_chunk() const {
		for (unsigned int i = 0; i < _chunks.size(); ++i) {
			if (_chunks[i] != nullptr) {
				return true;
			}
		}
		return false;
	}

	// The grid must be locked before doing operations on it.

	struct LockRead {
		LockRead(const VoxelDataGrid &p_grid) : grid(p_grid) {
			grid.lock_read();
		}
		~LockRead() {
			grid.unlock_read();
		}
		const VoxelDataGrid &grid;
	};

	struct LockWrite {
		LockWrite(VoxelDataGrid &p_grid) : grid(p_grid) {
			grid.lock_write();
		}
		~LockWrite() {
			grid.unlock_write();
		}
		VoxelDataGrid &grid;
	};

	inline void lock_read() const {
		ZN_ASSERT(_spatial_lock != nullptr);
		ZN_ASSERT(!_locked);
		_spatial_lock->lock_read(BoxBounds3i::from_position_size(_offset_in_chunks, _size_in_chunks));
		_locked = true;
	}

	inline void unlock_read() const {
		ZN_ASSERT(_spatial_lock != nullptr);
		ZN_ASSERT(_locked);
		_spatial_lock->unlock_read(BoxBounds3i::from_position_size(_offset_in_chunks, _size_in_chunks));
		_locked = false;
	}

	inline void lock_write() {
		ZN_ASSERT(_spatial_lock != nullptr);
		ZN_ASSERT(!_locked);
		_spatial_lock->lock_write(BoxBounds3i::from_position_size(_offset_in_chunks, _size_in_chunks));
		_locked = true;
	}

	inline void unlock_write() {
		ZN_ASSERT(_spatial_lock != nullptr);
		ZN_ASSERT(_locked);
		_spatial_lock->unlock_write(BoxBounds3i::from_position_size(_offset_in_chunks, _size_in_chunks));
		_locked = false;
	}

	inline bool try_get_voxel_f(Vector3i pos, float &out_value, VoxelBufferInternal::ChannelId channel) const {
#ifdef DEBUG_ENABLED
		ZN_ASSERT(_locked);
#endif
		const Vector3i bpos = (pos >> _chunk_size_po2) - _offset_in_chunks;
		if (!is_valid_relative_chunk_position(bpos)) {
			return false;
		}
		const unsigned int loc = Vector3iUtil::get_zxy_index(bpos, _size_in_chunks);
		const VoxelBufferInternal *voxels = _chunks[loc].get();
		if (voxels == nullptr) {
			return false;
		}
		const unsigned int mask = (1 << _chunk_size_po2) - 1;
		const Vector3i rpos = pos & mask;
		out_value = voxels->get_voxel_f(rpos, channel);
		return true;
	}

	// D action(Vector3i pos, D value)
	template <typename F>
	void write_box(Box3i voxel_box, unsigned int channel, F action) {
		if (_spatial_lock != nullptr) {
			lock_write();
		}
		_box_loop(voxel_box, [action, channel](VoxelBufferInternal &voxels, Box3i local_box, Vector3i voxel_offset) {
			voxels.write_box(local_box, channel, action, voxel_offset);
		});
		if (_spatial_lock != nullptr) {
			unlock_write();
		}
	}

	// void action(Vector3i pos, D0 &value, D1 &value)
	template <typename F>
	void write_box_2(const Box3i &voxel_box, unsigned int channel0, unsigned int channel1, F action) {
		if (_spatial_lock != nullptr) {
			lock_write();
		}
		_box_loop(voxel_box,
				[action, channel0, channel1](VoxelBufferInternal &voxels, Box3i local_box, Vector3i voxel_offset) {
					voxels.write_box_2_template<F, uint16_t, uint16_t>(
							local_box, channel0, channel1, action, voxel_offset);
				});
		if (_spatial_lock != nullptr) {
			unlock_write();
		}
	}

	// inline const VoxelBufferInternal *get_chunk(Vector3i position) const {
	// 	ERR_FAIL_COND_V(!is_valid_position(position), nullptr);
	// 	position -= _offset_in_chunks;
	// 	const unsigned int index = Vector3iUtil::get_zxy_index(position, _size_in_chunks);
	// 	CRASH_COND(index >= _chunks.size());
	// 	return _chunks[index].get();
	// }

	inline void clear() {
		ZN_ASSERT(!_locked);
		_chunks.clear();
		_size_in_chunks = Vector3i();
		_spatial_lock = nullptr;
	}

private:
	inline unsigned int get_chunk_size() const {
		return _chunk_size;
	}

	template <typename Chunk_F>
	inline void _box_loop(Box3i voxel_box, Chunk_F chunk_action) {
		Vector3i chunk_rpos;
		const Vector3i area_origin_in_voxels = _offset_in_chunks * _chunk_size;
		unsigned int index = 0;
		for (chunk_rpos.z = 0; chunk_rpos.z < _size_in_chunks.z; ++chunk_rpos.z) {
			for (chunk_rpos.x = 0; chunk_rpos.x < _size_in_chunks.x; ++chunk_rpos.x) {
				for (chunk_rpos.y = 0; chunk_rpos.y < _size_in_chunks.y; ++chunk_rpos.y) {
					VoxelBufferInternal *chunk = _chunks[index].get();
					// Flat grid and iteration order allows us to just increment the index since we iterate them all
					++index;
					if (chunk == nullptr) {
						continue;
					}
					const Vector3i chunk_origin = chunk_rpos * _chunk_size + area_origin_in_voxels;
					Box3i local_box(voxel_box.pos - chunk_origin, voxel_box.size);
					local_box.clip(Box3i(Vector3i(), Vector3iUtil::create(_chunk_size)));
					chunk_action(*chunk, local_box, chunk_origin);
				}
			}
		}
	}

	inline void create(Vector3i size, unsigned int chunk_size) {
		ZN_PROFILE_SCOPE();
		_chunks.clear();
		_chunks.resize(Vector3iUtil::get_volume(size));
		_size_in_chunks = size;
		_chunk_size = chunk_size;
	}

	inline bool is_valid_relative_chunk_position(Vector3i pos) const {
		return pos.x >= 0 && //
				pos.y >= 0 && //
				pos.z >= 0 && //
				pos.x < _size_in_chunks.x && //
				pos.y < _size_in_chunks.y && //
				pos.z < _size_in_chunks.z;
	}

	inline bool is_valid_chunk_position(Vector3i pos) const {
		return is_valid_relative_chunk_position(pos - _offset_in_chunks);
	}

	inline void set_chunk(Vector3i position, std::shared_ptr<VoxelBufferInternal> block) {
		ZN_ASSERT_RETURN(is_valid_chunk_position(position));
		position -= _offset_in_chunks;
		const unsigned int index = Vector3iUtil::get_zxy_index(position, _size_in_chunks);
		ZN_ASSERT(index < _chunks.size());
		_chunks[index] = block;
	}

	inline VoxelBufferInternal *get_chunk(Vector3i position) {
		ZN_ASSERT_RETURN_V(is_valid_chunk_position(position), nullptr);
		position -= _offset_in_chunks;
		const unsigned int index = Vector3iUtil::get_zxy_index(position, _size_in_chunks);
		ZN_ASSERT(index < _chunks.size());
		return _chunks[index].get();
	}

	// Flat grid indexed in ZXY order
	// TODO Ability to use thread-local/stack pool allocator? Such grids are often temporary
	std::vector<std::shared_ptr<VoxelBufferInternal>> _chunks;
	// Size of the grid in chunks
	Vector3i _size_in_chunks;
	// Block coordinates offset. This is used for when we cache a sub-region of a map, we need to keep the origin
	// of the area in memory so we can keep using the same coordinate space
	Vector3i _offset_in_chunks;
	// Size of a chunk in voxels
	unsigned int _chunk_size_po2 = constants::DEFAULT_CHUNK_SIZE_PO2;
	unsigned int _chunk_size = 1 << constants::DEFAULT_CHUNK_SIZE_PO2;
	// For protecting voxel data against multithreaded accesses. Not owned. Lifetime must be guaranteed by the user, for
	// example by having a std::shared_ptr<VoxelData> holding the spatial lock.
	VoxelSpatialLock *_spatial_lock = nullptr;
	mutable bool _locked = false;
};

} // namespace zylann::voxel

#endif // VOXEL_DATA_GRID_H
