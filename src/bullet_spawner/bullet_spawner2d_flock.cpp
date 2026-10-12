// BulletSpawner2D (split from bullet_spawner2d.cpp, same class): the
// Flocking group. Every volley fired while flocking is on joins flock with
// this spawner's shared FlockConfig2D (flock_scope decides who flies
// together); the factory's flock pass steers it from then on.

#include "bullet_spawner/bullet_spawner2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

bool BulletSpawner2D::get_flock_enabled() const {
	return flock_enabled;
}

void BulletSpawner2D::set_flock_enabled(bool value) {
	if (flock_enabled == value) {
		return;
	}
	flock_enabled = value;
	apply_flock_config();
	on_config_changed();
	notify_property_list_changed();
}

Ref<BulletFlock2D> BulletSpawner2D::get_flock() const {
	return flock;
}

void BulletSpawner2D::set_flock(const Ref<BulletFlock2D> &value) {
	if (flock == value) {
		return;
	}
	flock = value;
	apply_flock_config();
	on_config_changed();
}

BulletFlock2D::Scope BulletSpawner2D::get_flock_scope() const {
	return flock_scope;
}

void BulletSpawner2D::set_flock_scope(BulletFlock2D::Scope value) {
	if (value < BulletFlock2D::SCOPE_PER_SPAWNER || value > BulletFlock2D::SCOPE_PER_VOLLEY) {
		UtilityFunctions::push_error("BulletSpawner2D: flock_scope must be 0 (Per Spawner), 1 (Shared) or 2 (Per Volley), keeping the old value.");
		return;
	}
	if (flock_scope == value) {
		return;
	}
	flock_scope = value;
	apply_flock_config();
	on_config_changed();
}

void BulletSpawner2D::apply_flock_config() {
	if (flock_shared == nullptr) {
		return; // nothing armed yet: the first arm copies the settings
	}
	flock_shared->flock = flock;
	flock_shared->scope = flock_scope;
	flock_shared->enabled = flock_enabled;
	++flock_shared->serial;
}

void BulletSpawner2D::apply_volley_flock(BulletVolley2D *volley) {
	if (volley == nullptr || !flock_enabled || flock.is_null()) {
		return;
	}
	if (flock_shared == nullptr) {
		flock_shared = std::shared_ptr<FlockConfig2D>(new FlockConfig2D());
		flock_shared->owner_id = get_instance_id();
		apply_flock_config();
	}
	volley->flock_set_config(flock_shared);
}

} //namespace BlastBullets2D
