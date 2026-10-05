// BulletSpawner2D (split from bullet_spawner2d.cpp, same class):
// PatternPreviewLayer2D: the batched (draw_multimesh) canvas layer that
// renders preview dots, rings, arrows and tracks.

#include "bullet_spawner/bullet_spawner2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

// Self-repainting preview layer: rebuild_preview() stores a snapshot via the
// setters below, the engine re-invokes _draw() on every repaint (zoom, pan,
// selection, idle refresh), so the gizmo can never vanish between rebuilds.
void PatternPreviewLayer2D::_bind_methods() {
}

void PatternPreviewLayer2D::set_dots_data(const PackedVector2Array &p_dots, const Color &p_color, float p_radius) {
	dots = p_dots;
	dot_color = p_color;
	dot_radius = p_radius;
	queue_redraw();
}

void PatternPreviewLayer2D::set_first_marker(bool p_show, const Color &p_color, float p_radius_scale) {
	show_first_marker = p_show;
	first_dot_color = p_color;
	first_dot_radius_scale = p_radius_scale;
	queue_redraw();
}

void PatternPreviewLayer2D::set_path_data(const PackedVector2Array &p_points, const Color &p_color, float p_width, bool p_closed) {
	path_points = p_points;
	path_color = p_color;
	path_width = p_width;
	path_closed = p_closed;
	queue_redraw();
}

void PatternPreviewLayer2D::set_layer_path_data(const PackedVector2Array &p_points, const Color &p_color) {
	layer_path_points = p_points;
	layer_path_color = p_color;
	queue_redraw();
}

void PatternPreviewLayer2D::set_arrows_data(const PackedVector2Array &p_tails, const PackedVector2Array &p_dirs, const Color &p_color, float p_length, float p_width, float p_head_length, float p_head_width) {
	arrow_tails = p_tails;
	arrow_dirs = p_dirs;
	arrow_color = p_color;
	arrow_length = p_length;
	arrow_width = p_width;
	arrow_head_length = p_head_length;
	arrow_head_width = p_head_width;
	queue_redraw();
}

void PatternPreviewLayer2D::set_rings_data(float p_radius, const Color &p_color, float p_width) {
	ring_radius = p_radius;
	ring_color = p_color;
	ring_width = p_width;
	queue_redraw();
}

void PatternPreviewLayer2D::set_pose(const Transform2D &p_pose) {
	// Node transform only: no queue_redraw. The canvas item keeps its
	// command list; the server just re-transforms it.
	if (!p_pose.is_finite() || get_transform() == p_pose) {
		return;
	}
	set_transform(p_pose);
}

