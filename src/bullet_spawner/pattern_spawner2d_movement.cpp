// BulletSpawner2D (split from bullet_spawner2d.cpp, same class):
// Spawner movement along a Path2D: properties, easing, legs/loops,
// pose application and the play/pause/stop/seek/reverse API.

#include "bullet_spawner/pattern_spawner2d_internal.hpp"
#include "core/emit_on2d.hpp"
#include "core/transform_math2d.hpp"

using namespace godot;

namespace BlastBullets2D {

// ---- Movement along a Path2D -------------------------------------------------

bool PatternSpawner2D::get_movement_enabled() const { return movement_enabled; }

void PatternSpawner2D::set_movement_enabled(bool value) {
	on_config_changed();
	if (movement_enabled == value) {
		return;
	}
	movement_enabled = value;
	++movement_epoch;
	notify_property_list_changed();
	if (Engine::get_singleton()->is_editor_hint() || !is_inside_tree()) {
		return;
	}
	if (movement_enabled && movement_autostart) {
		movement_play();
	} else if (!movement_enabled) {
		movement_playing = false;
		movement_velocity = Vector2();
	}
	refresh_process_state();
}

NodePath PatternSpawner2D::get_movement_path() const { return movement_path; }

void PatternSpawner2D::set_movement_path(const NodePath &p_path) {
	if (!node_path_type_ok<Path2D>(this, p_path, "movement_path", "Path2D")) {
		return;
	}
	on_config_changed();
	movement_path = p_path;
	movement_path_cache = nullptr;
	movement_path_id = 0;
	movement_warned_unusable_path = false;
	if (!movement_path.is_empty() && is_inside_tree()) {
		Path2D *resolved = resolve_node_path(this, movement_path, movement_path_cache);
		movement_path_id = resolved != nullptr ? resolved->get_instance_id() : 0;
	}
}

Path2D *PatternSpawner2D::get_movement_path_node() const {
	return validate_cached_node(this, movement_path, movement_path_cache, movement_path_id);
}

void PatternSpawner2D::set_movement_path_node(Path2D *node) {
	on_config_changed();
	assign_node_to_path(this, node, movement_path, movement_path_cache);
	movement_path_id = node != nullptr ? node->get_instance_id() : 0;
	movement_warned_unusable_path = false;
}

// Small reject-and-keep helpers for the movement setters.
static bool movement_reject(bool bad, const String &message) {
	if (bad) {
		UtilityFunctions::push_error(message);
	}
	return bad;
}

PatternSpawner2D::MovementSpace PatternSpawner2D::get_movement_space() const { return static_cast<MovementSpace>(movement_space); }

void PatternSpawner2D::set_movement_space(MovementSpace value) {
	if (movement_reject(value < MOVEMENT_SPACE_ATTACH || value > MOVEMENT_SPACE_RELATIVE_TO_START, (String(get_class()) + ": movement_space must be 0 (Attach) or 1 (Relative To Start), keeping the old value."))) {
		return;
	}
	movement_space = value;
}

PatternSpawner2D::MovementLoopMode PatternSpawner2D::get_movement_loop_mode() const { return static_cast<MovementLoopMode>(movement_loop_mode); }

void PatternSpawner2D::set_movement_loop_mode(MovementLoopMode value) {
	if (movement_reject(value < MOVEMENT_LOOP_ONCE || value > MOVEMENT_LOOP_PING_PONG, (String(get_class()) + ": movement_loop_mode must be 0 (Once), 1 (Loop) or 2 (Ping Pong), keeping the old value."))) {
		return;
	}
	movement_loop_mode = value;
	notify_property_list_changed();
}

PatternSpawner2D::MovementDirection PatternSpawner2D::get_movement_direction() const { return static_cast<MovementDirection>(movement_direction); }

void PatternSpawner2D::set_movement_direction(MovementDirection value) {
	if (movement_reject(value < MOVEMENT_DIRECTION_FORWARD || value > MOVEMENT_DIRECTION_REVERSE, (String(get_class()) + ": movement_direction must be 0 (Forward) or 1 (Reverse), keeping the old value."))) {
		return;
	}
	movement_direction = value;
}

int PatternSpawner2D::get_movement_loops() const { return movement_loops; }

void PatternSpawner2D::set_movement_loops(int value) {
	if (movement_reject(value < 0, (String(get_class()) + ": movement_loops must be >= 0 (0 = forever), keeping the old value."))) {
		return;
	}
	movement_loops = value;
}

PatternSpawner2D::MovementTiming PatternSpawner2D::get_movement_timing() const { return static_cast<MovementTiming>(movement_timing); }

void PatternSpawner2D::set_movement_timing(MovementTiming value) {
	if (movement_reject(value < MOVEMENT_TIMING_DURATION || value > MOVEMENT_TIMING_SPEED, (String(get_class()) + ": movement_timing must be 0 (Duration) or 1 (Speed), keeping the old value."))) {
		return;
	}
	movement_timing = value;
	notify_property_list_changed();
}

double PatternSpawner2D::get_movement_duration_sec() const { return movement_duration_sec; }

void PatternSpawner2D::set_movement_duration_sec(double value) {
	if (movement_reject(!is_finite_real2d(value) || value <= 0.0, (String(get_class()) + ": movement_duration_sec must be finite and > 0, keeping the old value."))) {
		return;
	}
	movement_duration_sec = value;
}

double PatternSpawner2D::get_movement_speed() const { return movement_speed; }

void PatternSpawner2D::set_movement_speed(double value) {
	if (movement_reject(!is_finite_real2d(value) || value <= 0.0, (String(get_class()) + ": movement_speed must be finite and > 0, keeping the old value."))) {
		return;
	}
	movement_speed = value;
}

Tween::TransitionType PatternSpawner2D::get_movement_transition() const { return static_cast<Tween::TransitionType>(movement_transition); }

void PatternSpawner2D::set_movement_transition(Tween::TransitionType value) {
	if (movement_reject(value < 0 || value >= Easing2D::TRANS_COUNT, (String(get_class()) + ": movement_transition must be a Tween.TransitionType (0..11), keeping the old value."))) {
		return;
	}
	movement_transition = value;
}

Tween::EaseType PatternSpawner2D::get_movement_ease() const { return static_cast<Tween::EaseType>(movement_ease); }

void PatternSpawner2D::set_movement_ease(Tween::EaseType value) {
	if (movement_reject(value < 0 || value >= Easing2D::EASE_COUNT, (String(get_class()) + ": movement_ease must be a Tween.EaseType (0..3), keeping the old value."))) {
		return;
	}
	movement_ease = value;
}

Ref<Curve> PatternSpawner2D::get_movement_progress_curve() const { return movement_progress_curve; }

void PatternSpawner2D::set_movement_progress_curve(const Ref<Curve> &value) {
	movement_progress_curve = value;
	notify_property_list_changed();
}

double PatternSpawner2D::get_movement_start_ratio() const { return movement_start_ratio; }

void PatternSpawner2D::set_movement_start_ratio(double value) {
	if (movement_reject(!is_finite_real2d(value) || value < 0.0 || value > 1.0, (String(get_class()) + ": movement_start_ratio must be in [0, 1], keeping the old value."))) {
		return;
	}
	movement_start_ratio = value;
}

double PatternSpawner2D::get_movement_start_delay_sec() const { return movement_start_delay_sec; }

void PatternSpawner2D::set_movement_start_delay_sec(double value) {
	if (movement_reject(!is_finite_real2d(value) || value < 0.0, (String(get_class()) + ": movement_start_delay_sec must be finite and >= 0, keeping the old value."))) {
		return;
	}
	movement_start_delay_sec = value;
}

double PatternSpawner2D::get_movement_endpoint_pause_sec() const { return movement_endpoint_pause_sec; }

void PatternSpawner2D::set_movement_endpoint_pause_sec(double value) {
	if (movement_reject(!is_finite_real2d(value) || value < 0.0, (String(get_class()) + ": movement_endpoint_pause_sec must be finite and >= 0, keeping the old value."))) {
		return;
	}
	movement_endpoint_pause_sec = value;
}

bool PatternSpawner2D::get_movement_rotate_with_path() const { return movement_rotate_with_path; }

void PatternSpawner2D::set_movement_rotate_with_path(bool value) {
	movement_rotate_with_path = value;
	notify_property_list_changed();
}

double PatternSpawner2D::get_movement_rotation_offset_deg() const { return movement_rotation_offset_deg; }

void PatternSpawner2D::set_movement_rotation_offset_deg(double value) {
	if (movement_reject(!is_finite_real2d(value), (String(get_class()) + ": movement_rotation_offset_deg must be finite, keeping the old value."))) {
		return;
	}
	movement_rotation_offset_deg = value;
}

bool PatternSpawner2D::get_movement_cubic_sampling() const { return movement_cubic_sampling; }

void PatternSpawner2D::set_movement_cubic_sampling(bool value) { movement_cubic_sampling = value; }

bool PatternSpawner2D::get_movement_autostart() const { return movement_autostart; }

void PatternSpawner2D::set_movement_autostart(bool value) { movement_autostart = value; }

bool PatternSpawner2D::get_inherit_movement_velocity() const { return inherit_movement_velocity; }

void PatternSpawner2D::set_inherit_movement_velocity(bool value) {
	inherit_movement_velocity = value;
	notify_property_list_changed();
}

double PatternSpawner2D::get_movement_velocity_inherit_factor() const { return movement_velocity_inherit_factor; }

void PatternSpawner2D::set_movement_velocity_inherit_factor(double value) {
	if (movement_reject(!is_finite_real2d(value), (String(get_class()) + ": movement_velocity_inherit_factor must be finite, keeping the old value."))) {
		return;
	}
	movement_velocity_inherit_factor = value;
}

double PatternSpawner2D::debug_ease(double t, int transition, int ease) {
	return Easing2D::ease(t, transition, ease);
}

bool PatternSpawner2D::movement_active() const {
	return movement_enabled && movement_playing && is_inside_tree() && !Engine::get_singleton()->is_editor_hint();
}

void PatternSpawner2D::movement_reset_state() {
	movement_finished = false;
	movement_legs_completed = 0;
	movement_leg_forward = movement_direction == MOVEMENT_DIRECTION_FORWARD;
	movement_delay_left = movement_start_delay_sec;
	movement_pause_left = 0.0;
	movement_has_last_position = false;
	movement_velocity = Vector2();
	movement_leg_elapsed = 0.0;
	movement_progress = movement_leg_forward ? 0.0 : 1.0;
	movement_teleport_offset = Vector2();
}

double PatternSpawner2D::movement_eased(double leg_ratio) const {
	if (movement_progress_curve.is_valid()) {
		const double v = (double)movement_progress_curve->sample((real_t)CLAMP(leg_ratio, 0.0, 1.0));
		return Math::is_finite(v) ? v : leg_ratio;
	}
	return Easing2D::ease(leg_ratio, movement_transition, movement_ease);
}

double PatternSpawner2D::movement_leg_duration(double path_length) const {
	if (movement_timing == MOVEMENT_TIMING_SPEED) {
		return (movement_speed > 0.0 && path_length > 0.0) ? path_length / movement_speed : 0.0;
	}
	return movement_duration_sec;
}

bool PatternSpawner2D::apply_movement_pose(double delta) {
	Path2D *path = get_movement_path_node();
	Ref<Curve2D> curve = path != nullptr ? path->get_curve() : Ref<Curve2D>();
	const double length = curve.is_valid() ? (double)curve->get_baked_length() : 0.0;
	if (path == nullptr || !path->is_inside_tree() || curve.is_null() || !(length > 0.0) || !Math::is_finite(length)) {
		if (!movement_warned_unusable_path) {
			movement_warned_unusable_path = true;
			UtilityFunctions::push_warning((String(get_class()) + ": movement_path is not a Path2D with a non-empty curve in the tree; the spawner stays where it is."));
		}
		movement_velocity = Vector2();
		return false;
	}
	movement_warned_unusable_path = false;
	const Transform2D path_global = path->get_global_transform();
	if (!path_global.is_finite()) {
		return false;
	}
	const double distance = CLAMP(movement_progress, 0.0, 1.0) * length;
	const Transform2D sample = curve->sample_baked_with_rotation((real_t)distance, movement_cubic_sampling);
	Vector2 target = path_global.xform(sample.get_origin());
	if (movement_space == MOVEMENT_SPACE_RELATIVE_TO_START) {
		const Vector2 path_start = path_global.xform(curve->sample_baked(0.0, movement_cubic_sampling));
		target = movement_start_origin + (target - path_start);
	} else {
		target += movement_teleport_offset;
	}
	if (!target.is_finite()) {
		return false;
	}
	const Vector2 before = movement_has_last_position ? movement_last_pose : get_global_position();
	set_global_position(target);
	movement_last_pose = target;
	if (movement_rotate_with_path) {
		// Face the direction of travel: the baked tangent points along
		// increasing distance, so a backward leg turns around.
		real_t rot = (path_global * sample).get_rotation();
		if (!movement_leg_forward) {
			rot += (real_t)Math::PI;
		}
		rot += (real_t)Math::deg_to_rad(movement_rotation_offset_deg);
		if (Math::is_finite(rot)) {
			set_global_rotation(rot);
		}
	}
	if (delta > 0.0 && movement_has_last_position && !movement_jumped) {
		movement_velocity = (target - before) / (real_t)delta;
		if (!movement_velocity.is_finite()) {
			movement_velocity = Vector2();
		}
	} else {
		movement_velocity = Vector2();
	}
	movement_has_last_position = true;
	movement_jumped = false;
	return true;
}

bool PatternSpawner2D::advance_movement(double delta) {
	if (!movement_active() || !Math::is_finite(delta) || delta <= 0.0) {
		return true;
	}
	Path2D *path = get_movement_path_node();
	Ref<Curve2D> curve = path != nullptr ? path->get_curve() : Ref<Curve2D>();
	const double length = curve.is_valid() ? (double)curve->get_baked_length() : 0.0;
	const uint64_t self_id = get_instance_id();
	auto alive = [&]() { return ObjectDB::get_instance(ObjectID(self_id)) == this; };
	// A handler that stopped, sought, reversed or restarted the movement
	// owns it: the rest of this leg's bookkeeping must not overwrite that.
	const uint32_t epoch = movement_epoch;
	auto handler_took_over = [&]() { return movement_epoch != epoch; };
	double remaining = delta;
	if (movement_delay_left > 0.0) {
		const double used = MIN(remaining, movement_delay_left);
		movement_delay_left -= used;
		remaining -= used;
		if (movement_delay_left > 0.0) {
			apply_movement_pose(0.0);
			return true;
		}
	}
	// Bounded catch-up: a hitch spanning many legs advances at most this many
	// leg transitions per frame (tiny durations can never spin forever).
	for (int guard = 0; guard < 64 && remaining > 0.0 && movement_playing; ++guard) {
		if (movement_pause_left > 0.0) {
			const double used = MIN(remaining, movement_pause_left);
			movement_pause_left -= used;
			remaining -= used;
			if (movement_pause_left > 0.0) {
				break;
			}
			continue;
		}
		const double duration = movement_leg_duration(length);
		if (!(duration > 0.0) || !Math::is_finite(duration)) {
			break; // unusable path: apply_movement_pose below warns once
		}
		const double to_end = duration - movement_leg_elapsed;
		if (remaining < to_end) {
			movement_leg_elapsed += remaining;
			remaining = 0.0;
			const double eased = movement_eased(movement_leg_elapsed / duration);
			movement_progress = movement_leg_forward ? eased : 1.0 - eased;
			break;
		}
		// Leg completed.
		remaining -= MAX(to_end, 0.0);
		movement_leg_elapsed = 0.0;
		movement_progress = movement_leg_forward ? movement_eased(1.0) : 1.0 - movement_eased(1.0);
		const bool at_end = movement_leg_forward;
		++movement_legs_completed;
		apply_movement_pose(delta);
		emit_on2d(this, "movement_endpoint_reached", at_end);
		if (!alive()) {
			return false;
		}
		if (handler_took_over()) {
			return true;
		}
		const bool finite_run = movement_loop_mode == MOVEMENT_LOOP_ONCE || (movement_loops > 0 && movement_legs_completed >= movement_loops);
		if (finite_run) {
			movement_playing = false;
			movement_finished = true;
			movement_velocity = Vector2();
			emit_on2d(this, "movement_finished");
			if (!alive()) {
				return false;
			}
			if (handler_took_over()) {
				return true;
			}
			refresh_process_state();
			return true;
		}
		emit_on2d(this, "movement_loop_completed", movement_legs_completed - 1);
		if (!alive()) {
			return false;
		}
		if (handler_took_over()) {
			return true;
		}
		if (movement_loop_mode == MOVEMENT_LOOP_PING_PONG) {
			movement_leg_forward = !movement_leg_forward;
		} else {
			// LOOP: jump back to the leg's start (no interpolated sweep back).
			movement_progress = movement_leg_forward ? 0.0 : 1.0;
			movement_has_last_position = false;
		}
		movement_pause_left = movement_endpoint_pause_sec;
	}
	apply_movement_pose(delta);
	return true;
}

void PatternSpawner2D::movement_play() {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	++movement_epoch;
	if (!movement_playing && (movement_finished || !movement_has_last_position)) {
		const bool fresh = !movement_has_last_position && !movement_finished && movement_legs_completed == 0 && movement_leg_elapsed == 0.0;
		movement_reset_state();
		if (fresh || movement_space == MOVEMENT_SPACE_RELATIVE_TO_START) {
			movement_start_origin = is_inside_tree() ? get_global_position() : get_position();
		}
		// Start ratio: fraction of the first leg already elapsed.
		Path2D *path = get_movement_path_node();
		Ref<Curve2D> curve = path != nullptr ? path->get_curve() : Ref<Curve2D>();
		const double duration = movement_leg_duration(curve.is_valid() ? (double)curve->get_baked_length() : 0.0);
		if (duration > 0.0) {
			movement_leg_elapsed = movement_start_ratio * duration;
			const double eased = movement_eased(movement_start_ratio);
			movement_progress = movement_leg_forward ? eased : 1.0 - eased;
		}
	}
	const bool was_playing = movement_playing;
	movement_playing = true;
	if (is_inside_tree()) {
		apply_movement_pose(0.0);
		refresh_process_state();
	}
	if (!was_playing) {
		emit_on2d(this, "movement_started");
	}
}

void PatternSpawner2D::movement_pause() {
	++movement_epoch;
	movement_playing = false;
	movement_velocity = Vector2();
	if (is_inside_tree()) {
		refresh_process_state();
	}
}

void PatternSpawner2D::movement_stop(bool reset_to_start) {
	++movement_epoch;
	movement_playing = false;
	movement_velocity = Vector2();
	if (reset_to_start) {
		movement_reset_state();
		if (is_inside_tree() && movement_enabled) {
			apply_movement_pose(0.0);
		}
		movement_has_last_position = false;
	} else {
		movement_finished = true;
	}
	if (is_inside_tree()) {
		refresh_process_state();
	}
}

void PatternSpawner2D::teleport(const Vector2 &new_global_position, bool carry_live_bullets) {
	if (!new_global_position.is_finite()) {
		UtilityFunctions::push_error((String(get_class()) + "::teleport: global_position must be finite, the spawner stays where it is."));
		return;
	}
	if (!is_inside_tree()) {
		UtilityFunctions::push_error((String(get_class()) + "::teleport: the spawner is outside the scene tree, it stays where it is."));
		return;
	}
	const Vector2 jump = new_global_position - get_global_position();
	if (!jump.is_finite()) {
		UtilityFunctions::push_error((String(get_class()) + "::teleport: global_position must be finite, the spawner stays where it is."));
		return;
	}
	set_global_position(new_global_position);
	// The movement's own pose jumps with the spawner: the next step measures
	// only the path's motion, and the path goes on from here.
	if (movement_space == MOVEMENT_SPACE_RELATIVE_TO_START) {
		movement_start_origin += jump;
	} else {
		movement_teleport_offset += jump;
	}
	movement_last_pose += jump;
	if (!carry_live_bullets) {
		return;
	}
	shift_owned_shots(jump);
}

void PatternSpawner2D::movement_seek(double ratio) {
	if (!Math::is_finite(ratio) || ratio < 0.0 || ratio > 1.0) {
		UtilityFunctions::push_error((String(get_class()) + "::movement_seek: ratio must be in [0, 1], ignoring."));
		return;
	}
	++movement_epoch;
	Path2D *path = get_movement_path_node();
	Ref<Curve2D> curve = path != nullptr ? path->get_curve() : Ref<Curve2D>();
	const double duration = movement_leg_duration(curve.is_valid() ? (double)curve->get_baked_length() : 0.0);
	movement_leg_elapsed = duration > 0.0 ? ratio * duration : 0.0;
	const double eased = movement_eased(ratio);
	movement_progress = movement_leg_forward ? eased : 1.0 - eased;
	movement_has_last_position = false;
	if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint()) {
		apply_movement_pose(0.0);
	}
}

