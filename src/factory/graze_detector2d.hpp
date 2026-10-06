#pragma once

#include "core/graze_targets2d.hpp"
#include "core/node_scan2d.hpp"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace BlastBullets2D {
using namespace godot;

class BulletFactory2D;

// The graze targets a volley tests during one factory sweep (every zone of
// the volley rings the same targets). No cap: every target found counts.
// Slots are STABLE: a target keeps its slot while it stays a target (across
// ticks and rescans), so a visit (BulletVolley2D graze anchor) names its
// target by slot. Volleys read a list during their own tick only and keep
// just its uid and serial between ticks.
struct GrazeTargetList2D {
	// Lists with more live targets than this keep their test order sorted
	// by x, and volleys test each bullet only against the targets near it
	// in x (a slab around its motion) instead of every target. Tests move it
	// (BulletFactory2D.debug_set_graze_slab_min_targets) to prove both paths
	// find the same grazes.
	static constexpr int DEFAULT_SLAB_MIN_TARGETS = 8;
	static int slab_min_targets;
	// Unique per list object, never reused: a volley that meets another
	// uid treats every slot as changed.
	uint64_t uid = 0;
	// Bumped by every update that vacated or reassigned a slot;
	// slot_changed[s] is the serial of slot s's last change.
	uint64_t serial = 0;
	// Stable slots: slot -> target id (0 = free).
	std::vector<uint64_t> slot_ids;
	std::vector<uint64_t> slot_changed;
	// This sweep's usable targets in scan (tree) order; slots[k] is the
	// stable slot of target k.
	std::vector<GrazeTarget2D> targets;
	std::vector<uint32_t> slots;
	// The order volleys test them in: sorted by x when by_x_valid (more than
	// slab_min_targets at the last update; equal x by tree index), else
	// tree order. view_order[i] is the tree index of view entry i (ties).
	bool by_x_valid = false;
	std::vector<Vector2> view_centers;
	std::vector<uint32_t> view_slots;
	std::vector<uint32_t> view_order;
	// What the last scan found, kept until the next one (update interval).
	std::vector<uint64_t> members;
	// Node Group lists: the group they serve.
	StringName group;
	// Updated at most once per factory sweep (and per factory); within a
	// sweep, re-validated when user code ran since (user code epoch).
	uint64_t fresh_sweep = 0;
	uint64_t fresh_factory_id = 0;
	uint64_t validated_epoch = 0;
	bool scan_due = true;
	double next_scan_time = 0.0;
	uint64_t last_used_sweep = 0;

	int count() const { return (int)targets.size(); }
	bool contains(uint64_t id) const { return slot_of.find(id) != slot_of.end(); }

	// Slot bookkeeping (GrazeDetector2D::apply).
	std::unordered_map<uint64_t, uint32_t> slot_of;
	std::vector<uint32_t> free_slots;
	std::vector<uint64_t> slot_stamp;
	std::vector<uint32_t> by_x;
	uint64_t stamp = 0;
};

// Finds the graze targets of the volleys armed with it: THE one place that
// decides who grazes (zones only say how). BulletFactory2D owns one (one list
// per group, rescanned every tick) for factory volleys, which name their
// group when armed; spawners whose settings find exactly that (Node Group,
// no filter, every tick) share its lists. Every spawner owns a detector
// configured by its Graze group, shared (std::shared_ptr) with every volley
// it armed, so live setting edits and refresh_graze_targets() reach bullets
// in flight, and orphaned volleys keep it after the spawner is freed.
//
// Membership (who is a target) is rescanned every update_interval seconds
// of factory time (0 = every physics tick); positions are read every tick.
// Between rescans a found node that is freed or queued for deletion drops
// out at once, one that left the tree or its world sits out until it is
// back, and newcomers wait for the next rescan.
class GrazeDetector2D {
public:
	// SERIALIZED ids (BulletSpawner2D.graze_target_source): never renumber.
	enum Source {
		SOURCE_NODE_GROUP = 0, // Node2D members of node_group
		SOURCE_NODE_PATH = 1, // the Node2D at target_path
		SOURCE_NODE_NAME = 2, // Node2Ds named like node_name (scene scan)
		SOURCE_NODE_CHILDREN = 3, // Node2D children of children_parent_path
	};
	struct Config {
		int source = SOURCE_NODE_GROUP;
		StringName node_group;
		// Allow-list for every source (empty = everything found counts).
		StringName filter_group;
		NodePath target_path;
		String node_name;
		int node_name_match = NODE_NAME_MATCH_CONTAINS;
		bool node_name_case_sensitive = false;
		NodePath children_parent_path;
		bool children_recursive = false;
		double update_interval = 0.0;
	};
	// Lists nobody asked about for this many sweeps are dropped (bounded
	// memory when games churn through group names).
	static constexpr uint64_t kIdleSweeps = 600;

	// p_spawner_id: the spawner whose paths are resolved and who is never
	// its own target; 0 = the factory's detector.
	explicit GrazeDetector2D(uint64_t p_spawner_id);
	~GrazeDetector2D();
	GrazeDetector2D(const GrazeDetector2D &) = delete;
	GrazeDetector2D &operator=(const GrazeDetector2D &) = delete;

	const uint64_t spawner_id;

	const Config &get_config() const { return config; }
	// Every list rescans at its next use; a new source or group rebuilds
	// the lists.
	void set_config(const Config &p_config);
	// Every list rescans at its next use.
	void mark_due();
	// Drops every list (factory reset).
	void clear_lists();
	// Finds exactly what the factory's detector finds for node_group (Node
	// Group, no filter, every tick): list() hands out the factory's list.
	bool shares_factory_lists() const;

	// The targets this detector finds during `factory`'s current sweep,
	// updated at most once per sweep (rescanned when due) and re-validated
	// when user code ran since. nullptr when there is nothing to look for
	// (Node Group with an empty group) or the factory is not running.
	GrazeTargetList2D *list(BulletFactory2D &factory);
	// The Node Group list of an explicit group (the factory's detector, for
	// factory volleys); other sources ignore `group`.
	GrazeTargetList2D *list_for_group(const StringName &group, BulletFactory2D &factory);
	// A fresh scan (no lists, no clock): the editor ring preview and
	// scripts without a running factory.
	void collect_now(SceneTree *tree, const World2D *world, std::vector<GrazeTarget2D> &r_targets);

	int get_list_count() const { return (int)lists.size(); }
	// Debug counters: scans run by this detector, detectors alive.
	uint64_t scans = 0;
	static int64_t live_count;

private:
	Config config;
	// The nodes the paths resolved to at the last scan the spawner could
	// resolve (inside the tree): kept while it is out of the tree or freed.
	uint64_t path_target_id = 0;
	uint64_t children_parent_id = 0;
	std::vector<std::unique_ptr<GrazeTargetList2D>> lists;
	Array scan_stack;
	std::vector<GrazeTarget2D> live_scratch;
	std::vector<uint32_t> changed_scratch;
	static uint64_t next_list_uid;

	void scan(const StringName &group, SceneTree *tree, const World2D *world, std::vector<GrazeTarget2D> &r_targets);
	void update(GrazeTargetList2D &list, BulletFactory2D &factory);
	// Makes `live` the list's targets: stable slots, change serials, view.
	void apply(GrazeTargetList2D &list, const std::vector<GrazeTarget2D> &live);
	static void build_view(GrazeTargetList2D &list, bool same_targets);
};

} // namespace BlastBullets2D
