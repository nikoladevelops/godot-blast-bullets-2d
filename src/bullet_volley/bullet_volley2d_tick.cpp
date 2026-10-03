// The per-frame motion loop (move_bullets) and every helper it calls per bullet.
// Hot path: keep helpers here or in bullet_volley2d_internal.hpp (inline) so no
// per-bullet call crosses a translation unit. BulletFactory2D calls move_bullets
// once per active volley per physics frame.

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletVolley2D::move_bullets(double delta) {
	if (amount_bullets <= 0 || physics_server == nullptr || !area.is_valid()) {
		return;
	}
	if (!Math::is_finite(delta) || delta < 0.0) {
		return;
	}
	// Bounce bookkeeping runs only while armed; plain volleys skip it.
	if (bounce_enabled()) {
		++bounce_tick_counter;
		bounce_last_delta = delta;
	}
	const bool is_using_physics_interpolation = bullet_factory != nullptr && bullet_factory->use_physics_interpolation;
	if (is_using_physics_interpolation) {
		update_all_previous_transforms_for_interpolation();
	}

	bool homing_interval_reached = false;

	// Unified precedence: per-bullet deque wins for its bullet. The shared
// deque is the broadcast fallback, used only when the bullet's own
// deque is empty. Either deque steering the bullet marks homing active.
bool shared_homing_deque_enabled = !shared_homing_deque.empty();
	const bool is_per_bullet_homing_enabled = (active_homing_count > 0);

	// If homing is enabled (either shared or per-bullet) update the timer and cache mouse position if needed
	// The mouse query is hoisted here (once per tick, not once per push):
	// get_global_mouse_position() walks to the viewport each call, and a
	// volley with per-bullet mouse pushes would otherwise pay it N times.
	if (shared_homing_deque_enabled || is_per_bullet_homing_enabled) {
		// Update homing timer / how often to update the homing target position
		homing_interval_reached = update_homing_timer(delta);

		// In case we have the mouse as a homing target, make sure to cache its global position
		if (homing_interval_reached && HomingTargetDeque::mouse_homing_targets_amount > 0) {
			cached_mouse_global_position = get_global_mouse_position();
		}
	}

	// Since shared homing deque is used for all bullets, do this once
	if (shared_homing_deque_enabled) {
		// Delete any invalid (freed) targets
		auto targets_amount = shared_homing_deque.get_homing_targets_amount();
		int trimmed = shared_homing_deque.bullet_homing_trim_front_invalid_targets(cached_mouse_global_position, targets_amount);
		if (trimmed > 0) {
			// Front changed: route the orbit lock per policy (a new front
			// target never inherits a stale center) and give every bullet
			// a clean reached slate for the new target.
			orbit_route_shared_front_change();
		}
		shared_homing_deque_enabled = (targets_amount - trimmed) > 0;

		// If timer timed out, refresh the cached global position of the front target
		if (shared_homing_deque_enabled && homing_interval_reached) {
			shared_homing_deque.refresh_cached_front_target_global_position(cached_mouse_global_position);
		}
	}

	// While the shared deque holds targets, per-bullet caches go stale
	// (their branch never runs). Refresh them on the interval too, so a
	// shared drain hands over live positions instead of push-time ones.
	if (shared_homing_deque_enabled && homing_interval_reached && active_homing_count > 0) {
		for (size_t qi = 0; qi < all_bullet_homing_targets.size() && qi < all_homing_count.size(); ++qi) {
			if (all_homing_count[qi] > 0 && !all_bullet_homing_targets[qi].empty()) {
				all_bullet_homing_targets[qi].refresh_cached_front_target_global_position(cached_mouse_global_position);
			}
		}
	}

	const bool is_orbiting_feature_enabled = (active_orbiting_count > 0);

	Vector2 homing_bullet_pos;
	Vector2 homing_target_pos;

	const bool shared_curves_data_enabled = shared_bullet_curves_data.is_valid();
	const BulletCurvesData2D *const shared_curves_ptr = shared_bullet_curves_data.ptr();

	const bool shared_curves_x_direction_curve_valid = shared_curves_data_enabled && shared_curves_ptr->x_direction_curve.is_valid();
	const bool shared_curves_y_direction_curve_valid = shared_curves_data_enabled && shared_curves_ptr->y_direction_curve.is_valid();

	const bool shared_curves_rotation_curve_valid = shared_curves_data_enabled && shared_curves_ptr->rotation_speed_curve.is_valid();
	const bool shared_curves_acceleration_curve_valid = shared_curves_data_enabled && shared_curves_ptr->movement_speed_curve.is_valid();

	// Hoist shared curve samples outside loop (same for all bullets in this multimesh)
	real_t shared_x_offset = 0, shared_y_offset = 0;
	real_t shared_x_strength = 0, shared_y_strength = 0;
	DirectionCurveMode shared_x_mode = DirectionCurveMode::Additive, shared_y_mode = DirectionCurveMode::Additive;
	real_t shared_movement_speed_val = 0, shared_rotation_speed_val = 0;
	if (shared_curves_data_enabled) {
		if (shared_curves_x_direction_curve_valid) {
			shared_x_offset = get_bullet_curves_x_direction_offset(shared_curves_ptr);
			shared_x_strength = shared_curves_ptr->x_direction_curve_strength;
			shared_x_mode = shared_curves_ptr->x_direction_curve_mode;
		}
		if (shared_curves_y_direction_curve_valid) {
			shared_y_offset = get_bullet_curves_y_direction_offset(shared_curves_ptr);
			shared_y_strength = shared_curves_ptr->y_direction_curve_strength;
			shared_y_mode = shared_curves_ptr->y_direction_curve_mode;
		}
		if (shared_curves_acceleration_curve_valid) {
			shared_movement_speed_val = get_bullet_curves_movement_speed(shared_curves_ptr);
		}
		if (shared_curves_rotation_curve_valid) {
			shared_rotation_speed_val = get_bullet_curves_rotation_speed(shared_curves_ptr);
		}
	}

	bool is_per_bullet_curves_valid = false;
	const BulletCurvesData2D *per_bullet_curves_data = nullptr;

	// Shared movement pattern curve sampled once (same for all bullets).
	const bool shared_pattern_curve_valid = shared_movement_pattern_curve.is_valid();
	real_t shared_pattern_len = 0.0;
	if (shared_pattern_curve_valid) {
		shared_pattern_len = shared_movement_pattern_curve->get_baked_length();
		// NaN baked length (corrupt Curve2D points) would flow into the
		// per-bullet advance as known_len; normalize to 0 here so the
		// use_shared_pattern gate below stays shut (advance also guards).
		if (!Math::is_finite(shared_pattern_len)) {
			shared_pattern_len = 0.0;
		}
	}

	// Gravity time window sampled once (same for all bullets). Combined
	// with gravity_active below, a volley with no pull skips the gravity
	// block for every bullet.
	const bool gravity_window = gravity_window_open();

	// Locked-orbit snapshot for the pattern gate below: the orbit lock
	// state read per bullet must match what the orbit section below sees, or the pattern
	// gate and the orbit displacement disagree for one frame.
	// Reuses the member scratch (no per-tick allocation when sized).
	std::vector<uint8_t> &orbit_locked_snapshot = orbit_locked_scratch;
	if (is_orbiting_feature_enabled) {
		if ((int)orbit_locked_snapshot.size() != amount_bullets) {
			orbit_locked_snapshot.assign(amount_bullets, 0);
		} else {
			std::fill(orbit_locked_snapshot.begin(), orbit_locked_snapshot.end(), (uint8_t)0);
		}
		for (int li : all_bullets_enabled_set.get_active_indexes()) {
			if (li >= 0 && li < amount_bullets && li < (int)all_orbiting_status.size() && li < (int)all_orbiting_data.size()) {
				if (all_orbiting_status[li] && all_orbiting_data[li].is_locked_orbiting) {
					orbit_locked_snapshot[li] = 1;
				}
			}
		}
	}

	// Shout when homing can't visibly do anything (no steering, no pattern, no spin, no ring) - usually a forgotten take-control flag.
	// changes via rotate_to_target, movement patterns, rotation data, or orbiting).
	// Warn once.
	if (!homing_inert_warning_issued && (shared_homing_deque_enabled || is_per_bullet_homing_enabled) && !homing_take_control_of_texture_rotation && !is_rotation_data_active && active_orbiting_count == 0) {
		bool any_pattern = false;
		for (int pi : all_bullets_enabled_set.get_active_indexes()) {
			if (check_exists_bullet_movement_pattern_data(pi)) {
				any_pattern = true;
				break;
			}
		}
		// The spawn-data shared pattern counts too (per-bullet entries are
		// only half the story now).
		any_pattern = any_pattern || shared_movement_pattern_curve.is_valid();
		if (!any_pattern) {
			UtilityFunctions::push_warning("BulletVolley2D has homing targets but homing_take_control_of_texture_rotation is false (and no movement pattern/rotation data), so homing will not steer bullets. Set it to true.");
			homing_inert_warning_issued = true;
		}
	}

	// Loop only through ACTIVE bullets (skip the disabled ones)
	const auto &active_bullet_indexes = all_bullets_enabled_set.get_active_indexes();

	// BulletCurvesData2D is a mutable shared Resource: a user can gain a rotation
	// curve AFTER the multimesh was spawned/enabled, when populate_* had no reason
	// to size all_rotation_speed. Enforce the invariant once per tick so the
	// rotation branches below can never index out of bounds. All three
	// vectors travel together (max/accel are indexed beside speed), so a
	// future path that desyncs one must not leave the others short.
	// resize (not assign): existing speeds survive, only gaps fill with 0.
	if ((int)all_rotation_speed.size() != amount_bullets || (int)all_max_rotation_speed.size() != amount_bullets || (int)all_rotation_acceleration.size() != amount_bullets) {
		all_rotation_speed.resize(amount_bullets, 0.0);
		all_max_rotation_speed.resize(amount_bullets, 0.0);
		all_rotation_acceleration.resize(amount_bullets, 0.0);
	}
	// Same invariant for the shared-deque reached states (H5 fix storage).
	if ((int)all_shared_homing_reached.size() != amount_bullets) {
		all_shared_homing_reached.resize(amount_bullets);
	}

	// Texture-rotation strip for the physics shape, hoisted: rotated_local
	// recomputed the same sin/cos for every bullet every tick.
	const bool strip_texture_rotation = cache_texture_rotation_radians != 0.0;
	const real_t strip_cos = strip_texture_rotation ? (real_t)Math::cos(-cache_texture_rotation_radians) : (real_t)1.0;
	const real_t strip_sin = strip_texture_rotation ? (real_t)Math::sin(-cache_texture_rotation_radians) : (real_t)0.0;
	const bool has_shape_offset = cache_collision_shape_offset != Vector2(0, 0);

	{ // One node inverse for the whole movement loop (trail writes convert per bullet); no user code runs inside.
	NodeInverseScope tick_inverse_scope(this);
	for (int i : active_bullet_indexes) {
		if (i < 0 || i >= amount_bullets) {
			continue;
		}
		// Bounce cooldown ticks down so the bullet can escape the overlap
		// it just bounced out of (a body+area pair on one target would
		// otherwise double-flip it in place). Skipped entirely unless
		// bouncing is armed.
		if (bounce_enabled() && i >= 0 && i < (int)all_bounce_cooldown.size() && all_bounce_cooldown[i] > 0.0) {
			all_bounce_cooldown[i] -= (real_t)delta;
			if (all_bounce_cooldown[i] < 0.0) {
				all_bounce_cooldown[i] = 0.0;
			}
		}
		if (i >= (int)all_cached_instance_transforms.size() || i >= (int)all_cached_direction.size() || i >= (int)all_cached_velocity.size()) {
			continue;
		}
		// The speed arrays should always fit by now, but double-check in the hot loop - a crash here would take the whole game down.
		// but every consumer below indexes it - never trust the invariant in a hot loop.
		if (i >= (int)all_cached_speed.size() || i >= (int)all_cached_max_speed.size() || i >= (int)all_cached_acceleration.size()) {
			continue;
		}
		bool direction_got_updated = false;
		HomingTargetDeque *target_deque_used_for_orbiting = nullptr;

		// 1. STANDARD HOMING PHASE
		// Reached-signal timing runs AFTER steering below (see the reached-signal section):
		// orbit, pattern and curve steering rewrite the velocity, so the
		// reached test must predict with the final velocity_delta, not
		// the pre-steer cached velocity.
		if (is_per_bullet_homing_enabled || shared_homing_deque_enabled) { // Per-bullet deque wins per bullet; shared is the broadcast fallback
			// Whether this bullet has its own targets (non-empty per-bullet deque)
			bool bullet_has_own_targets = false;
			if (i >= 0 && i < (int)all_homing_count.size() && i < (int)all_bullet_homing_targets.size()) {
				bullet_has_own_targets = all_homing_count[i] > 0 && !all_bullet_homing_targets[i].empty();
			}
			if (bullet_has_own_targets) { // Handle per-bullet homing (wins over shared)
				auto &curr_homing_count = all_homing_count[i];

				if (curr_homing_count > 0) {
					auto &curr_homing_deque = all_bullet_homing_targets[i];

					// Drop freed targets off the front
					int trimmed_count = curr_homing_deque.bullet_homing_trim_front_invalid_targets(cached_mouse_global_position, curr_homing_count);

					// Keep the counters honest after trimming so the tick below sees the real queue
					curr_homing_count -= trimmed_count; // this deque lost some
					active_homing_count -= trimmed_count; // ...and so did the volley-wide total
					if (curr_homing_count < 0) {
						curr_homing_count = 0;
					}
					if (active_homing_count < 0) {
						active_homing_count = 0;
					}
					// If the counter somehow runs ahead of the real queue, pull it back - otherwise bullets would home on ghosts.
					const int live_after_trim = curr_homing_deque.get_homing_targets_amount();
					if (curr_homing_count > live_after_trim) {
						active_homing_count -= (curr_homing_count - live_after_trim);
						if (active_homing_count < 0) {
							active_homing_count = 0;
						}
						curr_homing_count = live_after_trim;
					}

					// Trimming exposed a new front target - treat it like a pop so orbit rings re-lock cleanly.
					if (trimmed_count > 0) {
						orbit_route_front_change_for_bullet(i, curr_homing_deque);
					}

					if (curr_homing_count > 0 && !curr_homing_deque.empty()) {
						// Refresh the cached target position on the interval so moving targets don't leave stale positions behind
						if (homing_interval_reached) {
							curr_homing_deque.refresh_cached_front_target_global_position(cached_mouse_global_position);
						}

						update_homing(curr_homing_deque, i, delta, homing_bullet_pos, homing_target_pos);
						direction_got_updated = true;
						target_deque_used_for_orbiting = &curr_homing_deque;
					} else if (shared_homing_deque_enabled) {
						// Per-bullet deque drained this tick: fall back to
						// the shared broadcast deque for this bullet.
						update_homing(shared_homing_deque, i, delta, homing_bullet_pos, homing_target_pos);
						direction_got_updated = true;
						target_deque_used_for_orbiting = &shared_homing_deque;
					}
				} else if (shared_homing_deque_enabled) {
					update_homing(shared_homing_deque, i, delta, homing_bullet_pos, homing_target_pos);
					direction_got_updated = true;
					target_deque_used_for_orbiting = &shared_homing_deque;
				}
			} else if (shared_homing_deque_enabled) { // Shared broadcast fallback (only when the bullet has no own targets)
				update_homing(shared_homing_deque, i, delta, homing_bullet_pos, homing_target_pos);
				direction_got_updated = true;
				target_deque_used_for_orbiting = &shared_homing_deque;
			}
		}

		auto &curr_bullet_transf = all_cached_instance_transforms[i];
		auto &curr_bullet_direction = all_cached_direction[i];

		// 2. DIRECTION CURVES - shared sampled once before loop
	// Unified precedence: per-bullet channels win over shared per
	// channel. A bullet with its own x curve uses it even when shared
	// also defines x; shared only covers channels the bullet lacks.
	is_per_bullet_curves_valid = (i >= 0 && i < (int)all_bullet_curves_data.size() && all_bullet_curves_data[i].is_valid());
	per_bullet_curves_data = is_per_bullet_curves_valid ? all_bullet_curves_data[i].ptr() : nullptr; // O(1) vector index, borrows - valid until vector reassigned
	const bool per_bullet_x_curve_valid = is_per_bullet_curves_valid && per_bullet_curves_data->x_direction_curve.is_valid();
	const bool per_bullet_y_curve_valid = is_per_bullet_curves_valid && per_bullet_curves_data->y_direction_curve.is_valid();

	const BulletCurvesData2D *direction_curves_for_texture = nullptr;
	if (per_bullet_x_curve_valid) {
		apply_x_direction_curve(curr_bullet_direction, per_bullet_curves_data);
		direction_curves_for_texture = per_bullet_curves_data;
	} else if (shared_curves_x_direction_curve_valid) {
		const Vector2 before_x = curr_bullet_direction;
		if (shared_x_mode == DirectionCurveMode::Additive) {
			curr_bullet_direction.x += shared_x_offset * shared_x_strength;
		} else {
			curr_bullet_direction.x = shared_x_offset * shared_x_strength;
		}
		// Same degenerate guard as the per-bullet appliers: an Override
		// result near zero keeps the incoming direction instead of
		// stalling the bullet (and snapping its texture to angle 0).
		if (curr_bullet_direction.length_squared() < 0.00000001) {
			curr_bullet_direction = before_x;
		} else {
			curr_bullet_direction = curr_bullet_direction.normalized();
		}
		direction_curves_for_texture = shared_curves_ptr;
	}

	if (per_bullet_y_curve_valid) {
		apply_y_direction_curve(curr_bullet_direction, per_bullet_curves_data);
		if (direction_curves_for_texture == nullptr) {
			direction_curves_for_texture = per_bullet_curves_data;
		}
	} else if (shared_curves_y_direction_curve_valid) {
		const Vector2 before_y = curr_bullet_direction;
		if (shared_y_mode == DirectionCurveMode::Additive) {
			curr_bullet_direction.y += shared_y_offset * shared_y_strength;
		} else {
			curr_bullet_direction.y = shared_y_offset * shared_y_strength;
		}
		if (curr_bullet_direction.length_squared() < 0.00000001) {
			curr_bullet_direction = before_y;
		} else {
			curr_bullet_direction = curr_bullet_direction.normalized();
		}
		direction_curves_for_texture = shared_curves_ptr;
	}

	if (direction_curves_for_texture != nullptr) {
		apply_direction_curve_texture_rotation_if_needed(curr_bullet_direction, curr_bullet_transf, delta, direction_curves_for_texture);
		direction_got_updated = true;
	}

	// 2b. WOBBLE (waveform flight modulation). Lateral displaces the
	// heading perpendicular to flight (snakes, weaves, curtains);
	// angular oscillates the heading itself (corkscrews, petals).
	// Runs on the analytic offset DELTA between frames (not the absolute
	// offset) so pausing/teleporting never jumps the bullet. Disabled
	// entries cost one bool check per bullet; fully disabled volleys
	// skip the loop via the hoisted flag below.
	// Texture follow: with face_movement_direction the visual slews
	// toward the steered heading (same contract as direction curves:
	// skipped while rotation data drives the visual). Angular mode
	// already rotates the heading; the follow keeps the sprite glued
	// to it. Lateral mode steers the heading too, so the same follow
	// points snakes along their path.
	if (is_wobble_feature_enabled && i >= 0 && i < (int)all_bullet_wobble.size() && all_bullet_wobble[i].active) {
		WobbleSeed &w = all_bullet_wobble[i];
		const real_t t = (real_t)curves_elapsed_time;
		bool in_window = t >= w.delay_sec && (w.duration_sec <= 0.0 || t < w.delay_sec + w.duration_sec);
		if (!in_window) {
			// Leaving (or not yet in) the window: the next entry starts
			// from the analytic baseline, exactly as before.
			w.has_last = false;
		}
		if (in_window && w.frequency_hz >= 0.0 && w.amplitude >= 0.0) {
			const real_t phase_base = w.distance_phased && i >= 0 && i < (int)wobble_distance_traveled.size()
					? wobble_distance_traveled[i] * 0.02
					: t;
			const real_t damp = (w.damping_per_sec > 0.0 && t > w.delay_sec) ? (real_t)Math::exp(-(double)(w.damping_per_sec * (t - w.delay_sec))) : 1.0;
			const real_t now_off = w.amplitude * damp * evaluate_wobble_waveform(w.waveform, Math::TAU * w.frequency_hz * phase_base + w.phase);
			const real_t prev_t = t - (real_t)delta;
			const bool prev_in = prev_t >= w.delay_sec && (w.duration_sec <= 0.0 || prev_t < w.delay_sec + w.duration_sec);
			real_t prev_off = 0.0;
			const bool clock_continuous = w.has_last && Math::abs((curves_elapsed_time - delta) - w.last_time) < 1e-6;
			if (clock_continuous) {
				// Exact: the offset actually applied last tick.
				prev_off = w.last_offset;
			} else if (prev_in && delta > 0.0) {
				const real_t prev_base = w.distance_phased && i >= 0 && i < (int)wobble_distance_traveled.size()
						? (wobble_distance_traveled[i] - (curr_bullet_direction * all_cached_speed[i]).length() * (real_t)delta) * 0.02
						: prev_t;
				const real_t prev_damp = (w.damping_per_sec > 0.0 && prev_t > w.delay_sec) ? (real_t)Math::exp(-(double)(w.damping_per_sec * (prev_t - w.delay_sec))) : 1.0;
				prev_off = w.amplitude * prev_damp * evaluate_wobble_waveform(w.waveform, Math::TAU * w.frequency_hz * prev_base + w.phase);
			}
			const real_t frame_delta = now_off - prev_off;
			if (Math::is_finite(now_off)) {
				w.has_last = true;
				w.last_offset = now_off;
				w.last_time = curves_elapsed_time;
			} else {
				w.has_last = false;
			}
			if (Math::is_finite(frame_delta) && Math::abs(frame_delta) > 0.00001) {
				if (w.mode == 1) {
					curr_bullet_direction = curr_bullet_direction.rotated(Math::deg_to_rad(frame_delta));
					if (curr_bullet_direction.length_squared() < 0.00000001) {
						curr_bullet_direction = Vector2(1, 0);
					} else {
						curr_bullet_direction = curr_bullet_direction.normalized();
					}
				} else {
					if (curr_bullet_direction.length_squared() > 0.00000001) {
						const Vector2 perp = Vector2(-curr_bullet_direction.y, curr_bullet_direction.x).normalized();
						const Vector2 steered = curr_bullet_direction + perp * (frame_delta * 0.01);
						// A pathological frame_delta (~100x amplitude)
						// could cancel the heading to zero; hold the old
						// heading instead of snapping to angle 0.
						if (steered.length_squared() > 0.00000001) {
							curr_bullet_direction = steered.normalized();
						}
					}
				}
				direction_got_updated = true;
				// Texture follow (see 2b header): slew the visual toward
				// the steered heading so snakes point along their path.
				// Same skip rule as direction curves: rotation data owns
				// the visual then. Snap when face_rotation_speed <= 0.
				if (w.face_movement_direction && !is_rotation_data_active && delta > 0.0) {
					const real_t target = curr_bullet_direction.angle();
					const real_t current = curr_bullet_transf.get_rotation();
					const real_t diff = Math::fposmod(target - current + static_cast<real_t>(Math::PI), static_cast<real_t>(Math::TAU)) - static_cast<real_t>(Math::PI);
					if (Math::is_finite(diff) && Math::abs(diff) > 0.00001) {
						if (w.face_rotation_speed <= 0.0) {
							rotate_transform_locally(curr_bullet_transf, diff);
						} else {
							const real_t step = Math::abs(w.face_rotation_speed) * (real_t)delta;
							rotate_transform_locally(curr_bullet_transf, Math::clamp(diff, -step, step));
						}
					}
				}
			}
		}
	}

		// 3. ROTATION - per-bullet curve wins per bullet, shared is fallback
		if (is_per_bullet_curves_valid && per_bullet_curves_data->rotation_speed_curve.is_valid()) {
			bullet_accelerate_rotation_speed_using_curve(i, delta, per_bullet_curves_data);
			update_rotation_using_curve(i, delta);
		} else if (shared_curves_rotation_curve_valid) {
			all_rotation_speed[i] = shared_rotation_speed_val;
			update_rotation_using_curve(i, delta);
		} else if (is_rotation_data_active) {
			bullet_accelerate_rotation_speed(i, delta);
			update_rotation(i, delta);
		}

		// 4. ADJUST DIRECTION BASED ON THE NEW ROTATION (OPTIONALLY)
		// Visual-only spin must not leak into ballistics: with
		// rotate_only_textures the instance rotation is rendering-only (the
		// shape path freezes too), so re-deriving direction from it would
		// steer bullets with texture spin. Skipped the same way.
		if (adjust_direction_based_on_rotation && !rotate_only_textures) {
			// columns[0] includes the texture rotation used for rendering;
			// strip it to recover the logical movement direction.
			// Degenerate basis (zero-scale) keeps the last good direction
			// instead of normalizing a ~zero vector into a stall.
			const Vector2 stripped_basis = all_cached_instance_transforms[i].columns[0].rotated(-cache_texture_rotation_radians);
			if (stripped_basis.length_squared() > 0.00000001) {
				curr_bullet_direction = stripped_basis.normalized();
			}
			direction_got_updated = true;
		}

	// 4b. BOUNCE VISUAL PURSUIT (smooth ricochet facing). Ballistics
	// always reflect instantly in the collision drain; with
	// bounce_rotation_smooth > 0 only the sprite slews toward the
	// reflected heading over several ticks. Visual-only: the logical
	// direction is never touched here. Skipped while
	// adjust_direction_based_on_rotation owns ballistics (the visual
	// owns movement there, so it snapped at bounce time instead).
	if (bounce_enabled() && bounce_rotate_texture && bounce_rotation_smooth > 0.0 && !adjust_direction_based_on_rotation && i >= 0 && i < (int)bounce_visual_pending.size() && bounce_visual_pending[i] && i < (int)bounce_visual_target.size()) {
		const Vector2 want = bounce_visual_target[i];
		if (want.length_squared() > 0.00000001 && Math::is_finite((double)bounce_rotation_smooth) && delta > 0.0) {
			rotate_to_target(i, want, bounce_rotation_smooth * (real_t)delta, false);
			const Vector2 fwd = all_cached_instance_transforms[i][0].rotated(-cache_texture_rotation_radians);
			if (fwd.length_squared() > 0.00000001 && fwd.normalized().dot(want.normalized()) > 0.9999) {
				bounce_visual_pending[i] = 0;
			}
		} else {
			bounce_visual_pending[i] = 0;
		}
	}

		// 5. VELOCITY CALCULATION (ONLY IF DIRECTION GOT UPDATED) - use temp to avoid mutating cached velocity
		if (direction_got_updated) {
			all_cached_velocity[i] = curr_bullet_direction * all_cached_speed[i] + inherited_velocity_offset + ((i >= 0 && i < (int)all_gravity_velocity.size()) ? all_gravity_velocity[i] : Vector2(0, 0));
		}
		// Wind-free base: the inherited offset rides along AFTER pattern
		// steering below. advance_movement_pattern measures advance_dist =
		// |velocity_delta|, so leaving the offset in would stretch the
		// pattern step along the pattern direction instead of adding
		// world-space drift. Re-added after the pattern runs.
		Vector2 velocity_delta = (curr_bullet_direction * all_cached_speed[i]) * (real_t)delta;

	// 6. MOVEMENT PATTERNS (RELYING ON CURVES AND PATH2D)
	// Unified precedence: per-bullet entries win per bullet; the shared
	// slot is the broadcast fallback for bullets without their own
	// pattern. A locked orbit owns the displacement: the pattern advance
	// (and its distance ledger) is skipped so a non-repeating pattern
	// cannot finish invisibly while the ring drives the bullet. The gate
	// reads the pre-tick snapshot above so it agrees with the orbit section even
	// when the deque empties mid-tick.
	const bool orbit_locked_this_bullet = is_orbiting_feature_enabled && i >= 0 && i < amount_bullets && i < (int)orbit_locked_snapshot.size() && orbit_locked_snapshot[i] && target_deque_used_for_orbiting != nullptr && !target_deque_used_for_orbiting->empty();
	const bool has_per_bullet_pattern = !orbit_locked_this_bullet && check_exists_bullet_movement_pattern_data(i);
	bool use_shared_pattern = false;
	if (!orbit_locked_this_bullet && !has_per_bullet_pattern) {
		if (shared_pattern_curve_valid && shared_pattern_len >= 0.001 && i >= 0 && i < (int)shared_movement_pattern_distances.size()) {
			use_shared_pattern = shared_movement_pattern_repeat || shared_movement_pattern_distances[i] < shared_pattern_len;
		}
	}
	const bool use_per_bullet_pattern = has_per_bullet_pattern;
	if (use_shared_pattern || use_per_bullet_pattern) {
		if (use_shared_pattern) {
			if (!advance_movement_pattern(shared_movement_pattern_curve, shared_movement_pattern_face_movement_direction, shared_movement_pattern_repeat, shared_movement_pattern_distances[i], velocity_delta, curr_bullet_direction, curr_bullet_transf, shared_pattern_len)) {
				// Park the ledger at the end (degenerate curve or completed
				// run) so finished bullets skip without an extra flag.
				shared_movement_pattern_distances[i] = shared_pattern_len;
			}
		} else {
			auto &pattern = all_movement_pattern_data[i];
			const Ref<Curve2D> &curve = pattern.path_curve;
			if (curve.is_null()) {
				all_movement_pattern_data[i] = BulletMovementPatternData2D();
			} else if (!advance_movement_pattern(curve, pattern.face_movement_direction, pattern.repeat_pattern, pattern.distance_traveled, velocity_delta, curr_bullet_direction, curr_bullet_transf)) {
				all_movement_pattern_data[i] = BulletMovementPatternData2D();
			}
		}
	}
	// World-space wind: added after pattern steering so patterns shape
	// the ballistic step and the offset drifts the result (matches the
	// documented velocity composition direction * speed + offset).
	velocity_delta += inherited_velocity_offset * (real_t)delta;
	// Gravity: per-bullet acceleration inside its time window, scaled by the
	// gravity strength curve when present. Default (zero vectors, zero
	// windows beyond immediate) reproduces straight top-down flight:
	// all_gravity reads zero and the window short-circuits below.
	// Zeroed on every new life and on set_gravity (new regime).
	// Both gates hoist out of the loop (gravity_active, gravity_window):
	// a volley with no pull anywhere skips the block for every bullet.
	if (gravity_active && gravity_window && i >= 0 && i < (int)all_gravity.size() && i < (int)all_gravity_velocity.size()) {
		const Vector2 base_g = all_gravity[i];
		if (base_g.length_squared() > 0.0) {
			const BulletCurvesData2D *grav_shared = shared_bullet_curves_data.is_valid() ? shared_bullet_curves_data.ptr() : nullptr;
			const BulletCurvesData2D *grav_per = (is_per_bullet_curves_valid && per_bullet_curves_data != nullptr) ? per_bullet_curves_data : nullptr;
			const real_t scale = gravity_strength_scale_for_bullet(grav_shared, grav_per);
			const Vector2 g = base_g * scale;
			if (g.is_finite()) {
				Vector2 &gv = all_gravity_velocity[i];
				gv += g * (real_t)delta;
				velocity_delta += gv * (real_t)delta;
			}
		}
	}
	if (is_wobble_feature_enabled && i >= 0 && i < (int)wobble_distance_traveled.size()) {
		wobble_distance_traveled[i] += velocity_delta.length();
	}

		auto &curr_bullet_origin = all_cached_instance_origin[i];

		// 7. ORBITING LOGIC (RELYING ON HOMING TARGETS)
		// A deque that ran dry unlocks the orbit under RelockAlways and
		// RelockOnTargetChange; StayLocked rides out the gap, keeping
		// angle/center/identity so the ring re-pins silently when a target
		// returns. Helpers below keep this policy-aware everywhere.
		const bool orbit_vectors_ready = i >= 0 && i < (int)all_orbiting_data.size() && i < (int)all_orbiting_status.size();
		if (is_orbiting_feature_enabled && (target_deque_used_for_orbiting == nullptr || target_deque_used_for_orbiting->empty())) {
			if (orbit_vectors_ready && all_orbiting_data[i].lock_policy != StayLocked) {
				all_orbiting_data[i].is_locked_orbiting = false;
			}
		}
		if (is_orbiting_feature_enabled && orbit_vectors_ready && all_orbiting_status[i] && target_deque_used_for_orbiting != nullptr && !target_deque_used_for_orbiting->empty()) {
			OrbitingData *const orbiting_data = &all_orbiting_data[i];
			if (orbiting_data != nullptr) {
				// RelockOnTargetChange watches identity, not distance: a new
				// front target (retarget, round-robin, reached-pop) unlocks
				// so the bullet re-acquires the ring around the new target;
				// the same target moving keeps the lock. StayLocked never
				// unlocks here; RelockAlways unlocked via the no-op write
				// and replace paths, not per-frame distance.
				if (orbiting_data->is_locked_orbiting && orbiting_data->lock_policy == RelockOnTargetChange && orbit_should_unlock_for_front_change(*orbiting_data, *target_deque_used_for_orbiting)) {
					orbiting_data->is_locked_orbiting = false;
				}
				// Ring center for this frame: FollowTarget tracks the target,
				// FollowDeadzone pins until the target walks out of the
				// deadzone, Anchored freezes at the lock point. Unlocked
				// bullets always fly toward the live target so they can
				// still reach the ring. A non-finite deque cache (freed
				// Node2D target between trim and tick) holds the bullet
				// still this frame instead of poisoning ballistics.
				if (!homing_target_pos.is_finite()) {
					velocity_delta = Vector2(0, 0);
				} else {
				const Vector2 orbit_center = orbiting_data->is_locked_orbiting ? orbit_effective_center(*orbiting_data, homing_target_pos) : homing_target_pos;
				const Vector2 to_center = curr_bullet_origin - orbit_center;
				const real_t current_dist = to_center.length();
				const bool already_locked = orbiting_data->is_locked_orbiting;

				// Track if we are ACTUALLY doing orbit movement this frame
				bool is_physically_orbiting_this_frame = false;

				// Movement Logic (Locked or Boundary Arrival)
				// DontMove = escort: holds a fixed ring slot (angle set at
				// lock time, never advanced) and translates with the target.
				// Zero-delta ticks and zero-speed bullets hold still: with
				// no time passing (or no speed to advance the sweep) the
				// ring slot is already correct, so any snap would be a
				// teleport, not motion.
				const real_t orbit_speed = all_cached_speed[i];
				const bool orbit_can_move = delta > 0.0 && orbit_speed > 0.0;
				if (already_locked && orbiting_data->direction == DontMove) {
					if (orbiting_data->rigid_follow || orbit_can_move) {
						Vector2 target_pos = orbit_center + Vector2(orbiting_data->radius, 0).rotated(orbiting_data->angle);
						Vector2 snap_delta = target_pos - curr_bullet_origin;
						if (orbiting_data->rigid_follow) {
							// Rigid follow: the formation holds regardless
							// of bullet speed, so fast targets can't drag
							// escorts behind.
							velocity_delta = snap_delta;
						} else if (orbit_can_move) {
							real_t max_step = orbit_speed * (real_t)delta;
							if (snap_delta.length_squared() > max_step * max_step) {
								snap_delta = snap_delta.normalized() * max_step;
							}
							velocity_delta = snap_delta;
						} else {
							velocity_delta = Vector2(0, 0);
						}
					} else {
						velocity_delta = Vector2(0, 0);
					}

					is_physically_orbiting_this_frame = true;
				} else if (already_locked) {
					real_t dir_multiplier = (orbiting_data->direction == OrbitRight) ? 1.0 : (orbiting_data->direction == OrbitLeft ? -1.0 : 0.0);

					if (dir_multiplier != 0.0) {
						// Guard against tiny radius (division by zero -> inf/NaN)
						real_t safe_radius = orbiting_data->radius;
						if (safe_radius < 0.01) {
							safe_radius = 0.01;
						}
						// Rigid follow moves the whole ring slot with the
						// target first (same as DontMove escorts), then
						// advances the angle: circling never lags behind a
						// moving target, no matter how slow the bullet is.
						// With rigid follow off, the angle advances but the
						// ring center lags: cheap drift look, bullets fall
						// behind fast targets (clamped to speed * delta).
						// Zero-delta ticks hold both angle and position: with
						// no time passing any snap would be a teleport.
						if (orbiting_data->rigid_follow) {
							if (delta > 0.0) {
								orbiting_data->angle += (orbit_speed / safe_radius) * dir_multiplier * (real_t)delta;
							}
							// Long-session guard: the angle feeds rotated()
							// only (periodic), so wrap it once float
							// precision could degrade. Threshold form keeps
							// every sane-session read bit-identical.
							if (orbiting_data->angle > 1000000.0 || orbiting_data->angle < -1000000.0) {
								orbiting_data->angle = Math::fposmod(orbiting_data->angle, (real_t)Math::TAU);
							}
							velocity_delta = (orbit_center + Vector2(orbiting_data->radius, 0).rotated(orbiting_data->angle)) - curr_bullet_origin;
						} else if (orbit_can_move) {
							real_t angular_speed = (orbit_speed / safe_radius) * dir_multiplier;
							orbiting_data->angle += angular_speed * delta;
							if (orbiting_data->angle > 1000000.0 || orbiting_data->angle < -1000000.0) {
								orbiting_data->angle = Math::fposmod(orbiting_data->angle, (real_t)Math::TAU);
							}
							Vector2 target_pos = orbit_center + Vector2(orbiting_data->radius, 0).rotated(orbiting_data->angle);
							Vector2 snap_delta = target_pos - curr_bullet_origin;
							real_t max_step = orbit_speed * (real_t)delta;
							if (snap_delta.length_squared() > max_step * max_step) {
								snap_delta = snap_delta.normalized() * max_step;
							}
							velocity_delta = snap_delta;
						} else {
							velocity_delta = Vector2(0, 0);
						}
					} else {
						velocity_delta = (orbit_center + Vector2(orbiting_data->radius, 0).rotated(orbiting_data->angle)) - curr_bullet_origin;
					}

					is_physically_orbiting_this_frame = true;
				}
				// Check exact frame arrival: use epsilon so low-speed / high-FPS bullets still lock (speed*delta can be <0.2px)
				// DontMove locks here too: the angle stored is the escort's
				// fixed slot, held (never advanced) by the branch above.
				// Zero-delta ticks never lock: with no motion the distance
				// test is meaningless and the snap below would teleport.
				else if (delta > 0.0 && Math::abs(current_dist - orbiting_data->radius) < Math::max((real_t)(orbit_speed * delta), (real_t)2.0)) {
					// REACHED RADIUS - LOCK NOW
					orbiting_data->angle = to_center.angle();
					orbit_stamp_lock(*orbiting_data, orbit_center, *target_deque_used_for_orbiting);

					Vector2 target_pos = orbit_center + Vector2(orbiting_data->radius, 0).rotated(orbiting_data->angle);
					Vector2 snap_delta = target_pos - curr_bullet_origin;
					real_t max_step = orbit_speed * (real_t)delta;
					if (snap_delta.length_squared() > max_step * max_step) {
						snap_delta = snap_delta.normalized() * max_step;
					}
					velocity_delta = snap_delta;

					// We consider this frame as orbiting because we just snapped to the ring
					is_physically_orbiting_this_frame = true;
				} else if (current_dist < orbiting_data->radius) {
					// SPAWNED INSIDE - PUSH OUT
					// This is technically NOT orbiting yet, it's just moving to the border.
					// Zero-delta ticks hold still: advancing next_dist by
					// speed * 0 keeps the bullet exactly where it is.
					Vector2 outward_dir = (current_dist > 0.1f) ? (to_center / current_dist) : Vector2(1, 0);
					real_t next_dist = current_dist + (orbit_speed * (real_t)delta);
					Vector2 target_pos = orbit_center + (outward_dir * next_dist);
					velocity_delta = target_pos - curr_bullet_origin;
				}

				// TEXTURE ROTATION WHEN ORBITING
				// Only rotate if the bullet is PHYSICALLY orbiting (Locked or just snapped).
				// DontMove escorts still face the target (FaceTarget /
				// FaceOppositeTarget); tangential modes are skipped - an
				// escort has no direction of travel.
				if (is_physically_orbiting_this_frame && (orbiting_data->direction != DontMove || (orbiting_data->texture_rotation != FaceOrbitingDirection && orbiting_data->texture_rotation != FaceOppositeOrbitingDirection))) {
					Vector2 look_dir = Vector2();
					// Zero-radius guard: a bullet sitting exactly on the orbit
					// center has no defined facing; keep the old yaw instead
					// of normalizing a zero vector into a stall (angle 0 snap).
					if ((curr_bullet_origin - orbit_center).length_squared() >= 0.000001) {
						Vector2 radial_vec = (curr_bullet_origin - orbit_center).normalized();

						switch (orbiting_data->texture_rotation) {
							case FaceTarget:
								look_dir = -radial_vec;
								break;
							case FaceOppositeTarget:
								look_dir = radial_vec;
								break;
							case FaceOrbitingDirection:
								look_dir = (orbiting_data->direction == OrbitRight) ? Vector2(-radial_vec.y, radial_vec.x) : Vector2(radial_vec.y, -radial_vec.x);
								break;
							case FaceOppositeOrbitingDirection:
								look_dir = (orbiting_data->direction == OrbitRight) ? Vector2(radial_vec.y, -radial_vec.x) : Vector2(-radial_vec.y, radial_vec.x);
								break;
							default:
								break;
						}

						if (look_dir != Vector2()) {
							// Orbiting owns its texture rotation: it must not require the
							// homing_take_control_of_texture_rotation flag (an undocumented
							// cross-feature dependency that left Face* modes silently dead).
							rotate_to_target_preserve_interpolation(i, look_dir, false);
						}
					}
				}
				} // end non-finite orbit-center guard
			}
		}

		// 8. TRANSFORM UPDATES
		curr_bullet_origin += velocity_delta;
		curr_bullet_transf.set_origin(curr_bullet_origin);

		auto &curr_shape_transf = all_cached_shape_transforms[i];
		auto &curr_shape_origin = all_cached_shape_origin[i];
		// Instance carries the texture rotation for rendering; physics must
		// use the logical (un-textured) rotation. When rotate_only_textures
		// is true, keep the shape at its previous logical orientation (it
		// does not follow bullet rotation), otherwise follow the bullet and
		// strip the texture-only offset.
		if (!rotate_only_textures) {
			curr_shape_transf = curr_bullet_transf;
			if (strip_texture_rotation) {
				// Same as rotated_local(-texture_rotation): basis * R.
				const Vector2 c0 = curr_shape_transf.columns[0];
				const Vector2 c1 = curr_shape_transf.columns[1];
				curr_shape_transf.columns[0] = c0 * strip_cos + c1 * strip_sin;
				curr_shape_transf.columns[1] = c1 * strip_cos - c0 * strip_sin;
			}
		}
		Vector2 rotated_offset = Vector2(0, 0);
		if (has_shape_offset) {
			// Rotate by the shape's facing (column 0 direction) without
			// atan2 + sin/cos: same result as rotated(get_rotation()).
			const Vector2 facing = curr_shape_transf.columns[0];
			const real_t facing_len = facing.length();
			if (facing_len > (real_t)0.0) {
				const Vector2 f = facing / facing_len;
				rotated_offset = Vector2(f.x * cache_collision_shape_offset.x - f.y * cache_collision_shape_offset.y, f.y * cache_collision_shape_offset.x + f.x * cache_collision_shape_offset.y);
			} else {
				rotated_offset = cache_collision_shape_offset;
			}
		}
		curr_shape_origin = curr_bullet_origin + rotated_offset;
		curr_shape_transf.set_origin(curr_shape_origin);

		physics_server->area_set_shape_transform(area, i, curr_shape_transf);
		move_bullet_attachment(velocity_delta, i);
		write_trail_instances(i);

		// 7b. REACHED SIGNAL (after all steering): tests the post-move
		// position directly. The transform was already advanced above, so
		// predicting again with velocity_delta would test two ticks
		// ahead. A bullet that followed the shared deque this tick has no per-bullet signal data - skip it.
		// ran: homing_target_pos below is only valid when this bullet
		// actually homed this tick.
		if (target_deque_used_for_orbiting != nullptr && !target_deque_used_for_orbiting->empty() && homing_target_pos.is_finite()) {
			try_to_emit_bullet_homing_target_reached_signal(*target_deque_used_for_orbiting, shared_homing_deque_enabled, i, curr_bullet_origin, homing_target_pos, Vector2(0, 0));
		}

		// 9. MOVEMENT SPEED ACCELERATION - per-bullet curve wins per
		// bullet, shared is the fallback; plain ballistics otherwise.
		// NOTE: disabled bullets freeze at their disable-time speed:
		// per-bullet ballistics are individually owned here, so a wake
		// resumes where that bullet left off (see enable_bullet).
		// Gravity steers velocity directly (no uphill slowdown model here:
		// speed magnitude stays ballistic, the step already curved above).
		// Linear drag trims speed after curves/accel so TD shells decay.
		if (is_per_bullet_curves_valid && per_bullet_curves_data->movement_speed_curve.is_valid()) {
			bullet_accelerate_speed_using_curve(i, delta, per_bullet_curves_data);
		} else if (shared_curves_acceleration_curve_valid) {
			all_cached_speed[i] = shared_movement_speed_val;
			all_cached_velocity[i] = all_cached_direction[i] * shared_movement_speed_val + inherited_velocity_offset + ((i >= 0 && i < (int)all_gravity_velocity.size()) ? all_gravity_velocity[i] : Vector2(0, 0));
		} else {
			bullet_accelerate_speed(i, delta);
		}
		// Bounce boost vs curve overwrite: curves rewrite speed every
		// tick, which would erase a bounce boost on the next frame, so
		// re-apply this bullet's cumulative multiplier on curve-driven
		// ticks. Plain accel needs no rescale (the boost rides inside
		// the cached speed and the bounce-time ceiling raise keeps its
		// clamp away); drag below then decays the scaled total.
		if ((is_per_bullet_curves_valid && per_bullet_curves_data != nullptr && per_bullet_curves_data->movement_speed_curve.is_valid()) || shared_curves_acceleration_curve_valid) {
			if (bounce_speed_scaled && i >= 0 && i < (int)all_bounce_speed_multiplier.size()) {
				const real_t bmult = all_bounce_speed_multiplier[i];
				if (Math::is_finite((double)bmult) && bmult != (real_t)1.0) {
					all_cached_speed[i] *= bmult;
					all_cached_velocity[i] = all_cached_direction[i] * all_cached_speed[i] + inherited_velocity_offset + ((i >= 0 && i < (int)all_gravity_velocity.size()) ? all_gravity_velocity[i] : Vector2(0, 0));
				}
			}
		}
		if (linear_drag > 0.0 && Math::is_finite(linear_drag)) {
			const real_t keep = Math::max((real_t)0.0, (real_t)1.0 - linear_drag * (real_t)delta);
			all_cached_speed[i] *= keep;
			all_cached_velocity[i] = all_cached_direction[i] * all_cached_speed[i] + inherited_velocity_offset + ((i >= 0 && i < (int)all_gravity_velocity.size()) ? all_gravity_velocity[i] : Vector2(0, 0));
		} else if (gravity.length_squared() > 0.0) {
			all_cached_velocity[i] = all_cached_direction[i] * all_cached_speed[i] + inherited_velocity_offset + ((i >= 0 && i < (int)all_gravity_velocity.size()) ? all_gravity_velocity[i] : Vector2(0, 0));
		}
	}
	} // tick_inverse_scope
	if (!is_using_physics_interpolation) {
		batch_flush_instance_transforms();
	}

	// Collisions last: a handler can kill the whole volley mid-drain, so work on a copy - the live list may vanish under us.
	// Self-liveness token (same pattern as handle_bullet_collision): a
	// handler that immediately frees this volley leaves every member
	// access below as use-after-free. ObjectDB validates the id without
	// touching the object, so a freed volley breaks safely instead of
	// crashing (misuse is still prohibited by the handler contract).
	const uint64_t drain_self_id = get_instance_id();
	if (!all_collided_bullets.empty()) {
		collision_scratch.clear();
		collision_scratch.swap(all_collided_bullets);
		// The dedup keys describe exactly this drain window. Clearing them
		// here (not at the end) is safe: the physics server only queues from
		// callbacks that run outside the drain, and any overlap that starts
		// re-filling mid-drain must be able to queue again next frame.
		clear_collision_dedup_keys();
		for (auto &data : collision_scratch) {
			handle_bullet_collision(data.collision_type, data.bullet_index, data.collided_instance_id, data.queue_bullet_epoch, data.queue_target_velocity, data.queue_target_velocity_valid, data.queue_target_position, data.queue_target_position_valid);
			// handle_bullet_collision calls straight into user code, and that code may free this very volley, so
			// check we're still alive before touching anything below (queue_free is caught by the second check).
			// reject via the factory guards).
			if (ObjectDB::get_instance(ObjectID(drain_self_id)) != this) {
				// Freed: every member (collision_scratch included) is gone,
				// so touching anything here would be use-after-free.
				return;
			}
			if (is_queued_for_deletion()) {
				collision_scratch.clear();
				break;
			}
		}
	}
}

