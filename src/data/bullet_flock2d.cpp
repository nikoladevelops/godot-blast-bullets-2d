#include "data/bullet_flock2d.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace BlastBullets2D {

void BulletFlock2D::store_amount(real_t &field, real_t value, const char *property_name, bool positive_only) {
	if (!Math::is_finite(value) || value < 0.0 || (positive_only && value == 0.0)) {
		UtilityFunctions::push_error(String("BulletFlock2D: ") + property_name + (positive_only ? " must be finite and > 0" : " must be finite and >= 0") + ", keeping the old value.");
		return;
	}
	if (field == value) {
		return;
	}
	field = value;
	rules_changed();
}

real_t BulletFlock2D::get_separation() const { return separation; }
void BulletFlock2D::set_separation(real_t value) { store_amount(separation, value, "separation", false); }

real_t BulletFlock2D::get_alignment() const { return alignment; }
void BulletFlock2D::set_alignment(real_t value) { store_amount(alignment, value, "alignment", false); }

real_t BulletFlock2D::get_cohesion() const { return cohesion; }
void BulletFlock2D::set_cohesion(real_t value) { store_amount(cohesion, value, "cohesion", false); }

real_t BulletFlock2D::get_neighbor_range() const { return neighbor_range; }
void BulletFlock2D::set_neighbor_range(real_t value) { store_amount(neighbor_range, value, "neighbor_range", true); }

real_t BulletFlock2D::get_separation_range() const { return separation_range; }
void BulletFlock2D::set_separation_range(real_t value) { store_amount(separation_range, value, "separation_range", false); }

real_t BulletFlock2D::get_separation_lookahead_sec() const { return separation_lookahead_sec; }
void BulletFlock2D::set_separation_lookahead_sec(real_t value) { store_amount(separation_lookahead_sec, value, "separation_lookahead_sec", false); }

bool BulletFlock2D::get_count_bullet_size() const { return count_bullet_size; }
void BulletFlock2D::set_count_bullet_size(bool value) {
	if (count_bullet_size == value) {
		return;
	}
	count_bullet_size = value;
	rules_changed();
}

real_t BulletFlock2D::get_max_turn_rate_degrees() const { return max_turn_rate_degrees; }
void BulletFlock2D::set_max_turn_rate_degrees(real_t value) { store_amount(max_turn_rate_degrees, value, "max_turn_rate_degrees", false); }

real_t BulletFlock2D::get_blend_in_sec() const { return blend_in_sec; }
void BulletFlock2D::set_blend_in_sec(real_t value) { store_amount(blend_in_sec, value, "blend_in_sec", false); }

bool BulletFlock2D::get_face_heading() const { return face_heading; }
void BulletFlock2D::set_face_heading(bool value) {
	if (face_heading == value) {
		return;
	}
	face_heading = value;
	rules_changed();
}

int BulletFlock2D::get_max_neighbors() const { return max_neighbors; }
void BulletFlock2D::set_max_neighbors(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("BulletFlock2D: max_neighbors must be >= 0 (0 = no limit), keeping the old value.");
		return;
	}
	if (max_neighbors == value) {
		return;
	}
	max_neighbors = value;
	rules_changed();
}

real_t BulletFlock2D::get_update_interval() const { return update_interval; }
void BulletFlock2D::set_update_interval(real_t value) { store_amount(update_interval, value, "update_interval", false); }

real_t BulletFlock2D::get_view_angle_degrees() const { return view_angle_degrees; }
void BulletFlock2D::set_view_angle_degrees(real_t value) {
	if (!Math::is_finite(value) || value < 0.0 || value > 360.0) {
		UtilityFunctions::push_error("BulletFlock2D: view_angle_degrees must be between 0 and 360, keeping the old value.");
		return;
	}
	if (view_angle_degrees == value) {
		return;
	}
	view_angle_degrees = value;
	rules_changed();
}

namespace {

// The steering knobs a preset writes, in one row per preset (index = id).
struct FlockPresetRow2D {
	real_t separation, alignment, cohesion, neighbor_range, separation_range, separation_lookahead_sec, max_turn_rate_degrees, blend_in_sec, view_angle_degrees;
};
constexpr FlockPresetRow2D FLOCK_PRESETS[] = {
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0 }, // Custom: never written
	{ 10.0, 1.0, 1.5, 150.0, 24.0, 0.2, 270.0, 0.3, 360.0 }, // Swarm (the defaults)
	{ 6.0, 3.0, 2.0, 150.0, 16.0, 0.2, 180.0, 0.3, 360.0 }, // School
	{ 10.0, 0.0, 0.0, 150.0, 32.0, 0.2, 270.0, 0.2, 360.0 }, // Spread
	{ 6.0, 0.5, 5.0, 200.0, 8.0, 0.2, 360.0, 0.3, 360.0 }, // Cluster
};
constexpr int FLOCK_PRESET_COUNT = sizeof(FLOCK_PRESETS) / sizeof(FLOCK_PRESETS[0]);

