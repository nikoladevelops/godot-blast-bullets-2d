#pragma once

#include <godot_cpp/classes/capsule_shape2d.hpp>
#include <godot_cpp/classes/circle_shape2d.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/rectangle_shape2d.hpp>
#include <godot_cpp/classes/shape2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace BlastBullets2D {
using namespace godot;

// Single source of truth for supported 2D collision shapes.
// Uses PhysicsServer2D::ShapeType enum instead of hardcoded class-name strings.
struct CollisionShapeHelper2D {
	static const Vector2 DEFAULT_RECT_SIZE; // 32x32 full size
	static const Vector2 DEFAULT_RECT_HALF; // 16x16 half extents
	static constexpr float DEFAULT_CIRCLE_RADIUS = 16.0f; // diameter 32, same coverage as rect 32

	// Resolve Ref<Shape2D> to engine enum. Null => CIRCLE (default r16).
	// Unsupported => SHAPE_CUSTOM (caller must error + fallback to circle r16).
	_ALWAYS_INLINE_ static PhysicsServer2D::ShapeType resolve_type(const Ref<Shape2D> &shape) {
		if (shape.is_null()) {
			return PhysicsServer2D::SHAPE_CIRCLE;
		}
		// Typed checks, no strings.
		if (Object::cast_to<RectangleShape2D>(shape.ptr()) != nullptr) {
			return PhysicsServer2D::SHAPE_RECTANGLE;
		}
		if (Object::cast_to<CircleShape2D>(shape.ptr()) != nullptr) {
			return PhysicsServer2D::SHAPE_CIRCLE;
		}
		if (Object::cast_to<CapsuleShape2D>(shape.ptr()) != nullptr) {
			return PhysicsServer2D::SHAPE_CAPSULE;
		}
		return PhysicsServer2D::SHAPE_CUSTOM;
	}

	_ALWAYS_INLINE_ static bool is_supported(PhysicsServer2D::ShapeType type) {
		return type == PhysicsServer2D::SHAPE_RECTANGLE || type == PhysicsServer2D::SHAPE_CIRCLE || type == PhysicsServer2D::SHAPE_CAPSULE;
	}

	// Quiet param validation, no error print. Null => true (default valid).
	_ALWAYS_INLINE_ static bool is_valid_data(const Ref<Shape2D> &shape) {
		if (shape.is_null()) {
			return true;
		}
		if (auto *circle = Object::cast_to<CircleShape2D>(shape.ptr())) {
			return circle->get_radius() > 0.0f;
		}
		if (auto *capsule = Object::cast_to<CapsuleShape2D>(shape.ptr())) {
			return capsule->get_radius() > 0.0f && capsule->get_height() > 0.0f;
		}
		if (auto *rect = Object::cast_to<RectangleShape2D>(shape.ptr())) {
			Vector2 s = rect->get_size();
			return s.x > 0.0f && s.y > 0.0f;
		}
		return false;
	}

	// Effective type after fallback. If print_error, prints once for unsupported/invalid (caller must ensure called once per spawn, not per bullet/tick).
	// Null/unsupported/invalid => CIRCLE r16 (consistent default, cheapest physics).
	_ALWAYS_INLINE_ static PhysicsServer2D::ShapeType get_effective_type(const Ref<Shape2D> &shape, bool print_error) {
		PhysicsServer2D::ShapeType raw = resolve_type(shape);
		if (shape.is_null()) {
			return PhysicsServer2D::SHAPE_CIRCLE;
		}
		if (!is_supported(raw)) {
			if (print_error) {
				UtilityFunctions::push_error("Unsupported collision shape type: " + shape->get_class() + " - only RectangleShape2D/CircleShape2D/CapsuleShape2D supported. Falling back to circle r16.");
			}
			return PhysicsServer2D::SHAPE_CIRCLE;
		}
		if (!is_valid_data(shape)) {
			if (print_error) {
				UtilityFunctions::push_error("Invalid collision shape parameters for " + shape->get_class() + " (size/radius/height must be > 0). Falling back to circle r16.");
			}
			return PhysicsServer2D::SHAPE_CIRCLE;
		}
		return raw;
	}

	// Quiet apply, no error print (error already printed once at creation). Uses effective type.
	// Null/fallback => circle r16. Effective determines RID type, so data must match RID.
	// NOTE: removed apply_shape_data_quiet() - it was dead code and its rect/capsule
	// fallbacks passed a float where Vector2 data is required (wrong shape data type).
	// generate_collision_shape_transform_for_area() in multimesh_bullets2d.cpp is the
	// single applier of shape data and is type-correct.

	// Create RID of correct server type. Caller must area_add_shape + track for free_rid.
	_ALWAYS_INLINE_ static RID create_server_shape(PhysicsServer2D *server, PhysicsServer2D::ShapeType type) {
		switch (type) {
			case PhysicsServer2D::SHAPE_CIRCLE:
				return server->circle_shape_create();
			case PhysicsServer2D::SHAPE_CAPSULE:
				return server->capsule_shape_create();
			case PhysicsServer2D::SHAPE_RECTANGLE:
			default:
				return server->rectangle_shape_create();
		}
	}
};

inline const Vector2 CollisionShapeHelper2D::DEFAULT_RECT_SIZE = Vector2(32, 32);
inline const Vector2 CollisionShapeHelper2D::DEFAULT_RECT_HALF = Vector2(16, 16);
} //namespace BlastBullets2D