// ---- Per-bullet helpers called from move_bullets (inline, this file only) ----

_ALWAYS_INLINE_ void BulletVolley2D::batch_flush_instance_transforms() {
	if (!multi.is_valid() || amount_bullets != multi->get_instance_count()) {
		return;
	}
	if ((int)all_cached_instance_transforms.size() != amount_bullets || (int)batch_buffer.size() != amount_bullets * 8) {
		return;
	}
#ifdef DEV_ENABLED
	ERR_FAIL_COND((int)batch_buffer.size() != amount_bullets * 8);
#endif
	float *w = batch_buffer.ptrw();
	// Same degenerate-global guard as interpolate_bullet_visuals: never
	// write a non-finite inverse into the buffer.
	const Transform2D node_global = get_global_transform();
	if (!is_transform_invertible_safe(node_global)) {
		return;
	}
	const Transform2D multimesh_inv = node_global.affine_inverse();
	for (int i = 0; i < amount_bullets; ++i) {
		Transform2D t = zero_transform;
		if (all_bullets_enabled_set.contains(i)) {
			t = multimesh_inv * all_cached_instance_transforms[i];
		}
		w[i * 8 + 0] = t.columns[0][0];
		w[i * 8 + 1] = t.columns[1][0];
		w[i * 8 + 2] = 0;
		w[i * 8 + 3] = t.columns[2][0];
		w[i * 8 + 4] = t.columns[0][1];
		w[i * 8 + 5] = t.columns[1][1];
		w[i * 8 + 6] = 0;
		w[i * 8 + 7] = t.columns[2][1];
	}

	multi->set_buffer(batch_buffer);
}

