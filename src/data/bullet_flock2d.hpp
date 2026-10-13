#pragma once

#include "core/variant_enum_cast2d.hpp"
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
// Only the direction turns (speed stays owned by the speed data): a bullet
// keeps its speed, so the wish acts like a sideways force on a car (the
// heading turns at |V| sin(angle to V) radians per second, never past V's
// direction, capped by max_turn_rate_degrees). A wish pointing straight
// back cannot turn a bullet, so a flockmate dead ahead never flips it.
//
// Armed volleys keep a reference and the factory reads these fields once
// per flock per tick, so a script that edits the resource changes every
// bullet already in flight on the next tick. Every accepted change emits
// `changed`.
class BulletFlock2D : public Resource {
	GDCLASS(BulletFlock2D, Resource)

public:
	// SERIALIZED ids: never renumber (BulletSpawner2D.flock_scope stores them).
	enum Scope : int {
		// The bullets of one spawner flock together (a factory volley: the
		// factory's own volleys of this flock).
		SCOPE_PER_SPAWNER = 0,
		// Every spawner and volley using this resource is ONE flock.
		SCOPE_SHARED = 1,
		// Each volley (one shot) is its own flock.
		SCOPE_PER_VOLLEY = 2
	};

	// Ready-made rule sets (apply_preset / create_preset; the inspector's
	// Preset menu). A preset writes the steering knobs only (weights, ranges,
	// lookahead, turn cap, blend-in, view angle); bullet size, facing, the
	// neighbor cap and the update interval are left alone. Never saved: the
	// menu shows the preset the current values match (Custom otherwise).
	enum Preset : int {
		PRESET_CUSTOM = 0, // the values as they are (applying it changes nothing)
		PRESET_SWARM = 1, // the defaults: spreads out while it chases, keeps clear gaps
		PRESET_SCHOOL = 2, // one aligned ribbon: everybody heads the same way
		PRESET_SPREAD = 3, // only avoid each other: no grouping, no lining up
		PRESET_CLUSTER = 4 // tight balls that swirl
	};

	// Defaults tuned so homing + flocking is clearly visible out of the box
	// with any sprite size (ranges are edge gaps, see count_bullet_size).
	real_t separation = 10.0;
	real_t alignment = 1.0;
	real_t cohesion = 1.5;
	real_t neighbor_range = 150.0;
	real_t separation_range = 24.0;
	// Separation measures where each pair will be this far ahead: bullets
	// already moving apart push less (no zigzag in a crowd).
	real_t separation_lookahead_sec = 0.2;
	// Ranges measured between bullet EDGES (the drawn sprite times the
	// bullet's scale) instead of centers: one setting fits every sprite size.
	bool count_bullet_size = true;
	// A cap keeps every turn a smooth arc (no snap in a crowded stack).
	real_t max_turn_rate_degrees = 270.0; // degrees per second, 0 = unlimited
	// A new bullet's steering fades in over this time (seconds since its
	// volley spawned): a burst stacked at the muzzle opens up first.
	real_t blend_in_sec = 0.3;
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

	real_t get_separation_lookahead_sec() const;
	void set_separation_lookahead_sec(real_t value);

	bool get_count_bullet_size() const;
	void set_count_bullet_size(bool value);

	real_t get_max_turn_rate_degrees() const;
	void set_max_turn_rate_degrees(real_t value);

	real_t get_blend_in_sec() const;
	void set_blend_in_sec(real_t value);

	bool get_face_heading() const;
	void set_face_heading(bool value);

	int get_max_neighbors() const;
	void set_max_neighbors(int value);

	real_t get_update_interval() const;
	void set_update_interval(real_t value);

	real_t get_view_angle_degrees() const;
	void set_view_angle_degrees(real_t value);

	// Writes a preset's steering knobs (one `changed` when anything moved).
	void apply_preset(Preset preset);
	// The preset whose steering knobs equal the current ones, else Custom.
	Preset get_matching_preset() const;
	// A new flock with a preset's rules.
	static Ref<BulletFlock2D> create_preset(Preset preset);

protected:
	static void _bind_methods();

private:
	// A finite value >= 0 (or > 0 when positive_only) stored into field.
	void store_amount(real_t &field, real_t value, const char *property_name, bool positive_only);
};
} //namespace BlastBullets2D

BLAST_VARIANT_ENUM_CAST(BlastBullets2D::BulletFlock2D::Scope);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::BulletFlock2D::Preset);
