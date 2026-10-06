// The per-frame motion loop (move_bullets) and every helper it calls per bullet.
// Hot path: keep helpers here or in bullet_volley2d_internal.hpp (inline) so no
// per-bullet call crosses a translation unit. BulletFactory2D calls move_bullets
// once per active volley per physics frame.

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

bool BulletVolley2D::tick_may_continue(uint64_t self_id) const {
	if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
		return false;
	}
	if (is_queued_for_deletion()) {
		return false;
	}
	return bullet_factory == nullptr || !bullet_factory->is_bullet_processing_paused();
}

void BulletVolley2D::tick(double delta) {
	if (!Math::is_finite(delta) || delta < 0.0) {
		return;
	}
	const uint64_t self_id = get_instance_id();
	// Bounce bookkeeping runs only while armed; plain volleys skip it. The
	// tick id advances BEFORE the drain: the one-bounce-per-bullet-per-tick
	// guard compares against it, and a fresh ledger holds 0 (a volley whose
	// first tick is a drain must not read as "already bounced this tick").
	if (bounce_enabled()) {
		++bounce_tick_counter;
		bounce_last_delta = delta;
	}
	// 1. Hits first: the records were queued by the physics server for the
	// pose it just tested (the current cached pose), so handlers, bounce
	// normals and hit effects all see the impact pose, and a killed bullet
	// never integrates one more step.
	drain_collisions();
	if (!tick_may_continue(self_id) || !is_active) {
		return;
	}
	// 2. Integration (no user code runs inside; homing reaches are queued).
	move_bullets(delta);
	// 3. Homing reached events (live) + auto-pops.
	if (!homing_reached_events.empty() || shared_pop_requested) {
		if (!dispatch_homing_events() || !tick_may_continue(self_id) || !is_active) {
			return;
		}
	}
	// 4. Sprite animation (may emit sprite_animation_finished, live).
	advance_sprite_animation(delta);
	if (anim_finished_event_pending) {
		anim_finished_event_pending = false;
		emit_signal(CachedStringNames2D::get().sprite_animation_finished, this);
		if (!tick_may_continue(self_id) || !is_active) {
			return;
		}
	}
	// 5. Lifetime (may emit life_time_over, live).
	reduce_lifetime(delta);
}

// Values move_bullets computes once per tick, before the bullet loop.
struct BulletVolley2D::MoveTick2D {
	double delta = 0.0;
	bool homing_interval_reached = false;
	bool shared_homing = false; // the shared deque holds targets this tick
	bool per_bullet_homing = false; // some bullet holds its own targets
	bool orbiting = false; // some bullet orbits
	bool bounce = false; // bouncing is armed
	// Every always-sized per-bullet array covers [0, core_limit).
	int core_limit = 0;
	// Shared curve channels, sampled once (the same for every bullet).
	const BulletCurvesData2D *shared_curves = nullptr;
	bool shared_x = false;
	bool shared_y = false;
	bool shared_rotation = false;
	bool shared_speed = false;
	real_t shared_x_offset = 0.0;
	real_t shared_y_offset = 0.0;
	real_t shared_x_strength = 0.0;
	real_t shared_y_strength = 0.0;
	DirectionCurveMode shared_x_mode = DirectionCurveMode::Additive;
	DirectionCurveMode shared_y_mode = DirectionCurveMode::Additive;
	real_t shared_speed_value = 0.0;
	real_t shared_rotation_value = 0.0;
	// Shared movement pattern (0 length = none or degenerate).
	bool shared_pattern = false;
	real_t shared_pattern_len = 0.0;
	bool gravity_window = false;
	// Locked orbits before this tick moved anything (orbiting only), so the
	// pattern gate and the orbit section agree even when a deque empties.
	const std::vector<uint8_t> *orbit_locked = nullptr;
	// Texture-rotation strip for the physics shape (sin/cos hoisted).
	bool strip_texture_rotation = false;
	real_t strip_cos = 1.0;
	real_t strip_sin = 0.0;
	bool has_shape_offset = false;
};

// One bullet's scratch while the stages run. Created once per tick and
// re-pointed per bullet (begin): only the fields every bullet must start
// from are reset, the rest is always written before it is read.
struct BulletVolley2D::BulletStep2D {
	int i = 0;
	Transform2D *transf = nullptr; // &all_cached_instance_transforms[i]
	Vector2 *direction = nullptr; // &all_cached_direction[i]
	bool direction_updated = false; // re-derive the cached velocity
	// The deque this bullet homed by this tick (its own, else the shared
	// one) and that deque's front target: the orbit center and the reached
	// test use them. Null when it did not home.
	HomingTargetDeque *steering_deque = nullptr;
	Vector2 target_pos;
	Vector2 bullet_pos;
	const BulletCurvesData2D *curves = nullptr; // per-bullet curves, null when none
	Vector2 velocity_delta; // this tick's displacement

	void begin(int index, Transform2D &instance_transf, Vector2 &heading) {
		i = index;
		transf = &instance_transf;
		direction = &heading;
		direction_updated = false;
		steering_deque = nullptr;
		curves = nullptr;
	}
};

static _ALWAYS_INLINE_ Vector2 clamp_step(Vector2 step, real_t max_step) {
	if (step.length_squared() > max_step * max_step) {
		step = step.normalized() * max_step;
	}
	return step;
}

