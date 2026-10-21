#pragma once

#include "bullet_spawner/pattern_spawner2d.hpp"
#include "data/laser_data2d.hpp"

namespace BlastBullets2D {
using namespace godot;

class LaserVolley2D;

// Fires lasers (LaserVolley2D, one per shot, a beam per pattern slot) along
// the patterns of PatternSpawner2D: patterns, cadence, bursts, telegraph,
// spin, movement, the graze and sound settings all work as for bullets.
// While LaserData2D.follow_spawner is on, the muzzles ride this spawner (its
// moves, spin and teleports). Track and Lock On lasers aim at the targets of
// the Laser Targeting group. The laser signals fire here while this spawner
// lives (grazes then bubble to the factory too); orphaned_lasers decides what
// its lasers do when it is freed.
class LaserSpawner2D : public PatternSpawner2D {
	GDCLASS(LaserSpawner2D, PatternSpawner2D)

public:
	// Serialized ids: never renumber.
	enum OrphanedLasers : int {
		ORPHANED_LASERS_FADE_OUT = 0, // every laser fades out (default)
		ORPHANED_LASERS_KEEP_FIRING = 1, // they fire on silently (no one hears their signals)
		ORPHANED_LASERS_HAND_TO_FACTORY = 2, // they fire on, signalling on the factory
		ORPHANED_LASERS_REMOVE = 3 // every laser ends at once
	};
	enum LaserTargetSource : int {
		LASER_TARGET_NONE = 0, // no targets (Track and Lock On keep their pattern direction)
		LASER_TARGET_NODE_GROUP = 1, // each beam: the nearest member of laser_target_group
		LASER_TARGET_NODE_PATH = 2, // every beam: the Node2D at laser_target_path
		LASER_TARGET_MOUSE = 3, // the cursor (read every frame)
		LASER_TARGET_GLOBAL_POSITION = 4 // laser_target_position
	};

	Ref<LaserData2D> laser_data;
	OrphanedLasers orphaned_lasers = ORPHANED_LASERS_FADE_OUT;
	int max_live_lasers = 0;
	LaserTargetSource laser_target_source = LASER_TARGET_NONE;
	StringName laser_target_group;
	StringName laser_target_filter_group;
	NodePath laser_target_path;
	Vector2 laser_target_position;
	double laser_retarget_interval_sec = 0.0;

	Ref<LaserData2D> get_laser_data() const { return laser_data; }
	void set_laser_data(const Ref<LaserData2D> &value);
	OrphanedLasers get_orphaned_lasers() const { return orphaned_lasers; }
	void set_orphaned_lasers(OrphanedLasers value);
	int get_max_live_lasers() const { return max_live_lasers; }
	void set_max_live_lasers(int value);
	LaserTargetSource get_laser_target_source() const { return laser_target_source; }
	void set_laser_target_source(LaserTargetSource value);
	StringName get_laser_target_group() const { return laser_target_group; }
	void set_laser_target_group(const StringName &value);
	StringName get_laser_target_filter_group() const { return laser_target_filter_group; }
	void set_laser_target_filter_group(const StringName &value);
	NodePath get_laser_target_path() const { return laser_target_path; }
	void set_laser_target_path(const NodePath &value);
	Vector2 get_laser_target_position() const { return laser_target_position; }
	void set_laser_target_position(const Vector2 &value);
	double get_laser_retarget_interval_sec() const { return laser_retarget_interval_sec; }
	void set_laser_retarget_interval_sec(double value);

	// Every laser this spawner fired that still fires.
	Array get_live_lasers() const;
	int get_live_laser_count() const;
	// Ends them all (fading, or at once). Returns how many.
	int end_live_lasers(bool fade = true);
	// Gives every live Track / Lock On laser the targets resolved now.
	// Returns how many lasers it retargeted.
	int retarget_live_lasers();
	// The targets the Laser Targeting group finds now: Node2Ds, or one
	// Vector2 for the point sources.
	Array resolve_laser_targets() const;
	// One sentence per group: what a shot fires, its targeting, graze, sound.
	PackedStringArray debug_explain_features() const;

	// ---- PatternSpawner2D hooks ----
	ShotRefusal2D shot_preflight(BulletFactory2D *factory) override;
	ShotResult2D fire_shot(std::vector<Transform2D> &transforms) override;
	bool needs_process_owned() const override;
	bool process_owned(double delta) override;
	void restart_owned_clocks() override;
	void on_predelete_owned(BulletFactory2D *factory) override;
	void append_payload_warnings(PackedStringArray &r_warnings) const override;
	void append_setup_warnings(PackedStringArray &r_warnings) const override;
	real_t preview_beam_length() const override;
	const char *payload_key() const override { return "laser_data"; }
	String payload_class_name() const override { return "LaserData2D"; }
	bool apply_payload_override(const Variant &value) override;

	void _validate_property(PropertyInfo &p_property) const;

protected:
	static void _bind_methods();

private:
	double retarget_time_left = 0.0;
	bool aims_at_targets() const;
	void assign_targets(LaserVolley2D *laser, const Array &targets) const;
	void collect_owned(std::vector<LaserVolley2D *> &r_out) const;
	void _on_laser_data_changed();
	void connect_laser_data(bool connect);
};

} // namespace BlastBullets2D

BLAST_VARIANT_ENUM_CAST(BlastBullets2D::LaserSpawner2D::OrphanedLasers);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::LaserSpawner2D::LaserTargetSource);
