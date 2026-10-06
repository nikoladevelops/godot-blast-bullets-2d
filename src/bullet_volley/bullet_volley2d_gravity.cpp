// Gravity and linear drag: per-bullet gravity vectors, the gravity time window and
// fall speed queries.

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletVolley2D::refresh_gravity_active() {
	gravity_active = false;
	for (const Vector2 &g : all_gravity) {
		if (g.length_squared() > 0.0) {
			gravity_active = true;
			break;
		}
	}
}

bool BulletVolley2D::gravity_window_open() const {
	if (curves_elapsed_time < gravity_delay_sec) {
		return false;
	}
	if (gravity_duration_sec > 0.0 && curves_elapsed_time >= gravity_delay_sec + gravity_duration_sec) {
		return false;
	}
	return true;
}

real_t BulletVolley2D::gravity_strength_scale_for_bullet(const BulletCurvesData2D *shared, const BulletCurvesData2D *per_bullet) const {
	const BulletCurvesData2D *src = nullptr;
	if (per_bullet != nullptr && per_bullet->gravity_strength_curve.is_valid()) {
		src = per_bullet;
	} else if (shared != nullptr && shared->gravity_strength_curve.is_valid()) {
		src = shared;
	} else {
		return 1.0;
	}
	const bool use_unit = src->gravity_use_unit_curve && !is_life_time_infinite;
	const real_t sampled = src->gravity_strength_curve->sample_baked(curve_get_input_value(use_unit));
	return Math::is_finite(sampled) ? sampled : 1.0;
}

void BulletVolley2D::set_gravity(const Vector2 &value) {
	if (!value.is_finite()) {
		UtilityFunctions::push_error("BulletVolley2D.set_gravity: value must be finite, keeping the old value.");
		return;
	}
	gravity = value;
	// Fill-gaps semantics (same contract as the speed/rotation shared
	// setters): slots with a user-authored per-bullet entry keep it;
	// only genuine gaps take the shared value. Changed slots restart
	// their integrated fall speed (new regime); untouched slots keep
	// integrating (no regime change for them). A slot that took the
	// shared value stays a gap (bit clear): the NEXT shared write must
	// still reach it, otherwise only the first set_gravity() works.
	// Same-value writes only refresh the member, never the fall speed.
	if ((int)has_per_bullet_gravity.size() != amount_bullets) {
		has_per_bullet_gravity.assign(amount_bullets, 0);
	}
	if ((int)all_gravity.size() != amount_bullets || (int)all_gravity_velocity.size() != amount_bullets) {
		all_gravity.assign(amount_bullets, Vector2(0, 0));
		all_gravity_velocity.assign(amount_bullets, Vector2(0, 0));
	}
	for (int i = 0; i < amount_bullets; ++i) {
		if (has_per_bullet_gravity[i]) {
			continue;
		}
		if (all_gravity[i] != value) {
			all_gravity_velocity[i] = Vector2(0, 0);
		}
		all_gravity[i] = value;
	}
	refresh_gravity_active();
}

Vector2 BulletVolley2D::bullet_get_gravity(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "bullet_get_gravity")) {
		return Vector2(0, 0);
	}
	if (bullet_index >= (int)all_gravity.size()) {
		return Vector2(0, 0);
	}
	return all_gravity[bullet_index];
}

void BulletVolley2D::bullet_set_gravity(int bullet_index, const Vector2 &value) {
	if (!validate_bullet_index(bullet_index, "bullet_set_gravity")) {
		return;
	}
	if (!value.is_finite()) {
		UtilityFunctions::push_error("BulletVolley2D.bullet_set_gravity: value must be finite, keeping the old value.");
		return;
	}
	if (bullet_index < 0 || bullet_index >= (int)all_gravity.size() || bullet_index >= (int)all_gravity_velocity.size()) {
		return;
	}
	all_gravity[bullet_index] = value;
	all_gravity_velocity[bullet_index] = Vector2(0, 0);
	// A direct per-bullet write claims presence like a seeded entry, so
	// a later set_gravity() cannot silently undo it.
	if ((int)has_per_bullet_gravity.size() != amount_bullets) {
		has_per_bullet_gravity.assign(amount_bullets, 0);
	}
	has_per_bullet_gravity[bullet_index] = 1;
	refresh_gravity_active();
}

void BulletVolley2D::all_bullets_set_gravity(const Vector2 &value, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_gravity");
	if (!value.is_finite()) {
		UtilityFunctions::push_error("BulletVolley2D.all_bullets_set_gravity: value must be finite, keeping old values.");
		return;
	}
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		bullet_set_gravity(i, value);
	}
}