// Long-session guard: the orbit angle only feeds rotated() (periodic), so
// wrap it once float precision could degrade. The threshold keeps every
// sane-session read bit-identical.
static _ALWAYS_INLINE_ void wrap_orbit_angle(real_t &angle) {
	if (angle > 1000000.0 || angle < -1000000.0) {
		angle = Math::fposmod(angle, (real_t)Math::TAU);
	}
}

void BulletVolley2D::move_bullets(double delta) {
	if (amount_bullets <= 0 || physics_server == nullptr || !area.is_valid()) {
		return;
	}
	if (!Math::is_finite(delta) || delta < 0.0) {
		return;
	}
	const bool is_using_physics_interpolation = bullet_factory != nullptr && bullet_factory->use_physics_interpolation;
	if (is_using_physics_interpolation) {
		update_all_previous_transforms_for_interpolation();
	}

	// Everything the loop reads but never changes, computed once.
	const MoveTick2D t = [&]() {
		MoveTick2D t;
		t.delta = delta;
		// Unified precedence: a bullet's own deque wins; the shared deque is the
		// broadcast fallback for bullets whose own deque is empty.
		t.shared_homing = !shared_homing_deque.empty();
		t.per_bullet_homing = active_homing_count > 0;
		if (t.shared_homing || t.per_bullet_homing) {
			t.homing_interval_reached = update_homing_timer(delta);
			// The mouse is read once per interval, not once per push:
			// get_global_mouse_position() walks to the viewport each call.
			if (t.homing_interval_reached && HomingTargetDeque::mouse_homing_targets_amount > 0) {
				cached_mouse_global_position = get_global_mouse_position();
			}
		}
		if (t.shared_homing) {
			// Drop freed targets off the shared front once for every bullet.
			auto targets_amount = shared_homing_deque.get_homing_targets_amount();
			int trimmed = shared_homing_deque.bullet_homing_trim_front_invalid_targets(cached_mouse_global_position, targets_amount);
			if (trimmed > 0) {
				// Front changed: route the orbit lock per policy (a new front
				// target never inherits a stale center) and give every bullet
				// a clean reached slate for the new target.
				orbit_route_shared_front_change();
			}
			t.shared_homing = (targets_amount - trimmed) > 0;
			if (t.shared_homing && t.homing_interval_reached) {
				shared_homing_deque.refresh_cached_front_target_global_position(cached_mouse_global_position);
			}
		}
		// While the shared deque holds targets, per-bullet caches go stale
		// (their branch never runs). Refresh them on the interval too, so a
		// shared drain hands over live positions instead of push-time ones.
		if (t.shared_homing && t.homing_interval_reached && active_homing_count > 0) {
			for (size_t qi = 0; qi < all_bullet_homing_targets.size() && qi < all_homing_count.size(); ++qi) {
				if (all_homing_count[qi] > 0 && !all_bullet_homing_targets[qi].empty()) {
					all_bullet_homing_targets[qi].refresh_cached_front_target_global_position(cached_mouse_global_position);
				}
			}
		}

		t.orbiting = active_orbiting_count > 0;
		t.bounce = bounce_enabled();

		if (shared_bullet_curves_data.is_valid()) {
			const BulletCurvesData2D *c = shared_bullet_curves_data.ptr();
			t.shared_curves = c;
			t.shared_x = c->x_direction_curve.is_valid();
			t.shared_y = c->y_direction_curve.is_valid();
			t.shared_rotation = c->rotation_speed_curve.is_valid();
			t.shared_speed = c->movement_speed_curve.is_valid();
			if (t.shared_x) {
				t.shared_x_offset = get_bullet_curves_x_direction_offset(c);
				t.shared_x_strength = c->x_direction_curve_strength;
				t.shared_x_mode = c->x_direction_curve_mode;
			}
			if (t.shared_y) {
				t.shared_y_offset = get_bullet_curves_y_direction_offset(c);
				t.shared_y_strength = c->y_direction_curve_strength;
				t.shared_y_mode = c->y_direction_curve_mode;
			}
			if (t.shared_speed) {
				t.shared_speed_value = get_bullet_curves_movement_speed(c);
			}
			if (t.shared_rotation) {
				t.shared_rotation_value = get_bullet_curves_rotation_speed(c);
			}
		}

		t.shared_pattern = shared_movement_pattern_curve.is_valid();
		if (t.shared_pattern) {
			t.shared_pattern_len = shared_movement_pattern_curve->get_baked_length();
			// NaN baked length (corrupt Curve2D points) would flow into the
			// per-bullet advance as known_len; normalize to 0 so the pattern
			// gate stays shut (advance also guards).
			if (!Math::is_finite(t.shared_pattern_len)) {
				t.shared_pattern_len = 0.0;
			}
		}

		// Combined with gravity_active, a volley with no pull skips the gravity
		// block for every bullet.
		t.gravity_window = gravity_window_open();

		// Locked-orbit snapshot (member scratch: no per-tick allocation).
		if (t.orbiting) {
			std::vector<uint8_t> &snapshot = orbit_locked_scratch;
			if ((int)snapshot.size() != amount_bullets) {
				snapshot.assign(amount_bullets, 0);
			} else {
				std::fill(snapshot.begin(), snapshot.end(), (uint8_t)0);
			}
			for (int li : all_bullets_enabled_set.get_active_indexes()) {
				if (li >= 0 && li < amount_bullets && li < (int)all_orbiting_status.size() && li < (int)all_orbiting_data.size()) {
					if (all_orbiting_status[li] && all_orbiting_data[li].is_locked_orbiting) {
						snapshot[li] = 1;
					}
				}
			}
			t.orbit_locked = &snapshot;
		}

		// Warn once when homing cannot visibly do anything: no texture
		// steering, no movement pattern, no rotation data and no orbit (usually
		// a forgotten homing_take_control_of_texture_rotation).
		if (!homing_inert_warning_issued && (t.shared_homing || t.per_bullet_homing) && !homing_take_control_of_texture_rotation && !is_rotation_data_active && active_orbiting_count == 0) {
			bool any_pattern = shared_movement_pattern_curve.is_valid();
			for (int pi : all_bullets_enabled_set.get_active_indexes()) {
				if (any_pattern) {
					break;
				}
				any_pattern = check_exists_bullet_movement_pattern_data(pi);
			}
			if (!any_pattern) {
				UtilityFunctions::push_warning("BulletVolley2D has homing targets but homing_take_control_of_texture_rotation is false (and no movement pattern/rotation data), so homing will not steer bullets. Set it to true.");
				homing_inert_warning_issued = true;
			}
		}

		// BulletCurvesData2D is a mutable shared Resource: a user can gain a
		// rotation curve AFTER the volley spawned, when nothing sized the
		// rotation trio. Enforce the invariant once per tick so the rotation
		// stage can never index out of bounds (resize keeps existing speeds).
		if ((int)all_rotation_speed.size() != amount_bullets || (int)all_max_rotation_speed.size() != amount_bullets || (int)all_rotation_acceleration.size() != amount_bullets) {
			visit_rotation_trio([&](std::vector<real_t> &v) { v.resize(amount_bullets, 0.0); });
		}
		// Same invariant for the shared-deque reached states.
		if ((int)all_shared_homing_reached.size() != amount_bullets) {
			all_shared_homing_reached.resize(amount_bullets);
		}

		// The always-sized arrays are checked once here instead of per bullet.
		t.core_limit = amount_bullets;
		for (size_t size : { all_cached_instance_transforms.size(), all_cached_direction.size(), all_cached_velocity.size(), all_cached_speed.size(), all_cached_max_speed.size(), all_cached_acceleration.size(), all_cached_instance_origin.size(), all_cached_shape_transforms.size() }) {
			t.core_limit = MIN(t.core_limit, (int)size);
		}

		t.strip_texture_rotation = cache_texture_rotation_radians != 0.0;
		t.strip_cos = t.strip_texture_rotation ? (real_t)Math::cos(-cache_texture_rotation_radians) : (real_t)1.0;
		t.strip_sin = t.strip_texture_rotation ? (real_t)Math::sin(-cache_texture_rotation_radians) : (real_t)0.0;
		t.has_shape_offset = cache_collision_shape_offset != Vector2(0, 0);
		return t;
	}();

	BulletStep2D b;
	{ // One node inverse for the whole loop (trail writes convert per bullet); no user code runs inside.
		NodeInverseScope tick_inverse_scope(this);
		for (int i : all_bullets_enabled_set.get_active_indexes()) {
			if (i < 0 || i >= amount_bullets) {
				continue;
			}
			// Bounce cooldown ticks down so the bullet can escape the overlap
			// it just bounced out of (a body+area pair on one target would
			// otherwise double-flip it in place).
			if (t.bounce && i < (int)all_bounce_cooldown.size() && all_bounce_cooldown[i] > 0.0) {
				all_bounce_cooldown[i] -= (real_t)delta;
				if (all_bounce_cooldown[i] < 0.0) {
					all_bounce_cooldown[i] = 0.0;
				}
			}
			if (i >= t.core_limit) {
				continue;
			}
			b.begin(i, all_cached_instance_transforms[i], all_cached_direction[i]);
			// Steering first (homing, curves, wobble, rotation), then the
			// displacement (pattern, wind, gravity, orbit), then the write
			// (transform, shape, attachment, trail, reached signal), then
			// next tick's speed.
			step_homing(t, b);
			if (i < (int)all_bullet_curves_data.size() && all_bullet_curves_data[i].is_valid()) {
				b.curves = all_bullet_curves_data[i].ptr(); // borrowed: valid until the vector is reassigned
			}
			step_direction_curves(t, b);
			step_wobble(t, b);
			step_rotation(t, b);
			if (b.direction_updated) {
				refresh_cached_velocity(i);
			}
			// Wind-free base: the inherited offset rides along AFTER pattern
			// steering (advance_movement_pattern measures |velocity_delta|,
			// so the offset would stretch the pattern step instead of adding
			// world-space drift).
			b.velocity_delta = (*b.direction * all_cached_speed[i]) * (real_t)delta;
			step_pattern_and_forces(t, b);
			step_orbit(t, b);
			step_place(t, b);
			step_speed(t, b);
		}
	} // tick_inverse_scope
	if (!is_using_physics_interpolation) {
		batch_flush_instance_transforms();
	}
}

