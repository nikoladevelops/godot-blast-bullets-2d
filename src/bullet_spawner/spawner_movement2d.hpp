#pragma once

#include "core/easing2d.hpp"

#include <godot_cpp/classes/curve.hpp>
#include <godot_cpp/classes/path2d.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <cstdint>

namespace BlastBullets2D {
using namespace godot;

// WHERE a spawner travels at runtime (the Movement group): its settings, its
// playback state and the pure playback math (leg timing, easing, a fresh
// start). The host spawner moves its node, reads the Path2D and emits the
// movement signals (user code: it re-checks `epoch` afterwards).
// Settings are trusted here: the spawner validates them (its error texts
// name its class). Enum values are the spawner's serialized ids.
struct SpawnerMovement2D {
	// Serialized ids (PatternSpawner2D.Movement*): never renumber.
	enum Space : int {
		SPACE_ATTACH = 0,
		SPACE_RELATIVE_TO_START = 1,
	};
	enum LoopMode : int {
		LOOP_ONCE = 0,
		LOOP_LOOP = 1,
		LOOP_PING_PONG = 2,
	};
	enum Direction : int {
		DIRECTION_FORWARD = 0,
		DIRECTION_REVERSE = 1,
	};
	enum Timing : int {
		TIMING_DURATION = 0,
		TIMING_SPEED = 1,
	};

	// Settings.
	bool enabled = false;
	NodePath path;
	mutable Path2D *path_cache = nullptr;
	mutable uint64_t path_id = 0;
	int space = SPACE_ATTACH;
	int loop_mode = LOOP_ONCE;
	int direction = DIRECTION_FORWARD;
	int loops = 0;
	int timing = TIMING_DURATION;
	double duration_sec = 3.0;
	double speed = 200.0;
	int transition = 0; // Tween.TRANS_LINEAR
	int ease = 2; // Tween.EASE_IN_OUT
	Ref<Curve> progress_curve;
	double start_ratio = 0.0;
	double start_delay_sec = 0.0;
	double endpoint_pause_sec = 0.0;
	bool rotate_with_path = false;
	double rotation_offset_deg = 0.0;
	bool cubic_sampling = false;
	bool autostart = true;
	bool inherit_velocity = false;
	double velocity_inherit_factor = 1.0;

	// Playback state.
	bool playing = false;
	bool finished = false;
	double leg_elapsed = 0.0;
	double delay_left = 0.0;
	double pause_left = 0.0;
	int legs_completed = 0;
	bool leg_forward = true;
	double progress = 0.0; // distance ratio along the path
	Vector2 start_origin; // RELATIVE_TO_START anchor (global)
	bool has_last_position = false;
	// The global position the movement itself last applied: velocity is
	// measured from it, so a position written by hand in between is never
	// motion a shot inherits.
	Vector2 last_pose;
	// teleport() jumps an Attach path along (Relative To Start moves its
	// anchor instead); reset_state drops it.
	Vector2 teleport_offset;
	// Bumped by every playback control (play, pause, stop, seek, reverse,
	// enable): a movement signal handler that used one owns what happens next.
	uint32_t epoch = 0;
	// Set on EXIT_TREE and by a new path or space: the next pose is a jump
	// (no velocity).
	bool jumped = false;
	// enabled turned on (autostart) while out of the tree after the first
	// _ready: plays on the next entry (_ready only runs once).
	bool autostart_pending = false;
	Vector2 velocity;
	bool warned_unusable_path = false;

	// A fresh start of the playback (the first leg, its start delay).
	void reset_state() {
		finished = false;
		legs_completed = 0;
		leg_forward = direction == DIRECTION_FORWARD;
		delay_left = start_delay_sec;
		pause_left = 0.0;
		has_last_position = false;
		velocity = Vector2();
		leg_elapsed = 0.0;
		progress = leg_forward ? 0.0 : 1.0;
		teleport_offset = Vector2();
	}

	// The distance ratio at `leg_ratio` of a leg's time: the progress curve
	// (a non-finite sample keeps the time ratio), else the Tween easing.
	double eased(double leg_ratio) const {
		if (progress_curve.is_valid()) {
			const double v = (double)progress_curve->sample((real_t)CLAMP(leg_ratio, 0.0, 1.0));
			return Math::is_finite(v) ? v : leg_ratio;
		}
		return Easing2D::ease(leg_ratio, transition, ease);
	}

	// Seconds one leg over a path of `path_length` px takes.
	double leg_duration(double path_length) const {
		if (timing == TIMING_SPEED) {
			return (speed > 0.0 && path_length > 0.0) ? path_length / speed : 0.0;
		}
		return duration_sec;
	}
};

} // namespace BlastBullets2D
