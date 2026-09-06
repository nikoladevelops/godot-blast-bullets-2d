#include "./bullet_attachment2d.hpp"
#include "./bullet_attachment_object_pool2d.hpp"

#include <godot_cpp/classes/engine.hpp>

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

void BulletAttachment2D::_bind_methods() {
	GDVIRTUAL_BIND(on_bullet_spawn);
	GDVIRTUAL_BIND(on_bullet_disable);
	GDVIRTUAL_BIND(on_bullet_enable);
	GDVIRTUAL_BIND(on_spawn_in_pool);
}

} //namespace BlastBullets2D
