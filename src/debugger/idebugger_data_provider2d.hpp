#pragma once

#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <vector>

namespace BlastBullets2D {
using namespace godot;

/// Any class that inherits this will be able to be debugged by the bullets debugger,
/// basically reduces coupling a bit
class IDebuggerDataProvider2D {
public:
	virtual ~IDebuggerDataProvider2D() = default;

	// Gets the collision shape full size (not half extents) that all collision shapes share.
	// Uses Shape2D::get_rect().size so math matches engine for rect/circle/capsule.
	virtual const Vector2 get_collision_shape_size_for_debugging() const = 0;

	// Gets the engine shape type so debugger can scale math per shape.
	virtual PhysicsServer2D::ShapeType get_collision_shape_type_for_debugging() const = 0;

	// Gets all collision shapes' global transforms. Takes a reference for performance
	// (no copies). Make sure the vector is a member of the class that stays alive.
	virtual const std::vector<Transform2D> &get_all_collision_shape_transforms_for_debugging() const = 0;

	// Policy: the debugger ALWAYS draws every shape. Disabled bullets keep their last
	// cached transform (frozen where they died), pooled instances render their full
	// frozen set - nothing is ever hidden with the zero transform on the debug side.
	virtual bool is_active_for_debugging() const = 0;

	// Whether the debugging should be skipped for some reason
	virtual bool get_skip_debugging() const = 0;
};
} //namespace BlastBullets2D