// 1. HOMING. The bullet's own deque wins; the shared deque is the fallback
// (also when its own deque drains this tick). Freed targets are trimmed off
// the own front first, keeping the counters honest. The reached test runs
// in step_place, after every steering stage moved the bullet.
_ALWAYS_INLINE_ void BulletVolley2D::step_homing(const MoveTick2D &t, BulletStep2D &b) {
	if (!t.per_bullet_homing && !t.shared_homing) {
		return;
	}
	const int i = b.i;
	HomingTargetDeque *deque = nullptr;
	if (i < (int)all_homing_count.size() && i < (int)all_bullet_homing_targets.size() && all_homing_count[i] > 0 && !all_bullet_homing_targets[i].empty()) {
		int &count = all_homing_count[i];
		HomingTargetDeque &own = all_bullet_homing_targets[i];
		const int trimmed_count = own.bullet_homing_trim_front_invalid_targets(cached_mouse_global_position, count);
		count -= trimmed_count;
		active_homing_count -= trimmed_count;
		if (count < 0) {
			count = 0;
		}
		if (active_homing_count < 0) {
			active_homing_count = 0;
		}
		homing_resync_count(i);
		// Trimming exposed a new front target: like a pop, orbit rings re-lock.
		if (trimmed_count > 0) {
			orbit_route_front_change_for_bullet(i, own);
		}
		if (count > 0 && !own.empty()) {
			// Moving targets refresh on the interval.
			if (t.homing_interval_reached) {
				own.refresh_cached_front_target_global_position(cached_mouse_global_position);
			}
			deque = &own;
		}
	}
	if (deque == nullptr && t.shared_homing) {
		deque = &shared_homing_deque;
	}
	if (deque == nullptr) {
		return;
	}
	update_homing(*deque, i, t.delta, b.bullet_pos, b.target_pos);
	b.direction_updated = true;
	b.steering_deque = deque;
}

