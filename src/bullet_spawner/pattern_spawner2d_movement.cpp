// BulletSpawner2D (split from bullet_spawner2d.cpp, same class):
// Spawner movement along a Path2D: properties, easing, legs/loops,
// pose application and the play/pause/stop/seek/reverse API.

#include "bullet_spawner/pattern_spawner2d_internal.hpp"
#include "core/emit_on2d.hpp"
#include "core/transform_math2d.hpp"

using namespace godot;

namespace BlastBullets2D {

// ---- Movement along a Path2D -------------------------------------------------

bool PatternSpawner2D::get_movement_enabled() const { return movement.enabled; }

void PatternSpawner2D::set_movement_enabled(bool value) {
	on_config_changed();
	if (movement.enabled == value) {
		return;
	}
	movement.enabled = value;
	++movement.epoch;
	notify_property_list_changed();
	// Out of the tree after the first _ready: autostart waits for the next
	// entry (POST_ENTER_TREE); before it, _ready starts it.
	movement.autostart_pending = value && movement.autostart && !is_inside_tree();
	if (Engine::get_singleton()->is_editor_hint() || !is_inside_tree()) {
		return;
	}
	if (movement.enabled && movement.autostart) {
		movement_play();
	} else if (!movement.enabled) {
		movement.playing = false;
		movement.velocity = Vector2();
	}
	refresh_process_state();
}

NodePath PatternSpawner2D::get_movement_path() const { return movement.path; }

void PatternSpawner2D::set_movement_path(const NodePath &p_path) {
	if (!node_path_type_ok<Path2D>(this, p_path, "movement_path", "Path2D")) {
		return;
	}
	on_config_changed();
	movement.jumped = movement.jumped || p_path != movement.path; // a new path is a jump, never travel
	movement.path = p_path;
	movement.path_cache = nullptr;
	movement.path_id = 0;
	movement.warned_unusable_path = false;
	if (!movement.path.is_empty() && is_inside_tree()) {
		Path2D *resolved = resolve_node_path(this, movement.path, movement.path_cache);
		movement.path_id = resolved != nullptr ? resolved->get_instance_id() : 0;
	}
}

Path2D *PatternSpawner2D::get_movement_path_node() const {
	return validate_cached_node(this, movement.path, movement.path_cache, movement.path_id);
}

void PatternSpawner2D::set_movement_path_node(Path2D *node) {
	on_config_changed();
	movement.jumped = movement.jumped || node != get_movement_path_node(); // a new path is a jump, never travel
	assign_node_to_path(this, node, movement.path, movement.path_cache);
	movement.path_id = node != nullptr ? node->get_instance_id() : 0;
	movement.warned_unusable_path = false;
}

// Small reject-and-keep helpers for the movement setters.
static bool movement_reject(bool bad, const String &message) {
	if (bad) {
		UtilityFunctions::push_error(message);
	}
	return bad;
}

PatternSpawner2D::MovementSpace PatternSpawner2D::get_movement_space() const { return static_cast<MovementSpace>(movement.space); }

void PatternSpawner2D::set_movement_space(MovementSpace value) {
	if (movement_reject(value < MOVEMENT_SPACE_ATTACH || value > MOVEMENT_SPACE_RELATIVE_TO_START, (String(get_class()) + ": movement_space must be 0 (Attach) or 1 (Relative To Start), keeping the old value."))) {
		return;
	}
	movement.jumped = movement.jumped || value != movement.space; // the path moves: a jump, never travel
	movement.space = value;
}

PatternSpawner2D::MovementLoopMode PatternSpawner2D::get_movement_loop_mode() const { return static_cast<MovementLoopMode>(movement.loop_mode); }

void PatternSpawner2D::set_movement_loop_mode(MovementLoopMode value) {
	if (movement_reject(value < MOVEMENT_LOOP_ONCE || value > MOVEMENT_LOOP_PING_PONG, (String(get_class()) + ": movement_loop_mode must be 0 (Once), 1 (Loop) or 2 (Ping Pong), keeping the old value."))) {
		return;
	}
	movement.loop_mode = value;
	notify_property_list_changed();
}

PatternSpawner2D::MovementDirection PatternSpawner2D::get_movement_direction() const { return static_cast<MovementDirection>(movement.direction); }

void PatternSpawner2D::set_movement_direction(MovementDirection value) {
	if (movement_reject(value < MOVEMENT_DIRECTION_FORWARD || value > MOVEMENT_DIRECTION_REVERSE, (String(get_class()) + ": movement_direction must be 0 (Forward) or 1 (Reverse), keeping the old value."))) {
		return;
	}
	movement.direction = value;
}

int PatternSpawner2D::get_movement_loops() const { return movement.loops; }

void PatternSpawner2D::set_movement_loops(int value) {
	if (movement_reject(value < 0, (String(get_class()) + ": movement_loops must be >= 0 (0 = forever), keeping the old value."))) {
		return;
	}
	movement.loops = value;
}

PatternSpawner2D::MovementTiming PatternSpawner2D::get_movement_timing() const { return static_cast<MovementTiming>(movement.timing); }

void PatternSpawner2D::set_movement_timing(MovementTiming value) {
	if (movement_reject(value < MOVEMENT_TIMING_DURATION || value > MOVEMENT_TIMING_SPEED, (String(get_class()) + ": movement_timing must be 0 (Duration) or 1 (Speed), keeping the old value."))) {
		return;
	}
	movement.timing = value;
	notify_property_list_changed();
}

double PatternSpawner2D::get_movement_duration_sec() const { return movement.duration_sec; }

void PatternSpawner2D::set_movement_duration_sec(double value) {
	if (movement_reject(!is_finite_real2d(value) || value <= 0.0, (String(get_class()) + ": movement_duration_sec must be finite and > 0, keeping the old value."))) {
		return;
	}
	movement.duration_sec = value;
}

double PatternSpawner2D::get_movement_speed() const { return movement.speed; }

void PatternSpawner2D::set_movement_speed(double value) {
	if (movement_reject(!is_finite_real2d(value) || value <= 0.0, (String(get_class()) + ": movement_speed must be finite and > 0, keeping the old value."))) {
		return;
	}
	movement.speed = value;
}

Tween::TransitionType PatternSpawner2D::get_movement_transition() const { return static_cast<Tween::TransitionType>(movement.transition); }

void PatternSpawner2D::set_movement_transition(Tween::TransitionType value) {
	if (movement_reject((int)value < 0 || (int)value >= (int)Easing2D::TRANS_COUNT, (String(get_class()) + ": movement_transition must be a Tween.TransitionType (0..11), keeping the old value."))) {
		return;
	}
	movement.transition = value;
}

Tween::EaseType PatternSpawner2D::get_movement_ease() const { return static_cast<Tween::EaseType>(movement.ease); }

void PatternSpawner2D::set_movement_ease(Tween::EaseType value) {
	if (movement_reject((int)value < 0 || (int)value >= (int)Easing2D::EASE_COUNT, (String(get_class()) + ": movement_ease must be a Tween.EaseType (0..3), keeping the old value."))) {
		return;
	}
	movement.ease = value;
}

Ref<Curve> PatternSpawner2D::get_movement_progress_curve() const { return movement.progress_curve; }

void PatternSpawner2D::set_movement_progress_curve(const Ref<Curve> &value) {
	movement.progress_curve = value;
	notify_property_list_changed();
}

double PatternSpawner2D::get_movement_start_ratio() const { return movement.start_ratio; }

void PatternSpawner2D::set_movement_start_ratio(double value) {
	if (movement_reject(!is_finite_real2d(value) || value < 0.0 || value > 1.0, (String(get_class()) + ": movement_start_ratio must be in [0, 1], keeping the old value."))) {
		return;
	}
	movement.start_ratio = value;
}

double PatternSpawner2D::get_movement_start_delay_sec() const { return movement.start_delay_sec; }

void PatternSpawner2D::set_movement_start_delay_sec(double value) {
	if (movement_reject(!is_finite_real2d(value) || value < 0.0, (String(get_class()) + ": movement_start_delay_sec must be finite and >= 0, keeping the old value."))) {
		return;
	}
	movement.start_delay_sec = value;
}

double PatternSpawner2D::get_movement_endpoint_pause_sec() const { return movement.endpoint_pause_sec; }

void PatternSpawner2D::set_movement_endpoint_pause_sec(double value) {
	if (movement_reject(!is_finite_real2d(value) || value < 0.0, (String(get_class()) + ": movement_endpoint_pause_sec must be finite and >= 0, keeping the old value."))) {
		return;
	}
	movement.endpoint_pause_sec = value;
}

bool PatternSpawner2D::get_movement_rotate_with_path() const { return movement.rotate_with_path; }

void PatternSpawner2D::set_movement_rotate_with_path(bool value) {
	movement.rotate_with_path = value;
	notify_property_list_changed();
}

double PatternSpawner2D::get_movement_rotation_offset_deg() const { return movement.rotation_offset_deg; }

void PatternSpawner2D::set_movement_rotation_offset_deg(double value) {
	if (movement_reject(!is_finite_real2d(value), (String(get_class()) + ": movement_rotation_offset_deg must be finite, keeping the old value."))) {
		return;
	}
	movement.rotation_offset_deg = value;
}

bool PatternSpawner2D::get_movement_cubic_sampling() const { return movement.cubic_sampling; }

void PatternSpawner2D::set_movement_cubic_sampling(bool value) { movement.cubic_sampling = value; }

bool PatternSpawner2D::get_movement_autostart() const { return movement.autostart; }

void PatternSpawner2D::set_movement_autostart(bool value) { movement.autostart = value; }

bool PatternSpawner2D::get_inherit_movement_velocity() const { return movement.inherit_velocity; }

void PatternSpawner2D::set_inherit_movement_velocity(bool value) {
	movement.inherit_velocity = value;
	notify_property_list_changed();
}

double PatternSpawner2D::get_movement_velocity_inherit_factor() const { return movement.velocity_inherit_factor; }

void PatternSpawner2D::set_movement_velocity_inherit_factor(double value) {
	if (movement_reject(!is_finite_real2d(value), (String(get_class()) + ": movement_velocity_inherit_factor must be finite, keeping the old value."))) {
		return;
	}
	movement.velocity_inherit_factor = value;
}

double PatternSpawner2D::debug_ease(double t, int transition, int ease) {
	return Easing2D::ease(t, transition, ease);
}

bool PatternSpawner2D::movement_active() const {
	return movement.enabled && movement.playing && is_inside_tree() && !Engine::get_singleton()->is_editor_hint();
}

bool PatternSpawner2D::apply_movement_pose(double delta, bool keep_velocity, bool report_unusable) {
	Path2D *path = get_movement_path_node();
	Ref<Curve2D> curve = path != nullptr ? path->get_curve() : Ref<Curve2D>();
	const double length = curve.is_valid() ? (double)curve->get_baked_length() : 0.0;
	if (path == nullptr || !path->is_inside_tree() || curve.is_null() || !(length > 0.0) || !Math::is_finite(length)) {
		if (report_unusable && !movement.warned_unusable_path) {
			movement.warned_unusable_path = true;
			UtilityFunctions::push_warning((String(get_class()) + ": movement_path is not a Path2D with a non-empty curve in the tree; the spawner stays where it is."));
		}
		movement.velocity = Vector2();
		return false;
	}
	movement.warned_unusable_path = false;
	const Transform2D path_global = path->get_global_transform();
	if (!path_global.is_finite()) {
		return false;
	}
	const double distance = CLAMP(movement.progress, 0.0, 1.0) * length;
	const Transform2D sample = curve->sample_baked_with_rotation((real_t)distance, movement.cubic_sampling);
	Vector2 target = path_global.xform(sample.get_origin());
	if (movement.space == MOVEMENT_SPACE_RELATIVE_TO_START) {
		const Vector2 path_start = path_global.xform(curve->sample_baked(0.0, movement.cubic_sampling));
		target = movement.start_origin + (target - path_start);
	} else {
		target += movement.teleport_offset;
	}
	if (!target.is_finite()) {
		return false;
	}
	const Vector2 before = movement.has_last_position ? movement.last_pose : get_global_position();
	set_global_position(target);
	movement.last_pose = target;
	if (movement.rotate_with_path) {
		// Face the direction of travel: the baked tangent points along
		// increasing distance, so a backward leg turns around.
		real_t rot = (path_global * sample).get_rotation();
		if (!movement.leg_forward) {
			rot += (real_t)Math::PI;
		}
		rot += (real_t)Math::deg_to_rad(movement.rotation_offset_deg);
		if (Math::is_finite(rot)) {
			set_global_rotation(rot);
		}
	}
	if (keep_velocity) {
		movement.has_last_position = true;
		return true;
	}
	if (delta > 0.0 && movement.has_last_position && !movement.jumped) {
		movement.velocity = (target - before) / (real_t)delta;
		if (!movement.velocity.is_finite()) {
			movement.velocity = Vector2();
		}
	} else {
		movement.velocity = Vector2();
	}
	movement.has_last_position = true;
	movement.jumped = false;
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
	const uint32_t epoch = movement.epoch;
	auto handler_took_over = [&]() { return movement.epoch != epoch; };
	double remaining = delta;
	if (movement.delay_left > 0.0) {
		const double used = MIN(remaining, movement.delay_left);
		movement.delay_left -= used;
		remaining -= used;
		if (movement.delay_left > 0.0) {
			apply_movement_pose(0.0);
			return true;
		}
	}
	// The velocity is measured over the time each applied pose covers: a frame
	// that completes a leg applies the endpoint (the time to it), then the
	// rest of the frame. A slice too short to measure (an exact landing)
	// keeps the last velocity instead of dividing ~0 by ~0.
	double pose_time = delta - remaining;
	auto apply_step = [&]() {
		const double slice = (delta - remaining) - pose_time;
		pose_time = delta - remaining;
		apply_movement_pose(slice, !(slice > 1e-6));
	};
	// Bounded catch-up: a hitch spanning many legs advances at most this many
	// leg transitions per frame (tiny durations can never spin forever).
	for (int guard = 0; guard < 64 && remaining > 0.0 && movement.playing; ++guard) {
		if (movement.pause_left > 0.0) {
			const double used = MIN(remaining, movement.pause_left);
			movement.pause_left -= used;
			remaining -= used;
			if (movement.pause_left > 0.0) {
				break;
			}
			continue;
		}
		const double duration = movement.leg_duration(length);
		if (!(duration > 0.0) || !Math::is_finite(duration)) {
			break; // unusable path: apply_movement_pose below warns once
		}
		const double to_end = duration - movement.leg_elapsed;
		if (remaining < to_end) {
			movement.leg_elapsed += remaining;
			remaining = 0.0;
			const double eased = movement.eased(movement.leg_elapsed / duration);
			movement.progress = movement.leg_forward ? eased : 1.0 - eased;
			break;
		}
		// Leg completed.
		remaining -= MAX(to_end, 0.0);
		movement.leg_elapsed = 0.0;
		movement.progress = movement.leg_forward ? movement.eased(1.0) : 1.0 - movement.eased(1.0);
		const bool at_end = movement.leg_forward;
		++movement.legs_completed;
		apply_step();
		emit_on2d(this, "movement_endpoint_reached", at_end);
		if (!alive()) {
			return false;
		}
		if (handler_took_over()) {
			return true;
		}
		const bool finite_run = movement.loop_mode == MOVEMENT_LOOP_ONCE || (movement.loops > 0 && movement.legs_completed >= movement.loops);
		if (finite_run) {
			movement.playing = false;
			movement.finished = true;
			movement.velocity = Vector2();
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
		emit_on2d(this, "movement_loop_completed", movement.legs_completed - 1);
		if (!alive()) {
			return false;
		}
		if (handler_took_over()) {
			return true;
		}
		if (movement.loop_mode == MOVEMENT_LOOP_PING_PONG) {
			movement.leg_forward = !movement.leg_forward;
		} else {
			// LOOP: jump back to the leg's start (no interpolated sweep back):
			// a pose only, the motion (and its velocity) goes on from there.
			movement.progress = movement.leg_forward ? 0.0 : 1.0;
			apply_movement_pose(0.0, true);
		}
		movement.pause_left = movement.endpoint_pause_sec;
	}
	apply_step();
	return true;
}

void PatternSpawner2D::movement_play() {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	if (!movement.enabled) {
		// It would snap onto the path, report a start and never advance.
		UtilityFunctions::push_warning(String(get_class()) + "::movement_play: movement_enabled is off, nothing moves (turn it on to play the path).");
		return;
	}
	++movement.epoch;
	if (!movement.playing && (movement.finished || !movement.has_last_position)) {
		const bool fresh = !movement.has_last_position && !movement.finished && movement.legs_completed == 0 && movement.leg_elapsed == 0.0;
		movement.reset_state();
		if (fresh || movement.space == MOVEMENT_SPACE_RELATIVE_TO_START) {
			movement.start_origin = is_inside_tree() ? get_global_position() : get_position();
		}
		// Start ratio: fraction of the first leg already elapsed.
		Path2D *path = get_movement_path_node();
		Ref<Curve2D> curve = path != nullptr ? path->get_curve() : Ref<Curve2D>();
		const double duration = movement.leg_duration(curve.is_valid() ? (double)curve->get_baked_length() : 0.0);
		if (duration > 0.0) {
			movement.leg_elapsed = movement.start_ratio * duration;
			const double eased = movement.eased(movement.start_ratio);
			movement.progress = movement.leg_forward ? eased : 1.0 - eased;
		}
	}
	const bool was_playing = movement.playing;
	movement.playing = true;
	if (is_inside_tree()) {
		// A placement only: the first frame that travels names a missing path
		// (code may assign it right after adding the spawner).
		apply_movement_pose(0.0, false, false);
		refresh_process_state();
	}
	if (!was_playing) {
		emit_on2d(this, "movement_started");
	}
}

void PatternSpawner2D::movement_pause() {
	++movement.epoch;
	movement.playing = false;
	movement.velocity = Vector2();
	if (is_inside_tree()) {
		refresh_process_state();
	}
}

void PatternSpawner2D::movement_stop(bool reset_to_start) {
	++movement.epoch;
	movement.playing = false;
	movement.velocity = Vector2();
	if (reset_to_start) {
		movement.reset_state();
		if (is_inside_tree() && movement.enabled) {
			apply_movement_pose(0.0, false, false);
		}
		movement.has_last_position = false;
	} else {
		movement.finished = true;
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
	// Godot's own interpolation of this node must not draw the jump as travel.
	reset_physics_interpolation();
	// The movement's own pose jumps with the spawner: the next step measures
	// only the path's motion, and the path goes on from here.
	if (movement.space == MOVEMENT_SPACE_RELATIVE_TO_START) {
		movement.start_origin += jump;
	} else {
		movement.teleport_offset += jump;
	}
	movement.last_pose += jump;
	on_teleported_owned(jump, carry_live_bullets);
}

void PatternSpawner2D::movement_seek(double ratio) {
	if (!Math::is_finite(ratio) || ratio < 0.0 || ratio > 1.0) {
		UtilityFunctions::push_error((String(get_class()) + "::movement_seek: ratio must be in [0, 1], ignoring."));
		return;
	}
	++movement.epoch;
	Path2D *path = get_movement_path_node();
	Ref<Curve2D> curve = path != nullptr ? path->get_curve() : Ref<Curve2D>();
	const double duration = movement.leg_duration(curve.is_valid() ? (double)curve->get_baked_length() : 0.0);
	movement.leg_elapsed = duration > 0.0 ? ratio * duration : 0.0;
	const double eased = movement.eased(ratio);
	movement.progress = movement.leg_forward ? eased : 1.0 - eased;
	movement.has_last_position = false;
	if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint()) {
		apply_movement_pose(0.0, false, false);
	}
}

void PatternSpawner2D::movement_reverse() {
	++movement.epoch;
	// Keep the current position: mirror the elapsed time within the leg so
	// the eased progress continues from here in the other direction.
	Path2D *path = get_movement_path_node();
	Ref<Curve2D> curve = path != nullptr ? path->get_curve() : Ref<Curve2D>();
	const double duration = movement.leg_duration(curve.is_valid() ? (double)curve->get_baked_length() : 0.0);
	movement.leg_forward = !movement.leg_forward;
	if (duration > 0.0) {
		// Find the leg time whose eased progress lands on the current spot
		// (bisection: easing may be non-monotonic, take the first crossing).
		const double target = movement.leg_forward ? movement.progress : 1.0 - movement.progress;
		double lo = 0.0;
		double hi = 1.0;
		for (int i = 0; i < 40; ++i) {
			const double mid = 0.5 * (lo + hi);
			if (movement.eased(mid) < target) {
				lo = mid;
			} else {
				hi = mid;
			}
		}
		movement.leg_elapsed = hi * duration;
	}
}

bool PatternSpawner2D::is_movement_playing() const { return movement.playing; }

double PatternSpawner2D::get_movement_progress() const { return movement.progress; }

int PatternSpawner2D::get_movement_leg() const { return movement.legs_completed; }

Vector2 PatternSpawner2D::get_movement_velocity() const { return movement.velocity; }

Vector2 PatternSpawner2D::shot_inherited_velocity() const {
	Vector2 inherited_velocity = shot_pose_active ? shot_pose_velocity : Vector2();
	if (!shot_pose_active && movement.inherit_velocity && movement.enabled) {
		inherited_velocity = movement.velocity * (real_t)movement.velocity_inherit_factor;
		if (!inherited_velocity.is_finite()) {
			inherited_velocity = Vector2();
		}
	}
	return inherited_velocity;
}

} // namespace BlastBullets2D
