#include "./multimesh_pool_key2d.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace BlastBullets2D {

void MultiMeshPoolKey2D::set_amount_bullets(int p_amount_bullets) {
	if (p_amount_bullets < 0) {
		UtilityFunctions::push_error("MultiMeshPoolKey2D amount_bullets must be >= 0.");
		return;
	}
	amount_bullets = p_amount_bullets;
}

void MultiMeshPoolKey2D::set_shape_type(int p_shape_type) {
	if (p_shape_type != PhysicsServer2D::SHAPE_CIRCLE && p_shape_type != PhysicsServer2D::SHAPE_RECTANGLE && p_shape_type != PhysicsServer2D::SHAPE_CAPSULE) {
		UtilityFunctions::push_error("MultiMeshPoolKey2D shape_type must be Circle, Rectangle or Capsule (the only types the bullet physics supports).");
		return;
	}
	shape_type = p_shape_type;
}

Ref<MultiMeshPoolKey2D> MultiMeshPoolKey2D::make(int p_amount_bullets, int p_shape_type) {
	Ref<MultiMeshPoolKey2D> key = memnew(MultiMeshPoolKey2D);
	key->set_amount_bullets(p_amount_bullets);
	key->set_shape_type(p_shape_type);
	return key;
}

Ref<MultiMeshPoolKey2D> MultiMeshPoolKey2D::from_internal(const PoolKey &key) {
	Ref<MultiMeshPoolKey2D> res = memnew(MultiMeshPoolKey2D);
	res->amount_bullets = key.amount_bullets;
	res->shape_type = static_cast<int>(key.shape_type);
	return res;
}

void MultiMeshPoolKey2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_amount_bullets"), &MultiMeshPoolKey2D::get_amount_bullets);
	ClassDB::bind_method(D_METHOD("set_amount_bullets", "amount_bullets"), &MultiMeshPoolKey2D::set_amount_bullets);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "amount_bullets"), "set_amount_bullets", "get_amount_bullets");

	ClassDB::bind_method(D_METHOD("get_shape_type"), &MultiMeshPoolKey2D::get_shape_type);
	ClassDB::bind_method(D_METHOD("set_shape_type", "shape_type"), &MultiMeshPoolKey2D::set_shape_type);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "shape_type", PROPERTY_HINT_ENUM, "Circle,Rectangle,Capsule"), "set_shape_type", "get_shape_type");

	ClassDB::bind_static_method("MultiMeshPoolKey2D", D_METHOD("make", "amount_bullets", "shape_type"), &MultiMeshPoolKey2D::make);
}

} //namespace BlastBullets2D
