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

namespace {

real_t polyline_length2d(const std::vector<Vector2> &pts) {
	real_t total = 0.0;
	for (size_t k = 1; k < pts.size(); ++k) {
		total += pts[k - 1].distance_to(pts[k]);
	}
	return total;
}

// The piece of `pts` between arc lengths `from` and `to` (clamped).
void sub_polyline2d(const std::vector<Vector2> &pts, real_t from, real_t to, std::vector<Vector2> &r_out) {
	r_out.clear();
	if (pts.empty()) {
		return;
	}
	real_t walked = 0.0;
	bool started = false;
	for (size_t k = 1; k < pts.size(); ++k) {
		const real_t seg = pts[k - 1].distance_to(pts[k]);
		const real_t seg_end = walked + seg;
		if (!started && from <= seg_end) {
			const real_t t = seg > (real_t)0.0 ? Math::clamp((from - walked) / seg, (real_t)0.0, (real_t)1.0) : (real_t)0.0;
			r_out.push_back(pts[k - 1].lerp(pts[k], t));
			started = true;
		}
		if (started) {
			if (to <= seg_end) {
				const real_t t = seg > (real_t)0.0 ? Math::clamp((to - walked) / seg, (real_t)0.0, (real_t)1.0) : (real_t)1.0;
				r_out.push_back(pts[k - 1].lerp(pts[k], t));
				return;
			}
			r_out.push_back(pts[k]);
		}
		walked = seg_end;
	}
	if (!started) {
		r_out.push_back(pts.back());
	}
	if (r_out.size() < 2) {
		r_out.push_back(r_out.back());
	}
}

// The facing of the polyline segment nearest to `at`.
real_t polyline_angle_at2d(const std::vector<Vector2> &pts, const Vector2 &at) {
	real_t best = -1.0;
	real_t angle = 0.0;
	for (size_t k = 1; k < pts.size(); ++k) {
		const Vector2 a = pts[k - 1];
		const Vector2 ab = pts[k] - a;
		const real_t len2 = ab.x * ab.x + ab.y * ab.y;
		if (!(len2 > (real_t)0.0)) {
			continue;
		}
		const real_t t = Math::clamp(((at - a).x * ab.x + (at - a).y * ab.y) / len2, (real_t)0.0, (real_t)1.0);
		const real_t dist = (a + ab * t).distance_squared_to(at);
		if (best < (real_t)0.0 || dist < best) {
			best = dist;
			angle = ab.angle();
		}
	}
	return angle;
}

} // namespace

void LaserVolley2D::interpolated_points(const Beam &b, double fraction, std::vector<Vector2> &r_points, bool *r_aligned) const {
	if (r_aligned != nullptr) {
		*r_aligned = true;
	}
	const real_t f = (real_t)Math::clamp(fraction, 0.0, 1.0);
	if (f >= (real_t)1.0 || b.prev_points.empty() || b.points.empty()) {
		r_points = b.points;
		return;
	}
	const LaserData2D *d = data.ptr();
	if (d != nullptr && d->type != LaserData2D::LASER_BEAM) {
		// A bolt or curvy body is the newest stretch of the path its head
		// flew: last tick's body plus what the head flew since. Its tail and
		// its head each glide along that path, so a growing trail, a
		// reflection or a turn never snaps (they used to while the point
		// count changed: half a second for a homing curvy laser).
		size_t joint = b.points.size();
		for (size_t k = b.points.size(); k-- > 0;) {
			if (b.points[k] == b.prev_points.back()) {
				joint = k;
				break;
			}
		}
		if (joint < b.points.size()) {
			std::vector<Vector2> &path = interp_path_scratch;
			path = b.prev_points;
			path.insert(path.end(), b.points.begin() + (std::ptrdiff_t)joint + 1, b.points.end());
			const real_t prev_len = polyline_length2d(b.prev_points);
			const real_t total = polyline_length2d(path);
			const real_t now_len = polyline_length2d(b.points);
			const real_t from = Math::lerp((real_t)0.0, Math::max((real_t)0.0, total - now_len), f);
			const real_t to = Math::lerp(prev_len, total, f);
			sub_polyline2d(path, from, to, r_points);
			if (r_aligned != nullptr) {
				*r_aligned = false;
			}
			return;
		}
		r_points = b.points; // the head jumped (no shared path): this tick's
		return;
	}
	const bool straight = b.points.size() == 2 && b.prev_points.size() == 2;
	if (straight) {
		// Muzzle, angle and length: a beam turning on its muzzle stays on it.
		const Vector2 muzzle = b.prev_points[0].lerp(b.points[0], f);
		const real_t angle = (real_t)Math::lerp_angle((double)b.prev_angle, (double)b.angle, (double)f);
		const real_t length = Math::lerp(b.prev_length, b.length_now, f);
		r_points.assign({ muzzle, muzzle + Vector2(Math::cos(angle), Math::sin(angle)) * length });
		return;
	}
	// Point by point. A reflection that appeared or vanished this tick grows
	// from (or shrinks into) the end of the shorter polyline instead of
	// snapping.
	const size_t n = b.points.size();
	r_points.resize(n);
	for (size_t k = 0; k < n; ++k) {
		const Vector2 from = b.prev_points[Math::min(k, b.prev_points.size() - 1)];
		r_points[k] = from.lerp(b.points[k], f);
	}
}