bool flock_preset_valid(int preset) {
	return preset >= BulletFlock2D::PRESET_CUSTOM && preset < FLOCK_PRESET_COUNT;
}

const char *FLOCK_PRESET_IDS = "0 (Custom), 1 (Swarm), 2 (School), 3 (Spread) or 4 (Cluster)";

} // namespace

void BulletFlock2D::apply_preset(Preset preset) {
	if (!flock_preset_valid(preset)) {
		UtilityFunctions::push_error(String("BulletFlock2D: preset must be ") + FLOCK_PRESET_IDS + ", keeping the old values.");
		return;
	}
	if (preset == PRESET_CUSTOM) {
		return;
	}
	const FlockPresetRow2D &row = FLOCK_PRESETS[preset];
	if (get_matching_preset() == preset) {
		return;
	}
	separation = row.separation;
	alignment = row.alignment;
	cohesion = row.cohesion;
	neighbor_range = row.neighbor_range;
	separation_range = row.separation_range;
	separation_lookahead_sec = row.separation_lookahead_sec;
	max_turn_rate_degrees = row.max_turn_rate_degrees;
	blend_in_sec = row.blend_in_sec;
	view_angle_degrees = row.view_angle_degrees;
	rules_changed();
	notify_property_list_changed();
}

BulletFlock2D::Preset BulletFlock2D::get_matching_preset() const {
	for (int id = PRESET_SWARM; id < FLOCK_PRESET_COUNT; ++id) {
		const FlockPresetRow2D &row = FLOCK_PRESETS[id];
		if (separation == row.separation && alignment == row.alignment && cohesion == row.cohesion && neighbor_range == row.neighbor_range && separation_range == row.separation_range && separation_lookahead_sec == row.separation_lookahead_sec && max_turn_rate_degrees == row.max_turn_rate_degrees && blend_in_sec == row.blend_in_sec && view_angle_degrees == row.view_angle_degrees) {
			return (Preset)id;
		}
	}
	return PRESET_CUSTOM;
}

Ref<BulletFlock2D> BulletFlock2D::create_preset(Preset preset) {
	if (!flock_preset_valid(preset)) {
		UtilityFunctions::push_error(String("BulletFlock2D.create_preset: preset must be ") + FLOCK_PRESET_IDS + ", nothing created.");
		return Ref<BulletFlock2D>();
	}
	Ref<BulletFlock2D> flock;
	flock.instantiate();
	flock->apply_preset(preset);
	return flock;
}

