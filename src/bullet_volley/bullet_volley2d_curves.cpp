// Bullet curves (speed/rotation/direction over time) and movement patterns
// (bullets following a Curve2D or Path2D).

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

// Resolves a movement-pattern NodePath to its Path2D curve, mirroring the
// shared-path behavior: relative to the factory with a scene-root fallback.
// Empty paths resolve to null silently (feature off for that slot); missing,
// wrong-type, or curve-less targets warn once per call site and resolve null.
static Ref<Curve2D> resolve_movement_pattern_curve(Node *p_factory, const NodePath &p_path, const String &p_context) {
	if (p_path.is_empty()) {
		return Ref<Curve2D>();
	}
	if (p_factory == nullptr) {
		UtilityFunctions::push_error("BulletVolley2D: cannot resolve " + p_context + " without a BulletFactory2D (multimesh was never spawned through one).");
		return Ref<Curve2D>();
	}
	// A Path2D node cannot live inside a Resource, so paths are stored and
	// resolved here. Relative to the factory (stable landmark, always in the
	// tree at spawn/enable time), with a scene-root fallback for editor-picked
	// paths.
	Node *node = p_factory->get_node_or_null(p_path);
	if (node == nullptr) {
		SceneTree *tree = p_factory->get_tree();
		Node *current_scene = (tree != nullptr) ? tree->get_current_scene() : nullptr;
		if (current_scene != nullptr) {
			node = current_scene->get_node_or_null(p_path);
		}
	}
	Path2D *path = (node != nullptr) ? Object::cast_to<Path2D>(node) : nullptr;
	if (path == nullptr) {
		if (node == nullptr) {
			UtilityFunctions::push_warning("BulletVolley2D: " + p_context + " target not found, movement pattern skipped.");
		} else {
			UtilityFunctions::push_warning("BulletVolley2D: " + p_context + " node is not a Path2D, movement pattern skipped.");
		}
		return Ref<Curve2D>();
	}
	const Ref<Curve2D> &curve = path->get_curve();
	if (curve.is_null()) {
		UtilityFunctions::push_warning("BulletVolley2D: " + p_context + " Path2D holds no Curve2D, movement pattern skipped.");
		return Ref<Curve2D>();
	}
	return curve;
}

void BulletVolley2D::set_shared_bullet_curves_data(const Ref<BulletCurvesData2D> &new_curves_data) {
	populate_shared_curves_related_data(new_curves_data);
}

bool BulletVolley2D::check_exists_bullet_movement_pattern_data(int bullet_index) const {
	if (bullet_index < 0 || bullet_index >= (int)all_movement_pattern_data.size()) {
		return false;
	}
	return all_movement_pattern_data[bullet_index].path_curve.is_valid();
}

_ALWAYS_INLINE_ void BulletVolley2D::populate_individual_bullet_curves_related_data(int bullet_index, const Ref<BulletCurvesData2D> &new_curves_data) {
	if (new_curves_data.is_null()) {
		if (bullet_index >= 0 && bullet_index < (int)all_bullet_curves_data.size() && all_bullet_curves_data[bullet_index].is_valid()) {
			all_bullet_curves_data[bullet_index].unref();
		}
		return;
	}

	// Vector is pre-sized to amount_bullets, direct index
	all_bullet_curves_data[bullet_index] = new_curves_data;
	Ref<BulletCurvesData2D> &curr_curves = all_bullet_curves_data[bullet_index];

	const bool is_movement_curve_valid = curr_curves->movement_speed_curve.is_valid();
	const bool is_rotation_curve_valid = curr_curves->rotation_speed_curve.is_valid();
	const bool is_x_direction_curve_valid = curr_curves->x_direction_curve.is_valid();
	const bool is_y_direction_curve_valid = curr_curves->y_direction_curve.is_valid();

	// Size this even when there's no rotation curve yet - someone can add one later and the tick reads it every frame.
	// All three rotation vectors must match (see populate_shared above).
	if ((int)all_rotation_speed.size() != amount_bullets) {
		all_rotation_speed.assign(amount_bullets, 0.0);
		all_max_rotation_speed.assign(amount_bullets, 0.0);
		all_rotation_acceleration.assign(amount_bullets, 0.0);
	}

	if (is_rotation_curve_valid) {
		all_rotation_speed[bullet_index] = get_bullet_curves_rotation_speed(curr_curves.ptr());
	}

	if (is_movement_curve_valid) {
		all_cached_speed[bullet_index] = get_bullet_curves_movement_speed(curr_curves.ptr());
	}

	auto &current_direction = all_cached_direction[bullet_index];

	if (is_x_direction_curve_valid) {
		apply_x_direction_curve(current_direction, curr_curves.ptr());
	}

	if (is_y_direction_curve_valid) {
		apply_y_direction_curve(current_direction, curr_curves.ptr());
	}

	if (is_movement_curve_valid || is_x_direction_curve_valid || is_y_direction_curve_valid) {
		all_cached_velocity[bullet_index] = all_cached_direction[bullet_index] * all_cached_speed[bullet_index] + inherited_velocity_offset;
	}
}

