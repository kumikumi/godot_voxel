#include "test_region_file.h"
#include "../streams/region/region_file.h"
#include "../streams/region/voxel_stream_region_files.h"
#include "../util/godot/core/random_pcg.h"
#include "testing.h"

namespace zylann::voxel::tests {

void test_region_file() {
	const int chunk_size_po2 = 4;
	const int chunk_size = 1 << chunk_size_po2;
	const char *region_file_name = "test_region_file.vxr";
	zylann::testing::TestDirectory test_dir;
	ZN_TEST_ASSERT(test_dir.is_valid());
	String region_file_path = test_dir.get_path().path_join(region_file_name);

	struct RandomChunkGenerator {
		RandomPCG rng;

		void generate(VoxelBufferInternal &buffer) {
			buffer.create(Vector3iUtil::create(chunk_size));
			buffer.set_channel_depth(0, VoxelBufferInternal::DEPTH_16_BIT);

			// Make a chunk with enough data to take some significant space even if compressed
			for (int z = 0; z < buffer.get_size().z; ++z) {
				for (int x = 0; x < buffer.get_size().x; ++x) {
					for (int y = 0; y < buffer.get_size().y; ++y) {
						buffer.set_voxel(rng.rand() % 256, x, y, z, 0);
					}
				}
			}
		}
	};

	RandomChunkGenerator generator;

	// Create a chunk of voxels
	VoxelBufferInternal voxel_buffer;
	generator.generate(voxel_buffer);

	{
		RegionFile region_file;

		// Configure region format
		RegionFormat region_format = region_file.get_format();
		region_format.chunk_size_po2 = chunk_size_po2;
		for (unsigned int channel_index = 0; channel_index < VoxelBufferInternal::MAX_CHANNELS; ++channel_index) {
			region_format.channel_depths[channel_index] = voxel_buffer.get_channel_depth(channel_index);
		}
		ZN_TEST_ASSERT(region_file.set_format(region_format));

		// Open file
		const Error open_error = region_file.open(region_file_path, true);
		ZN_TEST_ASSERT(open_error == OK);

		// Save chunk
		const Error save_error = region_file.save_chunk(Vector3i(1, 2, 3), voxel_buffer);
		ZN_TEST_ASSERT(save_error == OK);

		// Read back
		VoxelBufferInternal loaded_voxel_buffer;
		const Error load_error = region_file.load_chunk(Vector3i(1, 2, 3), loaded_voxel_buffer);
		ZN_TEST_ASSERT(load_error == OK);

		// Must be equal
		ZN_TEST_ASSERT(voxel_buffer.equals(loaded_voxel_buffer));
	}
	// Load again but using a new region file object
	{
		RegionFile region_file;

		// Open file
		const Error open_error = region_file.open(region_file_path, false);
		ZN_TEST_ASSERT(open_error == OK);

		// Read back
		VoxelBufferInternal loaded_voxel_buffer;
		const Error load_error = region_file.load_chunk(Vector3i(1, 2, 3), loaded_voxel_buffer);
		ZN_TEST_ASSERT(load_error == OK);

		// Must be equal
		ZN_TEST_ASSERT(voxel_buffer.equals(loaded_voxel_buffer));
	}
	// Save many chunks
	{
		RegionFile region_file;

		// Open file
		const Error open_error = region_file.open(region_file_path, false);
		ZN_TEST_ASSERT(open_error == OK);

		RandomPCG rng;

		std::unordered_map<Vector3i, VoxelBufferInternal> buffers;
		const Vector3i region_size = region_file.get_format().region_size;

		for (int i = 0; i < 1000; ++i) {
			const Vector3i pos = Vector3i( //
					rng.rand() % uint32_t(region_size.x), //
					rng.rand() % uint32_t(region_size.y), //
					rng.rand() % uint32_t(region_size.z) //
			);
			generator.generate(voxel_buffer);

			// Save chunk
			const Error save_error = region_file.save_chunk(pos, voxel_buffer);
			ZN_TEST_ASSERT(save_error == OK);

			// Note, the same position can occur twice, we just overwrite
			buffers[pos] = std::move(voxel_buffer);
		}

		// Read back
		for (auto it = buffers.begin(); it != buffers.end(); ++it) {
			VoxelBufferInternal loaded_voxel_buffer;
			const Error load_error = region_file.load_chunk(it->first, loaded_voxel_buffer);
			ZN_TEST_ASSERT(load_error == OK);
			ZN_TEST_ASSERT(it->second.equals(loaded_voxel_buffer));
		}

		const Error close_error = region_file.close();
		ZN_TEST_ASSERT(close_error == OK);

		// Open file
		const Error open_error2 = region_file.open(region_file_path, false);
		ZN_TEST_ASSERT(open_error2 == OK);

		// Read back again
		for (auto it = buffers.begin(); it != buffers.end(); ++it) {
			VoxelBufferInternal loaded_voxel_buffer;
			const Error load_error = region_file.load_chunk(it->first, loaded_voxel_buffer);
			ZN_TEST_ASSERT(load_error == OK);
			ZN_TEST_ASSERT(it->second.equals(loaded_voxel_buffer));
		}
	}
}

// Test based on an issue from `I am the Carl` on Discord. It should only not crash or cause errors.
void test_voxel_stream_region_files() {
	const int chunk_size_po2 = 4;
	const int chunk_size = 1 << chunk_size_po2;

	zylann::testing::TestDirectory test_dir;
	ZN_TEST_ASSERT(test_dir.is_valid());

	Ref<VoxelStreamRegionFiles> stream;
	stream.instantiate();
	stream->set_chunk_size_po2(chunk_size_po2);
	stream->set_directory(test_dir.get_path());

	RandomPCG rng;

	for (int cycle = 0; cycle < 1000; ++cycle) {
		VoxelBufferInternal buffer;
		buffer.create(chunk_size, chunk_size, chunk_size);

		// Make a chunk with enough data to take some significant space even if compressed
		for (int z = 0; z < buffer.get_size().z; ++z) {
			for (int x = 0; x < buffer.get_size().x; ++x) {
				for (int y = 0; y < buffer.get_size().y; ++y) {
					buffer.set_voxel(rng.rand() % 256, x, y, z, 0);
				}
			}
		}

		// The position isn't a correct use because it's in voxels, not chunks, but it remains a case that should
		// not cause errors or crash. The same chunks will simply get written to several times.
		VoxelStream::VoxelQueryData q{ buffer, Vector3(cycle, 0, 0), 0, VoxelStream::RESULT_ERROR };
		stream->save_voxel_chunk(q);
	}
}

} // namespace zylann::voxel::tests
