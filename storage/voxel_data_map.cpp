#include "voxel_data_map.h"
#include "../constants/cube_tables.h"
#include "../generators/voxel_generator.h"
#include "../util/macros.h"
#include "../util/memory.h"
#include "../util/string_funcs.h"

#include <limits>

namespace zylann::voxel {

VoxelDataMap::VoxelDataMap() {
	// This is not planned to change at runtime at the moment.
	// set_chunk_size_pow2(constants::DEFAULT_CHUNK_SIZE_PO2);
}

VoxelDataMap::~VoxelDataMap() {
	clear();
}

void VoxelDataMap::create(unsigned int lod_index) {
	ZN_ASSERT(lod_index < constants::MAX_LOD);
	clear();
	// set_chunk_size_pow2(chunk_size_po2);
	set_lod_index(lod_index);
}

// void VoxelDataMap::set_chunk_size_pow2(unsigned int p) {
// 	ZN_ASSERT_RETURN_MSG(p >= 1, "Block size is too small");
// 	ZN_ASSERT_RETURN_MSG(p <= 8, "Block size is too big");

// 	_chunk_size_pow2 = p;
// 	_chunk_size = 1 << _chunk_size_pow2;
// 	_chunk_size_mask = _chunk_size - 1;
// }

void VoxelDataMap::set_lod_index(int lod_index) {
	ZN_ASSERT_RETURN_MSG(lod_index >= 0, "LOD index can't be negative");
	ZN_ASSERT_RETURN_MSG(lod_index < 32, "LOD index is too big");

	_lod_index = lod_index;
}

unsigned int VoxelDataMap::get_lod_index() const {
	return _lod_index;
}

int VoxelDataMap::get_voxel(Vector3i pos, unsigned int c) const {
	Vector3i bpos = voxel_to_block(pos);
	const VoxelChunkData *block = get_block(bpos);
	if (block == nullptr || !block->has_voxels()) {
		return VoxelBufferInternal::get_default_value_static(c);
	}
	return block->get_voxels_const().get_voxel(to_local(pos), c);
}

VoxelChunkData *VoxelDataMap::create_default_chunk(Vector3i bpos) {
	std::shared_ptr<VoxelBufferInternal> buffer = make_shared_instance<VoxelBufferInternal>();
	buffer->create(get_chunk_size(), get_chunk_size(), get_chunk_size());
	// buffer->set_default_values(_default_voxel);
#ifdef DEBUG_ENABLED
	ZN_ASSERT_RETURN_V(!has_block(bpos), nullptr);
#endif
	VoxelChunkData &map_block = _blocks_map[bpos];
	map_block = VoxelChunkData(buffer, _lod_index);
	return &map_block;
}

VoxelChunkData *VoxelDataMap::get_or_create_chunk_at_voxel_pos(Vector3i pos) {
	Vector3i bpos = voxel_to_block(pos);
	VoxelChunkData *block = get_block(bpos);
	if (block == nullptr) {
		block = create_default_chunk(bpos);
	}
	return block;
}

void VoxelDataMap::set_voxel(int value, Vector3i pos, unsigned int c) {
	VoxelChunkData *block = get_or_create_chunk_at_voxel_pos(pos);
	// TODO If it turns out to be a problem, use CoW
	VoxelBufferInternal &voxels = block->get_voxels();
	voxels.set_voxel(value, to_local(pos), c);
}

float VoxelDataMap::get_voxel_f(Vector3i pos, unsigned int c) const {
	Vector3i bpos = voxel_to_block(pos);
	const VoxelChunkData *block = get_block(bpos);
	// TODO The generator needs to be invoked if the chunk has no voxels
	if (block == nullptr || !block->has_voxels()) {
		// TODO Not valid for a float return value
		return VoxelBufferInternal::get_default_value_static(c);
	}
	Vector3i lpos = to_local(pos);
	return block->get_voxels_const().get_voxel_f(lpos.x, lpos.y, lpos.z, c);
}

void VoxelDataMap::set_voxel_f(real_t value, Vector3i pos, unsigned int c) {
	VoxelChunkData *block = get_or_create_chunk_at_voxel_pos(pos);
	Vector3i lpos = to_local(pos);
	// TODO In this situation, the generator must be invoked to fill the chunk
	ZN_ASSERT_RETURN_MSG(block->has_voxels(), "Block not cached");
	VoxelBufferInternal &voxels = block->get_voxels();
	voxels.set_voxel_f(value, lpos.x, lpos.y, lpos.z, c);
}

VoxelChunkData *VoxelDataMap::get_block(Vector3i bpos) {
	auto it = _blocks_map.find(bpos);
	if (it != _blocks_map.end()) {
		return &it->second;
	}
	return nullptr;
}

const VoxelChunkData *VoxelDataMap::get_block(Vector3i bpos) const {
	auto it = _blocks_map.find(bpos);
	if (it != _blocks_map.end()) {
		return &it->second;
	}
	return nullptr;
}

VoxelChunkData *VoxelDataMap::set_block_buffer(
		Vector3i bpos, std::shared_ptr<VoxelBufferInternal> &buffer, bool overwrite) {
	ZN_ASSERT_RETURN_V(buffer != nullptr, nullptr);

	VoxelChunkData *block = get_block(bpos);

	if (block == nullptr) {
		VoxelChunkData &map_block = _blocks_map[bpos];
		map_block = VoxelChunkData(buffer, _lod_index);
		block = &map_block;

	} else if (overwrite) {
		block->set_voxels(buffer);

	} else {
		ZN_PROFILE_MESSAGE("Redundant data block");
		ZN_PRINT_VERBOSE(format(
				"Discarded block {} lod {}, there was already data and overwriting is not enabled", bpos, _lod_index));
	}

	return block;
}

void VoxelDataMap::set_block(Vector3i bpos, const VoxelChunkData &block) {
#ifdef DEBUG_ENABLED
	ZN_ASSERT(block.get_lod_index() == _lod_index);
#endif
	_blocks_map[bpos] = block;
}

VoxelChunkData *VoxelDataMap::set_empty_block(Vector3i bpos, bool overwrite) {
	VoxelChunkData *block = get_block(bpos);

	if (block == nullptr) {
		VoxelChunkData &map_block = _blocks_map[bpos];
		map_block = VoxelChunkData(_lod_index);
		block = &map_block;

	} else if (overwrite) {
		block->clear_voxels();

	} else {
		ZN_PROFILE_MESSAGE("Redundant data block");
		ZN_PRINT_VERBOSE(format(
				"Discarded block {} lod {}, there was already data and overwriting is not enabled", bpos, _lod_index));
	}

	return block;
}

bool VoxelDataMap::has_block(Vector3i pos) const {
	return _blocks_map.find(pos) != _blocks_map.end();
}

bool VoxelDataMap::is_block_surrounded(Vector3i pos) const {
	// TODO If that check proves to be too expensive with all chunks we deal with, cache it in VoxelBlocks
	for (unsigned int i = 0; i < Cube::MOORE_NEIGHBORING_3D_COUNT; ++i) {
		Vector3i bpos = pos + Cube::g_moore_neighboring_3d[i];
		if (!has_block(bpos)) {
			return false;
		}
	}
	return true;
}

void VoxelDataMap::copy(Vector3i min_pos, VoxelBufferInternal &dst_buffer, unsigned int channels_mask,
		void *callback_data, void (*gen_func)(void *, VoxelBufferInternal &, Vector3i)) const {
	ZN_ASSERT_RETURN_MSG(Vector3iUtil::get_volume(dst_buffer.get_size()) > 0, "The area to copy is empty");
	const Vector3i max_pos = min_pos + dst_buffer.get_size();

	const Vector3i min_block_pos = voxel_to_block(min_pos);
	const Vector3i max_block_pos = voxel_to_block(max_pos - Vector3i(1, 1, 1)) + Vector3i(1, 1, 1);

	const Vector3i chunk_size_v(get_chunk_size(), get_chunk_size(), get_chunk_size());

	unsigned int channels_count;
	FixedArray<uint8_t, VoxelBufferInternal::MAX_CHANNELS> channels =
			VoxelBufferInternal::mask_to_channels_list(channels_mask, channels_count);

	Vector3i bpos;
	for (bpos.z = min_block_pos.z; bpos.z < max_block_pos.z; ++bpos.z) {
		for (bpos.x = min_block_pos.x; bpos.x < max_block_pos.x; ++bpos.x) {
			for (bpos.y = min_block_pos.y; bpos.y < max_block_pos.y; ++bpos.y) {
				const VoxelChunkData *block = get_block(bpos);
				const Vector3i src_chunk_origin = chunk_to_voxel(bpos);

				if (block != nullptr && block->has_voxels()) {
					const VoxelBufferInternal &src_buffer = block->get_voxels_const();

					for (unsigned int ci = 0; ci < channels_count; ++ci) {
						const uint8_t channel = channels[ci];
						dst_buffer.set_channel_depth(channel, src_buffer.get_channel_depth(channel));
						// Note: copy_from takes care of clamping the area if it's on an edge
						dst_buffer.copy_from(
								src_buffer, min_pos - src_chunk_origin, src_buffer.get_size(), Vector3i(), channel);
					}

				} else if (gen_func != nullptr) {
					const Box3i box = Box3i(bpos << get_chunk_size_pow2(), chunk_size_v)
											  .clipped(Box3i(min_pos, dst_buffer.get_size()));

					// TODO Format?
					VoxelBufferInternal temp;
					temp.create(box.size);
					gen_func(callback_data, temp, box.pos);

					for (unsigned int ci = 0; ci < channels_count; ++ci) {
						dst_buffer.copy_from(temp, Vector3i(), temp.get_size(), box.pos - min_pos, channels[ci]);
					}

				} else {
					for (unsigned int ci = 0; ci < channels_count; ++ci) {
						const uint8_t channel = channels[ci];
						// For now, inexistent chunks default to hardcoded defaults, corresponding to "empty space".
						// If we want to change this, we may have to add an API for that.
						dst_buffer.fill_area(VoxelBufferInternal::get_default_value_static(channel),
								src_chunk_origin - min_pos, src_chunk_origin - min_pos + chunk_size_v, channel);
					}
				}
			}
		}
	}
}

void VoxelDataMap::paste(Vector3i min_pos, const VoxelBufferInternal &src_buffer, unsigned int channels_mask,
		bool use_mask, uint8_t mask_channel, uint64_t mask_value, bool create_new_chunks) {
	//
	const Vector3i max_pos = min_pos + src_buffer.get_size();

	const Vector3i min_block_pos = voxel_to_block(min_pos);
	const Vector3i max_block_pos = voxel_to_block(max_pos - Vector3i(1, 1, 1)) + Vector3i(1, 1, 1);

	Vector3i bpos;
	for (bpos.z = min_block_pos.z; bpos.z < max_block_pos.z; ++bpos.z) {
		for (bpos.x = min_block_pos.x; bpos.x < max_block_pos.x; ++bpos.x) {
			for (bpos.y = min_block_pos.y; bpos.y < max_block_pos.y; ++bpos.y) {
				for (unsigned int channel = 0; channel < VoxelBufferInternal::MAX_CHANNELS; ++channel) {
					if (((1 << channel) & channels_mask) == 0) {
						continue;
					}
					VoxelChunkData *block = get_block(bpos);

					if (block == nullptr) {
						if (create_new_chunks) {
							block = create_default_chunk(bpos);
						} else {
							continue;
						}
					}

					// TODO In this situation, the generator has to be invoked to fill the blanks
					ZN_ASSERT_CONTINUE_MSG(block->has_voxels(), "Area not cached");

					const Vector3i dst_chunk_origin = chunk_to_voxel(bpos);

					VoxelBufferInternal &dst_buffer = block->get_voxels();

					if (use_mask) {
						const Box3i dst_box(min_pos - dst_chunk_origin, src_buffer.get_size());

						const Vector3i src_offset = -dst_box.pos;

						if (channel == mask_channel) {
							dst_buffer.read_write_action(dst_box, channel,
									[&src_buffer, mask_value, src_offset, channel](const Vector3i pos, uint64_t dst_v) {
										const uint64_t src_v = src_buffer.get_voxel(pos + src_offset, channel);
										if (src_v == mask_value) {
											return dst_v;
										}
										return src_v;
									});
						} else {
							dst_buffer.read_write_action(dst_box, channel,
									[&src_buffer, mask_value, src_offset, channel, mask_channel](
											const Vector3i pos, uint64_t dst_v) {
										const uint64_t mv = src_buffer.get_voxel(pos + src_offset, mask_channel);
										if (mv == mask_value) {
											return dst_v;
										}
										const uint64_t src_v = src_buffer.get_voxel(pos + src_offset, channel);
										return src_v;
									});
						}

					} else {
						dst_buffer.copy_from(
								src_buffer, Vector3i(), src_buffer.get_size(), min_pos - dst_chunk_origin, channel);
					}
				}
			}
		}
	}
}

void VoxelDataMap::clear() {
	_blocks_map.clear();
}

int VoxelDataMap::get_block_count() const {
	return _blocks_map.size();
}

bool VoxelDataMap::is_area_fully_loaded(const Box3i voxels_box) const {
	Box3i block_box = voxels_box.downscaled(get_chunk_size());
	return block_box.all_cells_match([this](Vector3i pos) { //
		return has_block(pos);
	});
}

} // namespace zylann::voxel
