#pragma once

#include "godot_cpp/classes/curve.hpp"
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace BlastBullets2D {
using namespace godot;

class BulletCurvesData2D : public Resource {
	GDCLASS(BulletCurvesData2D, Resource)

public:
	Ref<Curve> movement_speed_curve;
	Ref<Curve> rotation_speed_curve;
	Ref<Curve> direction_curve;

	bool movement_use_unit_curve = true;
	bool rotation_use_unit_curve = true;

	Ref<Curve> get_movement_speed_curve() const;
	void set_movement_speed_curve(const Ref<Curve> &curve);

	Ref<Curve> get_rotation_speed_curve() const;
	void set_rotation_speed_curve(const Ref<Curve> &curve);

	Ref<Curve> get_direction_curve() const;
	void set_direction_curve(const Ref<Curve> &curve);

	bool get_movement_use_unit_curve() const;
	void set_movement_use_unit_curve(bool value);

	bool get_rotation_use_unit_curve() const;
	void set_rotation_use_unit_curve(bool value);

protected:
	static void _bind_methods();
};
} //namespace BlastBullets2D
