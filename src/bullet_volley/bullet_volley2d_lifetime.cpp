// Lifetime: the countdown, expiry (reduce_lifetime), the deferred life_time_over
// signal and the lifetime hold that keeps an expired volley out of the pool until
// its deferred work flushed.

#include "bullet_volley/bullet_volley2d_internal.hpp"
#include "core/emit_on2d.hpp"

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

// ---- Per-bullet lifetimes ------------------------------------------------------------

void BulletVolley2D::reset_bullet_life_times() {
	life_age = 0.0;
	own_life_span.clear();
	own_life_deadline.clear();
	next_own_deadline = Math::INF;
	own_life_count = 0;
}

void BulletVolley2D::give_bullet_own_life(int bullet_index, double span, double remaining) {
	if (own_life_span.empty()) {
		own_life_span.assign((size_t)amount_bullets, -1.0);
		own_life_deadline.assign((size_t)amount_bullets, 0.0);
	}
	if (own_life_span[bullet_index] < 0.0) {
		++own_life_count;
	}
	own_life_span[bullet_index] = span;
	own_life_deadline[bullet_index] = life_age + remaining;
	next_own_deadline = Math::min(next_own_deadline, own_life_deadline[bullet_index]);
}

void BulletVolley2D::seed_bullet_life_times(const BulletVolleyData2D &data) {
	const PackedFloat64Array &times = data.all_bullet_life_times;
	const int size = (int)times.size();
	if (size == 0) {
		return;
	}
	const bool tile = data.tile_all_bullet_life_times;
	if (size != amount_bullets) {
		WarnOnce2D::warn(warn_data_id, 13u, size, amount_bullets, "BulletVolley2D: all_bullet_life_times size (" + String::num_int64(size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets use max_life_time" + String(tile ? " (tiling on: wrapping short array)." : " (check tile_all_bullet_life_times to wrap, or provide one entry per bullet)."));
	}
	const int covered = tile ? amount_bullets : Math::min(size, amount_bullets);
	for (int i = 0; i < covered; ++i) {
		const double span = times[i % size];
		if (Math::is_finite(span) && span > 0.0) { // the data setter guarantees it
			give_bullet_own_life(i, span, span);
		}
	}
}

void BulletVolley2D::own_life_on_wake(int bullet_index) {
	if (own_life_span.empty() || own_life_span[bullet_index] < 0.0) {
		return;
	}
	// Woken after its lifetime ran out: a fresh one (the volley timer's rule
	// for an expired parked volley). A manual freeze keeps the time left.
	if (own_life_deadline[bullet_index] <= life_age) {
		own_life_deadline[bullet_index] = life_age + own_life_span[bullet_index];
	}
	next_own_deadline = Math::min(next_own_deadline, own_life_deadline[bullet_index]);
}

double BulletVolley2D::get_bullet_life_time_left(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_life_time_left")) {
		return 0.0;
	}
	if (own_life_span.empty() || own_life_span[bullet_index] < 0.0) {
		return get_life_time_left(); // the volley timer (0 while infinite)
	}
	return Math::max(own_life_deadline[bullet_index] - life_age, 0.0);
}

void BulletVolley2D::set_bullet_life_time_left(int bullet_index, double seconds) {
	if (!validate_bullet_index(bullet_index, "set_bullet_life_time_left")) {
		return;
	}
	if (!Math::is_finite(seconds) || !(seconds > 0.0)) {
		UtilityFunctions::push_error("set_bullet_life_time_left: seconds must be finite and > 0, keeping the old value.");
		return;
	}
	// A bullet that had no lifetime of its own takes this one for later
	// wakes too; one that had keeps its span (only the time left moves).
	const bool has_own = !own_life_span.empty() && own_life_span[bullet_index] >= 0.0;
	give_bullet_own_life(bullet_index, has_own ? own_life_span[bullet_index] : seconds, seconds);
}

void BulletVolley2D::all_bullets_scale_life_time(double factor, int bullet_index_start, int bullet_index_end_inclusive) {
	if (reject_pooled_handle("all_bullets_scale_life_time")) {
		return;
	}
	if (!ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_scale_life_time")) {
		return;
	}
	if (bullet_index_start > bullet_index_end_inclusive) {
		return;
	}
	if (!Math::is_finite(factor) || !(factor > 0.0)) {
		UtilityFunctions::push_error("all_bullets_scale_life_time: factor must be finite and > 0, keeping the old values.");
		return;
	}
	int overflowed = -1;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		const bool has_own = !own_life_span.empty() && own_life_span[i] >= 0.0;
		if (!has_own && is_life_time_infinite) {
			continue; // no time to scale: it stays infinite
		}
		const double left = get_bullet_life_time_left(i);
		const double scaled = left * factor;
		if (!(left > 0.0)) {
			continue; // already due this tick
		}
		if (!Math::is_finite(scaled) || !(scaled > 0.0)) {
			if (overflowed < 0) {
				overflowed = i;
			}
			continue;
		}
		// The bullet keeps its own span (or takes the scaled time as its own).
		give_bullet_own_life(i, has_own ? own_life_span[i] : scaled, scaled);
	}
	if (overflowed >= 0) {
		UtilityFunctions::push_error("all_bullets_scale_life_time: the scaled lifetime of bullet " + String::num_int64(overflowed) + " (and any later one) is not a finite time, keeping its old value.");
	}
}

