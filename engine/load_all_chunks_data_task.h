#ifndef LOAD_ALL_CHUNKS_DATA_TASK_H
#define LOAD_ALL_CHUNKS_DATA_TASK_H

#include "../streams/voxel_stream.h"
#include "../util/tasks/threaded_task.h"
#include "ids.h"
#include "streaming_dependency.h"

namespace zylann::voxel {

class VoxelData;

class LoadAllChunksDataTask : public IThreadedTask {
public:
	const char *get_debug_name() const override {
		return "LoadAllChunksData";
	}

	void run(ThreadedTaskContext &ctx) override;
	TaskPriority get_priority() override;
	bool is_cancelled() override;
	void apply_result() override;

	VolumeID volume_id;
	std::shared_ptr<StreamingDependency> stream_dependency;
	std::shared_ptr<VoxelData> data;

private:
	VoxelStream::FullLoadingResult _result;
};

} // namespace zylann::voxel

#endif // LOAD_ALL_CHUNKS_DATA_TASK_H
