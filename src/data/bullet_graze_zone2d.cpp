#include "data/bullet_graze_zone2d.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace BlastBullets2D {

bool BulletGrazeZone2D::get_enabled() const { return enabled; }
void BulletGrazeZone2D::set_enabled(bool value) {
	if (enabled == value) {
		return;
	}
	enabled = value;
	emit_changed();
}

StringName BulletGrazeZone2D::get_target_group() const { return target_group; }
void BulletGrazeZone2D::set_target_group(const StringName &value) {
	if (target_group == value) {
		return;
	}
	target_group = value;
	emit_changed();
}

int BulletGrazeZone2D::get_ring_count() const { return ring_count; }
void BulletGrazeZone2D::set_ring_count(int value) {
	if (value < 1 || value > MAX_RINGS) {
		UtilityFunctions::push_error("BulletGrazeZone2D: ring_count must be between 1 and 4, keeping the old value.");
		return;
	}
	if (ring_count == value) {
		return;
	}
	ring_count = value;
	notify_property_list_changed();
	emit_changed();
}

void BulletGrazeZone2D::store_ring_radius(int index, real_t value, const char *property_name) {
	if (!Math::is_finite(value) || value <= 0.0) {
		UtilityFunctions::push_error(String("BulletGrazeZone2D: ") + property_name + " must be finite and > 0, keeping the old value.");
		return;
	}
	if (ring_radii[index] == value) {
		return;
	}
	ring_radii[index] = value;
	emit_changed();
}

real_t BulletGrazeZone2D::get_ring_radius(int index) const {
	if (index < 0 || index >= MAX_RINGS) {
		UtilityFunctions::push_error("BulletGrazeZone2D.get_ring_radius: index " + String::num_int64(index) + " is out of range 0..3.");
		return 0.0;
	}
	return ring_radii[index];
}

void BulletGrazeZone2D::set_ring_radius(int index, real_t value) {
	if (index < 0 || index >= MAX_RINGS) {
		UtilityFunctions::push_error("BulletGrazeZone2D.set_ring_radius: index " + String::num_int64(index) + " is out of range 0..3, nothing changed.");
		return;
	}
	static const char *const names[MAX_RINGS] = { "ring_1_radius", "ring_2_radius", "ring_3_radius", "ring_4_radius" };
	store_ring_radius(index, value, names[index]);
}

PackedFloat32Array BulletGrazeZone2D::get_active_ring_radii() const {
	PackedFloat32Array out;
	out.resize(ring_count);
	for (int i = 0; i < ring_count; ++i) {
		out.set(i, ring_radii[i]);
	}
	return out;
}

bool BulletGrazeZone2D::get_count_bullet_size() const { return count_bullet_size; }
void BulletGrazeZone2D::set_count_bullet_size(bool value) {
	if (count_bullet_size == value) {
		return;
	}
	count_bullet_size = value;
	emit_changed();
}

BulletGrazeZone2D::Regraze BulletGrazeZone2D::get_regraze() const { return regraze; }
void BulletGrazeZone2D::set_regraze(Regraze value) {
	if (value < REGRAZE_ONCE || value > REGRAZE_AFTER_EXIT) {
		UtilityFunctions::push_error("BulletGrazeZone2D: regraze must be 0 (Once) or 1 (After Exit), keeping the old value.");
		return;
	}
	if (regraze == value) {
		return;
	}
	regraze = value;
	emit_changed();
}

Color BulletGrazeZone2D::get_preview_color() const { return preview_color; }
void BulletGrazeZone2D::set_preview_color(const Color &value) {
	if (!Math::is_finite(value.r) || !Math::is_finite(value.g) || !Math::is_finite(value.b) || !Math::is_finite(value.a)) {
		UtilityFunctions::push_error("BulletGrazeZone2D: preview_color must be finite, keeping the old value.");
		return;
	}
	if (preview_color == value) {
		return;
	}
	preview_color = value;
	emit_changed();
}

bool BulletGrazeZone2D::get_preview_during_runtime() const { return preview_during_runtime; }
void BulletGrazeZone2D::set_preview_during_runtime(bool value) {
	if (preview_during_runtime == value) {
		return;
	}
	preview_during_runtime = value;
	emit_changed();
}

