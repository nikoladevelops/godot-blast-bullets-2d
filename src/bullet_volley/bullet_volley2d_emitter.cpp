// Emitter bullets on the volley side: arming (the spawner's shared config, or
// emitter_set), the per-tick timer pass that records due shots, and the
// readouts. The shots themselves are the factory's (flush_emits in
// bullet_factory2d_emitter.cpp): nothing fires inside the sweep.

#include "bullet_spawner/pattern_spawner2d.hpp"
#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

namespace {

// SplitMix64: the seeded chance roll of (seed, salt, bullet, shot), in [0, 1).
double emitter_roll(int64_t seed, uint64_t salt, int bullet_index, int shot) {
	uint64_t x = (uint64_t)seed * 0x9E3779B97F4A7C15ull ^ (salt << 32) ^ ((uint64_t)(uint32_t)bullet_index << 1) ^ ((uint64_t)(uint32_t)shot * 0xD6E8FEB86659FD93ull);
	x += 0x9E3779B97F4A7C15ull;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
	x ^= x >> 31;
	return (double)(x >> 11) * (1.0 / 9007199254740992.0);
}

} // namespace

void BulletVolley2D::emitter_arm(const std::shared_ptr<EmitterConfig2D> &config, uint64_t salt) {
	if (config == emitter_config && config != nullptr && emit_next_at.size() == (size_t)amount_bullets) {
		return; // armed alike: the clocks keep running
	}
	emitter_config = config;
	emit_salt = salt;
	emit_next_at.clear();
	emit_shots.clear();
	emit_next_min = Math::INF;
	if (config == nullptr) {
		return;
	}
	const BulletEmitterData2D *data = config->data.ptr();
	const double delay = data != nullptr ? data->start_delay_sec : 0.0;
	const double stagger = data != nullptr ? data->stagger_sec : 0.0;
	emit_next_at.resize((size_t)amount_bullets);
	emit_shots.assign((size_t)amount_bullets, 0);
	for (int i = 0; i < amount_bullets; ++i) {
		emit_next_at[i] = life_age + delay + stagger * (double)i;
	}
	emit_next_min = amount_bullets > 0 ? emit_next_at[0] : Math::INF;
}

void BulletVolley2D::emitter_on_wake(int bullet_index, bool fresh_life) {
	if (emitter_config == nullptr || bullet_index < 0 || bullet_index >= (int)emit_next_at.size()) {
		return;
	}
	if (fresh_life && bullet_index < (int)emit_shots.size()) {
		const BulletEmitterData2D *data = emitter_config->data.ptr();
		const double delay = data != nullptr ? data->start_delay_sec : 0.0;
		const double stagger = data != nullptr ? data->stagger_sec : 0.0;
		emit_shots[bullet_index] = 0;
		emit_next_at[bullet_index] = life_age + delay + stagger * (double)bullet_index;
	}
	emit_next_min = Math::min(emit_next_min, emit_next_at[bullet_index]);
}