Ref<BulletCurvesData2D> BulletVolley2D::bullet_get_curves_data(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "bullet_get_curves_data")) {
		return Ref<BulletCurvesData2D>();
	}

	if (bullet_index < 0 || bullet_index >= (int)all_bullet_curves_data.size() || all_bullet_curves_data[bullet_index].is_null()) {
		UtilityFunctions::push_error("Invalid bullet_index at bullet_get_curves_data(). This bullet has no individual curves data, did you mean to access shared_bullet_curves_data?");
		return Ref<BulletCurvesData2D>();
	}

	return all_bullet_curves_data[bullet_index];
}

void BulletVolley2D::bullet_set_curves_data(int bullet_index, const Ref<BulletCurvesData2D> &curves_data) {
	if (!validate_bullet_index(bullet_index, "bullet_set_curves_data")) {
		return;
	}

	if (curves_data.is_null()) {
		if (bullet_index >= 0 && bullet_index < (int)all_bullet_curves_data.size()) {
			all_bullet_curves_data[bullet_index].unref();
		}
		return;
	}

	populate_individual_bullet_curves_related_data(bullet_index, curves_data);
}

void BulletVolley2D::all_bullets_set_curves_data(const Ref<BulletCurvesData2D> &curves_data, int bullet_index_start, int bullet_index_end_inclusive) {
	for_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_curves_data", [&](int i) { bullet_set_curves_data(i, curves_data); });
}

TypedArray<BulletCurvesData2D> BulletVolley2D::all_bullets_get_curves_data(int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_curves_data");

	TypedArray<BulletCurvesData2D> arr;

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(bullet_get_curves_data(i));
	}

	return arr;
}

void BulletVolley2D::all_bullets_clear_curves_data(int bullet_index_start, int bullet_index_end_inclusive) {
	for_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_clear_curves_data", [&](int i) { bullet_set_curves_data(i, Ref<BulletCurvesData2D>()); });
}

void BulletVolley2D::set_shared_movement_pattern_curve(const Ref<Curve2D> &new_curve) {
	shared_movement_pattern_curve = new_curve;
	// A fresh pattern starts every bullet at distance 0. A null curve
	// removes the feature: flags go back to defaults, matching the
	// spawn-data null handling.
	if (new_curve.is_null()) {
		shared_movement_pattern_face_movement_direction = false;
		shared_movement_pattern_repeat = true;
	}
	shared_movement_pattern_distances.assign(amount_bullets, 0.0);
}

real_t BulletVolley2D::get_curves_elapsed_time() const {
	return curves_elapsed_time;
}

void BulletVolley2D::set_curves_elapsed_time(real_t new_time) {
	// NaN/Inf here would poison every curve sample (speed/rotation/direction) with no
	// recovery, so reject non-finite time like the other movement setters do.
	if (!Math::is_finite(new_time) || new_time < 0.0) {
		UtilityFunctions::push_error("set_curves_elapsed_time: new_time must be a finite value >= 0.");
		return;
	}
	curves_elapsed_time = new_time;
}

Ref<Curve2D> BulletVolley2D::get_bullet_movement_pattern_curve(int bullet_index) const {
	if (check_exists_bullet_movement_pattern_data(bullet_index)) {
		return find_bullet_movement_pattern_data(bullet_index).path_curve;
	}

	return nullptr;
}

void BulletVolley2D::set_bullet_movement_pattern_from_path(int bullet_index, Path2D *path_holding_pattern, bool face_movement_direction, bool repeat_pattern) {
	if (path_holding_pattern == nullptr) {
		remove_bullet_movement_pattern(bullet_index);
		return;
	}

	const Ref<Curve2D> &curve = path_holding_pattern->get_curve();

	set_bullet_movement_pattern_from_curve(bullet_index, curve, face_movement_direction, repeat_pattern);
}

