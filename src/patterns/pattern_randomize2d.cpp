// BulletPatterns2D.randomize_transforms: the spawner's placement rolls
// (ShotRandomizer2D) for scripts, with the spawner's setting names and rules.

#include "patterns/bullet_patterns2d.hpp"
#include "patterns/shot_randomizer2d.hpp"

#include <godot_cpp/variant/utility_functions.hpp>

#include <vector>

using namespace godot;

namespace BlastBullets2D {

namespace {

constexpr const char *RANDOMIZE_FN = "BulletPatterns2D.randomize_transforms";

void randomize_error(const String &text) {
	UtilityFunctions::push_error(String(RANDOMIZE_FN) + ": " + text + ", nothing randomized.");
}

bool read_number(const Variant &value, double &r_out) {
	if (value.get_type() != Variant::INT && value.get_type() != Variant::FLOAT) {
		return false;
	}
	r_out = value;
	return Math::is_finite(r_out);
}

// Fills `r` from `settings` by the spawner's rules; false after one error.
bool read_settings(const Dictionary &settings, ShotRandomizer2D &r) {
	const Array keys = settings.keys();
	for (int64_t i = 0; i < keys.size(); ++i) {
		const Variant key_variant = keys[i];
		if (key_variant.get_type() != Variant::STRING && key_variant.get_type() != Variant::STRING_NAME) {
			randomize_error("every settings key must be a String");
			return false;
		}
		const String key = key_variant;
		const Variant value = settings[key_variant];
		double number = 0.0;
		if (key == "bullet_scale_mode") {
			const int64_t mode = value.get_type() == Variant::INT ? (int64_t)value : -1;
			if (mode < ShotRandomizer2D::SCALE_OFF || mode > ShotRandomizer2D::SCALE_PER_AXIS) {
				randomize_error("bullet_scale_mode must be 0 (Off), 1 (Uniform) or 2 (Per Axis)");
				return false;
			}
			r.scale_mode = (int)mode;
		} else if (key == "bullet_scale_min" || key == "bullet_scale_max") {
			if (!read_number(value, number) || !(number > 0.0)) {
				randomize_error(key + String(" must be finite and > 0"));
				return false;
			}
			(key == "bullet_scale_min" ? r.scale_min : r.scale_max) = number;
		} else if (key == "bullet_scale_min_xy" || key == "bullet_scale_max_xy") {
			const Vector2 v = value.get_type() == Variant::VECTOR2 ? (Vector2)value : Vector2(-1, -1);
			if (!Math::is_finite(v.x) || !Math::is_finite(v.y) || !(v.x > 0.0f) || !(v.y > 0.0f)) {
				randomize_error(key + String(" must be a finite Vector2 with both components > 0"));
				return false;
			}
			(key == "bullet_scale_min_xy" ? r.scale_min_xy : r.scale_max_xy) = v;
		} else if (key == "bullet_rotation_min_degrees" || key == "bullet_rotation_max_degrees") {
			if (!read_number(value, number)) {
				randomize_error(key + String(" must be a finite number"));
				return false;
			}
			(key == "bullet_rotation_min_degrees" ? r.rotation_min_degrees : r.rotation_max_degrees) = number;
		} else if (key == "bullet_position_jitter_min" || key == "bullet_position_jitter_max") {
			if (!read_number(value, number) || number < 0.0) {
				randomize_error(key + String(" must be finite and >= 0"));
				return false;
			}
			(key == "bullet_position_jitter_min" ? r.position_jitter_min : r.position_jitter_max) = number;
		} else {
			randomize_error(String("unknown setting \"") + key + String("\" (bullet_scale_mode, bullet_scale_min, bullet_scale_max, bullet_scale_min_xy, bullet_scale_max_xy, bullet_rotation_min_degrees, bullet_rotation_max_degrees, bullet_position_jitter_min, bullet_position_jitter_max)"));
			return false;
		}
	}
	return true;
}

} // namespace

TypedArray<Transform2D> BulletPatterns2D::randomize_transforms(const TypedArray<Transform2D> &transforms, const Dictionary &settings, int64_t seed, int64_t shot_index) {
	TypedArray<Transform2D> out;
	if (shot_index < 0) {
		randomize_error("shot_index must be >= 0");
		return out;
	}
	ShotRandomizer2D r;
	r.seed = seed;
	if (!read_settings(settings, r)) {
		return out;
	}
	std::vector<Transform2D> slots((size_t)transforms.size());
	for (int64_t i = 0; i < transforms.size(); ++i) {
		const Transform2D t = transforms[i];
		if (!t.is_finite()) {
			randomize_error(String("transforms[") + String::num_int64(i) + String("] must be finite"));
			return out;
		}
		slots[(size_t)i] = t;
	}
	r.apply(slots, (uint64_t)shot_index);
	out.resize((int64_t)slots.size());
	for (size_t i = 0; i < slots.size(); ++i) {
		out[(int64_t)i] = slots[i];
	}
	return out;
}

} // namespace BlastBullets2D
