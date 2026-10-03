#include "./volley_pool_key2d.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace BlastBullets2D {

void VolleyPoolKey2D::set_amount_bullets(int p_amount_bullets) {
	if (p_amount_bullets < 0) {
		UtilityFunctions::push_error("VolleyPoolKey2D amount_bullets must be >= 0.");
		return;
	}
	amount_bullets = p_amount_bullets;
}

void VolleyPoolKey2D::set_shape_type(int p_shape_type) {
	if (p_shape_type != PhysicsServer2D::SHAPE_CIRCLE && p_shape_type != PhysicsServer2D::SHAPE_RECTANGLE && p_shape_type != PhysicsServer2D::SHAPE_CAPSULE) {
		UtilityFunctions::push_error("VolleyPoolKey2D shape_type must be Circle, Rectangle or Capsule (the only types the bullet physics supports).");
		return;
	}
	shape_type = p_shape_type;
}

Ref<VolleyPoolKey2D> VolleyPoolKey2D::make(int p_amount_bullets, int p_shape_type) {
	if (p_amount_bullets < 0) {
		UtilityFunctions::push_error("VolleyPoolKey2D.make: amount_bullets must be >= 0, returning null.");
		return Ref<VolleyPoolKey2D>();
	}
	if (p_shape_type != PhysicsServer2D::SHAPE_CIRCLE && p_shape_type != PhysicsServer2D::SHAPE_RECTANGLE && p_shape_type != PhysicsServer2D::SHAPE_CAPSULE) {
		UtilityFunctions::push_error("VolleyPoolKey2D.make: shape_type must be Circle, Rectangle or Capsule, returning null.");
		return Ref<VolleyPoolKey2D>();
	}
	Ref<VolleyPoolKey2D> key = memnew(VolleyPoolKey2D);
	key->set_amount_bullets(p_amount_bullets);
	key->set_shape_type(p_shape_type);
	return key;
}

Ref<VolleyPoolKey2D> VolleyPoolKey2D::from_internal(const PoolKey &key) {
	Ref<VolleyPoolKey2D> res = memnew(VolleyPoolKey2D);
	res->amount_bullets = key.amount_bullets;
	res->shape_type = static_cast<int>(key.shape_type);
	return res;
}

void VolleyPoolKey2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_amount_bullets"), &VolleyPoolKey2D::get_amount_bullets);
	ClassDB::bind_method(D_METHOD("set_amount_bullets", "amount_bullets"), &VolleyPoolKey2D::set_amount_bullets);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "amount_bullets"), "set_amount_bullets", "get_amount_bullets");

	ClassDB::bind_method(D_METHOD("get_shape_type"), &VolleyPoolKey2D::get_shape_type);
	ClassDB::bind_method(D_METHOD("set_shape_type", "shape_type"), &VolleyPoolKey2D::set_shape_type);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "shape_type", PROPERTY_HINT_ENUM, "Circle:3,Rectangle:4,Capsule:5"), "set_shape_type", "get_shape_type");

	ClassDB::bind_static_method("VolleyPoolKey2D", D_METHOD("make", "amount_bullets", "shape_type"), &VolleyPoolKey2D::make);
}

} //namespace BlastBullets2D
