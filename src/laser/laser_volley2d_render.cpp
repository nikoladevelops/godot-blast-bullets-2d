// LaserVolley2D drawing: one canvas item per laser, redrawn after every tick
// (and every rendered frame while physics interpolation is active, lerping
// last tick's geometry to this tick's: a sweeping beam stays on its muzzle
// mid-frame). The body is one polygon per segment (stretched or tiled body
// frame, the color ramp along the length); the charge, muzzle, tip, pierce
// and wall-impact animations play on their own clocks.
// debug_get_draw_state reports exactly what _draw uses.

#include "laser/laser_volley2d.hpp"

#include "core/transform_math2d.hpp"
#include "factory/bullet_factory2d.hpp"

#include <godot_cpp/classes/engine.hpp>

using namespace godot;

namespace BlastBullets2D {

double LaserVolley2D::draw_fraction() const {
	if (factory != nullptr && factory_life != nullptr && *factory_life && factory->is_physics_interpolation_active()) {
		return Engine::get_singleton()->get_physics_interpolation_fraction();
	}
	return 1.0;
}

void LaserVolley2D::interpolated_points(const Beam &b, double fraction, std::vector<Vector2> &r_points) const {
	const real_t f = (real_t)Math::clamp(fraction, 0.0, 1.0);
	if (f >= (real_t)1.0 || b.prev_points.empty()) {
		r_points = b.points;
		return;
	}
	const LaserData2D *d = data.ptr();
	const bool straight = d != nullptr && d->type == LaserData2D::LASER_BEAM && b.points.size() == 2 && b.prev_points.size() == 2;
	if (straight) {
		// Muzzle, angle and length: a beam turning on its muzzle stays on it.
		const Vector2 muzzle = b.prev_points[0].lerp(b.points[0], f);
		const real_t angle = (real_t)Math::lerp_angle((double)b.prev_angle, (double)b.angle, (double)f);
		const real_t length = Math::lerp(b.prev_length, b.length_now, f);
		r_points.assign({ muzzle, muzzle + Vector2(Math::cos(angle), Math::sin(angle)) * length });
		return;
	}
	if (b.prev_points.size() == b.points.size()) {
		r_points.resize(b.points.size());
		for (size_t k = 0; k < b.points.size(); ++k) {
			r_points[k] = b.prev_points[k].lerp(b.points[k], f);
		}
		return;
	}
	r_points = b.points;
}

namespace {

// The centered, turned, scaled point animation at `pos`.
void draw_point_frame(CanvasItem *ci, Texture2D *tex, const Vector2 &pos, real_t angle, real_t scale, const Color &color) {
	if (tex == nullptr) {
		return;
	}
	const Vector2 size = tex->get_size();
	ci->draw_set_transform(pos, angle, Vector2(scale, scale));
	ci->draw_texture(tex, -size * (real_t)0.5, color);
	ci->draw_set_transform(Vector2(), 0.0, Vector2(1, 1));
}

} // namespace

void LaserVolley2D::_draw() {
	const LaserData2D *d = data.ptr();
	if (d == nullptr || !active || phase == PHASE_FINISHED) {
		return;
	}
	const double fraction = draw_fraction();
	const real_t width = get_current_width();
	const real_t effects = (real_t)d->effects_scale;
	std::vector<Vector2> pts;
	for (const Beam &b : beams) {
		if (b.spent) {
			continue;
		}
		interpolated_points(b, fraction, pts);
		if (pts.size() < 2) {
			continue;
		}
		const real_t angle = (real_t)(pts[1] - pts[0]).angle();
		if (phase == PHASE_CHARGING) {
			if (d->charge_sec > 0.0 && d->warning_width > 0.0) {
				PackedVector2Array line;
				for (const Vector2 &p : pts) {
					line.push_back(p);
				}
				draw_polyline(line, d->warning_color, (real_t)d->warning_width);
			}
			draw_point_frame(this, anim_charge.at(clock), pts[0], angle, effects, d->modulate);
			continue;
		}
		if (width > (real_t)0.0) {
			// The body: one polygon per segment, subdivided for the ramp.
			real_t total = 0.0;
			for (size_t k = 1; k < pts.size(); ++k) {
				total += pts[k - 1].distance_to(pts[k]);
			}
			Texture2D *body = anim_body.at(clock - grow_started);
			const bool tile = d->body_mode == LaserData2D::BODY_TILE && body != nullptr;
			const real_t tex_w = body != nullptr ? Math::max((real_t)1.0, body->get_size().x) : (real_t)1.0;
			const real_t scroll = (real_t)(-d->body_scroll_speed * (clock - grow_started));
			const int pieces = d->color_ramp.is_valid() ? 8 : 1;
			const real_t half = width * (real_t)0.5;
			real_t walked = 0.0;
			for (size_t k = 1; k < pts.size(); ++k) {
				const Vector2 a = pts[k - 1];
				const Vector2 c = pts[k];
				const real_t seg = a.distance_to(c);
				if (!(seg > (real_t)0.0)) {
					continue;
				}
				const Vector2 axis = (c - a) / seg;
				const Vector2 side(-axis.y * half, axis.x * half);
				PackedVector2Array poly;
				PackedColorArray colors;
				PackedVector2Array uvs;
				// Left side forward, right side back: one simple outline.
				for (int side_pass = 0; side_pass < 2; ++side_pass) {
					for (int p = 0; p <= pieces; ++p) {
						const int q = side_pass == 0 ? p : pieces - p;
						const real_t along = seg * (real_t)q / (real_t)pieces;
						const real_t dist = walked + along;
						const Vector2 at = a + axis * along;
						poly.push_back(side_pass == 0 ? at + side : at - side);
						const real_t t = total > (real_t)0.0 ? dist / total : (real_t)0.0;
						Color col = d->modulate;
						if (d->color_ramp.is_valid()) {
							col = col * d->color_ramp->sample(t);
						}
						colors.push_back(col);
						const real_t u = tile ? (dist + scroll) / tex_w : t;
						uvs.push_back(Vector2(u, side_pass == 0 ? (real_t)0.0 : (real_t)1.0));
					}
				}
				if (body != nullptr) {
					draw_polygon(poly, colors, uvs, body);
				} else {
					draw_polygon(poly, colors);
				}
				walked += seg;
			}
		}
		if (d->type == LaserData2D::LASER_BEAM) {
			draw_point_frame(this, anim_muzzle.at(clock - grow_started), pts.front(), angle, effects, d->modulate);
		}
		draw_point_frame(this, anim_tip.at(clock - grow_started), pts.back(), (real_t)(pts[pts.size() - 1] - pts[pts.size() - 2]).angle(), effects, d->modulate);
		for (const Contact &c : b.contacts) {
			draw_point_frame(this, anim_pierce.at(clock - c.started), c.position, angle, effects, d->modulate);
		}
		for (const Vector2 &impact : b.impacts) {
			draw_point_frame(this, anim_wall.at(clock - b.blocked_since), impact, angle, effects, d->modulate);
		}
	}
}

Array LaserVolley2D::debug_get_draw_state(double fraction) const {
	Array out;
	const LaserData2D *d = data.ptr();
	if (d == nullptr || !active) {
		return out;
	}
	const double f = fraction < 0.0 ? draw_fraction() : fraction;
	std::vector<Vector2> pts;
	const bool charging = phase == PHASE_CHARGING;
	for (const Beam &b : beams) {
		interpolated_points(b, f, pts);
		Dictionary s;
		s["spent"] = b.spent;
		PackedVector2Array line;
		for (const Vector2 &p : pts) {
			line.push_back(p);
		}
		s["points"] = line;
		s["warning"] = charging && d->charge_sec > 0.0 && d->warning_width > 0.0;
		s["width"] = charging ? (real_t)0.0 : get_current_width();
		s["color"] = d->modulate;
		s["charge"] = charging ? Variant(anim_charge.at(clock)) : Variant();
		s["body"] = !charging ? Variant(anim_body.at(clock - grow_started)) : Variant();
		s["muzzle"] = !charging ? Variant(anim_muzzle.at(clock - grow_started)) : Variant();
		s["tip"] = !charging ? Variant(anim_tip.at(clock - grow_started)) : Variant();
		Array pierces;
		for (const Contact &c : b.contacts) {
			Array entry;
			entry.push_back(c.position);
			entry.push_back(Variant(anim_pierce.at(clock - c.started)));
			pierces.push_back(entry);
		}
		s["pierces"] = pierces;
		Array impacts;
		for (const Vector2 &impact : b.impacts) {
			Array entry;
			entry.push_back(impact);
			entry.push_back(Variant(anim_wall.at(clock - b.blocked_since)));
			impacts.push_back(entry);
		}
		s["impacts"] = impacts;
		out.push_back(s);
	}
	return out;
}

} // namespace BlastBullets2D
