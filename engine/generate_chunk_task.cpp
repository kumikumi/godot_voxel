#include "generate_chunk_task.h"
#include "../storage/voxel_buffer_internal.h"
#include "../storage/voxel_data.h"
#include "../util/godot/funcs.h"
#include "../util/log.h"
#include "../util/math/conv.h"
#include "../util/profiling.h"
#include "../util/string_funcs.h"
#include "../util/tasks/async_dependency_tracker.h"
#include "save_chunk_data_task.h"
#include "voxel_engine.h"

namespace zylann::voxel {

namespace {
std::atomic_int g_debug_generate_tasks_count = { 0 };
}

GenerateChunkTask::GenerateChunkTask() {
	++g_debug_generate_tasks_count;
}

GenerateChunkTask::~GenerateChunkTask() {
	--g_debug_generate_tasks_count;
}

int GenerateChunkTask::debug_get_running_count() {
	return g_debug_generate_tasks_count;
}

void GenerateChunkTask::run(zylann::ThreadedTaskContext &ctx) {
	ZN_PROFILE_SCOPE();

	CRASH_COND(stream_dependency == nullptr);
	Ref<VoxelGenerator> generator = stream_dependency->generator;
	ERR_FAIL_COND(generator.is_null());

	if (voxels == nullptr) {
		voxels = make_shared_instance<VoxelBufferInternal>();
		voxels->create(chunk_size, chunk_size, chunk_size);
	}

	if (use_gpu) {
		if (_stage == 0) {
			run_gpu_task(ctx);
		}
		if (_stage == 1) {
			run_gpu_conversion();
		}
		if (_stage == 2) {
			run_stream_saving_and_finish();
		}
	} else {
		run_cpu_generation();
		run_stream_saving_and_finish();
	}
}

void GenerateChunkTask::run_gpu_task(zylann::ThreadedTaskContext &ctx) {
	Ref<VoxelGenerator> generator = stream_dependency->generator;
	ERR_FAIL_COND(generator.is_null());

	// TODO Broad-phase to avoid the GPU part entirely?
	// Implement and call `VoxelGenerator::generate_broad_chunk()`

	std::shared_ptr<ComputeShader> generator_shader = generator->get_chunk_rendering_shader();
	ERR_FAIL_COND(generator_shader == nullptr);

	const Vector3i origin_in_voxels = (position << lod_index) * chunk_size;

	ZN_ASSERT(voxels != nullptr);
	VoxelGenerator::VoxelQueryData generator_query{ *voxels, origin_in_voxels, lod_index };
	if (generator->generate_broad_chunk(generator_query)) {
		_stage = 2;
		return;
	}

	const Vector3i resolution = Vector3iUtil::create(chunk_size);

	GenerateChunkGPUTask *gpu_task = memnew(GenerateChunkGPUTask);
	gpu_task->boxes_to_generate.push_back(Box3i(Vector3i(), resolution));
	gpu_task->generator_shader = generator_shader;
	gpu_task->generator_shader_params = generator->get_chunk_rendering_shader_parameters();
	gpu_task->generator_shader_outputs = generator->get_chunk_rendering_shader_outputs();
	gpu_task->lod_index = lod_index;
	gpu_task->origin_in_voxels = origin_in_voxels;
	gpu_task->consumer_task = this;

	if (data != nullptr) {
		const AABB aabb_voxels(to_vec3(origin_in_voxels), to_vec3(resolution << lod_index));
		std::vector<VoxelModifier::ShaderData> modifiers_shader_data;
		const VoxelModifierStack &modifiers = data->get_modifiers();
		modifiers.apply_for_gpu_rendering(modifiers_shader_data, aabb_voxels, VoxelModifier::ShaderData::TYPE_BLOCK);
		for (const VoxelModifier::ShaderData &d : modifiers_shader_data) {
			gpu_task->modifiers.push_back(GenerateChunkGPUTask::ModifierData{
					d.shader_rids[VoxelModifier::ShaderData::TYPE_BLOCK], d.params });
		}
	}

	ctx.status = ThreadedTaskContext::STATUS_TAKEN_OUT;

	// Start GPU task, we'll continue after it
	VoxelEngine::get_singleton().push_gpu_task(gpu_task);
}

void GenerateChunkTask::set_gpu_results(std::vector<GenerateChunkGPUTaskResult> &&results) {
	_gpu_generation_results = std::move(results);
	_stage = 1;
}

void GenerateChunkTask::run_gpu_conversion() {
	GenerateChunkGPUTaskResult::convert_to_voxel_buffer(to_span(_gpu_generation_results), *voxels);
	_stage = 2;
}

void GenerateChunkTask::run_cpu_generation() {
	const Vector3i origin_in_voxels = (position << lod_index) * chunk_size;

	Ref<VoxelGenerator> generator = stream_dependency->generator;

	VoxelGenerator::VoxelQueryData query_data{ *voxels, origin_in_voxels, lod_index };
	const VoxelGenerator::Result result = generator->generate_chunk(query_data);
	max_lod_hint = result.max_lod_hint;

	if (data != nullptr) {
		data->get_modifiers().apply(query_data.voxel_buffer,
				AABB(query_data.origin_in_voxels, query_data.voxel_buffer.get_size() << lod_index));
	}
}

void GenerateChunkTask::run_stream_saving_and_finish() {
	if (stream_dependency->valid) {
		Ref<VoxelStream> stream = stream_dependency->stream;

		// TODO In some cases we don't want this to run all the time, do we?
		// Like in full load mode, where non-edited chunks remain generated on the fly...
		if (stream.is_valid() && stream->get_save_generator_output()) {
			ZN_PRINT_VERBOSE(
					format("Requesting save of generator output for block {} lod {}", position, int(lod_index)));

			// TODO Optimization: `voxels` doesn't actually need to be shared
			std::shared_ptr<VoxelBufferInternal> voxels_copy = make_shared_instance<VoxelBufferInternal>();
			voxels->duplicate_to(*voxels_copy, true);

			// No instances, generators are not designed to produce them at this stage yet.
			// No priority data, saving doesn't need sorting.

			SaveChunkDataTask *save_task = memnew(SaveChunkDataTask(
					volume_id, position, lod_index, chunk_size, voxels_copy, stream_dependency, nullptr));

			VoxelEngine::get_singleton().push_async_io_task(save_task);
		}
	}

	has_run = true;
}

TaskPriority GenerateChunkTask::get_priority() {
	float closest_viewer_distance_sq;
	const TaskPriority p = priority_dependency.evaluate(
			lod_index, constants::TASK_PRIORITY_GENERATE_BAND2, &closest_viewer_distance_sq);
	too_far = drop_beyond_max_distance && closest_viewer_distance_sq > priority_dependency.drop_distance_squared;
	return p;
}

bool GenerateChunkTask::is_cancelled() {
	return !stream_dependency->valid || too_far; // || stream_dependency->stream->get_fallback_generator().is_null();
}

void GenerateChunkTask::apply_result() {
	bool aborted = true;

	if (VoxelEngine::get_singleton().is_volume_valid(volume_id)) {
		// TODO Comparing pointer may not be guaranteed
		// The request response must match the dependency it would have been requested with.
		// If it doesn't match, we are no longer interested in the result.
		if (stream_dependency->valid) {
			Ref<VoxelStream> stream = stream_dependency->stream;

			VoxelEngine::ChunkDataOutput o;
			o.voxels = voxels;
			o.position = position;
			o.lod_index = lod_index;
			o.dropped = !has_run;
			if (stream.is_valid() && stream->get_save_generator_output()) {
				// We can't consider the chunk as "generated" since there is no state to tell that once saved,
				// so it has to be considered an edited chunk
				o.type = VoxelEngine::ChunkDataOutput::TYPE_LOADED;
			} else {
				o.type = VoxelEngine::ChunkDataOutput::TYPE_GENERATED;
			}
			o.max_lod_hint = max_lod_hint;
			o.initial_load = false;

			VoxelEngine::VolumeCallbacks callbacks = VoxelEngine::get_singleton().get_volume_callbacks(volume_id);
			ERR_FAIL_COND(callbacks.data_output_callback == nullptr);
			callbacks.data_output_callback(callbacks.data, o);

			aborted = !has_run;
		}

	} else {
		// This can happen if the user removes the volume while requests are still about to return
		ZN_PRINT_VERBOSE("Gemerated data request response came back but volume wasn't found");
	}

	// TODO We could complete earlier inside run() if we had access to the data structure to write the chunk into.
	// This would reduce latency a little. The rest of things the terrain needs to do with the generated chunk could
	// run later.
	if (tracker != nullptr) {
		if (aborted) {
			tracker->abort();
		} else {
			tracker->post_complete();
		}
	}
}

} // namespace zylann::voxel
