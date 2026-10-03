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

void BulletVolley2D::_do_emit_life_time_over(int expected_generation, uint64_t emitter_instance_id, const StringName &signal_name, const TypedArray<int> &bullet_indexes) {
	if (expected_generation != volley_generation) {
		return;
	}
	if (bullet_indexes.is_empty()) {
		return;
	}
	Object *emitter = ObjectDB::get_instance(ObjectID(emitter_instance_id));
	if (emitter == nullptr) {
		return;
	}
	// Re-ownership guard: the volley may have been handed to a different
	// owner without a generation bump (adopt_live_volley re-stamps only).
	// Never deliver the old life's expiry to the new owner, and never
	// deliver a spawner life's expiry to the factory. A cleared tag
	// (owner == 0 after the expiry pooled the instance) still belongs to
	// the captured emitter, so only a *different live owner* drops here.
	if (owner_spawner_id != 0 && owner_spawner_id != emitter_instance_id) {
		return;
	}
	emitter->emit_signal(signal_name, this, bullet_indexes);
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

		// Life time timer logic
		current_life_time -= delta;

		// The bullets still have life time left, so don't do anything yet
		if (current_life_time > 0) {
			return;
		}

		std::vector<int> active_copy = all_bullets_enabled_set.get_active_indexes();
		if (bullet_factory != nullptr) {
			bullet_factory->stats_expired_bullets_total += (uint64_t)active_copy.size();
		}

		// If the life_time_over signal is not enabled, we can just disable all bullets right away and skip the additional logic
		if (!is_life_time_over_signal_enabled) {
			for (int i : active_copy) {
				if (!all_bullets_enabled_set.contains(i)) {
					continue;
				}
				// Lifetime expiry visuals fire here (not on collision kills):
				// capture the pose first, the disable below never moves it.
				Transform2D fx_expire_transf;
				bool fx_have_pose = i >= 0 && i < (int)all_cached_instance_transforms.size();
				if (fx_have_pose) {
					fx_expire_transf = all_cached_instance_transforms[i];
				}
				disable_bullet(i, true);
				if (fx_have_pose) {
					fx_fire_oneshot(EFFECT_ON_LIFETIME_OVER, i, fx_expire_transf);
				}
			}

			return;
		}

		// If the life_time_over signal is enabled - collect indexes, disable bullets immediately (consistent with collision path),
		// but keep attachment disable and signal deferred so handler can still access attachment.
		// Full-volley expiry additionally detaches the survivors from the disable
		// sweep: the last disable_bullet() funnels into disable_volley(),
		// whose sweep would otherwise pool every attachment BEFORE the deferred
		// signal fires (handler would see nullptr). Detaching first keeps the
		// slots alive for the signal; the deferred disables re-pool them after.
		TypedArray<int> bullet_indexes;

		// Hold the volley out of the pool (and keep its attachment slots)
		// until the deferred signal flushes: the last disable below funnels
		// into disable_volley(), which would otherwise pool the volley at
		// once (a same-frame spawn then pops it and the generation bump drops
		// the signal) and release every attachment before the handler runs.
		lifetime_flush_pending = true;

		// Snapshot the signal owner BEFORE the disable loop below: a full-volley
		// expiry funnels into disable_volley(), which clears owner_spawner_id
		// and pools the instance. Resolving after would schedule the factory's
		// life_time_over signal for a spawner-owned volley.
		Object *lifetime_emitter = resolve_signal_emitter();

		// Caches already hold global-space transforms (spawn data is global and all
		// movement/homing math is global), so no get_global_transform() compose is
		// needed anywhere transforms are read (handlers use get_bullet_global_transform()).
		for (int i : active_copy) {
			if (!all_bullets_enabled_set.contains(i)) {
				continue;
			}
			bullet_indexes.push_back(i);
			Transform2D fx_expire_transf;
			const bool fx_have_pose = i >= 0 && i < (int)all_cached_instance_transforms.size();
			if (fx_have_pose) {
				fx_expire_transf = all_cached_instance_transforms[i];
			}
			disable_bullet(i, false); // immediate shape disable, keep attachment for signal
			if (fx_have_pose) {
				fx_fire_oneshot(EFFECT_ON_LIFETIME_OVER, i, fx_expire_transf);
			}
		}
		// Never drained (a callback woke a bullet, or nothing was live): no
		// hold applies, the volley lives on normally.
		if (is_active) {
			lifetime_flush_pending = false;
		}

	if (bullet_indexes.size() > 0) {
		// Emit deferred so user code runs outside physics step. Guarded by
		// spawn generation (not just emitter validity): expiry queues the
		// emit, then a same-frame pool reuse hands this instance to a new
		// owner before the flush. The bare call_deferred("emit_signal")
		// would then deliver the OLD life's indexes to the NEW life.
		// Uses the pre-disable snapshot above, never a post-disable resolve.
		Object *emitter = lifetime_emitter;
		if (emitter != nullptr) {
			// Factory and spawner share the signal name (life_time_over).
			const uint64_t emitter_id = emitter->get_instance_id();
			call_deferred(CachedStringNames2D::get().m_do_emit_life_time_over, volley_generation, emitter_id, CachedStringNames2D::get().life_time_over, bullet_indexes);
		}

		// Disable attachments after signal (deferred keeps order). The per-slot
		// assignment epoch travels with the request so a same-life ABA reuse
		// (pool returns the same node to the same slot) cannot match stale.
		// Only slots that actually hold an attachment are queued (one batched
		// call carrying ids + epochs, never pointers - see
		// _do_deferred_bullet_disable_attachments).
		PackedInt64Array attachment_requests;
		for (int i = 0; i < bullet_indexes.size(); ++i) {
			const int idx = bullet_indexes[i];
			BulletAttachment2D *queued_attachment = (idx >= 0 && idx < (int)attachments.size()) ? attachments[idx] : nullptr;
			if (queued_attachment == nullptr) {
				continue;
			}
			attachment_requests.push_back(idx);
			attachment_requests.push_back((int64_t)queued_attachment->get_instance_id());
			attachment_requests.push_back((int64_t)attachment_epoch_for(idx));
		}
		if (!attachment_requests.is_empty()) {
			call_deferred(CachedStringNames2D::get().m_do_deferred_bullet_disable_attachments, volley_generation, attachment_requests);
		}
	}
	// Queued last so it flushes after the signal and the slot releases.
	if (lifetime_flush_pending) {
		call_deferred(CachedStringNames2D::get().m_do_finish_lifetime_hold, volley_generation);
	}
	}