_ALWAYS_INLINE_ void BulletVolley2D::apply_direction_curve_texture_rotation_if_needed(Vector2 &curr_bullet_direction, Transform2D &curr_bullet_transf, double delta, const BulletCurvesData2D *curves_data) const {
	bool should_apply = curves_data->rotate_towards_adjusted_direction && !is_rotation_data_active;

	if (!should_apply) {
		return;
	}

	real_t target = curr_bullet_direction.angle();
	real_t current = curr_bullet_transf.get_rotation();

	real_t diff = Math::fposmod(target - current + static_cast<real_t>(Math::PI), static_cast<real_t>(Math::TAU)) - static_cast<real_t>(Math::PI);
	// Math::abs: a negative rotation speed would make Math::clamp's min > max,
	// collapsing the clamp to a single bound (full-rate rotation, wrong direction).
	real_t step = Math::abs(curves_data->direction_curve_rotation_speed) * (real_t)delta;

	rotate_transform_locally(curr_bullet_transf, Math::clamp(diff, -step, step));
}

_ALWAYS_INLINE_ void BulletVolley2D::bullet_accelerate_speed(int bullet_index, double delta) {
	real_t &curr_bullet_speed = all_cached_speed[bullet_index];
	real_t curr_max_bullet_speed = all_cached_max_speed[bullet_index];

	real_t acceleration = all_cached_acceleration[bullet_index] * delta;
	real_t new_speed = curr_bullet_speed + acceleration;
	// max_speed <= 0 means unlimited (matches max_collision_count = 0 and
	// the resource default of 0): a default-constructed BulletSpeedData2D
	// must fly at constant speed, not freeze after one tick.
	if (curr_max_bullet_speed > 0.0) {
		new_speed = Math::clamp(new_speed, -curr_max_bullet_speed, curr_max_bullet_speed);
	}
	curr_bullet_speed = new_speed;

	all_cached_velocity[bullet_index] = all_cached_direction[bullet_index] * curr_bullet_speed + inherited_velocity_offset;
}

