#pragma once

#include "godot_cpp/classes/curve2d.hpp"
#include "godot_cpp/classes/path2d.hpp"

using namespace godot;

struct BulletMovementPatternData2D {
	Ref<Curve2D> path_curve;
	real_t distance_traveled = 0.0;
	bool face_movement_direction = false;
	bool repeat_pattern = true;

	BulletMovementPatternData2D() = default;

	BulletMovementPatternData2D(const Ref<Curve2D> &new_curve, bool new_face_movement_direction, bool new_repeat_pattern) :
			path_curve(new_curve), distance_traveled(0.0), face_movement_direction(new_face_movement_direction), repeat_pattern(new_repeat_pattern) {}
};
