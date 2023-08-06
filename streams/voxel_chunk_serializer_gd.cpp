#include "voxel_chunk_serializer_gd.h"
#include "../util/godot/classes/stream_peer.h"
#include "voxel_chunk_serializer.h"

namespace zylann::voxel::gd {

int VoxelChunkSerializer::serialize_to_stream_peer(Ref<StreamPeer> peer, Ref<VoxelBuffer> voxel_buffer, bool compress) {
	ERR_FAIL_COND_V(voxel_buffer.is_null(), 0);
	ERR_FAIL_COND_V(peer.is_null(), 0);

	if (compress) {
		ChunkSerializer::SerializeResult res = ChunkSerializer::serialize_and_compress(voxel_buffer->get_buffer());
		ERR_FAIL_COND_V(!res.success, -1);
		stream_peer_put_data(**peer, to_span(res.data));
		return res.data.size();

	} else {
		ChunkSerializer::SerializeResult res = ChunkSerializer::serialize(voxel_buffer->get_buffer());
		ERR_FAIL_COND_V(!res.success, -1);
		stream_peer_put_data(**peer, to_span(res.data));
		return res.data.size();
	}
}

void VoxelChunkSerializer::deserialize_from_stream_peer(
		Ref<StreamPeer> peer, Ref<VoxelBuffer> voxel_buffer, int size, bool decompress) {
	ERR_FAIL_COND(voxel_buffer.is_null());
	ERR_FAIL_COND(peer.is_null());
	ERR_FAIL_COND(size <= 0);

	if (decompress) {
		std::vector<uint8_t> &compressed_data = ChunkSerializer::get_tls_compressed_data();
		compressed_data.resize(size);
		const Error err = stream_peer_get_data(**peer, to_span(compressed_data));
		ERR_FAIL_COND(err != OK);
		const bool success =
				ChunkSerializer::decompress_and_deserialize(to_span(compressed_data), voxel_buffer->get_buffer());
		ERR_FAIL_COND(!success);

	} else {
		std::vector<uint8_t> &data = ChunkSerializer::get_tls_data();
		data.resize(size);
		const Error err = stream_peer_get_data(**peer, to_span(data));
		ERR_FAIL_COND(err != OK);
		ChunkSerializer::deserialize(to_span(data), voxel_buffer->get_buffer());
	}
}

PackedByteArray VoxelChunkSerializer::serialize_to_byte_array(Ref<VoxelBuffer> voxel_buffer, bool compress) {
	ERR_FAIL_COND_V(voxel_buffer.is_null(), PackedByteArray());

	PackedByteArray bytes;
	if (compress) {
		ChunkSerializer::SerializeResult res = ChunkSerializer::serialize_and_compress(voxel_buffer->get_buffer());
		ERR_FAIL_COND_V(!res.success, PackedByteArray());
		copy_to(bytes, to_span(res.data));

	} else {
		ChunkSerializer::SerializeResult res = ChunkSerializer::serialize(voxel_buffer->get_buffer());
		ERR_FAIL_COND_V(!res.success, PackedByteArray());
		copy_to(bytes, to_span(res.data));
	}
	return bytes;
}

void VoxelChunkSerializer::deserialize_from_byte_array(
		PackedByteArray bytes, Ref<VoxelBuffer> voxel_buffer, bool decompress) {
	ERR_FAIL_COND(voxel_buffer.is_null());
	ERR_FAIL_COND(bytes.size() == 0);

	Span<const uint8_t> bytes_span = Span<const uint8_t>(bytes.ptr(), bytes.size());

	if (decompress) {
		const bool success = ChunkSerializer::decompress_and_deserialize(bytes_span, voxel_buffer->get_buffer());
		ERR_FAIL_COND(!success);

	} else {
		ChunkSerializer::deserialize(bytes_span, voxel_buffer->get_buffer());
	}
}

void VoxelChunkSerializer::_bind_methods() {
	auto cname = VoxelChunkSerializer::get_class_static();

	// Reasons for using methods with StreamPeer:
	// - Convenience, if you do write to a peer already
	// - Avoiding an allocation. When serializing to a PackedByteArray, the Godot API incurs allocating that
	// temporary array every time.
	ClassDB::bind_static_method(cname, D_METHOD("serialize_to_stream_peer", "peer", "voxel_buffer", "compress"),
			&VoxelChunkSerializer::serialize_to_stream_peer);
	ClassDB::bind_static_method(cname,
			D_METHOD("deserialize_from_stream_peer", "peer", "voxel_buffer", "size", "decompress"),
			&VoxelChunkSerializer::deserialize_from_stream_peer);

	ClassDB::bind_static_method(cname, D_METHOD("serialize_to_byte_array", "voxel_buffer", "compress"),
			&VoxelChunkSerializer::serialize_to_byte_array);
	ClassDB::bind_static_method(cname, D_METHOD("deserialize_from_byte_array", "bytes", "voxel_buffer", "decompress"),
			&VoxelChunkSerializer::deserialize_from_byte_array);
}

} // namespace zylann::voxel::gd