_ALWAYS_INLINE_ void BulletVolley2D::bullet_accelerate_speed_using_curve(int bullet_index, double delta, const BulletCurvesData2D *curves_data) {
	real_t &curr_bullet_speed = all_cached_speed[bullet_index];
	curr_bullet_speed = get_bullet_curves_movement_speed(curves_data);

	all_cached_velocity[bullet_index] = all_cached_direction[bullet_index] * curr_bullet_speed + inherited_velocity_offset;
}

_ALWAYS_INLINE_ void BulletVolley2D::bullet_accelerate_rotation_speed(int bullet_index, double delta) {
	real_t &curr_bullet_rotation_speed = all_rotation_speed[bullet_index];
	real_t curr_max_rotation_speed = all_max_rotation_speed[bullet_index];

	// max <= 0 means unlimited (same convention as linear speed).
	// Otherwise clamp symmetrically to [-max, max]: once the speed is
	// already outside the band (e.g. seeded by a curve), acceleration in
	// either direction recovers toward the band instead of freezing.
	real_t acceleration = all_rotation_acceleration[bullet_index] * delta;
	real_t new_speed = curr_bullet_rotation_speed + acceleration;
	if (curr_max_rotation_speed > 0.0) {
		new_speed = Math::clamp(new_speed, -curr_max_rotation_speed, curr_max_rotation_speed);
	}
	curr_bullet_rotation_speed = new_speed;
}

