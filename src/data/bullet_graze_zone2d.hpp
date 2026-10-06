#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace BlastBullets2D {
using namespace godot;

// Graze zone: which nodes can graze bullets (every live Node2D of
// target_group, the first MAX_TARGETS in tree order; a spawner may find
// them another way: BulletSpawner2D.graze_target_source) and the
// concentric rings around each of them. A bullet grazes ring k when its motion during
// one tick comes within ring_<k+1>_radius (plus its own bounding radius when
// count_bullet_size is on) of a target's global position. Rings are
// independent circles: their order here does not matter.
//
// Armed on volleys by BulletSpawner2D.graze_zones (or
// BulletVolley2D.graze_set_zones): the volley keeps a reference and reads
// these fields every tick, so a script that edits a zone (a power-up that
// widens a ring, enabled = false) changes every bullet already in flight.
// Every accepted change emits `changed` (the spawner's ring preview
// listens).
class BulletGrazeZone2D : public Resource {
	GDCLASS(BulletGrazeZone2D, Resource)

public:
	static constexpr int MAX_RINGS = 4;
	// Live targets tested per zone (first in tree order; more warn once).
	// The per-bullet cost grows with the targets that are live, never with
	// this cap. A visit's target slot must fit the 6 anchor bits of the
	// volley's graze state (BulletVolley2D::GRAZE_ANCHOR_MASK).
	static constexpr int MAX_TARGETS = 64;

	// SERIALIZED ids: never renumber.
	enum Regraze {
		// Each ring of the zone grazes at most once per bullet life.
		REGRAZE_ONCE = 0,
		// Leaving the zone (the outermost ring of every target) re-arms
		// every ring of the zone for that bullet.
		REGRAZE_AFTER_EXIT = 1
	};

	bool enabled = true;
	StringName target_group = StringName("player");
	int ring_count = 1;
	real_t ring_radii[MAX_RINGS] = { 24.0, 40.0, 56.0, 72.0 };
	bool count_bullet_size = true;
	Regraze regraze = REGRAZE_ONCE;
	Color preview_color = Color(0.2, 0.9, 0.8, 0.8);
	// Debug view: while the game runs, the factory draws this zone's rings
	// around its targets (once, however many spawners share the zone), so
	// sizes can be tuned live. Off by default.
	bool preview_during_runtime = false;

	bool get_enabled() const;
	void set_enabled(bool value);

	StringName get_target_group() const;
	void set_target_group(const StringName &value);

	int get_ring_count() const;
	void set_ring_count(int value);

	real_t get_ring_1_radius() const { return ring_radii[0]; }
	void set_ring_1_radius(real_t value) { store_ring_radius(0, value, "ring_1_radius"); }
	real_t get_ring_2_radius() const { return ring_radii[1]; }
	void set_ring_2_radius(real_t value) { store_ring_radius(1, value, "ring_2_radius"); }
	real_t get_ring_3_radius() const { return ring_radii[2]; }
	void set_ring_3_radius(real_t value) { store_ring_radius(2, value, "ring_3_radius"); }
	real_t get_ring_4_radius() const { return ring_radii[3]; }
	void set_ring_4_radius(real_t value) { store_ring_radius(3, value, "ring_4_radius"); }

	// Index access for scripts (0 = ring_1). Valid for 0..MAX_RINGS-1
	// whatever ring_count is (hidden rings keep their stored radius).
	real_t get_ring_radius(int index) const;
	void set_ring_radius(int index, real_t value);
	// The radii of the active rings (the first ring_count).
	PackedFloat32Array get_active_ring_radii() const;

	bool get_count_bullet_size() const;
	void set_count_bullet_size(bool value);

	Regraze get_regraze() const;
	void set_regraze(Regraze value);

	Color get_preview_color() const;
	void set_preview_color(const Color &value);

	bool get_preview_during_runtime() const;
	void set_preview_during_runtime(bool value);

	// Rings beyond ring_count hide in the inspector (still stored).
	void _validate_property(PropertyInfo &p_property) const;

protected:
	static void _bind_methods();

private:
	void store_ring_radius(int index, real_t value, const char *property_name);
};
} //namespace BlastBullets2D

VARIANT_ENUM_CAST(BlastBullets2D::BulletGrazeZone2D::Regraze);
