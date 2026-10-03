// Outline debug inspectors: mathematical conformance checks (dot positions, gaps,
// corner ownership, facing) used by the pattern test suites.

#include "factory/bullet_factory2d_patterns_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

// ---- Outline debug inspectors (mathematical conformance framework) ----

static real_t debug_param_real(const Dictionary &p, const StringName &k, real_t fallback) {
	if (!p.has(k)) {
		return fallback;
	}
	const Variant v = p[k];
	if (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) {
		const double d = (double)v;
		return Math::is_finite(d) ? (real_t)d : fallback;
	}
	return fallback;
}

static int debug_param_int(const Dictionary &p, const StringName &k, int fallback) {
	if (!p.has(k)) {
		return fallback;
	}
	const Variant v = p[k];
	if (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) {
		return (int)v;
	}
	if (v.get_type() == Variant::BOOL) {
		return (bool)v ? 1 : 0;
	}
	return fallback;
}

static bool debug_param_bool(const Dictionary &p, const StringName &k, bool fallback) {
	if (!p.has(k)) {
		return fallback;
	}
	const Variant v = p[k];
	if (v.get_type() == Variant::BOOL) {
		return (bool)v;
	}
	if (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) {
		return (int)v != 0;
	}
	return fallback;
}

static Vector2 debug_param_v2(const Dictionary &p, const StringName &k, const Vector2 &fallback) {
	if (!p.has(k)) {
		return fallback;
	}
	const Variant v = p[k];
	if (v.get_type() == Variant::VECTOR2) {
		const Vector2 w = v;
		return w.is_finite() ? w : fallback;
	}
	return fallback;
}

static real_t debug_wrap_angle(real_t a) {
	while (a > Math::PI) {
		a -= Math::TAU;
	}
	while (a < -Math::PI) {
		a += Math::TAU;
	}
	return a;
}

