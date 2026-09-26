#include "directional_bullets2d.hpp"

#include "../spawn-data/directional_bullets_data2d.hpp"
#include "godot_cpp/classes/scene_tree.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/object.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "godot_cpp/variant/vector2.hpp"
#include "spawn-data/multimesh_bullets_data2d.hpp"
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace BlastBullets2D {

DirectionalBullets2D::~DirectionalBullets2D() {
	for (auto &queue : all_bullet_homing_targets) {
		queue.clear_homing_targets(cached_mouse_global_position);
	}
	shared_homing_deque.clear_homing_targets(cached_mouse_global_position);
}

void DirectionalBullets2D::set_up_movement_data(const TypedArray<BulletSpeedData2D> &new_speed_data, bool tile_short_arrays) {
	int speed_data_size = new_speed_data.size();

	// Ensure vectors are the correct size before we start indexing
	if ((int)all_cached_speed.size() != amount_bullets) {
		all_cached_speed.resize(amount_bullets);
		all_cached_max_speed.resize(amount_bullets);
		all_cached_acceleration.resize(amount_bullets);
		all_cached_direction.resize(amount_bullets);
		all_cached_velocity.resize(amount_bullets);
	}
	// Fresh ballistics every seed - leftover fall speed from the last owner would make the new volley drop instantly.
	all_gravity_velocity.assign(amount_bullets, Vector2(0, 0));

	// Strict rule: entry i drives bullet i only. Slots past the end keep
	// zeros here and fall back to shared afterwards (see the gap filler
	// below). With the tile checkbox, short arrays wrap (i % size).
	const bool exact_speed = (speed_data_size == amount_bullets);
	const bool tiled_speed = tile_short_arrays && speed_data_size > 0 && !exact_speed;
	Ref<BulletSpeedData2D> fallback_data;

	if (!exact_speed) {
		if (tiled_speed && speed_data_size > 0) {
			fallback_data = new_speed_data[0];
		}
		if (speed_data_size != 0 && !exact_speed) {
			UtilityFunctions::push_warning("DirectionalBullets2D: all_bullet_speed_data size (" + String::num_int64(speed_data_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets fall back to shared/default" + String(tile_short_arrays ? " (tiling on: wrapping short array)." : " (check tile_all_bullet_speed_data to wrap, or provide one entry per bullet)."));
		}

		// In case no speed data was provided at all, create a default one (everything set to 0 by default)
		if (fallback_data.is_null()) {
			fallback_data.instantiate();
		}
	}

	for (int i = 0; i < amount_bullets; ++i) {
		const real_t rot = all_cached_shape_transforms[i].get_rotation();

		Ref<BulletSpeedData2D> data = fallback_data;

		if (exact_speed) {
			data = new_speed_data[i];
		} else if (tiled_speed && speed_data_size > 0) {
			data = new_speed_data[i % speed_data_size];
		} else if (i >= 0 && i < speed_data_size) {
			data = new_speed_data[i];
		}

		// Extract values with null safety
		real_t s = 0.0, m = 0.0, acc = 0.0;

		if (data.is_valid()) {
			s = data->speed;
			m = data->max_speed;
			acc = data->acceleration;
		}

		// Non-finite values would poison the tick path, so fail open to zeros like a missing entry.
		if (!Math::is_finite(s) || !Math::is_finite(m) || !Math::is_finite(acc)) {
			UtilityFunctions::push_error("DirectionalBullets2D movement data contains NaN/Inf, using zeros for bullet index " + String::num_int64(i) + ".");
			s = 0.0;
			m = 0.0;
			acc = 0.0;
		}

		// Overwrite existing memory slots
		all_cached_speed[i] = s;
		all_cached_max_speed[i] = m;
		all_cached_acceleration[i] = acc;

		Vector2 dir = Vector2(Math::cos(rot), Math::sin(rot));
		all_cached_direction[i] = dir;
		all_cached_velocity[i] = (dir * s) + inherited_velocity_offset;
	}
}

void DirectionalBullets2D::apply_shared_speed_fallback(const Ref<BulletSpeedData2D> &shared) {
	if (shared.is_null() || !Math::is_finite(shared->speed) || !Math::is_finite(shared->max_speed) || !Math::is_finite(shared->acceleration)) {
		UtilityFunctions::push_error("DirectionalBullets2D shared_bullet_speed_data contains NaN/Inf, ignoring shared fallback.");
		return;
	}
	if ((int)all_cached_speed.size() != amount_bullets) {
		return;
	}
	for (int i = 0; i < amount_bullets; ++i) {
		// Gap = slot seeded from an invalid (null/non-finite) per-bullet
		// entry. Valid per-bullet slots (including deliberate zeros) stay.
		// Heuristic: per-bullet seeding zeroes the whole triple on invalid
		// entries, so a fully-zero triple is the gap marker. Shared all-zero
		// would be a no-op anyway, so skip the write then.
		if (all_cached_speed[i] != 0.0 || all_cached_max_speed[i] != 0.0 || all_cached_acceleration[i] != 0.0) {
			continue;
		}
		if (shared->speed == 0.0 && shared->max_speed == 0.0 && shared->acceleration == 0.0) {
			continue;
		}
		all_cached_speed[i] = shared->speed;
		all_cached_max_speed[i] = shared->max_speed;
		all_cached_acceleration[i] = shared->acceleration;
		const real_t rot = (i >= 0 && i < (int)all_cached_shape_transforms.size()) ? all_cached_shape_transforms[i].get_rotation() : 0.0;
		Vector2 dir = Vector2(Math::cos(rot), Math::sin(rot));
		all_cached_direction[i] = dir;
		all_cached_velocity[i] = (dir * shared->speed) + inherited_velocity_offset;
	}
}

void DirectionalBullets2D::apply_shared_rotation_fallback(const Ref<BulletRotationData2D> &shared, bool new_rotate_only_textures) {
	if (shared.is_null() || !Math::is_finite(shared->rotation_speed) || !Math::is_finite(shared->max_rotation_speed) || !Math::is_finite(shared->rotation_acceleration)) {
		UtilityFunctions::push_error("DirectionalBullets2D shared_bullet_rotation_data contains NaN/Inf, ignoring shared fallback.");
		return;
	}
	// No rotation seeded at all (empty array path): shared seeds everything.
	// Fan out to all N slots directly (a 1-entry set_rotation_data call
	// would only cover slot 0 under strict indexing). This also activates
	// rotation so the tick spins every slot, not just slot 0.
	if (!is_rotation_data_active) {
		if ((int)all_rotation_speed.size() != amount_bullets) {
			all_rotation_speed.assign(amount_bullets, 0.0);
			all_max_rotation_speed.assign(amount_bullets, 0.0);
			all_rotation_acceleration.assign(amount_bullets, 0.0);
		}
		for (int i = 0; i < amount_bullets; ++i) {
			all_rotation_speed[i] = shared->rotation_speed;
			all_max_rotation_speed[i] = shared->max_rotation_speed;
			all_rotation_acceleration[i] = shared->rotation_acceleration;
		}
		is_rotation_data_active = true;
		use_only_first_rotation_data = false;
		rotate_only_textures = new_rotate_only_textures;
		return;
	}
	// Rotation active from per-bullet seeding: only fill slots that hold a
	// fully-zero triple (the invalid-entry gap marker). Valid per-bullet
	// slots, including deliberate zeros mixed with non-zero siblings, stay.
	if ((int)all_rotation_speed.size() != amount_bullets) {
		return;
	}
	bool filled_any = false;
	for (int i = 0; i < amount_bullets; ++i) {
		if (all_rotation_speed[i] != 0.0 || all_max_rotation_speed[i] != 0.0 || all_rotation_acceleration[i] != 0.0) {
			continue;
		}
		if (shared->rotation_speed == 0.0 && shared->max_rotation_speed == 0.0 && shared->rotation_acceleration == 0.0) {
			continue;
		}
		all_rotation_speed[i] = shared->rotation_speed;
		all_max_rotation_speed[i] = shared->max_rotation_speed;
		all_rotation_acceleration[i] = shared->rotation_acceleration;
		filled_any = true;
	}
	// Visual follow mode only changes when the fallback actually filled
	// something; flipping it on a no-op write would silently re-target
	// shapes mid-flight.
	if (filled_any) {
		rotate_only_textures = new_rotate_only_textures;
	}
}

void DirectionalBullets2D::apply_per_bullet_curves_from_data(const DirectionalBulletsData2D &directional_data) {
	// This vector should already fit the volley, but make sure before indexing - a wrong-type spawn can skip sizing.
	if ((int)all_bullet_curves_data.size() != amount_bullets) {
		all_bullet_curves_data.assign(amount_bullets, Ref<BulletCurvesData2D>());
	}
	const int curves_size = directional_data.all_bullet_curves_data.size();
	const bool tile = directional_data.tile_all_bullet_curves_data;
	if (curves_size != 0 && curves_size != amount_bullets) {
		UtilityFunctions::push_warning("DirectionalBullets2D: all_bullet_curves_data size (" + String::num_int64(curves_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets use shared/default" + String(tile ? " (tiling on: wrapping short array)." : " (check tile_all_bullet_curves_data to wrap, or provide one entry per bullet)."));
	}
	for (int i = 0; i < amount_bullets; ++i) {
		const int entry = tile ? resolve_tiled_data_index(curves_size, i) : resolve_strict_data_index(curves_size, i);
		if (entry < 0) {
			continue; // empty or uncovered = off for this bullet
		}
		Ref<BulletCurvesData2D> curves = directional_data.all_bullet_curves_data[entry];
		if (curves.is_null()) {
			continue;
		}
		populate_individual_bullet_curves_related_data(i, curves);
	}
}

// Resolves a movement-pattern NodePath to its Path2D curve, mirroring the
// shared-path behavior: relative to the factory with a scene-root fallback.
// Empty paths resolve to null silently (feature off for that slot); missing,
// wrong-type, or curve-less targets warn once per call site and resolve null.
static Ref<Curve2D> resolve_movement_pattern_curve(Node *p_factory, const NodePath &p_path, const String &p_context) {
	if (p_path.is_empty()) {
		return Ref<Curve2D>();
	}
	if (p_factory == nullptr) {
		UtilityFunctions::push_error("DirectionalBullets2D: cannot resolve " + p_context + " without a BulletFactory2D (multimesh was never spawned through one).");
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
			UtilityFunctions::push_warning("DirectionalBullets2D: " + p_context + " target not found, movement pattern skipped.");
		} else {
			UtilityFunctions::push_warning("DirectionalBullets2D: " + p_context + " node is not a Path2D, movement pattern skipped.");
		}
		return Ref<Curve2D>();
	}
	const Ref<Curve2D> &curve = path->get_curve();
	if (curve.is_null()) {
		UtilityFunctions::push_warning("DirectionalBullets2D: " + p_context + " Path2D holds no Curve2D, movement pattern skipped.");
		return Ref<Curve2D>();
	}
	return curve;
}

void DirectionalBullets2D::apply_per_bullet_movement_patterns_from_data(const DirectionalBulletsData2D &directional_data) {
	const int paths_size = directional_data.all_bullet_movement_pattern_paths.size();
	const bool tile_paths = directional_data.tile_all_bullet_movement_pattern_paths;
	if (paths_size != 0 && paths_size != amount_bullets) {
		UtilityFunctions::push_warning("DirectionalBullets2D: all_bullet_movement_pattern_paths size (" + String::num_int64(paths_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets use the shared pattern" + String(tile_paths ? " (tiling on: wrapping short array)." : " (check tile_all_bullet_movement_pattern_paths to wrap, or provide one entry per bullet)."));
	}
	const int face_size = directional_data.all_bullet_movement_pattern_face_movement_directions.size();
	const bool tile_face = directional_data.tile_all_bullet_movement_pattern_face_movement_directions;
	if (face_size != 0 && face_size != amount_bullets) {
		UtilityFunctions::push_warning("DirectionalBullets2D: all_bullet_movement_pattern_face_movement_directions size (" + String::num_int64(face_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets use the shared face flag" + String(tile_face ? " (tiling on)." : " (check its tile box or provide one entry per bullet)."));
	}
	const int repeat_size = directional_data.all_bullet_movement_pattern_repeats.size();
	const bool tile_repeat = directional_data.tile_all_bullet_movement_pattern_repeats;
	if (repeat_size != 0 && repeat_size != amount_bullets) {
		UtilityFunctions::push_warning("DirectionalBullets2D: all_bullet_movement_pattern_repeats size (" + String::num_int64(repeat_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets use the shared repeat flag" + String(tile_repeat ? " (tiling on)." : " (check its tile box or provide one entry per bullet)."));
	}
	for (int i = 0; i < amount_bullets; ++i) {
		const int entry = tile_paths ? resolve_tiled_data_index(paths_size, i) : resolve_strict_data_index(paths_size, i);
		if (entry < 0) {
			continue; // empty or uncovered = shared pattern (if any) for this bullet
		}
		const NodePath entry_path = directional_data.all_bullet_movement_pattern_paths[entry];
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
				? (bool)directional_data.all_bullet_movement_pattern_face_movement_directions[face_entry]
				: directional_data.shared_movement_pattern_face_movement_direction;
		const int repeat_entry = tile_repeat ? resolve_tiled_data_index(repeat_size, i) : resolve_strict_data_index(repeat_size, i);
		const bool repeat = (repeat_entry >= 0 && repeat_entry < repeat_size)
				? (bool)directional_data.all_bullet_movement_pattern_repeats[repeat_entry]
				: directional_data.shared_movement_pattern_repeat;
		set_bullet_movement_pattern_from_curve(i, curve, face, repeat);
	}
}

Dictionary DirectionalBullets2D::debug_get_gravity_info(int bullet_index) const {
	Dictionary d;
	d["vector"] = Vector2(0, 0);
	d["fall_speed"] = 0.0;
	d["window_active"] = false;
	d["curve_scale"] = 1.0;
	if (bullet_index < 0 || bullet_index >= amount_bullets) {
		return d;
	}
	if (bullet_index >= 0 && bullet_index < (int)all_gravity.size()) {
		d["vector"] = all_gravity[bullet_index];
	}
	if (bullet_index >= 0 && bullet_index < (int)all_gravity_velocity.size()) {
		d["fall_speed"] = all_gravity_velocity[bullet_index].length();
	}
	d["window_active"] = gravity_window_open();
	const BulletCurvesData2D *grav_shared = shared_bullet_curves_data.is_valid() ? shared_bullet_curves_data.ptr() : nullptr;
	const BulletCurvesData2D *grav_per = (bullet_index >= 0 && bullet_index < (int)all_bullet_curves_data.size() && all_bullet_curves_data[bullet_index].is_valid()) ? all_bullet_curves_data[bullet_index].ptr() : nullptr;
	d["curve_scale"] = gravity_strength_scale_for_bullet(grav_shared, grav_per);
	return d;
}

void DirectionalBullets2D::apply_gravity_from_data(const DirectionalBulletsData2D &directional_data) {
	all_gravity.assign(amount_bullets, Vector2(0, 0));
	all_gravity_velocity.assign(amount_bullets, Vector2(0, 0));
	gravity_delay_sec = 0.0;
	gravity_duration_sec = 0.0;
	// Windows validate like their homing twins; bad values keep 0 (off/immediate).
	if (Math::is_finite(directional_data.gravity_delay_sec) && directional_data.gravity_delay_sec >= 0.0) {
		gravity_delay_sec = (real_t)directional_data.gravity_delay_sec;
	} else {
		UtilityFunctions::push_error("DirectionalBulletsData2D gravity_delay_sec must be finite and >= 0, using 0 (immediate).");
	}
	if (Math::is_finite(directional_data.gravity_duration_sec) && directional_data.gravity_duration_sec >= 0.0) {
		gravity_duration_sec = (real_t)directional_data.gravity_duration_sec;
	} else {
		UtilityFunctions::push_error("DirectionalBulletsData2D gravity_duration_sec must be finite and >= 0 (0 = infinite), using 0 (infinite).");
	}
	gravity = directional_data.gravity;
	if (!gravity.is_finite()) {
		UtilityFunctions::push_error("DirectionalBulletsData2D gravity must be finite, using (0, 0).");
		gravity = Vector2(0, 0);
	}
	// Strict: entry i pulls bullet i only. Uncovered bullets keep the
	// shared gravity above. Tile checkbox wraps short arrays.
	const int grav_size = directional_data.all_bullet_gravity.size();
	const bool tile_grav = directional_data.tile_all_bullet_gravity;
	if (grav_size != 0 && grav_size != amount_bullets) {
		UtilityFunctions::push_warning("DirectionalBullets2D: all_bullet_gravity size (" + String::num_int64(grav_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets use gravity" + String(tile_grav ? " (tiling on: wrapping short array)." : " (check tile_all_bullet_gravity to wrap, or provide one entry per bullet)."));
	}
	for (int i = 0; i < amount_bullets; ++i) {
		const int entry = tile_grav ? resolve_tiled_data_index(grav_size, i) : resolve_strict_data_index(grav_size, i);
		Vector2 g = gravity;
		if (entry >= 0 && entry < directional_data.all_bullet_gravity.size()) {
			const Vector2 candidate = directional_data.all_bullet_gravity[entry];
			if (candidate.is_finite()) {
				g = candidate;
			} else {
				UtilityFunctions::push_error("DirectionalBulletsData2D all_bullet_gravity[" + String::num_int64(entry) + "] is not finite, using (0, 0) for bullet index " + String::num_int64(i) + ".");
				g = Vector2(0, 0);
			}
		}
		all_gravity[i] = g;
	}
}

void DirectionalBullets2D::apply_wobble_from_data(const DirectionalBulletsData2D &directional_data) {
	all_bullet_wobble.assign(amount_bullets, WobbleSeed());
	all_bullet_wobble_data.assign(amount_bullets, Ref<BulletWobbleData2D>());
	wobble_distance_traveled.assign(amount_bullets, 0.0);
	is_wobble_feature_enabled = false;
	shared_bullet_wobble_data = directional_data.shared_bullet_wobble_data;
	// Strict: entry i seeds bullet i only, and a live seed always beats
	// shared for its bullet. Missing/null/disabled entries fall back to
	// shared per bullet. Tile checkbox wraps short arrays.
	const int wobble_size = directional_data.all_bullet_wobble_data.size();
	const bool tile_wobble = directional_data.tile_all_bullet_wobble_data;
	if (wobble_size != 0 && wobble_size != amount_bullets) {
		UtilityFunctions::push_warning("DirectionalBullets2D: all_bullet_wobble_data size (" + String::num_int64(wobble_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets use shared wobble" + String(tile_wobble ? " (tiling on: wrapping short array)." : " (check tile_all_bullet_wobble_data to wrap, or provide one entry per bullet)."));
	}
	for (int i = 0; i < amount_bullets; ++i) {
		const int entry = tile_wobble ? resolve_tiled_data_index(wobble_size, i) : resolve_strict_data_index(wobble_size, i);
		if (entry >= 0 && entry < wobble_size) {
			const Ref<BulletWobbleData2D> res = directional_data.all_bullet_wobble_data[entry];
			WobbleSeed seed = make_wobble_seed(res, i);
			if (seed.active) {
				all_bullet_wobble[i] = seed;
				all_bullet_wobble_data[i] = res;
				continue;
			}
		} else if (entry < 0 && wobble_size == 0) {
			// empty array: fall through to shared below
		}
		if (shared_bullet_wobble_data.is_valid()) {
			all_bullet_wobble[i] = make_wobble_seed(shared_bullet_wobble_data, i);
		}
	}
	refresh_wobble_feature_flag();
}

void DirectionalBullets2D::bullet_set_wobble_data(int bullet_index, const Ref<BulletWobbleData2D> &wobble_data) {
	if (!validate_bullet_index(bullet_index, "bullet_set_wobble_data")) {
		return;
	}
	if ((int)all_bullet_wobble.size() != amount_bullets || (int)all_bullet_wobble_data.size() != amount_bullets) {
		UtilityFunctions::push_error("bullet_set_wobble_data: wobble storage is not set up for this multimesh.");
		return;
	}
	if (wobble_data.is_null()) {
		// Clear back to the shared fallback (or inactive when unset).
		all_bullet_wobble_data[bullet_index].unref();
		if (shared_bullet_wobble_data.is_valid()) {
			all_bullet_wobble[bullet_index] = make_wobble_seed(shared_bullet_wobble_data, bullet_index);
		} else {
			all_bullet_wobble[bullet_index] = WobbleSeed();
		}
		refresh_wobble_feature_flag();
		return;
	}
	WobbleSeed seed = make_wobble_seed(wobble_data, bullet_index);
	if (!seed.active) {
		UtilityFunctions::push_error("bullet_set_wobble_data: wobble data is disabled or invalid, slot cleared to the shared fallback.");
		all_bullet_wobble_data[bullet_index].unref();
		if (shared_bullet_wobble_data.is_valid()) {
			all_bullet_wobble[bullet_index] = make_wobble_seed(shared_bullet_wobble_data, bullet_index);
		} else {
			all_bullet_wobble[bullet_index] = WobbleSeed();
		}
		refresh_wobble_feature_flag();
		return;
	}
	all_bullet_wobble[bullet_index] = seed;
	all_bullet_wobble_data[bullet_index] = wobble_data;
	refresh_wobble_feature_flag();
}

void DirectionalBullets2D::all_bullets_set_wobble_data(const Ref<BulletWobbleData2D> &wobble_data, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_wobble_data");
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		bullet_set_wobble_data(i, wobble_data);
	}
}

Ref<BulletWobbleData2D> DirectionalBullets2D::bullet_get_wobble_data(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "bullet_get_wobble_data")) {
		return Ref<BulletWobbleData2D>();
	}
	if (bullet_index < 0 || bullet_index >= (int)all_bullet_wobble_data.size()) {
		return Ref<BulletWobbleData2D>();
	}
	return all_bullet_wobble_data[bullet_index];
}

TypedArray<BulletWobbleData2D> DirectionalBullets2D::all_bullets_get_wobble_data(int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_wobble_data");
	TypedArray<BulletWobbleData2D> arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(bullet_get_wobble_data(i));
	}
	return arr;
}

void DirectionalBullets2D::set_shared_bullet_wobble_data(const Ref<BulletWobbleData2D> &new_wobble_data) {
	shared_bullet_wobble_data = new_wobble_data;
	if ((int)all_bullet_wobble.size() != amount_bullets || (int)all_bullet_wobble_data.size() != amount_bullets) {
		return;
	}
	// Shared is the fallback: re-seed only slots without a live per-bullet
	// resource. Clearing shared (null) deactivates exactly those slots.
	for (int i = 0; i < amount_bullets; ++i) {
		if (all_bullet_wobble_data[i].is_valid()) {
			continue;
		}
		if (new_wobble_data.is_valid()) {
			all_bullet_wobble[i] = make_wobble_seed(new_wobble_data, i);
		} else {
			all_bullet_wobble[i] = WobbleSeed();
		}
	}
	refresh_wobble_feature_flag();
}

void DirectionalBullets2D::remove_shared_bullet_wobble_data() {
	set_shared_bullet_wobble_data(Ref<BulletWobbleData2D>());
}

Dictionary DirectionalBullets2D::debug_get_wobble_info(int bullet_index) const {
	Dictionary d;
	d["active"] = false;
	d["amplitude"] = 0.0;
	d["frequency_hz"] = 0.0;
	d["mode"] = 0;
	d["waveform"] = 0;
	d["phase"] = 0.0;
	d["damping_per_sec"] = 0.0;
	d["delay_sec"] = 0.0;
	d["duration_sec"] = 0.0;
	d["face_movement_direction"] = false;
	d["face_rotation_speed"] = 0.0;
	d["has_per_bullet_resource"] = false;
	d["has_shared_fallback"] = shared_bullet_wobble_data.is_valid();
	if (bullet_index < 0 || bullet_index >= amount_bullets) {
		return d;
	}
	if (bullet_index >= 0 && bullet_index < (int)all_bullet_wobble.size()) {
		const WobbleSeed &w = all_bullet_wobble[bullet_index];
		d["active"] = w.active;
		d["amplitude"] = w.active ? w.amplitude : 0.0;
		d["frequency_hz"] = w.active ? w.frequency_hz : 0.0;
		d["mode"] = w.active ? w.mode : 0;
		d["waveform"] = w.active ? w.waveform : 0;
		d["phase"] = w.active ? w.phase : 0.0;
		d["damping_per_sec"] = w.active ? w.damping_per_sec : 0.0;
		d["delay_sec"] = w.active ? w.delay_sec : 0.0;
		d["duration_sec"] = w.active ? w.duration_sec : 0.0;
		d["face_movement_direction"] = w.active && w.face_movement_direction;
		d["face_rotation_speed"] = w.active ? w.face_rotation_speed : 0.0;
	}
	if (bullet_index >= 0 && bullet_index < (int)all_bullet_wobble_data.size()) {
		d["has_per_bullet_resource"] = all_bullet_wobble_data[bullet_index].is_valid();
	}
	return d;
}

Dictionary DirectionalBullets2D::debug_get_bullet_info(int bullet_index) const {
	Dictionary d;
	d["index"] = bullet_index;
	d["valid"] = false;
	d["active"] = false;
	d["direction"] = Vector2(0, 0);
	d["velocity"] = Vector2(0, 0);
	d["speed"] = 0.0;
	d["gravity"] = Vector2(0, 0);
	d["fall_speed"] = 0.0;
	d["wobble_active"] = false;
	d["wobble_amplitude"] = 0.0;
	d["has_homing_targets"] = false;
	d["homing_targets_amount"] = 0;
	d["homing_smoothing"] = 0.0;
	d["orbiting_enabled"] = false;
	d["orbiting_locked"] = false;
	d["pattern"] = false;
	d["shared_pattern_active"] = shared_movement_pattern_curve.is_valid();
	d["rotation_speed"] = 0.0;
	if (bullet_index < 0 || bullet_index >= amount_bullets) {
		return d;
	}
	d["valid"] = true;
	d["active"] = all_bullets_enabled_set.contains(bullet_index);
	if (bullet_index >= 0 && bullet_index < (int)all_cached_direction.size()) {
		d["direction"] = all_cached_direction[bullet_index];
	}
	if (bullet_index >= 0 && bullet_index < (int)all_cached_velocity.size()) {
		d["velocity"] = all_cached_velocity[bullet_index];
	}
	if (bullet_index >= 0 && bullet_index < (int)all_cached_speed.size()) {
		d["speed"] = all_cached_speed[bullet_index];
	}
	if (bullet_index >= 0 && bullet_index < (int)all_gravity.size()) {
		d["gravity"] = all_gravity[bullet_index];
	}
	if (bullet_index >= 0 && bullet_index < (int)all_gravity_velocity.size()) {
		d["fall_speed"] = all_gravity_velocity[bullet_index].length();
	}
	if (bullet_index >= 0 && bullet_index < (int)all_bullet_wobble.size()) {
		const WobbleSeed &w = all_bullet_wobble[bullet_index];
		d["wobble_active"] = w.active;
		d["wobble_amplitude"] = w.active ? w.amplitude : 0.0;
	}
	if (bullet_index >= 0 && bullet_index < (int)all_bullet_homing_targets.size() && bullet_index < (int)all_homing_count.size()) {
		d["has_homing_targets"] = all_homing_count[bullet_index] > 0 && !all_bullet_homing_targets[bullet_index].empty();
		d["homing_targets_amount"] = all_homing_count[bullet_index];
	}
	d["homing_smoothing"] = bullet_get_homing_smoothing(bullet_index);
	if (bullet_index >= 0 && bullet_index < (int)all_orbiting_status.size()) {
		d["orbiting_enabled"] = all_orbiting_status[bullet_index] != 0;
	}
	if (bullet_index >= 0 && bullet_index < (int)all_orbiting_data.size()) {
		d["orbiting_locked"] = all_orbiting_data[bullet_index].is_locked_orbiting;
	}
	d["pattern"] = check_exists_bullet_movement_pattern_data(bullet_index);
	if (bullet_index >= 0 && bullet_index < (int)all_rotation_speed.size()) {
		d["rotation_speed"] = all_rotation_speed[bullet_index];
	}
	return d;
}

void DirectionalBullets2D::apply_shared_movement_pattern_from_data(const DirectionalBulletsData2D &directional_data) {
	// Null/empty removes the feature: clear the shared slot so a previously
	// set pattern (or curves, via populate_shared below) cannot linger.
	// Distances reset so re-adding the feature later starts every bullet at 0.
	if (directional_data.shared_movement_pattern_path.is_empty()) {
		shared_movement_pattern_curve.unref();
		shared_movement_pattern_face_movement_direction = false;
		shared_movement_pattern_repeat = true;
		shared_movement_pattern_distances.assign(amount_bullets, 0.0);
		return;
	}
	Ref<Curve2D> curve = resolve_movement_pattern_curve(bullet_factory, directional_data.shared_movement_pattern_path, "shared_movement_pattern_path");
	if (curve.is_null()) {
		shared_movement_pattern_curve.unref();
		shared_movement_pattern_face_movement_direction = false;
		shared_movement_pattern_repeat = true;
		shared_movement_pattern_distances.assign(amount_bullets, 0.0);
		return;
	}
	shared_movement_pattern_curve = curve;
	shared_movement_pattern_face_movement_direction = directional_data.shared_movement_pattern_face_movement_direction;
	shared_movement_pattern_repeat = directional_data.shared_movement_pattern_repeat;
	shared_movement_pattern_distances.assign(amount_bullets, 0.0);
}

void DirectionalBullets2D::custom_additional_spawn_logic(const MultiMeshBulletsData2D &data) {
	const DirectionalBulletsData2D *directional_data = Object::cast_to<DirectionalBulletsData2D>(&data);
	// Size movement/homing/orbit SoA up front: the tick path indexes them
	// unconditionally, so even a wrong-type early-return must leave them sized.
	// Gravity vectors/velocities sized here too (same invariant as movement).
	set_up_movement_data(TypedArray<BulletSpeedData2D>());
	adjust_direction_based_on_rotation = false;
	homing_update_timer = 0.0;
	all_bullet_wobble.assign(amount_bullets, WobbleSeed());
	all_bullet_wobble_data.assign(amount_bullets, Ref<BulletWobbleData2D>());
	wobble_distance_traveled.assign(amount_bullets, 0.0);
	is_wobble_feature_enabled = false;
	shared_bullet_wobble_data.unref();
	gravity = Vector2(0, 0);
	all_gravity.assign(amount_bullets, Vector2(0, 0));
	all_gravity_velocity.assign(amount_bullets, Vector2(0, 0));
	gravity_delay_sec = 0.0;
	gravity_duration_sec = 0.0;
	linear_drag = 0.0;
	homing_delay_sec = 0.0;
	homing_duration_sec = 0.0;
	homing_lose_range_px = 0.0;
	all_bullet_homing_targets.resize(amount_bullets);
	all_homing_count.assign(amount_bullets, 0);
	all_bullet_homing_smoothing.assign(amount_bullets, 0.0);
	use_per_bullet_homing_smoothing = false;
	all_orbiting_data.resize(amount_bullets);
	all_orbiting_status.assign(amount_bullets, 0);
	all_shared_homing_reached.resize(amount_bullets);
	homing_inert_warning_issued = false;
	// Like enable: a fresh volley starts with zeroed homing/orbit counters and no cached mouse position.
	active_homing_count = 0;
	active_orbiting_count = 0;
	cached_mouse_global_position = Vector2(0, 0);
	// Calls scheduled by the previous owner no-op when they finally run - they carry the old generation stamp.
	++homing_operation_generation;
	bullet_homing_epochs.assign(amount_bullets, 0);
	shared_auto_pop_queued = false;
	if (directional_data == nullptr) {
		UtilityFunctions::push_error("DirectionalBullets2D::spawn got wrong spawn data type, expected DirectionalBulletsData2D.");
		return;
	}

	set_up_movement_data(directional_data->all_bullet_speed_data, directional_data->tile_all_bullet_speed_data);

	adjust_direction_based_on_rotation = directional_data->adjust_direction_based_on_rotation;

	// Unified precedence: per-bullet wins over shared. Seed per-bullet
	// first, then fill only the gaps left by invalid entries (null or
	// non-finite) from shared. Shared is the fallback default, never an
	// override. Members mirror the data so runtime getters stay truthful.
	shared_bullet_speed_data = directional_data->shared_bullet_speed_data;
	if (shared_bullet_speed_data.is_valid()) {
		apply_shared_speed_fallback(shared_bullet_speed_data);
	}
	shared_bullet_rotation_data = directional_data->shared_bullet_rotation_data;
	if (shared_bullet_rotation_data.is_valid()) {
		apply_shared_rotation_fallback(shared_bullet_rotation_data, data.rotate_only_textures);
	}

	// Per-bullet spawn-data curves/patterns first, then the shared features
	// below (separate storages, so the tick resolves precedence live).
	apply_per_bullet_curves_from_data(*directional_data);
	apply_per_bullet_movement_patterns_from_data(*directional_data);

	// Shared spawn-data features. Curves always applied (a null data unrefs
	// any stale member via populate_shared); the pattern resolver clears its
	// own slot on empty/unresolvable paths. Per-bullet runtime state set after
	// spawn still overrides afterwards.
	populate_shared_curves_related_data(directional_data->shared_bullet_curves_data);
	apply_shared_movement_pattern_from_data(*directional_data);
	apply_wobble_from_data(*directional_data);
	apply_gravity_from_data(*directional_data);

	// Homing steering seeds from the same spawn data (direct factory users
	// keep it on fresh spawns too, matching the enable path).
	homing_smoothing = (real_t)directional_data->homing_smoothing;
	homing_update_interval = (real_t)directional_data->homing_update_interval;
	homing_update_timer = 0.0;
	homing_take_control_of_texture_rotation = directional_data->homing_take_control_of_texture_rotation;
	homing_distance_before_reached = (real_t)directional_data->homing_distance_before_reached;
	bullet_homing_auto_pop_after_target_reached = directional_data->bullet_homing_auto_pop_after_target_reached;
	shared_homing_deque_auto_pop_after_target_reached = directional_data->shared_homing_deque_auto_pop_after_target_reached;
	linear_drag = (real_t)directional_data->linear_drag;
	homing_delay_sec = (real_t)directional_data->homing_delay_sec;
	homing_duration_sec = (real_t)directional_data->homing_duration_sec;
	homing_lose_range_px = (real_t)directional_data->homing_lose_range_px;
}

bool DirectionalBullets2D::is_data_type_compatible(const MultiMeshBulletsData2D &data) const {
	return Object::cast_to<DirectionalBulletsData2D>(&data) != nullptr;
}

void DirectionalBullets2D::reset_transient_subclass_state(bool drop_stale_work) {
	// Neutralize ballistics/shared/homing so a new life never inherits the
	// previous owner's values (base reset cannot clear subclass state).
	set_up_movement_data(TypedArray<BulletSpeedData2D>());
	shared_bullet_speed_data.unref();
	shared_bullet_rotation_data.unref();
	adjust_direction_based_on_rotation = false;
	homing_update_timer = 0.0;
	shared_movement_pattern_curve.unref();
	shared_movement_pattern_face_movement_direction = false;
	shared_movement_pattern_repeat = true;
	shared_movement_pattern_distances.assign(amount_bullets, 0.0);
	all_bullet_wobble.assign(amount_bullets, WobbleSeed());
	all_bullet_wobble_data.assign(amount_bullets, Ref<BulletWobbleData2D>());
	wobble_distance_traveled.assign(amount_bullets, 0.0);
	is_wobble_feature_enabled = false;
	shared_bullet_wobble_data.unref();
	gravity = Vector2(0, 0);
	all_gravity.assign(amount_bullets, Vector2(0, 0));
	all_gravity_velocity.assign(amount_bullets, Vector2(0, 0));
	gravity_delay_sec = 0.0;
	gravity_duration_sec = 0.0;
	linear_drag = 0.0;
	homing_delay_sec = 0.0;
	homing_duration_sec = 0.0;
	homing_lose_range_px = 0.0;
	clear_homing_state_for_teardown();
	if (drop_stale_work) {
		// New pooled life only: drop the last owner's signal connections. A plain wake keeps them - same owner, same listeners.
		for (const Dictionary &connection : get_signal_connection_list("bullet_homing_target_reached")) {
			const Callable callable = connection["callable"];
			disconnect("bullet_homing_target_reached", callable);
		}
		++homing_operation_generation;
		bullet_homing_epochs.assign(amount_bullets, 0);
		shared_auto_pop_queued = false;
	}
}

bool DirectionalBullets2D::custom_additional_enable_logic(const MultiMeshBulletsData2D &data) {
	const DirectionalBulletsData2D *directional_data = Object::cast_to<DirectionalBulletsData2D>(&data);
	// Wrong data type here means something bypassed the type check - refuse without going live half-seeded.
	if (directional_data == nullptr) {
		UtilityFunctions::push_error("DirectionalBullets2D::enable got wrong spawn data type, expected DirectionalBulletsData2D.");
		return false;
	}

	// Seeding-only from here: base reset left blank ballistics/homing/orbit.
	set_up_movement_data(directional_data->all_bullet_speed_data, directional_data->tile_all_bullet_speed_data);

	adjust_direction_based_on_rotation = directional_data->adjust_direction_based_on_rotation;

	// Unified precedence (same as spawn; enable runs on every pool reuse):
	// per-bullet wins, shared fills only invalid-entry gaps.
	shared_bullet_speed_data = directional_data->shared_bullet_speed_data;
	if (shared_bullet_speed_data.is_valid()) {
		apply_shared_speed_fallback(shared_bullet_speed_data);
	}
	shared_bullet_rotation_data = directional_data->shared_bullet_rotation_data;
	if (shared_bullet_rotation_data.is_valid()) {
		apply_shared_rotation_fallback(shared_bullet_rotation_data, data.rotate_only_textures);
	}

	// Per-bullet spawn-data curves/patterns first, then the shared features
	// below (separate storages, so the tick resolves precedence live).
	apply_per_bullet_curves_from_data(*directional_data);
	apply_per_bullet_movement_patterns_from_data(*directional_data);

	// Shared spawn-data features (same as spawn; enable runs on every pool
	// reuse, and stale state was cleared above by enable_multimesh).
	// Always applied: null data removes previously set features.
	populate_shared_curves_related_data(directional_data->shared_bullet_curves_data);
	apply_shared_movement_pattern_from_data(*directional_data);
	apply_wobble_from_data(*directional_data);
	apply_gravity_from_data(*directional_data);

	// Vectors are sized in spawn, but a wrong-type spawn early-returns before
	// sizing. Resize defensively (base reset already blanked them, so no
	// clears are needed here anymore). Epochs re-assigned (not resized) so
	// a shrunken-then-regrown volley never keeps stale per-bullet epochs.
	all_bullet_wobble.resize(amount_bullets);
	all_bullet_wobble_data.resize(amount_bullets);
	wobble_distance_traveled.resize(amount_bullets, 0.0);
	all_bullet_homing_targets.resize(amount_bullets);
	all_homing_count.resize(amount_bullets, 0);
	all_bullet_homing_smoothing.resize(amount_bullets, 0.0);
	all_orbiting_data.resize(amount_bullets);
	all_orbiting_status.resize(amount_bullets, 0);
	all_shared_homing_reached.resize(amount_bullets);
	bullet_homing_epochs.assign(amount_bullets, 0);
	shared_auto_pop_queued = false;

	// Homing steering seeds from spawn data (direct factory users keep it
	// across pool reuse now) and is always overwritten by the spawner
	// afterwards, so spawner users keep their exact tuning either way.
	homing_smoothing = (real_t)directional_data->homing_smoothing;
	homing_update_interval = (real_t)directional_data->homing_update_interval;
	homing_update_timer = 0.0;
	homing_take_control_of_texture_rotation = directional_data->homing_take_control_of_texture_rotation;
	homing_inert_warning_issued = false;

	homing_distance_before_reached = (real_t)directional_data->homing_distance_before_reached;
	bullet_homing_auto_pop_after_target_reached = directional_data->bullet_homing_auto_pop_after_target_reached;
	shared_homing_deque_auto_pop_after_target_reached = directional_data->shared_homing_deque_auto_pop_after_target_reached;
	// Gravity fully seeded by apply_gravity_from_data above (vectors +
	// windows); the shared member mirrors it for the runtime getter.
	linear_drag = (real_t)directional_data->linear_drag;
	homing_delay_sec = (real_t)directional_data->homing_delay_sec;
	homing_duration_sec = (real_t)directional_data->homing_duration_sec;
	homing_lose_range_px = (real_t)directional_data->homing_lose_range_px;
	return true;
}

void DirectionalBullets2D::custom_additional_disable_logic() {
	// Dying life: any deferred emit/pop still queued no-ops at flush instead
	// of operating on whatever the pool slot becomes next.
	++homing_operation_generation;
	if (bullet_factory != nullptr) {
		bullet_factory->directional_bullets_set.disable_data(sparse_set_id);
	}
}

void DirectionalBullets2D::_bind_methods() {
	// PER BULLET HOMING DEQUE POP METHODS
	ClassDB::bind_method(D_METHOD("bullet_homing_pop_front_target", "bullet_index"), &DirectionalBullets2D::bullet_homing_pop_front_target);
	ClassDB::bind_method(D_METHOD("bullet_homing_pop_back_target", "bullet_index"), &DirectionalBullets2D::bullet_homing_pop_back_target);

	// PER BULLET HOMING DEQUE PUSH METHODS
	ClassDB::bind_method(D_METHOD("bullet_homing_push_front_mouse_position_target", "bullet_index"), &DirectionalBullets2D::bullet_homing_push_front_mouse_position_target);
	ClassDB::bind_method(D_METHOD("bullet_homing_push_front_node2d_target", "bullet_index", "new_homing_target"), &DirectionalBullets2D::bullet_homing_push_front_node2d_target);
	ClassDB::bind_method(D_METHOD("bullet_homing_push_front_global_position_target", "bullet_index", "global_position"), &DirectionalBullets2D::bullet_homing_push_front_global_position_target);

	ClassDB::bind_method(D_METHOD("bullet_homing_push_back_mouse_position_target", "bullet_index"), &DirectionalBullets2D::bullet_homing_push_back_mouse_position_target);
	ClassDB::bind_method(D_METHOD("bullet_homing_push_back_node2d_target", "bullet_index", "new_homing_target"), &DirectionalBullets2D::bullet_homing_push_back_node2d_target);
	ClassDB::bind_method(D_METHOD("bullet_homing_push_back_global_position_target", "bullet_index", "global_position"), &DirectionalBullets2D::bullet_homing_push_back_global_position_target);
	ClassDB::bind_method(D_METHOD("bullet_homing_push_back_homing_target", "bullet_index", "node2d_or_global_position"), &DirectionalBullets2D::bullet_homing_push_back_homing_target);
	ClassDB::bind_method(D_METHOD("bullet_homing_push_front_homing_target", "bullet_index", "node2d_or_global_position"), &DirectionalBullets2D::bullet_homing_push_front_homing_target);
	ClassDB::bind_method(D_METHOD("all_bullets_assign_homing_targets_array", "node2ds_or_global_positions_array", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_assign_homing_targets_array, DEFVAL(0), DEFVAL(-1));

	// PER BULLET HOMING DEQUE HELPERS
	ClassDB::bind_method(D_METHOD("all_bullets_push_front_mouse_position_target", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_front_mouse_position_target, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_push_back_mouse_position_target", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_back_mouse_position_target, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("all_bullets_push_front_homing_target", "node2d_or_global_position", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_front_homing_target, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_push_back_homing_target", "node2d_or_global_position", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_back_homing_target, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("all_bullets_push_front_homing_targets_array", "node2ds_or_global_positions_array", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_front_homing_targets_array, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_push_back_homing_targets_array", "node2ds_or_global_positions_array", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_push_back_homing_targets_array, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("all_bullets_replace_homing_targets_with_new_target", "node2d_or_global_position", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_replace_homing_targets_with_new_target, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_replace_homing_targets_with_new_target_array", "node2ds_or_global_positions_array", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_replace_homing_targets_with_new_target_array, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("all_bullets_pop_front_target", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_pop_front_target, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("all_bullets_pop_back_target", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_pop_back_target, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("all_bullets_replace_homing_targets_with_mouse", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_replace_homing_targets_with_mouse, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("all_bullets_clear_homing_targets", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_clear_homing_targets, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("bullet_clear_homing_targets", "bullet_index"), &DirectionalBullets2D::bullet_clear_homing_targets);

	ClassDB::bind_method(D_METHOD("bullet_homing_check_targets_amount", "bullet_index"), &DirectionalBullets2D::bullet_homing_check_targets_amount);

	ClassDB::bind_method(D_METHOD("bullet_check_has_homing_targets", "bullet_index"), &DirectionalBullets2D::bullet_check_has_homing_targets);

	ClassDB::bind_method(D_METHOD("bullet_homing_check_current_target_type", "bullet_index"), &DirectionalBullets2D::bullet_homing_check_current_target_type);

	ClassDB::bind_method(D_METHOD("bullet_get_current_homing_target", "bullet_index"), &DirectionalBullets2D::bullet_get_current_homing_target);

	ClassDB::bind_method(D_METHOD("get_bullet_homing_auto_pop_after_target_reached"), &DirectionalBullets2D::get_bullet_homing_auto_pop_after_target_reached);
	ClassDB::bind_method(D_METHOD("set_bullet_homing_auto_pop_after_target_reached", "value"), &DirectionalBullets2D::set_bullet_homing_auto_pop_after_target_reached);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bullet_homing_auto_pop_after_target_reached"), "set_bullet_homing_auto_pop_after_target_reached", "get_bullet_homing_auto_pop_after_target_reached");

	// SHARED HOMING DEQUE POP METHODS
	ClassDB::bind_method(D_METHOD("shared_homing_deque_pop_front_target"), &DirectionalBullets2D::shared_homing_deque_pop_front_target);
	ClassDB::bind_method(D_METHOD("shared_homing_deque_pop_back_target"), &DirectionalBullets2D::shared_homing_deque_pop_back_target);
	ClassDB::bind_method(D_METHOD("_do_shared_auto_pop_front_target", "operation_generation"), &DirectionalBullets2D::_do_shared_auto_pop_front_target);
	ClassDB::bind_method(D_METHOD("_do_auto_pop_front_target", "operation_generation", "bullet_index", "bullet_epoch"), &DirectionalBullets2D::_do_auto_pop_front_target);
	ClassDB::bind_method(D_METHOD("_do_emit_homing_target_reached", "operation_generation", "bullet_index", "bullet_epoch", "target_instance_id", "target_global_position"), &DirectionalBullets2D::_do_emit_homing_target_reached);

	// SHARED HOMING DEQUE PUSH METHODS
	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_front_mouse_position_target"), &DirectionalBullets2D::shared_homing_deque_push_front_mouse_position_target);
	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_front_node2d_target", "new_homing_target"), &DirectionalBullets2D::shared_homing_deque_push_front_node2d_target);
	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_front_global_position_target", "global_position"), &DirectionalBullets2D::shared_homing_deque_push_front_global_position_target);

	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_back_mouse_position_target"), &DirectionalBullets2D::shared_homing_deque_push_back_mouse_position_target);
	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_back_node2d_target", "new_homing_target"), &DirectionalBullets2D::shared_homing_deque_push_back_node2d_target);
	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_back_global_position_target", "global_position"), &DirectionalBullets2D::shared_homing_deque_push_back_global_position_target);

	// SHARED HOMING DEQUE HELPERS
	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_front_homing_targets_array", "node2ds_or_global_positions_array"), &DirectionalBullets2D::shared_homing_deque_push_front_homing_targets_array);
	ClassDB::bind_method(D_METHOD("shared_homing_deque_push_back_homing_targets_array", "node2ds_or_global_positions_array"), &DirectionalBullets2D::shared_homing_deque_push_back_homing_targets_array);

	ClassDB::bind_method(D_METHOD("shared_homing_deque_replace_homing_targets_with_new_target", "node2d_or_global_position"), &DirectionalBullets2D::shared_homing_deque_replace_homing_targets_with_new_target);
	ClassDB::bind_method(D_METHOD("shared_homing_deque_replace_homing_targets_with_new_target_array", "node2ds_or_global_positions_array"), &DirectionalBullets2D::shared_homing_deque_replace_homing_targets_with_new_target_array);

	ClassDB::bind_method(D_METHOD("shared_homing_deque_clear_homing_targets"), &DirectionalBullets2D::shared_homing_deque_clear_homing_targets);

	ClassDB::bind_method(D_METHOD("shared_homing_deque_check_homing_targets_amount"), &DirectionalBullets2D::shared_homing_deque_check_homing_targets_amount);

	ClassDB::bind_method(D_METHOD("shared_homing_deque_check_has_homing_targets"), &DirectionalBullets2D::shared_homing_deque_check_has_homing_targets);

	ClassDB::bind_method(D_METHOD("shared_homing_deque_check_current_target_type"), &DirectionalBullets2D::shared_homing_deque_check_current_target_type);

	ClassDB::bind_method(D_METHOD("shared_homing_deque_get_current_homing_target"), &DirectionalBullets2D::shared_homing_deque_get_current_homing_target);

	ClassDB::bind_method(D_METHOD("get_shared_homing_deque_auto_pop_after_target_reached"), &DirectionalBullets2D::get_shared_homing_deque_auto_pop_after_target_reached);
	ClassDB::bind_method(D_METHOD("set_shared_homing_deque_auto_pop_after_target_reached", "value"), &DirectionalBullets2D::set_shared_homing_deque_auto_pop_after_target_reached);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shared_homing_deque_auto_pop_after_target_reached"), "set_shared_homing_deque_auto_pop_after_target_reached", "get_shared_homing_deque_auto_pop_after_target_reached");

	// OTHER HOMING RELATED

	ClassDB::bind_method(D_METHOD("get_homing_distance_before_reached"), &DirectionalBullets2D::get_homing_distance_before_reached);
	ClassDB::bind_method(D_METHOD("set_homing_distance_before_reached", "value"), &DirectionalBullets2D::set_homing_distance_before_reached);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_distance_before_reached"), "set_homing_distance_before_reached", "get_homing_distance_before_reached");

	ClassDB::bind_method(D_METHOD("get_homing_smoothing"), &DirectionalBullets2D::get_homing_smoothing);
	ClassDB::bind_method(D_METHOD("set_homing_smoothing", "value"), &DirectionalBullets2D::set_homing_smoothing);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_smoothing"), "set_homing_smoothing", "get_homing_smoothing");

	ClassDB::bind_method(D_METHOD("bullet_get_homing_smoothing", "bullet_index"), &DirectionalBullets2D::bullet_get_homing_smoothing);
	ClassDB::bind_method(D_METHOD("bullet_set_homing_smoothing", "bullet_index", "value"), &DirectionalBullets2D::bullet_set_homing_smoothing);
	ClassDB::bind_method(D_METHOD("all_bullets_set_homing_smoothing", "value", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_set_homing_smoothing, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_get_homing_smoothing", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_get_homing_smoothing, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("clear_per_bullet_homing_smoothing"), &DirectionalBullets2D::clear_per_bullet_homing_smoothing);

	ClassDB::bind_method(D_METHOD("get_homing_update_interval"), &DirectionalBullets2D::get_homing_update_interval);
	ClassDB::bind_method(D_METHOD("set_homing_update_interval", "value"), &DirectionalBullets2D::set_homing_update_interval);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_update_interval"), "set_homing_update_interval", "get_homing_update_interval");

	ClassDB::bind_method(D_METHOD("get_homing_take_control_of_texture_rotation"), &DirectionalBullets2D::get_homing_take_control_of_texture_rotation);
	ClassDB::bind_method(D_METHOD("set_homing_take_control_of_texture_rotation", "value"), &DirectionalBullets2D::set_homing_take_control_of_texture_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_take_control_of_texture_rotation"), "set_homing_take_control_of_texture_rotation", "get_homing_take_control_of_texture_rotation");

	ClassDB::bind_method(D_METHOD("get_gravity"), &DirectionalBullets2D::get_gravity);
	ClassDB::bind_method(D_METHOD("set_gravity", "value"), &DirectionalBullets2D::set_gravity);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "gravity"), "set_gravity", "get_gravity");

	ClassDB::bind_method(D_METHOD("bullet_get_gravity", "bullet_index"), &DirectionalBullets2D::bullet_get_gravity);
	ClassDB::bind_method(D_METHOD("bullet_set_gravity", "bullet_index", "value"), &DirectionalBullets2D::bullet_set_gravity);
	ClassDB::bind_method(D_METHOD("all_bullets_set_gravity", "value", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_set_gravity, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_get_gravity", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_get_gravity, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("get_gravity_delay_sec"), &DirectionalBullets2D::get_gravity_delay_sec);
	ClassDB::bind_method(D_METHOD("set_gravity_delay_sec", "value"), &DirectionalBullets2D::set_gravity_delay_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity_delay_sec"), "set_gravity_delay_sec", "get_gravity_delay_sec");

	ClassDB::bind_method(D_METHOD("get_gravity_duration_sec"), &DirectionalBullets2D::get_gravity_duration_sec);
	ClassDB::bind_method(D_METHOD("set_gravity_duration_sec", "value"), &DirectionalBullets2D::set_gravity_duration_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity_duration_sec"), "set_gravity_duration_sec", "get_gravity_duration_sec");

	ClassDB::bind_method(D_METHOD("bullet_get_fall_speed", "bullet_index"), &DirectionalBullets2D::bullet_get_fall_speed);
	ClassDB::bind_method(D_METHOD("all_bullets_get_fall_speed", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_get_fall_speed, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("debug_get_gravity_info", "bullet_index"), &DirectionalBullets2D::debug_get_gravity_info);
	ClassDB::bind_method(D_METHOD("debug_get_bullet_info", "bullet_index"), &DirectionalBullets2D::debug_get_bullet_info);

	ClassDB::bind_method(D_METHOD("get_linear_drag"), &DirectionalBullets2D::get_linear_drag);
	ClassDB::bind_method(D_METHOD("set_linear_drag", "value"), &DirectionalBullets2D::set_linear_drag);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "linear_drag"), "set_linear_drag", "get_linear_drag");

	ClassDB::bind_method(D_METHOD("get_homing_delay_sec"), &DirectionalBullets2D::get_homing_delay_sec);
	ClassDB::bind_method(D_METHOD("set_homing_delay_sec", "value"), &DirectionalBullets2D::set_homing_delay_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_delay_sec"), "set_homing_delay_sec", "get_homing_delay_sec");

	ClassDB::bind_method(D_METHOD("get_homing_duration_sec"), &DirectionalBullets2D::get_homing_duration_sec);
	ClassDB::bind_method(D_METHOD("set_homing_duration_sec", "value"), &DirectionalBullets2D::set_homing_duration_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_duration_sec"), "set_homing_duration_sec", "get_homing_duration_sec");

	ClassDB::bind_method(D_METHOD("get_homing_lose_range_px"), &DirectionalBullets2D::get_homing_lose_range_px);
	ClassDB::bind_method(D_METHOD("set_homing_lose_range_px", "value"), &DirectionalBullets2D::set_homing_lose_range_px);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_lose_range_px"), "set_homing_lose_range_px", "get_homing_lose_range_px");

	ClassDB::bind_method(D_METHOD("get_is_wobble_enabled"), &DirectionalBullets2D::get_is_wobble_enabled);
	ClassDB::bind_method(D_METHOD("bullet_get_wobble_amplitude", "bullet_index"), &DirectionalBullets2D::bullet_get_wobble_amplitude);
	ClassDB::bind_method(D_METHOD("bullet_get_wobble_face_movement_direction", "bullet_index"), &DirectionalBullets2D::bullet_get_wobble_face_movement_direction);
	ClassDB::bind_method(D_METHOD("bullet_get_wobble_face_rotation_speed", "bullet_index"), &DirectionalBullets2D::bullet_get_wobble_face_rotation_speed);
	ClassDB::bind_method(D_METHOD("bullet_set_wobble_data", "bullet_index", "wobble_data"), &DirectionalBullets2D::bullet_set_wobble_data);
	ClassDB::bind_method(D_METHOD("all_bullets_set_wobble_data", "wobble_data", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_set_wobble_data, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("bullet_get_wobble_data", "bullet_index"), &DirectionalBullets2D::bullet_get_wobble_data);
	ClassDB::bind_method(D_METHOD("all_bullets_get_wobble_data", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_get_wobble_data, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("get_shared_bullet_wobble_data"), &DirectionalBullets2D::get_shared_bullet_wobble_data);
	ClassDB::bind_method(D_METHOD("set_shared_bullet_wobble_data", "new_wobble_data"), &DirectionalBullets2D::set_shared_bullet_wobble_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_bullet_wobble_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletWobbleData2D"), "set_shared_bullet_wobble_data", "get_shared_bullet_wobble_data");
	ClassDB::bind_method(D_METHOD("has_shared_bullet_wobble_data"), &DirectionalBullets2D::has_shared_bullet_wobble_data);
	ClassDB::bind_method(D_METHOD("remove_shared_bullet_wobble_data"), &DirectionalBullets2D::remove_shared_bullet_wobble_data);
	ClassDB::bind_method(D_METHOD("debug_get_wobble_info", "bullet_index"), &DirectionalBullets2D::debug_get_wobble_info);

	// ORBITING RELATED

	ClassDB::bind_method(D_METHOD("bullet_enable_orbiting", "bullet_index", "orbiting_radius", "orbiting_direction", "orbiting_texture_rotation", "orbiting_follow_mode", "orbiting_follow_deadzone", "orbiting_lock_policy", "orbiting_rigid_follow"), &DirectionalBullets2D::bullet_enable_orbiting, DEFVAL(OrbitRight), DEFVAL(FaceTarget), DEFVAL(FollowTarget), DEFVAL(0.0), DEFVAL(RelockAlways), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("bullet_disable_orbiting", "bullet_index"), &DirectionalBullets2D::bullet_disable_orbiting);
	ClassDB::bind_method(D_METHOD("bullet_is_orbiting_enabled", "bullet_index"), &DirectionalBullets2D::bullet_is_orbiting_enabled);
	ClassDB::bind_method(D_METHOD("bullet_is_orbiting_locked", "bullet_index"), &DirectionalBullets2D::bullet_is_orbiting_locked);
	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_center", "bullet_index"), &DirectionalBullets2D::bullet_get_orbiting_center);
	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_angle", "bullet_index"), &DirectionalBullets2D::bullet_get_orbiting_angle);
	ClassDB::bind_method(D_METHOD("bullet_set_orbiting_center", "bullet_index", "new_center"), &DirectionalBullets2D::bullet_set_orbiting_center);

	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_radius", "bullet_index"), &DirectionalBullets2D::bullet_get_orbiting_radius);
	ClassDB::bind_method(D_METHOD("bullet_set_orbiting_radius", "bullet_index", "new_radius"), &DirectionalBullets2D::bullet_set_orbiting_radius);

	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_texture_rotation", "bullet_index"), &DirectionalBullets2D::bullet_get_orbiting_texture_rotation);
	ClassDB::bind_method(D_METHOD("bullet_set_orbiting_texture_rotation", "bullet_index", "new_texture_rotation"), &DirectionalBullets2D::bullet_set_orbiting_texture_rotation);

	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_direction", "bullet_index"), &DirectionalBullets2D::bullet_get_orbiting_direction);
	ClassDB::bind_method(D_METHOD("bullet_set_orbiting_direction", "bullet_index", "new_direction"), &DirectionalBullets2D::bullet_set_orbiting_direction);

	ClassDB::bind_method(D_METHOD("bullet_replace_homing_targets_with_new_target", "bullet_index", "node2d_or_global_position"), &DirectionalBullets2D::bullet_replace_homing_targets_with_new_target);

	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_follow_mode", "bullet_index"), &DirectionalBullets2D::bullet_get_orbiting_follow_mode);
	ClassDB::bind_method(D_METHOD("bullet_set_orbiting_follow_mode", "bullet_index", "new_follow_mode"), &DirectionalBullets2D::bullet_set_orbiting_follow_mode);

	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_follow_deadzone", "bullet_index"), &DirectionalBullets2D::bullet_get_orbiting_follow_deadzone);
	ClassDB::bind_method(D_METHOD("bullet_set_orbiting_follow_deadzone", "bullet_index", "new_deadzone"), &DirectionalBullets2D::bullet_set_orbiting_follow_deadzone);

	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_lock_policy", "bullet_index"), &DirectionalBullets2D::bullet_get_orbiting_lock_policy);
	ClassDB::bind_method(D_METHOD("bullet_set_orbiting_lock_policy", "bullet_index", "new_lock_policy"), &DirectionalBullets2D::bullet_set_orbiting_lock_policy);

	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_rigid_follow", "bullet_index"), &DirectionalBullets2D::bullet_get_orbiting_rigid_follow);
	ClassDB::bind_method(D_METHOD("bullet_set_orbiting_rigid_follow", "bullet_index", "new_rigid_follow"), &DirectionalBullets2D::bullet_set_orbiting_rigid_follow);
	ClassDB::bind_method(D_METHOD("all_bullets_set_orbiting_rigid_follow", "new_rigid_follow", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_set_orbiting_rigid_follow, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("all_bullets_enable_orbiting", "orbiting_radius", "orbiting_direction", "orbiting_texture_rotation", "bullet_index_start", "bullet_index_end_inclusive", "orbiting_follow_mode", "orbiting_follow_deadzone", "orbiting_lock_policy", "orbiting_rigid_follow"), &DirectionalBullets2D::all_bullets_enable_orbiting, DEFVAL(OrbitRight), DEFVAL(FaceTarget), DEFVAL(0), DEFVAL(-1), DEFVAL(FollowTarget), DEFVAL(0.0), DEFVAL(RelockAlways), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("all_bullets_enable_orbiting_linear", "radius_start", "radius_step", "orbiting_direction", "orbiting_texture_rotation", "bullet_index_start", "bullet_index_end_inclusive", "orbiting_follow_mode", "orbiting_follow_deadzone", "orbiting_lock_policy", "orbiting_rigid_follow"), &DirectionalBullets2D::all_bullets_enable_orbiting_linear, DEFVAL(OrbitRight), DEFVAL(FaceTarget), DEFVAL(0), DEFVAL(-1), DEFVAL(FollowTarget), DEFVAL(0.0), DEFVAL(RelockAlways), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("all_bullets_get_orbiting_radius", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_get_orbiting_radius, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_get_orbiting_center", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_get_orbiting_center, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_get_orbiting_angle", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_get_orbiting_angle, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("debug_get_orbiting_info", "bullet_index"), &DirectionalBullets2D::debug_get_orbiting_info);
	ClassDB::bind_method(D_METHOD("all_bullets_get_homing_targets_amount", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_get_homing_targets_amount, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("debug_get_curves_info", "bullet_index"), &DirectionalBullets2D::debug_get_curves_info);
	ClassDB::bind_method(D_METHOD("debug_get_pattern_info", "bullet_index"), &DirectionalBullets2D::debug_get_pattern_info);
	ClassDB::bind_method(D_METHOD("all_bullets_is_orbiting_enabled", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_is_orbiting_enabled, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_is_orbiting_locked", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_is_orbiting_locked, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_disable_orbiting", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_disable_orbiting, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_orbiting_radius", "new_radius", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_set_orbiting_radius, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_orbiting_direction", "new_direction", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_set_orbiting_direction, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_orbiting_texture_rotation", "new_rotation", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_set_orbiting_texture_rotation, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_orbiting_follow_mode", "new_follow_mode", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_set_orbiting_follow_mode, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_orbiting_follow_deadzone", "new_deadzone", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_set_orbiting_follow_deadzone, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_orbiting_lock_policy", "new_lock_policy", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_set_orbiting_lock_policy, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_orbiting_center", "new_center", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_set_orbiting_center, DEFVAL(0), DEFVAL(-1));

	// OTHER USEFUL METHODS
	ClassDB::bind_method(D_METHOD("teleport_bullet", "bullet_index", "new_global_pos"), &DirectionalBullets2D::teleport_bullet);
	ClassDB::bind_method(D_METHOD("teleport_shift_bullet", "bullet_index", "shift_value"), &DirectionalBullets2D::teleport_shift_bullet);
	ClassDB::bind_method(D_METHOD("teleport_shift_all_bullets", "shift_value", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::teleport_shift_all_bullets, DEFVAL(0), DEFVAL(-1));

	ClassDB::bind_method(D_METHOD("bullet_set_velocity", "bullet_index", "new_velocity"), &DirectionalBullets2D::bullet_set_velocity);
	ClassDB::bind_method(D_METHOD("all_bullets_set_velocity", "new_velocity", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_set_velocity, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_get_velocity", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_get_velocity, DEFVAL(0), DEFVAL(-1));

	// The get/set methods live on the base class (bound there so BlockBullets2D
	// gets them too). Re-binding the same names here trips a duplicate-method
	// error at startup, so only the property is declared on top of them.
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "inherited_velocity_offset"), "set_inherited_velocity_offset", "get_inherited_velocity_offset");

	// SHARED MOVEMENT PATTERN RUNTIME API (spawn-data equivalent, editable live).
	ClassDB::bind_method(D_METHOD("get_shared_movement_pattern_curve"), &DirectionalBullets2D::get_shared_movement_pattern_curve);
	ClassDB::bind_method(D_METHOD("set_shared_movement_pattern_curve", "new_curve"), &DirectionalBullets2D::set_shared_movement_pattern_curve);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_movement_pattern_curve", PROPERTY_HINT_RESOURCE_TYPE, "Curve2D"), "set_shared_movement_pattern_curve", "get_shared_movement_pattern_curve");

	ClassDB::bind_method(D_METHOD("get_shared_movement_pattern_face_movement_direction"), &DirectionalBullets2D::get_shared_movement_pattern_face_movement_direction);
	ClassDB::bind_method(D_METHOD("set_shared_movement_pattern_face_movement_direction", "value"), &DirectionalBullets2D::set_shared_movement_pattern_face_movement_direction);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shared_movement_pattern_face_movement_direction"), "set_shared_movement_pattern_face_movement_direction", "get_shared_movement_pattern_face_movement_direction");

	ClassDB::bind_method(D_METHOD("get_shared_movement_pattern_repeat"), &DirectionalBullets2D::get_shared_movement_pattern_repeat);
	ClassDB::bind_method(D_METHOD("set_shared_movement_pattern_repeat", "value"), &DirectionalBullets2D::set_shared_movement_pattern_repeat);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shared_movement_pattern_repeat"), "set_shared_movement_pattern_repeat", "get_shared_movement_pattern_repeat");

	ClassDB::bind_method(D_METHOD("has_shared_movement_pattern"), &DirectionalBullets2D::has_shared_movement_pattern);
	ClassDB::bind_method(D_METHOD("remove_shared_movement_pattern"), &DirectionalBullets2D::remove_shared_movement_pattern);

	// SHARED SPEED / ROTATION RUNTIME API.
	ClassDB::bind_method(D_METHOD("get_shared_bullet_speed_data"), &DirectionalBullets2D::get_shared_bullet_speed_data);
	ClassDB::bind_method(D_METHOD("set_shared_bullet_speed_data", "new_speed_data"), &DirectionalBullets2D::set_shared_bullet_speed_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_bullet_speed_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletSpeedData2D"), "set_shared_bullet_speed_data", "get_shared_bullet_speed_data");

	ClassDB::bind_method(D_METHOD("has_shared_bullet_speed_data"), &DirectionalBullets2D::has_shared_bullet_speed_data);
	ClassDB::bind_method(D_METHOD("remove_shared_bullet_speed_data"), &DirectionalBullets2D::remove_shared_bullet_speed_data);

	ClassDB::bind_method(D_METHOD("get_shared_bullet_rotation_data"), &DirectionalBullets2D::get_shared_bullet_rotation_data);
	ClassDB::bind_method(D_METHOD("set_shared_bullet_rotation_data", "new_rotation_data"), &DirectionalBullets2D::set_shared_bullet_rotation_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_bullet_rotation_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletRotationData2D"), "set_shared_bullet_rotation_data", "get_shared_bullet_rotation_data");

	ClassDB::bind_method(D_METHOD("has_shared_bullet_rotation_data"), &DirectionalBullets2D::has_shared_bullet_rotation_data);
	ClassDB::bind_method(D_METHOD("remove_shared_bullet_rotation_data"), &DirectionalBullets2D::remove_shared_bullet_rotation_data);

	ClassDB::bind_method(D_METHOD("get_adjust_direction_based_on_rotation"), &DirectionalBullets2D::get_adjust_direction_based_on_rotation);
	ClassDB::bind_method(D_METHOD("set_adjust_direction_based_on_rotation", "value"), &DirectionalBullets2D::set_adjust_direction_based_on_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "adjust_direction_based_on_rotation"), "set_adjust_direction_based_on_rotation", "get_adjust_direction_based_on_rotation");

	BIND_ENUM_CONSTANT(GlobalPositionTarget);
	BIND_ENUM_CONSTANT(Node2DTarget);
	BIND_ENUM_CONSTANT(MousePositionTarget);
	BIND_ENUM_CONSTANT(NotHoming);

	BIND_ENUM_CONSTANT(DontMove);
	BIND_ENUM_CONSTANT(OrbitLeft);
	BIND_ENUM_CONSTANT(OrbitRight);
	BIND_ENUM_CONSTANT(OrbitRandom);

	BIND_ENUM_CONSTANT(FaceTarget);
	BIND_ENUM_CONSTANT(FaceOppositeTarget);
	BIND_ENUM_CONSTANT(FaceOrbitingDirection);
	BIND_ENUM_CONSTANT(FaceOppositeOrbitingDirection);

	BIND_ENUM_CONSTANT(FollowTarget);
	BIND_ENUM_CONSTANT(FollowDeadzone);
	BIND_ENUM_CONSTANT(Anchored);

	BIND_ENUM_CONSTANT(RelockAlways);
	BIND_ENUM_CONSTANT(StayLocked);
	BIND_ENUM_CONSTANT(RelockOnTargetChange);

	// NOTE: signal args use PROPERTY_HINT_RESOURCE_TYPE (not NODE_TYPE) so the
	// class name reaches ClassDB and --doctool; see the note on the factory
	// signals.
	ADD_SIGNAL(MethodInfo("bullet_homing_target_reached",
						  PropertyInfo(Variant::OBJECT, "multimesh_instance", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBullets2D"),
						  PropertyInfo(Variant::INT, "bullet_index"),
						  PropertyInfo(Variant::OBJECT, "target", PROPERTY_HINT_RESOURCE_TYPE, "Node2D"),
						  PropertyInfo(Variant::VECTOR2, "target_global_position")));
}

} //namespace BlastBullets2D
