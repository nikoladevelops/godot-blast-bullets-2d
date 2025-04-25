#pragma once

#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/transform2d.hpp>
#include <vector>

namespace BlastBullets2D {
using namespace godot;

    /// Any class that inherits this will be able to be debugged by the bullets debugger,
    /// basically reduces coupling a bit
    class IDebuggerDataProvider2D {
    public:
        virtual ~IDebuggerDataProvider2D() = default;

        // Gets the collision shape amount_bullets that all collision shapes share
        virtual const Vector2 get_collision_shape_size_for_debugging() const = 0;

        // Gets all collision shapes' global transforms
        virtual const std::vector<Transform2D>& get_all_collision_shape_transforms_for_debugging() const = 0; // Reference here for performance reasons I don't want copies - ensure that the vector is an actual lvalue that is a member of the class / doesn't get freed accidentally
        
        // Whether the debugging should be skipped for some reason
        virtual bool get_skip_debugging() const = 0;
    };
}