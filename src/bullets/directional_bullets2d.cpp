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
void DirectionalBullets2D::set_up_movement_data(const TypedArray<BulletSpeedData2D> &new_speed_data) {
	int speed_data_size = new_speed_data.size();

	// Ensure vectors are the correct size before we start indexing
	if ((int)all_cached_speed.size() != amount_bullets) {
		all_cached_speed.resize(amount_bullets);
		all_cached_max_speed.resize(amount_bullets);
		all_cached_acceleration.resize(amount_bullets);
		all_cached_direction.resize(amount_bullets);
		all_cached_velocity.resize(amount_bullets);
	}

	// In case not enough speed data was provided, we will use the first element as fallback for all bullets
	bool use_per_bullet = (speed_data_size == amount_bullets);
	Ref<BulletSpeedData2D> fallback_data;

	if (!use_per_bullet) {
		if (speed_data_size > 0) {
			fallback_data = new_speed_data[0];
		}

		// In case no speed data was provided at all, create a default one (everything set to 0 by default)
		if (fallback_data.is_null()) {
			fallback_data.instantiate();
		}
	}

	for (int i = 0; i < amount_bullets; ++i) {
		const real_t rot = all_cached_shape_transforms[i].get_rotation();

		Ref<BulletSpeedData2D> data = fallback_data;

		if (use_per_bullet) {
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

void DirectionalBullets2D::apply_per_bullet_curves_from_data(const DirectionalBulletsData2D &directional_data) {
	// Seatbelt: base spawn() sizes this before the custom logic runs, and
	// enable preserves the size - but never index an unsized vector.
	if ((int)all_bullet_curves_data.size() != amount_bullets) {
		all_bullet_curves_data.assign(amount_bullets, Ref<BulletCurvesData2D>());
	}
	for (int i = 0; i < amount_bullets; ++i) {
		const int entry = resolve_per_bullet_data_index(directional_data.all_bullet_curves_data.size(), i);
		if (entry < 0) {
			return; // empty = off
		}
		Ref<BulletCurvesData2D> curves = directional_data.all_bullet_curves_data[entry];
		if (curves.is_null()) {
			continue;
		}
		populate_individual_bullet_curves_related_data(i, curves);
	}
}

void DirectionalBullets2D::apply_per_bullet_movement_patterns_from_data(const DirectionalBulletsData2D &directional_data) {
	for (int i = 0; i < amount_bullets; ++i) {
		const int entry = resolve_per_bullet_data_index(directional_data.all_bullet_movement_pattern_curves.size(), i);
		if (entry < 0) {
			return; // empty = off
		}
		Ref<Curve2D> curve = directional_data.all_bullet_movement_pattern_curves[entry];
		if (curve.is_null()) {
			continue;
		}
		// Flags resolve per bullet when sized, otherwise the shared flags drive.
		const bool face = (directional_data.all_bullet_movement_pattern_face_movement_directions.size() == amount_bullets)
				? (bool)directional_data.all_bullet_movement_pattern_face_movement_directions[i]
				: directional_data.shared_movement_pattern_face_movement_direction;
		const bool repeat = (directional_data.all_bullet_movement_pattern_repeats.size() == amount_bullets)
				? (bool)directional_data.all_bullet_movement_pattern_repeats[i]
				: directional_data.shared_movement_pattern_repeat;
		set_bullet_movement_pattern_from_curve(i, curve, face, repeat);
	}
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
	if (bullet_factory == nullptr) {
		UtilityFunctions::push_error("DirectionalBullets2D: cannot resolve shared_movement_pattern_path without a BulletFactory2D (multimesh was never spawned through one).");
		return;
	}
	// A Path2D node cannot live inside a Resource, so the path is stored and
	// resolved here. Relative to the factory (stable landmark, always in the
	// tree at spawn/enable time), with a scene-root fallback for editor-picked
	// paths. Wrong-type or missing targets warn and clear the slot; the Curve2D
	// itself is extracted by storing the resolved Path2D's curve below.
	Node *node = bullet_factory->get_node_or_null(directional_data.shared_movement_pattern_path);
	if (node == nullptr) {
		SceneTree *tree = bullet_factory->get_tree();
		Node *current_scene = (tree != nullptr) ? tree->get_current_scene() : nullptr;
		if (current_scene != nullptr) {
			node = current_scene->get_node_or_null(directional_data.shared_movement_pattern_path);
		}
	}
	Path2D *path = (node != nullptr) ? Object::cast_to<Path2D>(node) : nullptr;
	if (path == nullptr) {
		if (node == nullptr) {
			UtilityFunctions::push_warning("DirectionalBullets2D: shared_movement_pattern_path target not found, shared movement pattern removed.");
		} else {
			UtilityFunctions::push_warning("DirectionalBullets2D: shared_movement_pattern_path node is not a Path2D, shared movement pattern removed.");
		}
		shared_movement_pattern_curve.unref();
		shared_movement_pattern_face_movement_direction = false;
		shared_movement_pattern_repeat = true;
		shared_movement_pattern_distances.assign(amount_bullets, 0.0);
		return;
	}
	const Ref<Curve2D> &curve = path->get_curve();
	if (curve.is_null()) {
		UtilityFunctions::push_warning("DirectionalBullets2D: shared_movement_pattern_path Path2D holds no Curve2D, shared movement pattern removed.");
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
	set_up_movement_data(TypedArray<BulletSpeedData2D>());
	adjust_direction_based_on_rotation = false;
	all_bullet_homing_targets.resize(amount_bullets);
	all_homing_count.assign(amount_bullets, 0);
	all_bullet_homing_smoothing.assign(amount_bullets, 0.0);
	use_per_bullet_homing_smoothing = false;
	all_orbiting_data.resize(amount_bullets);
	all_orbiting_status.assign(amount_bullets, 0);
	all_shared_homing_reached.resize(amount_bullets);
	homing_inert_warning_issued = false;
	// Parity with custom_additional_enable_logic: spawn must not inherit counters or
	// the mouse cache from a previous owner either (defensive - fresh instances start
	// zeroed, but the invariant belongs here in one place).
	active_homing_count = 0;
	active_orbiting_count = 0;
	cached_mouse_global_position = Vector2(0, 0);
	if (directional_data == nullptr) {
		UtilityFunctions::push_error("DirectionalBullets2D::spawn got wrong spawn data type, expected DirectionalBulletsData2D.");
		return;
	}

	set_up_movement_data(directional_data->all_bullet_speed_data);

	adjust_direction_based_on_rotation = directional_data->adjust_direction_based_on_rotation;

	// Shared spawn-data speed/rotation take precedence over the arrays when
	// set (a single entry fans out to every bullet through the same
	// machinery). Null hands control back to the arrays (vectors were just
	// re-seeded above), and the members mirror the data so the runtime
	// getters stay truthful across pool reuse.
	shared_bullet_speed_data = directional_data->shared_bullet_speed_data;
	if (shared_bullet_speed_data.is_valid()) {
		TypedArray<BulletSpeedData2D> single_speed;
		single_speed.push_back(shared_bullet_speed_data);
		set_up_movement_data(single_speed);
	}
	shared_bullet_rotation_data = directional_data->shared_bullet_rotation_data;
	if (shared_bullet_rotation_data.is_valid()) {
		TypedArray<BulletRotationData2D> single_rotation;
		single_rotation.push_back(shared_bullet_rotation_data);
		set_rotation_data(single_rotation, data.rotate_only_textures);
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
}

void DirectionalBullets2D::custom_additional_enable_logic(const MultiMeshBulletsData2D &data) {
	const DirectionalBulletsData2D *directional_data = Object::cast_to<DirectionalBulletsData2D>(&data);
	if (directional_data == nullptr) {
		UtilityFunctions::push_error("DirectionalBullets2D::enable got wrong spawn data type, expected DirectionalBulletsData2D.");
		return;
	}

	// Get the list of connections for the signal
	TypedArray<Dictionary> connections = get_signal_connection_list("bullet_homing_target_reached");

	// Iterate through all connections and disconnect them
	for (int i = 0; i < connections.size(); ++i) {
		Dictionary connection = connections[i];
		Callable callable = connection["callable"];
		disconnect("bullet_homing_target_reached", callable);
	}

	set_up_movement_data(directional_data->all_bullet_speed_data);

	adjust_direction_based_on_rotation = directional_data->adjust_direction_based_on_rotation;

	// Shared spawn-data speed/rotation (same as spawn; enable runs on every
	// pool reuse). Members mirror the data so runtime getters stay truthful.
	shared_bullet_speed_data = directional_data->shared_bullet_speed_data;
	if (shared_bullet_speed_data.is_valid()) {
		TypedArray<BulletSpeedData2D> single_speed;
		single_speed.push_back(shared_bullet_speed_data);
		set_up_movement_data(single_speed);
	}
	shared_bullet_rotation_data = directional_data->shared_bullet_rotation_data;
	if (shared_bullet_rotation_data.is_valid()) {
		TypedArray<BulletRotationData2D> single_rotation;
		single_rotation.push_back(shared_bullet_rotation_data);
		set_rotation_data(single_rotation, data.rotate_only_textures);
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

	// Vectors are sized in spawn, but a wrong-type spawn early-returns before
	// sizing. Resize here so the clears/assigns below can't run on empty vectors.
	all_bullet_homing_targets.resize(amount_bullets);
	all_homing_count.resize(amount_bullets, 0);
	all_bullet_homing_smoothing.resize(amount_bullets, 0.0);
	all_orbiting_data.resize(amount_bullets);
	all_orbiting_status.resize(amount_bullets, 0);
	all_shared_homing_reached.resize(amount_bullets);

	// Homing

	// Ensure all old homing targets are cleared. Do NOT clear the vector ever.
	for (auto &queue : all_bullet_homing_targets) {
		queue.clear_homing_targets(cached_mouse_global_position); // Passing garbage mouse global position but its fine
	}
	active_homing_count = 0;
	all_homing_count.assign(amount_bullets, 0);
	all_bullet_homing_smoothing.assign(amount_bullets, 0.0);
	use_per_bullet_homing_smoothing = false;

	shared_homing_deque.clear_homing_targets(cached_mouse_global_position); // Passing garbage mouse global position but its fine
	reset_shared_homing_reached_state();
	//

	// Orbiting

	all_orbiting_status.assign(amount_bullets, 0); // Initialize all orbiting status to disabled
	active_orbiting_count = 0;

	//

	homing_update_interval = 0.0;
	homing_update_timer = 0.0;
	homing_smoothing = 0.0;
	homing_take_control_of_texture_rotation = false;
	homing_inert_warning_issued = false;

	homing_distance_before_reached = 5.0;
	bullet_homing_auto_pop_after_target_reached = false;
	shared_homing_deque_auto_pop_after_target_reached = false;
}

void DirectionalBullets2D::custom_additional_disable_logic() {
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
	ClassDB::bind_method(D_METHOD("_do_shared_auto_pop_front_target"), &DirectionalBullets2D::_do_shared_auto_pop_front_target);

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

	ClassDB::bind_method(D_METHOD("get_homing_update_interval"), &DirectionalBullets2D::get_homing_update_interval);
	ClassDB::bind_method(D_METHOD("set_homing_update_interval", "value"), &DirectionalBullets2D::set_homing_update_interval);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_update_interval"), "set_homing_update_interval", "get_homing_update_interval");

	ClassDB::bind_method(D_METHOD("get_homing_take_control_of_texture_rotation"), &DirectionalBullets2D::get_homing_take_control_of_texture_rotation);
	ClassDB::bind_method(D_METHOD("set_homing_take_control_of_texture_rotation", "value"), &DirectionalBullets2D::set_homing_take_control_of_texture_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_take_control_of_texture_rotation"), "set_homing_take_control_of_texture_rotation", "get_homing_take_control_of_texture_rotation");

	// ORBITING RELATED

	ClassDB::bind_method(D_METHOD("bullet_enable_orbiting", "bullet_index", "orbiting_radius", "orbiting_direction", "orbiting_texture_rotation"), &DirectionalBullets2D::bullet_enable_orbiting, DEFVAL(OrbitRight), DEFVAL(FaceTarget));
	ClassDB::bind_method(D_METHOD("bullet_disable_orbiting", "bullet_index"), &DirectionalBullets2D::bullet_disable_orbiting);
	ClassDB::bind_method(D_METHOD("bullet_is_orbiting_enabled", "bullet_index"), &DirectionalBullets2D::bullet_is_orbiting_enabled);

	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_radius", "bullet_index"), &DirectionalBullets2D::bullet_get_orbiting_radius);
	ClassDB::bind_method(D_METHOD("bullet_set_orbiting_radius", "bullet_index", "new_radius"), &DirectionalBullets2D::bullet_set_orbiting_radius);

	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_texture_rotation", "bullet_index"), &DirectionalBullets2D::bullet_get_orbiting_texture_rotation);
	ClassDB::bind_method(D_METHOD("bullet_set_orbiting_texture_rotation", "bullet_index", "new_texture_rotation"), &DirectionalBullets2D::bullet_set_orbiting_texture_rotation);

	ClassDB::bind_method(D_METHOD("bullet_get_orbiting_direction", "bullet_index"), &DirectionalBullets2D::bullet_get_orbiting_direction);
	ClassDB::bind_method(D_METHOD("bullet_set_orbiting_direction", "bullet_index", "new_direction"), &DirectionalBullets2D::bullet_set_orbiting_direction);

	ClassDB::bind_method(D_METHOD("all_bullets_enable_orbiting", "orbiting_radius", "orbiting_direction", "orbiting_texture_rotation", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_enable_orbiting, DEFVAL(OrbitRight), DEFVAL(FaceTarget), DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_enable_orbiting_linear", "radius_start", "radius_step", "orbiting_direction", "orbiting_texture_rotation", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_enable_orbiting_linear, DEFVAL(OrbitRight), DEFVAL(FaceTarget), DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_get_orbiting_radius", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_get_orbiting_radius, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_is_orbiting_enabled", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_is_orbiting_enabled, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_disable_orbiting", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_disable_orbiting, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_orbiting_radius", "new_radius", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_set_orbiting_radius, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_orbiting_direction", "new_direction", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_set_orbiting_direction, DEFVAL(0), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("all_bullets_set_orbiting_texture_rotation", "new_rotation", "bullet_index_start", "bullet_index_end_inclusive"), &DirectionalBullets2D::all_bullets_set_orbiting_texture_rotation, DEFVAL(0), DEFVAL(-1));

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

	BIND_ENUM_CONSTANT(FaceTarget);
	BIND_ENUM_CONSTANT(FaceOppositeTarget);
	BIND_ENUM_CONSTANT(FaceOrbitingDirection);
	BIND_ENUM_CONSTANT(FaceOppositeOrbitingDirection);

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
