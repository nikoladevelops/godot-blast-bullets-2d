#pragma once

#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
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

// A script argument that must be a live T or null (the bound methods take a
// Variant: a typed Object* parameter is converted by godot-cpp before any
// check can run). True with r_out = the object, or null for a null argument;
// false (one error naming `fn`, `param` and `type`) for anything else,
// a freed object included.
template <typename T>
inline bool live_argument2d(const Variant &value, const char *fn, const char *param, const char *type, T *&r_out) {
	r_out = nullptr;
	if (value.get_type() == Variant::NIL) {
		return true;
	}
	r_out = live_object2d<T>(value);
	if (r_out == nullptr) {
		UtilityFunctions::push_error(String(fn) + ": " + param + " must be a " + type + " or null (a freed object is neither), nothing changed.");
		return false;
	}
	return true;
}

} // namespace BlastBullets2D
