#pragma once

#include <godot_cpp/classes/resource.hpp>

namespace BlastBullets2D {
using namespace godot;

// Flock rules (boids): HOW the bullets of one flock steer by their
// flockmates. One resource = one flock's rules; WHO flies together is
// decided where the flock is armed: BulletSpawner2D.flock_scope for spawner
// volleys, the scope passed to BulletVolley2D.flock_join for the others.
// Separate flocks never see each other.
//
// Every tick, each bullet looks at the flockmates within neighbor_range
// (at most max_neighbors, nearest cells first) and turns its heading toward
// a mix of three wishes, weighted per second:
//   - separation: away from flockmates closer than separation_range,
//     harder the closer they are;
//   - alignment: the mean flight heading of its flockmates;
//   - cohesion: the center of its flockmates.
// Only the direction turns (speed stays owned by the speed data), with an
// exponential approach that never overshoots and looks the same at any
// frame rate.
//
// Armed volleys keep a reference and the factory reads these fields once
// per flock per tick, so a script that edits the resource changes every
// bullet already in flight on the next tick. Every accepted change emits
// `changed`.
class BulletFlock2D : public Resource {
	GDCLASS(BulletFlock2D, Resource)

public:
	// SERIALIZED ids: never renumber (BulletSpawner2D.flock_scope stores them).
	enum Scope {
		// The bullets of one spawner flock together (a factory volley: the
		// factory's own volleys of this flock).
		SCOPE_PER_SPAWNER = 0,
		// Every spawner and volley using this resource is ONE flock.
		SCOPE_SHARED = 1,
		// Each volley (one shot) is its own flock.
		SCOPE_PER_VOLLEY = 2
	};

	real_t separation = 1.5;
	real_t alignment = 1.0;
	real_t cohesion = 1.0;
	real_t neighbor_range = 100.0;
	real_t separation_range = 40.0;
	real_t max_turn_rate_degrees = 0.0; // degrees per second, 0 = unlimited
	bool face_heading = true;
	int max_neighbors = 32; // 0 = no limit
	real_t update_interval = 0.0; // seconds between steering updates, 0 = every tick
	real_t view_angle_degrees = 360.0; // field of view around the flight heading

	real_t get_separation() const;
	void set_separation(real_t value);

	real_t get_alignment() const;
	void set_alignment(real_t value);

	real_t get_cohesion() const;
	void set_cohesion(real_t value);

	real_t get_neighbor_range() const;
	void set_neighbor_range(real_t value);

	real_t get_separation_range() const;
	void set_separation_range(real_t value);

	real_t get_max_turn_rate_degrees() const;
	void set_max_turn_rate_degrees(real_t value);

	bool get_face_heading() const;
	void set_face_heading(bool value);

	int get_max_neighbors() const;
	void set_max_neighbors(int value);

	real_t get_update_interval() const;
	void set_update_interval(real_t value);

	real_t get_view_angle_degrees() const;
	void set_view_angle_degrees(real_t value);

protected:
	static void _bind_methods();

private:
	// A finite value >= 0 (or > 0 when positive_only) stored into field.
	void store_amount(real_t &field, real_t value, const char *property_name, bool positive_only);
};
} //namespace BlastBullets2D

VARIANT_ENUM_CAST(BlastBullets2D::BulletFlock2D::Scope);
