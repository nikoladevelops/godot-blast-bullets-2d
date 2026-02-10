#include "./bullet_speed_data2d.hpp"

#include <godot_cpp/classes/random_number_generator.hpp>

using namespace godot;
namespace BlastBullets2D {

TypedArray<BulletSpeedData2D> BulletSpeedData2D::generate_random_data(
		int amount_to_generate,
		real_t speed_MIN,
		real_t speed_MAX,
		real_t max_speed_MIN,
		real_t max_speed_MAX,
		real_t acceleration_MIN,
		real_t acceleration_MAX) {
	Ref<RandomNumberGenerator> rand_gen = memnew(RandomNumberGenerator);

	TypedArray<BulletSpeedData2D> data;
	data.resize(amount_to_generate);

	for (int i = 0; i < amount_to_generate; ++i) {
		Ref<BulletSpeedData2D> bullet_data = memnew(BulletSpeedData2D);
		bullet_data->speed = rand_gen->randf_range(speed_MIN, speed_MAX);
		bullet_data->max_speed = rand_gen->randf_range(max_speed_MIN, max_speed_MAX);
		bullet_data->acceleration = rand_gen->randf_range(acceleration_MIN, acceleration_MAX);

		data[i] = bullet_data;
	}

	return data;
}

real_t BulletSpeedData2D::get_speed() {
	return speed;
}
void BulletSpeedData2D::set_speed(real_t new_speed) {
	speed = new_speed;
}

real_t BulletSpeedData2D::get_max_speed() {
	return max_speed;
}
void BulletSpeedData2D::set_max_speed(real_t new_max_speed) {
	max_speed = new_max_speed;
}

real_t BulletSpeedData2D::get_acceleration() {
	return acceleration;
}
void BulletSpeedData2D::set_acceleration(real_t new_acceleration) {
	acceleration = new_acceleration;
}

void BulletSpeedData2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_speed", "new_speed"), &BulletSpeedData2D::set_speed);
	ClassDB::bind_method(D_METHOD("get_speed"), &BulletSpeedData2D::get_speed);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed"), "set_speed", "get_speed");

	ClassDB::bind_method(D_METHOD("set_max_speed", "new_max_speed"), &BulletSpeedData2D::set_max_speed);
	ClassDB::bind_method(D_METHOD("get_max_speed"), &BulletSpeedData2D::get_max_speed);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_speed"), "set_max_speed", "get_max_speed");

	ClassDB::bind_method(D_METHOD("set_acceleration", "new_acceleration"), &BulletSpeedData2D::set_acceleration);
	ClassDB::bind_method(D_METHOD("get_acceleration"), &BulletSpeedData2D::get_acceleration);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "acceleration"), "set_acceleration", "get_acceleration");

	ClassDB::bind_static_method(
			"BulletSpeedData2D",
			D_METHOD(
					"generate_random_data",
					"amount_to_generate",
					"speed_MIN",
					"speed_MAX",
					"max_speed_MIN",
					"max_speed_MAX",
					"acceleration_MIN",
					"acceleration_MAX"),
			&BulletSpeedData2D::generate_random_data);
}
} //namespace BlastBullets2D
