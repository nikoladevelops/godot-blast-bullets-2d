// Edge patterns: bullets along arbitrary point lists or image edges, edge
// normals and side spread.

#include "factory/bullet_factory2d_patterns_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

// Quiet worker shared by the bound normal computer and the edge sampler:
// fills r_normals, returns false (no error spam) when unusable.
bool compute_edge_normals_quiet(const PackedVector2Array &edge_points, bool closed, bool flip, PackedVector2Array &r_normals) {
	r_normals.clear();
	const int n = edge_points.size();
	if (n <= 0) {
		return false;
	}
	r_normals.resize(n);
	if (n == 1) {
		Vector2 solo(0, -1);
		r_normals[0] = flip ? -solo : solo;
		return edge_points[0].is_finite();
	}
	// First valid tangent fallback for runs of duplicate points.
	Vector2 fallback_tangent(1, 0);
	bool has_fallback = false;
	for (int i = 0; i < n; ++i) {
		Vector2 tangent;
		bool have = false;
		if (closed) {
			// Central difference on the loop; walk outward past duplicates.
			Vector2 prev = edge_points[(i - 1 + n) % n];
			Vector2 next = edge_points[(i + 1) % n];
			Vector2 d = next - prev;
			if (d.length_squared() > 1e-12) {
				tangent = d;
				have = true;
			} else {
				for (int k = 2; k < n; ++k) {
					prev = edge_points[(i - k + n * 2) % n];
					next = edge_points[(i + k) % n];
					d = next - prev;
					if (d.length_squared() > 1e-12) {
						tangent = d;
						have = true;
						break;
					}
				}
			}
		} else {
			if (i == 0) {
				tangent = edge_points[1] - edge_points[0];
				have = tangent.length_squared() > 1e-12;
				if (!have) {
					for (int k = 2; k < n; ++k) {
						tangent = edge_points[k] - edge_points[0];
						if (tangent.length_squared() > 1e-12) {
							have = true;
							break;
						}
					}
				}
			} else if (i == n - 1) {
				tangent = edge_points[n - 1] - edge_points[n - 2];
				have = tangent.length_squared() > 1e-12;
				if (!have) {
					for (int k = n - 3; k >= 0; --k) {
						tangent = edge_points[n - 1] - edge_points[k];
						if (tangent.length_squared() > 1e-12) {
							have = true;
							break;
						}
					}
				}
			} else {
				Vector2 d = edge_points[i + 1] - edge_points[i - 1];
				if (d.length_squared() > 1e-12) {
					tangent = d;
					have = true;
				} else {
					// Degenerate joint: fall back to the longer one-sided leg.
					Vector2 a = edge_points[i] - edge_points[i - 1];
					Vector2 b = edge_points[i + 1] - edge_points[i];
					if (b.length_squared() >= a.length_squared() && b.length_squared() > 1e-12) {
						tangent = b;
						have = true;
					} else if (a.length_squared() > 1e-12) {
						tangent = a;
						have = true;
					}
				}
			}
		}
		if (have) {
			fallback_tangent = tangent.normalized();
			has_fallback = true;
		} else if (has_fallback) {
			tangent = fallback_tangent;
			have = true;
		} else {
			tangent = Vector2(1, 0);
			have = true;
		}
		Vector2 normal = tangent.normalized().orthogonal();
		if (flip) {
			normal = -normal;
		}
		r_normals[i] = normal;
	}
	for (int i = 0; i < n; ++i) {
		if (!edge_points[i].is_finite() || !r_normals[i].is_finite()) {
			return false;
		}
	}
	return true;
}

