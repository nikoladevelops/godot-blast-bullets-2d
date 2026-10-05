#include "patterns/pattern_bake_cache2d.hpp"

#include "core/transform_math2d.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/variant/variant.hpp"

using namespace godot;

namespace BlastBullets2D {

bool PatternBakeCache2D::verify = false;

bool PatternBakeCache2D::basis_is_rigid(const Transform2D &t, real_t eps) {
	// Rotation + translation only (unit columns, orthogonal, det > 0).
	const Vector2 x = t.columns[0];
	const Vector2 y = t.columns[1];
	return Math::abs(x.length_squared() - 1.0) <= eps && Math::abs(y.length_squared() - 1.0) <= eps &&
			Math::abs(x.dot(y)) <= eps && (x.x * y.y - x.y * y.x) > 0.0;
}

bool PatternBakeCache2D::basis_equal(const Transform2D &a, const Transform2D &b, real_t eps) {
	return (a.columns[0] - b.columns[0]).length_squared() <= eps * eps && (a.columns[1] - b.columns[1]).length_squared() <= eps * eps;
}

bool PatternBakeCache2D::transforms_match(const Transform2D &a, const Transform2D &b) {
	// Origins within a pixel-fraction, basis columns (rotation AND scale)
	// within a tight relative tolerance.
	if (!a.is_finite() || !b.is_finite()) {
		return a.is_finite() == b.is_finite();
	}
	const real_t origin_tol = (real_t)1e-3 + (real_t)1e-5 * MAX(a.columns[2].length(), b.columns[2].length());
	if ((a.columns[2] - b.columns[2]).length() > origin_tol) {
		return false;
	}
	for (int k = 0; k < 2; ++k) {
		const real_t tol = (real_t)1e-4 * MAX((real_t)1.0, a.columns[k].length());
		if ((a.columns[k] - b.columns[k]).length() > tol) {
			return false;
		}
	}
	return true;
}

int PatternBakeCache2D::classify(const Transform2D &marker, const std::vector<Transform2D> &raw, const Generator &generate) {
	if (raw.empty() || !marker.is_finite() || !is_transform_invertible_safe2d(marker)) {
		return PATTERN_MOTION_NONE;
	}
	std::vector<Transform2D> probe;
	// Probe 1: a rigid motion with an awkward angle + offset. A generator
	// whose output follows (positions AND facings) is RIGID. Unseeded
	// randomness re-rolls between the two calls and fails here by itself.
	const Transform2D rigid_probe(0.7311, Vector2(37.25, -19.5));
	generate(rigid_probe * marker, true, probe);
	if (probe.size() == raw.size()) {
		bool ok = true;
		for (size_t i = 0; i < raw.size() && ok; ++i) {
			ok = transforms_match(rigid_probe * raw[i], probe[i]);
		}
		if (ok) {
			return PATTERN_MOTION_RIGID;
		}
	}
	// Probe 2: pure translation (world-direction patterns like rain).
	const Transform2D shift_probe(0.0, Vector2(41.5, -23.75));
	generate(shift_probe * marker, true, probe);
	if (probe.size() == raw.size()) {
		bool ok = true;
		for (size_t i = 0; i < raw.size() && ok; ++i) {
			ok = transforms_match(shift_probe * raw[i], probe[i]);
		}
		if (ok) {
			return PATTERN_MOTION_TRANSLATION;
		}
	}
	return PATTERN_MOTION_NONE;
}

void PatternBakeCache2D::resolve(int channel, uint64_t version, bool cache_on, bool reads_external_state, const Transform2D &marker, bool quiet, const Generator &generate, std::vector<Transform2D> &r_raw, const String &verify_label) {
	r_raw.clear();
	if (channel < 0 || channel >= CHANNEL_COUNT) {
		channel = CHANNEL_SHOT;
	}
	Bake &bake = bakes_by_channel[channel];
	if (cache_on && bake.valid && bake.version == version && bake.motion_class != PATTERN_MOTION_NONE) {
		bool served = false;
		if (bake.motion_class == PATTERN_MOTION_RIGID) {
			const Transform2D delta = marker * bake.marker_inv;
			if (delta.is_finite() && basis_is_rigid(delta)) {
				r_raw.resize(bake.raw.size());
				for (size_t i = 0; i < bake.raw.size(); ++i) {
					r_raw[i] = delta * bake.raw[i];
				}
				served = true;
			}
		} else if (basis_equal(marker, bake.marker)) {
			const Vector2 shift = marker.columns[2] - bake.marker.columns[2];
			r_raw = bake.raw;
			for (Transform2D &t : r_raw) {
				t.columns[2] += shift;
			}
			served = true;
		}
		if (served) {
			++hits;
			if (verify) {
				generate(marker, true, verify_scratch);
				int bad = -2;
				if (verify_scratch.size() != r_raw.size()) {
					bad = -1;
				} else {
					for (size_t i = 0; i < r_raw.size(); ++i) {
						if (!transforms_match(r_raw[i], verify_scratch[i])) {
							bad = (int)i;
							break;
						}
					}
				}
				if (bad != -2) {
					const String detail = bad < 0 ? String("size ") + itos((int64_t)r_raw.size()) + " vs " + itos((int64_t)verify_scratch.size())
												  : String("slot ") + itos(bad) + ": cached " + String(Variant(r_raw[bad])) + " vs fresh " + String(Variant(verify_scratch[bad]));
					UtilityFunctions::push_error(verify_label + String(", class ") + itos(bake.motion_class) + String(") ") + detail);
				}
			}
			return;
		}
	}
	// Miss: generate, then (re)bake. A version change re-probes the class;
	// a marker change outside the reuse rule just re-anchors the bake.
	++misses;
	generate(marker, quiet, r_raw);
	if (!cache_on) {
		return;
	}
	if (!bake.valid || bake.version != version) {
		bake.motion_class = reads_external_state ? (int)PATTERN_MOTION_NONE : classify(marker, r_raw, generate);
		bake.version = version;
		bake.valid = true;
	}
	if (bake.motion_class == PATTERN_MOTION_NONE) {
		bake.raw.clear();
		return;
	}
	bake.marker = marker;
	bake.marker_inv = marker.affine_inverse();
	bake.raw = r_raw;
	++bakes;
}

bool PatternBakeCache2D::survives_marker_move(uint64_t version, bool cache_on, const Transform2D &old_marker, const Transform2D &new_marker) const {
	const Bake &bake = bakes_by_channel[CHANNEL_PREVIEW];
	if (!cache_on || !bake.valid || bake.version != version) {
		return false;
	}
	if (!old_marker.is_finite() || !new_marker.is_finite()) {
		return false;
	}
	if (bake.motion_class == PATTERN_MOTION_RIGID) {
		if (!is_transform_invertible_safe2d(old_marker)) {
			return false;
		}
		return basis_is_rigid(new_marker * old_marker.affine_inverse());
	}
	if (bake.motion_class == PATTERN_MOTION_TRANSLATION) {
		return basis_equal(old_marker, new_marker);
	}
	return false;
}

int PatternBakeCache2D::motion_class(int channel, uint64_t version) const {
	if (channel < 0 || channel >= CHANNEL_COUNT) {
		return -1;
	}
	const Bake &bake = bakes_by_channel[channel];
	return (bake.valid && bake.version == version) ? bake.motion_class : -1;
}

} //namespace BlastBullets2D
