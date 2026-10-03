// Wobble: per-bullet sideways oscillation seeds and their runtime API.

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

BulletVolley2D::WobbleSeed BulletVolley2D::make_wobble_seed(const Ref<BulletWobbleData2D> &wobble, int bullet_index) const {
	WobbleSeed seed;
	BulletWobbleData2D *w = wobble.ptr();
	if (w == nullptr || !w->enabled) {
		return seed;
	}
	if (!Math::is_finite(w->amplitude) || w->amplitude < 0.0 || !Math::is_finite(w->frequency_hz) || w->frequency_hz < 0.0) {
		return seed;
	}
	if (!Math::is_finite(w->phase_rad) || !Math::is_finite(w->phase_step_per_bullet) || !Math::is_finite(w->damping_per_sec) || w->damping_per_sec < 0.0) {
		return seed;
	}
	if (!Math::is_finite(w->delay_sec) || w->delay_sec < 0.0 || !Math::is_finite(w->duration_sec) || w->duration_sec < 0.0) {
		return seed;
	}
	if (!Math::is_finite(w->face_rotation_speed) || w->face_rotation_speed < 0.0) {
		return seed;
	}
	seed.active = true;
	seed.mode = (w->mode == BulletWobbleData2D::WOBBLE_ANGULAR) ? 1 : 0;
	seed.waveform = (w->waveform == BulletWobbleData2D::WOBBLE_COSINE) ? 1 : 0;
	seed.amplitude = w->amplitude;
	seed.frequency_hz = w->frequency_hz;
	seed.phase = w->phase_rad + w->phase_step_per_bullet * (real_t)bullet_index;
	seed.distance_phased = w->distance_phased;
	seed.damping_per_sec = w->damping_per_sec;
	seed.delay_sec = w->delay_sec;
	seed.duration_sec = w->duration_sec;
	seed.face_movement_direction = w->face_movement_direction;
	seed.face_rotation_speed = w->face_rotation_speed;
	return seed;
}

void BulletVolley2D::refresh_wobble_feature_flag() {
	is_wobble_feature_enabled = false;
	for (const auto &w : all_bullet_wobble) {
		if (w.active) {
			is_wobble_feature_enabled = true;
			break;
		}
	}
	if (!is_wobble_feature_enabled) {
		wobble_distance_traveled.assign(amount_bullets, 0.0);
	}
}

real_t BulletVolley2D::bullet_get_wobble_amplitude(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "bullet_get_wobble_amplitude")) {
		return 0.0;
	}
	if (bullet_index < 0 || bullet_index >= (int)all_bullet_wobble.size()) {
		return 0.0;
	}
	const WobbleSeed &w = all_bullet_wobble[bullet_index];
	return w.active ? w.amplitude : 0.0;
}

bool BulletVolley2D::bullet_get_wobble_face_movement_direction(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "bullet_get_wobble_face_movement_direction")) {
		return false;
	}
	if (bullet_index < 0 || bullet_index >= (int)all_bullet_wobble.size()) {
		return false;
	}
	return all_bullet_wobble[bullet_index].active && all_bullet_wobble[bullet_index].face_movement_direction;
}

real_t BulletVolley2D::bullet_get_wobble_face_rotation_speed(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "bullet_get_wobble_face_rotation_speed")) {
		return 0.0;
	}
	if (bullet_index < 0 || bullet_index >= (int)all_bullet_wobble.size()) {
		return 0.0;
	}
	const WobbleSeed &w = all_bullet_wobble[bullet_index];
	return w.active ? w.face_rotation_speed : 0.0;
}

