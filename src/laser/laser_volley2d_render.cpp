// LaserVolley2D drawing: one canvas item per laser, redrawn after every tick
// (and every rendered frame while physics interpolation is active, lerping
// last tick's geometry to this tick's: a sweeping beam stays on its muzzle
// mid-frame). Every body of a laser is ONE triangle array (a strip along each
// beam: stretched or tiled body frame, the color ramp along the length; no
// per-segment polygon, no triangulation); the charge, muzzle, tip, pierce and
// wall-impact animations play on their own clocks, drawn over the bodies.
// debug_get_draw_state reports exactly what _draw uses.

#include "laser/laser_volley2d.hpp"

#include "core/transform_math2d.hpp"
#include "factory/bullet_factory2d.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/rendering_server.hpp>

#include <cmath>
#include <cstring>

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

// The bodies of one laser, built on the CPU and handed over in one call.
struct LaserBodyMesh2D {
	std::vector<Vector2> points;
	std::vector<Vector2> uvs;
	std::vector<Color> colors;
	std::vector<int32_t> indices;
	void clear() {
		points.clear();
		uvs.clear();
		colors.clear();
		indices.clear();
	}
};

// What the point animations of a beam need after its body was built.
struct LaserBeamMarks2D {
	const LaserVolley2D::Beam *beam;
	Vector2 front;
	Vector2 back;
	real_t angle; // the first segment's
	real_t tip_angle; // the last segment's
};

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

struct BodyStyle2D {
	const LaserData2D *data;
	real_t half; // half the width
	bool tile;
	real_t tex_w;
	real_t scroll;
};

// One beam's strip: a left/right vertex pair per point (v = 0 left, 1
// right), joined at a miter where the beam turns by at most 90 degrees and
// split where it turns sharper (a reflection). A segment is cut into pieces
// for the color ramp (8 over the whole length, at least one per segment).
void append_body_strip(LaserBodyMesh2D &m, const std::vector<Vector2> &pts, const BodyStyle2D &s) {
	real_t total = 0.0;
	for (size_t k = 1; k < pts.size(); ++k) {
		total += pts[k - 1].distance_to(pts[k]);
	}
	if (!(total > (real_t)0.0)) {
		return;
	}
	const LaserData2D *d = s.data;
	const bool ramp = d->color_ramp.is_valid();
	auto pair = [&](const Vector2 &at, const Vector2 &offset, real_t dist, bool connect) {
		const int32_t base = (int32_t)m.points.size();
		m.points.push_back(at + offset);
		m.points.push_back(at - offset);
		const real_t t = dist / total;
		Color col = d->modulate;
		if (ramp) {
			col = col * d->color_ramp->sample(t);
		}
		m.colors.push_back(col);
		m.colors.push_back(col);
		const real_t u = s.tile ? (dist + s.scroll) / s.tex_w : t;
		m.uvs.push_back(Vector2(u, 0.0));
		m.uvs.push_back(Vector2(u, 1.0));
		if (connect) {
			const int32_t quad[6] = { base - 2, base - 1, base, base - 1, base + 1, base };
			m.indices.insert(m.indices.end(), quad, quad + 6);
		}
	};
	bool started = false;
	Vector2 prev_axis;
	Vector2 prev_normal;
	Vector2 last_end;
	real_t walked = 0.0;
	for (size_t k = 1; k < pts.size(); ++k) {
		const Vector2 a = pts[k - 1];
		const Vector2 c = pts[k];
		const real_t seg = a.distance_to(c);
		if (!(seg > (real_t)0.0)) {
			continue;
		}
		const Vector2 axis = (c - a) / seg;
		const Vector2 normal(-axis.y, axis.x);
		if (!started) {
			pair(a, normal * s.half, walked, false);
			started = true;
		} else {
			const real_t cos_turn = prev_axis.x * axis.x + prev_axis.y * axis.y;
			if (cos_turn >= (real_t)0.0) {
				// |n0 + n1|^2 = 2 + 2 cos: the miter reaches half / cos(turn / 2).
				pair(a, (prev_normal + normal) * (s.half / ((real_t)1.0 + cos_turn)), walked, true);
			} else {
				pair(a, prev_normal * s.half, walked, true);
				pair(a, normal * s.half, walked, false);
			}
		}
		const int pieces = ramp ? (int)Math::clamp(std::ceil((double)((real_t)8.0 * seg / total)), 1.0, 8.0) : 1;
		for (int p = 1; p < pieces; ++p) {
			const real_t along = seg * (real_t)p / (real_t)pieces;
			pair(a + axis * along, normal * s.half, walked + along, true);
		}
		walked += seg;
		prev_axis = axis;
		prev_normal = normal;
		last_end = c;
	}
	if (started) {
		pair(last_end, prev_normal * s.half, walked, true);
	}
}

