// BulletPatterns2D.generate: any pattern source by id with its knobs given by
// name, without a spawner. Knobs are written through the same table and the
// same checks as the BulletSpawner2D setters (pattern_knob_table2d.inc), so
// a value a spawner refuses is refused here too, with the same wording.

#include "patterns/pattern_knob_checks2d.hpp"
#include "patterns/pattern_knobs2d.hpp"
#include "patterns/pattern_registry2d.hpp"
#include "patterns/patterns_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

static const char *kGenerateOwner = "BulletPatterns2D.generate";

// Variant -> knob value. Numbers accept int or float; ints need an int.
static bool knob_value(const Variant &v, double &r) {
	if (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) {
		r = (double)v;
		return true;
	}
	return false;
}
static bool knob_value(const Variant &v, int &r) {
	if (v.get_type() == Variant::INT) {
		r = (int)v;
		return true;
	}
	return false;
}
static bool knob_value(const Variant &v, bool &r) {
	if (v.get_type() == Variant::BOOL) {
		r = (bool)v;
		return true;
	}
	return false;
}
static bool knob_value(const Variant &v, Vector2 &r) {
	if (v.get_type() == Variant::VECTOR2 || v.get_type() == Variant::VECTOR2I) {
		r = (Vector2)v;
		return true;
	}
	return false;
}
static bool knob_value(const Variant &v, PackedInt32Array &r) {
	if (v.get_type() == Variant::PACKED_INT32_ARRAY || v.get_type() == Variant::ARRAY) {
		r = v;
		return true;
	}
	return false;
}
static const char *knob_kind(double) { return "a number"; }
static const char *knob_kind(int) { return "an int"; }
static const char *knob_kind(bool) { return "a bool"; }
static const char *knob_kind(const Vector2 &) { return "a Vector2"; }
static const char *knob_kind(const PackedInt32Array &) { return "an array of ints"; }

enum KnobWrite2D {
	KNOB_UNKNOWN,
	KNOB_WRITTEN,
	KNOB_REJECTED,
};

