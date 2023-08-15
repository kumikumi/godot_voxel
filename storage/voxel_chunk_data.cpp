#include "voxel_chunk_data.h"
#include "../util/log.h"
#include "../util/string_funcs.h"

namespace zylann::voxel {

void VoxelChunkData::set_modified(bool modified) {
	// #ifdef TOOLS_ENABLED
	// 	if (_modified == false && modified) {
	// 		ZN_PRINT_VERBOSE(format("Marking chunk {} as modified", size_t(this)));
	// 	}
	// #endif
	_modified = modified;
}

} // namespace zylann::voxel
