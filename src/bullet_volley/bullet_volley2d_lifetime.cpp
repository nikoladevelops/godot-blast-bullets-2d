// Lifetime: the countdown, expiry (reduce_lifetime), the deferred life_time_over
// signal and the lifetime hold that keeps an expired volley out of the pool until
// its deferred work flushed.

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletVolley2D::set_is_life_time_infinite(bool value) {
	if (is_life_time_infinite == value) {
		return;
	}
	// A finite lifetime needs a positive max_life_time: with 0 the multimesh dies
	// on the first tick and unit curves divide 0/0 = NaN (see curve_get_input_value).
	if (!value && !(max_life_time > 0.0)) {
		UtilityFunctions::push_error("set_is_life_time_infinite(false) requires a max_life_time > 0, but this multimesh has none (it was spawned with an infinite lifetime). Keep the lifetime infinite or spawn with a finite max_life_time.");
		return;
	}
	is_life_time_infinite = value;
	curves_elapsed_time = 0.0;
	if (!value) {
		current_life_time = max_life_time;
	}
}

void BulletVolley2D::set_up_life_time_timer(double new_max_life_time, double new_current_life_time) {
	max_life_time = new_max_life_time;
	current_life_time = new_current_life_time;
}

double BulletVolley2D::get_life_time_left() const {
	if (is_life_time_infinite) {
		return 0.0;
	}
	return current_life_time > 0.0 ? current_life_time : 0.0;
}

void BulletVolley2D::set_life_time_left(double seconds) {
	if (is_life_time_infinite) {
		UtilityFunctions::push_error("set_life_time_left: this volley has an infinite lifetime (set_is_life_time_infinite(false) first).");
		return;
	}
	if (!Math::is_finite(seconds) || seconds <= 0.0) {
		UtilityFunctions::push_error("set_life_time_left: seconds must be finite and > 0, keeping the old value.");
		return;
	}
	// Only the remaining time moves: the curve clock keeps running (unit
	// curves keep sampling elapsed / max_life_time, clamped at the end).
	current_life_time = seconds;
}

// Cold paths live here; per-tick hot paths stay inline in the header.

void BulletVolley2D::reduce_lifetime(double delta) {
	if (!Math::is_finite(delta) || delta < 0.0) {
		return;
	}
	curves_elapsed_time += delta;
	tick_volley_fade();

	// If the lifetime is infinite there is no lifetime timer
	if (is_life_time_infinite) {
		return;
	}

	current_life_time -= delta;
	if (current_life_time > 0) {
		return;
	}
	expire_live_bullets();
}

void BulletVolley2D::expire_live_bullets() {
	// Snapshot first: the handler and every disable below mutate the live set.
	expiry_indexes_scratch = all_bullets_enabled_set.get_active_indexes();
	const int expiring_count = (int)expiry_indexes_scratch.size();
	if (expiring_count <= 0) {
		return;
	}
	if (bullet_factory != nullptr) {
		bullet_factory->stats_expired_bullets_total += (uint64_t)expiring_count;
	}
	const uint64_t self_id = get_instance_id();
	if (is_life_time_over_signal_enabled) {
		// Epoch snapshot: a bullet the handler disabled (and maybe woke)
		// belongs to the handler afterwards; only untouched bullets expire.
		expiry_epochs_scratch.resize(expiring_count);
		TypedArray<int> bullet_indexes;
		bullet_indexes.resize(expiring_count);
		for (int k = 0; k < expiring_count; ++k) {
			const int i = expiry_indexes_scratch[k];
			expiry_epochs_scratch[k] = collision_epoch_for_bullet(i);
			bullet_indexes[k] = i;
		}
		// LIVE emit (synchronous, inside the factory tick): every listed
		// bullet is still alive with its attachment, custom data and pose.
		// Factory and spawner share the signal name. A null emitter (spawner
		// gone, teardown) only skips the notification.
		Object *emitter = resolve_lifetime_emitter_checked();
		if (emitter != nullptr) {
			emitter->emit_signal(CachedStringNames2D::get().life_time_over, this, bullet_indexes);
		}
		if (ObjectDB::get_instance(ObjectID(self_id)) != this || is_queued_for_deletion()) {
			return;
		}
		// Volley-wide veto: the handler made the lifetime infinite or gave
		// it more time (set_life_time_left). Nothing expires this tick.
		if (is_life_time_infinite || current_life_time > 0.0) {
			return;
		}
	}
	// Batched expiry (On Lifetime Over at each pose, one render upload when
	// the whole volley expires). Bullets the handler disabled/woke (epoch
	// moved) are skipped.
	disable_bullets_bulk(expiry_indexes_scratch, EFFECT_ON_LIFETIME_OVER, is_life_time_over_signal_enabled ? &expiry_epochs_scratch : nullptr);
}

} // namespace BlastBullets2D
