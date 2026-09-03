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

	// Resolve Ref<Shape2D> to engine enum. Null => RECTANGLE (default).
	// Unsupported => SHAPE_CUSTOM (caller must error + fallback).
	_ALWAYS_INLINE_ static PhysicsServer2D::ShapeType resolve_type(const Ref<Shape2D> &shape) {
		if (shape.is_null()) {
			return PhysicsServer2D::SHAPE_RECTANGLE;
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
	_ALWAYS_INLINE_ static PhysicsServer2D::ShapeType get_effective_type(const Ref<Shape2D> &shape, bool print_error) {
		PhysicsServer2D::ShapeType raw = resolve_type(shape);
		if (shape.is_null()) {
			return PhysicsServer2D::SHAPE_RECTANGLE;
		}
		if (!is_supported(raw)) {
			if (print_error) {
				UtilityFunctions::push_error("Unsupported collision shape type: " + shape->get_class() + " - only RectangleShape2D/CircleShape2D/CapsuleShape2D supported. Falling back to rectangle 32x32.");
			}
			return PhysicsServer2D::SHAPE_RECTANGLE;
		}
		if (!is_valid_data(shape)) {
			if (print_error) {
				UtilityFunctions::push_error("Invalid collision shape parameters for " + shape->get_class() + " (size/radius/height must be > 0). Falling back to rectangle 32x32.");
			}
			return PhysicsServer2D::SHAPE_RECTANGLE;
		}
		return raw;
	}

	// Quiet apply, no error print (error already printed once at creation). Uses effective type.
	_ALWAYS_INLINE_ static void apply_shape_data_quiet(PhysicsServer2D *server, const RID &rid, const Ref<Shape2D> &shape, PhysicsServer2D::ShapeType effective) {
		switch (effective) {
			case PhysicsServer2D::SHAPE_CIRCLE: {
				auto *circle = Object::cast_to<CircleShape2D>(shape.ptr());
				float r = circle ? circle->get_radius() : 0.0f;
				if (r <= 0.0f) {
					server->shape_set_data(rid, DEFAULT_RECT_HALF);
				} else {
					server->shape_set_data(rid, r);
				}
				break;
			}
			case PhysicsServer2D::SHAPE_CAPSULE: {
				auto *capsule = Object::cast_to<CapsuleShape2D>(shape.ptr());
				float r = capsule ? capsule->get_radius() : 0.0f;
				float h = capsule ? capsule->get_height() : 0.0f;
				if (r <= 0.0f || h <= 0.0f) {
					server->shape_set_data(rid, DEFAULT_RECT_HALF);
				} else {
					server->shape_set_data(rid, Vector2(r, h));
				}
				break;
			}
			case PhysicsServer2D::SHAPE_RECTANGLE:
			default: {
				if (shape.is_null()) {
					server->shape_set_data(rid, DEFAULT_RECT_HALF);
				} else if (auto *rect = Object::cast_to<RectangleShape2D>(shape.ptr())) {
					Vector2 s = rect->get_size();
					if (s.x <= 0.0f || s.y <= 0.0f) {
						server->shape_set_data(rid, DEFAULT_RECT_HALF);
					} else {
						server->shape_set_data(rid, s / 2);
					}
				} else {
					// Effective should already be RECTANGLE here, but shape is circle/capsule with fallback? Use default.
					server->shape_set_data(rid, DEFAULT_RECT_HALF);
				}
				break;
			}
		}
	}

	// Full visual size for debugger (not half extents). Uses Shape2D::get_rect() so math matches engine.
	_ALWAYS_INLINE_ static Vector2 get_full_size(const Ref<Shape2D> &shape) {
		if (shape.is_null()) {
			return DEFAULT_RECT_SIZE;
		}
		PhysicsServer2D::ShapeType type = resolve_type(shape);
		switch (type) {
			case PhysicsServer2D::SHAPE_RECTANGLE:
			case PhysicsServer2D::SHAPE_CIRCLE:
			case PhysicsServer2D::SHAPE_CAPSULE:
				return shape->get_rect().size;
			default:
				return DEFAULT_RECT_SIZE;
		}
	}

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

	// Push correct Variant to server. Returns resolved type actually used (fallback => RECTANGLE).
	// Always prints error on unsupported so user sees what went wrong.
	_ALWAYS_INLINE_ static PhysicsServer2D::ShapeType apply_shape_data(PhysicsServer2D *server, const RID &rid, const Ref<Shape2D> &shape) {
		if (shape.is_null()) {
			server->shape_set_data(rid, DEFAULT_RECT_HALF);
			return PhysicsServer2D::SHAPE_RECTANGLE;
		}
		if (auto *circle = Object::cast_to<CircleShape2D>(shape.ptr())) {
			float r = circle->get_radius();
			if (r <= 0.0f) {
				UtilityFunctions::push_error("CircleShape2D radius must be > 0. Falling back to rectangle 32x32.");
				server->shape_set_data(rid, DEFAULT_RECT_HALF);
				return PhysicsServer2D::SHAPE_RECTANGLE;
			}
			server->shape_set_data(rid, r);
			return PhysicsServer2D::SHAPE_CIRCLE;
		}
		if (auto *capsule = Object::cast_to<CapsuleShape2D>(shape.ptr())) {
			float r = capsule->get_radius();
			float h = capsule->get_height();
			if (r <= 0.0f || h <= 0.0f) {
				UtilityFunctions::push_error("CapsuleShape2D radius/height must be > 0. Falling back to rectangle 32x32.");
				server->shape_set_data(rid, DEFAULT_RECT_HALF);
				return PhysicsServer2D::SHAPE_RECTANGLE;
			}
			server->shape_set_data(rid, Vector2(r, h));
			return PhysicsServer2D::SHAPE_CAPSULE;
		}
		if (auto *rect = Object::cast_to<RectangleShape2D>(shape.ptr())) {
			Vector2 size = rect->get_size();
			if (size.x <= 0.0f || size.y <= 0.0f) {
				UtilityFunctions::push_error("RectangleShape2D size must be > 0. Falling back to rectangle 32x32.");
				server->shape_set_data(rid, DEFAULT_RECT_HALF);
				return PhysicsServer2D::SHAPE_RECTANGLE;
			}
			server->shape_set_data(rid, size / 2);
			return PhysicsServer2D::SHAPE_RECTANGLE;
		}
		UtilityFunctions::push_error("Unsupported collision shape type: " + shape->get_class() + " - only RectangleShape2D/CircleShape2D/CapsuleShape2D supported. Falling back to rectangle 32x32.");
		server->shape_set_data(rid, DEFAULT_RECT_HALF);
		return PhysicsServer2D::SHAPE_RECTANGLE;
	}
};

inline const Vector2 CollisionShapeHelper2D::DEFAULT_RECT_SIZE = Vector2(32, 32);
inline const Vector2 CollisionShapeHelper2D::DEFAULT_RECT_HALF = Vector2(16, 16);
} //namespace BlastBullets2D