PackedFloat64Array BulletVolley2D::all_bullets_get_life_time_left(int bullet_index_start, int bullet_index_end_inclusive) const {
	return collect_range<PackedFloat64Array>(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_life_time_left", [&](int i) { return get_bullet_life_time_left(i); });
}

void BulletVolley2D::all_bullets_set_life_time_left(double seconds, int bullet_index_start, int bullet_index_end_inclusive) {
	if (!ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_life_time_left")) {
		return;
	}
	if (bullet_index_start > bullet_index_end_inclusive) {
		return;
	}
	if (!Math::is_finite(seconds) || !(seconds > 0.0)) {
		UtilityFunctions::push_error("all_bullets_set_life_time_left: seconds must be finite and > 0, keeping the old values.");
		return;
	}
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_life_time_left(i, seconds);
	}
}

void BulletVolley2D::apply_life_time_factors(const real_t *factors, int count) {
	const int n = Math::min(count, amount_bullets);
	for (int i = 0; i < n; ++i) {
		const bool has_own = !own_life_span.empty() && own_life_span[i] >= 0.0;
		const double base = has_own ? own_life_span[i] : (is_life_time_infinite ? -1.0 : max_life_time);
		const double span = base * (double)factors[i];
		if (!(base > 0.0) || !Math::is_finite(span) || !(span > 0.0)) {
			continue; // an endless bullet stays endless; an overflow keeps its lifetime
		}
		give_bullet_own_life(i, span, span);
	}
}

void BulletVolley2D::expire_own_life_bullets() {
	own_expiry_scratch.clear();
	double next = Math::INF;
	for (int i : all_bullets_enabled_set.get_active_indexes()) {
		if (i < 0 || i >= (int)own_life_span.size() || own_life_span[i] < 0.0) {
			continue;
		}
		const double due = own_life_deadline[i];
		if (due <= life_age) {
			own_expiry_scratch.push_back(i);
		} else if (due < next) {
			next = due;
		}
	}
	next_own_deadline = next;
	const int expiring_count = (int)own_expiry_scratch.size();
	if (expiring_count == 0) {
		return;
	}
	if (bullet_factory != nullptr) {
		bullet_factory->stats_expired_bullets_total += (uint64_t)expiring_count;
	}
	const std::vector<uint64_t> *epochs = nullptr;
	if (is_life_time_over_signal_enabled) {
		const uint64_t self_id = get_instance_id();
		own_expiry_epochs_scratch.resize(expiring_count);
		TypedArray<int> bullet_indexes;
		bullet_indexes.resize(expiring_count);
		for (int k = 0; k < expiring_count; ++k) {
			const int i = own_expiry_scratch[k];
			own_expiry_epochs_scratch[k] = collision_epoch_for_bullet(i);
			bullet_indexes[k] = i;
		}
		// LIVE emit, same contract as the volley expiry: every listed bullet
		// is alive in the handler.
		Object *emitter = resolve_lifetime_emitter_checked();
		if (emitter != nullptr) {
			note_user_code();
			emit_on2d(emitter, CachedStringNames2D::get().life_time_over, this, bullet_indexes);
		}
		if (ObjectDB::get_instance(ObjectID(self_id)) != this || is_queued_for_deletion()) {
			return;
		}
		// Per-bullet veto: a bullet the handler gave more time keeps flying
		// (set_bullet_life_time_left moved its deadline past now).
		int kept = 0;
		for (int k = 0; k < expiring_count; ++k) {
			const int i = own_expiry_scratch[k];
			const bool still_due = i < (int)own_life_span.size() && own_life_span[i] >= 0.0 && own_life_deadline[i] <= life_age;
			if (still_due) {
				own_expiry_scratch[kept] = i;
				own_expiry_epochs_scratch[kept] = own_expiry_epochs_scratch[k];
				++kept;
			}
		}
		own_expiry_scratch.resize(kept);
		own_expiry_epochs_scratch.resize(kept);
		epochs = &own_expiry_epochs_scratch;
	}
	if (!own_expiry_scratch.empty()) {
		disable_bullets_bulk(own_expiry_scratch, EFFECT_ON_LIFETIME_OVER, epochs, SPLIT_CAUSE_LIFETIME_OVER, false, "lifetime");
	}
}