Dictionary BulletFactory2D::debug_describe_outline(int shape, int count, const Dictionary &params) {
	Dictionary out;
	out["ok"] = false;
	out["error"] = String("unknown shape");
	out["points"] = PackedVector2Array();
	out["facings"] = PackedFloat32Array();
	out["edge_ids"] = PackedInt32Array();
	out["corner_flags"] = PackedInt32Array();
	out["corner_index"] = PackedInt32Array();
	out["gaps"] = PackedFloat32Array();
	out["settings"] = Dictionary();
	if (count <= 0) {
		out["error"] = String("count must be > 0");
		return out;
	}
	// Common knobs (helper-matching defaults).
	const bool face_outward = debug_param_bool(params, "face_outward", true);
	const real_t facing_offset_deg = debug_param_real(params, "facing_offset_degrees", 0.0);
	const int outline_facing = debug_param_int(params, "outline_facing", 0);
	const bool outline_reverse = debug_param_bool(params, "outline_reverse", false);
	const int outline_slot_offset = debug_param_int(params, "outline_slot_offset", 0);
	const int distribution = debug_param_int(params, "outline_distribution", 1);
	const int corner_priority = debug_param_int(params, "outline_corner_priority", 0);
	const int corner_mode = debug_param_int(params, "outline_corner_mode", 0);
	const int corner_facing = debug_param_int(params, "outline_corner_facing", 0);
	const double edge_margin = (double)debug_param_real(params, "outline_edge_margin", 0.0);
	const Transform2D identity;
	TypedArray<Transform2D> volley;
	PackedVector2Array corners; // marker-local corners for polygonal shapes
	bool has_corners = false;
	bool radial_facings = true;
	switch (shape) {
		case DEBUG_SHAPE_CIRCLE: {
			const real_t radius = debug_param_real(params, "radius", 150.0);
			volley = helper_generate_transforms_circle(count, identity, radius, face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0);
			break;
		}
		case DEBUG_SHAPE_RING: {
			const real_t radius = debug_param_real(params, "radius", 150.0);
			const real_t start_angle = debug_param_real(params, "start_angle", 0.0);
			const real_t arc = debug_param_real(params, "arc", Math::TAU);
			const real_t y_scale = debug_param_real(params, "y_scale", 1.0);
			volley = helper_generate_transforms_ring(count, identity, radius, start_angle, arc, true, false, face_outward, y_scale, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, (uint64_t)debug_param_int(params, "seed", 0));
			break;
		}
		case DEBUG_SHAPE_ELLIPSE: {
			const real_t rx = debug_param_real(params, "radius_x", 150.0);
			const real_t ry = debug_param_real(params, "radius_y", 100.0);
			volley = helper_generate_transforms_ellipse(count, identity, rx, ry, debug_param_real(params, "rotation", 0.0), debug_param_real(params, "start_angle", 0.0), debug_param_real(params, "arc", Math::TAU), (EllipseMode)debug_param_int(params, "mode", 0), debug_param_int(params, "gap_count", 0), debug_param_real(params, "gap_width", 0.0), face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0);
			break;
		}
		case DEBUG_SHAPE_RECTANGLE: {
			const Vector2 size = debug_param_v2(params, "size", Vector2(300, 200));
			volley = helper_generate_transforms_rectangle(count, identity, size, face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, distribution, 1, corner_priority, corner_mode, (double)edge_margin, corner_facing);
			const Vector2 hw(size.x * 0.5, size.y * 0.5);
			corners.push_back(Vector2(-hw.x, -hw.y));
			corners.push_back(Vector2(hw.x, -hw.y));
			corners.push_back(Vector2(hw.x, hw.y));
			corners.push_back(Vector2(-hw.x, hw.y));
			has_corners = true;
			radial_facings = false;
			break;
		}
		case DEBUG_SHAPE_SQUARE: {
			const real_t s = debug_param_real(params, "size", 150.0);
			volley = helper_generate_transforms_rectangle(count, identity, Vector2(s, s), face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, distribution, 1, corner_priority, corner_mode, (double)edge_margin, corner_facing);
			const Vector2 hw(s * 0.5, s * 0.5);
			corners.push_back(Vector2(-hw.x, -hw.y));
			corners.push_back(Vector2(hw.x, -hw.y));
			corners.push_back(Vector2(hw.x, hw.y));
			corners.push_back(Vector2(-hw.x, hw.y));
			has_corners = true;
			radial_facings = false;
			break;
		}
		case DEBUG_SHAPE_POLYGON: {
			const int vertices = debug_param_int(params, "vertices", 6);
			const real_t radius = debug_param_real(params, "radius", 150.0);
			const real_t rotation = debug_param_real(params, "rotation", 0.0);
			volley = helper_generate_transforms_polygon(count, identity, vertices, radius, rotation, face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, distribution, 1, corner_priority, corner_mode, (double)edge_margin, corner_facing);
			if (vertices >= 3) {
				for (int k = 0; k < vertices; ++k) {
					const real_t a = rotation + Math::TAU * (real_t)k / (real_t)vertices;
					corners.push_back(Vector2(Math::cos(a), Math::sin(a)) * radius);
				}
				has_corners = true;
			}
			radial_facings = false;
			break;
		}
		case DEBUG_SHAPE_TRIANGLE: {
			const int ttype = debug_param_int(params, "triangle_type", 0);
			const real_t sa = debug_param_real(params, "size_a", 150.0);
			const real_t sb = debug_param_real(params, "size_b", 150.0);
			const real_t rotation = debug_param_real(params, "rotation", 0.0);
			volley = helper_generate_transforms_triangle(count, identity, (TriangleType)ttype, sa, sb, rotation, face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, distribution, 1, corner_priority, corner_mode, (double)edge_margin, corner_facing);
			corners = build_triangle_corners(ttype, sa, sb, rotation);
			has_corners = corners.size() == 3;
			radial_facings = false;
			break;
		}
		case DEBUG_SHAPE_TRAPEZOID: {
			const real_t top = debug_param_real(params, "base_top", 200.0);
			const real_t bottom = debug_param_real(params, "base_bottom", 300.0);
			const real_t height = debug_param_real(params, "height", 200.0);
			const real_t rotation = debug_param_real(params, "rotation", 0.0);
			volley = helper_generate_transforms_trapezoid(count, identity, top, bottom, height, rotation, face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, distribution, 1, corner_priority, corner_mode, (double)edge_margin, corner_facing);
			corners = build_trapezoid_corners(top, bottom, height, rotation);
			has_corners = corners.size() == 4;
			radial_facings = false;
			break;
		}
		case DEBUG_SHAPE_DIAMOND: {
			const real_t dx = debug_param_real(params, "diagonal_x", 200.0);
			const real_t dy = debug_param_real(params, "diagonal_y", 300.0);
			const real_t rotation = debug_param_real(params, "rotation", 0.0);
			volley = helper_generate_transforms_diamond(count, identity, dx, dy, rotation, face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, distribution, 1, corner_priority, corner_mode, (double)edge_margin, corner_facing);
			corners = build_diamond_corners(dx, dy, rotation);
			has_corners = corners.size() == 4;
			radial_facings = false;
			break;
		}
		case DEBUG_SHAPE_STAR: {
			const int points = debug_param_int(params, "points", 5);
			const real_t outer = debug_param_real(params, "outer_radius", 150.0);
			const real_t inner = debug_param_real(params, "inner_radius", 65.0);
			const real_t rotation = debug_param_real(params, "rotation", 0.0);
			volley = helper_generate_transforms_star(count, identity, points, outer, inner, rotation, face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, distribution, 1, corner_priority, corner_mode, (double)edge_margin, corner_facing);
			if (points >= 2) {
				const int cn = points * 2;
				for (int c = 0; c < cn; ++c) {
					const real_t a = rotation + Math::TAU * (real_t)c / (real_t)cn;
					corners.push_back(Vector2(Math::cos(a), Math::sin(a)) * ((c % 2 == 0) ? outer : inner));
				}
				has_corners = true;
			}
			radial_facings = false;
			break;
		}
		case DEBUG_SHAPE_HEART: {
			volley = helper_generate_transforms_heart(count, identity, debug_param_real(params, "size", 150.0), debug_param_real(params, "rotation", 0.0), face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0);
			break;
		}
		case DEBUG_SHAPE_FLOWER: {
			volley = helper_generate_transforms_flower(count, identity, debug_param_int(params, "petals", 6), debug_param_real(params, "radius", 150.0), debug_param_real(params, "petal_spread", 0.5), debug_param_real(params, "petal_sharpness", 1.0), debug_param_real(params, "rotation", 0.0), face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, debug_param_int(params, "flower_type", 0), (double)debug_param_real(params, "inner_radius_scale", 0.0), (double)debug_param_real(params, "spiro_roller", 45.0), (double)debug_param_real(params, "spiro_pen", 80.0), (double)debug_param_real(params, "super_lobes", 6.0), (double)debug_param_real(params, "super_fullness", 1.0));
			break;
		}
		case DEBUG_SHAPE_ROSE: {
			volley = helper_generate_transforms_rose(count, identity, debug_param_int(params, "petals", 6), debug_param_real(params, "radius", 150.0), debug_param_real(params, "lobe_sharpness", 1.0), debug_param_real(params, "rotation", 0.0), face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0);
			break;
		}
		case DEBUG_SHAPE_LISSAJOUS: {
			volley = helper_generate_transforms_lissajous(count, identity, debug_param_real(params, "size_x", 200.0), debug_param_real(params, "size_y", 120.0), debug_param_real(params, "freq_x", 3.0), debug_param_real(params, "freq_y", 2.0), debug_param_real(params, "phase", 0.0), face_outward, facing_offset_deg, 0, outline_facing, outline_reverse, outline_slot_offset, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0);
			break;
		}
		default: {
			return out;
		}
	}
	const int m = volley.size();
	if (m <= 0) {
		out["error"] = String("generator emitted no slots");
		return out;
	}
	// Edge outward normals for ownership matching (polygonal shapes), plus
	// the averaged corner normals reused by the facing-policy expectations.
	PackedVector2Array edge_normals;
	PackedVector2Array averaged_normals;
	if (has_corners) {
		PackedVector2Array averaged;
		if (!compute_edge_normals_quiet(corners, true, false, averaged) || averaged.size() != corners.size()) {
			has_corners = false;
		} else {
			averaged_normals = averaged;
			const int cn = corners.size();
			for (int e = 0; e < cn; ++e) {
				Vector2 en = oriented_edge_normal(corners[e], corners[(e + 1) % cn], averaged[e]);
				if (en.length_squared() <= 1e-12) {
					en = averaged[e];
				}
				edge_normals.push_back(en.length_squared() > 1e-12 ? en.normalized() : Vector2(0, -1));
			}
		}
	}
	auto averaged_corner = [&](int c) -> Vector2 {
		if (c < 0 || c >= averaged_normals.size()) {
			return Vector2(0, -1);
		}
		const Vector2 a = averaged_normals[c];
		return a.length_squared() > 1e-12 ? a.normalized() : Vector2(0, -1);
	};
	const real_t selector = outline_facing == 1 ? Math::PI * 0.5 : (outline_facing == 2 ? -Math::PI * 0.5 : 0.0);
	const real_t facing_offset = Math::deg_to_rad(facing_offset_deg);
	const real_t flip = face_outward ? 0.0f : Math::PI;
	PackedVector2Array pts;
	PackedFloat32Array facings;
	PackedInt32Array edge_ids;
	PackedInt32Array corner_flags;
	PackedInt32Array corner_index;
	PackedFloat32Array facing_dev;
	PackedFloat32Array gaps;
	double worst_dev = 0.0;
	int worst_at = -1;
	// Ellipse/ring-with-yscale facings follow the analytic gradient, not the
	// radial direction (they coincide only on circles/axes).
	const bool gradient_ref = (shape == DEBUG_SHAPE_ELLIPSE);
	const real_t grad_rx = debug_param_real(params, "radius_x", 150.0);
	const real_t grad_ry = debug_param_real(params, "radius_y", 100.0);
	const real_t grad_rot = debug_param_real(params, "rotation", 0.0);
	const real_t grad_cos = Math::cos(grad_rot);
	const real_t grad_sin = Math::sin(grad_rot);
	// Owning edge of corner c under the priority rule (position ownership,
	// independent of facing policy): the edge the corner dot counts toward.
	auto corner_owner_edge = [&](int c) -> int {
		const int cn = corners.size();
		const bool in_h = edge_is_horizontal(corners[(c - 1 + cn) % cn], corners[c]);
		const bool out_h = edge_is_horizontal(corners[c], corners[(c + 1) % cn]);
		if (corner_priority == BlastBullets2D::BulletFactory2D::OUTLINE_CORNER_PRIORITY_HORIZONTAL) {
			return (in_h && !out_h) ? (c - 1 + cn) % cn : c;
		}
		if (corner_priority == BlastBullets2D::BulletFactory2D::OUTLINE_CORNER_PRIORITY_VERTICAL) {
			return (!in_h && out_h) ? (c - 1 + cn) % cn : c;
		}
		return c;
	};
	for (int i = 0; i < m; ++i) {
		const Transform2D t = volley[i];
		const Vector2 p = t.get_origin();
		const real_t f = t.get_rotation();
		pts.push_back(p);
		facings.push_back(f);
		const real_t base = f - flip - selector - facing_offset;
		if (has_corners) {
			const int cn = corners.size();
			int ci = -1;
			for (int c = 0; c < cn; ++c) {
				if (p.distance_to(corners[c]) <= 1e-4) {
					ci = c;
					break;
				}
			}
			corner_index.push_back(ci);
			corner_flags.push_back(ci >= 0 ? 1 : 0);
			double best_d = 1e30;
			int best_e = 0;
			if (ci >= 0) {
				// Corner dot: ownership from the priority rule, facing
				// expectation from the facing policy (side = owner edge,
				// miter = bisector, smooth = averaged corner normal).
				best_e = corner_owner_edge(ci);
				Vector2 expect = edge_normals[best_e];
				if (corner_facing == BlastBullets2D::BulletFactory2D::OUTLINE_CORNER_FACING_MITER) {
					expect = miter_normal(corners, ci, averaged_corner(ci));
				} else if (corner_facing == BlastBullets2D::BulletFactory2D::OUTLINE_CORNER_FACING_SMOOTH) {
					expect = averaged_corner(ci);
				}
				best_d = (expect.length_squared() > 1e-12) ? Math::abs((double)debug_wrap_angle(base - expect.angle())) : 0.0;
			} else {
				// Interior dot: nearest segment by distance; facing
				// expectation is that edge's normal (smooth lerps averaged).
				double best_seg = 1e30;
				for (int e = 0; e < cn; ++e) {
					const double dd = outline_point_seg_dist(p, corners[e], corners[(e + 1) % cn]);
					if (dd < best_seg) {
						best_seg = dd;
						best_e = e;
					}
				}
				Vector2 expect = edge_normals[best_e];
				if (corner_facing == BlastBullets2D::BulletFactory2D::OUTLINE_CORNER_FACING_SMOOTH) {
					const double seg_len = (double)corners[best_e].distance_to(corners[(best_e + 1) % cn]);
					double tt = 0.0;
					if (seg_len > 1e-9) {
						tt = Math::clamp((double)(p - corners[best_e]).dot(corners[(best_e + 1) % cn] - corners[best_e]) / (seg_len * seg_len), 0.0, 1.0);
					}
					Vector2 sn = averaged_corner(best_e).lerp(averaged_corner((best_e + 1) % cn), (real_t)tt);
					if (sn.length_squared() > 1e-12) {
						expect = sn.normalized();
					}
				}
				best_d = (expect.length_squared() > 1e-12) ? Math::abs((double)debug_wrap_angle(base - expect.angle())) : 0.0;
			}
			edge_ids.push_back(best_e);
			facing_dev.push_back((real_t)best_d);
			if (best_d > worst_dev) {
				worst_dev = best_d;
				worst_at = i;
			}
		} else {
			edge_ids.push_back(-1);
			corner_index.push_back(-1);
			corner_flags.push_back(0);
			double dev = 0.0;
			if (gradient_ref) {
				// Expected facing = ellipse gradient at the dot's own
				// position (exact for on-ellipse points, independent of the
				// generator's sampling).
				const double ux = (double)p.x * (double)grad_cos + (double)p.y * (double)grad_sin;
				const double uy = -(double)p.x * (double)grad_sin + (double)p.y * (double)grad_cos;
				const double rx2 = Math::max((double)grad_rx * (double)grad_rx, 0.0001);
				const double ry2 = Math::max((double)grad_ry * (double)grad_ry, 0.0001);
				Vector2 g = Vector2((real_t)(ux / rx2), (real_t)(uy / ry2));
				if (g.length_squared() > 1e-12) {
					g = g.normalized().rotated(grad_rot);
					dev = Math::abs((double)debug_wrap_angle(base - g.angle()));
				}
			} else {
				const Vector2 radial = p;
				if (radial.length_squared() > 1e-12) {
					dev = Math::abs((double)debug_wrap_angle(base - radial.angle()));
				}
			}
			facing_dev.push_back((real_t)dev);
			if (dev > worst_dev) {
				worst_dev = dev;
				worst_at = i;
			}
		}
	}
	for (int i = 0; i < m; ++i) {
		gaps.push_back(pts[i].distance_to(pts[(i + 1) % m]));
	}
	Dictionary settings;
	settings["shape"] = shape;
	settings["count"] = count;
	settings["emitted"] = m;
	settings["face_outward"] = face_outward;
	settings["facing_offset_degrees"] = (double)facing_offset_deg;
	settings["outline_facing"] = outline_facing;
	settings["outline_reverse"] = outline_reverse;
	settings["outline_slot_offset"] = outline_slot_offset;
	settings["outline_distribution"] = distribution;
	settings["outline_corner_priority"] = corner_priority;
	settings["outline_corner_mode"] = corner_mode;
	settings["outline_edge_margin"] = edge_margin;
	settings["outline_corner_facing"] = corner_facing;
	PackedInt32Array edge_histogram;
	if (has_corners) {
		edge_histogram.resize(corners.size());
		for (int i = 0; i < edge_ids.size(); ++i) {
			const int e = edge_ids[i];
			if (e >= 0 && e < edge_histogram.size()) {
				edge_histogram[e] = edge_histogram[e] + 1;
			}
		}
	}
	out["ok"] = true;
	out["error"] = String("");
	out["points"] = pts;
	out["facings"] = facings;
	out["edge_ids"] = edge_ids;
	out["corner_flags"] = corner_flags;
	out["corner_index"] = corner_index;
	out["facing_deviations"] = facing_dev;
	out["worst_facing_deviation"] = worst_dev;
	out["worst_facing_index"] = worst_at;
	out["gaps"] = gaps;
	out["edge_histogram"] = edge_histogram;
	out["corners"] = corners;
	out["settings"] = settings;
	return out;
}