TypedArray<Transform2D> BulletFactory2D::helper_apply_side_spread(
		const TypedArray<Transform2D> &transforms,
		int side_mode,
		real_t spread,
		real_t spread_exponent,
		uint64_t seed) {
	if (side_mode < 0 || side_mode > 3) {
		UtilityFunctions::push_error("helper_apply_side_spread: side_mode must be 0 (on path), 1 (outside), 2 (inside) or 3 (both).");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(spread) || spread < 0.0 || !Math::is_finite(spread_exponent) || spread_exponent < 0.01) {
		UtilityFunctions::push_error("helper_apply_side_spread: spread must be finite and >= 0, spread_exponent finite and >= 0.01.");
		return TypedArray<Transform2D>();
	}
	TypedArray<Transform2D> out;
	out.resize(transforms.size());
	for (int i = 0; i < transforms.size(); ++i) {
		out[i] = transforms[i];
	}
	if (side_mode == 0 || spread <= 0.0) {
		return out;
	}
	Ref<RandomNumberGenerator> rng = memnew(RandomNumberGenerator);
	if (seed != 0) {
		rng->set_seed(seed);
	} else {
		rng->randomize();
	}
	for (int i = 0; i < out.size(); ++i) {
		Transform2D t = out[i];
		if (!t.is_finite()) {
			continue;
		}
		const Vector2 dir = Vector2(1.0, 0.0).rotated(t.get_rotation());
		if (!dir.is_finite()) {
			continue;
		}
		real_t off = spread * Math::pow(rng->randf(), spread_exponent);
		if (!Math::is_finite(off)) {
			continue;
		}
		if (side_mode == 3) {
			off *= (rng->randf() < 0.5) ? -1.0 : 1.0;
		} else if (side_mode == 2) {
			off = -off;
		}
		const Vector2 origin = t.get_origin() + dir * off;
		if (!origin.is_finite()) {
			continue;
		}
		t.set_origin(origin);
		out[i] = t;
	}
	return out;
}

PackedVector2Array BulletFactory2D::helper_compute_edge_normals(
		const PackedVector2Array &edge_points,
		bool closed,
		bool flip) {
	PackedVector2Array normals;
	if (edge_points.size() <= 0) {
		UtilityFunctions::push_error("helper_compute_edge_normals: edge_points must contain at least 1 point.");
		return normals;
	}
	if (!compute_edge_normals_quiet(edge_points, closed, flip, normals)) {
		UtilityFunctions::push_error("helper_compute_edge_normals: edge_points must be finite.");
		normals.clear();
		return normals;
	}
	return normals;
}