void BulletVolley2D::all_bullets_set_movement_pattern_from_path(Path2D *path_holding_pattern, bool face_movement_direction, bool repeat_pattern, int start_index, int end_index_inclusive) {
	ensure_indexes_match_amount_bullets_range(start_index, end_index_inclusive, "all_bullets_set_movement_pattern_from_path");

	if (path_holding_pattern == nullptr) {
		all_bullets_remove_movement_pattern(start_index, end_index_inclusive);
		return;
	}

	const Ref<Curve2D> &curve = path_holding_pattern->get_curve();

	if (curve.is_null()) {
		all_bullets_remove_movement_pattern(start_index, end_index_inclusive);
		return;
	}

	for (int i = start_index; i <= end_index_inclusive; ++i) {
		set_bullet_movement_pattern_from_curve(i, curve, face_movement_direction, repeat_pattern);
	}
}

void BulletVolley2D::set_bullet_movement_pattern_from_curve(int bullet_index, const Ref<Curve2D> &curve_pattern, bool face_movement_direction, bool repeat_pattern) {
	if (!validate_bullet_index(bullet_index, "set_bullet_movement_pattern_from_curve")) {
		return;
	}
	if (curve_pattern.is_null()) {
		remove_bullet_movement_pattern(bullet_index);
		return;
	}
	all_movement_pattern_data[bullet_index] = BulletMovementPatternData2D{ curve_pattern, face_movement_direction, repeat_pattern };
}

void BulletVolley2D::all_bullets_set_movement_pattern_from_curve(const Ref<Curve2D> &curve_pattern, bool face_movement_direction, bool repeat_pattern, int start_index, int end_index_inclusive) {
	ensure_indexes_match_amount_bullets_range(start_index, end_index_inclusive, "all_bullets_set_movement_pattern_from_curve");

	if (curve_pattern.is_null()) {
		all_bullets_remove_movement_pattern(start_index, end_index_inclusive);
		return;
	}

	for (int i = start_index; i <= end_index_inclusive; ++i) {
		set_bullet_movement_pattern_from_curve(i, curve_pattern, face_movement_direction, repeat_pattern);
	}
}

void BulletVolley2D::remove_bullet_movement_pattern(int bullet_index) {
	if (check_exists_bullet_movement_pattern_data(bullet_index)) {
		all_movement_pattern_data[bullet_index] = BulletMovementPatternData2D();
	}
}

void BulletVolley2D::all_bullets_remove_movement_pattern(int start_index, int end_index_inclusive) {
	for_range(start_index, end_index_inclusive, "all_bullets_remove_movement_pattern", [&](int i) { remove_bullet_movement_pattern(i); });
}