void PatternSpawner2D::movement_reverse() {
	++movement_epoch;
	// Keep the current position: mirror the elapsed time within the leg so
	// the eased progress continues from here in the other direction.
	Path2D *path = get_movement_path_node();
	Ref<Curve2D> curve = path != nullptr ? path->get_curve() : Ref<Curve2D>();
	const double duration = movement_leg_duration(curve.is_valid() ? (double)curve->get_baked_length() : 0.0);
	movement_leg_forward = !movement_leg_forward;
	if (duration > 0.0) {
		// Find the leg time whose eased progress lands on the current spot
		// (bisection: easing may be non-monotonic, take the first crossing).
		const double target = movement_leg_forward ? movement_progress : 1.0 - movement_progress;
		double lo = 0.0;
		double hi = 1.0;
		for (int i = 0; i < 40; ++i) {
			const double mid = 0.5 * (lo + hi);
			if (movement_eased(mid) < target) {
				lo = mid;
			} else {
				hi = mid;
			}
		}
		movement_leg_elapsed = hi * duration;
	}
}

bool PatternSpawner2D::is_movement_playing() const { return movement_playing; }

double PatternSpawner2D::get_movement_progress() const { return movement_progress; }

int PatternSpawner2D::get_movement_leg() const { return movement_legs_completed; }

Vector2 PatternSpawner2D::get_movement_velocity() const { return movement_velocity; }

} // namespace BlastBullets2D