_ALWAYS_INLINE_ void BulletVolley2D::bullet_accelerate_rotation_speed_using_curve(int bullet_index, double delta, const BulletCurvesData2D *curves_data) {
	real_t &curr_bullet_rotation_speed = all_rotation_speed[bullet_index];

	curr_bullet_rotation_speed = get_bullet_curves_rotation_speed(curves_data);
}

_ALWAYS_INLINE_ void BulletVolley2D::move_bullet_attachment(const Vector2 &translate_by, int bullet_index) {
	if (bullet_index < 0 || bullet_index >= amount_bullets) {
		return;
	}
	if (bullet_index >= (int)attachments.size() || bullet_index >= (int)attachment_stick_relative_to_bullet.size() || bullet_index >= (int)attachment_transforms.size() || bullet_index >= (int)all_cached_instance_transforms.size()) {
		return;
	}
	auto &curr_attachment = attachments[bullet_index];

	if (!curr_attachment) {
		return;
	}

	Transform2D new_attachment_transf;
	if (attachment_stick_relative_to_bullet[bullet_index]) {
		const Transform2D &bullet_global_transf = all_cached_instance_transforms[bullet_index];
		new_attachment_transf = calculate_attachment_global_transf(bullet_index, bullet_global_transf);
	} else {
		new_attachment_transf = attachment_transforms[bullet_index];
		new_attachment_transf = new_attachment_transf.translated(translate_by);
	}

	// Store the new transform as the current one
	attachment_transforms[bullet_index] = new_attachment_transf;

	// Apply immediately only if not using interpolation
	if (bullet_factory == nullptr || !bullet_factory->use_physics_interpolation) {
		curr_attachment->set_global_transform(new_attachment_transf);
	}
}

