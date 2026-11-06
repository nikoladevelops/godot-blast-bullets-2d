// BulletSpawner2D (split from bullet_spawner2d.cpp, same class): the
// Splitting group. Every volley fired while splitting is on carries this
// spawner's shared SplitConfig2D (the chain + the switch) and the shot's
// spawn data; dying bullets record splits that the factory spawns after its
// sweep (BulletFactory2D::flush_splits).

#include "bullet_spawner/pattern_spawner2d_internal.hpp"

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
	const uint64_t self_id = get_instance_id();
	apply_split_config();
	if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
		return; // the replaced chain's PREDELETE freed this spawner
	}
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
	// Dropping the old member Ref below can destroy a script resource: its
	// NOTIFICATION_PREDELETE is user code that may free this spawner (proven
	// under ASan). The id is captured first so the check never reads `this`
	// after the drop; nothing below may run while it fails.
	const uint64_t self_id = get_instance_id();
	connect_split_data(false);
	split_data = value;
	if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
		return;
	}
	connect_split_data(true);
	apply_split_config();
	if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
		return; // the shared config's own Ref drop freed this spawner
	}
	on_config_changed();
}

void BulletSpawner2D::connect_split_data(bool connect) {
	if (split_data.is_null()) {
		return;
	}
	const Callable on_changed = callable_mp(this, &BulletSpawner2D::_on_split_data_changed);
	const StringName changed("changed");
	const bool connected = split_data->is_connected(changed, on_changed);
	if (connect && !connected) {
		split_data->connect(changed, on_changed);
	} else if (!connect && connected) {
		split_data->disconnect(changed, on_changed);
	}
}

void BulletSpawner2D::_on_split_data_changed() {
	on_config_changed();
}

void BulletSpawner2D::apply_split_config() {
	if (split_shared == nullptr) {
		return; // nothing armed yet: the first arm copies the settings
	}
	// Replacing the shared config's chain drops its old Ref: the last one
	// destroys a script resource whose NOTIFICATION_PREDELETE is user code
	// that may free this spawner (proven under ASan). Capture the id before
	// the drop; nothing below may read `this` while the check fails.
	const uint64_t self_id = get_instance_id();
	split_shared->split = split_data;
	if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
		return;
	}
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
	// The shot number salts the seeded rolls: successive shots differ.
	volley->split_arm(split_shared, spawn_data, 0, cadence.next_shot_number());
}

bool BulletSpawner2D::split_configure_child_homing(BulletVolley2D *child) {
	if (child == nullptr || !homing_enabled) {
		return false;
	}
	apply_volley_homing_and_orbiting(child);
	return true;
}

} //namespace BlastBullets2D
