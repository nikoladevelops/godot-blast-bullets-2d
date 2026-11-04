#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/object.hpp>

namespace BlastBullets2D {

// A node the plugin created and caches (containers, debugger, voices, effect
// shards). It is an ordinary child: a script can free() it at any time, so
// the raw pointer may dangle. get() resolves the id through ObjectDB and
// compares POINTERS, never dereferencing the stored one (calling
// ptr->get_instance_id() to validate ptr reads freed memory).
template <typename T>
struct OwnedNode2D {
	T *ptr = nullptr;
	uint64_t id = 0;

	OwnedNode2D() = default;
	OwnedNode2D(T *p_node) { set(p_node); } // NOLINT: implicit on purpose (push_back(raw))

	void set(T *p_node) {
		ptr = p_node;
		id = p_node != nullptr ? (uint64_t)p_node->get_instance_id() : 0;
	}
	void reset() {
		ptr = nullptr;
		id = 0;
	}
	// The node, or null when it was never set or has been freed.
	T *get() const {
		if (ptr == nullptr || id == 0) {
			return nullptr;
		}
		return godot::ObjectDB::get_instance(godot::ObjectID(id)) == static_cast<godot::Object *>(ptr) ? ptr : nullptr;
	}
	// Set but freed behind our back.
	bool lost() const { return ptr != nullptr && get() == nullptr; }
};

// Detaches a node the plugin owns from its parent and deletes it, unless the
// removal's tree signals (user code: the parent's child_exiting_tree, the
// node's own tree_exiting) already freed it. Every plugin "remove, then
// delete" goes through here: a second delete of a freed node is a crash.
inline void remove_and_delete2d(godot::Node *node) {
	if (node == nullptr) {
		return;
	}
	const uint64_t id = (uint64_t)node->get_instance_id();
	if (godot::Node *parent = node->get_parent()) {
		parent->remove_child(node);
	}
	if (godot::ObjectDB::get_instance(godot::ObjectID(id)) == static_cast<godot::Object *>(node)) {
		godot::memdelete(node);
	}
}

} // namespace BlastBullets2D
