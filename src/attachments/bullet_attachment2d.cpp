#include "attachments/bullet_attachment2d.hpp"
#include "attachments/bullet_attachment_object_pool2d.hpp"

#include "bullet_volley/bullet_volley2d.hpp"

#include <godot_cpp/classes/animated_sprite2d.hpp>
#include <godot_cpp/classes/animation_player.hpp>
#include <godot_cpp/classes/cpu_particles2d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/gpu_particles2d.hpp>
#include <godot_cpp/core/object.hpp>

using namespace godot;
namespace BlastBullets2D {

void BulletAttachment2D::_notification(int p_what) {
	if (p_what != NOTIFICATION_PREDELETE) {
		return;
	}
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	// Manually freed while pooled: drop from the pool so a later pop() can never
	// hand out freed memory. Factory teardown detaches first (see detach_all),
	// so home_pool is always alive here.
	if (is_pooled && home_pool != nullptr) {
		home_pool->remove_instance(this, home_pooling_id);
	}
	is_pooled = false;
	home_pool = nullptr;

	// Manually freed while ACTIVE: drop the owning multimesh's slot so it can't
	// keep a dangling pointer. The owner may itself be dying/freed - resolving via
	// ObjectDB returns null then and the call is skipped safely.
	if (owner_volley_id != 0) {
		Object *owner_object = ObjectDB::get_instance(ObjectID(owner_volley_id));
		BulletVolley2D *owner = Object::cast_to<BulletVolley2D>(owner_object);
		if (owner != nullptr && owner_bullet_index >= 0) {
			owner->_do_drop_attachment_slot_if_matches(owner_bullet_index, this);
		}
		owner_volley_id = 0;
		owner_bullet_index = -1;
	}
}

void BulletAttachment2D::call_on_bullet_spawn() {
	GDVIRTUAL_CALL(on_bullet_spawn);
}

void BulletAttachment2D::call_on_bullet_disable() {
	GDVIRTUAL_CALL(on_bullet_disable);
}

void BulletAttachment2D::call_on_bullet_enable() {
	GDVIRTUAL_CALL(on_bullet_enable);
}

void BulletAttachment2D::call_on_spawn_in_pool() {
	GDVIRTUAL_CALL(on_spawn_in_pool);
}

void BulletAttachment2D::call_on_time_scale_changed(double scale) {
	GDVIRTUAL_CALL(on_time_scale_changed, scale);
}

// Meta key under which a scaled node remembers its authored speed_scale.
static const char *TIME_SCALE_BASE_META = "_blast_time_scale_base";

template <typename TNode>
static void scale_speed_of(TNode *node, double scale) {
	const StringName meta_key(TIME_SCALE_BASE_META);
	if (scale == 1.0) {
		// Back to real time: put the authored value back exactly.
		if (node->has_meta(meta_key)) {
			node->set_speed_scale((double)node->get_meta(meta_key));
			node->remove_meta(meta_key);
		}
		return;
	}
	if (!node->has_meta(meta_key)) {
		node->set_meta(meta_key, (double)node->get_speed_scale());
	}
	node->set_speed_scale((double)node->get_meta(meta_key) * scale);
}

void BulletAttachment2D::scale_descendants(double scale) {
	std::vector<Node *> pending;
	pending.push_back(this);
	while (!pending.empty()) {
		Node *node = pending.back();
		pending.pop_back();
		const int count = node->get_child_count(true);
		for (int i = 0; i < count; ++i) {
			Node *child = node->get_child(i, true);
			if (child == nullptr) {
				continue;
			}
			if (GPUParticles2D *gpu = Object::cast_to<GPUParticles2D>(child)) {
				scale_speed_of(gpu, scale);
			} else if (CPUParticles2D *cpu = Object::cast_to<CPUParticles2D>(child)) {
				scale_speed_of(cpu, scale);
			} else if (AnimatedSprite2D *sprite = Object::cast_to<AnimatedSprite2D>(child)) {
				scale_speed_of(sprite, scale);
			} else if (AnimationPlayer *player = Object::cast_to<AnimationPlayer>(child)) {
				scale_speed_of(player, scale);
			}
			pending.push_back(child);
		}
	}
}

void BulletAttachment2D::set_apply_time_scale(bool value) {
	if (apply_time_scale == value) {
		return;
	}
	apply_time_scale = value;
	if (applied_time_scale != 1.0) {
		// Switching on applies the current scale, switching off restores.
		scale_descendants(value ? applied_time_scale : 1.0);
	}
}

void BulletAttachment2D::set_time_scale_factor(double scale) {
	if (!Math::is_finite(scale) || scale < 0.0 || scale == applied_time_scale) {
		return;
	}
	applied_time_scale = scale;
	if (apply_time_scale) {
		scale_descendants(scale);
	}
	call_on_time_scale_changed(scale);
}

void BulletAttachment2D::refresh_time_scale() {
	if (apply_time_scale && applied_time_scale != 1.0) {
		scale_descendants(applied_time_scale);
	}
}

void BulletAttachment2D::_bind_methods() {
	GDVIRTUAL_BIND(on_time_scale_changed, "scale");
	ClassDB::bind_method(D_METHOD("get_apply_time_scale"), &BulletAttachment2D::get_apply_time_scale);
	ClassDB::bind_method(D_METHOD("set_apply_time_scale", "value"), &BulletAttachment2D::set_apply_time_scale);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "apply_time_scale"), "set_apply_time_scale", "get_apply_time_scale");
	ClassDB::bind_method(D_METHOD("get_applied_time_scale"), &BulletAttachment2D::get_applied_time_scale);
	ClassDB::bind_method(D_METHOD("refresh_time_scale"), &BulletAttachment2D::refresh_time_scale);
	GDVIRTUAL_BIND(on_bullet_spawn);
	GDVIRTUAL_BIND(on_bullet_disable);
	GDVIRTUAL_BIND(on_bullet_enable);
	GDVIRTUAL_BIND(on_spawn_in_pool);
}

} //namespace BlastBullets2D