_ALWAYS_INLINE_ bool BulletVolley2D::advance_movement_pattern(const Ref<Curve2D> &curve, bool face_movement_direction, bool repeat_pattern, real_t &distance_traveled, Vector2 &velocity_delta, Vector2 &curr_bullet_direction, Transform2D &curr_bullet_transf, real_t known_len) {
	const real_t len = (known_len >= 0.001) ? known_len : curve->get_baked_length();
	// NaN baked length (corrupt Curve2D points) fails the < comparison, so
	// check finiteness explicitly: without it fmod/sample_baked propagate
	// NaN into velocity/origin and brick the volley permanently.
	if (!Math::is_finite(len) || len < 0.001) {
		return false;
	}
	const real_t prev_dist = distance_traveled;
	const real_t advance_dist = velocity_delta.length();
	distance_traveled += advance_dist;
	// Non-repeating patterns stop exactly at the end: clamp into the
	// final tile instead of wrapping once past it and snapping back.
	const real_t clamped_dist = (!repeat_pattern && distance_traveled >= len) ? len : distance_traveled;
	const real_t s1 = Math::fmod(prev_dist, len);
	const real_t s2 = Math::fmod(clamped_dist, len);
	const int64_t l1 = (int64_t)(prev_dist / len);
	const int64_t l2 = (int64_t)(clamped_dist / len);
	// p2 - p1 = (l2 - l1) * disp + sample(s2) - sample(s1): the curve
	// start cancels, and the lap displacement only matters on the tick a
	// lap boundary is crossed. Two sample_baked calls per bullet per tick
	// instead of four.
	Vector2 local_delta = curve->sample_baked(s2) - curve->sample_baked(s1);
	if (l2 != l1) {
		const Vector2 disp = curve->sample_baked(len * 0.9999) - curve->sample_baked(0.0);
		local_delta += (real_t)(l2 - l1) * disp;
	}
	// Reuse advance_dist (== |velocity_delta|, unchanged since entry) instead
	// of a second length() sqrt per bullet per tick.
	if (advance_dist > 0.0001 && local_delta.length_squared() > 0.00000001) {
		Vector2 pattern_direction = local_delta.rotated(curr_bullet_direction.angle()).normalized();
		velocity_delta = pattern_direction * advance_dist;
	}
	if (face_movement_direction && velocity_delta.length_squared() > 0.0001) {
		const Vector2 tangent = velocity_delta.normalized();
		// Preserve scale like set_bullet_texture_rotation_towards_position does.
		const Vector2 pattern_scale = curr_bullet_transf.get_scale();
		curr_bullet_transf.set_rotation_and_scale(tangent.angle(), pattern_scale);
	}
	if (!repeat_pattern && distance_traveled >= len) {
		if (face_movement_direction) {
			const Vector2 logical_dir = curr_bullet_direction.normalized();
			const Vector2 pattern_scale = curr_bullet_transf.get_scale();
			curr_bullet_transf.set_rotation_and_scale(logical_dir.angle(), pattern_scale);
		}
		return false;
	}
	return true;
}