TypedArray<Vector2> BulletVolley2D::all_bullets_get_gravity(int bullet_index_start, int bullet_index_end_inclusive) const {
	return collect_range<TypedArray<Vector2>>(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_gravity", [&](int i) { return bullet_get_gravity(i); });
}

void BulletVolley2D::set_gravity_delay_sec(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolley2D.set_gravity_delay_sec: value must be finite and >= 0, keeping the old value.");
		return;
	}
	gravity_delay_sec = value;
}

void BulletVolley2D::set_gravity_duration_sec(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolley2D.set_gravity_duration_sec: value must be finite and >= 0 (0 = infinite), keeping the old value.");
		return;
	}
	gravity_duration_sec = value;
}

real_t BulletVolley2D::bullet_get_fall_speed(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "bullet_get_fall_speed")) {
		return 0.0;
	}
	if (bullet_index >= (int)all_gravity_velocity.size()) {
		return 0.0;
	}
	return all_gravity_velocity[bullet_index].length();
}

TypedArray<real_t> BulletVolley2D::all_bullets_get_fall_speed(int bullet_index_start, int bullet_index_end_inclusive) const {
	return collect_range<TypedArray<real_t>>(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_fall_speed", [&](int i) { return bullet_get_fall_speed(i); });
}

void BulletVolley2D::set_linear_drag(real_t value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolley2D.set_linear_drag: value must be finite and >= 0, keeping the old value.");
		return;
	}
	linear_drag = value;
}

void BulletVolley2D::apply_gravity_from_data(const BulletVolleyData2D &volley_data) {
	all_gravity.assign(amount_bullets, Vector2(0, 0));
	all_gravity_velocity.assign(amount_bullets, Vector2(0, 0));
	gravity_delay_sec = 0.0;
	gravity_duration_sec = 0.0;
	// Windows validate like their homing twins; bad values keep 0 (off/immediate).
	if (Math::is_finite(volley_data.gravity_delay_sec) && volley_data.gravity_delay_sec >= 0.0) {
		gravity_delay_sec = (real_t)volley_data.gravity_delay_sec;
	} else {
		UtilityFunctions::push_error("BulletVolleyData2D gravity_delay_sec must be finite and >= 0, using 0 (immediate).");
	}
	if (Math::is_finite(volley_data.gravity_duration_sec) && volley_data.gravity_duration_sec >= 0.0) {
		gravity_duration_sec = (real_t)volley_data.gravity_duration_sec;
	} else {
		UtilityFunctions::push_error("BulletVolleyData2D gravity_duration_sec must be finite and >= 0 (0 = infinite), using 0 (infinite).");
	}
	gravity = volley_data.gravity;
	if (!gravity.is_finite()) {
		UtilityFunctions::push_error("BulletVolleyData2D gravity must be finite, using (0, 0).");
		gravity = Vector2(0, 0);
	}
	// Strict: entry i pulls bullet i only. Uncovered bullets keep the
	// shared gravity above. Tile checkbox wraps short arrays.
	const int grav_size = volley_data.all_bullet_gravity.size();
	const bool tile_grav = volley_data.tile_all_bullet_gravity;
	if (grav_size != 0 && grav_size != amount_bullets) {
		WarnOnce2D::warn(warn_data_id, 10u, grav_size, amount_bullets, "BulletVolley2D: all_bullet_gravity size (" + String::num_int64(grav_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets use gravity" + String(tile_grav ? " (tiling on: wrapping short array)." : " (check tile_all_bullet_gravity to wrap, or provide one entry per bullet)."));
	}
	// Fresh seed: presence re-derived per slot below (authored entries claim
	// it, even deliberate zeros; gaps stay fillable by set_gravity).
	has_per_bullet_gravity.assign(amount_bullets, 0);
	for (int i = 0; i < amount_bullets; ++i) {
		const int entry = tile_grav ? resolve_tiled_data_index(grav_size, i) : resolve_strict_data_index(grav_size, i);
		Vector2 g = gravity;
		bool authored = false;
		if (entry >= 0 && entry < volley_data.all_bullet_gravity.size()) {
			const Vector2 candidate = volley_data.all_bullet_gravity[entry];
			if (candidate.is_finite()) {
				g = candidate;
				authored = true;
			} else {
				UtilityFunctions::push_error("BulletVolleyData2D all_bullet_gravity[" + String::num_int64(entry) + "] is not finite, using (0, 0) for bullet index " + String::num_int64(i) + ".");
				g = Vector2(0, 0);
			}
		}
		all_gravity[i] = g;
		has_per_bullet_gravity[i] = authored ? 1 : 0;
	}
	refresh_gravity_active();
}

} // namespace BlastBullets2D
