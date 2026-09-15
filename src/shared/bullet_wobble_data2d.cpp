#include "./bullet_wobble_data2d.hpp"

#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace BlastBullets2D {

TypedArray<BulletWobbleData2D> BulletWobbleData2D::generate_random_data(
		int amount_to_generate,
		real_t amplitude_MIN,
		real_t amplitude_MAX,
		real_t frequency_MIN,
		real_t frequency_MAX) {
	TypedArray<BulletWobbleData2D> data;
	if (amount_to_generate <= 0) {
		UtilityFunctions::push_error("BulletWobbleData2D.generate_random_data: amount_to_generate must be > 0.");
		return data;
	}
	if (!(amplitude_MIN <= amplitude_MAX) || !(frequency_MIN <= frequency_MAX)) {
		UtilityFunctions::push_error("BulletWobbleData2D.generate_random_data: every MIN must be <= its MAX.");
		return data;
	}
	if (!Math::is_finite(amplitude_MIN) || !Math::is_finite(amplitude_MAX) || !Math::is_finite(frequency_MIN) || !Math::is_finite(frequency_MAX)) {
		UtilityFunctions::push_error("BulletWobbleData2D.generate_random_data: MIN/MAX bounds must be finite.");
		return data;
	}
	if (amplitude_MIN < 0.0 || frequency_MIN < 0.0) {
		UtilityFunctions::push_error("BulletWobbleData2D.generate_random_data: amplitude and frequency bounds must be >= 0.");
		return data;
	}
	Ref<RandomNumberGenerator> rand_gen = memnew(RandomNumberGenerator);
	rand_gen->randomize();
	data.resize(amount_to_generate);
	for (int i = 0; i < amount_to_generate; ++i) {
		Ref<BulletWobbleData2D> wobble = memnew(BulletWobbleData2D);
		wobble->enabled = true;
		wobble->amplitude = rand_gen->randf_range(amplitude_MIN, amplitude_MAX);
		wobble->frequency_hz = rand_gen->randf_range(frequency_MIN, frequency_MAX);
		data[i] = wobble;
	}
	return data;
}

bool BulletWobbleData2D::get_enabled() const { return enabled; }
void BulletWobbleData2D::set_enabled(bool value) { enabled = value; }

BulletWobbleData2D::WobbleMode BulletWobbleData2D::get_mode() const { return mode; }
void BulletWobbleData2D::set_mode(WobbleMode value) {
	if (value < WOBBLE_LATERAL || value > WOBBLE_ANGULAR) {
		UtilityFunctions::push_error("BulletWobbleData2D.mode is out of range, keeping the old value.");
		return;
	}
	mode = value;
}

real_t BulletWobbleData2D::get_amplitude() const { return amplitude; }
void BulletWobbleData2D::set_amplitude(real_t value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletWobbleData2D.amplitude must be finite and >= 0.");
		return;
	}
	amplitude = value;
}

real_t BulletWobbleData2D::get_frequency_hz() const { return frequency_hz; }
void BulletWobbleData2D::set_frequency_hz(real_t value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletWobbleData2D.frequency_hz must be finite and >= 0.");
		return;
	}
	frequency_hz = value;
}

real_t BulletWobbleData2D::get_phase_rad() const { return phase_rad; }
void BulletWobbleData2D::set_phase_rad(real_t value) {
	if (!Math::is_finite(value)) {
		UtilityFunctions::push_error("BulletWobbleData2D.phase_rad must be finite.");
		return;
	}
	phase_rad = value;
}

real_t BulletWobbleData2D::get_phase_step_per_bullet() const { return phase_step_per_bullet; }
void BulletWobbleData2D::set_phase_step_per_bullet(real_t value) {
	if (!Math::is_finite(value)) {
		UtilityFunctions::push_error("BulletWobbleData2D.phase_step_per_bullet must be finite.");
		return;
	}
	phase_step_per_bullet = value;
}

bool BulletWobbleData2D::get_distance_phased() const { return distance_phased; }
void BulletWobbleData2D::set_distance_phased(bool value) { distance_phased = value; }

