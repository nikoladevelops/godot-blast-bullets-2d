#pragma once

#include "godot_cpp/core/math.hpp"
#include "godot_cpp/variant/color.hpp"

namespace BlastBullets2D {
using namespace godot;

// A color every later write can carry: modulate, a MultiMesh instance color,
// a `draw_polygon` vertex color. A NaN component (a Gradient resource a tool
// wrote, a script-authored sample) propagates into the renderer and never
// compares equal again, so every sampled color is checked BEFORE it is
// applied. One definition for the volley colors API, the fade ramp, the
// trail/one-shot tints and the laser body ramp.
inline bool color_is_finite2d(const Color &c) {
	return Math::is_finite(c.r) && Math::is_finite(c.g) && Math::is_finite(c.b) && Math::is_finite(c.a);
}

} //namespace BlastBullets2D
