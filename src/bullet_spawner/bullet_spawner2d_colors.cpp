// Bullet Colors group: one color per bullet picked at shot time from a
// gradient (empty = a rainbow), handed to the factory next to the shot's
// transforms (BulletFactory2D::spawn_volley_span colors). The spawn data is
// never touched; a shot without a mode draws the data's own colors.

#include "bullet_spawner/bullet_spawner2d.hpp"
#include "bullet_spawner/pattern_spawner2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletSpawner2D::set_bullet_color_mode(BulletColorMode value) {
	if (value < COLOR_MODE_OFF || value > COLOR_MODE_GRADIENT_PER_SHOT) {
		UtilityFunctions::push_error("BulletSpawner2D: bullet_color_mode must be 0 (Off), 1 (Gradient Across Pattern), 2 (Random From Gradient) or 3 (Gradient Per Shot), keeping the old value.");
		return;
	}
	bullet_color_mode = value;
	notify_property_list_changed(); // the gradient and the mode knobs gate on it
}

void BulletSpawner2D::set_bullet_color_gradient(const Ref<Gradient> &value) {
	bullet_color_gradient = value;
}

void BulletSpawner2D::set_bullet_color_shift_per_shot(double value) {
	if (!Math::is_finite(value)) {
		UtilityFunctions::push_error("BulletSpawner2D: bullet_color_shift_per_shot must be finite, keeping the old value.");
		return;
	}
	bullet_color_shift_per_shot = value;
}

void BulletSpawner2D::set_bullet_color_cycle_shots(int value) {
	if (value < 1) {
		UtilityFunctions::push_error("BulletSpawner2D: bullet_color_cycle_shots must be >= 1, keeping the old value.");
		return;
	}
	bullet_color_cycle_shots = value;
}

Color BulletSpawner2D::sample_bullet_color(double t) const {
	const double at = Math::clamp(Math::is_finite(t) ? t : 0.0, 0.0, 1.0);
	// Red through magenta: the ends differ, so a ring shows every hue once.
	Color c = bullet_color_gradient.is_valid() ? bullet_color_gradient->sample((float)at) : Color::from_hsv((float)(at * (5.0 / 6.0)), 1.0f, 1.0f);
	if (!Math::is_finite(c.r) || !Math::is_finite(c.g) || !Math::is_finite(c.b) || !Math::is_finite(c.a)) {
		c = Color(1, 1, 1, 1); // a gradient holding a NaN point never reaches the renderer
	}
	return c;
}

void BulletSpawner2D::roll_shot_colors(size_t count) {
	shot_colors.clear();
	if (bullet_color_mode == COLOR_MODE_OFF || count == 0) {
		return;
	}
	shot_colors.resize(count);
	const uint64_t shot = (uint64_t)cadence.volleys_fired;
	switch (bullet_color_mode) {
		case COLOR_MODE_GRADIENT_ACROSS_PATTERN: {
			if (bullet_color_shift_per_shot == 0.0) {
				const double last = count > 1 ? (double)(count - 1) : 1.0;
				for (size_t i = 0; i < count; ++i) {
					shot_colors[i] = sample_bullet_color((double)i / last);
				}
			} else {
				// The fraction first: shots x shift never overflows into INF.
				const double offset = Math::fposmod(Math::fposmod(bullet_color_shift_per_shot, 1.0) * (double)shot, 1.0);
				for (size_t i = 0; i < count; ++i) {
					shot_colors[i] = sample_bullet_color(Math::fposmod((double)i / (double)count + offset, 1.0));
				}
			}
		} break;
		case COLOR_MODE_RANDOM_FROM_GRADIENT: {
			for (size_t i = 0; i < count; ++i) {
				shot_colors[i] = sample_bullet_color(roll_unit(CHANNEL_COLOR, (uint64_t)i));
			}
		} break;
		case COLOR_MODE_GRADIENT_PER_SHOT: {
			const uint64_t cycle = (uint64_t)bullet_color_cycle_shots;
			const double t = cycle > 1 ? (double)(shot % cycle) / (double)(cycle - 1) : 0.0;
			const Color c = sample_bullet_color(t);
			for (size_t i = 0; i < count; ++i) {
				shot_colors[i] = c;
			}
		} break;
		default:
			shot_colors.clear();
			break;
	}
}

PackedColorArray BulletSpawner2D::collect_shot_colors(int count) const {
	PackedColorArray out;
	if (count <= 0 || bullet_color_mode == COLOR_MODE_OFF) {
		return out;
	}
	// The same rolls as the next shot; the scratch is restored so a shot in
	// progress (a handler calling this) keeps its own colors.
	BulletSpawner2D *self = const_cast<BulletSpawner2D *>(this);
	std::vector<Color> kept;
	kept.swap(self->shot_colors);
	self->roll_shot_colors((size_t)Math::min(count, pattern_max_bullets()));
	out.resize((int64_t)self->shot_colors.size());
	for (size_t i = 0; i < self->shot_colors.size(); ++i) {
		out.set((int64_t)i, self->shot_colors[i]);
	}
	self->shot_colors.swap(kept);
	return out;
}

} // namespace BlastBullets2D