// 2. DIRECTION CURVES. Per channel, the bullet's own curve wins over the
// shared one (sampled once per tick); the texture follows the first
// channel that steered.
_ALWAYS_INLINE_ void BulletVolley2D::step_direction_curves(const MoveTick2D &t, BulletStep2D &b) {
	const bool own_x = b.curves != nullptr && b.curves->x_direction_curve.is_valid();
	const bool own_y = b.curves != nullptr && b.curves->y_direction_curve.is_valid();
	const BulletCurvesData2D *texture_source = nullptr;
	if (own_x) {
		apply_x_direction_curve(*b.direction, b.curves);
		texture_source = b.curves;
	} else if (t.shared_x) {
		apply_direction_axis2d(*b.direction, 0, t.shared_x_mode, t.shared_x_offset, t.shared_x_strength);
		texture_source = t.shared_curves;
	}
	if (own_y) {
		apply_y_direction_curve(*b.direction, b.curves);
		if (texture_source == nullptr) {
			texture_source = b.curves;
		}
	} else if (t.shared_y) {
		apply_direction_axis2d(*b.direction, 1, t.shared_y_mode, t.shared_y_offset, t.shared_y_strength);
		texture_source = t.shared_curves;
	}
	if (texture_source != nullptr) {
		apply_direction_curve_texture_rotation_if_needed(*b.direction, *b.transf, t.delta, texture_source);
		b.direction_updated = true;
	}
}

// 2b. WOBBLE (waveform flight modulation). Lateral displaces the heading
// perpendicular to flight (snakes, weaves, curtains); angular oscillates
// the heading itself (corkscrews, petals). Runs on the analytic offset DELTA
// between frames (not the absolute offset) so pausing/teleporting never
// jumps the bullet. With face_movement_direction the visual slews toward the
// steered heading (skipped while rotation data owns the visual; snaps when
// face_rotation_speed <= 0).
_ALWAYS_INLINE_ void BulletVolley2D::step_wobble(const MoveTick2D &t, BulletStep2D &b) {
	const int i = b.i;
	if (!is_wobble_feature_enabled || i >= (int)all_bullet_wobble.size() || !all_bullet_wobble[i].active) {
		return;
	}
	const double delta = t.delta;
	WobbleSeed &w = all_bullet_wobble[i];
	const real_t now = (real_t)curves_elapsed_time;
	const bool in_window = now >= w.delay_sec && (w.duration_sec <= 0.0 || now < w.delay_sec + w.duration_sec);
	if (!in_window) {
		// Leaving (or not yet in) the window: the next entry starts from the
		// analytic baseline.
		w.has_last = false;
		return;
	}
	if (!(w.frequency_hz >= 0.0 && w.amplitude >= 0.0)) {
		return;
	}
	const bool phased = w.distance_phased && i < (int)wobble_distance_traveled.size();
	const real_t phase_base = phased ? wobble_distance_traveled[i] * 0.02 : now;
	const real_t damp = (w.damping_per_sec > 0.0 && now > w.delay_sec) ? (real_t)Math::exp(-(double)(w.damping_per_sec * (now - w.delay_sec))) : 1.0;
	const real_t now_off = w.amplitude * damp * evaluate_wobble_waveform(w.waveform, Math::TAU * w.frequency_hz * phase_base + w.phase);
	const real_t prev_t = now - (real_t)delta;
	const bool prev_in = prev_t >= w.delay_sec && (w.duration_sec <= 0.0 || prev_t < w.delay_sec + w.duration_sec);
	real_t prev_off = 0.0;
	if (w.has_last && Math::abs((curves_elapsed_time - delta) - w.last_time) < 1e-6) {
		// Clock continuous: exactly the offset applied last tick.
		prev_off = w.last_offset;
	} else if (prev_in && delta > 0.0) {
		const real_t prev_base = phased ? (wobble_distance_traveled[i] - (*b.direction * all_cached_speed[i]).length() * (real_t)delta) * 0.02 : prev_t;
		const real_t prev_damp = (w.damping_per_sec > 0.0 && prev_t > w.delay_sec) ? (real_t)Math::exp(-(double)(w.damping_per_sec * (prev_t - w.delay_sec))) : 1.0;
		prev_off = w.amplitude * prev_damp * evaluate_wobble_waveform(w.waveform, Math::TAU * w.frequency_hz * prev_base + w.phase);
	}
	const real_t frame_delta = now_off - prev_off;
	w.has_last = Math::is_finite(now_off);
	if (w.has_last) {
		w.last_offset = now_off;
		w.last_time = curves_elapsed_time;
	}
	if (!Math::is_finite(frame_delta) || Math::abs(frame_delta) <= 0.00001) {
		return;
	}
	Vector2 &dir = *b.direction;
	if (w.mode == 1) {
		dir = dir.rotated(Math::deg_to_rad(frame_delta));
		dir = dir.length_squared() < 0.00000001 ? Vector2(1, 0) : dir.normalized();
	} else if (dir.length_squared() > 0.00000001) {
		const Vector2 perp = Vector2(-dir.y, dir.x).normalized();
		const Vector2 steered = dir + perp * (frame_delta * 0.01);
		// A pathological frame_delta (~100x amplitude) could cancel the
		// heading; hold the old heading instead of snapping to angle 0.
		if (steered.length_squared() > 0.00000001) {
			dir = steered.normalized();
		}
	}
	b.direction_updated = true;
	if (w.face_movement_direction && !is_rotation_data_active && delta > 0.0) {
		const real_t target = dir.angle();
		const real_t current = b.transf->get_rotation();
		const real_t diff = Math::fposmod(target - current + static_cast<real_t>(Math::PI), static_cast<real_t>(Math::TAU)) - static_cast<real_t>(Math::PI);
		if (Math::is_finite(diff) && Math::abs(diff) > 0.00001) {
			if (w.face_rotation_speed <= 0.0) {
				rotate_transform_locally(*b.transf, diff);
			} else {
				const real_t step = Math::abs(w.face_rotation_speed) * (real_t)delta;
				rotate_transform_locally(*b.transf, Math::clamp(diff, -step, step));
			}
		}
	}
}

