// BulletSpawner2D (split from bullet_spawner2d.cpp, same class): the Emitter
// Bullets group. Every volley fired while it is on carries this spawner's
// shared EmitterConfig2D (the timing, the template spawner by id, the switch);
// its bullets record shots that the factory fires through the template after
// its sweep (BulletFactory2D::flush_emits).

#include "bullet_spawner/pattern_spawner2d_internal.hpp"
#include "bullet_spawner/spawner_live_helpers2d.hpp"

#include <unordered_set>

using namespace godot;

namespace BlastBullets2D {

void BulletSpawner2D::set_emitter_enabled(bool value) {
	if (emitter_enabled == value) {
		return;
	}
	emitter_enabled = value;
	const uint64_t self_id = get_instance_id();
	apply_emitter_config();
	if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
		return; // the replaced timing resource's PREDELETE freed this spawner
	}
	emitter_cycle_reported = false;
	on_config_changed();
	notify_property_list_changed();
}

void BulletSpawner2D::set_emitter(const Ref<BulletEmitterData2D> &value) {
	if (emitter == value) {
		return;
	}
	// Dropping the old member Ref below can destroy a script resource: its
	// NOTIFICATION_PREDELETE is user code that may free this spawner (proven
	// under ASan). The id is captured first so the check never reads `this`
	// after the drop; nothing below may run while it fails.
	const uint64_t self_id = get_instance_id();
	emitter = value;
	if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
		return;
	}
	apply_emitter_config();
	if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
		return; // the shared config's own Ref drop freed this spawner
	}
	on_config_changed();
}

void BulletSpawner2D::set_emitter_spawner_path(const NodePath &value) {
	if (emitter_spawner_path == value) {
		return;
	}
	emitter_spawner_path = value;
	const uint64_t self_id = get_instance_id();
	apply_emitter_config();
	if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
		return; // the replaced timing resource's PREDELETE freed this spawner
	}
	emitter_cycle_reported = false;
	on_config_changed();
}

PatternSpawner2D *BulletSpawner2D::get_emitter_template() const {
	if (emitter_spawner_path.is_empty() || !is_inside_tree()) {
		return nullptr;
	}
	PatternSpawner2D *tpl = Object::cast_to<PatternSpawner2D>(get_node_or_null(emitter_spawner_path));
	if (tpl == nullptr || tpl->is_queued_for_deletion()) {
		return nullptr;
	}
	return tpl;
}

Ref<BulletEmitterData2D> BulletSpawner2D::get_effective_emitter() const {
	if (emitter.is_valid()) {
		return emitter;
	}
	if (default_emitter.is_null()) {
		default_emitter.instantiate();
	}
	return default_emitter;
}

bool BulletSpawner2D::emitter_leads_back() const {
	// Each step: the template's own emitter template. A loop that does not
	// pass through this spawner is that chain's own business (its members
	// refuse themselves); only a way back here is this spawner's cycle. No
	// depth cap: a chain of distinct spawners ends, a loop revisits one.
	std::unordered_set<const PatternSpawner2D *> visited;
	const PatternSpawner2D *step = get_emitter_template();
	while (step != nullptr) {
		if (step == this) {
			return true;
		}
		if (!visited.insert(step).second) {
			return false;
		}
		const BulletSpawner2D *next = Object::cast_to<BulletSpawner2D>(step);
		if (next == nullptr || !next->emitter_enabled) {
			return false;
		}
		step = next->get_emitter_template();
	}
	return false;
}

void BulletSpawner2D::apply_emitter_config() {
	if (emitter_shared == nullptr) {
		return; // nothing armed yet: the first arm copies the settings
	}
	// Replacing the shared config's timing resource drops its old Ref: the
	// last one destroys a script resource whose NOTIFICATION_PREDELETE is user
	// code that may free this spawner (proven under ASan). Capture the id
	// before the drop; nothing below may read `this` while the check fails.
	const uint64_t self_id = get_instance_id();
	const Ref<BulletEmitterData2D> data = get_effective_emitter();
	const uint64_t template_before = emitter_shared->template_id;
	const bool changed = emitter_shared->data != data || emitter_shared->enabled != emitter_enabled;
	emitter_shared->data = data;
	if (ObjectDB::get_instance(ObjectID(self_id)) != this) {
		return;
	}
	emitter_shared->enabled = emitter_enabled;
	// A template that cannot be resolved right now (outside the tree, a
	// path being edited) keeps the last one: flying bullets never lose it.
	const PatternSpawner2D *tpl = get_emitter_template();
	if (tpl != nullptr) {
		// A way back to this spawner (made while bullets fly): they stop
		// firing at once; the next shot reports the cycle (apply_volley_emitter).
		emitter_shared->template_id = emitter_enabled && emitter_leads_back() ? 0 : tpl->get_instance_id();
	} else if (emitter_spawner_path.is_empty()) {
		emitter_shared->template_id = 0;
	}
	if (changed || emitter_shared->template_id != template_before) {
		// Flying bullets re-open their schedule gate (another resource may
		// lift a max_shots they used up), like an edit of the resource.
		++BulletEmitterData2D::rules_epoch;
	}
}

void BulletSpawner2D::apply_volley_emitter(BulletVolley2D *volley) {
	if (volley == nullptr || !emitter_enabled) {
		return;
	}
	const PatternSpawner2D *tpl = get_emitter_template();
	if (tpl == nullptr) {
		return; // named by the setup warnings, never an error per shot
	}
	if (emitter_leads_back()) {
		if (!emitter_cycle_reported) {
			emitter_cycle_reported = true;
			UtilityFunctions::push_error("BulletSpawner2D: emitter_spawner_path leads back to this spawner (an emitter cycle), its bullets do not emit.");
		}
		return;
	}
	// No cycle now (also when another spawner of the chain fixed it): a
	// cycle made again later reports again.
	emitter_cycle_reported = false;
	if (emitter_shared == nullptr) {
		emitter_shared = std::shared_ptr<EmitterConfig2D>(new EmitterConfig2D());
	}
	apply_emitter_config();
	volley->emitter_arm(emitter_shared, cadence.next_shot_number()); // the shot number salts the seeded rolls
}

void BulletSpawner2D::emitter_live_volleys(std::unordered_set<uint64_t> &r_changed) {
	BulletFactory2D *factory = get_bullet_factory();
	// Off reaches flying bullets through the shared config already.
	if (factory == nullptr || !emitter_enabled) {
		return;
	}
	const uint64_t self_id = get_instance_id();
	const std::vector<OwnedVolley2D> owned = snapshot_owned2d(factory, self_id);
	for (const OwnedVolley2D &entry : owned) {
		BulletVolley2D *volley = still_owned2d(entry, self_id);
		if (volley == nullptr) {
			continue;
		}
		if (emitter_shared == nullptr || volley->emitter_config != emitter_shared) {
			// The arm replaces the config a script may have given this volley
			// (emitter_set): that config's Ref to its timing resource is then
			// the last one, and the resource's PREDELETE is user code that may
			// free this spawner (proven under AddressSanitizer). Read nothing
			// on `this` after it and re-validate the volley before counting it.
			apply_volley_emitter(volley);
			if (still_owned2d(entry, self_id) != nullptr && entry.volley->emitter_config != nullptr) {
				r_changed.insert(entry.id);
			}
			if (ObjectDB::get_instance(ObjectID(self_id)) != this || shot_owner_dying()) {
				return;
			}
		}
	}
}

} // namespace BlastBullets2D
