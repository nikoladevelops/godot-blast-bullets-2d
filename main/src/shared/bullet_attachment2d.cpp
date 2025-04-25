#include "./bullet_attachment2d.hpp"

using namespace godot;
namespace BlastBullets2D{


void BulletAttachment2D::call_on_bullet_spawn(){
    GDVIRTUAL_CALL(on_bullet_spawn);
}
void BulletAttachment2D::call_on_bullet_disable(){
    GDVIRTUAL_CALL(on_bullet_disable);
}
void BulletAttachment2D::call_on_bullet_activate(){
    GDVIRTUAL_CALL(on_bullet_activate);
}
Ref<Resource> BulletAttachment2D::call_on_bullet_save(){
    Ref<Resource> data;
    GDVIRTUAL_CALL(on_bullet_save, data);

    return data;
}
void BulletAttachment2D::call_on_bullet_load(Ref<Resource> custom_data_to_load){
    GDVIRTUAL_CALL(on_bullet_load, custom_data_to_load);
}
void BulletAttachment2D::call_on_bullet_spawn_as_disabled(){
    GDVIRTUAL_CALL(on_bullet_spawn_as_disabled);
}

int BulletAttachment2D::get_attachment_id() const{
    return attachment_id;
}

void BulletAttachment2D::set_attachment_id(int new_attachment_id){
    attachment_id = new_attachment_id;
}

bool BulletAttachment2D::get_stick_relative_to_bullet() const{
    return stick_relative_to_bullet;
}

void BulletAttachment2D::set_stick_relative_to_bullet(bool new_stick_relative_to_bullet){
    stick_relative_to_bullet = new_stick_relative_to_bullet;
}

void BulletAttachment2D::_bind_methods(){
    GDVIRTUAL_BIND(on_bullet_spawn);
    GDVIRTUAL_BIND(on_bullet_disable);
    GDVIRTUAL_BIND(on_bullet_activate);
    GDVIRTUAL_BIND(on_bullet_save);
    GDVIRTUAL_BIND(on_bullet_load, "custom_data_to_load");
    GDVIRTUAL_BIND(on_bullet_spawn_as_disabled);

    ClassDB::bind_method(D_METHOD("set_attachment_id"), &BulletAttachment2D::set_attachment_id);
    ClassDB::bind_method(D_METHOD("get_attachment_id"), &BulletAttachment2D::get_attachment_id);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "attachment_id"), "set_attachment_id", "get_attachment_id");

    ClassDB::bind_method(D_METHOD("set_stick_relative_to_bullet"), &BulletAttachment2D::set_stick_relative_to_bullet);
    ClassDB::bind_method(D_METHOD("get_stick_relative_to_bullet"), &BulletAttachment2D::get_stick_relative_to_bullet);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "stick_relative_to_bullet"), "set_stick_relative_to_bullet", "get_stick_relative_to_bullet");
}

}