// 3-4. ROTATION. Own rotation curve > shared curve > rotation data. With
// adjust_direction_based_on_rotation the heading follows the instance
// rotation (texture rotation stripped; visual-only spin never steers). A
// pending bounce ricochet slews the sprite (visual only) toward the
// reflected heading.
_ALWAYS_INLINE_ void BulletVolley2D::step_rotation(const MoveTick2D &t, BulletStep2D &b) {
	const int i = b.i;
	const double delta = t.delta;
	if (b.curves != nullptr && b.curves->rotation_speed_curve.is_valid()) {
		bullet_accelerate_rotation_speed_using_curve(i, delta, b.curves);
		update_rotation_using_curve(i, delta);
	} else if (t.shared_rotation) {
		all_rotation_speed[i] = t.shared_rotation_value;
		update_rotation_using_curve(i, delta);
	} else if (is_rotation_data_active) {
		bullet_accelerate_rotation_speed(i, delta);
		update_rotation(i, delta);
	}
	if (adjust_direction_based_on_rotation && !rotate_only_textures) {
		// Degenerate basis (zero scale) keeps the last good direction.
		const Vector2 stripped_basis = b.transf->columns[0].rotated(-cache_texture_rotation_radians);
		if (stripped_basis.length_squared() > 0.00000001) {
			*b.direction = stripped_basis.normalized();
		}
		b.direction_updated = true;
	}
	// Bounce visual pursuit: ballistics reflected instantly in the drain;
	// skipped while adjust_direction_based_on_rotation owns ballistics.
	if (t.bounce && bounce_rotate_texture && bounce_rotation_smooth > 0.0 && !adjust_direction_based_on_rotation && i < (int)bounce_visual_pending.size() && bounce_visual_pending[i] && i < (int)bounce_visual_target.size()) {
		const Vector2 want = bounce_visual_target[i];
		if (want.length_squared() > 0.00000001 && Math::is_finite((double)bounce_rotation_smooth) && delta > 0.0) {
			rotate_to_target(i, want, bounce_rotation_smooth * (real_t)delta, false);
			const Vector2 fwd = (*b.transf)[0].rotated(-cache_texture_rotation_radians);
			if (fwd.length_squared() > 0.00000001 && fwd.normalized().dot(want.normalized()) > 0.9999) {
				bounce_visual_pending[i] = 0;
			}
		} else {
			bounce_visual_pending[i] = 0;
		}
	}
}

