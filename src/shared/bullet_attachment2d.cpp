#include "./bullet_attachment2d.hpp"

using namespace godot;
namespace BlastBullets2D {

void BulletAttachment2D::call_on_bullet_spawn() {
	GDVIRTUAL_CALL(on_bullet_spawn);
}

void BulletAttachment2D::call_on_bullet_disable() {
	GDVIRTUAL_CALL(on_bullet_disable);
}

void BulletAttachment2D::call_on_bullet_enable() {
	GDVIRTUAL_CALL(on_bullet_enable);
}

Ref<Resource> BulletAttachment2D::call_on_bullet_save() {
	Ref<Resource> data;
	GDVIRTUAL_CALL(on_bullet_save, data);

	return data;
}

void BulletAttachment2D::call_on_bullet_load(Ref<Resource> custom_data_to_load) {
	GDVIRTUAL_CALL(on_bullet_load, custom_data_to_load);
}

void BulletAttachment2D::call_on_spawn_in_pool() {
	GDVIRTUAL_CALL(on_spawn_in_pool);
}

void BulletAttachment2D::_bind_methods() {
	GDVIRTUAL_BIND(on_bullet_spawn);
	GDVIRTUAL_BIND(on_bullet_disable);
	GDVIRTUAL_BIND(on_bullet_enable);
	GDVIRTUAL_BIND(on_bullet_save);
	GDVIRTUAL_BIND(on_bullet_load, "custom_data_to_load");
	GDVIRTUAL_BIND(on_spawn_in_pool);
}

} //namespace BlastBullets2D
