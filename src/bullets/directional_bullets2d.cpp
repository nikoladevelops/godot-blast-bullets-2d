#include "../shared/warn_once2d.hpp"
#include "../shared/cached_string_names2d.hpp"
#include "directional_bullets2d.hpp"

#include "../spawn-data/directional_bullets_data2d.hpp"
#include "godot_cpp/classes/capsule_shape2d.hpp"
#include "godot_cpp/classes/circle_shape2d.hpp"
#include "godot_cpp/classes/collision_shape2d.hpp"
#include "godot_cpp/classes/node.hpp"
#include "godot_cpp/classes/rectangle_shape2d.hpp"
#include "godot_cpp/classes/scene_tree.hpp"
#include "godot_cpp/classes/segment_shape2d.hpp"
#include "godot_cpp/classes/tile_map_layer.hpp"
#include "godot_cpp/classes/world_boundary_shape2d.hpp"
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
	has_per_bullet_speed_data.assign(amount_bullets, 0);
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
			WarnOnce2D::warn(warn_data_id, 5u, speed_data_size, amount_bullets, "DirectionalBullets2D: all_bullet_speed_data size (" + String::num_int64(speed_data_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets fall back to shared/default" + String(tile_short_arrays ? " (tiling on: wrapping short array)." : " (check tile_all_bullet_speed_data to wrap, or provide one entry per bullet)."));
		}

		// In case no speed data was provided at all, create a default one (everything set to 0 by default)
		if (fallback_data.is_null()) {
			fallback_data.instantiate();
		}
	}

	for (int i = 0; i < amount_bullets; ++i) {
		const real_t rot = all_cached_shape_transforms[i].get_rotation();

		Ref<BulletSpeedData2D> data = fallback_data;
		// True only when this slot reads an entry the USER authored (exact,
		// tiled, or in-range short array). A synthesized default below is not
		// a user intent, so it must not claim presence - otherwise a volley
		// with no per-bullet speed data at all would freeze permanently
		// instead of picking up shared speed.
		bool user_authored_entry = false;

		if (exact_speed) {
			data = new_speed_data[i];
			user_authored_entry = true;
		} else if (tiled_speed && speed_data_size > 0) {
			data = new_speed_data[i % speed_data_size];
			user_authored_entry = true;
		} else if (i >= 0 && i < speed_data_size) {
			data = new_speed_data[i];
			user_authored_entry = true;
		}

		// Extract values with null safety
		real_t s = 0.0, m = 0.0, acc = 0.0;

		if (data.is_valid()) {
			s = data->speed;
			m = data->max_speed;
			acc = data->acceleration;
		}

		// Non-finite values would poison the tick path, so fail open to zeros like a missing entry.
		bool valid_entry = user_authored_entry && data.is_valid();
		if (!Math::is_finite(s) || !Math::is_finite(m) || !Math::is_finite(acc)) {
			UtilityFunctions::push_error("DirectionalBullets2D movement data contains NaN/Inf, using zeros for bullet index " + String::num_int64(i) + ".");
			s = 0.0;
			m = 0.0;
			acc = 0.0;
			valid_entry = false;
		}

		// Overwrite existing memory slots
		all_cached_speed[i] = s;
		all_cached_max_speed[i] = m;
		all_cached_acceleration[i] = acc;
		// Explicit presence: a VALID entry (even all-zero "don't move") must
		// never be mistaken for a gap by the shared fallback below.
		has_per_bullet_speed_data[i] = valid_entry ? 1 : 0;

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
	// The vector is sized in set_up_movement_data, but the fallback can also
	// run on a volley whose movement data was never seeded (or was cleared), so
	// normalize the size here rather than trusting the seed path.
	if ((int)has_per_bullet_speed_data.size() != amount_bullets) {
		has_per_bullet_speed_data.assign(amount_bullets, 0);
	}
	for (int i = 0; i < amount_bullets; ++i) {
		// Presence, not a zero-triple test. Bit 1 covers BOTH a valid per-bullet
		// entry and a slot this fallback already filled, so shared data never
		// overwrites a deliberate all-zero ("don't move") and never re-fills.
		// Bit 0 is a genuine gap (invalid entry, or never seeded).
		if (has_per_bullet_speed_data[i]) {
			continue;
		}
		if (shared->speed == 0.0 && shared->max_speed == 0.0 && shared->acceleration == 0.0) {
			continue;
		}
		all_cached_speed[i] = shared->speed;
		all_cached_max_speed[i] = shared->max_speed;
		all_cached_acceleration[i] = shared->acceleration;
		// Fill-once: mark the slot so a later set_shared_* does not re-apply.
		has_per_bullet_speed_data[i] = 1;
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
		has_per_bullet_rotation_data.assign(amount_bullets, 1);
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
	// Rotation active from per-bullet seeding: only fill slots the seed marked
	// as gaps. Presence, not a zero-triple test - a valid all-zero entry
	// ("no spin") must survive, and a slot this fallback already filled must
	// not be re-filled.
	if ((int)all_rotation_speed.size() != amount_bullets) {
		return;
	}
	if ((int)has_per_bullet_rotation_data.size() != amount_bullets) {
		has_per_bullet_rotation_data.assign(amount_bullets, 0);
	}
	bool filled_any = false;
	for (int i = 0; i < amount_bullets; ++i) {
		if (has_per_bullet_rotation_data[i]) {
			continue;
		}
		if (shared->rotation_speed == 0.0 && shared->max_rotation_speed == 0.0 && shared->rotation_acceleration == 0.0) {
			continue;
		}
		all_rotation_speed[i] = shared->rotation_speed;
		all_max_rotation_speed[i] = shared->max_rotation_speed;
		all_rotation_acceleration[i] = shared->rotation_acceleration;
		has_per_bullet_rotation_data[i] = 1;
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
		WarnOnce2D::warn(warn_data_id, 6u, curves_size, amount_bullets, "DirectionalBullets2D: all_bullet_curves_data size (" + String::num_int64(curves_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets use shared/default" + String(tile ? " (tiling on: wrapping short array)." : " (check tile_all_bullet_curves_data to wrap, or provide one entry per bullet)."));
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
		WarnOnce2D::warn(warn_data_id, 7u, paths_size, amount_bullets, "DirectionalBullets2D: all_bullet_movement_pattern_paths size (" + String::num_int64(paths_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets use the shared pattern" + String(tile_paths ? " (tiling on: wrapping short array)." : " (check tile_all_bullet_movement_pattern_paths to wrap, or provide one entry per bullet)."));
	}
	const int face_size = directional_data.all_bullet_movement_pattern_face_movement_directions.size();
	const bool tile_face = directional_data.tile_all_bullet_movement_pattern_face_movement_directions;
	if (face_size != 0 && face_size != amount_bullets) {
		WarnOnce2D::warn(warn_data_id, 8u, face_size, amount_bullets, "DirectionalBullets2D: all_bullet_movement_pattern_face_movement_directions size (" + String::num_int64(face_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets use the shared face flag" + String(tile_face ? " (tiling on)." : " (check its tile box or provide one entry per bullet)."));
	}
	const int repeat_size = directional_data.all_bullet_movement_pattern_repeats.size();
	const bool tile_repeat = directional_data.tile_all_bullet_movement_pattern_repeats;
	if (repeat_size != 0 && repeat_size != amount_bullets) {
		WarnOnce2D::warn(warn_data_id, 9u, repeat_size, amount_bullets, "DirectionalBullets2D: all_bullet_movement_pattern_repeats size (" + String::num_int64(repeat_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets use the shared repeat flag" + String(tile_repeat ? " (tiling on)." : " (check its tile box or provide one entry per bullet)."));
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
		WarnOnce2D::warn(warn_data_id, 10u, grav_size, amount_bullets, "DirectionalBullets2D: all_bullet_gravity size (" + String::num_int64(grav_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets use gravity" + String(tile_grav ? " (tiling on: wrapping short array)." : " (check tile_all_bullet_gravity to wrap, or provide one entry per bullet)."));
	}
	// Fresh seed: presence re-derived per slot below (authored entries claim
	// it, even deliberate zeros; gaps stay fillable by set_gravity).
	has_per_bullet_gravity.assign(amount_bullets, 0);
	for (int i = 0; i < amount_bullets; ++i) {
		const int entry = tile_grav ? resolve_tiled_data_index(grav_size, i) : resolve_strict_data_index(grav_size, i);
		Vector2 g = gravity;
		bool authored = false;
		if (entry >= 0 && entry < directional_data.all_bullet_gravity.size()) {
			const Vector2 candidate = directional_data.all_bullet_gravity[entry];
			if (candidate.is_finite()) {
				g = candidate;
				authored = true;
			} else {
				UtilityFunctions::push_error("DirectionalBulletsData2D all_bullet_gravity[" + String::num_int64(entry) + "] is not finite, using (0, 0) for bullet index " + String::num_int64(i) + ".");
				g = Vector2(0, 0);
			}
		}
		all_gravity[i] = g;
		has_per_bullet_gravity[i] = authored ? 1 : 0;
	}
	refresh_gravity_active();
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
		WarnOnce2D::warn(warn_data_id, 11u, wobble_size, amount_bullets, "DirectionalBullets2D: all_bullet_wobble_data size (" + String::num_int64(wobble_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets use shared wobble" + String(tile_wobble ? " (tiling on: wrapping short array)." : " (check tile_all_bullet_wobble_data to wrap, or provide one entry per bullet)."));
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
	has_per_bullet_gravity.assign(amount_bullets, 0);
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
	apply_bounce_from_data(*directional_data, data.collision_mask);
}

bool DirectionalBullets2D::is_data_type_compatible(const MultiMeshBulletsData2D &data) const {
	return Object::cast_to<DirectionalBulletsData2D>(&data) != nullptr;
}

void DirectionalBullets2D::reset_transient_subclass_state(bool drop_stale_work) {
	// Neutralize ballistics/shared/homing so a new life never inherits the
	// previous owner's values (base reset cannot clear subclass state).
	// set_up_movement_data re-seeds has_per_bullet_speed_data; the rotation
	// presence is handled below (kept across plain disables, reset only for
	// new pooled lives).
	// Linear ballistics (speed/max/accel/direction/velocity) survive a plain
	// full drain so a same-owner wake resumes them, exactly like rotation
	// speeds below (contract fix: a full drain used to zero them, so waking
	// any bullet after the last one died revived a frozen bullet). A new life
	// (drop_stale_work) neutralizes them; every spawn/enable reseeds anyway.
	if (drop_stale_work) {
		set_up_movement_data(TypedArray<BulletSpeedData2D>());
	}
	// Rotation presence follows the VALUES: rotation speeds survive a plain
	// disable (for same-owner wakes), so their presence decisions must too —
	// otherwise a later shared write would clobber authored entries the wake
	// meant to resume. Only a new pooled life (drop_stale_work) resets the
	// decisions (the seed below re-derives them anyway).
	if (drop_stale_work) {
		reset_per_bullet_rotation_presence();
	}
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
	has_per_bullet_gravity.assign(amount_bullets, 0);
	refresh_gravity_active();
	gravity_delay_sec = 0.0;
	gravity_duration_sec = 0.0;
	linear_drag = 0.0;
	homing_delay_sec = 0.0;
	homing_duration_sec = 0.0;
	homing_lose_range_px = 0.0;
	bounce_mask = 0;
	bounce_strength = 1.0;
	bounce_tilemap_layers = false;
	bounce_push_assist = true;
	bounce_charge_amplify = true;
	bounce_hit_consumed = false;
	bounce_max_count = 0;
	bounce_mode = 0;
	bounce_rotate_texture = true;
	bounce_rotation_smooth = 0.0;
	bounce_randomness_deg = 0.0;
	bounce_cooldown_sec = 0.05;
	bounce_debounce_sec = 0.15;
	all_bounce_count.clear();
	all_bounce_cooldown.clear();
	all_bounce_last_tick.clear();
	all_bounce_last_target.clear();
	all_bounce_last_time.clear();
	all_bounce_last_normal.clear();
	all_bounce_last_target_velocity.clear();
	bounce_visual_pending.clear();
	bounce_visual_target.clear();
	all_bounce_speed_multiplier.clear();
	bounce_speed_scaled = false;
	bounce_mask_warning_issued = false;
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
	apply_bounce_from_data(*directional_data, data.collision_mask);
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

// Analytic surface normal from the collided target's first usable direct
// CollisionShape2D child (rect/circle/capsule/segment/world boundary).
// Drain-safe: pure math, no physics-server queries. Shapes must be direct
// children (a Godot physics requirement: deeper nesting never registers,
// so it can never collide). Returns false when there is no usable shape
// (the caller falls back to the radial normal).
// Local-space surface normal -> world space. Normals transform by the
// inverse-transpose of the basis, not the basis itself: under non-uniform
// node scale (stretched walls, scaled ramps) basis_xform tilts slopes and
// rounded corners away from the true perpendicular. Identical to
// basis_xform for rotation + uniform scale. Caller checks invertibility.
static Vector2 bounce_local_normal_to_world(const Transform2D &shape_global, const Vector2 &local_n) {
	return shape_global.affine_inverse().basis_xform_inv(local_n);
}

static bool bounce_normal_from_shape_node(CollisionShape2D *cs, const Vector2 &bullet_pos, Vector2 &r_normal) {
	if (cs == nullptr || cs->is_queued_for_deletion()) {
		return false;
	}
	Ref<Shape2D> shape = cs->get_shape();
	if (shape.is_null()) {
		return false;
	}
	const Transform2D shape_global = cs->get_global_transform();
	if (!MultiMeshBullets2D::is_transform_invertible_safe(shape_global)) {
		return false;
	}
	if (RectangleShape2D *rect = Object::cast_to<RectangleShape2D>(shape.ptr())) {
		const Vector2 size = rect->get_size();
		if (!size.is_finite() || size.x <= 0.0 || size.y <= 0.0) {
			return false;
		}
		const Vector2 local = shape_global.affine_inverse().xform(bullet_pos);
		if (!local.is_finite()) {
			return false;
		}
		const Vector2 half = size * 0.5;
		const Vector2 clamped(Math::clamp(local.x, -half.x, half.x), Math::clamp(local.y, -half.y, half.y));
		const Vector2 diff = local - clamped;
		Vector2 local_n;
		if (diff.length_squared() > 0.00000001) {
			local_n = diff.normalized();
		} else {
			// Bullet center inside the box: push along min-penetration axis.
			// Compare penetrations in world units: under non-uniform scale
			// a local-unit compare picks the wrong (deeper) axis.
			const real_t px = (half.x - Math::abs(local.x)) * shape_global.columns[0].length();
			const real_t py = (half.y - Math::abs(local.y)) * shape_global.columns[1].length();
			local_n = (px < py) ? Vector2(local.x >= 0.0 ? 1.0 : -1.0, 0.0) : Vector2(0.0, local.y >= 0.0 ? 1.0 : -1.0);
		}
		const Vector2 world_n = bounce_local_normal_to_world(shape_global, local_n);
		if (world_n.is_finite() && world_n.length_squared() > 0.00000001) {
			r_normal = world_n.normalized();
			return true;
		}
		return false;
	}
	if (CircleShape2D *circle = Object::cast_to<CircleShape2D>(shape.ptr())) {
		const real_t r = circle->get_radius();
		if (!Math::is_finite((double)r) || r <= 0.0) {
			return false;
		}
		const Vector2 diff = bullet_pos - shape_global.get_origin();
		if (diff.is_finite() && diff.length_squared() > 0.00000001) {
			r_normal = diff.normalized();
			return true;
		}
		return false;
	}
	if (CapsuleShape2D *cap = Object::cast_to<CapsuleShape2D>(shape.ptr())) {
		const real_t r = cap->get_radius();
		const real_t h = cap->get_height();
		if (!Math::is_finite((double)r) || !Math::is_finite((double)h) || r <= 0.0 || h <= 0.0) {
			return false;
		}
		// Godot capsules run along local Y: segment between the cap centers.
		const Vector2 local = shape_global.affine_inverse().xform(bullet_pos);
		if (!local.is_finite()) {
			return false;
		}
		const real_t half_seg = Math::max(0.0, h * 0.5 - r);
		const Vector2 closest(0.0, Math::clamp(local.y, (real_t)-half_seg, (real_t)half_seg));
		const Vector2 diff = local - closest;
		Vector2 local_n = (diff.length_squared() > 0.00000001) ? diff.normalized() : Vector2(local.x >= 0.0 ? 1.0 : -1.0, 0.0);
		const Vector2 world_n = bounce_local_normal_to_world(shape_global, local_n);
		if (world_n.is_finite() && world_n.length_squared() > 0.00000001) {
			r_normal = world_n.normalized();
			return true;
		}
		return false;
	}
	if (SegmentShape2D *seg = Object::cast_to<SegmentShape2D>(shape.ptr())) {
		// Sloped static ground: the normal is the segment perpendicular on
		// the bullet's side (orientation-agnostic, so winding never matters).
		const Vector2 a = seg->get_a();
		const Vector2 b = seg->get_b();
		if (!a.is_finite() || !b.is_finite()) {
			return false;
		}
		const Vector2 along = b - a;
		if (!along.is_finite() || along.length_squared() < 0.00000001) {
			return false;
		}
		const Vector2 local = shape_global.affine_inverse().xform(bullet_pos);
		if (!local.is_finite()) {
			return false;
		}
		const Vector2 mid = (a + b) * 0.5;
		if (!mid.is_finite()) {
			return false;
		}
		Vector2 local_n(-along.y, along.x);
		if (!local_n.is_finite() || local_n.length_squared() < 0.00000001) {
			return false;
		}
		local_n = local_n.normalized();
		if (local_n.dot(local - mid) < 0.0) {
			local_n = -local_n;
		}
		const Vector2 world_n = bounce_local_normal_to_world(shape_global, local_n);
		if (world_n.is_finite() && world_n.length_squared() > 0.00000001) {
			r_normal = world_n.normalized();
			return true;
		}
		return false;
	}
	if (WorldBoundaryShape2D *boundary = Object::cast_to<WorldBoundaryShape2D>(shape.ptr())) {
		// Screen-edge planes store their normal outright: rotate it by the
		// shape node (translation-independent, so the plane offset needs
		// no handling here).
		const Vector2 stored = boundary->get_normal();
		if (!stored.is_finite() || stored.length_squared() < 0.00000001) {
			return false;
		}
		const Vector2 world_n = bounce_local_normal_to_world(shape_global, stored.normalized());
		if (world_n.is_finite() && world_n.length_squared() > 0.00000001) {
			r_normal = world_n.normalized();
			return true;
		}
		return false;
	}
	return false;
}

static bool bounce_precise_normal_from_target(Object *hit_target, const Vector2 &bullet_pos, Vector2 &r_normal) {
	Node *target_node = Object::cast_to<Node>(hit_target);
	if (target_node == nullptr || !bullet_pos.is_finite()) {
		return false;
	}
	// Direct children only: Godot only registers CollisionShape2D nodes that
	// are direct children of the body/area, so deeper nesting can never
	// produce a collision record in the first place.
	TypedArray<Node> children = target_node->get_children();
	for (int k = 0; k < children.size(); ++k) {
		if (CollisionShape2D *cs = Object::cast_to<CollisionShape2D>(children[k])) {
			if (bounce_normal_from_shape_node(cs, bullet_pos, r_normal)) {
				return true;
			}
		}
	}
	return false;
}

int DirectionalBullets2D::try_handle_bounce(CollisionType collision_type, int bullet_index, int64_t entered_instance_id, Vector2 queued_target_velocity, bool queued_velocity_valid, Vector2 queued_target_position, bool queue_position_valid) {
	// Cached once: hit_target->get() with a fresh StringName per call pays
	// an interning lookup on every bounce drain.
	const StringName &prop_linear_velocity = CachedStringNames2D::get().linear_velocity;
	const StringName &prop_velocity = CachedStringNames2D::get().velocity;
	const StringName &prop_constant_linear_velocity = CachedStringNames2D::get().constant_linear_velocity;
	if (bounce_mask == 0) {
		return 0;
	}
	if (bullet_index < 0 || bullet_index >= amount_bullets) {
		return 0;
	}
	if (bullet_index >= (int)all_cached_direction.size() || bullet_index >= (int)all_cached_speed.size() || bullet_index >= (int)all_cached_velocity.size() || bullet_index >= (int)all_cached_instance_origin.size() || bullet_index >= (int)all_cached_instance_transforms.size()) {
		return 0;
	}
	if (!all_bullets_enabled_set.contains(bullet_index)) {
		return 0;
	}
	// Bounce budget exhausted: take the normal collision path instead.
	if (bullet_index < (int)all_bounce_count.size() && bounce_max_count > 0 && all_bounce_count[bullet_index] >= bounce_max_count) {
		return 0;
	}
	// One bounce per bullet per tick: a target carrying both a body and an
	// area queues two records for one overlap; the second would flip the
	// just-reflected heading straight back. Swallow it (fully handled).
	if (bullet_index < (int)all_bounce_last_tick.size() && all_bounce_last_tick[bullet_index] == bounce_tick_counter) {
		return 1;
	}
	// Cooldown: lets the bullet escape the overlap it just left. A free
	// bounce swallows the record (fully handled); a consumed hit still
	// counts through the normal path so wall contact inside the window is
	// never silently dropped (the per-tick guard above already stops the
	// body+area pair from double counting).
	if (bullet_index < (int)all_bounce_cooldown.size() && all_bounce_cooldown[bullet_index] > 0.0) {
		return bounce_hit_consumed ? 0 : 1;
	}
	Object *hit_target = ObjectDB::get_instance(entered_instance_id);
	if (hit_target == nullptr) {
		return 0;
	}
	// Bounce eligibility reads the TARGET's layer (Area2D/PhysicsBody2D both
	// expose collision_layer; anything else can never match).
	int target_layer = 0;
	const Variant layer_v = hit_target->get(CachedStringNames2D::get().collision_layer);
	if (layer_v.get_type() == Variant::INT) {
		target_layer = (int)layer_v;
	}
	// TileMapLayer walls report no collision_layer (internal bodies),
	// so they never match the mask above. Opt-in only: the exact cell
	// surface is unknowable from the record, so the normal section
	// below reflects head-on instead of guessing radial from the
	// (possibly far) layer origin. Default off keeps them lethal.
	const bool tilemap_head_on = target_layer == 0 && bounce_tilemap_layers && bounce_mask != 0 && Object::cast_to<TileMapLayer>(hit_target) != nullptr;
	if ((target_layer == 0 || (target_layer & bounce_mask) == 0) && !tilemap_head_on) {
		return 0;
	}
	// Same-target debounce: re-hits against the object just bounced off
	// (still overlapping it, sliding along it, steered straight back into
	// it) must not machine-gun the bullet. A free bounce swallows the
	// record; a consumed hit still counts through the normal path, same
	// contract as the cooldown above. Other targets bounce freely.
	if (bounce_debounce_sec > 0.0 && Math::is_finite((double)bounce_debounce_sec)
			&& bullet_index < (int)all_bounce_last_target.size() && bullet_index < (int)all_bounce_last_time.size()
			&& all_bounce_last_target[bullet_index] == entered_instance_id && Math::is_finite(curves_elapsed_time)) {
		const double since_bounce = curves_elapsed_time - all_bounce_last_time[bullet_index];
		if (Math::is_finite(since_bounce) && since_bounce >= 0.0 && since_bounce < (double)bounce_debounce_sec) {
			return bounce_hit_consumed ? 0 : 1;
		}
	}
	// A StayLocked orbit owns the displacement: bouncing would fight the
	// ring every tick, so the orbit wins and the hit takes the normal path.
	if (bullet_index < (int)all_orbiting_status.size() && bullet_index < (int)all_orbiting_data.size() && all_orbiting_status[bullet_index] && all_orbiting_data[bullet_index].is_locked_orbiting && all_orbiting_data[bullet_index].lock_policy == StayLocked) {
		return 0;
	}
	const Vector2 origin = all_cached_instance_origin[bullet_index];
	if (!origin.is_finite()) {
		return 0;
	}
	Vector2 dir = all_cached_direction[bullet_index];
	if (!dir.is_finite() || dir.length_squared() < 0.00000001) {
		// Zero heading (unseeded ballistics): recover from velocity, else
		// there is nothing meaningful to reflect.
		Vector2 v0 = all_cached_velocity[bullet_index] - inherited_velocity_offset;
		if (v0.is_finite() && v0.length_squared() > 0.00000001) {
			dir = v0.normalized();
		} else {
			return 0;
		}
	} else {
		dir = dir.normalized();
	}
	real_t speed = all_cached_speed[bullet_index];
	if (!Math::is_finite((double)speed) || speed < 0.0) {
		speed = 0.0;
	}
	// Surface normal: precise shape-analytic when asked (falls back), else
	// radial from the target center. Head-on fallback when neither resolves.
	Vector2 surface_n(0, 0);
	bool have_normal = false;
	if (tilemap_head_on) {
		surface_n = -dir;
		have_normal = true;
	} else if (bounce_mode == 1) {
		have_normal = bounce_precise_normal_from_target(hit_target, origin, surface_n);
	}
	if (!have_normal) {
		if (Node2D *target_n2d = Object::cast_to<Node2D>(hit_target)) {
			const Vector2 target_pos = target_n2d->get_global_position();
			if (target_pos.is_finite()) {
				const Vector2 radial = origin - target_pos;
				if (radial.is_finite() && radial.length_squared() > 0.00000001) {
					surface_n = radial.normalized();
					have_normal = true;
				}
			}
		}
	}
	if (!have_normal) {
		surface_n = -dir;
	}
	if (!surface_n.is_finite() || surface_n.length_squared() < 0.00000001) {
		return 0;
	}
	surface_n = surface_n.normalized();
	// Target motion: a pusher running into the bullet from behind must shove
	// it forward, never U-turn it. Prefer the queue-time snapshot: the
	// overlap is detected by the physics server but drained later in the
	// volley tick, and scripts can change the target's velocity in between
	// (a charger backing off reads as a pusher at drain and flips the
	// bounce forward through the target). Fall back to the live read when
	// the target exposed no velocity at queue time. Area2D, static and
	// animatable bodies report none and count as static either way.
	// Non-finite values fail safe to zero.
	Vector2 target_v(0, 0);
	if (queued_velocity_valid && queued_target_velocity.is_finite()) {
		target_v = queued_target_velocity;
	} else {
		bool has_velocity_property = false;
		const Variant linear_v = hit_target->get(prop_linear_velocity);
		if (linear_v.get_type() == Variant::VECTOR2) {
			target_v = (Vector2)linear_v;
			has_velocity_property = true;
		} else {
			const Variant vel_v = hit_target->get(prop_velocity);
			if (vel_v.get_type() == Variant::VECTOR2) {
				target_v = (Vector2)vel_v;
				has_velocity_property = true;
			} else {
				// AnimatableBody2D platforms expose neither of the above:
				// their motion lives in constant_linear_velocity (same
				// fallback as the queue-time reader above).
				const Variant const_v = hit_target->get(prop_constant_linear_velocity);
				if (const_v.get_type() == Variant::VECTOR2) {
					target_v = (Vector2)const_v;
					has_velocity_property = true;
				}
			}
		}
		if (!target_v.is_finite()) {
			target_v = Vector2(0, 0);
		}
		// Velocity-less estimate: Area2D hurtboxes, tweened bosses and
		// position-moved statics expose no motion property, so compare the
		// queue-time pose against the live one over one tick (records
		// queue during the physics flush and drain on the next volley
		// tick by construction; same-tick body+area pairs carry zero
		// displacement and self-neutralize). Divided by the last tick
		// delta so custom physics rates stay exact. Clamped: a blink
		// across the screen must not yield a 1e5 px/s phantom kick, and
		// any unusually stale record is bounded the same way.
		if (!has_velocity_property && queue_position_valid && queued_target_position.is_finite() && Math::is_finite((double)bounce_last_delta) && bounce_last_delta > 0.0) {
			Node2D *target_n2d = Object::cast_to<Node2D>(hit_target);
			if (target_n2d != nullptr) {
				const Vector2 now_pos = target_n2d->get_global_position();
				if (now_pos.is_finite()) {
					Vector2 estimate = (now_pos - queued_target_position) / (real_t)bounce_last_delta;
					if (estimate.is_finite()) {
						const real_t estimate_len = estimate.length();
						if (estimate_len > 4000.0) {
							estimate = estimate.normalized() * 4000.0;
						}
						target_v = estimate;
					}
				}
			}
		}
	}
	// Branch the influence by alignment: same-direction motion is a push
	// (surge allowed iff bounce_push_assist), opposing motion is a charge
	// (amplified iff bounce_charge_amplify). A zero target lands push-side
	// with identical math either way. Grazing flips are invisible: the
	// branch difference scales with the aligned motion, which vanishes at
	// the boundary. Everything below runs on the effective velocity, so a
	// disabled side behaves exactly as if the target stood still.
	const bool push_side = dir.dot(target_v) >= 0.0;
	Vector2 t_eff(0, 0);
	if ((push_side && bounce_push_assist) || (!push_side && bounce_charge_amplify)) {
		t_eff = target_v;
	}
	// Reflect the RELATIVE velocity, then ride the target back on. A static
	// target (or a disabled side) reduces to the absolute path exactly; a
	// pusher catching the bullet from behind surges it forward, and a
	// head-on charger amplifies the rebound. A degenerate relative motion
	// (co-moving touch, dead-stop bullet) falls through to the absolute
	// path, which reproduces the historical behavior for it (strength 0
	// keeps heading at zero speed).
	Vector2 incoming = dir * speed;
	bool use_relative = false;
	const Vector2 rel = dir * speed - t_eff;
	// A disabled side zeroes its effective velocity above, so this block
	// needs no further gating: off means the absolute bounce below.
	if (rel.is_finite() && rel.length_squared() >= 0.00000001) {
		// Separating repeat contact (tunneled and still inside, sliding
		// along, steered back in) is no impact at all: free bounces swallow
		// the record, consumed hits still count through the normal path
		// (same contract as the cooldown above). First contacts skip this
		// on purpose: a fresh overlap (teleport, spawn, tunnel entry) keeps
		// the historical bounce, so the guard can only ever silence a
		// target the bullet already bounced off, never a new one. Ping-pong
		// between two walls is untouched (each hit is a new target there).
		if (bullet_index >= 0 && bullet_index < (int)all_bounce_last_target.size() && all_bounce_last_target[bullet_index] == entered_instance_id) {
			if (rel.dot(surface_n) >= 0.0) {
				return bounce_hit_consumed ? 0 : 1;
			}
		}
		incoming = rel;
		use_relative = true;
		// Fresh overlap already separating in the relative frame (a target
		// that changed motion between contact and drain, e.g. backing off
		// faster than the bullet): reflecting it would flip the bounce
		// into the target and accelerate the bullet through it. The
		// absolute path below still separates correctly, so drop the
		// relative frame here. Repeat contacts never reach this: the
		// separating guard above already swallowed them.
		if (rel.dot(surface_n) > 0.0) {
			incoming = dir * speed;
			use_relative = false;
		}
	}
	// Reflect, scatter, scale. Dead-stop (strength 0) keeps the old heading
	// with zero speed instead of normalizing a zero vector into a stall.
	Vector2 refl = incoming.bounce(surface_n);
	if (bounce_randomness_deg > 0.0 && Math::is_finite((double)bounce_randomness_deg)) {
		const real_t jitter = UtilityFunctions::randf_range(-bounce_randomness_deg, bounce_randomness_deg);
		if (Math::is_finite((double)jitter)) {
			refl = refl.rotated(Math::deg_to_rad(jitter));
		}
	}
	refl *= bounce_strength;
	if (!refl.is_finite()) {
		return 0;
	}
	if (use_relative) {
		// Ride the target back on (see above): the surge on a push, the
		// extra kick on a head-on charge. Rejects a cancelled reflection.
		// The ride must never point the outcome back into the target (a
		// faster co-directional target would otherwise flip the bounce
		// forward through it): drop the ride and keep the reflection.
		const Vector2 ridden = refl + t_eff;
		if (ridden.is_finite() && ridden.dot(surface_n) >= 0.0) {
			refl = ridden;
		}
		if (!refl.is_finite()) {
			return 0;
		}
	}
	real_t new_speed = refl.length();
	// Length can overflow to +Inf even when the components are finite
	// (float Vector2: ~1e38 components overflow the hypotenuse). Refuse
	// the bounce instead of storing an Inf speed that poisons every later
	// tick's movement, curves, and interpolation.
	if (!Math::is_finite((double)new_speed)) {
		return 0;
	}
	Vector2 new_dir = (new_speed > 0.0001) ? (refl / new_speed) : dir;
	if (!new_dir.is_finite()) {
		new_dir = dir;
	}
	// Strength is uncapped by design: a strong bounce raises the cached
	// ceiling instead of clamping back down, so the boost survives the
	// next tick's acceleration clamp. The per-bullet multiplier carries
	// the same boost across curve overwrites (curves rewrite speed every
	// tick, see the speed section); plain accel carries it inside the
	// cached speed. Multipliers accumulate across bounces in one life.
	// Deferred until the velocity commit below proves finite (see the
	// overflow guard there): a refused bounce must not raise ceilings.
	const real_t pending_ceiling_raise = (bullet_index < (int)all_cached_max_speed.size() && all_cached_max_speed[bullet_index] > 0.0 && new_speed > all_cached_max_speed[bullet_index]) ? new_speed : (real_t)-1.0;
	// A non-StayLocked lock breaks: the bullet left the ring by definition.
	if (bullet_index < (int)all_orbiting_status.size() && bullet_index < (int)all_orbiting_data.size() && all_orbiting_status[bullet_index] && all_orbiting_data[bullet_index].is_locked_orbiting) {
		all_orbiting_data[bullet_index].is_locked_orbiting = false;
	}
	// Commit ballistics. Gravity fall speed reflects too so arcs continue
	// naturally; the inherited wind offset rides along untouched.
	// The composed velocity is guarded as a whole BEFORE any write:
	// individually finite parts (unit direction, huge speed, wind, gravity)
	// can still overflow their SUM (float Vector2 saturates near ~3.4e38).
	// A non-finite total refuses the bounce with ballistics untouched, so
	// no Inf speed/velocity ever poisons movement, curves, or
	// interpolation for the rest of the volley's life.
	Vector2 commit_gravity = Vector2(0, 0);
	if (bullet_index < (int)all_gravity_velocity.size()) {
		const Vector2 gv = all_gravity_velocity[bullet_index];
		if (gv.is_finite() && gv.length_squared() > 0.0) {
			// Gravity stays absolute on purpose (it is environmental fall,
			// not contact motion): relativizing it would fling arcs near
			// fast targets instead of continuing them naturally.
			const Vector2 gr = gv.bounce(surface_n) * bounce_strength;
			commit_gravity = gr.is_finite() ? gr : Vector2(0, 0);
		} else if (gv.is_finite()) {
			commit_gravity = gv;
		}
	}
	const Vector2 commit_velocity = new_dir * new_speed + inherited_velocity_offset + commit_gravity;
	if (!commit_velocity.is_finite()) {
		return 0;
	}
	all_cached_direction[bullet_index] = new_dir;
	all_cached_speed[bullet_index] = new_speed;
	// Ceiling raise lands only on a committed bounce: an overflowing hit
	// must not lift the clamp for later ticks (acceleration would then
	// chase an unreachable ceiling forever).
	if (pending_ceiling_raise > 0.0 && bullet_index < (int)all_cached_max_speed.size()) {
		all_cached_max_speed[bullet_index] = pending_ceiling_raise;
	}
	if (bullet_index >= 0) {
		if (bullet_index >= (int)all_bounce_speed_multiplier.size()) {
			ensure_bounce_vectors();
		}
		if (bullet_index < (int)all_bounce_speed_multiplier.size()) {
			const real_t updated = all_bounce_speed_multiplier[bullet_index] * bounce_strength;
			all_bounce_speed_multiplier[bullet_index] = Math::is_finite((double)updated) ? updated : (real_t)1.0;
			bounce_speed_scaled = true;
		}
	}
	if (bullet_index < (int)all_gravity_velocity.size()) {
		all_gravity_velocity[bullet_index] = commit_gravity;
	}
	all_cached_velocity[bullet_index] = commit_velocity;
	// Nudge out of the overlap along the normal so the next tick starts
	// clean (no immediate re-hit). Position continuity is preserved for
	// interpolation (prev untouched: the render lerps out of the wall).
	real_t bound_radius = cached_circle_radius;
	if (cached_effective_shape_type == PhysicsServer2D::SHAPE_RECTANGLE) {
		bound_radius = MIN(cached_rect_size.x, cached_rect_size.y) * 0.5;
	} else if (cached_effective_shape_type == PhysicsServer2D::SHAPE_CAPSULE) {
		bound_radius = cached_capsule_height * 0.5;
	}
	if (!Math::is_finite((double)bound_radius) || bound_radius <= 0.0) {
		bound_radius = 8.0;
	}
	const Vector2 new_origin = origin + surface_n * (bound_radius + 2.0);
	if (new_origin.is_finite()) {
		const Vector2 shift = new_origin - origin;
		all_cached_instance_origin[bullet_index] = new_origin;
		all_cached_instance_transforms[bullet_index].set_origin(new_origin);
		sync_shape_transform_from_instance(bullet_index, all_cached_instance_transforms[bullet_index]);
		carry_attachment_with_transform(bullet_index, all_cached_instance_transforms[bullet_index], shift);
	}
	// Visual follows the reflection. Snap now (same contract as homing:
	// prev synced so the sprite never lags a frame), or arm the smooth
	// pursuit that the movement tick slews toward. adjust_direction owns
	// ballistics through the visual, so it must snap.
	if (bounce_rotate_texture) {
		if (bounce_rotation_smooth > 0.0 && Math::is_finite((double)bounce_rotation_smooth) && !adjust_direction_based_on_rotation) {
			if (bullet_index < (int)bounce_visual_pending.size() && bullet_index < (int)bounce_visual_target.size()) {
				bounce_visual_pending[bullet_index] = 1;
				bounce_visual_target[bullet_index] = new_dir;
				if (bounce_last_delta > 0.0) {
					rotate_to_target(bullet_index, new_dir, bounce_rotation_smooth * (real_t)bounce_last_delta, false);
				}
			}
		} else {
			// Snap now. rotate_to_target syncs the whole previous transform,
			// which would also snap the render position to the nudged origin;
			// restore the previous origin so position keeps lerping while
			// only the rotation snaps.
			Vector2 prev_origin(0, 0);
			bool have_prev = bullet_index >= 0 && bullet_index < (int)all_previous_instance_transf.size();
			if (have_prev) {
				prev_origin = all_previous_instance_transf[bullet_index].get_origin();
			}
			rotate_to_target(bullet_index, new_dir, 0.0, false);
			if (have_prev && bullet_index < (int)all_previous_instance_transf.size()) {
				all_previous_instance_transf[bullet_index].set_origin(prev_origin);
			}
		}
	}
	if (bullet_index < (int)all_bounce_count.size()) {
		++all_bounce_count[bullet_index];
	}
	// Forensics for debug_get_bounce_info: the normal and target motion
	// this bounce committed with (size-checked like every ledger write).
	if (bullet_index >= 0) {
		if (bullet_index < (int)all_bounce_last_normal.size()) {
			all_bounce_last_normal[bullet_index] = surface_n;
		}
		if (bullet_index < (int)all_bounce_last_target_velocity.size()) {
			all_bounce_last_target_velocity[bullet_index] = target_v;
		}
	}
	if (bullet_index < (int)all_bounce_cooldown.size()) {
		all_bounce_cooldown[bullet_index] = bounce_cooldown_sec;
	}
	if (bullet_index < (int)all_bounce_last_tick.size()) {
		all_bounce_last_tick[bullet_index] = bounce_tick_counter;
	}
	// Bounce sparks own this record (a consumed bounce falls through to the
	// counter below, but must not double-fire the hit spark there).
	fx_fire_oneshot(EFFECT_ON_BOUNCE, bullet_index, all_cached_instance_transforms[bullet_index]);
	// Arm the same-target debounce: further records against THIS object
	// inside the window never re-bounce (see the check above).
	if (bullet_index < (int)all_bounce_last_target.size()) {
		all_bounce_last_target[bullet_index] = entered_instance_id;
	}
	if (bullet_index < (int)all_bounce_last_time.size() && Math::is_finite(curves_elapsed_time)) {
		all_bounce_last_time[bullet_index] = curves_elapsed_time;
	}
	// Slim bounce signal, same ownership routing as collisions: spawner
	// volleys report to their spawner, the rest to the factory. Snapshot the
	// emitter first (a consumed hit below may pool the volley), guard the
	// post-emit with the self-liveness token like the normal path.
	Object *emitter = resolve_signal_emitter();
	const uint64_t self_id = get_instance_id();
	if (emitter != nullptr) {
		if (emitter == bullet_factory) {
			if (collision_type == CollisionType::AREA) {
				emitter->emit_signal(CachedStringNames2D::get().directional_bounce_area_entered, hit_target, this, bullet_index);
			} else {
				emitter->emit_signal(CachedStringNames2D::get().directional_bounce_body_entered, hit_target, this, bullet_index);
			}
		} else {
			if (collision_type == CollisionType::AREA) {
				emitter->emit_signal(CachedStringNames2D::get().bounce_area_entered, hit_target, this, bullet_index);
			} else {
				emitter->emit_signal(CachedStringNames2D::get().bounce_body_entered, hit_target, this, bullet_index);
			}
		}
	}
	// A handler that freed this volley leaves every member access below as
	// use-after-free: swallow the record so the base path returns instantly
	// without touching members (misuse is still prohibited by contract).
	if (ObjectDB::get_instance(ObjectID(self_id)) != this || is_queued_for_deletion()) {
		return 1;
	}
	return bounce_hit_consumed ? 2 : 1;
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

	ADD_GROUP("Movement Speed", "");
	ClassDB::bind_method(D_METHOD("get_shared_bullet_speed_data"), &DirectionalBullets2D::get_shared_bullet_speed_data);
	ClassDB::bind_method(D_METHOD("set_shared_bullet_speed_data", "new_speed_data"), &DirectionalBullets2D::set_shared_bullet_speed_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_bullet_speed_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletSpeedData2D"), "set_shared_bullet_speed_data", "get_shared_bullet_speed_data");

	// Get/set methods live on the base class (bound there so BlockBullets2D
	// gets them too); only the property is declared here.
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "inherited_velocity_offset"), "set_inherited_velocity_offset", "get_inherited_velocity_offset");

	ADD_GROUP("Bullet Rotation", "");

	ClassDB::bind_method(D_METHOD("get_adjust_direction_based_on_rotation"), &DirectionalBullets2D::get_adjust_direction_based_on_rotation);
	ClassDB::bind_method(D_METHOD("set_adjust_direction_based_on_rotation", "value"), &DirectionalBullets2D::set_adjust_direction_based_on_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "adjust_direction_based_on_rotation"), "set_adjust_direction_based_on_rotation", "get_adjust_direction_based_on_rotation");

	ClassDB::bind_method(D_METHOD("get_shared_bullet_rotation_data"), &DirectionalBullets2D::get_shared_bullet_rotation_data);
	ClassDB::bind_method(D_METHOD("set_shared_bullet_rotation_data", "new_rotation_data"), &DirectionalBullets2D::set_shared_bullet_rotation_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_bullet_rotation_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletRotationData2D"), "set_shared_bullet_rotation_data", "get_shared_bullet_rotation_data");

	ADD_GROUP("Wobble", "");
	ClassDB::bind_method(D_METHOD("get_shared_bullet_wobble_data"), &DirectionalBullets2D::get_shared_bullet_wobble_data);
	ClassDB::bind_method(D_METHOD("set_shared_bullet_wobble_data", "new_wobble_data"), &DirectionalBullets2D::set_shared_bullet_wobble_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_bullet_wobble_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletWobbleData2D"), "set_shared_bullet_wobble_data", "get_shared_bullet_wobble_data");


	ADD_GROUP("Gravity", "");
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

	ADD_GROUP("Bounce and Ricochet", "");
	// BOUNCE / RICOCHET RUNTIME API (spawn-data equivalents, editable live).
	ClassDB::bind_method(D_METHOD("get_bounce_mask"), &DirectionalBullets2D::get_bounce_mask);
	ClassDB::bind_method(D_METHOD("set_bounce_mask", "value"), &DirectionalBullets2D::set_bounce_mask);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bounce_mask", PROPERTY_HINT_LAYERS_2D_PHYSICS), "set_bounce_mask", "get_bounce_mask");
	ClassDB::bind_method(D_METHOD("set_bounce_mask_from_array", "array_of_masks"), &DirectionalBullets2D::set_bounce_mask_from_array);
	ClassDB::bind_method(D_METHOD("get_bounce_tilemap_layers"), &DirectionalBullets2D::get_bounce_tilemap_layers);
	ClassDB::bind_method(D_METHOD("set_bounce_tilemap_layers", "value"), &DirectionalBullets2D::set_bounce_tilemap_layers);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bounce_tilemap_layers"), "set_bounce_tilemap_layers", "get_bounce_tilemap_layers");
	ClassDB::bind_method(D_METHOD("get_bounce_strength"), &DirectionalBullets2D::get_bounce_strength);
	ClassDB::bind_method(D_METHOD("set_bounce_strength", "value"), &DirectionalBullets2D::set_bounce_strength);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bounce_strength"), "set_bounce_strength", "get_bounce_strength");
	ClassDB::bind_method(D_METHOD("get_bounce_push_assist"), &DirectionalBullets2D::get_bounce_push_assist);
	ClassDB::bind_method(D_METHOD("set_bounce_push_assist", "value"), &DirectionalBullets2D::set_bounce_push_assist);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bounce_push_assist"), "set_bounce_push_assist", "get_bounce_push_assist");
	ClassDB::bind_method(D_METHOD("get_bounce_charge_amplify"), &DirectionalBullets2D::get_bounce_charge_amplify);
	ClassDB::bind_method(D_METHOD("set_bounce_charge_amplify", "value"), &DirectionalBullets2D::set_bounce_charge_amplify);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bounce_charge_amplify"), "set_bounce_charge_amplify", "get_bounce_charge_amplify");
	ClassDB::bind_method(D_METHOD("get_bounce_hit_consumed"), &DirectionalBullets2D::get_bounce_hit_consumed);
	ClassDB::bind_method(D_METHOD("set_bounce_hit_consumed", "value"), &DirectionalBullets2D::set_bounce_hit_consumed);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bounce_hit_consumed"), "set_bounce_hit_consumed", "get_bounce_hit_consumed");
	ClassDB::bind_method(D_METHOD("get_bounce_max_count"), &DirectionalBullets2D::get_bounce_max_count);
	ClassDB::bind_method(D_METHOD("set_bounce_max_count", "value"), &DirectionalBullets2D::set_bounce_max_count);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bounce_max_count", PROPERTY_HINT_RANGE, "0,1000000,1"), "set_bounce_max_count", "get_bounce_max_count");
	ClassDB::bind_method(D_METHOD("get_bounce_mode"), &DirectionalBullets2D::get_bounce_mode);
	ClassDB::bind_method(D_METHOD("set_bounce_mode", "value"), &DirectionalBullets2D::set_bounce_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bounce_mode", PROPERTY_HINT_ENUM, "Simple Radial,Precise Shape"), "set_bounce_mode", "get_bounce_mode");
	ClassDB::bind_method(D_METHOD("get_bounce_rotate_texture"), &DirectionalBullets2D::get_bounce_rotate_texture);
	ClassDB::bind_method(D_METHOD("set_bounce_rotate_texture", "value"), &DirectionalBullets2D::set_bounce_rotate_texture);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bounce_rotate_texture"), "set_bounce_rotate_texture", "get_bounce_rotate_texture");
	ClassDB::bind_method(D_METHOD("get_bounce_rotation_smooth"), &DirectionalBullets2D::get_bounce_rotation_smooth);
	ClassDB::bind_method(D_METHOD("set_bounce_rotation_smooth", "value"), &DirectionalBullets2D::set_bounce_rotation_smooth);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bounce_rotation_smooth"), "set_bounce_rotation_smooth", "get_bounce_rotation_smooth");
	ClassDB::bind_method(D_METHOD("get_bounce_randomness_deg"), &DirectionalBullets2D::get_bounce_randomness_deg);
	ClassDB::bind_method(D_METHOD("set_bounce_randomness_deg", "value"), &DirectionalBullets2D::set_bounce_randomness_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bounce_randomness_deg", PROPERTY_HINT_RANGE, "0,180,0.1"), "set_bounce_randomness_deg", "get_bounce_randomness_deg");
	ClassDB::bind_method(D_METHOD("get_bounce_cooldown_sec"), &DirectionalBullets2D::get_bounce_cooldown_sec);
	ClassDB::bind_method(D_METHOD("set_bounce_cooldown_sec", "value"), &DirectionalBullets2D::set_bounce_cooldown_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bounce_cooldown_sec"), "set_bounce_cooldown_sec", "get_bounce_cooldown_sec");
	ClassDB::bind_method(D_METHOD("get_bounce_debounce_sec"), &DirectionalBullets2D::get_bounce_debounce_sec);
	ClassDB::bind_method(D_METHOD("set_bounce_debounce_sec", "value"), &DirectionalBullets2D::set_bounce_debounce_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bounce_debounce_sec"), "set_bounce_debounce_sec", "get_bounce_debounce_sec");

	ClassDB::bind_method(D_METHOD("bullet_get_bounce_count", "bullet_index"), &DirectionalBullets2D::bullet_get_bounce_count);
	ClassDB::bind_method(D_METHOD("all_bullets_get_bounce_count", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_get_bounce_count, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("debug_get_bounce_info", "bullet_index"), &DirectionalBullets2D::debug_get_bounce_info);
	ClassDB::bind_method(D_METHOD("debug_get_previous_origin", "bullet_index"), &DirectionalBullets2D::debug_get_previous_origin);

	ClassDB::bind_method(D_METHOD("get_is_wobble_enabled"), &DirectionalBullets2D::get_is_wobble_enabled);
	ClassDB::bind_method(D_METHOD("bullet_get_wobble_amplitude", "bullet_index"), &DirectionalBullets2D::bullet_get_wobble_amplitude);
	ClassDB::bind_method(D_METHOD("bullet_get_wobble_face_movement_direction", "bullet_index"), &DirectionalBullets2D::bullet_get_wobble_face_movement_direction);
	ClassDB::bind_method(D_METHOD("bullet_get_wobble_face_rotation_speed", "bullet_index"), &DirectionalBullets2D::bullet_get_wobble_face_rotation_speed);
	ClassDB::bind_method(D_METHOD("bullet_set_wobble_data", "bullet_index", "wobble_data"), &DirectionalBullets2D::bullet_set_wobble_data);
	ClassDB::bind_method(D_METHOD("all_bullets_set_wobble_data", "wobble_data", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_set_wobble_data, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("bullet_get_wobble_data", "bullet_index"), &DirectionalBullets2D::bullet_get_wobble_data);
	ClassDB::bind_method(D_METHOD("all_bullets_get_wobble_data", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_get_wobble_data, DEFVAL(0), DEFVAL(-1));
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

	// SHARED MOVEMENT PATTERN RUNTIME API (spawn-data equivalent, editable live).

	// SHARED MOVEMENT PATTERN RUNTIME API (spawn-data equivalent, editable live).
	ADD_GROUP("Movement Pattern Paths", "");
	ClassDB::bind_method(D_METHOD("get_shared_movement_pattern_curve"), &DirectionalBullets2D::get_shared_movement_pattern_curve);
	ClassDB::bind_method(D_METHOD("set_shared_movement_pattern_curve", "new_curve"), &DirectionalBullets2D::set_shared_movement_pattern_curve);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shared_movement_pattern_curve", PROPERTY_HINT_RESOURCE_TYPE, "Curve2D"), "set_shared_movement_pattern_curve", "get_shared_movement_pattern_curve");

	ClassDB::bind_method(D_METHOD("get_shared_movement_pattern_face_movement_direction"), &DirectionalBullets2D::get_shared_movement_pattern_face_movement_direction);
	ClassDB::bind_method(D_METHOD("set_shared_movement_pattern_face_movement_direction", "value"), &DirectionalBullets2D::set_shared_movement_pattern_face_movement_direction);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shared_movement_pattern_face_movement_direction"), "set_shared_movement_pattern_face_movement_direction", "get_shared_movement_pattern_face_movement_direction");

	ClassDB::bind_method(D_METHOD("get_shared_movement_pattern_repeat"), &DirectionalBullets2D::get_shared_movement_pattern_repeat);
	ClassDB::bind_method(D_METHOD("set_shared_movement_pattern_repeat", "value"), &DirectionalBullets2D::set_shared_movement_pattern_repeat);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shared_movement_pattern_repeat"), "set_shared_movement_pattern_repeat", "get_shared_movement_pattern_repeat");

	ADD_GROUP("Homing", "");
	ClassDB::bind_method(D_METHOD("get_bullet_homing_auto_pop_after_target_reached"), &DirectionalBullets2D::get_bullet_homing_auto_pop_after_target_reached);
	ClassDB::bind_method(D_METHOD("set_bullet_homing_auto_pop_after_target_reached", "value"), &DirectionalBullets2D::set_bullet_homing_auto_pop_after_target_reached);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bullet_homing_auto_pop_after_target_reached"), "set_bullet_homing_auto_pop_after_target_reached", "get_bullet_homing_auto_pop_after_target_reached");

	// SHARED HOMING DEQUE POP METHODS
	ClassDB::bind_method(D_METHOD("shared_homing_deque_pop_front_target"), &DirectionalBullets2D::shared_homing_deque_pop_front_target);
	ClassDB::bind_method(D_METHOD("shared_homing_deque_pop_back_target"), &DirectionalBullets2D::shared_homing_deque_pop_back_target);
	ClassDB::bind_method(D_METHOD("_do_shared_auto_pop_front_target", "operation_generation", "front_epoch"), &DirectionalBullets2D::_do_shared_auto_pop_front_target);
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

	ClassDB::bind_method(D_METHOD("get_homing_delay_sec"), &DirectionalBullets2D::get_homing_delay_sec);
	ClassDB::bind_method(D_METHOD("set_homing_delay_sec", "value"), &DirectionalBullets2D::set_homing_delay_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_delay_sec"), "set_homing_delay_sec", "get_homing_delay_sec");

	ClassDB::bind_method(D_METHOD("get_homing_duration_sec"), &DirectionalBullets2D::get_homing_duration_sec);
	ClassDB::bind_method(D_METHOD("set_homing_duration_sec", "value"), &DirectionalBullets2D::set_homing_duration_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_duration_sec"), "set_homing_duration_sec", "get_homing_duration_sec");

	ClassDB::bind_method(D_METHOD("get_homing_lose_range_px"), &DirectionalBullets2D::get_homing_lose_range_px);
	ClassDB::bind_method(D_METHOD("set_homing_lose_range_px", "value"), &DirectionalBullets2D::set_homing_lose_range_px);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_lose_range_px"), "set_homing_lose_range_px", "get_homing_lose_range_px");



	ClassDB::bind_method(D_METHOD("has_shared_movement_pattern"), &DirectionalBullets2D::has_shared_movement_pattern);
	ClassDB::bind_method(D_METHOD("remove_shared_movement_pattern"), &DirectionalBullets2D::remove_shared_movement_pattern);

	// SHARED SPEED / ROTATION RUNTIME API.

	ClassDB::bind_method(D_METHOD("has_shared_bullet_speed_data"), &DirectionalBullets2D::has_shared_bullet_speed_data);
	ClassDB::bind_method(D_METHOD("remove_shared_bullet_speed_data"), &DirectionalBullets2D::remove_shared_bullet_speed_data);

	ClassDB::bind_method(D_METHOD("has_shared_bullet_rotation_data"), &DirectionalBullets2D::has_shared_bullet_rotation_data);
	ClassDB::bind_method(D_METHOD("remove_shared_bullet_rotation_data"), &DirectionalBullets2D::remove_shared_bullet_rotation_data);

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