// 6. MOVEMENT PATTERN, WIND, GRAVITY. The own pattern wins over the shared
// one; a locked orbit owns the displacement (the pattern and its distance
// ledger are skipped, so a non-repeating pattern cannot finish invisibly).
// World-space wind is added after the pattern shaped the step; gravity
// integrates the fall speed (semi-implicit) inside its time window.
_ALWAYS_INLINE_ void BulletVolley2D::step_pattern_and_forces(const MoveTick2D &t, BulletStep2D &b) {
	const int i = b.i;
	const double delta = t.delta;
	const bool orbit_locked = t.orbit_locked != nullptr && i < (int)t.orbit_locked->size() && (*t.orbit_locked)[i] && b.steering_deque != nullptr && !b.steering_deque->empty();
	if (!orbit_locked) {
		if (check_exists_bullet_movement_pattern_data(i)) {
			BulletMovementPatternData2D &pattern = all_movement_pattern_data[i];
			if (pattern.path_curve.is_null() || !advance_movement_pattern(pattern.path_curve, pattern.face_movement_direction, pattern.repeat_pattern, pattern.distance_traveled, b.velocity_delta, *b.direction, *b.transf)) {
				pattern = BulletMovementPatternData2D();
			}
		} else if (t.shared_pattern && t.shared_pattern_len >= 0.001 && i < (int)shared_movement_pattern_distances.size() && (shared_movement_pattern_repeat || shared_movement_pattern_distances[i] < t.shared_pattern_len)) {
			if (!advance_movement_pattern(shared_movement_pattern_curve, shared_movement_pattern_face_movement_direction, shared_movement_pattern_repeat, shared_movement_pattern_distances[i], b.velocity_delta, *b.direction, *b.transf, t.shared_pattern_len)) {
				// Park the ledger at the end (degenerate curve or completed
				// run) so finished bullets skip without an extra flag.
				shared_movement_pattern_distances[i] = t.shared_pattern_len;
			}
		}
	}
	b.velocity_delta += inherited_velocity_offset * (real_t)delta;
	if (gravity_active && t.gravity_window && i < (int)all_gravity.size() && i < (int)all_gravity_velocity.size()) {
		const Vector2 base_g = all_gravity[i];
		if (base_g.length_squared() > 0.0) {
			const Vector2 g = base_g * gravity_strength_scale_for_bullet(t.shared_curves, b.curves);
			if (g.is_finite()) {
				Vector2 &gv = all_gravity_velocity[i];
				gv += g * (real_t)delta;
				b.velocity_delta += gv * (real_t)delta;
			}
		}
	}
	if (is_wobble_feature_enabled && i < (int)wobble_distance_traveled.size()) {
		wobble_distance_traveled[i] += b.velocity_delta.length();
	}
}

// 7. ORBIT (around the steering deque's front target). A deque that ran dry
// unlocks the orbit under RelockAlways / RelockOnTargetChange; StayLocked
// rides out the gap and re-pins silently when a target returns.
_ALWAYS_INLINE_ void BulletVolley2D::step_orbit(const MoveTick2D &t, BulletStep2D &b) {
	const int i = b.i;
	if (!t.orbiting || i >= (int)all_orbiting_data.size() || i >= (int)all_orbiting_status.size()) {
		return;
	}
	OrbitingData &o = all_orbiting_data[i];
	const bool has_target = b.steering_deque != nullptr && !b.steering_deque->empty();
	if (!has_target) {
		if (o.lock_policy != StayLocked) {
			o.is_locked_orbiting = false;
		}
		return;
	}
	if (!all_orbiting_status[i]) {
		return;
	}
	const double delta = t.delta;
	// RelockOnTargetChange watches identity, not distance: a new front
	// target unlocks so the bullet re-acquires the ring around it; the same
	// target moving keeps the lock.
	if (o.is_locked_orbiting && o.lock_policy == RelockOnTargetChange && orbit_should_unlock_for_front_change(o, *b.steering_deque)) {
		o.is_locked_orbiting = false;
	}
	// A non-finite front cache (a target freed between trim and tick) holds
	// the bullet still this tick instead of poisoning ballistics.
	if (!b.target_pos.is_finite()) {
		b.velocity_delta = Vector2(0, 0);
		return;
	}
	const Vector2 &origin = all_cached_instance_origin[i];
	// Ring center: FollowTarget tracks the target, FollowDeadzone pins until
	// it leaves the deadzone, Anchored freezes at the lock point. Unlocked
	// bullets fly toward the live target to reach the ring.
	const Vector2 center = o.is_locked_orbiting ? orbit_effective_center(o, b.target_pos) : b.target_pos;
	const Vector2 to_center = origin - center;
	const real_t current_dist = to_center.length();
	const bool already_locked = o.is_locked_orbiting;
	const real_t speed = all_cached_speed[i];
	// Zero-delta ticks and zero-speed bullets hold still: any snap would be
	// a teleport, not motion.
	const bool can_move = delta > 0.0 && speed > 0.0;
	auto ring_slot = [&]() { return center + Vector2(o.radius, 0).rotated(o.angle); };
	bool on_ring = false; // moving along the ring this tick (drives the facing)
	if (already_locked && o.direction == DontMove) {
		// Escort: a fixed ring slot (angle set at lock time) that translates
		// with the target; rigid follow holds it regardless of speed.
		if (o.rigid_follow || can_move) {
			const Vector2 snap = ring_slot() - origin;
			b.velocity_delta = o.rigid_follow ? snap : clamp_step(snap, speed * (real_t)delta);
		} else {
			b.velocity_delta = Vector2(0, 0);
		}
		on_ring = true;
	} else if (already_locked) {
		const real_t dir_multiplier = (o.direction == OrbitRight) ? 1.0 : (o.direction == OrbitLeft ? -1.0 : 0.0);
		if (dir_multiplier != 0.0) {
			// Tiny radii would divide into inf/NaN.
			const real_t safe_radius = o.radius < 0.01 ? (real_t)0.01 : o.radius;
			// Rigid follow moves the ring slot with the target, then
			// advances the angle (circling never lags a moving target);
			// without it the angle advances but the step is clamped to
			// speed * delta (cheap drift look).
			if (o.rigid_follow) {
				if (delta > 0.0) {
					o.angle += (speed / safe_radius) * dir_multiplier * (real_t)delta;
				}
				wrap_orbit_angle(o.angle);
				b.velocity_delta = ring_slot() - origin;
			} else if (can_move) {
				const real_t angular_speed = (speed / safe_radius) * dir_multiplier;
				o.angle += angular_speed * delta;
				wrap_orbit_angle(o.angle);
				b.velocity_delta = clamp_step(ring_slot() - origin, speed * (real_t)delta);
			} else {
				b.velocity_delta = Vector2(0, 0);
			}
		} else {
			b.velocity_delta = ring_slot() - origin;
		}
		on_ring = true;
	} else if (delta > 0.0 && Math::abs(current_dist - o.radius) < Math::max((real_t)(speed * delta), (real_t)2.0)) {
		// Reached the ring this tick (epsilon so slow / high-FPS bullets
		// still lock): lock and snap toward the slot. DontMove escorts lock
		// here too; zero-delta ticks never lock.
		o.angle = to_center.angle();
		orbit_stamp_lock(o, center, *b.steering_deque);
		b.velocity_delta = clamp_step(ring_slot() - origin, speed * (real_t)delta);
		on_ring = true;
	} else if (current_dist < o.radius) {
		// Spawned inside the ring: push outward (not orbiting yet).
		const Vector2 outward_dir = (current_dist > 0.1f) ? (to_center / current_dist) : Vector2(1, 0);
		const real_t next_dist = current_dist + (speed * (real_t)delta);
		b.velocity_delta = (center + (outward_dir * next_dist)) - origin;
	}
	// Facing while on the ring (orbiting owns its texture rotation, no
	// homing flag needed). DontMove escorts only face the target: they have
	// no direction of travel. A bullet exactly on the center keeps its yaw.
	if (!on_ring || (o.direction == DontMove && (o.texture_rotation == FaceOrbitingDirection || o.texture_rotation == FaceOppositeOrbitingDirection))) {
		return;
	}
	if ((origin - center).length_squared() < 0.000001) {
		return;
	}
	const Vector2 radial = (origin - center).normalized();
	Vector2 look_dir;
	switch (o.texture_rotation) {
		case FaceTarget:
			look_dir = -radial;
			break;
		case FaceOppositeTarget:
			look_dir = radial;
			break;
		case FaceOrbitingDirection:
			look_dir = (o.direction == OrbitRight) ? Vector2(-radial.y, radial.x) : Vector2(radial.y, -radial.x);
			break;
		case FaceOppositeOrbitingDirection:
			look_dir = (o.direction == OrbitRight) ? Vector2(radial.y, -radial.x) : Vector2(-radial.y, radial.x);
			break;
		default:
			break;
	}
	if (look_dir != Vector2()) {
		rotate_to_target_preserve_interpolation(i, look_dir, false);
	}
}