Dictionary BulletFactory2D::debug_outline_quotas(int shape, int count, const Dictionary &params) {
	// Per-edge dot quotas plus an optimality verdict against the
	// length-proportional largest-remainder optimum: every edge must sit
	// within < 1 slot of its exact share (both LEGACY and SYMMETRIC satisfy
	// this; they differ only in tie-breaks). SYMMETRIC additionally keeps
	// opposite pairs within 1 on even corner counts. Verdict fields:
	// optimal (length-aware), symmetric_pairs (opposite equality, even n).
	Dictionary out;
	out["ok"] = false;
	const Dictionary rep = debug_describe_outline(shape, count, params);
	if (!(bool)rep.get("ok", false)) {
		out["error"] = String("describe failed: ") + String(rep.get("error", ""));
		return out;
	}
	const PackedInt32Array hist = rep["edge_histogram"];
	const int cn = hist.size();
	if (cn < 3) {
		out["error"] = String("shape has no corner-anchored edges");
		return out;
	}
	// Corners ride along from describe (single source of truth, no rebuild).
	const PackedVector2Array corners = rep["corners"];
	if (corners.size() != cn) {
		out["error"] = String("corner data mismatch");
		return out;
	}
	// Interiors carry the length signal: dots strictly inside each segment
	// (corner dots excluded geometrically, so ownership assignment can never
	// pollute the metric). Compared against exact proportional shares.
	PackedInt32Array interiors;
	interiors.resize(cn);
	for (int e = 0; e < cn; ++e) {
		interiors[e] = 0;
	}
	const PackedVector2Array rpts = rep["points"];
	const PackedInt32Array rcidx = rep["corner_index"];
	for (int i = 0; i < rpts.size(); ++i) {
		if (rcidx[i] >= 0) {
			continue;
		}
		double best_seg = 1e30;
		int best_e = 0;
		for (int e = 0; e < cn; ++e) {
			const double dd = outline_point_seg_dist(rpts[i], corners[e], corners[(e + 1) % cn]);
			if (dd < best_seg) {
				best_seg = dd;
				best_e = e;
			}
		}
		interiors[best_e] = interiors[best_e] + 1;
	}
	double total = 0.0;
	for (int e = 0; e < cn; ++e) {
		total += (double)corners[e].distance_to(corners[(e + 1) % cn]);
	}
	const int corner_mode = debug_param_int(params, "outline_corner_mode", 0);
	const int distribution = debug_param_int(params, "outline_distribution", 1);
	(void)distribution;
	bool optimal = true;
	PackedFloat64Array exact_shares;
	exact_shares.resize(cn);
	if (corner_mode == OUTLINE_CORNER_MODE_PIN_CORNERS && count > cn && total > 0.0) {
		const int rest = count - cn;
		for (int e = 0; e < cn; ++e) {
			const double exact = (double)rest * (double)corners[e].distance_to(corners[(e + 1) % cn]) / total;
			exact_shares[e] = exact;
			if (Math::abs((double)interiors[e] - exact) >= 1.0) {
				optimal = false;
			}
		}
	} else {
		for (int e = 0; e < cn; ++e) {
			exact_shares[e] = 0.0;
		}
		// Even-arc spreads ignore apportionment (no optimum applies);
		// small pin-mode counts trivially satisfy it.
		optimal = (corner_mode != OUTLINE_CORNER_MODE_EVEN_ARC);
	}
	// Opposite-pair equality (even corner counts only): the SYMMETRIC
	// promise, measured on interiors so shared corners can't skew it.
	// Reports worst pair spread instead of a pass/fail so tests can assert
	// the mathematically achievable bound (0 for even leftovers, 1 for a
	// single odd leftover). Unequal opposites (trapezoid top/bottom) can
	// never be pair-equal; their verdict is `optimal` above.
	double worst_pair_spread = 0.0;
	if (cn % 2 == 0) {
		for (int e = 0; e < cn / 2; ++e) {
			const double d = Math::abs((double)interiors[e] - (double)interiors[e + cn / 2]);
			worst_pair_spread = MAX(worst_pair_spread, d);
		}
	}
	out["ok"] = true;
	out["error"] = String("");
	out["edge_counts"] = hist;
	out["interiors"] = interiors;
	out["exact_shares"] = exact_shares;
	out["optimal"] = optimal;
	out["worst_pair_spread"] = worst_pair_spread;
	out["settings"] = rep["settings"];
	return out;
}

