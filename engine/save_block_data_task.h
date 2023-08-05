#ifndef SAVE_BLOCK_DATA_TASK_H
#define SAVE_BLOCK_DATA_TASK_H

#include "../util/memory.h"
#include "../util/tasks/threaded_task.h"
#include "ids.h"
#include "streaming_dependency.h"

namespace zylann {

class AsyncDependencyTracker;

namespace voxel {

class SaveChunkDataTask : public IThreadedTask {
public:
	// For saving voxels only
	SaveChunkDataTask(VolumeID p_volume_id, Vector3i p_block_pos, uint8_t p_lod, uint8_t p_block_size,
			std::shared_ptr<VoxelBufferInternal> p_voxels, std::shared_ptr<StreamingDependency> p_stream_dependency,
			std::shared_ptr<AsyncDependencyTracker> p_tracker);

	// For saving instances only
	SaveChunkDataTask(VolumeID p_volume_id, Vector3i p_block_pos, uint8_t p_lod, uint8_t p_block_size,
			UniquePtr<InstanceChunkData> p_instances, std::shared_ptr<StreamingDependency> p_stream_dependency,
			std::shared_ptr<AsyncDependencyTracker> p_tracker);

	~SaveChunkDataTask();

	const char *get_debug_name() const override {
		return "SaveChunkData";
	}

	void run(ThreadedTaskContext &ctx) override;
	TaskPriority get_priority() override;
	bool is_cancelled() override;
	void apply_result() override;

	static int debug_get_running_count();

private:
	std::shared_ptr<VoxelBufferInternal> _voxels;
	UniquePtr<InstanceChunkData> _instances;
	Vector3i _position; // In data blocks of the specified lod
	VolumeID _volume_id;
	uint8_t _lod;
	uint8_t _block_size;
	bool _has_run = false;
	bool _save_instances = false;
	bool _save_voxels = false;
	std::shared_ptr<StreamingDependency> _stream_dependency;
	// Optional tracking, can be null
	std::shared_ptr<AsyncDependencyTracker> _tracker;
};

} // namespace voxel
} // namespace zylann

#endif // SAVE_BLOCK_DATA_TASK_H
