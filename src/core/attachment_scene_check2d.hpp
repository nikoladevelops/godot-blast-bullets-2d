#pragma once

#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/scene_state.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "attachments/bullet_attachment2d.hpp"

namespace BlastBullets2D {
using namespace godot;

// What a PackedScene's own state says about it as an attachment scene, read
// WITHOUT instantiating it (no user code runs, no node is made and thrown away):
// EMPTY when it holds no node (instantiating it is an engine error), FOREIGN when
// its root's class is known and is not a BulletAttachment2D. Anything else is OK
// here (an inherited root names no class: instantiating still decides). The
// state is read on every call, so a scene packed again later is seen as it is now.
enum class AttachmentSceneCheck2D {
	OK,
	EMPTY,
	FOREIGN,
};

inline AttachmentSceneCheck2D check_attachment_scene2d(const Ref<PackedScene> &scene, StringName *root_type = nullptr) {
	if (scene.is_null() || !scene->can_instantiate()) {
		return AttachmentSceneCheck2D::EMPTY;
	}
	const Ref<SceneState> state = scene->get_state();
	if (state.is_null() || state->get_node_count() == 0) {
		return AttachmentSceneCheck2D::EMPTY;
	}
	const StringName type = state->get_node_type(0);
	if (root_type != nullptr) {
		*root_type = type;
	}
	ClassDBSingleton *db = ClassDBSingleton::get_singleton();
	if (type == StringName() || !db->class_exists(type)) {
		return AttachmentSceneCheck2D::OK; // unknown here: the instantiation decides
	}
	return db->is_parent_class(type, BulletAttachment2D::get_class_static()) ? AttachmentSceneCheck2D::OK : AttachmentSceneCheck2D::FOREIGN;
}

} //namespace BlastBullets2D