Dictionary BulletFactory2D::debug_volley_gaps(const TypedArray<Transform2D> &volley) {
	Dictionary out;
	const int m = volley.size();
	PackedFloat32Array gaps;
	double mn = 1e30;
	double mx = 0.0;
	double sum = 0.0;
	for (int i = 0; i < m; ++i) {
		const Transform2D a = volley[i];
		const Transform2D b = volley[(i + 1) % MAX(m, 1)];
		const double g = (m > 0 && a.is_finite() && b.is_finite()) ? (double)a.get_origin().distance_to(b.get_origin()) : 0.0;
		gaps.push_back((real_t)g);
		mn = MIN(mn, g);
		mx = MAX(mx, g);
		sum += g;
	}
	out["count"] = m;
	out["gaps"] = gaps;
	out["min_gap"] = m > 0 ? mn : 0.0;
	out["max_gap"] = m > 0 ? mx : 0.0;
	out["mean_gap"] = m > 0 ? sum / (double)m : 0.0;
	out["gap_ratio"] = (m > 0 && mn > 1e-9) ? mx / mn : 0.0;
	return out;
}

Dictionary BulletFactory2D::debug_verify_volley(const TypedArray<Transform2D> &volley, int shape, const Transform2D &marker, int count, const Dictionary &params, double tolerance_px, double tolerance_rad) {
	Dictionary out;
	out["ok"] = false;
	out["checked"] = 0;
	out["worst_pos_px"] = -1.0;
	out["worst_face_rad"] = -1.0;
	out["bad_index"] = -1;
	const Dictionary ref = debug_describe_outline(shape, count, params);
	if (!(bool)ref.get("ok", false)) {
		out["error"] = String("describe failed: ") + String(ref.get("error", ""));
		return out;
	}
	const PackedVector2Array exp_pts = ref["points"];
	const PackedFloat32Array exp_fac = ref["facings"];
	if (exp_pts.size() != volley.size() || exp_fac.size() != volley.size()) {
		out["error"] = String("size mismatch");
		return out;
	}
	if (!marker.is_finite()) {
		out["error"] = String("marker not finite");
		return out;
	}
	const real_t marker_rot = marker.get_rotation();
	double worst_p = 0.0;
	double worst_f = 0.0;
	int bad = -1;
	for (int i = 0; i < volley.size(); ++i) {
		const Transform2D t = volley[i];
		const Vector2 expect_p = marker.xform(exp_pts[i]);
		const double dp = (double)t.get_origin().distance_to(expect_p);
		const double df = Math::abs((double)debug_wrap_angle(t.get_rotation() - (exp_fac[i] + marker_rot)));
		if (dp > worst_p) {
			worst_p = dp;
		}
		if (df > worst_f) {
			worst_f = df;
		}
		if ((dp > tolerance_px || df > tolerance_rad) && bad < 0) {
			bad = i;
		}
	}
	out["checked"] = volley.size();
	out["worst_pos_px"] = worst_p;
	out["worst_face_rad"] = worst_f;
	out["bad_index"] = bad;
	out["ok"] = bad < 0;
	return out;
}

} // namespace BlastBullets2D
