#pragma once

// Value checks of the pattern knob table (patterns/pattern_knob_table2d.inc).
// Each returns true when the value FAILS; a and b are the row's bounds (0
// when unused). Every comparison is the one the hand-written setters did,
// so NaN fails exactly where it failed before.

#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/string.hpp"
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

} //namespace BlastBullets2D