// Writes one table-driven knob by its property name.
static KnobWrite2D write_table_knob(PatternKnobs2D &k, const String &name, const Variant &v) {
#define PATTERN_SUBGROUP(TITLE, PREFIX)
#define PATTERN_PROPERTY(VTYPE, NAME, HINT, HINT_STRING, SETTER, GETTER)
#define PATTERN_KNOB(PTYPE, CTYPE, VTYPE, NAME, C1, A1, B1, M1, C2, A2, B2, M2, NOTIFY, HINT, HINT_STRING) \
	if (name == #NAME) {                                                                                   \
		CTYPE value{};                                                                                     \
		if (!knob_value(v, value)) {                                                                       \
			pattern_knob_reject2d(#NAME, String("must be ") + knob_kind(value), kGenerateOwner);           \
			return KNOB_REJECTED;                                                                          \
		}                                                                                                  \
		if (PatternKnobCheck2D::C1(value, A1, B1)) {                                                       \
			pattern_knob_reject2d(#NAME, M1, kGenerateOwner);                                              \
			return KNOB_REJECTED;                                                                          \
		}                                                                                                  \
		if (PatternKnobCheck2D::C2(value, A2, B2)) {                                                       \
			pattern_knob_reject2d(#NAME, M2, kGenerateOwner);                                              \
			return KNOB_REJECTED;                                                                          \
		}                                                                                                  \
		k.NAME = value;                                                                                    \
		return KNOB_WRITTEN;                                                                               \
	}
#include "patterns/pattern_knob_table2d.inc"
#undef PATTERN_SUBGROUP
#undef PATTERN_PROPERTY
#undef PATTERN_KNOB
	return KNOB_UNKNOWN;
}

// The knobs whose spawner accessors are hand-written (arrays, enum-typed
// Path2D knobs), with the spawner's rules and wording.
static KnobWrite2D write_special_knob(PatternKnobs2D &k, const String &name, const Variant &v) {
	struct EnumKnob {
		const char *name;
		int PatternKnobs2D::*field;
		int max;
	};
	static const EnumKnob kEnums[] = {
		{ "helper_path2d_space", &PatternKnobs2D::helper_path2d_space, 1 },
		{ "helper_path2d_distribution", &PatternKnobs2D::helper_path2d_distribution, 1 },
		{ "helper_path2d_overflow", &PatternKnobs2D::helper_path2d_overflow, 2 },
		{ "helper_path2d_anchor", &PatternKnobs2D::helper_path2d_anchor, 2 },
		{ "helper_path2d_facing", &PatternKnobs2D::helper_path2d_facing, 2 },
	};
	for (const EnumKnob &e : kEnums) {
		if (name == e.name) {
			if (v.get_type() != Variant::INT || (int)v < 0 || (int)v > e.max) {
				pattern_knob_reject2d(e.name, String("must be an int in [0, ") + itos(e.max) + "]", kGenerateOwner);
				return KNOB_REJECTED;
			}
			k.*(e.field) = (int)v;
			return KNOB_WRITTEN;
		}
	}
	if (name == "helper_custom_transforms") {
		if (v.get_type() != Variant::ARRAY) {
			pattern_knob_reject2d("helper_custom_transforms", "must be an Array of Transform2D", kGenerateOwner);
			return KNOB_REJECTED;
		}
		const Array arr = v;
		if (arr.size() > kPatternMaxBullets) {
			pattern_knob_reject2d("helper_custom_transforms", "must hold <= 10000 entries", kGenerateOwner);
			return KNOB_REJECTED;
		}
		TypedArray<Transform2D> out;
		for (int i = 0; i < arr.size(); ++i) {
			if (arr[i].get_type() != Variant::TRANSFORM2D) {
				pattern_knob_reject2d("helper_custom_transforms", "must hold only Transform2D entries", kGenerateOwner);
				return KNOB_REJECTED;
			}
			if (!((Transform2D)arr[i]).is_finite()) {
				pattern_knob_reject2d("helper_custom_transforms", "must hold finite transforms", kGenerateOwner);
				return KNOB_REJECTED;
			}
			out.push_back(arr[i]);
		}
		k.helper_custom_transforms = out;
		return KNOB_WRITTEN;
	}
	if (name == "helper_outline_layer_scales") {
		if (v.get_type() != Variant::PACKED_FLOAT32_ARRAY && v.get_type() != Variant::PACKED_FLOAT64_ARRAY && v.get_type() != Variant::ARRAY) {
			pattern_knob_reject2d("helper_outline_layer_scales", "must be an array of numbers", kGenerateOwner);
			return KNOB_REJECTED;
		}
		const PackedFloat32Array scales = v;
		if (scales.size() > kPatternMaxOutlineLayers) {
			pattern_knob_reject2d("helper_outline_layer_scales", "holds at most 64 entries", kGenerateOwner);
			return KNOB_REJECTED;
		}
		for (int i = 0; i < scales.size(); ++i) {
			const double s = (double)scales[i];
			if (!Math::is_finite(s) || s < 0.05 || s > 64.0) {
				pattern_knob_reject2d("helper_outline_layer_scales", "entries must be finite in [0.05, 64]", kGenerateOwner);
				return KNOB_REJECTED;
			}
		}
		k.helper_outline_layer_scales = scales;
		return KNOB_WRITTEN;
	}
	return KNOB_UNKNOWN;
}

// Every knob name generate() understands (did-you-mean candidates).
static const char *const kGenerateKnobNames[] = {
#define PATTERN_SUBGROUP(TITLE, PREFIX)
#define PATTERN_PROPERTY(VTYPE, NAME, HINT, HINT_STRING, SETTER, GETTER)
#define PATTERN_KNOB(PTYPE, CTYPE, VTYPE, NAME, ...) #NAME,
#include "patterns/pattern_knob_table2d.inc"
#undef PATTERN_SUBGROUP
#undef PATTERN_PROPERTY
#undef PATTERN_KNOB
	"helper_path2d_space",
	"helper_path2d_distribution",
	"helper_path2d_overflow",
	"helper_path2d_anchor",
	"helper_path2d_facing",
	"helper_custom_transforms",
	"helper_outline_layer_scales",
};

// The scene inputs a spawner would read from the tree.
static const char *const kGenerateInputs[] = { "aim_position", "path_points", "children" };

TypedArray<Transform2D> BulletPatterns2D::generate(int shape, int amount, const Transform2D &marker_transform, const Dictionary &params) {
	if (pattern_shape_info2d(shape) == nullptr) {
		UtilityFunctions::push_error(String(kGenerateOwner) + ": unknown shape " + itos(shape) + " (see BulletPatterns2D.get_shapes()), nothing generated.");
		return TypedArray<Transform2D>();
	}
	if (!marker_transform.is_finite()) {
		UtilityFunctions::push_error(String(kGenerateOwner) + ": marker_transform must be finite, nothing generated.");
		return TypedArray<Transform2D>();
	}
	PatternKnobs2D knobs;
	if (write_table_knob(knobs, "helper_bullets_amount", amount) != KNOB_WRITTEN) {
		return TypedArray<Transform2D>(); // the amount rejection says why
	}
	PatternInputs2D in;
	in.source = shape;
	in.marker = marker_transform;
	in.warn_owner_id = 0;
	std::vector<Transform2D> children;
	PackedVector2Array path_points;
	const Array keys = params.keys();
	for (int i = 0; i < keys.size(); ++i) {
		if (keys[i].get_type() != Variant::STRING && keys[i].get_type() != Variant::STRING_NAME) {
			UtilityFunctions::push_error(String(kGenerateOwner) + ": params keys must be knob names (strings), ignoring " + String(keys[i]) + ".");
			continue;
		}
		const String key = keys[i];
		const Variant value = params[keys[i]];
		if (key == "aim_position") {
			if (value.get_type() != Variant::VECTOR2 || !((Vector2)value).is_finite()) {
				UtilityFunctions::push_error(String(kGenerateOwner) + ": aim_position must be a finite Vector2, ignoring it.");
				continue;
			}
			in.has_aim_target = true;
			in.aim_position = value;
			continue;
		}
		if (key == "path_points") {
			if (value.get_type() != Variant::PACKED_VECTOR2_ARRAY) {
				UtilityFunctions::push_error(String(kGenerateOwner) + ": path_points must be a PackedVector2Array (marker-local), ignoring it.");
				continue;
			}
			path_points = value;
			continue;
		}
		if (key == "children") {
			if (value.get_type() != Variant::ARRAY) {
				UtilityFunctions::push_error(String(kGenerateOwner) + ": children must be an Array of global Transform2D, ignoring it.");
				continue;
			}
			const Array arr = value;
			for (int c = 0; c < arr.size(); ++c) {
				if (arr[c].get_type() == Variant::TRANSFORM2D && ((Transform2D)arr[c]).is_finite()) {
					children.push_back(arr[c]);
				}
			}
			continue;
		}
		// Knob names work with or without the helper_ prefix.
		const String knob = key.begins_with("helper_") ? key : String("helper_") + key;
		KnobWrite2D written = write_table_knob(knobs, knob, value);
		if (written == KNOB_UNKNOWN) {
			written = write_special_knob(knobs, knob, value);
		}
		if (written == KNOB_UNKNOWN) {
			String best;
			double best_score = 0.0;
			for (const char *candidate : kGenerateKnobNames) {
				const String short_name = String(candidate).trim_prefix("helper_");
				const double score = MAX(key.similarity(String(candidate)), key.similarity(short_name));
				if (score > best_score) {
					best_score = score;
					best = short_name;
				}
			}
			for (const char *input : kGenerateInputs) {
				const double score = key.similarity(String(input));
				if (score > best_score) {
					best_score = score;
					best = input;
				}
			}
			const String hint = best_score >= 0.5 ? String(" (did you mean '") + best + "'?)" : String();
			UtilityFunctions::push_error(String(kGenerateOwner) + ": unknown param '" + key + "'" + hint + ", ignoring it.");
		}
	}
	if (shape == PATTERN_SHAPE_AIMED && !in.has_aim_target) {
		UtilityFunctions::push_error(String(kGenerateOwner) + ": the Aimed shape needs params.aim_position (a global Vector2), nothing generated.");
		return TypedArray<Transform2D>();
	}
	if (shape == PATTERN_SHAPE_PATH2D && path_points.size() < 2) {
		UtilityFunctions::push_error(String(kGenerateOwner) + ": the Path2D shape needs params.path_points (at least 2 marker-local points), nothing generated.");
		return TypedArray<Transform2D>();
	}
	if (shape == PATTERN_SHAPE_CORRIDOR) {
		// A spawner aims the corridor at its live target when it has one,
		// else along helper_corridor_aim_direction.
		in.corridor_aim = knobs.helper_corridor_aim_direction;
		if (in.has_aim_target) {
			const Vector2 to_target = in.aim_position - marker_transform.get_origin();
			if (to_target.is_finite() && to_target.length_squared() > 0.0) {
				in.corridor_aim = to_target.normalized();
			}
		}
	}
	if (shape == PATTERN_SHAPE_PATH2D && knobs.helper_path2d_space == 0) {
		// Follow Generator (default), exactly like a spawner: the curve
		// shape blooms around the marker wherever its points were drawn.
		path_points = recenter_polyline2d(path_points);
	}
	in.children = &children;
	in.path_points = &path_points;
	PatternSlots2D raw = knobs.generate_raw(in);
	TypedArray<Transform2D> out = pattern_slots_to_array(raw);
	if (!knobs.helper_skip_indices.is_empty()) {
		out = helper_apply_skip_indices(out, knobs.helper_skip_indices);
	}
	return out;
}

} //namespace BlastBullets2D