void BulletGrazeZone2D::_validate_property(PropertyInfo &p_property) const {
	const String name = p_property.name;
	if (!name.begins_with("ring_") || !name.ends_with("_radius")) {
		return;
	}
	const int number = name.substr(5, name.length() - 12).to_int();
	if (number > ring_count) {
		p_property.usage &= ~PROPERTY_USAGE_EDITOR;
	}
}

void BulletGrazeZone2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_enabled"), &BulletGrazeZone2D::get_enabled);
	ClassDB::bind_method(D_METHOD("set_enabled", "value"), &BulletGrazeZone2D::set_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "get_enabled");

	ClassDB::bind_method(D_METHOD("get_target_group"), &BulletGrazeZone2D::get_target_group);
	ClassDB::bind_method(D_METHOD("set_target_group", "value"), &BulletGrazeZone2D::set_target_group);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "target_group"), "set_target_group", "get_target_group");

	ClassDB::bind_method(D_METHOD("get_ring_count"), &BulletGrazeZone2D::get_ring_count);
	ClassDB::bind_method(D_METHOD("set_ring_count", "value"), &BulletGrazeZone2D::set_ring_count);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ring_count", PROPERTY_HINT_RANGE, "1,4,1"), "set_ring_count", "get_ring_count");

#define GRAZE_BIND_RING(N)                                                                                        \
	ClassDB::bind_method(D_METHOD("get_ring_" #N "_radius"), &BulletGrazeZone2D::get_ring_##N##_radius);          \
	ClassDB::bind_method(D_METHOD("set_ring_" #N "_radius", "value"), &BulletGrazeZone2D::set_ring_##N##_radius); \
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ring_" #N "_radius", PROPERTY_HINT_RANGE, "0.5,512,0.5,or_greater,suffix:px"), "set_ring_" #N "_radius", "get_ring_" #N "_radius");
	GRAZE_BIND_RING(1)
	GRAZE_BIND_RING(2)
	GRAZE_BIND_RING(3)
	GRAZE_BIND_RING(4)
#undef GRAZE_BIND_RING

	ClassDB::bind_method(D_METHOD("get_count_bullet_size"), &BulletGrazeZone2D::get_count_bullet_size);
	ClassDB::bind_method(D_METHOD("set_count_bullet_size", "value"), &BulletGrazeZone2D::set_count_bullet_size);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "count_bullet_size"), "set_count_bullet_size", "get_count_bullet_size");

	ClassDB::bind_method(D_METHOD("get_regraze"), &BulletGrazeZone2D::get_regraze);
	ClassDB::bind_method(D_METHOD("set_regraze", "value"), &BulletGrazeZone2D::set_regraze);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "regraze", PROPERTY_HINT_ENUM, "Once:0,After Exit:1"), "set_regraze", "get_regraze");

	ClassDB::bind_method(D_METHOD("get_preview_color"), &BulletGrazeZone2D::get_preview_color);
	ClassDB::bind_method(D_METHOD("set_preview_color", "value"), &BulletGrazeZone2D::set_preview_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "preview_color"), "set_preview_color", "get_preview_color");

	ClassDB::bind_method(D_METHOD("get_preview_during_runtime"), &BulletGrazeZone2D::get_preview_during_runtime);
	ClassDB::bind_method(D_METHOD("set_preview_during_runtime", "value"), &BulletGrazeZone2D::set_preview_during_runtime);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "preview_during_runtime"), "set_preview_during_runtime", "get_preview_during_runtime");

	ClassDB::bind_method(D_METHOD("get_ring_radius", "index"), &BulletGrazeZone2D::get_ring_radius);
	ClassDB::bind_method(D_METHOD("set_ring_radius", "index", "value"), &BulletGrazeZone2D::set_ring_radius);
	ClassDB::bind_method(D_METHOD("get_active_ring_radii"), &BulletGrazeZone2D::get_active_ring_radii);

	BIND_CONSTANT(MAX_RINGS);
	BIND_CONSTANT(MAX_TARGETS);
	BIND_ENUM_CONSTANT(REGRAZE_ONCE);
	BIND_ENUM_CONSTANT(REGRAZE_AFTER_EXIT);
}
} //namespace BlastBullets2D
