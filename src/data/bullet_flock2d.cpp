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
	emit_changed();
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

real_t BulletFlock2D::get_max_turn_rate_degrees() const { return max_turn_rate_degrees; }
void BulletFlock2D::set_max_turn_rate_degrees(real_t value) { store_amount(max_turn_rate_degrees, value, "max_turn_rate_degrees", false); }

bool BulletFlock2D::get_face_heading() const { return face_heading; }
void BulletFlock2D::set_face_heading(bool value) {
	if (face_heading == value) {
		return;
	}
	face_heading = value;
	emit_changed();
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
	emit_changed();
}

void BulletFlock2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_separation"), &BulletFlock2D::get_separation);
	ClassDB::bind_method(D_METHOD("set_separation", "value"), &BulletFlock2D::set_separation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "separation", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_separation", "get_separation");

	ClassDB::bind_method(D_METHOD("get_alignment"), &BulletFlock2D::get_alignment);
	ClassDB::bind_method(D_METHOD("set_alignment", "value"), &BulletFlock2D::set_alignment);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "alignment", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_alignment", "get_alignment");

	ClassDB::bind_method(D_METHOD("get_cohesion"), &BulletFlock2D::get_cohesion);
	ClassDB::bind_method(D_METHOD("set_cohesion", "value"), &BulletFlock2D::set_cohesion);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cohesion", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_cohesion", "get_cohesion");

	ClassDB::bind_method(D_METHOD("get_neighbor_range"), &BulletFlock2D::get_neighbor_range);
	ClassDB::bind_method(D_METHOD("set_neighbor_range", "value"), &BulletFlock2D::set_neighbor_range);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "neighbor_range", PROPERTY_HINT_RANGE, "1,1000,0.5,or_greater,suffix:px"), "set_neighbor_range", "get_neighbor_range");

	ClassDB::bind_method(D_METHOD("get_separation_range"), &BulletFlock2D::get_separation_range);
	ClassDB::bind_method(D_METHOD("set_separation_range", "value"), &BulletFlock2D::set_separation_range);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "separation_range", PROPERTY_HINT_RANGE, "0,500,0.5,or_greater,suffix:px"), "set_separation_range", "get_separation_range");

	ClassDB::bind_method(D_METHOD("get_max_turn_rate_degrees"), &BulletFlock2D::get_max_turn_rate_degrees);
	ClassDB::bind_method(D_METHOD("set_max_turn_rate_degrees", "value"), &BulletFlock2D::set_max_turn_rate_degrees);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_turn_rate_degrees", PROPERTY_HINT_RANGE, "0,1440,1,or_greater,suffix:deg/s"), "set_max_turn_rate_degrees", "get_max_turn_rate_degrees");

	ClassDB::bind_method(D_METHOD("get_face_heading"), &BulletFlock2D::get_face_heading);
	ClassDB::bind_method(D_METHOD("set_face_heading", "value"), &BulletFlock2D::set_face_heading);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "face_heading"), "set_face_heading", "get_face_heading");

	ClassDB::bind_method(D_METHOD("get_max_neighbors"), &BulletFlock2D::get_max_neighbors);
	ClassDB::bind_method(D_METHOD("set_max_neighbors", "value"), &BulletFlock2D::set_max_neighbors);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_neighbors", PROPERTY_HINT_RANGE, "0,256,1,or_greater"), "set_max_neighbors", "get_max_neighbors");

	BIND_ENUM_CONSTANT(SCOPE_PER_SPAWNER);
	BIND_ENUM_CONSTANT(SCOPE_SHARED);
	BIND_ENUM_CONSTANT(SCOPE_PER_VOLLEY);
}
} //namespace BlastBullets2D
