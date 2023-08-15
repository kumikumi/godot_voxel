#ifndef VOXEL_INSTANCE_COMPONENT_H
#define VOXEL_INSTANCE_COMPONENT_H

#include "../../util/godot/classes/node.h"
#include "voxel_instancer.h"

namespace zylann::voxel {

// Used as child of scene items instanced with VoxelInstancer.
//
// It is needed because such instances are tied with some of the logic in VoxelInstancer.
// The root of a scene could be anything derived from Node3D,
// so offering an API using inheritance on the root node is impractical.
// So instead the component approach is taken.
// If a huge amount of instances is needed, prefer using fast/multimesh instances.
class VoxelInstanceComponent : public Node {
	GDCLASS(VoxelInstanceComponent, Node)
public:
	void mark_modified() {
		ERR_FAIL_COND(_instancer == nullptr);
		_instancer->on_scene_instance_modified(_chunk_position, _render_chunk_index);
	}

	void detach() {
		ERR_FAIL_COND_MSG(_instancer == nullptr, "Already detached");
		_instancer = nullptr;
	}

	void attach(VoxelInstancer *instancer) {
		ERR_FAIL_COND_MSG(_instancer != nullptr, "Already attached");
		_instancer = instancer;
	}

	// TODO Need to investigate if we need this
	//
	// This must be called by the user from a script if they want the instancer to remember a removal.
	// It may be common to call `queue_free()` on the root (which could be a body or an area) but there doesn't seem to
	// be a reliable way to detect this scenario happens from a child node.
	// `_exit_tree` could happen for different reasons.
	// `unparented` won't happen because the parent is unparented, not the child.
	void detach_as_removed() {
		ERR_FAIL_COND_MSG(_instancer == nullptr, "Already detached");
		_instancer->on_scene_instance_removed(_chunk_position, _render_chunk_index, _instance_index);
		_instancer = nullptr;
	}

	Variant serialize_state() {
		// TODO Scripting
		return Variant();
	}

	Variant deserialize_state() {
		// TODO Scripting
		return Variant();
	}

	void set_instance_index(int instance_index) {
		_instance_index = instance_index;
	}

	void set_chunk_position(Vector3i chunk_position) {
		_chunk_position = chunk_position;
	}

	void set_render_chunk_index(unsigned int render_chunk_index) {
		_render_chunk_index = render_chunk_index;
	}

	static VoxelInstanceComponent *find_in(Node *root) {
		ERR_FAIL_COND_V(root == nullptr, nullptr);
		for (int i = 0; i < root->get_child_count(); ++i) {
			Node *child = root->get_child(i);
			VoxelInstanceComponent *cmp = Object::cast_to<VoxelInstanceComponent>(child);
			if (cmp != nullptr) {
				return cmp;
			}
		}
		return nullptr;
	}

protected:
	void _notification(int p_what) {
		switch (p_what) {
				// case NOTIFICATION_PARENTED:
				// 	Spatial *spatial = Object::cast_to<Spatial>(get_parent());
				// 	if (spatial == nullptr) {
				// 		ERR_PRINT("VoxelInstanceComponent must have a parent derived from Spatial");
				// 	}
				// 	break;

			// TODO Optimization: this is also called when we quit the game or destroy the world
			// which can make things a bit slow, but I don't know if it can easily be avoided
			case NOTIFICATION_UNPARENTED:
				// The user could queue_free() that node or its parent in game for some reason,
				// so we have to notify the instancer to remove the instance
				if (_instancer != nullptr) {
					detach_as_removed();
				}
				break;
		}
	}

private:
	static void _bind_methods() {
		// TODO Scripting
	}

	VoxelInstancer *_instancer = nullptr;
	Vector3i _chunk_position;
	unsigned int _render_chunk_index;
	int _instance_index = -1;
};

} // namespace zylann::voxel

#endif // VOXEL_INSTANCE_COMPONENT_H