// Main thread only; plain C++ storage (no Godot type outlives the engine).
LaserBodyMesh2D &body_mesh_scratch() {
	static LaserBodyMesh2D mesh;
	return mesh;
}

std::vector<LaserBeamMarks2D> &beam_marks_scratch() {
	static std::vector<LaserBeamMarks2D> marks;
	return marks;
}

} // namespace

void LaserVolley2D::build_body_mesh(double fraction, LaserBodyMesh2D &r_mesh, std::vector<LaserBeamMarks2D> *r_marks) const {
	r_mesh.clear();
	if (r_marks != nullptr) {
		r_marks->clear();
	}
	const LaserData2D *d = data.ptr();
	if (d == nullptr || !active || phase == PHASE_FINISHED || phase == PHASE_CHARGING) {
		return;
	}
	const real_t width = get_current_width();
	Texture2D *body = anim_body.texture_at(clock - grow_started);
	BodyStyle2D style;
	style.data = d;
	style.half = width * (real_t)0.5;
	style.tile = d->body_mode == LaserData2D::BODY_TILE && body != nullptr;
	style.tex_w = body != nullptr ? Math::max((real_t)1.0, body->get_size().x) : (real_t)1.0;
	style.scroll = (real_t)(-d->body_scroll_speed * (clock - grow_started));
	const bool bodies = width > (real_t)0.0;
	std::vector<Vector2> pts;
	for (const Beam &b : beams) {
		if (b.spent) {
			continue;
		}
		interpolated_points(b, fraction, pts);
		if (pts.size() < 2) {
			continue;
		}
		if (bodies) {
			append_body_strip(r_mesh, pts, style);
		}
		if (r_marks != nullptr) {
			r_marks->push_back({ &b, pts.front(), pts.back(), (real_t)(pts[1] - pts[0]).angle(), (real_t)(pts[pts.size() - 1] - pts[pts.size() - 2]).angle() });
		}
	}
}

