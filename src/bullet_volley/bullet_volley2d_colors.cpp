// Per-bullet colors: the MultiMesh color format (stride 12 instead of 8),
// seeding from spawn data and the runtime color API. Colors are stored in the
// batch buffer itself (floats 8..11 of each instance): the transform passes
// rewrite only the first 8 floats, so every upload carries the colors along
// at no extra cost.

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

static bool color_is_finite2d(const Color &c) {
	return Math::is_finite(c.r) && Math::is_finite(c.g) && Math::is_finite(c.b) && Math::is_finite(c.a);
}

static void write_color2d(float *at, const Color &c) {
	at[0] = c.r;
	at[1] = c.g;
	at[2] = c.b;
	at[3] = c.a;
}

void BulletVolley2D::set_up_colors_format(bool use_colors) {
	if (multi.is_null()) {
		return;
	}
	if (multi->is_using_colors() != use_colors) {
		const int count = multi->get_instance_count();
		if (count > 0) {
			multi->set_instance_count(0); // Godot refuses a format change while instances exist
		}
		multi->set_use_colors(use_colors);
		if (count > 0) {
			multi->set_instance_count(count);
		}
	}
	colors_enabled = use_colors;
	buffer_stride = use_colors ? 12 : 8;
}

void BulletVolley2D::seed_bullet_colors(const BulletVolleyData2D &data, const Color *span, float *w) {
	if (span != nullptr) {
		for (int i = 0; i < amount_bullets; ++i) {
			write_color2d(w + i * 12 + 8, span[i]);
		}
		return;
	}
	const PackedColorArray &colors = data.bullet_colors;
	const int size = colors.size();
	const bool tile = data.tile_bullet_colors;
	if (size > 0 && size != amount_bullets) {
		WarnOnce2D::warn(warn_data_id, 12u, size, amount_bullets, "BulletVolley2D: bullet_colors size (" + String::num_int64(size) + ") != bullets (" + String::num_int64(amount_bullets) + "); uncovered bullets are white" + String(tile ? " (tiling on: wrapping short array)." : " (check tile_bullet_colors to wrap, or provide one entry per bullet)."));
	}
	const Color white(1, 1, 1, 1);
	for (int i = 0; i < amount_bullets; ++i) {
		const int src = (tile && size > 0) ? (i % size) : i;
		write_color2d(w + i * 12 + 8, src < size ? colors[src] : white);
	}
}

Color BulletVolley2D::get_bullet_color(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "get_bullet_color")) {
		return Color(1, 1, 1, 1);
	}
	if (!colors_enabled || (int)batch_buffer.size() != amount_bullets * 12) {
		return Color(1, 1, 1, 1); // no per-bullet colors: every bullet is drawn untinted
	}
	const float *r = batch_buffer.ptr() + bullet_index * 12 + 8;
	return Color(r[0], r[1], r[2], r[3]);
}

void BulletVolley2D::set_bullet_color(int bullet_index, const Color &new_color) {
	if (!validate_bullet_index(bullet_index, "set_bullet_color")) {
		return;
	}
	if (!colors_enabled) {
		UtilityFunctions::push_error("set_bullet_color: this volley has no per-bullet colors (spawn it with BulletVolleyData2D.use_bullet_colors on), keeping the old color.");
		return;
	}
	if (!color_is_finite2d(new_color)) {
		UtilityFunctions::push_error("set_bullet_color: new_color must be finite, keeping the old color.");
		return;
	}
	if ((int)batch_buffer.size() != amount_bullets * 12 || multi.is_null()) {
		return;
	}
	write_color2d(batch_buffer.ptrw() + bullet_index * 12 + 8, new_color);
	// Shown at once (a paused factory or a still bullet never uploads the
	// buffer); the next upload carries the same color.
	multi->set_instance_color(bullet_index, new_color);
}

PackedColorArray BulletVolley2D::all_bullets_get_color(int bullet_index_start, int bullet_index_end_inclusive) const {
	return collect_range<PackedColorArray>(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_color", [&](int i) { return get_bullet_color(i); });
}

void BulletVolley2D::all_bullets_set_color(const Color &new_color, int bullet_index_start, int bullet_index_end_inclusive) {
	// The range contract first (like every all_bullets_* method), then one
	// refusal for the whole call, not one per bullet.
	if (!ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_color")) {
		return;
	}
	if (bullet_index_start > bullet_index_end_inclusive) {
		return;
	}
	if (!colors_enabled) {
		UtilityFunctions::push_error("all_bullets_set_color: this volley has no per-bullet colors (spawn it with BulletVolleyData2D.use_bullet_colors on), keeping the old colors.");
		return;
	}
	if (!color_is_finite2d(new_color)) {
		UtilityFunctions::push_error("all_bullets_set_color: new_color must be finite, keeping the old colors.");
		return;
	}
	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		set_bullet_color(i, new_color);
	}
}

void BulletVolley2D::all_bullets_set_colors(const PackedColorArray &new_colors, bool tile) {
	if (!colors_enabled) {
		UtilityFunctions::push_error("all_bullets_set_colors: this volley has no per-bullet colors (spawn it with BulletVolleyData2D.use_bullet_colors on), keeping the old colors.");
		return;
	}
	const int size = new_colors.size();
	for (int i = 0; i < size; ++i) {
		if (!color_is_finite2d(new_colors[i])) {
			UtilityFunctions::push_error("all_bullets_set_colors: new_colors must hold finite colors, keeping the old colors.");
			return;
		}
	}
	if (size == 0 || (int)batch_buffer.size() != amount_bullets * 12 || multi.is_null()) {
		return;
	}
	float *w = batch_buffer.ptrw();
	const int covered = tile ? amount_bullets : MIN(size, amount_bullets);
	for (int i = 0; i < covered; ++i) {
		const Color &c = new_colors[i % size];
		write_color2d(w + i * 12 + 8, c);
		// Per instance, never a whole-buffer upload: the buffer's transforms
		// may predate a teleport or a disable written straight to the server.
		multi->set_instance_color(i, c);
	}
}

} // namespace BlastBullets2D