// 8. WRITE. Move the instance, derive the physics shape (logical rotation:
// the texture-only rotation stripped, frozen while rotate_only_textures),
// offset it, push it to the physics server, carry the attachment and the
// trail, then test the homing reach at the post-move position.
_ALWAYS_INLINE_ void BulletVolley2D::step_place(const MoveTick2D &t, BulletStep2D &b) {
	const int i = b.i;
	Vector2 &origin = all_cached_instance_origin[i];
	origin += b.velocity_delta;
	b.transf->set_origin(origin);

	Transform2D &shape = all_cached_shape_transforms[i];
	if (!rotate_only_textures) {
		shape = *b.transf;
		if (t.strip_texture_rotation) {
			// Same as rotated_local(-texture_rotation): basis * R.
			const Vector2 c0 = shape.columns[0];
			const Vector2 c1 = shape.columns[1];
			shape.columns[0] = c0 * t.strip_cos + c1 * t.strip_sin;
			shape.columns[1] = c1 * t.strip_cos - c0 * t.strip_sin;
		}
	}
	Vector2 rotated_offset = Vector2(0, 0);
	if (t.has_shape_offset) {
		// Rotate by the shape's facing (column 0) without atan2 + sin/cos.
		const Vector2 facing = shape.columns[0];
		const real_t facing_len = facing.length();
		if (facing_len > (real_t)0.0) {
			const Vector2 f = facing / facing_len;
			rotated_offset = Vector2(f.x * cache_collision_shape_offset.x - f.y * cache_collision_shape_offset.y, f.y * cache_collision_shape_offset.x + f.x * cache_collision_shape_offset.y);
		} else {
			rotated_offset = cache_collision_shape_offset;
		}
	}
	shape.set_origin(origin + rotated_offset);

	physics_server->area_set_shape_transform(area, i, shape);
	move_bullet_attachment(b.velocity_delta, i);
	write_trail_instances(i);

	// 7b. REACHED: tested at the post-move position (predicting again with
	// velocity_delta would test two ticks ahead); the reach belongs to the
	// deque the bullet steered by.
	if (b.steering_deque != nullptr && !b.steering_deque->empty() && b.target_pos.is_finite()) {
		try_to_emit_bullet_homing_target_reached_signal(*b.steering_deque, b.steering_deque == &shared_homing_deque, i, origin, b.target_pos, Vector2(0, 0));
	}
}