TypedArray<Transform2D> BulletFactory2D::helper_generate_transforms_edge_from_points(
		int transforms_amount,
		Transform2D marker_transform,
		const PackedVector2Array &edge_points,
		bool closed,
		bool flip_normals,
		bool random_sample,
		real_t jitter,
		real_t facing_offset_degrees,
		uint64_t seed,
		real_t spread,
		real_t spread_exponent,
		int spread_side,
		real_t tangent_jitter) {
	if (!danmaku_validate_head("helper_generate_transforms_edge_from_points", transforms_amount, marker_transform)) {
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(jitter) || jitter < 0.0 || !Math::is_finite(facing_offset_degrees)) {
		UtilityFunctions::push_error("helper_generate_transforms_edge_from_points: jitter must be finite and >= 0, facing_offset_degrees must be finite.");
		return TypedArray<Transform2D>();
	}
	if (!Math::is_finite(spread) || spread < 0.0 || !Math::is_finite(spread_exponent) || spread_exponent < 0.01 || !Math::is_finite(tangent_jitter) || tangent_jitter < 0.0) {
		UtilityFunctions::push_error("helper_generate_transforms_edge_from_points: spread must be finite and >= 0, spread_exponent finite and >= 0.01, tangent_jitter finite and >= 0.");
		return TypedArray<Transform2D>();
	}
	if (spread_side < 0 || spread_side > 2) {
		UtilityFunctions::push_error("helper_generate_transforms_edge_from_points: spread_side must be 0 (along), 1 (behind) or 2 (both).");
		return TypedArray<Transform2D>();
	}
	TypedArray<Transform2D> generated_transforms = danmaku_make_slots(transforms_amount);
	if (transforms_amount == 0) {
		return generated_transforms;
	}
	if (edge_points.size() <= 0) {
		UtilityFunctions::push_error("helper_generate_transforms_edge_from_points: edge_points must contain at least 1 point.");
		return TypedArray<Transform2D>();
	}
	PackedVector2Array normals;
	if (!compute_edge_normals_quiet(edge_points, closed, flip_normals, normals)) {
		UtilityFunctions::push_error("helper_generate_transforms_edge_from_points: edge_points must be finite.");
		return TypedArray<Transform2D>();
	}
	const int n = edge_points.size();
	const real_t marker_rot = marker_transform.get_rotation();
	const real_t facing_offset = Math::deg_to_rad(facing_offset_degrees);
	// Engine Ref classes must be heap-instantiated: a stack
	// RandomNumberGenerator has no binding callbacks and hard-crashes
	// (SIGILL) on first use. Same memnew+Ref pattern as the scatter helper.
	Ref<RandomNumberGenerator> rng = memnew(RandomNumberGenerator);
	if (seed != 0) {
		rng->set_seed(seed);
	} else {
		rng->randomize();
	}
	// One-sided normal falloff shared by all three sampling paths below:
	// offset = spread * pow(u, exponent), u uniform in [0, 1]. Exponent > 1
	// clusters bullets near the crest (the reference look); exponent < 1
	// pushes them deeper. Side 2 picks a random sign per bullet.
	// Tangent jitter is intentionally independent of spread: it softens the
	// crest core even when the falloff cloud is disabled (spread = 0).
	auto apply_edge_spread = [&](Vector2 &r_local, const Vector2 &p_nrm) {
		if (spread <= 0.0) {
			return;
		}
		const real_t u = rng->randf();
		real_t off = spread * Math::pow(u, spread_exponent);
		if (!Math::is_finite(off)) {
			return;
		}
		if (spread_side == 2) {
			off *= (rng->randf() < 0.5) ? -1.0 : 1.0;
		} else if (spread_side == 1) {
			off = -off;
		}
		r_local += p_nrm * off;
	};
	auto apply_edge_tangent_jitter = [&](Vector2 &r_local, const Vector2 &p_nrm) {
		if (tangent_jitter <= 0.0) {
			return;
		}
		const Vector2 tangent = p_nrm.orthogonal();
		r_local += tangent * rng->randf_range(-tangent_jitter, tangent_jitter);
	};
	// Single-point edge: every bullet spawns there facing the point normal.
	if (n == 1) {
		for (int i = 0; i < transforms_amount; ++i) {
			Vector2 local = edge_points[0];
			apply_edge_spread(local, normals[0]);
			apply_edge_tangent_jitter(local, normals[0]);
			if (jitter > 0.0) {
				const real_t a = rng->randf_range(0.0, Math::TAU);
				const real_t r = Math::sqrt(rng->randf()) * jitter;
				local += Vector2(Math::cos(a), Math::sin(a)) * r;
			}
			Transform2D slot(marker_rot + normals[0].angle() + facing_offset, marker_transform.xform(local));
			danmaku_apply_marker_scale(slot, marker_transform);
			generated_transforms[i] = slot;
		}
		return generated_transforms;
	}
	// Cumulative arc lengths (plus closing segment for loops).
	std::vector<real_t> cum;
	cum.reserve(n + 1);
	cum.push_back(0.0);
	for (int i = 1; i < n; ++i) {
		cum.push_back(cum.back() + edge_points[i - 1].distance_to(edge_points[i]));
	}
	real_t total = cum.back();
	if (closed) {
		total += edge_points[n - 1].distance_to(edge_points[0]);
	}
	if (!(total > 0.0) || !Math::is_finite(total)) {
		// All points coincide: same as the single-point case.
		for (int i = 0; i < transforms_amount; ++i) {
			Vector2 local = edge_points[0];
			apply_edge_spread(local, normals[0]);
			apply_edge_tangent_jitter(local, normals[0]);
			if (jitter > 0.0) {
				const real_t a = rng->randf_range(0.0, Math::TAU);
				const real_t r = Math::sqrt(rng->randf()) * jitter;
				local += Vector2(Math::cos(a), Math::sin(a)) * r;
			}
			Transform2D slot(marker_rot + normals[0].angle() + facing_offset, marker_transform.xform(local));
			danmaku_apply_marker_scale(slot, marker_transform);
			generated_transforms[i] = slot;
		}
		return generated_transforms;
	}
	const int seg_count = closed ? n : n - 1;
	for (int i = 0; i < transforms_amount; ++i) {
		real_t d = 0.0;
		if (random_sample) {
			d = rng->randf() * total;
		} else if (transforms_amount == 1) {
			d = 0.0;
		} else if (closed) {
			d = total * (real_t)i / (real_t)transforms_amount;
		} else {
			d = total * (real_t)i / (real_t)(transforms_amount - 1);
		}
		if (d >= total) {
			d = Math::fposmod(d, total);
		}
		// Locate the segment holding d (linear scan; edges are small).
		int seg = 0;
		while (seg < seg_count - 1) {
			const real_t seg_end = (seg + 1 < n) ? cum[seg + 1] : total;
			if (d < seg_end) {
				break;
			}
			++seg;
		}
		const int ia = seg % n;
		const int ib = (seg + 1) % n;
		const real_t seg_start = (seg < n) ? cum[seg] : total;
		const real_t seg_end = (seg + 1 < n) ? cum[seg + 1] : total;
		const real_t seg_len = seg_end - seg_start;
		real_t t = (seg_len > 1e-9) ? (d - seg_start) / seg_len : 0.0;
		t = Math::clamp(t, (real_t)0.0, (real_t)1.0);
		Vector2 local = edge_points[ia].lerp(edge_points[ib], t);
		Vector2 nrm = normals[ia].lerp(normals[ib], t);
		if (nrm.length_squared() <= 1e-12) {
			nrm = normals[ia];
		}
		nrm = nrm.normalized();
		apply_edge_spread(local, nrm);
		apply_edge_tangent_jitter(local, nrm);
		if (jitter > 0.0) {
			const real_t a = rng->randf_range(0.0, Math::TAU);
			const real_t r = Math::sqrt(rng->randf()) * jitter;
			local += Vector2(Math::cos(a), Math::sin(a)) * r;
		}
		if (!local.is_finite()) {
			local = edge_points[ia];
		}
		Transform2D slot(marker_rot + nrm.angle() + facing_offset, marker_transform.xform(local));
		danmaku_apply_marker_scale(slot, marker_transform);
		generated_transforms[i] = slot;
	}
	return generated_transforms;
}