real_t LaserVolley2D::draw_width(double fraction) const {
	const real_t now = get_current_width();
	const real_t f = (real_t)Math::clamp(fraction, 0.0, 1.0);
	return f >= (real_t)1.0 ? now : Math::lerp(prev_width, now, f);
}

void LaserVolley2D::snap_beam_history(Beam &b) {
	b.prev_muzzle = b.points.empty() ? b.muzzle : b.points.front();
	b.prev_angle = b.angle;
	b.prev_length = b.length_now;
	b.prev_points = b.points;
	b.prev_impacts = b.impacts;
	for (Contact &c : b.contacts) {
		c.prev_position = c.position;
		c.has_prev = true;
	}
}

void LaserVolley2D::build_draw_frame(const Beam &b, double fraction, BeamDrawFrame2D &r_frame) const {
	bool aligned = true;
	interpolated_points(b, fraction, r_frame.points, &aligned);
	r_frame.pierces.clear();
	r_frame.impacts.clear();
	const std::vector<Vector2> &pts = r_frame.points;
	const real_t f = (real_t)Math::clamp(fraction, 0.0, 1.0);
	for (const Contact &c : b.contacts) {
		DrawMark2D m;
		m.position = c.has_prev && f < (real_t)1.0 ? c.prev_position.lerp(c.position, f) : c.position;
		m.angle = polyline_angle_at2d(pts, m.position);
		m.since = c.started;
		r_frame.pierces.push_back(m);
	}
	for (size_t k = 0; k < b.impacts.size(); ++k) {
		const Vector2 &impact = b.impacts[k];
		DrawMark2D m;
		m.position = impact;
		m.since = k < b.impact_since.size() ? b.impact_since[k] : clock;
		// An impact is a vertex of the beam (a reflection or the stop): it
		// rides the interpolated vertex and faces along the beam arriving.
		size_t vertex = b.points.size();
		if (aligned) {
			for (size_t v = 1; v < b.points.size() && v < pts.size(); ++v) {
				if (b.points[v] == impact) {
					vertex = v;
					break;
				}
			}
		} else if (!b.points.empty() && b.points.back() == impact && pts.size() >= 2) {
			vertex = pts.size() - 1;
		}
		if (vertex < pts.size()) {
			m.position = pts[vertex];
			m.angle = (pts[vertex] - pts[vertex - 1]).angle();
		} else {
			if (k < b.prev_impacts.size() && f < (real_t)1.0) {
				m.position = b.prev_impacts[k].lerp(impact, f);
			}
			m.angle = polyline_angle_at2d(pts, m.position);
		}
		r_frame.impacts.push_back(m);
	}
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

namespace {

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

std::vector<LaserVolley2D::BeamDrawFrame2D> &draw_frames_scratch() {
	static std::vector<LaserVolley2D::BeamDrawFrame2D> frames;
	return frames;
}

std::vector<LaserVolley2D::LookMark2D> &look_marks_scratch() {
	static std::vector<LaserVolley2D::LookMark2D> marks;
	return marks;
}

} // namespace

void LaserVolley2D::build_body_mesh(double fraction, LaserBodyMesh2D &r_mesh, std::vector<BeamDrawFrame2D> *r_frames) const {
	r_mesh.clear();
	if (r_frames != nullptr) {
		r_frames->clear();
	}
	const LaserData2D *d = data.ptr();
	if (d == nullptr || !active || phase == PHASE_FINISHED || phase == PHASE_CHARGING) {
		return;
	}
	const real_t width = draw_width(fraction);
	Texture2D *body = anim_body.texture_at(clock - grow_started);
	// The scroll runs on the interpolated clock: a tiled body flows between
	// ticks instead of stepping each tick.
	const double draw_clock = clock - (1.0 - Math::clamp(fraction, 0.0, 1.0)) * last_tick_delta;
	BodyStyle2D style;
	style.data = d;
	style.half = width * (real_t)d->body_width_scale * (real_t)0.5;
	style.tile = d->body_mode == LaserData2D::BODY_TILE && body != nullptr;
	style.tex_w = body != nullptr ? Math::max((real_t)1.0, body->get_size().x) : (real_t)1.0;
	style.scroll = (real_t)(-d->body_scroll_speed * (draw_clock - grow_started));
	const bool bodies = style.half > (real_t)0.0;
	BeamDrawFrame2D local;
	for (const Beam &b : beams) {
		if (b.spent && (r_frames == nullptr || b.impacts.empty())) {
			continue;
		}
		BeamDrawFrame2D &frame = r_frames != nullptr ? r_frames->emplace_back() : local;
		build_draw_frame(b, fraction, frame);
		if (b.spent) {
			continue; // only its wall impact still plays (see _draw)
		}
		if (bodies && frame.points.size() >= 2) {
			append_body_strip(r_mesh, frame.points, style);
		}
	}
}

Texture2D *LaserVolley2D::look_texture(int slot, double t) const {
	const LookLayer2D &layer = look_layers[slot];
	const LaserSpriteLook2D *look = layer.look.ptr();
	return look != nullptr ? layer.anim.texture_at(t * look->speed_scale) : nullptr;
}

bool LaserVolley2D::look_still_playing(int slot, double t) const {
	const LookLayer2D &layer = look_layers[slot];
	const double speed = layer.look.is_valid() ? layer.look->speed_scale : 1.0;
	return t * speed < layer.anim.total;
}

void LaserVolley2D::collect_look_marks(double fraction, const std::vector<BeamDrawFrame2D> &frames, std::vector<LookMark2D> &r_marks) const {
	r_marks.clear();
	const LaserData2D *d = data.ptr();
	if (d == nullptr || !active || phase == PHASE_FINISHED || !look_layers[0].item.is_valid()) {
		return;
	}
	auto mark = [&](int slot, double t, const Vector2 &pos, real_t angle) {
		const LaserSpriteLook2D *look = look_layers[slot].look.ptr();
		if (look == nullptr || !look->visible) {
			return;
		}
		Texture2D *tex = look_texture(slot, t);
		if (tex == nullptr) {
			return;
		}
		LookMark2D m;
		m.slot = slot;
		m.pose = look->pose_at(pos, angle);
		m.texture = tex;
		m.color = d->modulate * look->modulate;
		r_marks.push_back(m);
	};
	if (phase == PHASE_CHARGING) {
		std::vector<Vector2> pts;
		for (const Beam &b : beams) {
			if (b.spent) {
				continue;
			}
			interpolated_points(b, fraction, pts);
			if (pts.size() >= 2) {
				mark(LaserData2D::LOOK_CHARGE, clock, pts[0], (pts[1] - pts[0]).angle());
			}
		}
		return;
	}
	size_t fi = 0;
	for (const Beam &b : beams) {
		if (b.spent && b.impacts.empty()) {
			continue;
		}
		if (fi >= frames.size()) {
			break;
		}
		const BeamDrawFrame2D &frame = frames[fi++];
		const std::vector<Vector2> &pts = frame.points;
		if (!b.spent && pts.size() >= 2) {
			if (d->type == LaserData2D::LASER_BEAM) {
				mark(LaserData2D::LOOK_MUZZLE, clock - grow_started, pts.front(), (pts[1] - pts[0]).angle());
			}
			mark(LaserData2D::LOOK_TIP, clock - grow_started, pts.back(), (pts[pts.size() - 1] - pts[pts.size() - 2]).angle());
			for (const DrawMark2D &m : frame.pierces) {
				mark(LaserData2D::LOOK_PIERCE, clock - m.since, m.position, m.angle);
			}
		}
		for (const DrawMark2D &m : frame.impacts) {
			// A head spent at a wall still plays its impact once.
			if (b.spent && !look_still_playing(LaserData2D::LOOK_WALL_IMPACT, clock - m.since)) {
				continue;
			}
			mark(LaserData2D::LOOK_WALL_IMPACT, clock - m.since, m.position, m.angle);
		}
	}
}

void LaserVolley2D::_draw() {
	clear_look_layers();
	const LaserData2D *d = data.ptr();
	if (d == nullptr || !active || phase == PHASE_FINISHED) {
		return;
	}
	const double fraction = draw_fraction();
	LaserBodyMesh2D &mesh = body_mesh_scratch();
	std::vector<BeamDrawFrame2D> &frames = draw_frames_scratch();
	std::vector<LookMark2D> &marks = look_marks_scratch();
	if (phase == PHASE_CHARGING) {
		if (d->charge_sec > 0.0 && d->warning_width > 0.0) {
			std::vector<Vector2> pts;
			for (const Beam &b : beams) {
				if (b.spent) {
					continue;
				}
				interpolated_points(b, fraction, pts);
				if (pts.size() < 2) {
					continue;
				}
				PackedVector2Array line;
				line.resize((int64_t)pts.size());
				std::memcpy(line.ptrw(), pts.data(), pts.size() * sizeof(Vector2));
				draw_polyline(line, d->warning_color, (real_t)d->warning_width);
			}
		}
		frames.clear();
	} else {
		build_body_mesh(fraction, mesh, &frames);
	}
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
	collect_look_marks(fraction, frames, marks);
	if (!marks.empty()) {
		RenderingServer *rs = RenderingServer::get_singleton();
		for (const LookMark2D &m : marks) {
			// Centered on its point; Texture2D::draw_rect draws an AtlasTexture's
			// region (the bake already made atlas frames plain textures).
			const RID item = look_layers[m.slot].item;
			const Vector2 size = m.texture->get_size();
			rs->canvas_item_add_set_transform(item, m.pose);
			m.texture->draw_rect(item, Rect2(-size * (real_t)0.5, size), false, m.color);
		}
	}
	mesh.clear();
	frames.clear();
	marks.clear();
}

Array LaserVolley2D::debug_get_look_draws(double fraction) const {
	Array out;
	const double f = fraction < 0.0 ? draw_fraction() : fraction;
	LaserBodyMesh2D mesh;
	std::vector<BeamDrawFrame2D> frames;
	if (phase != PHASE_CHARGING) {
		build_body_mesh(f, mesh, &frames);
	}
	std::vector<LookMark2D> marks;
	collect_look_marks(f, frames, marks);
	static const char *const slot_names[LaserData2D::LOOK_SLOT_COUNT] = { "charge", "muzzle", "tip", "pierce", "wall_impact" };
	for (const LookMark2D &m : marks) {
		Dictionary e;
		e["slot"] = slot_names[m.slot];
		e["transform"] = m.pose;
		e["texture"] = Variant(m.texture);
		e["color"] = m.color;
		out.push_back(e);
	}
	return out;
}

Array LaserVolley2D::debug_get_look_layers() const {
	Array out;
	for (const LookLayer2D &layer : look_layers) {
		const LaserSpriteLook2D *look = layer.look.ptr();
		Dictionary e;
		e["created"] = layer.item.is_valid();
		e["z_index"] = layer.z_index;
		e["z_as_relative"] = layer.z_as_relative;
		e["material"] = layer.material;
		e["visible"] = look != nullptr && look->visible;
		out.push_back(e);
	}
	return out;
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
	const bool charging = phase == PHASE_CHARGING;
	BeamDrawFrame2D frame;
	for (const Beam &b : beams) {
		build_draw_frame(b, f, frame);
		Dictionary s;
		s["spent"] = b.spent;
		PackedVector2Array line;
		for (const Vector2 &p : frame.points) {
			line.push_back(p);
		}
		s["points"] = line;
		s["warning"] = charging && d->charge_sec > 0.0 && d->warning_width > 0.0;
		s["width"] = charging ? (real_t)0.0 : draw_width(f);
		s["body_width"] = charging ? (real_t)0.0 : draw_width(f) * (real_t)d->body_width_scale;
		s["color"] = d->modulate;
		s["charge"] = charging ? Variant(look_texture(LaserData2D::LOOK_CHARGE, clock)) : Variant();
		s["body"] = !charging ? Variant(anim_body.texture_at(clock - grow_started)) : Variant();
		s["muzzle"] = !charging ? Variant(look_texture(LaserData2D::LOOK_MUZZLE, clock - grow_started)) : Variant();
		s["tip"] = !charging ? Variant(look_texture(LaserData2D::LOOK_TIP, clock - grow_started)) : Variant();
		Array pierces;
		for (const DrawMark2D &m : frame.pierces) {
			Array entry;
			entry.push_back(m.position);
			entry.push_back(Variant(look_texture(LaserData2D::LOOK_PIERCE, clock - m.since)));
			entry.push_back(m.angle);
			pierces.push_back(entry);
		}
		s["pierces"] = pierces;
		Array impacts;
		for (const DrawMark2D &m : frame.impacts) {
			Array entry;
			entry.push_back(m.position);
			entry.push_back(Variant(look_texture(LaserData2D::LOOK_WALL_IMPACT, clock - m.since)));
			entry.push_back(m.angle);
			// Drawn: a spent head plays its impact once.
			entry.push_back(!b.spent || look_still_playing(LaserData2D::LOOK_WALL_IMPACT, clock - m.since));
			impacts.push_back(entry);
		}
		s["impacts"] = impacts;
		out.push_back(s);
	}
	return out;
}

} // namespace BlastBullets2D
