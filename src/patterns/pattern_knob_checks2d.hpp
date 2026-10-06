#pragma once

// Value checks of the pattern knob table (patterns/pattern_knob_table2d.inc).
// Each returns true when the value FAILS; a and b are the row's bounds (0
// when unused). Every comparison is the one the hand-written setters did,
// so NaN fails exactly where it failed before.

#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/packed_float32_array.hpp"
#include "godot_cpp/variant/string.hpp"
#include "godot_cpp/variant/transform2d.hpp"
#include "godot_cpp/variant/utility_functions.hpp"
#include "godot_cpp/variant/vector2.hpp"

namespace BlastBullets2D {
using namespace godot;

namespace PatternKnobCheck2D {

template <typename T>
static inline bool NONE(const T &, double, double) {
	return false;
}
// Floats.
static inline bool FINITE(double v, double, double) { return !Math::is_finite(v); }
static inline bool FINITE_GE(double v, double a, double) { return !Math::is_finite(v) || v < a; }
static inline bool FINITE_GT(double v, double a, double) { return !Math::is_finite(v) || !(v > a); }
static inline bool FINITE_IN(double v, double a, double b) { return !Math::is_finite(v) || v < a || v > b; }
static inline bool FINITE_IN_OC(double v, double a, double b) { return !Math::is_finite(v) || v <= a || v > b; }
static inline bool FINITE_IN_CO(double v, double a, double b) { return !Math::is_finite(v) || v < a || v >= b; }
// Numbers (ints and second-stage float checks).
static inline bool GE(double v, double a, double) { return v < a; }
static inline bool LE(double v, double a, double) { return v > a; } // single bound in a, like GE
static inline bool IN(double v, double a, double b) { return v < a || v > b; }
// Vectors.
static inline bool VEC_FINITE(const Vector2 &v, double, double) { return !v.is_finite(); }
static inline bool VEC_NONZERO(const Vector2 &v, double, double) { return v.length_squared() <= 0.0; }
static inline bool VEC_FINITE_NONZERO(const Vector2 &v, double, double) { return !v.is_finite() || v.length_squared() <= 0.0; }
static inline bool VEC_FINITE_SIDES_GE0(const Vector2 &v, double, double) { return !v.is_finite() || v.x < 0.0 || v.y < 0.0; }

} //namespace PatternKnobCheck2D

// "<owner>: <knob> <message>, keeping the old value." (the one wording every
// knob rejection uses).
static inline void pattern_knob_reject2d(const char *knob, const String &message, const char *owner = "BulletSpawner2D") {
	UtilityFunctions::push_error(String(owner) + ": " + knob + " " + message + ", keeping the old value.");
}

// Array knobs with hand-written accessors: nullptr when the value is valid,
// else the rejection message for pattern_knob_reject2d. Shared by the
// spawner setters and BulletPatterns2D.generate.
static inline const char *pattern_custom_transforms_problem2d(const Array &value, int max_entries) {
	if (value.size() > max_entries) {
		return "must hold <= 10000 entries";
	}
	for (int i = 0; i < value.size(); ++i) {
		// Type first, conversion second: inspector array edits can hand
		// transient nulls/wrong types across.
		const Variant element = value[i];
		if (element.get_type() != Variant::TRANSFORM2D) {
			return "must hold only Transform2D entries";
		}
		if (!((Transform2D)element).is_finite()) {
			return "must hold finite transforms";
		}
	}
	return nullptr;
}

static inline const char *pattern_layer_scales_problem2d(const PackedFloat32Array &value, int max_entries) {
	if (value.size() > max_entries) {
		return "holds at most 64 entries";
	}
	for (int i = 0; i < value.size(); ++i) {
		const double s = (double)value[i];
		if (!Math::is_finite(s) || s < 0.05 || s > 64.0) {
			return "entries must be finite in [0.05, 64]";
		}
	}
	return nullptr;
}

} //namespace BlastBullets2D