_ALWAYS_INLINE_ void BulletVolley2D::orbit_stamp_lock(OrbitingData &orbiting_data, const Vector2 &center, const HomingTargetDeque &deque) {
	orbiting_data.is_locked_orbiting = true;
	orbiting_data.locked_center = center;
	orbiting_data.locked_target_type = orbit_target_type(deque);
	orbiting_data.locked_target_identity = orbit_target_identity(deque);
}

_ALWAYS_INLINE_ void BulletVolley2D::update_homing(HomingTargetDeque &homing_deque, int bullet_index, double delta, Vector2 &bullet_pos, Vector2 &target_pos) {
	if (bullet_index < 0 || bullet_index >= (int)all_cached_instance_origin.size() || bullet_index >= (int)all_cached_direction.size() || bullet_index >= (int)all_cached_instance_transforms.size()) {
		return;
	}
	if (!Math::is_finite(delta) || delta <= 0.0) {
		return;
	}
	// Get the front target's cached position
	target_pos = homing_deque.get_cached_front_target_global_position();
	if (!target_pos.is_finite()) {
		return;
	}

	bullet_pos = all_cached_instance_origin[bullet_index];
	Vector2 diff = target_pos - bullet_pos;

	real_t dist_sq = diff.length_squared();
	if (dist_sq <= 0.0) {
		return;
	}

	// Homing gating: delay (straight flight first), duration (escape
	// window), lose-range (pause while too far). All cheap float
	// compares; curves_elapsed_time is the volley clock.
	if (homing_delay_sec > 0.0 && curves_elapsed_time < homing_delay_sec) {
		return;
	}
	if (homing_duration_sec > 0.0 && curves_elapsed_time >= homing_delay_sec + homing_duration_sec) {
		return;
	}
	if (homing_lose_range_px > 0.0 && dist_sq > homing_lose_range_px * homing_lose_range_px) {
		return;
	}

	// Steering gains. A zero heading (unseeded ballistics) holds: any
	// steering below would normalize it into an angle-0 snap.
	real_t max_turn = homing_smoothing * delta;
	if (use_per_bullet_homing_smoothing && bullet_index >= 0 && bullet_index < (int)all_bullet_homing_smoothing.size()) {
		max_turn = all_bullet_homing_smoothing[bullet_index] * delta;
	}
	if (max_turn < 0.0) {
		max_turn = 0.0;
	}

	Vector2 &current_direction = all_cached_direction[bullet_index];
	if (current_direction.length_squared() < 0.00000001) {
		return;
	}

	auto &curr_transf = all_cached_instance_transforms[bullet_index];
	// If rotation is controlled via movement pattern (per-bullet or shared)
	// or rotation data, just set direction directly toward target
	if (check_exists_bullet_movement_pattern_data(bullet_index) || shared_movement_pattern_curve.is_valid() || is_rotation_data_active) {
		current_direction = diff.normalized();
	} else { // Otherwise use smoothing to rotate toward target
		// Rotate toward target with smoothing
		rotate_to_target(bullet_index, diff, max_turn);

		// Logical direction strips the texture rotation used for rendering
		// (rotate_to_target aims the visual forward; movement must not
		// inherit that offset or bullets head off-target every tick).
		current_direction = curr_transf[0].rotated(-cache_texture_rotation_radians).normalized();
	}
}

