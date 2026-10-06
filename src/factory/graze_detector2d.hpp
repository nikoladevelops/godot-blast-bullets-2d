#pragma once

#include "core/graze_targets2d.hpp"
#include "core/node_scan2d.hpp"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/node_path.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace BlastBullets2D {
using namespace godot;

class BulletFactory2D;

// The graze targets one zone tests during one factory sweep. Slots are
// STABLE: a target keeps its slot while it stays a target (across ticks and
// rescans), so a visit (BulletVolley2D graze state) names its target by
// slot. Volleys read a list during their own tick only and keep just its
// uid and serial between ticks.
struct GrazeTargetList2D {
	static constexpr int CAP = BulletGrazeZone2D::MAX_TARGETS;
	// Lists with more live targets than this keep by_x, and volleys test
	// each bullet only against the targets near it in x (a slab around its
	// motion) instead of every target. Tests move it
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
	uint64_t slot_ids[CAP] = {};
	uint64_t slot_changed[CAP] = {};
	// This sweep's usable targets, compact, in scan (tree) order;
	// slots[k] is the stable slot of target k.
	int count = 0;
	GrazeTarget2D targets[CAP];
	uint8_t slots[CAP] = {};
	// by_x_valid (count > slab_min_targets at the last update): indexes
	// into targets sorted by x (equal x by index, so tree order still
	// breaks ties).
	bool by_x_valid = false;
	uint8_t by_x[CAP] = {};
	// What the last scan found, kept until the next one (update interval).
	int member_count = 0;
	uint64_t members[CAP] = {};
	// Zone-group lists: the group they serve.
	StringName group;
	// Updated at most once per factory sweep (and per factory).
	uint64_t fresh_sweep = 0;
	uint64_t fresh_factory_id = 0;
	bool scan_due = true;
	double next_scan_time = 0.0;
	uint64_t last_used_sweep = 0;
};

// Finds the graze targets of the volleys armed with it. BulletFactory2D owns
// one (zone groups, rescanned every tick): factory volleys and spawners left
// at the default settings share its lists. A spawner with its own graze
// target settings owns another, shared (std::shared_ptr) with every volley
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
		SOURCE_ZONE_GROUPS = 0, // each zone's target_group
		SOURCE_NODE_PATH = 1, // the Node2D at target_path
		SOURCE_NODE_NAME = 2, // Node2Ds named like node_name (scene scan)
		SOURCE_NODE_CHILDREN = 3, // Node2D children of children_parent_path
	};
	struct Config {
		int source = SOURCE_ZONE_GROUPS;
		// Allow-list for every source (empty = everything found counts).
		StringName filter_group;
		NodePath target_path;
		String node_name = "Player";
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
	// Every list rescans at its next use; a new source rebuilds the lists.
	void set_config(const Config &p_config);
	// Every list rescans at its next use.
	void mark_due();
	// Drops every list (factory reset).
	void clear_lists();
	// Finds exactly what the factory's detector finds (zone groups, no
	// filter, every tick): list_for() hands out the factory's lists.
	bool shares_factory_lists() const;

	// The list `zone` tests during `factory`'s current sweep, updated at most
	// once per sweep (rescanned when due). nullptr when there is nothing to
	// look for (zone disabled, empty group) or the factory is not running.
	GrazeTargetList2D *list_for(const BulletGrazeZone2D &zone, BulletFactory2D &factory);
	// Same for an explicit group (zone-group sources; others ignore it).
	GrazeTargetList2D *list_for_group(const StringName &group, BulletFactory2D &factory);
	// A fresh scan (no lists, no clock): the editor ring preview and
	// scripts without a running factory. Returns how many qualified.
	int collect_now(const BulletGrazeZone2D &zone, SceneTree *tree, const World2D *world, GrazeTarget2D *r_targets, int &r_count);

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
	static uint64_t next_list_uid;

	int scan(const StringName &group, SceneTree *tree, const World2D *world, GrazeTarget2D *r_targets, int &r_count);
	void update(GrazeTargetList2D &list, BulletFactory2D &factory);
	void warn_overflow(const GrazeTargetList2D &list, int total, uint64_t factory_id) const;
	static void assign_slots(GrazeTargetList2D &list, const GrazeTarget2D *live, int live_count);
	static void sort_by_x(GrazeTargetList2D &list, bool same_targets);
};

} // namespace BlastBullets2D
