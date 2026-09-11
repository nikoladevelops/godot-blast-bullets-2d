#pragma once

#include "../shared/bullet_curves_data2d.hpp"
#include "../shared/bullet_rotation_data2d.hpp"
#include "../shared/bullet_speed_data2d.hpp"
#include "./multimesh_bullets_data2d.hpp"

#include "godot_cpp/variant/node_path.hpp"

namespace BlastBullets2D {
using namespace godot;

class DirectionalBulletsData2D : public MultiMeshBulletsData2D {
	GDCLASS(DirectionalBulletsData2D, MultiMeshBulletsData2D)

public:
	// You are required to pass AT LEAST 1 BulletSpeedData2D in order for the bullets to work. If you want each bullet to have different data (different speed/max speed/ acceleration for each bullet), you would provide the same amount of BulletSpeedData2D as the .size() of the transforms. If you provide less than .size(), the bullets will use only the first BulletSpeedData2D. Note that BulletSpeedData2D has a helper static method that you can use to generate random speed data - BulletSpeedData2D.generate_random_data()
	TypedArray<BulletSpeedData2D> all_bullet_speed_data;

	// Whether each bullet's direction should be adjusted based on the rotation data provided by the user (bullet rotates, so it now moves in that direction)
	bool adjust_direction_based_on_rotation = false;

	// SHARED SPEED / ROTATION RELATED

	// Shared speed applied to every bullet at spawn/enable time, taking
	// precedence over all_bullet_speed_data when set. Null (default) disables
	// the feature and the array drives instead.
	Ref<BulletSpeedData2D> shared_bullet_speed_data;

	// Shared rotation applied to every bullet at spawn/enable time, taking
	// precedence over all_bullet_rotation_data when set. Null (default)
	// disables the feature and the array drives instead.
	Ref<BulletRotationData2D> shared_bullet_rotation_data;

	// Shared curves applied to every bullet at spawn/enable time, before any
	// per-bullet curves. Null (default) disables the feature. Tick precedence
	// (shared wins ties, per-bullet fills gaps) is unchanged.
	Ref<BulletCurvesData2D> shared_bullet_curves_data;

	// SHARED MOVEMENT PATTERN RELATED

	// Path to a Path2D in the scene tree whose Curve2D is applied as the
	// movement pattern of every bullet at spawn/enable time. A Path2D node
	// cannot live inside a Resource, so the path is stored instead and
	// resolved through the BulletFactory2D (absolute paths always work).
	// Empty (default) disables the feature.
	NodePath shared_movement_pattern_path;

	// Whether the shared movement pattern rotates bullets to face their direction of travel.
	bool shared_movement_pattern_face_movement_direction = false;

	// Whether the shared movement pattern repeats instead of stopping at the end of the curve.
	bool shared_movement_pattern_repeat = true;

	TypedArray<BulletSpeedData2D> get_all_bullet_speed_data() const;
	void set_all_bullet_speed_data(const TypedArray<BulletSpeedData2D> &new_data);

	bool get_adjust_direction_based_on_rotation() const;
	void set_adjust_direction_based_on_rotation(bool new_adjust_direction_based_on_rotation);

	Ref<BulletSpeedData2D> get_shared_bullet_speed_data() const;
	void set_shared_bullet_speed_data(const Ref<BulletSpeedData2D> &new_speed_data);

	Ref<BulletRotationData2D> get_shared_bullet_rotation_data() const;
	void set_shared_bullet_rotation_data(const Ref<BulletRotationData2D> &new_rotation_data);

	Ref<BulletCurvesData2D> get_shared_bullet_curves_data() const;
	void set_shared_bullet_curves_data(const Ref<BulletCurvesData2D> &new_curves_data);

	NodePath get_shared_movement_pattern_path() const;
	void set_shared_movement_pattern_path(const NodePath &new_path);

	bool get_shared_movement_pattern_face_movement_direction() const;
	void set_shared_movement_pattern_face_movement_direction(bool value);

	bool get_shared_movement_pattern_repeat() const;
	void set_shared_movement_pattern_repeat(bool value);

protected:
	static void _bind_methods();
};
} //namespace BlastBullets2D