// Unit glyph meshes (2D triangle lists). Built per layer, never static: a
// static Ref would outlive the engine at extension unload.
static Ref<ArrayMesh> make_2d_triangle_mesh(const PackedVector2Array &p_vertices) {
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = p_vertices;
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

static constexpr int kPreviewCircleSegments = 24;

// Filled unit disc as a triangle list (center fan).
static PackedVector2Array unit_disc_triangles() {
	PackedVector2Array v;
	v.resize(kPreviewCircleSegments * 3);
	Vector2 *w = v.ptrw();
	for (int i = 0; i < kPreviewCircleSegments; ++i) {
		const real_t a0 = Math::TAU * (real_t)i / (real_t)kPreviewCircleSegments;
		const real_t a1 = Math::TAU * (real_t)(i + 1) / (real_t)kPreviewCircleSegments;
		w[i * 3 + 0] = Vector2();
		w[i * 3 + 1] = Vector2(Math::cos(a0), Math::sin(a0));
		w[i * 3 + 2] = Vector2(Math::cos(a1), Math::sin(a1));
	}
	return v;
}

// Annulus [r_in, r_out] as a triangle list (two triangles per segment).
static PackedVector2Array annulus_triangles(real_t r_in, real_t r_out) {
	PackedVector2Array v;
	v.resize(kPreviewCircleSegments * 6);
	Vector2 *w = v.ptrw();
	for (int i = 0; i < kPreviewCircleSegments; ++i) {
		const real_t a0 = Math::TAU * (real_t)i / (real_t)kPreviewCircleSegments;
		const real_t a1 = Math::TAU * (real_t)(i + 1) / (real_t)kPreviewCircleSegments;
		const Vector2 d0(Math::cos(a0), Math::sin(a0));
		const Vector2 d1(Math::cos(a1), Math::sin(a1));
		w[i * 6 + 0] = d0 * r_in;
		w[i * 6 + 1] = d0 * r_out;
		w[i * 6 + 2] = d1 * r_out;
		w[i * 6 + 3] = d0 * r_in;
		w[i * 6 + 4] = d1 * r_out;
		w[i * 6 + 5] = d1 * r_in;
	}
	return v;
}

// Fills a 2D multimesh (transform + color) from `count` instances in one
// set_buffer call. Instance i: basis (dir * sx, perp * sy), origin.
static void fill_glyph_multimesh(Ref<MultiMesh> &mm, const Ref<Mesh> &mesh, int count, const Color &color, PackedFloat32Array &buffer,
		const std::function<bool(int, Vector2 &, Vector2 &, Vector2 &)> &instance_at) {
	if (mm.is_null()) {
		mm.instantiate();
		mm->set_transform_format(MultiMesh::TRANSFORM_2D);
		mm->set_use_colors(true);
	}
	mm->set_mesh(mesh);
	// Count finite instances first so the buffer has no holes.
	buffer.resize(MAX(count, 0) * 12);
	float *w = buffer.ptrw();
	int n = 0;
	for (int i = 0; i < count; ++i) {
		Vector2 bx, by, origin;
		if (!instance_at(i, bx, by, origin)) {
			continue;
		}
		float *o = w + n * 12;
		o[0] = bx.x;
		o[1] = by.x;
		o[2] = 0.0f;
		o[3] = origin.x;
		o[4] = bx.y;
		o[5] = by.y;
		o[6] = 0.0f;
		o[7] = origin.y;
		o[8] = color.r;
		o[9] = color.g;
		o[10] = color.b;
		o[11] = color.a;
		++n;
	}
	if (mm->get_instance_count() != n) {
		mm->set_instance_count(n);
	}
	if (n > 0) {
		buffer.resize(n * 12);
		mm->set_buffer(buffer);
	}
}

void PatternPreviewLayer2D::_draw() {
	++debug_draw_count;
	if (kind == LAYER_DOTS) {
		// Track as polylines split on non-finite separators: samplers emit
		// INF points between strips (grid rows, spiral arms), and each run
		// draws separately so rows/arms never bridge. Single runs batch
		// into one call; run_scratch is reused (no per-repaint allocation).
		if (path_width > 0.0f && path_points.size() >= 2) {
			draw_scratch.clear();
			bool has_gap = false;
			for (int i = 0; i < path_points.size(); i++) {
				const Vector2 p = path_points[i];
				if (p.is_finite()) {
					draw_scratch.push_back(p);
				} else {
					has_gap = true;
				}
			}
			if (!has_gap) {
				if (draw_scratch.size() >= 2) {
					if (path_closed && draw_scratch.size() >= 3) {
						draw_scratch.push_back(draw_scratch[0]);
					}
					draw_polyline(draw_scratch, path_color, path_width, false);
				}
			} else {
				// Split at the separators: each finite run is its own strip.
				// Closed loops never emit separators by contract, so runs
				// here are always open.
				run_scratch.clear();
				for (int i = 0; i <= path_points.size(); i++) {
					const bool valid = i < path_points.size() && path_points[i].is_finite();
					if (valid) {
						run_scratch.push_back(path_points[i]);
					} else if (run_scratch.size() >= 2) {
						draw_polyline(run_scratch, path_color, path_width, false);
						run_scratch.clear();
					} else {
						run_scratch.clear();
					}
				}
			}
		}
		// Extra outline-layer rings (one closed run per layer, pre-closed at
		// build time, INF-separated). Same width as the base track, never
		// antialiased. Degenerate runs (< 2 points after filtering) draw
		// nothing; runs are independent so one bad ring cannot kill the rest.
		if (path_width > 0.0f && layer_path_points.size() >= 2) {
			run_scratch.clear();
			for (int i = 0; i <= layer_path_points.size(); i++) {
				const bool valid = i < layer_path_points.size() && layer_path_points[i].is_finite();
				if (valid) {
					run_scratch.push_back(layer_path_points[i]);
				} else if (run_scratch.size() >= 2) {
					draw_polyline(run_scratch, layer_path_color, path_width, false);
					run_scratch.clear();
				} else {
					run_scratch.clear();
				}
			}
		}
		// Every dot, full fidelity, in ONE draw call (multimesh of a unit
		// disc scaled by dot_radius). The first marker draws last so later
		// dots on a closed loop never bury it.
		const int dot_count = dots.size();
		if (dot_radius > 0.0f && dot_count > 0) {
			if (dot_mesh.is_null()) {
				dot_mesh = make_2d_triangle_mesh(unit_disc_triangles());
			}
			const Vector2 *dp = dots.ptr();
			const real_t r = dot_radius;
			fill_glyph_multimesh(dots_multimesh, dot_mesh, dot_count, dot_color, glyph_buffer, [dp, r](int i, Vector2 &bx, Vector2 &by, Vector2 &o) {
				if (!dp[i].is_finite()) {
					return false;
				}
				bx = Vector2(r, 0);
				by = Vector2(0, r);
				o = dp[i];
				return true;
			});
			draw_multimesh(dots_multimesh, Ref<Texture2D>());
		}
		if (show_first_marker && !dots.is_empty() && first_dot_radius_scale > 0.0f && dot_radius > 0.0f) {
			const Vector2 p0 = dots[0];
			if (p0.is_finite()) {
				draw_circle(p0, dot_radius * first_dot_radius_scale, first_dot_color);
			}
		}
		// Collision-ring overlay: bounding-radius outline per dot (hitbox vs
		// visual), one multimesh of a prebuilt annulus. <= 0 hides.
		if (ring_radius > 0.0f && ring_width > 0.0f && dot_count > 0) {
			if (ring_mesh.is_null() || ring_mesh_radius != ring_radius || ring_mesh_width != ring_width) {
				const real_t half = ring_width * 0.5f;
				ring_mesh = make_2d_triangle_mesh(annulus_triangles(MAX(ring_radius - half, (real_t)0.0), ring_radius + half));
				ring_mesh_radius = ring_radius;
				ring_mesh_width = ring_width;
			}
			const Vector2 *dp = dots.ptr();
			fill_glyph_multimesh(rings_multimesh, ring_mesh, dot_count, ring_color, glyph_buffer, [dp](int i, Vector2 &bx, Vector2 &by, Vector2 &o) {
				if (!dp[i].is_finite()) {
					return false;
				}
				bx = Vector2(1, 0);
				by = Vector2(0, 1);
				o = dp[i];
				return true;
			});
			draw_multimesh(rings_multimesh, Ref<Texture2D>());
		}
		return;
	}
	// One true arrow silhouette per bullet: the shaft ends exactly where the
	// head begins (clamped, never inverted), and the filled triangular head
	// sits forward of that joint. Shafts: one draw_multiline. Heads: one
	// draw_multimesh of a unit triangle (tip at +X) oriented per arrow.
	const int count = MIN(arrow_tails.size(), arrow_dirs.size());
	draw_scratch.clear();
	const bool want_shafts = arrow_length > 0.0f && arrow_width > 0.0f;
	const float head_len = MIN(arrow_head_length, arrow_length);
	const bool want_heads = head_len > 0.0f && arrow_head_width > 0.0f;
	const Vector2 *tails_p = arrow_tails.ptr();
	const Vector2 *dirs_p = arrow_dirs.ptr();
	auto arrow_ok = [&](int i) {
		const Vector2 dir = dirs_p[i];
		return dir.is_finite() && tails_p[i].is_finite() && dir.length_squared() >= 0.00000001;
	};
	if (want_shafts) {
		for (int i = 0; i < count; i++) {
			if (!arrow_ok(i)) {
				continue;
			}
			const Vector2 tip = tails_p[i] + dirs_p[i] * arrow_length;
			draw_scratch.push_back(tails_p[i]);
			draw_scratch.push_back(want_heads ? tip - dirs_p[i] * head_len : tip);
		}
		if (draw_scratch.size() >= 2) {
			draw_multiline(draw_scratch, arrow_color, arrow_width, false);
		}
	}
	if (want_heads && count > 0) {
		if (head_mesh.is_null() || head_mesh_length != head_len || head_mesh_width != arrow_head_width) {
			PackedVector2Array tri;
			tri.push_back(Vector2(head_len, 0));
			tri.push_back(Vector2(0, arrow_head_width * 0.5f));
			tri.push_back(Vector2(0, -arrow_head_width * 0.5f));
			head_mesh = make_2d_triangle_mesh(tri);
			head_mesh_length = head_len;
			head_mesh_width = arrow_head_width;
		}
		const real_t len = arrow_length;
		const real_t hl = head_len;
		fill_glyph_multimesh(heads_multimesh, head_mesh, count, arrow_color, glyph_buffer, [&, len, hl](int i, Vector2 &bx, Vector2 &by, Vector2 &o) {
			if (!arrow_ok(i)) {
				return false;
			}
			const Vector2 dir = dirs_p[i];
			bx = dir;
			by = dir.orthogonal();
			o = tails_p[i] + dir * (len - hl);
			return true;
		});
		draw_multimesh(heads_multimesh, Ref<Texture2D>());
	}
}

} // namespace BlastBullets2D
