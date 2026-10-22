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
		LASER_TARGET_GLOBAL_POSITION = 4, // laser_target_position
		LASER_TARGET_NODE_NAME = 5, // each beam: the nearest Node2D named like laser_target_node_name
		LASER_TARGET_NODE_CHILDREN = 6 // each beam: the nearest Node2D child of laser_target_children_parent_path
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
	String laser_target_node_name;
	HomingNodeNameMatch laser_target_name_match_mode = HOMING_NAME_MATCH_CONTAINS;
	bool laser_target_name_case_sensitive = false;
	NodePath laser_target_children_parent_path;
	bool laser_target_children_recursive = false;

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
	String get_laser_target_node_name() const { return laser_target_node_name; }
	void set_laser_target_node_name(const String &value);
	HomingNodeNameMatch get_laser_target_name_match_mode() const { return laser_target_name_match_mode; }
	void set_laser_target_name_match_mode(HomingNodeNameMatch value);
	bool get_laser_target_name_case_sensitive() const { return laser_target_name_case_sensitive; }
	void set_laser_target_name_case_sensitive(bool value);
	NodePath get_laser_target_children_parent_path() const { return laser_target_children_parent_path; }
	void set_laser_target_children_parent_path(const NodePath &value);
	bool get_laser_target_children_recursive() const { return laser_target_children_recursive; }
	void set_laser_target_children_recursive(bool value);

	// Every laser this spawner fired that still fires.
	Array get_live_lasers() const;
	int get_live_laser_count() const;
	// Ends them all (fading, or at once). Returns how many.
	int end_live_lasers(bool fade = true);
	// Gives every live Track / Lock On laser (by the data it was fired with)
	// the targets resolved now; None leaves them none. Returns how many
	// lasers it retargeted.
	int retarget_live_lasers();
	// Re-applies the spawner's settings to the lasers it fired: LIVE_GRAZE and
	// LIVE_SOUND arm or disarm them as the spawner stands now (a laser armed
	// alike is untouched), LIVE_HOMING retargets Track / Lock On lasers.
	// Returns how many lasers changed.
	int apply_to_live_lasers(BitField<LiveVolleyGroup> groups = LIVE_ALL);
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
	void on_teleported_owned(const Vector2 &jump, bool carry) override;
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
	// What a retarget hands the live Track / Lock On lasers (None: nothing).
	Array live_laser_targets() const;
	void assign_targets(LaserVolley2D *laser, const Array &targets) const;
	void collect_owned(std::vector<LaserVolley2D *> &r_out) const;
	void _on_laser_data_changed();
	void connect_laser_data(bool connect);
};

} // namespace BlastBullets2D

BLAST_VARIANT_ENUM_CAST(BlastBullets2D::LaserSpawner2D::OrphanedLasers);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::LaserSpawner2D::LaserTargetSource);