void BulletVolley2D::apply_wobble_from_data(const BulletVolleyData2D &volley_data) {
	all_bullet_wobble.assign(amount_bullets, WobbleSeed());
	all_bullet_wobble_data.assign(amount_bullets, Ref<BulletWobbleData2D>());
	wobble_distance_traveled.assign(amount_bullets, 0.0);
	is_wobble_feature_enabled = false;
	shared_bullet_wobble_data = volley_data.shared_bullet_wobble_data;
	// Strict: entry i seeds bullet i only, and a live seed always beats
	// shared for its bullet. Missing/null/disabled entries fall back to
	// shared per bullet. Tile checkbox wraps short arrays.
	const int wobble_size = volley_data.all_bullet_wobble_data.size();
	const bool tile_wobble = volley_data.tile_all_bullet_wobble_data;
	if (wobble_size != 0 && wobble_size != amount_bullets) {
		WarnOnce2D::warn(warn_data_id, 11u, wobble_size, amount_bullets, "BulletVolley2D: all_bullet_wobble_data size (" + String::num_int64(wobble_size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets use shared wobble" + String(tile_wobble ? " (tiling on: wrapping short array)." : " (check tile_all_bullet_wobble_data to wrap, or provide one entry per bullet)."));
	}
	for (int i = 0; i < amount_bullets; ++i) {
		const int entry = tile_wobble ? resolve_tiled_data_index(wobble_size, i) : resolve_strict_data_index(wobble_size, i);
		if (entry >= 0 && entry < wobble_size) {
			const Ref<BulletWobbleData2D> res = volley_data.all_bullet_wobble_data[entry];
			WobbleSeed seed = make_wobble_seed(res, i);
			if (seed.active) {
				all_bullet_wobble[i] = seed;
				all_bullet_wobble_data[i] = res;
				continue;
			}
		} else if (entry < 0 && wobble_size == 0) {
			// empty array: fall through to shared below
		}
		if (shared_bullet_wobble_data.is_valid()) {
			all_bullet_wobble[i] = make_wobble_seed(shared_bullet_wobble_data, i);
		}
	}
	refresh_wobble_feature_flag();
}

void BulletVolley2D::bullet_set_wobble_data(int bullet_index, const Ref<BulletWobbleData2D> &wobble_data) {
	if (!validate_bullet_index(bullet_index, "bullet_set_wobble_data")) {
		return;
	}
	if ((int)all_bullet_wobble.size() != amount_bullets || (int)all_bullet_wobble_data.size() != amount_bullets) {
		UtilityFunctions::push_error("bullet_set_wobble_data: wobble storage is not set up for this multimesh.");
		return;
	}
	if (wobble_data.is_null()) {
		// Clear back to the shared fallback (or inactive when unset).
		all_bullet_wobble_data[bullet_index].unref();
		if (shared_bullet_wobble_data.is_valid()) {
			all_bullet_wobble[bullet_index] = make_wobble_seed(shared_bullet_wobble_data, bullet_index);
		} else {
			all_bullet_wobble[bullet_index] = WobbleSeed();
		}
		refresh_wobble_feature_flag();
		return;
	}
	WobbleSeed seed = make_wobble_seed(wobble_data, bullet_index);
	if (!seed.active) {
		UtilityFunctions::push_error("bullet_set_wobble_data: wobble data is disabled or invalid, slot cleared to the shared fallback.");
		all_bullet_wobble_data[bullet_index].unref();
		if (shared_bullet_wobble_data.is_valid()) {
			all_bullet_wobble[bullet_index] = make_wobble_seed(shared_bullet_wobble_data, bullet_index);
		} else {
			all_bullet_wobble[bullet_index] = WobbleSeed();
		}
		refresh_wobble_feature_flag();
		return;
	}
	all_bullet_wobble[bullet_index] = seed;
	all_bullet_wobble_data[bullet_index] = wobble_data;
	refresh_wobble_feature_flag();
}

void BulletVolley2D::all_bullets_set_wobble_data(const Ref<BulletWobbleData2D> &wobble_data, int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_wobble_data");
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		bullet_set_wobble_data(i, wobble_data);
	}
}

Ref<BulletWobbleData2D> BulletVolley2D::bullet_get_wobble_data(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "bullet_get_wobble_data")) {
		return Ref<BulletWobbleData2D>();
	}
	if (bullet_index < 0 || bullet_index >= (int)all_bullet_wobble_data.size()) {
		return Ref<BulletWobbleData2D>();
	}
	return all_bullet_wobble_data[bullet_index];
}

TypedArray<BulletWobbleData2D> BulletVolley2D::all_bullets_get_wobble_data(int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_wobble_data");
	TypedArray<BulletWobbleData2D> arr;
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(bullet_get_wobble_data(i));
	}
	return arr;
}

void BulletVolley2D::set_shared_bullet_wobble_data(const Ref<BulletWobbleData2D> &new_wobble_data) {
	shared_bullet_wobble_data = new_wobble_data;
	if ((int)all_bullet_wobble.size() != amount_bullets || (int)all_bullet_wobble_data.size() != amount_bullets) {
		return;
	}
	// Shared is the fallback: re-seed only slots without a live per-bullet
	// resource. Clearing shared (null) deactivates exactly those slots.
	for (int i = 0; i < amount_bullets; ++i) {
		if (all_bullet_wobble_data[i].is_valid()) {
			continue;
		}
		if (new_wobble_data.is_valid()) {
			all_bullet_wobble[i] = make_wobble_seed(new_wobble_data, i);
		} else {
			all_bullet_wobble[i] = WobbleSeed();
		}
	}
	refresh_wobble_feature_flag();
}

void BulletVolley2D::remove_shared_bullet_wobble_data() {
	set_shared_bullet_wobble_data(Ref<BulletWobbleData2D>());
}

} // namespace BlastBullets2D
