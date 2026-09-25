#pragma once

#include "godot_cpp/classes/curve.hpp"
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace BlastBullets2D {
using namespace godot;

enum DirectionCurveMode {
	Additive,
	Override
};

class BulletCurvesData2D : public Resource {
	GDCLASS(BulletCurvesData2D, Resource)

public:
	Ref<Curve> movement_speed_curve;
	bool movement_use_unit_curve = true;

	Ref<Curve> rotation_speed_curve;
	bool rotation_use_unit_curve = true;

	Ref<Curve> x_direction_curve;
	bool x_direction_use_unit_curve = true;
	real_t x_direction_curve_strength = 1.0;
	DirectionCurveMode x_direction_curve_mode = DirectionCurveMode::Additive;

	Ref<Curve> y_direction_curve;
	bool y_direction_use_unit_curve = true;
	real_t y_direction_curve_strength = 1.0;
	DirectionCurveMode y_direction_curve_mode = DirectionCurveMode::Additive;

	// Gravity strength scale over the volley life (multiplies the per-bullet
	// gravity vector each tick). Null = full strength always. Follows the same
	// unit/absolute sampling as the other curves; a non-finite sample reads
	// as 1.0 (neutral) so a broken curve can never brick the fall.
	Ref<Curve> gravity_strength_curve;
	bool gravity_use_unit_curve = true;

	bool rotate_towards_adjusted_direction = true;
	real_t direction_curve_rotation_speed = 18.0f;

	Ref<Curve> get_movement_speed_curve() const;
	void set_movement_speed_curve(const Ref<Curve> &curve);

	Ref<Curve> get_rotation_speed_curve() const;
	void set_rotation_speed_curve(const Ref<Curve> &curve);

	Ref<Curve> get_x_direction_curve() const;
	void set_x_direction_curve(const Ref<Curve> &curve);

	bool get_movement_use_unit_curve() const;
	void set_movement_use_unit_curve(bool value);

	bool get_rotation_use_unit_curve() const;
	void set_rotation_use_unit_curve(bool value);

	bool get_x_direction_use_unit_curve() const;
	void set_x_direction_use_unit_curve(bool value);

	real_t get_x_direction_curve_strength() const;
	void set_x_direction_curve_strength(real_t value);

	DirectionCurveMode get_x_direction_curve_mode() const;
	void set_x_direction_curve_mode(DirectionCurveMode mode);

	bool get_rotate_towards_adjusted_direction() const;
	void set_rotate_towards_adjusted_direction(bool value);

	real_t get_direction_curve_rotation_speed() const;
	void set_direction_curve_rotation_speed(real_t value);

	Ref<Curve> get_y_direction_curve() const;
	void set_y_direction_curve(const Ref<Curve> &curve);

	Ref<Curve> get_gravity_strength_curve() const;
	void set_gravity_strength_curve(const Ref<Curve> &curve);

	bool get_gravity_use_unit_curve() const;
	void set_gravity_use_unit_curve(bool value);

	bool get_y_direction_use_unit_curve() const;
	void set_y_direction_use_unit_curve(bool value);

	real_t get_y_direction_curve_strength() const;
	void set_y_direction_curve_strength(real_t value);

	DirectionCurveMode get_y_direction_curve_mode() const;
	void set_y_direction_curve_mode(DirectionCurveMode mode);

protected:
	static void _bind_methods();
};
} //namespace BlastBullets2D

VARIANT_ENUM_CAST(BlastBullets2D::DirectionCurveMode);
