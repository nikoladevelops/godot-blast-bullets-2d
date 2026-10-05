// BulletSpawner2D (split from bullet_spawner2d.cpp, same class):
// Spawner movement along a Path2D: properties, easing, legs/loops,
// pose application and the play/pause/stop/seek/reverse API.

#include "bullet_spawner/bullet_spawner2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

// ---- Movement along a Path2D -------------------------------------------------

bool BulletSpawner2D::get_movement_enabled() const { return movement_enabled; }

void BulletSpawner2D::set_movement_enabled(bool value) {
	on_config_changed();
	if (movement_enabled == value) {
		return;
	}
	movement_enabled = value;
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

NodePath BulletSpawner2D::get_movement_path() const { return movement_path; }

void BulletSpawner2D::set_movement_path(const NodePath &p_path) {
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

Path2D *BulletSpawner2D::get_movement_path_node() const {
	return validate_cached_node(this, movement_path, movement_path_cache, movement_path_id);
}

void BulletSpawner2D::set_movement_path_node(Path2D *node) {
	on_config_changed();
	assign_node_to_path(this, node, movement_path, movement_path_cache);
	movement_path_id = node != nullptr ? node->get_instance_id() : 0;
	movement_warned_unusable_path = false;
}

// Small reject-and-keep helpers for the movement setters.
static bool movement_reject(bool bad, const char *message) {
	if (bad) {
		UtilityFunctions::push_error(message);
	}
	return bad;
}

int BulletSpawner2D::get_movement_space() const { return movement_space; }

void BulletSpawner2D::set_movement_space(int value) {
	if (movement_reject(value < MOVEMENT_SPACE_ATTACH || value > MOVEMENT_SPACE_RELATIVE_TO_START, "BulletSpawner2D: movement_space must be 0 (Attach) or 1 (Relative To Start), keeping the old value.")) {
		return;
	}
	movement_space = value;
}

int BulletSpawner2D::get_movement_loop_mode() const { return movement_loop_mode; }

void BulletSpawner2D::set_movement_loop_mode(int value) {
	if (movement_reject(value < MOVEMENT_LOOP_ONCE || value > MOVEMENT_LOOP_PING_PONG, "BulletSpawner2D: movement_loop_mode must be 0 (Once), 1 (Loop) or 2 (Ping Pong), keeping the old value.")) {
		return;
	}
	movement_loop_mode = value;
	notify_property_list_changed();
}

int BulletSpawner2D::get_movement_direction() const { return movement_direction; }

void BulletSpawner2D::set_movement_direction(int value) {
	if (movement_reject(value < MOVEMENT_DIRECTION_FORWARD || value > MOVEMENT_DIRECTION_REVERSE, "BulletSpawner2D: movement_direction must be 0 (Forward) or 1 (Reverse), keeping the old value.")) {
		return;
	}
	movement_direction = value;
}

int BulletSpawner2D::get_movement_loops() const { return movement_loops; }

void BulletSpawner2D::set_movement_loops(int value) {
	if (movement_reject(value < 0, "BulletSpawner2D: movement_loops must be >= 0 (0 = forever), keeping the old value.")) {
		return;
	}
	movement_loops = value;
}

int BulletSpawner2D::get_movement_timing() const { return movement_timing; }

void BulletSpawner2D::set_movement_timing(int value) {
	if (movement_reject(value < MOVEMENT_TIMING_DURATION || value > MOVEMENT_TIMING_SPEED, "BulletSpawner2D: movement_timing must be 0 (Duration) or 1 (Speed), keeping the old value.")) {
		return;
	}
	movement_timing = value;
	notify_property_list_changed();
}

double BulletSpawner2D::get_movement_duration_sec() const { return movement_duration_sec; }

void BulletSpawner2D::set_movement_duration_sec(double value) {
	if (movement_reject(!Math::is_finite(value) || value <= 0.0, "BulletSpawner2D: movement_duration_sec must be finite and > 0, keeping the old value.")) {
		return;
	}
	movement_duration_sec = value;
}

double BulletSpawner2D::get_movement_speed() const { return movement_speed; }

void BulletSpawner2D::set_movement_speed(double value) {
	if (movement_reject(!Math::is_finite(value) || value <= 0.0, "BulletSpawner2D: movement_speed must be finite and > 0, keeping the old value.")) {
		return;
	}
	movement_speed = value;
}

int BulletSpawner2D::get_movement_transition() const { return movement_transition; }

void BulletSpawner2D::set_movement_transition(int value) {
	if (movement_reject(value < 0 || value >= Easing2D::TRANS_COUNT, "BulletSpawner2D: movement_transition must be a Tween.TransitionType (0..11), keeping the old value.")) {
		return;
	}
	movement_transition = value;
}

int BulletSpawner2D::get_movement_ease() const { return movement_ease; }

void BulletSpawner2D::set_movement_ease(int value) {
	if (movement_reject(value < 0 || value >= Easing2D::EASE_COUNT, "BulletSpawner2D: movement_ease must be a Tween.EaseType (0..3), keeping the old value.")) {
		return;
	}
	movement_ease = value;
}

Ref<Curve> BulletSpawner2D::get_movement_progress_curve() const { return movement_progress_curve; }

void BulletSpawner2D::set_movement_progress_curve(const Ref<Curve> &value) {
	movement_progress_curve = value;
	notify_property_list_changed();
}

double BulletSpawner2D::get_movement_start_ratio() const { return movement_start_ratio; }

void BulletSpawner2D::set_movement_start_ratio(double value) {
	if (movement_reject(!Math::is_finite(value) || value < 0.0 || value > 1.0, "BulletSpawner2D: movement_start_ratio must be in [0, 1], keeping the old value.")) {
		return;
	}
	movement_start_ratio = value;
}

double BulletSpawner2D::get_movement_start_delay_sec() const { return movement_start_delay_sec; }

void BulletSpawner2D::set_movement_start_delay_sec(double value) {
	if (movement_reject(!Math::is_finite(value) || value < 0.0, "BulletSpawner2D: movement_start_delay_sec must be finite and >= 0, keeping the old value.")) {
		return;
	}
	movement_start_delay_sec = value;
}

double BulletSpawner2D::get_movement_endpoint_pause_sec() const { return movement_endpoint_pause_sec; }

void BulletSpawner2D::set_movement_endpoint_pause_sec(double value) {
	if (movement_reject(!Math::is_finite(value) || value < 0.0, "BulletSpawner2D: movement_endpoint_pause_sec must be finite and >= 0, keeping the old value.")) {
		return;
	}
	movement_endpoint_pause_sec = value;
}

bool BulletSpawner2D::get_movement_rotate_with_path() const { return movement_rotate_with_path; }

void BulletSpawner2D::set_movement_rotate_with_path(bool value) {
	movement_rotate_with_path = value;
	notify_property_list_changed();
}

double BulletSpawner2D::get_movement_rotation_offset_deg() const { return movement_rotation_offset_deg; }

void BulletSpawner2D::set_movement_rotation_offset_deg(double value) {
	if (movement_reject(!Math::is_finite(value), "BulletSpawner2D: movement_rotation_offset_deg must be finite, keeping the old value.")) {
		return;
	}
	movement_rotation_offset_deg = value;
}

bool BulletSpawner2D::get_movement_cubic_sampling() const { return movement_cubic_sampling; }

void BulletSpawner2D::set_movement_cubic_sampling(bool value) { movement_cubic_sampling = value; }

bool BulletSpawner2D::get_movement_autostart() const { return movement_autostart; }

void BulletSpawner2D::set_movement_autostart(bool value) { movement_autostart = value; }

bool BulletSpawner2D::get_inherit_movement_velocity() const { return inherit_movement_velocity; }

void BulletSpawner2D::set_inherit_movement_velocity(bool value) {
	inherit_movement_velocity = value;
	notify_property_list_changed();
}

double BulletSpawner2D::get_movement_velocity_inherit_factor() const { return movement_velocity_inherit_factor; }

void BulletSpawner2D::set_movement_velocity_inherit_factor(double value) {
	if (movement_reject(!Math::is_finite(value), "BulletSpawner2D: movement_velocity_inherit_factor must be finite, keeping the old value.")) {
		return;
	}
	movement_velocity_inherit_factor = value;
}

double BulletSpawner2D::debug_ease(double t, int transition, int ease) {
	return Easing2D::ease(t, transition, ease);
}

bool BulletSpawner2D::movement_active() const {
	return movement_enabled && movement_playing && is_inside_tree() && !Engine::get_singleton()->is_editor_hint();
}

void BulletSpawner2D::movement_reset_state() {
	movement_finished = false;
	movement_legs_completed = 0;
	movement_leg_forward = movement_direction == MOVEMENT_DIRECTION_FORWARD;
	movement_delay_left = movement_start_delay_sec;
	movement_pause_left = 0.0;
	movement_has_last_position = false;
	movement_velocity = Vector2();
	movement_leg_elapsed = 0.0;
	movement_progress = movement_leg_forward ? 0.0 : 1.0;
}

double BulletSpawner2D::movement_eased(double leg_ratio) const {
	if (movement_progress_curve.is_valid()) {
		const double v = (double)movement_progress_curve->sample((real_t)CLAMP(leg_ratio, 0.0, 1.0));
		return Math::is_finite(v) ? v : leg_ratio;
	}
	return Easing2D::ease(leg_ratio, movement_transition, movement_ease);
}

double BulletSpawner2D::movement_leg_duration(double path_length) const {
	if (movement_timing == MOVEMENT_TIMING_SPEED) {
		return (movement_speed > 0.0 && path_length > 0.0) ? path_length / movement_speed : 0.0;
	}
	return movement_duration_sec;
}

bool BulletSpawner2D::apply_movement_pose(double delta) {
	Path2D *path = get_movement_path_node();
	Ref<Curve2D> curve = path != nullptr ? path->get_curve() : Ref<Curve2D>();
	const double length = curve.is_valid() ? (double)curve->get_baked_length() : 0.0;
	if (path == nullptr || !path->is_inside_tree() || curve.is_null() || !(length > 0.0) || !Math::is_finite(length)) {
		if (!movement_warned_unusable_path) {
			movement_warned_unusable_path = true;
			UtilityFunctions::push_warning("BulletSpawner2D: movement_path is not a Path2D with a non-empty curve in the tree; the spawner stays where it is.");
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
	}
	if (!target.is_finite()) {
		return false;
	}
	const Vector2 before = get_global_position();
	set_global_position(target);
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
	if (delta > 0.0 && movement_has_last_position) {
		movement_velocity = (target - before) / (real_t)delta;
		if (!movement_velocity.is_finite()) {
			movement_velocity = Vector2();
		}
	} else {
		movement_velocity = Vector2();
	}
	movement_has_last_position = true;
	return true;
}

bool BulletSpawner2D::advance_movement(double delta) {
	if (!movement_active() || !Math::is_finite(delta) || delta <= 0.0) {
		return true;
	}
	Path2D *path = get_movement_path_node();
	Ref<Curve2D> curve = path != nullptr ? path->get_curve() : Ref<Curve2D>();
	const double length = curve.is_valid() ? (double)curve->get_baked_length() : 0.0;
	const uint64_t self_id = get_instance_id();
	auto alive = [&]() { return ObjectDB::get_instance(ObjectID(self_id)) == this; };
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
		emit_signal("movement_endpoint_reached", at_end);
		if (!alive()) {
			return false;
		}
		const bool finite_run = movement_loop_mode == MOVEMENT_LOOP_ONCE || (movement_loops > 0 && movement_legs_completed >= movement_loops);
		if (finite_run) {
			movement_playing = false;
			movement_finished = true;
			movement_velocity = Vector2();
			emit_signal("movement_finished");
			if (!alive()) {
				return false;
			}
			refresh_process_state();
			return true;
		}
		emit_signal("movement_loop_completed", movement_legs_completed - 1);
		if (!alive()) {
			return false;
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

void BulletSpawner2D::movement_play() {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
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
		emit_signal("movement_started");
	}
}

void BulletSpawner2D::movement_pause() {
	movement_playing = false;
	movement_velocity = Vector2();
	if (is_inside_tree()) {
		refresh_process_state();
	}
}

void BulletSpawner2D::movement_stop(bool reset_to_start) {
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

void BulletSpawner2D::movement_seek(double ratio) {
	if (!Math::is_finite(ratio) || ratio < 0.0 || ratio > 1.0) {
		UtilityFunctions::push_error("BulletSpawner2D::movement_seek: ratio must be in [0, 1], ignoring.");
		return;
	}
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

void BulletSpawner2D::movement_reverse() {
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

bool BulletSpawner2D::is_movement_playing() const { return movement_playing; }

double BulletSpawner2D::get_movement_progress() const { return movement_progress; }

int BulletSpawner2D::get_movement_leg() const { return movement_legs_completed; }

Vector2 BulletSpawner2D::get_movement_velocity() const { return movement_velocity; }

} // namespace BlastBullets2D
