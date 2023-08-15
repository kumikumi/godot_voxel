#ifndef VOXEL_CHUNK_MESH_TASK_H
#define VOXEL_CHUNK_MESH_TASK_H

#include "../constants/voxel_constants.h"
#include "../storage/voxel_buffer_internal.h"
#include "../util/godot/classes/array_mesh.h"
#include "../util/tasks/threaded_task.h"
#include "detail_rendering.h"
#include "generate_chunk_gpu_task.h"
#include "ids.h"
#include "chunk_mesh_task.h"
#include "meshing_dependency.h"
#include "priority_dependency.h"

namespace zylann::voxel {

class VoxelData;

// Asynchronous task generating a mesh from voxel chunks and their neighbors, in a particular volume
class ChunkMeshTask : public IGeneratingVoxelsThreadedTask {
public:
	ChunkMeshTask();
	~ChunkMeshTask();

	const char *get_debug_name() const override {
		return "ChunkMesh";
	}

	void run(ThreadedTaskContext &ctx) override;
	TaskPriority get_priority() override;
	bool is_cancelled() override;
	void apply_result() override;

	void set_gpu_results(std::vector<GenerateChunkGPUTaskResult> &&results) override;

	static int debug_get_running_count();

	// 3x3x3 or 4x4x4 grid of voxel chunks.
	FixedArray<std::shared_ptr<VoxelBufferInternal>, constants::MAX_BLOCK_COUNT_PER_REQUEST> blocks;
	// TODO Need to provide format
	// FixedArray<uint8_t, VoxelBufferInternal::MAX_CHANNELS> channel_depths;
	Vector3i chunk_mesh_position; // In mesh chunks of the specified lod
	VolumeID volume_id;
	uint8_t lod_index = 0;
	uint8_t blocks_count = 0;
	bool collision_hint = false;
	bool lod_hint = false;
	// Detail textures might be enabled, but we don't always want to update them in every mesh update.
	// So this boolean is also checked to know if they should be computed.
	bool require_detail_texture = false;
	uint8_t detail_texture_generator_override_begin_lod_index = 0;
	bool detail_texture_use_gpu = false;
	bool block_generation_use_gpu = false;
	PriorityDependency priority_dependency;
	std::shared_ptr<MeshingDependency> meshing_dependency;
	std::shared_ptr<VoxelData> data;
	DetailRenderingSettings detail_texture_settings;
	Ref<VoxelGenerator> detail_texture_generator_override;

private:
	void gather_voxels_gpu(zylann::ThreadedTaskContext &ctx);
	void gather_voxels_cpu();
	void build_mesh();

	bool _has_run = false;
	bool _too_far = false;
	bool _has_mesh_resource = false;
	uint8_t _stage = 0;
	VoxelBufferInternal _voxels;
	VoxelMesher::Output _surfaces_output;
	Ref<Mesh> _mesh;
	std::vector<uint8_t> _mesh_material_indices; // Indexed by mesh surface
	std::shared_ptr<DetailTextureOutput> _detail_textures;
	std::vector<GenerateChunkGPUTaskResult> _gpu_generation_results;
};

Ref<ArrayMesh> build_mesh(Span<const VoxelMesher::Output::Surface> surfaces, Mesh::PrimitiveType primitive, int flags,
		std::vector<uint8_t> &surface_indices);

} // namespace zylann::voxel

#endif // VOXEL_CHUNK_MESH_TASK_H