void LaserVolley2D::_draw() {
	const LaserData2D *d = data.ptr();
	if (d == nullptr || !active || phase == PHASE_FINISHED) {
		return;
	}
	const double fraction = draw_fraction();
	const real_t effects = (real_t)d->effects_scale;
	if (phase == PHASE_CHARGING) {
		std::vector<Vector2> pts;
		for (const Beam &b : beams) {
			if (b.spent) {
				continue;
			}
			interpolated_points(b, fraction, pts);
			if (pts.size() < 2) {
				continue;
			}
			if (d->charge_sec > 0.0 && d->warning_width > 0.0) {
				PackedVector2Array line;
				line.resize((int64_t)pts.size());
				std::memcpy(line.ptrw(), pts.data(), pts.size() * sizeof(Vector2));
				draw_polyline(line, d->warning_color, (real_t)d->warning_width);
			}
			draw_point_frame(this, anim_charge.texture_at(clock), pts[0], (real_t)(pts[1] - pts[0]).angle(), effects, d->modulate);
		}
		return;
	}
	LaserBodyMesh2D &mesh = body_mesh_scratch();
	std::vector<LaserBeamMarks2D> &marks = beam_marks_scratch();
	build_body_mesh(fraction, mesh, &marks);
	if (!mesh.indices.empty()) {
		PackedInt32Array indices;
		PackedVector2Array points;
		PackedColorArray colors;
		PackedVector2Array uvs;
		indices.resize((int64_t)mesh.indices.size());
		points.resize((int64_t)mesh.points.size());
		colors.resize((int64_t)mesh.colors.size());
		uvs.resize((int64_t)mesh.uvs.size());
		std::memcpy(indices.ptrw(), mesh.indices.data(), mesh.indices.size() * sizeof(int32_t));
		std::memcpy(points.ptrw(), mesh.points.data(), mesh.points.size() * sizeof(Vector2));
		std::memcpy(colors.ptrw(), mesh.colors.data(), mesh.colors.size() * sizeof(Color));
		std::memcpy(uvs.ptrw(), mesh.uvs.data(), mesh.uvs.size() * sizeof(Vector2));
		Texture2D *body = anim_body.texture_at(clock - grow_started);
		RenderingServer::get_singleton()->canvas_item_add_triangle_array(get_canvas_item(), indices, points, colors, uvs, PackedInt32Array(), PackedFloat32Array(), body != nullptr ? body->get_rid() : RID());
	}
	for (const LaserBeamMarks2D &mk : marks) {
		const Beam &b = *mk.beam;
		if (d->type == LaserData2D::LASER_BEAM) {
			draw_point_frame(this, anim_muzzle.texture_at(clock - grow_started), mk.front, mk.angle, effects, d->modulate);
		}
		draw_point_frame(this, anim_tip.texture_at(clock - grow_started), mk.back, mk.tip_angle, effects, d->modulate);
		for (const Contact &c : b.contacts) {
			draw_point_frame(this, anim_pierce.texture_at(clock - c.started), c.position, mk.angle, effects, d->modulate);
		}
		for (const Vector2 &impact : b.impacts) {
			draw_point_frame(this, anim_wall.texture_at(clock - b.blocked_since), impact, mk.angle, effects, d->modulate);
		}
	}
	mesh.clear();
	marks.clear();
}

Dictionary LaserVolley2D::debug_get_body_mesh(double fraction) const {
	LaserBodyMesh2D mesh;
	build_body_mesh(fraction < 0.0 ? draw_fraction() : fraction, mesh, nullptr);
	PackedVector2Array points;
	PackedVector2Array uvs;
	PackedColorArray colors;
	PackedInt32Array indices;
	points.resize((int64_t)mesh.points.size());
	uvs.resize((int64_t)mesh.uvs.size());
	colors.resize((int64_t)mesh.colors.size());
	indices.resize((int64_t)mesh.indices.size());
	if (!mesh.points.empty()) {
		std::memcpy(points.ptrw(), mesh.points.data(), mesh.points.size() * sizeof(Vector2));
		std::memcpy(uvs.ptrw(), mesh.uvs.data(), mesh.uvs.size() * sizeof(Vector2));
		std::memcpy(colors.ptrw(), mesh.colors.data(), mesh.colors.size() * sizeof(Color));
	}
	if (!mesh.indices.empty()) {
		std::memcpy(indices.ptrw(), mesh.indices.data(), mesh.indices.size() * sizeof(int32_t));
	}
	Dictionary out;
	out["points"] = points;
	out["uvs"] = uvs;
	out["colors"] = colors;
	out["indices"] = indices;
	return out;
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
		s["charge"] = charging ? Variant(anim_charge.texture_at(clock)) : Variant();
		s["body"] = !charging ? Variant(anim_body.texture_at(clock - grow_started)) : Variant();
		s["muzzle"] = !charging ? Variant(anim_muzzle.texture_at(clock - grow_started)) : Variant();
		s["tip"] = !charging ? Variant(anim_tip.texture_at(clock - grow_started)) : Variant();
		Array pierces;
		for (const Contact &c : b.contacts) {
			Array entry;
			entry.push_back(c.position);
			entry.push_back(Variant(anim_pierce.texture_at(clock - c.started)));
			pierces.push_back(entry);
		}
		s["pierces"] = pierces;
		Array impacts;
		for (const Vector2 &impact : b.impacts) {
			Array entry;
			entry.push_back(impact);
			entry.push_back(Variant(anim_wall.texture_at(clock - b.blocked_since)));
			impacts.push_back(entry);
		}
		s["impacts"] = impacts;
		out.push_back(s);
	}
	return out;
}

} // namespace BlastBullets2D