void BulletVolley2D::emitter_tick() {
	if (life_age < emit_next_min && emit_rules_seen == BulletEmitterData2D::rules_epoch) {
		return; // nobody is due and nothing was edited: two compares per tick
	}
	emit_rules_seen = BulletEmitterData2D::rules_epoch;
	const EmitterConfig2D *config = emitter_config.get();
	const BulletEmitterData2D *data = config != nullptr ? config->data.ptr() : nullptr;
	if (data == nullptr || bullet_factory == nullptr || emit_next_at.size() != (size_t)amount_bullets || emit_shots.size() != (size_t)amount_bullets) {
		emit_next_min = Math::INF;
		return;
	}
	if (!config->enabled || config->template_id == 0) {
		// Switched off (or no template yet): the clocks hold their schedule,
		// checked again next tick (a switch back on fires what is due).
		return;
	}
	const double interval = data->interval_sec > 0.0 ? data->interval_sec : 1.0;
	const int max_shots = data->max_shots;
	const double chance = data->chance;
	const BulletEmitterData2D::EmitterAim aim = data->aim;
	const double spin = data->spin_per_shot_degrees;
	const bool inherit = data->inherit_bullet_velocity;
	const PoolKey key = get_pool_key();
	double next = Math::INF;
	for (int i : all_bullets_enabled_set.get_active_indexes()) {
		if (i < 0 || i >= amount_bullets) {
			continue;
		}
		double &due = emit_next_at[i];
		if (max_shots > 0 && emit_shots[i] >= max_shots) {
			continue; // done firing this life
		}
		if (due > life_age) {
			next = Math::min(next, due);
			continue;
		}
		// One shot per bullet per tick: a hitch never fires a burst of
		// catch-up shots, the schedule simply moves on.
		const int shot = emit_shots[i]++;
		due += interval;
		if (due <= life_age) {
			due = life_age + interval;
		}
		if (max_shots == 0 || emit_shots[i] < max_shots) {
			next = Math::min(next, due);
		}
		if (chance < 1.0) {
			const double roll = data->seed != 0 ? emitter_roll(data->seed, emit_salt, i, shot) : UtilityFunctions::randf();
			if (roll >= chance) {
				continue;
			}
		}
		if (!bullet_factory->emit_has_room()) {
			continue;
		}
		const Transform2D &pose = all_cached_instance_transforms[i];
		EmitRequest2D request;
		request.template_id = config->template_id;
		request.owner_id = owner_spawner_id;
		request.source_key = key;
		double angle = 0.0;
		if (aim == BulletEmitterData2D::EMITTER_AIM_HEADING) {
			Vector2 heading = i < (int)all_cached_velocity.size() ? all_cached_velocity[i] : Vector2();
			if (heading.x == 0.0f && heading.y == 0.0f) {
				heading = i < (int)all_cached_direction.size() ? all_cached_direction[i] : Vector2();
			}
			angle = (heading.x != 0.0f || heading.y != 0.0f) ? Math::atan2((double)heading.y, (double)heading.x) : (double)pose.get_rotation() - (double)cache_texture_rotation_radians;
		}
		request.aim_template = aim == BulletEmitterData2D::EMITTER_AIM_TEMPLATE;
		request.pose = Transform2D((real_t)angle, pose.columns[2]);
		request.extra_spin_deg = spin * (double)shot;
		if (!Math::is_finite(request.extra_spin_deg)) {
			request.extra_spin_deg = 0.0;
		}
		if (inherit && i < (int)all_cached_velocity.size() && all_cached_velocity[i].is_finite()) {
			request.velocity = all_cached_velocity[i];
		}
		bullet_factory->emit_enqueue(std::move(request));
	}
	emit_next_min = next;
}

bool BulletVolley2D::emitter_set(const Ref<BulletEmitterData2D> &emitter, Node *template_spawner) {
	if (reject_pooled_handle("emitter_set")) {
		return false;
	}
	if (emitter.is_null()) {
		UtilityFunctions::push_error("emitter_set: emitter is null, nothing changed.");
		return false;
	}
	PatternSpawner2D *tpl = Object::cast_to<PatternSpawner2D>(template_spawner);
	if (tpl == nullptr) {
		UtilityFunctions::push_error("emitter_set: template_spawner must be a PatternSpawner2D (a BulletSpawner2D or LaserSpawner2D), nothing changed.");
		return false;
	}
	std::shared_ptr<EmitterConfig2D> config(new EmitterConfig2D());
	config->data = emitter;
	config->template_id = tpl->get_instance_id();
	emitter_arm(config, (uint64_t)volley_generation);
	return true;
}

void BulletVolley2D::emitter_clear() {
	emitter_arm(nullptr);
}

Ref<BulletEmitterData2D> BulletVolley2D::emitter_get_data() const {
	return emitter_config != nullptr ? emitter_config->data : Ref<BulletEmitterData2D>();
}

int BulletVolley2D::get_bullet_emitter_shots(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_emitter_shots")) {
		return 0;
	}
	return bullet_index < (int)emit_shots.size() ? emit_shots[bullet_index] : 0;
}

} // namespace BlastBullets2D
