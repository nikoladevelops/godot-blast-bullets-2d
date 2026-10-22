#pragma once

#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace BlastBullets2D {
using namespace godot;

// The Object a script-provided Variant holds, or null when it holds none or
// a FREED one. godot-cpp's Variant -> Object* conversion reads the object's
// instance binding through the Variant's raw pointer, so a freed node passed
// from GDScript (a var that outlived its node) was read after free: a crash.
// The id stored in the Variant is read without touching the object (godot-cpp
// 4.4+), then resolved through ObjectDB.
inline Object *live_object2d(const Variant &value) {
	if (value.get_type() != Variant::OBJECT) {
		return nullptr;
	}
	const ObjectID id = value;
	return id.is_valid() ? ObjectDB::get_instance(id) : nullptr;
}

template <typename T>
inline T *live_object2d(const Variant &value) {
	return Object::cast_to<T>(live_object2d(value));
}

} // namespace BlastBullets2D
