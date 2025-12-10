#pragma once

#include "godot_cpp/classes/curve2d.hpp"
#include "godot_cpp/classes/path2d.hpp"
#include "godot_cpp/variant/vector2.hpp"

using namespace godot;

struct BulletMovementPatternData2D{
    Ref<Curve2D> path_curve;
    real_t distance_traveled;

    // Stored once when movement begins
    Vector2 path_origin;

    BulletMovementPatternData2D(){}
    BulletMovementPatternData2D(Ref<Curve2D> new_path_curve, Vector2 new_path_origin) :
			path_curve(new_path_curve), distance_traveled(0.0), path_origin(new_path_origin) {}
};
