#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace BlastBullets2D {
using namespace godot;

class BulletWobbleData2D : public Resource {
	GDCLASS(BulletWobbleData2D, Resource)

public:
	enum WobbleMode {
		WOBBLE_LATERAL = 0,
		WOBBLE_ANGULAR
	};

	enum WobbleWaveform {
		WOBBLE_SINE = 0,
		WOBBLE_COSINE
	};

	bool enabled = false;
	WobbleMode mode = WOBBLE_LATERAL;
	WobbleWaveform waveform = WOBBLE_SINE;
	real_t amplitude = 24.0;
	real_t frequency_hz = 2.0;
	real_t phase_rad = 0.0;
	real_t phase_step_per_bullet = 0.35;
	bool distance_phased = false;
	real_t damping_per_sec = 0.0;
	real_t delay_sec = 0.0;
	real_t duration_sec = 0.0;
	// Texture follow for wobble-steered bullets (mirrors the direction-curve
	// rotate_towards_adjusted_direction contract, but opt-in per wobble
	// entry instead of global). true (default) rotates the bullet visual
	// toward the wobble-steered heading each tick, so snakes point along
	// their path out of the box; false keeps the old heading-only behavior
	// (visual yaw untouched, position still snakes). Skipped while rotation
	// data drives the visual (same rule as direction curves).
	bool face_movement_direction = true;
	// Slew limit for the follow above (radians per second, 18 matches
	// direction curves). <= 0 snaps instantly; negative is rejected.
	real_t face_rotation_speed = 18.0;

	static TypedArray<BulletWobbleData2D> generate_random_data(
			int amount_to_generate,
			real_t amplitude_MIN,
			real_t amplitude_MAX,
			real_t frequency_MIN,
			real_t frequency_MAX);

	bool get_enabled() const;
	void set_enabled(bool value);

	WobbleMode get_mode() const;
	void set_mode(WobbleMode value);

	WobbleWaveform get_waveform() const;
	void set_waveform(WobbleWaveform value);

	real_t get_amplitude() const;
	void set_amplitude(real_t value);

	real_t get_frequency_hz() const;
	void set_frequency_hz(real_t value);

	real_t get_phase_rad() const;
	void set_phase_rad(real_t value);

	real_t get_phase_step_per_bullet() const;
	void set_phase_step_per_bullet(real_t value);

	bool get_distance_phased() const;
	void set_distance_phased(bool value);

	real_t get_damping_per_sec() const;
	void set_damping_per_sec(real_t value);

	real_t get_delay_sec() const;
	void set_delay_sec(real_t value);

	real_t get_duration_sec() const;
	void set_duration_sec(real_t value);

	bool get_face_movement_direction() const;
	void set_face_movement_direction(bool value);

	real_t get_face_rotation_speed() const;
	void set_face_rotation_speed(real_t value);

protected:
	static void _bind_methods();
};
} //namespace BlastBullets2D

VARIANT_ENUM_CAST(BlastBullets2D::BulletWobbleData2D::WobbleMode);
VARIANT_ENUM_CAST(BlastBullets2D::BulletWobbleData2D::WobbleWaveform);
