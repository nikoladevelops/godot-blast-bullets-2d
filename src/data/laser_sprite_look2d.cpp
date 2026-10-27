#include "data/laser_sprite_look2d.hpp"

#include "core/assign_if_changed2d.hpp"
#include "core/transform_math2d.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace BlastBullets2D {

namespace {

bool color_ok2d(const Color &c) {
	return Math::is_finite(c.r) && Math::is_finite(c.g) && Math::is_finite(c.b) && Math::is_finite(c.a);
}

} // namespace

void LaserSpriteLook2D::set_frames(const Ref<SpriteFrames> &value) {
	if (!assign_if_changed2d(frames, value)) {
		return;
	}
	emit_changed();
}

void LaserSpriteLook2D::set_animation(const StringName &value) {
	if (!assign_if_changed2d(animation, value)) {
		return;
	}
	emit_changed();
}

void LaserSpriteLook2D::set_speed_scale(double value) {
	if (!is_finite_real2d(value) || value < 0.0) {
		UtilityFunctions::push_error("LaserSpriteLook2D: speed_scale must be finite and >= 0 (0 = holds the first frame), keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(speed_scale, value)) {
		return;
	}
	emit_changed();
}

void LaserSpriteLook2D::set_scale(const Vector2 &value) {
	if (!is_finite_real2d(value.x) || !is_finite_real2d(value.y)) {
		UtilityFunctions::push_error("LaserSpriteLook2D: scale must be finite, keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(scale, value)) {
		return;
	}
	emit_changed();
}

void LaserSpriteLook2D::set_offset(const Vector2 &value) {
	if (!is_finite_real2d(value.x) || !is_finite_real2d(value.y)) {
		UtilityFunctions::push_error("LaserSpriteLook2D: offset must be finite, keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(offset, value)) {
		return;
	}
	emit_changed();
}

void LaserSpriteLook2D::set_rotation_degrees(double value) {
	if (!is_finite_real2d(value)) {
		UtilityFunctions::push_error("LaserSpriteLook2D: rotation_degrees must be finite, keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(rotation_degrees, value)) {
		return;
	}
	emit_changed();
}

void LaserSpriteLook2D::set_align_to_beam(bool value) {
	if (!assign_if_changed2d(align_to_beam, value)) {
		return;
	}
	emit_changed();
}

void LaserSpriteLook2D::set_modulate(const Color &value) {
	if (!color_ok2d(value)) {
		UtilityFunctions::push_error("LaserSpriteLook2D: modulate must be finite, keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(modulate, value)) {
		return;
	}
	emit_changed();
}

void LaserSpriteLook2D::set_z_index(int value) {
	if (value < -4096 || value > 4096) {
		UtilityFunctions::push_error("LaserSpriteLook2D: z_index must be between -4096 and 4096, keeping the old value.");
		return;
	}
	if (!assign_if_changed2d(z_index, value)) {
		return;
	}
	emit_changed();
}

void LaserSpriteLook2D::set_z_as_relative(bool value) {
	if (!assign_if_changed2d(z_as_relative, value)) {
		return;
	}
	emit_changed();
}

void LaserSpriteLook2D::set_material(const Ref<Material> &value) {
	if (!assign_if_changed2d(material, value)) {
		return;
	}
	emit_changed();
}

void LaserSpriteLook2D::set_visible(bool value) {
	if (!assign_if_changed2d(visible, value)) {
		return;
	}
	emit_changed();
}

Transform2D LaserSpriteLook2D::pose_at(const Vector2 &point, real_t beam_angle) const {
	const real_t turn = align_to_beam ? beam_angle : (real_t)0.0;
	const Vector2 at = point + offset.rotated(turn);
	// Rotation, then scale, then translation: the frame turns about its center.
	return Transform2D(turn + (real_t)Math::deg_to_rad(rotation_degrees), scale, (real_t)0.0, at);
}

#define LOOK_BIND(NAME, VARIANT, HINT, HINT_STRING)                                        \
	ClassDB::bind_method(D_METHOD("get_" #NAME), &LaserSpriteLook2D::get_##NAME);          \
	ClassDB::bind_method(D_METHOD("set_" #NAME, "value"), &LaserSpriteLook2D::set_##NAME); \
	ADD_PROPERTY(PropertyInfo(Variant::VARIANT, #NAME, HINT, HINT_STRING), "set_" #NAME, "get_" #NAME);

void LaserSpriteLook2D::_bind_methods() {
	ADD_GROUP("Animation", "");
	LOOK_BIND(frames, OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "SpriteFrames")
	LOOK_BIND(animation, STRING_NAME, PROPERTY_HINT_NONE, "")
	LOOK_BIND(speed_scale, FLOAT, PROPERTY_HINT_RANGE, "0,8,0.01,or_greater")
	ADD_GROUP("Placement", "");
	LOOK_BIND(scale, VECTOR2, PROPERTY_HINT_LINK, "")
	LOOK_BIND(offset, VECTOR2, PROPERTY_HINT_NONE, "suffix:px")
	LOOK_BIND(rotation_degrees, FLOAT, PROPERTY_HINT_RANGE, "-360,360,0.1,or_less,or_greater,suffix:deg")
	LOOK_BIND(align_to_beam, BOOL, PROPERTY_HINT_NONE, "")
	ADD_GROUP("Rendering", "");
	LOOK_BIND(modulate, COLOR, PROPERTY_HINT_NONE, "")
	LOOK_BIND(z_index, INT, PROPERTY_HINT_RANGE, "-4096,4096,1")
	LOOK_BIND(z_as_relative, BOOL, PROPERTY_HINT_NONE, "")
	LOOK_BIND(material, OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "CanvasItemMaterial,ShaderMaterial")
	LOOK_BIND(visible, BOOL, PROPERTY_HINT_NONE, "")
}

#undef LOOK_BIND

} // namespace BlastBullets2D
