#pragma once

// Pattern shape registry: the single source of truth for every pattern
// source (BulletSpawner2D.pattern_source): id, inspector name, the helper_*
// prefix of the knobs it owns, and its layout capabilities. The inspector
// hint, the name lookup, the capability predicates and the knob gating all
// read this one table, so they can never drift apart.
//
// SCENE COMPAT LOCK: ids are serialized as plain ints in .tscn files. Never
// renumber; append new shapes with the next free id.

#include "godot_cpp/variant/string.hpp"

namespace BlastBullets2D {
using namespace godot;

enum PatternShape2D {
	PATTERN_SHAPE_CHILDREN = 0,
	PATTERN_SHAPE_SELF = 1,
	PATTERN_SHAPE_GRID = 2,
	PATTERN_SHAPE_RING = 3,
	PATTERN_SHAPE_FAN = 4,
	PATTERN_SHAPE_SPIRAL = 5,
	PATTERN_SHAPE_LINE = 6,
	PATTERN_SHAPE_AIMED = 7,
	PATTERN_SHAPE_FLOWER = 8,
	PATTERN_SHAPE_ELLIPSE = 9,
	PATTERN_SHAPE_RAIN = 10,
	PATTERN_SHAPE_SCATTER = 11,
	PATTERN_SHAPE_STAR_POLYGON = 12,
	PATTERN_SHAPE_MULTISPIRAL = 13,
	PATTERN_SHAPE_CROSS = 14,
	PATTERN_SHAPE_STAR = 15,
	PATTERN_SHAPE_HEART = 16,
	PATTERN_SHAPE_WAVE = 17,
	PATTERN_SHAPE_WATERFALL = 18,
	PATTERN_SHAPE_LATTICE = 19,
	PATTERN_SHAPE_ROSE = 20,
	PATTERN_SHAPE_COUNTER_SPIRAL = 21,
	PATTERN_SHAPE_CORRIDOR = 22,
	PATTERN_SHAPE_LISSAJOUS = 23,
	PATTERN_SHAPE_CUSTOM = 24,
	PATTERN_SHAPE_CIRCLE = 25,
	PATTERN_SHAPE_RECTANGLE = 26,
	PATTERN_SHAPE_SQUARE = 27,
	PATTERN_SHAPE_POLYGON = 28,
	PATTERN_SHAPE_PATH2D = 29,
	PATTERN_SHAPE_TRIANGLE = 30,
	PATTERN_SHAPE_TRAPEZOID = 31,
	PATTERN_SHAPE_DIAMOND = 32,
	// One past the last real shape (never serialized, never valid).
	PATTERN_SHAPE_COUNT = 33
};

struct PatternShapeInfo2D {
	int id;
	const char *name; // inspector label
	const char *knob_prefix; // helper_<shape>_ (nullptr: owns no knobs)
	bool outline; // closed loop: the shared outline layout applies
	bool corners; // corner-anchored polygon loop: corner knobs apply
	bool reads_external_state; // reads nodes / user arrays: never baked
};

// Row order is the inspector order (historical), not id order.
const PatternShapeInfo2D *pattern_shapes2d(int &r_count);
const PatternShapeInfo2D *pattern_shape_info2d(int id);
const char *pattern_shape_name2d(int id);
bool pattern_shape_supports_outline2d(int id);
bool pattern_shape_supports_corners2d(int id);
bool pattern_shape_reads_external_state2d(int id);
// "Name:id,Name:id,..." for PROPERTY_HINT_ENUM.
String pattern_shape_hint2d();
// The shape whose knob prefix owns this property (longest registered prefix
// wins: helper_star_polygon_ beats helper_star_), -1 when none does.
int pattern_shape_owning_knob2d(const String &property_name);

} //namespace BlastBullets2D
