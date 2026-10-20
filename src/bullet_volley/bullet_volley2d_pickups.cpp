// Pickups on the volley side: the burst-out / magnet phases, collection when
// a pickup reaches its target, and the volley-scoped conversion entry. The
// conversion engine itself (all scopes, one new volley per call) lives in
// factory/bullet_factory2d_pickups.cpp.

#include "bullet_volley/bullet_volley2d_internal.hpp"
#include "core/emit_on2d.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletVolley2D::seed_pickups(const Ref<BulletPickupData2D> &pickup, const PickupSeed *seeds, int count) {
	pickup_data = pickup;
	const int mode = pickup->motion_mode;
	// Only the built-in coin physics has a magnet phase; the other modes steer
	// from the start of the pull (homing delay) with the speed they were given.
	pickup_magnet_started = mode != BulletPickupData2D::MOTION_BURST_AND_MAGNET;
	const int limit = Math::min(count, Math::min(amount_bullets, (int)all_cached_speed.size()));
	for (int i = 0; i < limit; ++i) {
		const Vector2 v = seeds[i].velocity;
		const real_t speed = Math::sqrt(v.x * v.x + v.y * v.y);
		if (!Math::is_finite(speed)) {
			continue; // keeps the spawn-time motion
		}
		// The heading is always the source's; the speed depends on the mode.
		if (speed > 0.0001f) {
			all_cached_direction[i] = Vector2(v.x / speed, v.y / speed);
		}
		if (mode == BulletPickupData2D::MOTION_BURST_AND_MAGNET) {
			all_cached_speed[i] = speed;
			// Burst phase: no growth until the magnet starts.
			all_cached_max_speed[i] = speed;
			all_cached_acceleration[i] = 0.0f;
		} else if (mode == BulletPickupData2D::MOTION_KEEP_BULLET_MOTION) {
			all_cached_speed[i] = speed;
			// A top speed of 0 means no cap: keep it unlimited.
			all_cached_max_speed[i] = seeds[i].max_speed > 0.0f ? Math::max(seeds[i].max_speed, speed) : 0.0f;
			all_cached_acceleration[i] = seeds[i].acceleration;
		}
		// MOTION_CUSTOM: speed, top speed and acceleration stay what the
		// developer's speed_data (or the volley's defaults) seeded.
		refresh_cached_velocity(i);
	}
	if (mode == BulletPickupData2D::MOTION_BURST_AND_MAGNET && pickup->magnet_delay_sec <= 0.0) {
		maybe_begin_pickup_magnet();
	}
}

void BulletVolley2D::collect_bullet_epochs(const std::vector<int> &indexes, std::vector<uint64_t> &r_epochs) const {
	r_epochs.clear();
	r_epochs.reserve(indexes.size());
	for (const int i : indexes) {
		r_epochs.push_back(collision_epoch_for_bullet(i));
	}
}

void BulletVolley2D::maybe_begin_pickup_magnet() {
	if (pickup_data.is_null() || pickup_magnet_started || curves_elapsed_time < pickup_data->magnet_delay_sec) {
		return;
	}
	pickup_magnet_started = true;
	linear_drag = 0.0f; // the burst is over; the pool re-seeds this from the data
	const real_t acceleration = (real_t)pickup_data->magnet_acceleration;
	const real_t top_speed = (real_t)pickup_data->magnet_max_speed;
	for (const int i : all_bullets_enabled_set.get_active_indexes()) {
		if (i < 0 || i >= (int)all_cached_speed.size()) {
			continue;
		}
		all_cached_acceleration[i] = acceleration;
		// magnet_max_speed 0 = no cap (the max_speed convention).
		all_cached_max_speed[i] = top_speed > 0.0f ? Math::max(all_cached_speed[i], top_speed) : 0.0f;
	}
}

bool BulletVolley2D::collect_pickup_bullet(int bullet_index, Node2D *collector) {
	if (bullet_index < 0 || bullet_index >= (int)all_cached_instance_transforms.size() || bullet_factory == nullptr) {
		return true;
	}
	const uint64_t self_id = get_instance_id();
	const Ref<BulletPickupData2D> pickup = pickup_data; // survives a handler that releases the life
	const Vector2 position = all_cached_instance_transforms[bullet_index].columns[2];
	const uint64_t epoch = collision_epoch_for_bullet(bullet_index);
	// The bullet is alive in the handler (contract of every bullet signal).
	bullet_factory->note_user_code();
	const Variant collector_variant = collector != nullptr ? Variant(collector) : Variant();
	emit_on2d(bullet_factory, "pickup_collected", collector_variant, position, this, bullet_index, pickup->value);
	if (!survived_user_code(self_id)) {
		return false;
	}
	// A handler that took the bullet out (disabled it, converted it) owns the
	// outcome: no callback, sound or effect for a collection that did not happen.
	auto still_ours = [&]() { return all_bullets_enabled_set.contains(bullet_index) && collision_epoch_for_bullet(bullet_index) == epoch; };
	if (!still_ours()) {
		return true;
	}
	// The optional callback of the pickup resource: same arguments as the signal.
	if (pickup->collect_callback.is_valid()) {
		bullet_factory->note_user_code();
		Array args;
		args.push_back(collector_variant);
		args.push_back(position);
		args.push_back(this);
		args.push_back(bullet_index);
		args.push_back(pickup->value);
		pickup->collect_callback.callv(args);
		if (!survived_user_code(self_id)) {
			return false;
		}
		if (!still_ours()) {
			return true;
		}
	}
	if (pickup->collect_sound.is_valid()) {
		bullet_factory->play_sound(pickup->collect_sound, position);
	}
	if (pickup->collect_effect.is_valid()) {
		bullet_factory->spawn_layer_effect(pickup->collect_effect, Transform2D(0.0f, position));
	}
	// A handler may have disabled or recycled the bullet: only a still-live,
	// same-life bullet is taken out now.
	if (all_bullets_enabled_set.contains(bullet_index) && collision_epoch_for_bullet(bullet_index) == epoch) {
		disable_bullet(bullet_index, true);
	}
	return ObjectDB::get_instance(ObjectID(self_id)) == this;
}

Array BulletVolley2D::convert_to_pickups(const Ref<BulletPickupData2D> &pickup, const Variant &target, const PackedInt32Array &bullet_indexes) {
	const char *function_name = "BulletVolley2D.convert_to_pickups";
	for (int64_t k = 0; k < bullet_indexes.size(); ++k) {
		const int i = bullet_indexes[k];
		if (i < 0 || i >= amount_bullets) {
			UtilityFunctions::push_error(String(function_name) + ": bullet index " + String::num_int64(i) + " is out of range (0.." + String::num_int64(amount_bullets - 1) + "), nothing converted.");
			return Array();
		}
	}
	if (bullet_factory == nullptr) {
		UtilityFunctions::push_error(String(function_name) + ": this volley was never spawned through a BulletFactory2D, nothing converted.");
		return Array();
	}
	BulletFactory2D::PickupSelection selection;
	selection.kind = BulletFactory2D::PickupSelection::VOLLEY;
	selection.volley = this;
	selection.indexes = &bullet_indexes;
	return bullet_factory->convert_to_pickups_impl(function_name, pickup, target, selection);
}

} // namespace BlastBullets2D
