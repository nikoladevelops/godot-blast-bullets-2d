#pragma once

#include "core/cached_string_names2d.hpp"

#include <godot_cpp/classes/canvas_item.hpp>
#include <godot_cpp/classes/node2d.hpp>

namespace BlastBullets2D {
using namespace godot;

// A pooled plugin node (a volley, a laser) is handed to the next owner as a
// neutral node: what a previous owner's script added to it must not leak.

// User groups (engine-internal groups start with '_') and metadata.
inline void strip_user_decorations2d(Node2D *node) {
	const TypedArray<StringName> groups = node->get_groups();
	for (int g = 0; g < groups.size(); ++g) {
		const StringName group = groups[g];
		if (!String(group).begins_with("_")) {
			node->remove_from_group(group);
		}
	}
	const TypedArray<StringName> meta_keys = node->get_meta_list();
	for (int m = 0; m < meta_keys.size(); ++m) {
		node->remove_meta(meta_keys[m]);
	}
}

// Node/CanvasItem/Node2D settings the plugin never seeds: a previous owner's
// change (moved node, top_level, filtering, sorting, process mode...) must
// not leak into the next life. Each setter runs only when the value actually
// changed (the common pooled release costs reads).
inline void restore_node_baseline2d(Node2D *node) {
	if (node->get_modulate() != Color(1, 1, 1, 1)) {
		node->set_modulate(Color(1, 1, 1, 1));
	}
	if (node->get_process_mode() != Node::PROCESS_MODE_INHERIT) {
		node->set_process_mode(Node::PROCESS_MODE_INHERIT);
	}
	if (node->get_process_priority() != 0) {
		node->set_process_priority(0);
	}
	if (node->get_physics_process_priority() != 0) {
		node->set_physics_process_priority(0);
	}
	if (node->is_draw_behind_parent_enabled()) {
		node->set_draw_behind_parent(false);
	}
	if (node->is_set_as_top_level()) {
		node->set_as_top_level(false);
	}
	if (node->get_clip_children_mode() != CanvasItem::CLIP_CHILDREN_DISABLED) {
		node->set_clip_children_mode(CanvasItem::CLIP_CHILDREN_DISABLED);
	}
	if (!node->is_z_relative()) {
		node->set_z_as_relative(true);
	}
	if (node->is_y_sort_enabled()) {
		node->set_y_sort_enabled(false);
	}
	if (node->get_texture_filter() != CanvasItem::TEXTURE_FILTER_PARENT_NODE) {
		node->set_texture_filter(CanvasItem::TEXTURE_FILTER_PARENT_NODE);
	}
	if (node->get_texture_repeat() != CanvasItem::TEXTURE_REPEAT_PARENT_NODE) {
		node->set_texture_repeat(CanvasItem::TEXTURE_REPEAT_PARENT_NODE);
	}
	if (node->get_use_parent_material()) {
		node->set_use_parent_material(false);
	}
	if (node->get_transform() != Transform2D()) {
		node->set_transform(Transform2D());
	}
	// Newer engines add CanvasItem.oversampling_with_scale: restore it when
	// the running engine has it (absent properties read as nil).
	const StringName &oversampling_name = CachedStringNames2D::get().oversampling_with_scale;
	const Variant oversampling = node->get(oversampling_name);
	if (oversampling.get_type() == Variant::INT && (int64_t)oversampling != 0) {
		node->set(oversampling_name, 0);
	}
}

} // namespace BlastBullets2D
