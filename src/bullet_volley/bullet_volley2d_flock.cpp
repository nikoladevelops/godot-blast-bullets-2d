// Flocking: arming (the spawner's shared config, or flock_join for factory
// volleys), leaving, and the readouts. The pass that steers every flock is
// the factory's (FlockSolver2D, before any volley ticks); the per-bullet turn
// is step_flock in bullet_volley2d_tick.cpp.

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletVolley2D::flock_set_config(const std::shared_ptr<FlockConfig2D> &config) {
	const bool had = flock_config != nullptr;
	const bool has = config != nullptr;
	flock_config = config;
	flock_view = FlockView2D();
	flock_cache.clear();
	if (had != has && bullet_factory != nullptr && factory_alive()) {
		bullet_factory->flock_note_armed(has ? 1 : -1);
	}
}

bool BulletVolley2D::flock_join(const Ref<BulletFlock2D> &flock, BulletFlock2D::Scope scope) {
	if (reject_pooled_handle("flock_join")) {
		return false;
	}
	if (bullet_factory == nullptr) {
		UtilityFunctions::push_error("flock_join: this volley was never spawned by a BulletFactory2D, nothing changed.");
		return false;
	}
	if (scope < BulletFlock2D::SCOPE_PER_SPAWNER || scope > BulletFlock2D::SCOPE_PER_VOLLEY) {
		UtilityFunctions::push_error("flock_join: scope must be 0 (Per Spawner), 1 (Shared) or 2 (Per Volley), nothing changed.");
		return false;
	}
	std::shared_ptr<FlockConfig2D> config(new FlockConfig2D());
	// null = the factory's default flock (the default rules).
	config->flock = flock.is_valid() ? flock : bullet_factory->get_default_flock();
	config->scope = scope;
	config->owner_id = owner_spawner_id;
	flock_set_config(config);
	return true;
}

void BulletVolley2D::flock_leave() {
	if (reject_pooled_handle("flock_leave")) {
		return;
	}
	flock_set_config(nullptr);
}

Ref<BulletFlock2D> BulletVolley2D::flock_get() const {
	if (flock_config == nullptr || !flock_config->enabled) {
		return Ref<BulletFlock2D>();
	}
	return flock_config->flock;
}

int BulletVolley2D::flock_get_scope() const {
	if (flock_config == nullptr || !flock_config->enabled || flock_config->flock.is_null()) {
		return -1;
	}
	return flock_config->scope;
}

bool BulletVolley2D::flock_view_current() const {
	return flock_config != nullptr && bullet_factory != nullptr && flock_view.sweep != 0 && flock_view.sweep == bullet_factory->get_flock_solver().get_sweep() && flock_view.life == volley_generation && flock_view.serial == flock_config->serial && bullet_factory->get_flock_solver().covers(flock_view.base, amount_bullets);
}

Vector2 BulletVolley2D::debug_get_bullet_flock_steering(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "debug_get_bullet_flock_steering") || !flock_view_current()) {
		return Vector2();
	}
	return bullet_factory->get_flock_solver().steer_at(flock_view.base + (uint32_t)bullet_index);
}

int BulletVolley2D::debug_get_bullet_flock_neighbors(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "debug_get_bullet_flock_neighbors") || !flock_view_current()) {
		return 0;
	}
	return bullet_factory->get_flock_solver().neighbors_at(flock_view.base + (uint32_t)bullet_index);
}

Dictionary BulletVolley2D::debug_get_bullet_flock_info(int bullet_index) const {
	Dictionary d;
	if (!validate_bullet_index(bullet_index, "debug_get_bullet_flock_info")) {
		return d;
	}
	const Ref<BulletFlock2D> flock = flock_get();
	const BulletFlock2D *f = flock.ptr();
	d["flocking"] = f != nullptr;
	d["flock"] = flock;
	d["scope"] = flock_get_scope();
	d["steering"] = debug_get_bullet_flock_steering(bullet_index);
	d["neighbors"] = debug_get_bullet_flock_neighbors(bullet_index);
	double blend = f != nullptr ? 1.0 : 0.0;
	if (f != nullptr && f->blend_in_sec > 0.0) {
		const double x = CLAMP(curves_elapsed_time / (double)f->blend_in_sec, 0.0, 1.0);
		blend = x * x * (3.0 - 2.0 * x);
	}
	d["blend"] = blend;
	const bool posed = bullet_index < (int)all_cached_instance_transforms.size();
	d["radius"] = (f != nullptr && f->count_bullet_size && posed) ? flock_bullet_radius2d(flock_quad_radius2d(texture_size), all_cached_instance_transforms[bullet_index]) : 0.0;
	Vector2 heading;
	if (bullet_index < (int)all_cached_direction.size() && bullet_index < (int)all_cached_speed.size()) {
		heading = all_cached_direction[bullet_index] * (all_cached_speed[bullet_index] < 0.0 ? (real_t)-1.0 : (real_t)1.0);
	}
	d["heading"] = heading;
	d["facing"] = posed ? (double)Math::atan2(all_cached_instance_transforms[bullet_index].columns[0].y, all_cached_instance_transforms[bullet_index].columns[0].x) - (double)cache_texture_rotation_radians : 0.0;
	d["max_turn_deg"] = f != nullptr ? (double)f->max_turn_rate_degrees : 0.0;
	return d;
}

} // namespace BlastBullets2D