void BulletFlock2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("apply_preset", "preset"), &BulletFlock2D::apply_preset);
	ClassDB::bind_method(D_METHOD("get_matching_preset"), &BulletFlock2D::get_matching_preset);
	ClassDB::bind_static_method("BulletFlock2D", D_METHOD("create_preset", "preset"), &BulletFlock2D::create_preset);
	// Editor only (never saved): picking a preset writes the knobs below.
	ADD_PROPERTY(PropertyInfo(Variant::INT, "preset", PROPERTY_HINT_ENUM, "Custom,Swarm,School,Spread,Cluster", PROPERTY_USAGE_EDITOR), "apply_preset", "get_matching_preset");

	ClassDB::bind_method(D_METHOD("get_separation"), &BulletFlock2D::get_separation);
	ClassDB::bind_method(D_METHOD("set_separation", "value"), &BulletFlock2D::set_separation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "separation", PROPERTY_HINT_RANGE, "0,20,0.01,or_greater"), "set_separation", "get_separation");

	ClassDB::bind_method(D_METHOD("get_alignment"), &BulletFlock2D::get_alignment);
	ClassDB::bind_method(D_METHOD("set_alignment", "value"), &BulletFlock2D::set_alignment);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "alignment", PROPERTY_HINT_RANGE, "0,20,0.01,or_greater"), "set_alignment", "get_alignment");

	ClassDB::bind_method(D_METHOD("get_cohesion"), &BulletFlock2D::get_cohesion);
	ClassDB::bind_method(D_METHOD("set_cohesion", "value"), &BulletFlock2D::set_cohesion);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cohesion", PROPERTY_HINT_RANGE, "0,20,0.01,or_greater"), "set_cohesion", "get_cohesion");

	ClassDB::bind_method(D_METHOD("get_neighbor_range"), &BulletFlock2D::get_neighbor_range);
	ClassDB::bind_method(D_METHOD("set_neighbor_range", "value"), &BulletFlock2D::set_neighbor_range);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "neighbor_range", PROPERTY_HINT_RANGE, "1,1000,0.5,or_greater,suffix:px"), "set_neighbor_range", "get_neighbor_range");

	ClassDB::bind_method(D_METHOD("get_separation_range"), &BulletFlock2D::get_separation_range);
	ClassDB::bind_method(D_METHOD("set_separation_range", "value"), &BulletFlock2D::set_separation_range);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "separation_range", PROPERTY_HINT_RANGE, "0,500,0.5,or_greater,suffix:px"), "set_separation_range", "get_separation_range");

	ClassDB::bind_method(D_METHOD("get_separation_lookahead_sec"), &BulletFlock2D::get_separation_lookahead_sec);
	ClassDB::bind_method(D_METHOD("set_separation_lookahead_sec", "value"), &BulletFlock2D::set_separation_lookahead_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "separation_lookahead_sec", PROPERTY_HINT_RANGE, "0,1,0.01,or_greater,suffix:s"), "set_separation_lookahead_sec", "get_separation_lookahead_sec");

	ClassDB::bind_method(D_METHOD("get_count_bullet_size"), &BulletFlock2D::get_count_bullet_size);
	ClassDB::bind_method(D_METHOD("set_count_bullet_size", "value"), &BulletFlock2D::set_count_bullet_size);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "count_bullet_size"), "set_count_bullet_size", "get_count_bullet_size");

	ClassDB::bind_method(D_METHOD("get_max_turn_rate_degrees"), &BulletFlock2D::get_max_turn_rate_degrees);
	ClassDB::bind_method(D_METHOD("set_max_turn_rate_degrees", "value"), &BulletFlock2D::set_max_turn_rate_degrees);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_turn_rate_degrees", PROPERTY_HINT_RANGE, "0,1440,1,or_greater,suffix:deg/s"), "set_max_turn_rate_degrees", "get_max_turn_rate_degrees");

	ClassDB::bind_method(D_METHOD("get_blend_in_sec"), &BulletFlock2D::get_blend_in_sec);
	ClassDB::bind_method(D_METHOD("set_blend_in_sec", "value"), &BulletFlock2D::set_blend_in_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blend_in_sec", PROPERTY_HINT_RANGE, "0,2,0.01,or_greater,suffix:s"), "set_blend_in_sec", "get_blend_in_sec");

	ClassDB::bind_method(D_METHOD("get_face_heading"), &BulletFlock2D::get_face_heading);
	ClassDB::bind_method(D_METHOD("set_face_heading", "value"), &BulletFlock2D::set_face_heading);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "face_heading"), "set_face_heading", "get_face_heading");

	ClassDB::bind_method(D_METHOD("get_max_neighbors"), &BulletFlock2D::get_max_neighbors);
	ClassDB::bind_method(D_METHOD("set_max_neighbors", "value"), &BulletFlock2D::set_max_neighbors);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_neighbors", PROPERTY_HINT_RANGE, "0,256,1,or_greater"), "set_max_neighbors", "get_max_neighbors");

	ClassDB::bind_method(D_METHOD("get_update_interval"), &BulletFlock2D::get_update_interval);
	ClassDB::bind_method(D_METHOD("set_update_interval", "value"), &BulletFlock2D::set_update_interval);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "update_interval", PROPERTY_HINT_RANGE, "0,1,0.01,or_greater,suffix:s"), "set_update_interval", "get_update_interval");

	ClassDB::bind_method(D_METHOD("get_view_angle_degrees"), &BulletFlock2D::get_view_angle_degrees);
	ClassDB::bind_method(D_METHOD("set_view_angle_degrees", "value"), &BulletFlock2D::set_view_angle_degrees);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "view_angle_degrees", PROPERTY_HINT_RANGE, "0,360,1,suffix:deg"), "set_view_angle_degrees", "get_view_angle_degrees");

	BIND_ENUM_CONSTANT(SCOPE_PER_SPAWNER);
	BIND_ENUM_CONSTANT(SCOPE_SHARED);
	BIND_ENUM_CONSTANT(SCOPE_PER_VOLLEY);

	BIND_ENUM_CONSTANT(PRESET_CUSTOM);
	BIND_ENUM_CONSTANT(PRESET_SWARM);
	BIND_ENUM_CONSTANT(PRESET_SCHOOL);
	BIND_ENUM_CONSTANT(PRESET_SPREAD);
	BIND_ENUM_CONSTANT(PRESET_CLUSTER);
}
} //namespace BlastBullets2D
