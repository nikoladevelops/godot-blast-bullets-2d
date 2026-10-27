#pragma once

#include "core/variant_enum_cast2d.hpp"
#include "factory/graze_detector2d.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>

#include <memory>

namespace BlastBullets2D {
using namespace godot;

// WHO a script's bullets or lasers aim at, without a spawner: the members of
// a node group, one node, a parent's children, nodes named like a name, the
// mouse or fixed points (BulletTargets2D.from_*), filtered by filter_group
// and THE target filter, rescanned every update_interval. One detector
// (GrazeDetector2D) behind it is shared with every volley and laser it arms
// (graze_set_zones, sound_set_effects, LaserVolley2D.track): edits reach
// them in flight, and they keep it when the script drops the handle.
class BulletTargets2D : public RefCounted {
	GDCLASS(BulletTargets2D, RefCounted)

public:
	// SERIALIZED with GrazeDetector2D::Source (the same ids).
	enum Source : int {
		SOURCE_NODE_GROUP = 0,
		SOURCE_NODE = 1,
		SOURCE_NODE_NAME = 2,
		SOURCE_NODE_CHILDREN = 3,
		SOURCE_MOUSE = 4,
		SOURCE_POSITIONS = 5,
	};

	BulletTargets2D();

	static Ref<BulletTargets2D> from_group(const StringName &group, const StringName &filter_group = StringName());
	static Ref<BulletTargets2D> from_node(const Variant &node);
	static Ref<BulletTargets2D> from_children(const Variant &parent, bool recursive = false);
	static Ref<BulletTargets2D> from_name(const String &name, int match_mode = NODE_NAME_MATCH_CONTAINS, bool case_sensitive = false);
	static Ref<BulletTargets2D> from_positions(const PackedVector2Array &positions);
	static Ref<BulletTargets2D> from_mouse();

	Source get_source() const { return (Source)config.source; }
	StringName get_filter_group() const { return config.filter_group; }
	void set_filter_group(const StringName &value);
	double get_update_interval() const { return config.update_interval; }
	void set_update_interval(double value);
	PackedVector2Array get_positions() const { return config.global_positions; }
	void set_positions(const PackedVector2Array &value);

	// The targets now, as the bullets of `factory` see them this sweep (nodes
	// and, for points, Vector2s); a fresh scan of its world when it does not
	// run yet.
	Array resolve(const Variant &factory) const;
	// Every list rescans at its next use.
	void refresh();

	// C++: the detector every armed volley and laser shares.
	const std::shared_ptr<GrazeDetector2D> &get_detector() const { return detector; }

protected:
	static void _bind_methods();

private:
	GrazeDetector2D::Config config;
	std::shared_ptr<GrazeDetector2D> detector;
	void push() { detector->set_config(config); }
};

// A "targets" argument of the code API: a group name (StringName / String;
// null or empty = none) or a BulletTargets2D. False + one error "<fn>:
// <param> must be a group name or a BulletTargets2D, nothing changed."
// otherwise.
bool read_targets_argument2d(const Variant &value, const char *fn, const char *param, StringName &r_group, std::shared_ptr<GrazeDetector2D> &r_detector);

} // namespace BlastBullets2D

BLAST_VARIANT_ENUM_CAST(BlastBullets2D::BulletTargets2D::Source);