// 9. SPEED for the next tick. Own speed curve > shared curve > plain
// acceleration. Curves rewrite the speed every tick, so the cumulative
// bounce boost is re-applied on curve-driven ticks; linear drag then decays
// the total. Disabled bullets freeze at their disable-time speed (a wake
// resumes where that bullet left off).
_ALWAYS_INLINE_ void BulletVolley2D::step_speed(const MoveTick2D &t, BulletStep2D &b) {
	const int i = b.i;
	const double delta = t.delta;
	const bool curve_drives_speed = (b.curves != nullptr && b.curves->movement_speed_curve.is_valid()) || t.shared_speed;
	if (b.curves != nullptr && b.curves->movement_speed_curve.is_valid()) {
		bullet_accelerate_speed_using_curve(i, delta, b.curves);
	} else if (t.shared_speed) {
		all_cached_speed[i] = t.shared_speed_value;
		refresh_cached_velocity(i);
	} else {
		bullet_accelerate_speed(i, delta);
	}
	if (curve_drives_speed && bounce_speed_scaled && i < (int)all_bounce_speed_multiplier.size()) {
		const real_t bmult = all_bounce_speed_multiplier[i];
		if (Math::is_finite((double)bmult) && bmult != (real_t)1.0) {
			all_cached_speed[i] *= bmult;
			refresh_cached_velocity(i);
		}
	}
	if (linear_drag > 0.0 && Math::is_finite(linear_drag)) {
		const real_t keep = Math::max((real_t)0.0, (real_t)1.0 - linear_drag * (real_t)delta);
		all_cached_speed[i] *= keep;
		refresh_cached_velocity(i);
	} else if (gravity_active) {
		// Any gravity (shared or per-bullet) keeps its fall speed in the
		// reported velocity; the plain accel path above omits it.
		refresh_cached_velocity(i);
	}
}

_ALWAYS_INLINE_ void BulletVolley2D::refresh_cached_velocity(int bullet_index) {
	all_cached_velocity[bullet_index] = all_cached_direction[bullet_index] * all_cached_speed[bullet_index] + inherited_velocity_offset + ((bullet_index < (int)all_gravity_velocity.size()) ? all_gravity_velocity[bullet_index] : Vector2(0, 0));
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

// The heading a homing bullet must fly so that its GROUND velocity (own
// heading x speed + drift) points along diff: the drift component across the
// line of sight is cancelled, the rest of the speed closes in (the crab angle
// of a missile in a crosswind). A bullet too slow to cancel the drift leans
// fully against it. No drift, or no own speed: diff itself.
static _ALWAYS_INLINE_ Vector2 homing_aim_through_drift(const Vector2 &diff, const Vector2 &drift, real_t speed) {
	if (!(speed > 0.0) || (drift.x == 0.0 && drift.y == 0.0)) {
		return diff;
	}
	const real_t dist = diff.length();
	if (!(dist > 0.0)) {
		return diff;
	}
	const Vector2 los = diff / dist;
	const Vector2 own_across = (los * drift.dot(los) - drift) / speed;
	const real_t across_sq = own_across.length_squared();
	if (!Math::is_finite(across_sq)) {
		return diff;
	}
	if (across_sq >= 1.0) {
		return own_across;
	}
	return los * Math::sqrt(1.0 - across_sq) + own_across;
}

_ALWAYS_INLINE_ void BulletVolley2D::update_homing(HomingTargetDeque &homing_deque, int bullet_index, double delta, Vector2 &bullet_pos, Vector2 &target_pos) {
	if (bullet_index < 0 || bullet_index >= (int)all_cached_instance_origin.size() || bullet_index >= (int)all_cached_direction.size() || bullet_index >= (int)all_cached_instance_transforms.size()) {
		return;
	}
	// The steering deque's front is this bullet's homing (and orbit) target
	// on EVERY tick: the orbit section and the reached test read target_pos
	// right after this call, so a zero-delta tick must not leave it holding
	// the previous bullet's target.
	target_pos = homing_deque.get_cached_front_target_global_position();
	if (!Math::is_finite(delta) || delta <= 0.0) {
		return;
	}
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
	// Aim through the inherited drift (the spawner's momentum rides on the
	// bullet for its whole life): steering the own heading straight at the
	// target let the drift carry the bullet past it.
	const real_t own_speed = bullet_index < (int)all_cached_speed.size() ? all_cached_speed[bullet_index] : (real_t)0.0;
	const Vector2 aim = homing_aim_through_drift(diff, inherited_velocity_offset, own_speed);
	// If rotation is controlled via movement pattern (per-bullet or shared)
	// or rotation data, just set direction directly toward target
	if (check_exists_bullet_movement_pattern_data(bullet_index) || shared_movement_pattern_curve.is_valid() || is_rotation_data_active) {
		current_direction = aim.normalized();
	} else { // Otherwise use smoothing to rotate toward target
		// Rotate toward target with smoothing
		rotate_to_target(bullet_index, aim, max_turn);

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

		// Ensure that the signal is emitted only ONCE per bullet per target.
		// No user code may run inside the move loop: record the event and
		// let dispatch_homing_events() emit it right after the loop.
		if (fire_for_this_bullet) {
			HomingReachedEvent ev;
			ev.bullet_index = bullet_index;
			ev.epoch = collision_epoch_for_bullet(bullet_index);
			ev.target_position = target_pos;
			// A freed Node2D target still emits, with a null target.
			if (target.type == Node2DTarget && homing_deque.is_homing_target_valid(target.node2d_target_data.target, target.node2d_target_data.cached_valid_instance_id)) {
				ev.target_instance_id = target.node2d_target_data.cached_valid_instance_id;
			}
			ev.front_identity = orbit_target_identity(homing_deque);
			if (is_using_shared_homing_deque) {
				if (shared_homing_deque_auto_pop_after_target_reached && !shared_pop_requested) {
					shared_pop_requested = true;
					shared_pop_identity = ev.front_identity;
				}
			} else {
				ev.auto_pop = bullet_homing_auto_pop_after_target_reached;
			}
			homing_reached_events.push_back(ev);
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