void BulletVolley2D::apply_per_bullet_curves_from_data(const BulletVolleyData2D &volley_data) {
	// This vector should already fit the volley, but make sure before indexing - a wrong-type spawn can skip sizing.
	if ((int)all_bullet_curves_data.size() != amount_bullets) {
		all_bullet_curves_data.assign(amount_bullets, Ref<BulletCurvesData2D>());
	}
	const int curves_size = volley_data.all_bullet_curves_data.size();
	const bool tile = volley_data.tile_all_bullet_curves_data;
	if (curves_size != 0 && curves_size != amount_bullets) {
		WarnOnce2D::warn(warn_data_id, 6u, curves_size, amount_bullets, "BulletVolley2D: all_bullet_curves_data size (" + String::num_int64(curves_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets use shared/default" + String(tile ? " (tiling on: wrapping short array)." : " (check tile_all_bullet_curves_data to wrap, or provide one entry per bullet)."));
	}
	for (int i = 0; i < amount_bullets; ++i) {
		const int entry = tile ? resolve_tiled_data_index(curves_size, i) : resolve_strict_data_index(curves_size, i);
		if (entry < 0) {
			continue; // empty or uncovered = off for this bullet
		}
		Ref<BulletCurvesData2D> curves = volley_data.all_bullet_curves_data[entry];
		if (curves.is_null()) {
			continue;
		}
		populate_individual_bullet_curves_related_data(i, curves);
	}
}

void BulletVolley2D::apply_per_bullet_movement_patterns_from_data(const BulletVolleyData2D &volley_data) {
	const int paths_size = volley_data.all_bullet_movement_pattern_paths.size();
	const bool tile_paths = volley_data.tile_all_bullet_movement_pattern_paths;
	if (paths_size != 0 && paths_size != amount_bullets) {
		WarnOnce2D::warn(warn_data_id, 7u, paths_size, amount_bullets, "BulletVolley2D: all_bullet_movement_pattern_paths size (" + String::num_int64(paths_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets use the shared pattern" + String(tile_paths ? " (tiling on: wrapping short array)." : " (check tile_all_bullet_movement_pattern_paths to wrap, or provide one entry per bullet)."));
	}
	const int face_size = volley_data.all_bullet_movement_pattern_face_movement_directions.size();
	const bool tile_face = volley_data.tile_all_bullet_movement_pattern_face_movement_directions;
	if (face_size != 0 && face_size != amount_bullets) {
		WarnOnce2D::warn(warn_data_id, 8u, face_size, amount_bullets, "BulletVolley2D: all_bullet_movement_pattern_face_movement_directions size (" + String::num_int64(face_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets use the shared face flag" + String(tile_face ? " (tiling on)." : " (check its tile box or provide one entry per bullet)."));
	}
	const int repeat_size = volley_data.all_bullet_movement_pattern_repeats.size();
	const bool tile_repeat = volley_data.tile_all_bullet_movement_pattern_repeats;
	if (repeat_size != 0 && repeat_size != amount_bullets) {
		WarnOnce2D::warn(warn_data_id, 9u, repeat_size, amount_bullets, "BulletVolley2D: all_bullet_movement_pattern_repeats size (" + String::num_int64(repeat_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets use the shared repeat flag" + String(tile_repeat ? " (tiling on)." : " (check its tile box or provide one entry per bullet)."));
	}
	for (int i = 0; i < amount_bullets; ++i) {
		const int entry = tile_paths ? resolve_tiled_data_index(paths_size, i) : resolve_strict_data_index(paths_size, i);
		if (entry < 0) {
			continue; // empty or uncovered = shared pattern (if any) for this bullet
		}
		const NodePath entry_path = volley_data.all_bullet_movement_pattern_paths[entry];
		if (entry_path.is_empty()) {
			continue;
		}
		Ref<Curve2D> curve = resolve_movement_pattern_curve(bullet_factory, entry_path, "all_bullet_movement_pattern_paths[" + String::num_int64(entry) + "]");
		if (curve.is_null()) {
			continue;
		}
		// Each flag array resolves on its own: entry i, else the shared flag.
		const int face_entry = tile_face ? resolve_tiled_data_index(face_size, i) : resolve_strict_data_index(face_size, i);
		const bool face = (face_entry >= 0 && face_entry < face_size)
				? (bool)volley_data.all_bullet_movement_pattern_face_movement_directions[face_entry]
				: volley_data.shared_movement_pattern_face_movement_direction;
		const int repeat_entry = tile_repeat ? resolve_tiled_data_index(repeat_size, i) : resolve_strict_data_index(repeat_size, i);
		const bool repeat = (repeat_entry >= 0 && repeat_entry < repeat_size)
				? (bool)volley_data.all_bullet_movement_pattern_repeats[repeat_entry]
				: volley_data.shared_movement_pattern_repeat;
		set_bullet_movement_pattern_from_curve(i, curve, face, repeat);
	}
}

void BulletVolley2D::apply_shared_movement_pattern_from_data(const BulletVolleyData2D &volley_data) {
	// Null/empty removes the feature: clear the shared slot so a previously
	// set pattern (or curves, via populate_shared below) cannot linger.
	// Distances reset so re-adding the feature later starts every bullet at 0.
	if (volley_data.shared_movement_pattern_path.is_empty()) {
		shared_movement_pattern_curve.unref();
		shared_movement_pattern_face_movement_direction = false;
		shared_movement_pattern_repeat = true;
		shared_movement_pattern_distances.assign(amount_bullets, 0.0);
		return;
	}
	Ref<Curve2D> curve = resolve_movement_pattern_curve(bullet_factory, volley_data.shared_movement_pattern_path, "shared_movement_pattern_path");
	if (curve.is_null()) {
		shared_movement_pattern_curve.unref();
		shared_movement_pattern_face_movement_direction = false;
		shared_movement_pattern_repeat = true;
		shared_movement_pattern_distances.assign(amount_bullets, 0.0);
		return;
	}
	shared_movement_pattern_curve = curve;
	shared_movement_pattern_face_movement_direction = volley_data.shared_movement_pattern_face_movement_direction;
	shared_movement_pattern_repeat = volley_data.shared_movement_pattern_repeat;
	shared_movement_pattern_distances.assign(amount_bullets, 0.0);
}

} // namespace BlastBullets2D
