// BulletSpawner2D (split from bullet_spawner2d.cpp, same class): the
// Splitting group. Every volley fired while splitting is on carries this
// spawner's shared SplitConfig2D (the chain + the switch) and the shot's
// spawn data; dying bullets record splits that the factory spawns after its
// sweep (BulletFactory2D::flush_splits).

#include "bullet_spawner/bullet_spawner2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

bool BulletSpawner2D::get_split_enabled() const {
	return split_enabled;
}

void BulletSpawner2D::set_split_enabled(bool value) {
	if (split_enabled == value) {
		return;
	}
	split_enabled = value;
	apply_split_config();
	on_config_changed();
	notify_property_list_changed();
}

Ref<BulletSplitData2D> BulletSpawner2D::get_split_data() const {
	return split_data;
}

void BulletSpawner2D::set_split_data(const Ref<BulletSplitData2D> &value) {
	if (split_data == value) {
		return;
	}
	split_data = value;
	apply_split_config();
	on_config_changed();
}

void BulletSpawner2D::apply_split_config() {
	if (split_shared == nullptr) {
		return; // nothing armed yet: the first arm copies the settings
	}
	split_shared->split = split_data;
	split_shared->enabled = split_enabled;
}

void BulletSpawner2D::apply_volley_split(BulletVolley2D *volley, const Ref<BulletVolleyData2D> &spawn_data) {
	if (volley == nullptr || !split_enabled || split_data.is_null() || spawn_data.is_null()) {
		return;
	}
	if (split_shared == nullptr) {
		split_shared = std::shared_ptr<SplitConfig2D>(new SplitConfig2D());
		apply_split_config();
	}
	volley->split_arm(split_shared, spawn_data, 0);
}

bool BulletSpawner2D::split_configure_child_homing(BulletVolley2D *child) {
	if (child == nullptr || !homing_enabled) {
		return false;
	}
	apply_volley_homing_and_orbiting(child);
	return true;
}

} //namespace BlastBullets2D
