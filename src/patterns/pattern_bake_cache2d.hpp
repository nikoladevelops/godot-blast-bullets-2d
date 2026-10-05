#pragma once

// Raw-pattern bake cache: generate a pattern's raw transforms (pre spin /
// scale / skip) once per pattern version, then re-pose them per shot with one
// 2x3 multiply per bullet. The motion class is MEASURED with probe markers,
// never declared:
//   RIGID        the pattern follows any rigid move of its generator
//   TRANSLATION  it follows translation only (same basis, e.g. world rain)
//   NONE         regenerate every time (reads outside state, unseeded random)
// With verify on (tests run with it on), every served result is regenerated
// and compared; a mismatch is reported as an error.

#include "godot_cpp/variant/string.hpp"
#include "godot_cpp/variant/transform2d.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace BlastBullets2D {
using namespace godot;

enum PatternMotionClass2D {
	PATTERN_MOTION_NONE = 0,
	PATTERN_MOTION_TRANSLATION = 1,
	PATTERN_MOTION_RIGID = 2,
};

class PatternBakeCache2D {
public:
	// Channels: separate bakes so a mirrored burst shot or the preview never
	// evict the normal shot's bake.
	enum Channel {
		CHANNEL_SHOT = 0,
		CHANNEL_MIRRORED_SHOT = 1,
		CHANNEL_PREVIEW = 2,
		CHANNEL_COUNT = 3,
	};

	// Writes the raw pattern for `marker` into r_out (quiet: no errors).
	using Generator = std::function<void(const Transform2D &marker, bool quiet, std::vector<Transform2D> &r_out)>;

	// Serves the raw pattern for `marker` from the channel's bake when the
	// version still matches and the marker moved within the class rule,
	// otherwise generates (and bakes when cache_on and the source does not
	// read external state). verify_label prefixes the verifier's error.
	void resolve(int channel, uint64_t version, bool cache_on, bool reads_external_state, const Transform2D &marker, bool quiet, const Generator &generate, std::vector<Transform2D> &r_raw, const String &verify_label);

	// Preview fast path: would the preview bake survive this marker move
	// (rigid move of a RIGID bake, translation of a TRANSLATION bake)?
	bool survives_marker_move(uint64_t version, bool cache_on, const Transform2D &old_marker, const Transform2D &new_marker) const;

	// Motion class of a channel's current bake, -1 when stale/empty.
	int motion_class(int channel, uint64_t version) const;

	// Counters (debug_get_pattern_cache_info).
	uint64_t hits = 0;
	uint64_t misses = 0;
	uint64_t bakes = 0;

	// Global debug switch (BulletSpawner2D.debug_set_pattern_cache_verify).
	static bool verify;

	// Measures the motion class of a freshly generated raw pattern.
	static int classify(const Transform2D &marker, const std::vector<Transform2D> &raw, const Generator &generate);

	// Cache-parity helpers (shared with the verifier).
	static bool transforms_match(const Transform2D &a, const Transform2D &b);
	static bool basis_is_rigid(const Transform2D &t, real_t eps = (real_t)1e-5);
	static bool basis_equal(const Transform2D &a, const Transform2D &b, real_t eps = (real_t)1e-6);

private:
	struct Bake {
		bool valid = false;
		uint64_t version = 0;
		int motion_class = PATTERN_MOTION_NONE;
		Transform2D marker;
		Transform2D marker_inv;
		std::vector<Transform2D> raw;
	};
	Bake bakes_by_channel[CHANNEL_COUNT];
	std::vector<Transform2D> verify_scratch;
};

} //namespace BlastBullets2D