void BulletVolley2D::_do_finish_lifetime_hold(int expected_generation) {
	// A new life (wake/enable) already ended the hold and owns the volley.
	if (expected_generation != volley_generation || !lifetime_flush_pending) {
		return;
	}
	if (is_active || is_queued_for_deletion()) {
		lifetime_flush_pending = false;
		return;
	}
	// Same busy window as disable_volley(): the release below runs user
	// callbacks (on_bullet_disable), which must not reset/free the factory
	// under us.
	const bool saved_factory_busy = bullet_factory != nullptr ? bullet_factory->get_is_factory_busy() : false;
	if (bullet_factory != nullptr) {
		bullet_factory->_set_internal_operation_busy(true);
	}
	const uint64_t self_id = get_instance_id();
	release_lifetime_hold_attachments();
	if (bullet_factory != nullptr) {
		bullet_factory->_set_internal_operation_busy(saved_factory_busy);
	}
	if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
		return;
	}
	// The handler may have woken a bullet (new life) or opted out of pooling.
	if (is_active || active_bullets_counter > 0 || !is_auto_pooling_enabled) {
		return;
	}
	// Slots are released: blank them like a normal disable would have.
	reset_attachment_state_for_reuse();
	if (bullets_pool != nullptr) {
		bullets_pool->push(this, get_pool_key());
	}
}

} // namespace BlastBullets2D
