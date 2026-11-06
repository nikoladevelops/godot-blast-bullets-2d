// Script readouts of a volley's live state (counts, per-bullet ledgers,
// clocks) and the few writes that go with them. Plain reads of state the tick
// keeps anyway: nothing here runs per bullet per tick.
#include "bullet_volley/bullet_volley2d_internal.hpp"

#include "data/bullet_emitter_data2d.hpp"
#include "data/bullet_pickup_data2d.hpp"
#include "factory/emit_queue2d.hpp"

namespace BlastBullets2D {

int BulletVolley2D::get_active_bullet_count() const {
	return is_pooled_in_pool ? 0 : active_bullets_counter;
}

PackedInt32Array BulletVolley2D::get_active_bullet_indexes() const {
	PackedInt32Array out;
	if (is_pooled_in_pool) {
		return out;
	}
	for (int i : all_bullets_enabled_set.get_active_indexes()) {
		if (i >= 0 && i < amount_bullets) {
			out.push_back(i);
		}
	}
	out.sort();
	return out;
}

bool BulletVolley2D::is_volley_active() const {
	return is_active && !is_pooled_in_pool;
}

double BulletVolley2D::get_life_age() const {
	return is_pooled_in_pool ? 0.0 : life_age;
}

int BulletVolley2D::bullet_get_split_count(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "bullet_get_split_count")) {
		return 0;
	}
	return split_count_of(bullet_index);
}

Vector2 BulletVolley2D::bullet_get_last_bounce_normal(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "bullet_get_last_bounce_normal")) {
		return Vector2();
	}
	return bullet_index < (int)all_bounce_last_normal.size() ? all_bounce_last_normal[bullet_index] : Vector2();
}

void BulletVolley2D::bullet_set_bounce_count(int bullet_index, int count) {
	if (!validate_bullet_index(bullet_index, "bullet_set_bounce_count") || reject_pooled_handle("bullet_set_bounce_count")) {
		return;
	}
	if (count < 0) {
		UtilityFunctions::push_error("BulletVolley2D: bullet_set_bounce_count count must be >= 0, nothing changed.");
		return;
	}
	if (bullet_index >= (int)all_bounce_count.size()) {
		UtilityFunctions::push_error("BulletVolley2D: bullet_set_bounce_count needs bouncing on (bounce_mask is 0), nothing changed.");
		return;
	}
	all_bounce_count[bullet_index] = count;
}

double BulletVolley2D::get_scale_in_sec() const {
	return scale_in_sec;
}

double BulletVolley2D::get_scale_in_from() const {
	return scale_in_from;
}

double BulletVolley2D::get_scale_in_factor() const {
	return scale_in_active ? scale_in_applied : 1.0;
}

Ref<BulletPickupData2D> BulletVolley2D::get_pickup_data() const {
	return pickup_data;
}

double BulletVolley2D::bullet_get_emitter_next_shot_sec(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "bullet_get_emitter_next_shot_sec")) {
		return -1.0;
	}
	const EmitterConfig2D *config = emitter_config.get();
	const BulletEmitterData2D *data = config != nullptr ? config->data.ptr() : nullptr;
	if (is_pooled_in_pool || data == nullptr || !config->enabled || bullet_index >= (int)emit_next_at.size() || bullet_index >= (int)emit_shots.size() || !all_bullets_enabled_set.contains(bullet_index)) {
		return -1.0;
	}
	if (data->max_shots > 0 && emit_shots[bullet_index] >= data->max_shots) {
		return -1.0; // done firing this life
	}
	return Math::max(emit_next_at[bullet_index] - life_age, 0.0);
}

} // namespace BlastBullets2D
