// BulletSpawner2D (split from bullet_spawner2d.cpp, same class): the Emitter
// Bullets group. Every volley fired while it is on carries this spawner's
// shared EmitterConfig2D (the timing, the template spawner by id, the switch);
// its bullets record shots that the factory fires through the template after
// its sweep (BulletFactory2D::flush_emits).

#include "bullet_spawner/pattern_spawner2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletSpawner2D::set_emitter_enabled(bool value) {
	if (emitter_enabled == value) {
		return;
	}
	emitter_enabled = value;
	apply_emitter_config();
	emitter_cycle_reported = false;
	on_config_changed();
	notify_property_list_changed();
}

void BulletSpawner2D::set_emitter(const Ref<BulletEmitterData2D> &value) {
	if (emitter == value) {
		return;
	}
	emitter = value;
	apply_emitter_config();
	on_config_changed();
}

void BulletSpawner2D::set_emitter_spawner_path(const NodePath &value) {
	if (emitter_spawner_path == value) {
		return;
	}
	emitter_spawner_path = value;
	apply_emitter_config();
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
	// refuse themselves); only a way back here is this spawner's cycle.
	const PatternSpawner2D *visited[16];
	int count = 0;
	const PatternSpawner2D *step = get_emitter_template();
	while (step != nullptr && count < 16) {
		if (step == this) {
			return true;
		}
		for (int k = 0; k < count; ++k) {
			if (visited[k] == step) {
				return false;
			}
		}
		visited[count++] = step;
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
	emitter_shared->data = get_effective_emitter();
	emitter_shared->enabled = emitter_enabled;
	// A template that cannot be resolved right now (outside the tree, a
	// path being edited) keeps the last one: flying bullets never lose it.
	const PatternSpawner2D *tpl = get_emitter_template();
	if (tpl != nullptr) {
		emitter_shared->template_id = tpl->get_instance_id();
	} else if (emitter_spawner_path.is_empty()) {
		emitter_shared->template_id = 0;
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
	if (emitter_shared == nullptr) {
		emitter_shared = std::shared_ptr<EmitterConfig2D>(new EmitterConfig2D());
	}
	apply_emitter_config();
	volley->emitter_arm(emitter_shared, (uint64_t)volleys_fired + 1); // the shot number salts the seeded rolls
}

void BulletSpawner2D::emitter_live_volleys(std::unordered_set<uint64_t> &r_changed) {
	BulletFactory2D *factory = get_bullet_factory();
	// Off reaches flying bullets through the shared config already.
	if (factory == nullptr || !emitter_enabled) {
		return;
	}
	std::vector<BulletVolley2D *> owned;
	factory->collect_volleys_owned_by(get_instance_id(), owned);
	for (BulletVolley2D *volley : owned) {
		if (emitter_shared == nullptr || volley->emitter_config != emitter_shared) {
			apply_volley_emitter(volley);
			if (volley->emitter_config != nullptr) {
				r_changed.insert(volley->get_instance_id());
			}
		}
	}
}

} // namespace BlastBullets2D
