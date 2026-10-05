#pragma once

// PatternSlots2D: the native result of a pattern generator (one global
// Transform2D per bullet), and its conversion to the TypedArray GDScript sees.

#include "patterns/bullet_patterns2d.hpp"
#include "godot_cpp/variant/transform2d.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include <vector>

namespace BlastBullets2D {
using namespace godot;

inline TypedArray<Transform2D> pattern_slots_to_array(const PatternSlots2D &slots) {
	TypedArray<Transform2D> out;
	out.resize((int64_t)slots.size());
	for (size_t i = 0; i < slots.size(); ++i) {
		out[(int64_t)i] = slots[i];
	}
	return out;
}

} //namespace BlastBullets2D