// Cold paths live here; per-tick hot paths stay inline in the header.

void BulletVolley2D::reduce_lifetime(double delta) {
	if (!Math::is_finite(delta) || delta < 0.0) {
		return;
	}
	curves_elapsed_time += delta;
	life_age += delta;
	tick_volley_fade();

	// Bullets with their own lifetime: one compare until the earliest is due.
	if (life_age >= next_own_deadline) {
		const uint64_t self_id = get_instance_id();
		expire_own_life_bullets();
		if (ObjectDB::get_instance(ObjectID(self_id)) != this || is_queued_for_deletion() || !is_active) {
			return;
		}
	}

	// If the lifetime is infinite there is no lifetime timer
	if (is_life_time_infinite) {
		return;
	}

	current_life_time -= delta;
	// Every bullet on its own lifetime: the volley timer expires nobody.
	if (current_life_time > 0 || own_life_count >= amount_bullets) {
		return;
	}
	expire_live_bullets();
}

void BulletVolley2D::expire_live_bullets() {
	// Snapshot first: the handler and every disable below mutate the live set.
	expiry_indexes_scratch = all_bullets_enabled_set.get_active_indexes();
	// Bullets on their own lifetime ignore the volley timer.
	auto drop_own_life_bullets = [this](std::vector<uint64_t> *parallel) {
		if (own_life_count == 0) {
			return;
		}
		size_t kept = 0;
		for (size_t k = 0; k < expiry_indexes_scratch.size(); ++k) {
			const int i = expiry_indexes_scratch[k];
			if (i >= 0 && i < (int)own_life_span.size() && own_life_span[i] >= 0.0) {
				continue;
			}
			expiry_indexes_scratch[kept] = i;
			if (parallel != nullptr) {
				(*parallel)[kept] = (*parallel)[k];
			}
			++kept;
		}
		expiry_indexes_scratch.resize(kept);
		if (parallel != nullptr) {
			parallel->resize(kept);
		}
	};
	drop_own_life_bullets(nullptr);
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
			note_user_code();
			emit_on2d(emitter, CachedStringNames2D::get().life_time_over, this, bullet_indexes);
		}
		if (ObjectDB::get_instance(ObjectID(self_id)) != this || is_queued_for_deletion()) {
			return;
		}
		// Volley-wide veto: the handler made the lifetime infinite or gave
		// it more time (set_life_time_left). Nothing expires this tick.
		if (is_life_time_infinite || current_life_time > 0.0) {
			return;
		}
		// A bullet the handler gave its own lifetime keeps flying.
		drop_own_life_bullets(&expiry_epochs_scratch);
	}
	// Batched expiry (On Lifetime Over at each pose, one render upload when
	// the whole volley expires). Bullets the handler disabled/woke (epoch
	// moved) are skipped.
	disable_bullets_bulk(expiry_indexes_scratch, EFFECT_ON_LIFETIME_OVER, is_life_time_over_signal_enabled ? &expiry_epochs_scratch : nullptr, SPLIT_CAUSE_LIFETIME_OVER, false, "lifetime");
}

} // namespace BlastBullets2D