_ALWAYS_INLINE_ void BulletVolley2D::rotate_to_target_preserve_interpolation(int bullet_index, const Vector2 &diff, bool require_homing_flag) {
	if (bullet_index < 0 || bullet_index >= (int)all_cached_instance_transforms.size() || bullet_index >= (int)all_previous_instance_transf.size()) {
		return;
	}
	const Transform2D prev = all_cached_instance_transforms[bullet_index];
	rotate_to_target(bullet_index, diff, 0.0, require_homing_flag);
	all_previous_instance_transf[bullet_index] = prev;
}

_ALWAYS_INLINE_ void BulletVolley2D::update_rotation(int bullet_index, double delta) {
	real_t cache_rotation_speed = all_rotation_speed[bullet_index];
	real_t rot_delta = cache_rotation_speed * (real_t)delta;

	// Skip standing still - rotating by 0 would just add float noise
	if (cache_rotation_speed != 0.0f) {
		// max <= 0 means unlimited (same convention as linear speed):
		// without the gate, max == 0 makes abs(speed) >= 0 always true
		// and the stop flag freezes default-constructed rotation data.
		const real_t max_speed = (bullet_index >= 0 && bullet_index < (int)all_max_rotation_speed.size()) ? all_max_rotation_speed[bullet_index] : 0.0;
		const bool max_reached = max_speed > 0.0 && Math::abs(cache_rotation_speed) >= max_speed;

		if (!(max_reached && stop_rotation_when_max_reached)) {
			rotate_transform_locally(all_cached_instance_transforms[bullet_index], rot_delta);
		}
	}
}

_ALWAYS_INLINE_ void BulletVolley2D::update_rotation_using_curve(int bullet_index, double delta) {
	real_t cache_rotation_speed = all_rotation_speed[bullet_index];
	real_t rot_delta = cache_rotation_speed * (real_t)delta;

	rotate_transform_locally(all_cached_instance_transforms[bullet_index], rot_delta);
}

_ALWAYS_INLINE_ void BulletVolley2D::try_to_emit_bullet_homing_target_reached_signal(HomingTargetDeque &homing_deque, bool is_using_shared_homing_deque, int bullet_index, const Vector2 &bullet_pos, const Vector2 &target_pos, const Vector2 &post_velocity_delta) {
	if (homing_deque.empty()) {
		return;
	}
	// Reached check against the predicted post-move position: at high speed a bullet
	// can tunnel past the threshold within one tick and never fire on pre-move pos.
	const Vector2 check_pos = bullet_pos + post_velocity_delta;
	Vector2 post_to_target = target_pos - check_pos;
	real_t post_dist_sq = post_to_target.length_squared();
	real_t threshold_sq = homing_distance_before_reached * homing_distance_before_reached;
	if (post_dist_sq <= threshold_sq) { // Fully squared for perf

		HomingTarget &target = homing_deque.front();

		// Decide whether THIS bullet may fire:
		// - shared deque: per-bullet state keyed on the current front target
		// - per-bullet deque: the target's own flag (each bullet owns its targets)
		bool fire_for_this_bullet = false;
		if (is_using_shared_homing_deque) {
			if (bullet_index >= 0 && bullet_index < (int)all_shared_homing_reached.size()) {
				SharedHomingReachedState &state = all_shared_homing_reached[bullet_index];
				fire_for_this_bullet = (state.front_target != (const void *)&target) || !state.fired;
				state.front_target = &target;
				state.fired = true;
			}
		} else {
			fire_for_this_bullet = !target.has_bullet_reached_target;
			target.has_bullet_reached_target = true;
		}

		// Ensure that the signal is emitted only ONCE per bullet per target
		if (fire_for_this_bullet) {
			const uint64_t bullet_epoch = (bullet_index >= 0 && bullet_index < (int)bullet_homing_epochs.size()) ? bullet_homing_epochs[bullet_index] : 0;
			switch (target.type) {
				case GlobalPositionTarget:
					// Deferred through the generation-guarded emitter: the target travels
					// as an instance id (resolved at fire time, null when freed) and a
					// stale generation no-ops, so pool reuse before the flush can neither
					// crash on a dangling pointer nor emit ghosts.
					call_deferred(CachedStringNames2D::get().m_do_emit_homing_target_reached, homing_operation_generation, bullet_index, bullet_epoch, (uint64_t)0, target_pos);
					break;
				case Node2DTarget: {
					auto &target_data = target.node2d_target_data;

					// In case the target instance is freed - will still emit the signal, but with a nullptr as the target
					uint64_t target_id = 0;
					if (homing_deque.is_homing_target_valid(target_data.target, target_data.cached_valid_instance_id)) {
						target_id = target_data.cached_valid_instance_id;
					}
					call_deferred(CachedStringNames2D::get().m_do_emit_homing_target_reached, homing_operation_generation, bullet_index, bullet_epoch, target_id, target_pos);
					break;
				}
				case NotHoming:
					break;
				case MousePositionTarget:
					call_deferred(CachedStringNames2D::get().m_do_emit_homing_target_reached, homing_operation_generation, bullet_index, bullet_epoch, (uint64_t)0, target_pos);
					break;
			}

			// Pop the front target automatically if that's what the user wants.
			// Shared deque: N bullets reaching in the same tick must queue exactly
			// ONE deferred pop, otherwise the storm would drain every target the
			// user pushed. Per-bullet deques pop their own deque per bullet - no
			// storm there.
			if (is_using_shared_homing_deque) {
				// Both the generation and the shared front epoch travel with
				// the pop: a pool reuse no-ops, and a manual shared-deque edit
				// landing before the flush cancels this stale pop.
				if (shared_homing_deque_auto_pop_after_target_reached && !shared_auto_pop_queued) {
					shared_auto_pop_queued = true;
					call_deferred(CachedStringNames2D::get().m_do_shared_auto_pop_front_target, homing_operation_generation, shared_homing_front_epoch);
				}
			} else {
				if (bullet_homing_auto_pop_after_target_reached) {
					const uint64_t pop_epoch = (bullet_index >= 0 && bullet_index < (int)bullet_homing_epochs.size()) ? bullet_homing_epochs[bullet_index] : 0;
					call_deferred(CachedStringNames2D::get().m_do_auto_pop_front_target, homing_operation_generation, bullet_index, pop_epoch);
				}
			}
		}
	}
}

_ALWAYS_INLINE_ bool BulletVolley2D::update_homing_timer(double delta) {
	homing_update_timer -= delta;
	if (homing_update_timer <= 0.0) {
		homing_update_timer = homing_update_interval;
		return true;
	}
	return false;
}

} // namespace BlastBullets2D