Dictionary BulletFactory2D::helper_extract_edge_from_image(
		const Ref<Image> &image,
		real_t threshold,
		int step,
		bool quiet) {
	Dictionary result;
	result["points"] = PackedVector2Array();
	result["normals"] = PackedVector2Array();
	if (image.is_null() || image->is_empty()) {
		if (!quiet) UtilityFunctions::push_error("helper_extract_edge_from_image: image is null or empty.");
		return result;
	}
	if (!Math::is_finite(threshold) || threshold < 0.0 || threshold > 1.0) {
		if (!quiet) UtilityFunctions::push_error("helper_extract_edge_from_image: threshold must be in [0, 1].");
		return result;
	}
	if (step < 1) {
		if (!quiet) UtilityFunctions::push_error("helper_extract_edge_from_image: step must be >= 1.");
		return result;
	}
	const int w = image->get_width();
	const int h = image->get_height();
	if (w <= 0 || h <= 0) {
		if (!quiet) UtilityFunctions::push_error("helper_extract_edge_from_image: image has no pixels.");
		return result;
	}
	if (w > 2048 || h > 2048) {
		if (!quiet) UtilityFunctions::push_error("helper_extract_edge_from_image: image too large (max 2048x2048, got " + itos(w) + "x" + itos(h) + "); downscale or use the NODE source.");
		return result;
	}
	auto alpha_at = [&](int x, int y) -> real_t {
		x = Math::clamp(x, 0, w - 1);
		y = Math::clamp(y, 0, h - 1);
		return image->get_pixel(x, y).a;
	};
	PackedVector2Array points;
	PackedVector2Array normals;
	const Vector2 center((real_t)w * 0.5, (real_t)h * 0.5);
	for (int y = 0; y < h; y += step) {
		for (int x = 0; x < w; x += step) {
			if (alpha_at(x, y) < threshold) {
				continue;
			}
			// 4-neighborhood at stride step; out of bounds counts as empty.
			Vector2 outward_sum(0, 0);
			bool is_edge = false;
			const int nx[4] = { x + step, x - step, x, x };
			const int ny[4] = { y, y, y + step, y - step };
			const Vector2 dirs[4] = { Vector2(1, 0), Vector2(-1, 0), Vector2(0, 1), Vector2(0, -1) };
			for (int k = 0; k < 4; ++k) {
				bool empty = nx[k] < 0 || ny[k] < 0 || nx[k] >= w || ny[k] >= h || alpha_at(nx[k], ny[k]) < threshold;
				if (empty) {
					is_edge = true;
					outward_sum += dirs[k];
				}
			}
			if (!is_edge) {
				continue;
			}
			Vector2 nrm = outward_sum;
			if (nrm.length_squared() <= 1e-12) {
				// Isolated sample: fall back to the alpha gradient (inward),
				// negated to point outward.
				const real_t gx = alpha_at(x + step, y) - alpha_at(x - step, y);
				const real_t gy = alpha_at(x, y + step) - alpha_at(x, y - step);
				nrm = -Vector2(gx, gy);
			}
			if (nrm.length_squared() <= 1e-12) {
				nrm = Vector2(0, -1);
			}
			points.push_back(Vector2((real_t)x, (real_t)y) - center);
			normals.push_back(nrm.normalized());
		}
	}
	if (points.is_empty()) {
		if (!quiet) UtilityFunctions::push_error("helper_extract_edge_from_image: no edge pixels found (threshold too high or image fully transparent).");
	}
	result["points"] = points;
	result["normals"] = normals;
	return result;
}

} // namespace BlastBullets2D
