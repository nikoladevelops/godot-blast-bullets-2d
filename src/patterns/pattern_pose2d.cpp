#include "patterns/pattern_pose2d.hpp"

#include "godot_cpp/core/math.hpp"

using namespace godot;

namespace BlastBullets2D {

Transform2D PatternPose2D::make_spin_scale_matrix(const Vector2 &origin, real_t radians, real_t scale, bool &r_valid) {
	r_valid = false;
	if (spin_is_trivial(radians, scale)) {
		r_valid = true;
		return Transform2D();
	}
	const real_t c = Math::cos(radians);
	const real_t s = Math::sin(radians);
	if (!Math::is_finite(c) || !Math::is_finite(s) || !Math::is_finite(scale) || !origin.is_finite()) {
		return Transform2D();
	}
	// A = s·R: columns of the combined basis.
	const Vector2 ax = Vector2(c, s) * scale;
	const Vector2 ay = Vector2(-s, c) * scale;
	// Origin offset o − A·o, so out = M·t matches rotate-then-scale exactly.
	const Vector2 ao = Vector2(ax.x * origin.x + ay.x * origin.y, ax.y * origin.x + ay.y * origin.y);
	const Vector2 off = origin - ao;
	if (!ax.is_finite() || !ay.is_finite() || !off.is_finite()) {
		return Transform2D();
	}
	r_valid = true;
	return Transform2D(ax, ay, off);
}

bool PatternPose2D::build_skip_mask(const PackedInt32Array &skip_indices, int count, std::vector<uint8_t> &r_mask, bool &r_out_of_range) {
	r_out_of_range = false;
	r_mask.assign(count > 0 ? count : 0, 0);
	bool any = false;
	const int32_t *skip_p = skip_indices.ptr();
	for (int k = 0; k < skip_indices.size(); ++k) {
		const int idx = skip_p[k];
		if (idx < 0 || idx >= count) {
			r_out_of_range = true;
			continue;
		}
		r_mask[idx] = 1;
		any = true;
	}
	return any;
}

void PatternPose2D::pose(const std::vector<Transform2D> &raw, const Transform2D &spin_scale, bool use_matrix, const std::vector<uint8_t> *skip_mask, real_t local_factor, std::vector<Transform2D> &r_out) {
	const int count = (int)raw.size();
	r_out.reserve(r_out.size() + count);
	const bool skipping = skip_mask != nullptr && (int)skip_mask->size() == count;
	for (int i = 0; i < count; ++i) {
		if (skipping && (*skip_mask)[i]) {
			continue;
		}
		Transform2D t = raw[i];
		if (use_matrix) {
			t = spin_scale * t;
		}
		// Plain column scaling: set_scale(get_scale() * f) normalizes the
		// columns and re-applies a signed y scale, which flips mirrored
		// (det < 0) bases back and drops shear.
		if (local_factor != 1.0f) {
			t.columns[0] *= local_factor;
			t.columns[1] *= local_factor;
		}
		r_out.push_back(t);
	}
}

} //namespace BlastBullets2D