real_t BulletWobbleData2D::get_damping_per_sec() const { return damping_per_sec; }
void BulletWobbleData2D::set_damping_per_sec(real_t value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletWobbleData2D.damping_per_sec must be finite and >= 0.");
		return;
	}
	damping_per_sec = value;
}

real_t BulletWobbleData2D::get_delay_sec() const { return delay_sec; }
void BulletWobbleData2D::set_delay_sec(real_t value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletWobbleData2D.delay_sec must be finite and >= 0.");
		return;
	}
	delay_sec = value;
}

real_t BulletWobbleData2D::get_duration_sec() const { return duration_sec; }
void BulletWobbleData2D::set_duration_sec(real_t value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletWobbleData2D.duration_sec must be finite and >= 0 (0 = infinite).");
		return;
	}
	duration_sec = value;
}

void BulletWobbleData2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_enabled"), &BulletWobbleData2D::get_enabled);
	ClassDB::bind_method(D_METHOD("set_enabled", "value"), &BulletWobbleData2D::set_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "get_enabled");

	ClassDB::bind_method(D_METHOD("get_mode"), &BulletWobbleData2D::get_mode);
	ClassDB::bind_method(D_METHOD("set_mode", "value"), &BulletWobbleData2D::set_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "Lateral,Angular"), "set_mode", "get_mode");

	ClassDB::bind_method(D_METHOD("get_amplitude"), &BulletWobbleData2D::get_amplitude);
	ClassDB::bind_method(D_METHOD("set_amplitude", "value"), &BulletWobbleData2D::set_amplitude);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "amplitude"), "set_amplitude", "get_amplitude");

	ClassDB::bind_method(D_METHOD("get_frequency_hz"), &BulletWobbleData2D::get_frequency_hz);
	ClassDB::bind_method(D_METHOD("set_frequency_hz", "value"), &BulletWobbleData2D::set_frequency_hz);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "frequency_hz"), "set_frequency_hz", "get_frequency_hz");

	ClassDB::bind_method(D_METHOD("get_phase_rad"), &BulletWobbleData2D::get_phase_rad);
	ClassDB::bind_method(D_METHOD("set_phase_rad", "value"), &BulletWobbleData2D::set_phase_rad);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "phase_rad"), "set_phase_rad", "get_phase_rad");

	ClassDB::bind_method(D_METHOD("get_phase_step_per_bullet"), &BulletWobbleData2D::get_phase_step_per_bullet);
	ClassDB::bind_method(D_METHOD("set_phase_step_per_bullet", "value"), &BulletWobbleData2D::set_phase_step_per_bullet);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "phase_step_per_bullet"), "set_phase_step_per_bullet", "get_phase_step_per_bullet");

	ClassDB::bind_method(D_METHOD("get_distance_phased"), &BulletWobbleData2D::get_distance_phased);
	ClassDB::bind_method(D_METHOD("set_distance_phased", "value"), &BulletWobbleData2D::set_distance_phased);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "distance_phased"), "set_distance_phased", "get_distance_phased");

	ClassDB::bind_method(D_METHOD("get_damping_per_sec"), &BulletWobbleData2D::get_damping_per_sec);
	ClassDB::bind_method(D_METHOD("set_damping_per_sec", "value"), &BulletWobbleData2D::set_damping_per_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "damping_per_sec"), "set_damping_per_sec", "get_damping_per_sec");

	ClassDB::bind_method(D_METHOD("get_delay_sec"), &BulletWobbleData2D::get_delay_sec);
	ClassDB::bind_method(D_METHOD("set_delay_sec", "value"), &BulletWobbleData2D::set_delay_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "delay_sec"), "set_delay_sec", "get_delay_sec");

	ClassDB::bind_method(D_METHOD("get_duration_sec"), &BulletWobbleData2D::get_duration_sec);
	ClassDB::bind_method(D_METHOD("set_duration_sec", "value"), &BulletWobbleData2D::set_duration_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "duration_sec"), "set_duration_sec", "get_duration_sec");

	ClassDB::bind_static_method(
			"BulletWobbleData2D",
			D_METHOD("generate_random_data", "amount_to_generate", "amplitude_MIN", "amplitude_MAX", "frequency_MIN", "frequency_MAX"),
			&BulletWobbleData2D::generate_random_data);
}
} //namespace BlastBullets2D
