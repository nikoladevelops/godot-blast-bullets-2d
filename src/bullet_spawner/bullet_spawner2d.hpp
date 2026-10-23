#pragma once

#include "bullet_spawner/pattern_spawner2d.hpp"
#include "bullet_spawner/volley_tracker2d.hpp"

#include <unordered_set>

namespace BlastBullets2D {
using namespace godot;

// Fires bullets (BulletVolley2D) along the patterns of PatternSpawner2D:
// the spawn data, homing and orbiting, flocking, splitting, the arming of
// graze and sound, the orphan policy and the live-volley API.
class BulletSpawner2D : public PatternSpawner2D {
	GDCLASS(BulletSpawner2D, PatternSpawner2D)

public:
	// What happens to this spawner's in-flight volleys when the spawner
	// is FREED (never on a reparent / tree exit). Signal connections
	// live on the emitter, so once the spawner is gone nothing can handle
	// its bullets' hits or expiry - not even a callback that belongs to a
	// node that is still alive. Serialized ids: never renumber.
	enum OrphanedVolleys : int {
		// Bullets keep flying and dying; their hits reach no one, and a
		// one-time warning explains why (default).
		ORPHANED_VOLLEYS_KEEP_FLYING = 0,
		// The volleys become factory-owned: BulletFactory2D's
		// area_entered / body_entered / life_time_over fire for them.
		ORPHANED_VOLLEYS_HAND_TO_FACTORY = 1,
		// Every live bullet is cleared with its On Clear effects.
		ORPHANED_VOLLEYS_CLEAR = 2,
		// Every live bullet is removed silently.
		ORPHANED_VOLLEYS_REMOVE = 3
	};

	Ref<BulletVolleyData2D> spawn_data;

	Ref<BulletVolleyData2D> get_spawn_data() const;
	void set_spawn_data(const Ref<BulletVolleyData2D> &new_spawn_data);

	// PERFORMANCE / REPRODUCIBILITY.
	// Soft live-bullet fuse: 0 = unlimited, otherwise auto-shooting and
	// retargeting pause while active live bullets reach this count.
	int max_live_bullets = 0;
	// Stagger the retarget phase so N spawners don't scene-scan on the same
	// tick: actual period stays homing_retarget_interval_sec.
	double homing_retarget_phase = 0.0;

	// HOMING (EASY API)
	//
	// The spawner resolves homing targets at volley time and pushes them
	// onto the controllable BulletVolley2D instance returned by the
	// factory, using that class's shared / per-bullet deque API. Nothing
	// is stored in spawn_data: the same .tres stays movement-only while
	// every volley can chase different targets.
	//
	// Engine rules that still apply (handled for you, documented so the
	// behavior never surprises):
	// - Enabling (pool reuse) wipes all homing/orbit state, so the
	//   configuration below is re-applied on EVERY volley.
	// - Orbiting without a homing target does nothing: enable orbiting
	//   only takes effect once a non-empty deque exists, which is why
	//   targets are always pushed before orbiting is enabled.
	// - homing_take_control_of_texture_rotation defaults to TRUE here
	//   (the engine class defaults to false, which silently steers
	//   nothing). Turn it off only when a movement pattern, rotation
	//   data, or orbiting texture mode already owns the facing.
	enum HomingMode : int {
		HOMING_SHARED = 0, // one deque shared by every bullet of the volley
		HOMING_PER_BULLET // every bullet owns its own deque (same queue each)
	};

	// Where volley homing targets come from. Each source owns its own
	// setting group below (same dropdown-and-details pattern as
	// pattern_source): only the active source's options show in the
	// inspector, plus the shared steering block.
	enum HomingTargetSource : int {
		HOMING_SOURCE_NODE_GROUP = 0, // poll get_nodes_in_group(homing_node_group)
		HOMING_SOURCE_MOUSE, // chase the mouse cursor position
		HOMING_SOURCE_GLOBAL_POSITION, // chase homing_global_position (snapshot per volley)
		HOMING_SOURCE_NODE_PATH, // chase the Node2D at homing_target_path
		HOMING_SOURCE_NODE_NAME, // chase Node2Ds whose name matches homing_node_name
		HOMING_SOURCE_NODE_CHILDREN // chase Node2D children of homing_children_parent_path
	};

	// Which of the detected nodes become targets (node-group source).
	enum HomingTargetSelection : int {
		HOMING_SELECT_NEAREST = 0, // closest to the spawner, up to homing_max_targets
		HOMING_SELECT_RANDOM, // random pick, up to homing_max_targets
		HOMING_SELECT_FIRST, // tree order, up to homing_max_targets
		HOMING_SELECT_ROUND_ROBIN, // cycle through the group across volleys
		HOMING_SELECT_DISTRIBUTE // deal targets across bullets: bullet i chases pool[i % pool]
	};

	// Whether already-flying volleys keep chasing fresh targets.
	enum HomingRetargetMode : int {
		HOMING_RETARGET_OFF = 0,
		HOMING_RETARGET_ON_INTERVAL // re-resolve + replace every homing_retarget_interval_sec
	};

	// Master switch. False = volleys fly exactly as spawn_data says.
	bool homing_enabled = false;
	HomingMode homing_mode = HOMING_SHARED;
	HomingTargetSource homing_target_source = HOMING_SOURCE_NODE_GROUP;
	// Polled group for HOMING_SOURCE_NODE_GROUP (empty by default: the
	// developer names it). Only Node2D members are usable; the rest are
	// skipped quietly.
	StringName homing_node_group;
	// Extra allow-list applied on top of any source: when non-empty, only
	// targets that are ALSO in this group are kept. Empty = no filtering.
	StringName homing_filter_group;
	HomingTargetSelection homing_target_selection = HOMING_SELECT_NEAREST;
	// Seed for HOMING_SELECT_RANDOM picks. 0 = non-deterministic, >0 = reproducible.
	int homing_random_seed = 0;
	// Seconds of straight flight before volley steering starts (0 = steer
	// immediately). Must stay finite and >= 0.
	double homing_delay_sec = 0.0;
	// Seconds of steering before volleys fly straight (0 = infinite).
	// Must stay finite and >= 0.
	double homing_duration_sec = 0.0;
	// Steering pauses beyond this distance from the target (0 = unlimited).
	// Must stay finite and >= 0.
	double homing_lose_range_px = 0.0;
	// Fire cone: volleys whose aim deviates more than half this from the
	// target bearing are skipped (0 = omnidirectional). Must stay finite and >= 0.
	double homing_fire_arc_deg = 0.0;

	// How many targets enter the queue (1 = classic single-target homing).
	// Only the multi-target sources (node group, node name) use it.
	// Must stay >= 1 (setter rejects the rest).
	// NOTE: DISTRIBUTE needs 2+ to actually spread targets across the
	// volley. At 1 (this default) the deal is `pool[i % 1] == pool[0]` for
	// every bullet, so DISTRIBUTE behaves exactly like SHARED and warns.
	int homing_max_targets = 1;
	// One-shot latch for that DISTRIBUTE-degenerates warning; re-armed
	// when the selection leaves DISTRIBUTE (see resolve_homing_targets).
	mutable bool homing_distribute_degenerate_warned = false;
	// Detection radius around the spawner for the multi-target sources
	// (node group, node name). 0 = unlimited. Must stay finite and >= 0.
	double homing_max_detection_range = 0.0;
	// Snapshot chased per volley for HOMING_SOURCE_GLOBAL_POSITION.
	Vector2 homing_global_position = Vector2(0, 0);
	// Node chased per volley for HOMING_SOURCE_NODE_PATH. Must point at a
	// Node2D; a missing/freed/wrong-type target skips homing for that
	// volley with a warning, never aborts it.
	NodePath homing_target_path;
	// NODE-NAME SOURCE (HOMING_SOURCE_NODE_NAME)
	//
	// Finds targets by node name instead of group membership: the whole
	// scene is scanned for Node2Ds whose name matches homing_node_name
	// per homing_node_name_match_mode, then the shared selection
	// (nearest/random/first/round robin), homing_max_targets, range, and
	// filter-group rules pick the queue. Handy when enemies are spawned
	// dynamically and never added to a group.
	// Empty by default: the developer names it.
	String homing_node_name;
	HomingNodeNameMatch homing_node_name_match_mode = HOMING_NAME_MATCH_CONTAINS;
	// Case-insensitive by default: "Boss" matches "BOSS". Turn on for
	// strict comparison.
	bool homing_node_name_case_sensitive = false;
	// NODE-CHILDREN SOURCE (HOMING_SOURCE_NODE_CHILDREN)
	//
	// Chases the Node2D children of one parent node instead of a group or
	// a name scan: point it at an enemy container, a carrier, or anything
	// whose children should be hunted. The path is required (empty
	// resolves nothing with a warning); a missing/freed/non-Node parent
	// skips homing for that volley, never aborts it. The shared selection
	// (nearest/random/first/round robin), homing_max_targets, range, and
	// filter-group rules pick the queue, exactly like the group source.
	NodePath homing_children_parent_path;
	// When true, grandchildren and deeper descendants are included too.
	bool homing_children_recursive = false;
	// Max turn rate in radians/sec. 0 = snap instantly. Must stay finite
	// and >= 0 (setter rejects the rest).
	double homing_smoothing = 5.0;
	// Seconds between target position refreshes. 0 = every tick.
	double homing_update_interval = 0.0;
	// A target counts as reached inside this distance (predictive, so
	// fast bullets still trigger). Must stay finite and >= 0.
	double homing_distance_before_reached = 5.0;
	bool homing_reach_counts_bullet_size = false;
	bool homing_take_control_of_texture_rotation = true;
	// Pop the reached target so the queue advances: per bullet in
	// per-bullet mode, once per tick for the shared queue in shared mode.
	// (Bullet directions following rotation is the spawn data's own
	// adjust_direction_based_on_rotation; the spawner no longer
	// overrides it.)
	bool homing_auto_pop_after_target_reached = false;
	// When true, per-bullet smoothing fans out linearly: bullet i steers
	// with homing_smoothing_start + homing_smoothing_step * i.
	bool homing_per_bullet_smoothing_enabled = false;
	double homing_smoothing_start = 5.0;
	double homing_smoothing_step = 0.0;
	HomingRetargetMode homing_retarget_mode = HOMING_RETARGET_ON_INTERVAL;
	// Seconds between retarget passes. Must stay > 0. Short by default so
	// group / name / children membership (spawns, deaths) is picked up
	// live; each pass is cheap (id validation + one re-resolve, skipped
	// volleys untouched).
	double homing_retarget_interval_sec = 0.5;
	// When true (default), interval retargeting re-aims every tracked
	// live volley. When false, only the newest tracked volley is
	// re-aimed: older volleys keep flying at whatever they chased last
	// (or nothing). Useful for "latest shot follows the player, old
	// shots go dumb" patterns.
	bool homing_retarget_previous_volleys = true;

	// ORBITING (EASY API)
	//
	// Applied after the homing targets of the same volley, so freshly
	// spawned bullets can lock onto their ring immediately. Requires a
	// homing target: orbiting_enabled without homing_enabled warns and
	// does nothing (engine rule, see above).
	bool orbiting_enabled = false;
	// Ring radius in pixels. Must stay >= 0.01 (setter rejects the rest).
	double orbiting_radius = 64.0;
	BulletVolley2D::OrbitingDirection orbiting_direction = BulletVolley2D::OrbitRight;
	BulletVolley2D::OrbitingTextureRotation orbiting_texture_rotation = BulletVolley2D::FaceTarget;
	// When true, bullet i orbits with orbiting_radius_start +
	// orbiting_radius_step * i (concentric shells) instead of the flat
	// orbiting_radius.
	bool orbiting_radius_linear_enabled = false;
	double orbiting_radius_start = 64.0;
	double orbiting_radius_step = 0.0;
	// How the locked ring follows a moving target: FollowTarget tracks it
	// 1:1, FollowDeadzone pins the center until the target walks
	// farther than orbiting_follow_deadzone from it (jitter zone), Anchored
	// freezes the ring where it locked and ignores target motion.
	BulletVolley2D::OrbitingFollowMode orbiting_follow_mode = BulletVolley2D::FollowTarget;
	// Deadzone radius in pixels, used only by FollowDeadzone.
	double orbiting_follow_deadzone = 8.0;
	// What drops the lock: RelockAlways re-acquires every retarget,
	// StayLocked never unlocks on retarget/empty (only explicit
	// disable, clear, or a freed target), RelockOnTargetChange unlocks
	// only when the front target is a different target.
	BulletVolley2D::OrbitingLockPolicy orbiting_lock_policy = BulletVolley2D::RelockAlways;
	// When on, locked OrbitLeft/OrbitRight bullets translate 1:1 with the
	// target and keep circling (rings never lag or stretch when the
	// target moves). When off, locked bullets chase the ring center
	// clamped to speed * delta, so slow bullets trail fast targets.
	bool orbiting_rigid_follow = true;

	bool get_homing_enabled() const;
	void set_homing_enabled(bool value);
	HomingMode get_homing_mode() const;
	void set_homing_mode(HomingMode value);
	HomingTargetSource get_homing_target_source() const;
	void set_homing_target_source(HomingTargetSource value);
	StringName get_homing_node_group() const;
	void set_homing_node_group(const StringName &value);
	StringName get_homing_filter_group() const;
	void set_homing_filter_group(const StringName &value);
	HomingTargetSelection get_homing_target_selection() const;
	void set_homing_target_selection(HomingTargetSelection value);
	int get_homing_max_targets() const;
	void set_homing_max_targets(int value);
	double get_homing_max_detection_range() const;
	void set_homing_max_detection_range(double value);
	Vector2 get_homing_global_position() const;
	void set_homing_global_position(const Vector2 &value);
	NodePath get_homing_target_path() const;
	void set_homing_target_path(const NodePath &p_path);
	String get_homing_node_name() const;
	void set_homing_node_name(const String &value);
	HomingNodeNameMatch get_homing_node_name_match_mode() const;
	void set_homing_node_name_match_mode(HomingNodeNameMatch value);
	bool get_homing_node_name_case_sensitive() const;
	void set_homing_node_name_case_sensitive(bool value);
	NodePath get_homing_children_parent_path() const;
	void set_homing_children_parent_path(const NodePath &p_path);
	bool get_homing_children_recursive() const;
	void set_homing_children_recursive(bool value);
	double get_homing_smoothing() const;
	void set_homing_smoothing(double value);
	double get_homing_update_interval() const;
	void set_homing_update_interval(double value);
	double get_homing_distance_before_reached() const;
	void set_homing_distance_before_reached(double value);
	bool get_homing_reach_counts_bullet_size() const;
	void set_homing_reach_counts_bullet_size(bool value);
	bool get_homing_take_control_of_texture_rotation() const;
	void set_homing_take_control_of_texture_rotation(bool value);
	bool get_homing_auto_pop_after_target_reached() const;
	void set_homing_auto_pop_after_target_reached(bool value);
	bool get_homing_per_bullet_smoothing_enabled() const;
	void set_homing_per_bullet_smoothing_enabled(bool value);
	double get_homing_smoothing_start() const;
	void set_homing_smoothing_start(double value);
	double get_homing_smoothing_step() const;
	void set_homing_smoothing_step(double value);
	HomingRetargetMode get_homing_retarget_mode() const;
	void set_homing_retarget_mode(HomingRetargetMode value);
	double get_homing_retarget_interval_sec() const;
	void set_homing_retarget_interval_sec(double value);
	bool get_homing_retarget_previous_volleys() const;
	void set_homing_retarget_previous_volleys(bool value);

	bool get_orbiting_enabled() const;
	void set_orbiting_enabled(bool value);
	double get_orbiting_radius() const;
	void set_orbiting_radius(double value);
	BulletVolley2D::OrbitingDirection get_orbiting_direction() const;
	void set_orbiting_direction(BulletVolley2D::OrbitingDirection value);
	BulletVolley2D::OrbitingTextureRotation get_orbiting_texture_rotation() const;
	void set_orbiting_texture_rotation(BulletVolley2D::OrbitingTextureRotation value);
	bool get_orbiting_radius_linear_enabled() const;
	void set_orbiting_radius_linear_enabled(bool value);
	double get_orbiting_radius_start() const;
	void set_orbiting_radius_start(double value);
	double get_orbiting_radius_step() const;
	void set_orbiting_radius_step(double value);
	BulletVolley2D::OrbitingFollowMode get_orbiting_follow_mode() const;
	void set_orbiting_follow_mode(BulletVolley2D::OrbitingFollowMode value);
	double get_orbiting_follow_deadzone() const;
	void set_orbiting_follow_deadzone(double value);
	BulletVolley2D::OrbitingLockPolicy get_orbiting_lock_policy() const;
	void set_orbiting_lock_policy(BulletVolley2D::OrbitingLockPolicy value);
	bool get_orbiting_rigid_follow() const;
	void set_orbiting_rigid_follow(bool value);

	// FLOCKING (bullet_spawner2d_flock.cpp). Every volley fired while
	// flock_enabled is on (and flock is set) flies as a boids flock by the
	// rules in flock (BulletFlock2D: separation, alignment, cohesion,
	// ranges). WHO flies together is decided here: flock_scope groups this
	// spawner's bullets (Per Spawner), every user of the same resource
	// (Shared) or each shot alone (Per Volley). The settings are shared with
	// every volley this spawner armed: turning flocking off stops it for
	// bullets in flight (on again resumes them), a new flock or scope reaches
	// them too, and orphaned volleys keep the last settings. Bullets fired
	// while flocking was off never flock.
	bool flock_enabled = false;
	Ref<BulletFlock2D> flock;
	BulletFlock2D::Scope flock_scope = BulletFlock2D::SCOPE_PER_SPAWNER;

	bool get_flock_enabled() const;
	void set_flock_enabled(bool value);
	Ref<BulletFlock2D> get_flock() const;
	void set_flock(const Ref<BulletFlock2D> &value);
	BulletFlock2D::Scope get_flock_scope() const;
	void set_flock_scope(BulletFlock2D::Scope value);

	// SPLITTING (bullet_spawner2d_split.cpp). Every volley fired while
	// split_enabled is on (and split_data is set) splits by split_data: a
	// bullet that dies by one of its stage's triggers bursts into children
	// that copy this spawner's spawn data, with the stage's direction and
	// speed. Children stay this spawner's (its signals, graze, sound, flock,
	// time scale, orphan policy) and split again by the next stage. The
	// settings are shared with every volley this spawner armed: turning
	// splitting off stops it for bullets in flight (on again resumes them),
	// edits to split_data reach them too, orphans keep the last settings.
	// Bullets fired while splitting was off never split.
	bool split_enabled = false;
	Ref<BulletSplitData2D> split_data;

	bool get_split_enabled() const;
	void set_split_enabled(bool value);
	Ref<BulletSplitData2D> get_split_data() const;
	void set_split_data(const Ref<BulletSplitData2D> &value);
	// The factory flush: aims a child volley like a fresh shot of this
	// spawner (homing and orbiting, with their signals). False while homing
	// is off: the children then chase their parent's target.
	bool split_configure_child_homing(BulletVolley2D *child);

	// EMITTER BULLETS (bullet_spawner2d_emitter.cpp). Every volley fired
	// while emitter_enabled is on fires the pattern of the spawner at
	// emitter_spawner_path (any PatternSpawner2D, normally with its own
	// shooting off) from each flying bullet, timed by `emitter` (empty = a
	// default BulletEmitterData2D: every 0.5 s along the heading). The
	// template fires its whole setup (pattern, spawn data, homing, sound,
	// graze, splits, its own emitters) and its own signals. Turning it off
	// stops bullets in flight (on resumes them), a new template or edits to
	// `emitter` reach them too; orphans keep the last settings. A template
	// chain that leads back to this spawner is refused (one error).
	bool emitter_enabled = false;
	Ref<BulletEmitterData2D> emitter;
	NodePath emitter_spawner_path;
	bool get_emitter_enabled() const { return emitter_enabled; }
	void set_emitter_enabled(bool value);
	Ref<BulletEmitterData2D> get_emitter() const { return emitter; }
	void set_emitter(const Ref<BulletEmitterData2D> &value);
	NodePath get_emitter_spawner_path() const { return emitter_spawner_path; }
	void set_emitter_spawner_path(const NodePath &value);
	// The template the path names now (null when empty, missing or not a
	// PatternSpawner2D).
	PatternSpawner2D *get_emitter_template() const;
	// The emitter in use: `emitter`, else the spawner's default one.
	Ref<BulletEmitterData2D> get_effective_emitter() const;

	int get_homing_random_seed() const;
	void set_homing_random_seed(int value);
	int get_max_live_bullets() const;
	void set_max_live_bullets(int value);
	double get_homing_retarget_phase() const;
	void set_homing_retarget_phase(double value);

	double get_homing_delay_sec() const;
	void set_homing_delay_sec(double value);
	double get_homing_duration_sec() const;
	void set_homing_duration_sec(double value);
	double get_homing_lose_range_px() const;
	void set_homing_lose_range_px(double value);
	double get_homing_fire_arc_deg() const;
	void set_homing_fire_arc_deg(double value);

	int get_active_live_bullet_count() const;

	// Resolves the current homing targets without touching any volley:
	// node-group members (filtered + selected), the node at
	// homing_target_path, or the homing_global_position snapshot.
	// MOUSE source returns an empty array (the cursor is pushed live, not
	// resolved). Entries are Node2D* Variants or Vector2. With quiet =
	// false (default) an empty result warns once until a resolution
	// succeeds again; retarget passes use quiet = true and silently keep
	// the old queues instead. advance_round_robin = false resolves the
	// round-robin pick without consuming the cursor (used by retarget
	// passes so flying volleys keep stable targets).
	Array resolve_homing_targets(bool quiet = false, bool advance_round_robin = true) const;
	// Re-resolves targets and replaces the queues of every still-alive
	// volley owned by this spawner. Volleys with no resolvable targets
	// are skipped quietly. Returns how many volleys were retargeted.
	int retarget_live_volleys();
	// How many ACTIVE volleys this spawner owns (factory census: every
	// shot, homing or not, plus adopted volleys).
	int get_live_volley_count() const;
	// Every ACTIVE volley this spawner owns, in factory order. Lets
	// GDScript call the full BulletVolley2D API on each volley directly.
	Array get_live_volleys() const;
	// The retarget list: every live homing shot + adopted volley (no cap,
	// pruned first). retarget_live_volleys() walks this list.
	int get_tracked_volley_count() const;
	// Forgets the retarget list (the volleys keep flying untouched).
	void forget_tracked_volleys();
	// Clears every live bullet this spawner owns (fire_clear_effects:
	// On Clear at each pose); parked volleys go back to the pool. Safe
	// from handlers. Returns how many bullets were cleared.
	int clear_active_bullets(bool fire_clear_effects = true);
	int orphaned_volleys = ORPHANED_VOLLEYS_KEEP_FLYING;

	double bullet_speed_scale_min = 1.0;
	double bullet_speed_scale_max = 1.0;
	// Per-bullet lifetime multiplier, rolled per bullet like the speed scale
	// (bullet_random_seed): each bullet lives its lifetime (the data's own
	// entry, else max_life_time) times its roll. 1..1 (default) = off.
	double bullet_life_time_scale_min = 1.0;
	double bullet_life_time_scale_max = 1.0;

	std::vector<real_t> shot_speed_factors;
	std::vector<real_t> shot_life_factors;
	OrphanedVolleys get_orphaned_volleys() const { return static_cast<OrphanedVolleys>(orphaned_volleys); }
	void set_orphaned_volleys(OrphanedVolleys value);

	// Turns every live bullet of the volleys this spawner fired into pickups
	// (see BulletFactory2D.convert_all_bullets_to_pickups). Returns the pickup volleys.
	Array convert_live_bullets_to_pickups(const Ref<BulletPickupData2D> &pickup, const Variant &target);

	double get_bullet_speed_scale_min() const { return bullet_speed_scale_min; }
	void set_bullet_speed_scale_min(double value);
	double get_bullet_speed_scale_max() const { return bullet_speed_scale_max; }
	void set_bullet_speed_scale_max(double value);

	void roll_speed_factors(size_t count);
	double get_bullet_life_time_scale_min() const { return bullet_life_time_scale_min; }
	void set_bullet_life_time_scale_min(double value);
	double get_bullet_life_time_scale_max() const { return bullet_life_time_scale_max; }
	void set_bullet_life_time_scale_max(double value);
	bool life_time_roll_active() const { return bullet_life_time_scale_min != 1.0 || bullet_life_time_scale_max != 1.0; }
	void roll_life_time_factors(size_t count);

	// BULLET COLORS (bullet_spawner2d_colors.cpp). Each shot picks one color
	// per bullet from bullet_color_gradient (empty = a rainbow) and hands them
	// to the factory with the transforms: the volley draws per-bullet colors
	// (BulletVolley2D.get_bullet_color) while spawn_data stays untouched.
	// Serialized ids: never renumber.
	enum BulletColorMode : int {
		COLOR_MODE_OFF = 0, // the spawn data's own colors (or none)
		COLOR_MODE_GRADIENT_ACROSS_PATTERN = 1, // bullet i of n samples i / (n - 1): a rainbow ring
		COLOR_MODE_RANDOM_FROM_GRADIENT = 2, // every bullet samples a random point (bullet_random_seed)
		COLOR_MODE_GRADIENT_PER_SHOT = 3 // the whole shot wears one color, stepping every shot
	};
	BulletColorMode bullet_color_mode = COLOR_MODE_OFF;
	Ref<Gradient> bullet_color_gradient;
	// Across Pattern: how far along the gradient the colors move per shot
	// (0 = still; any other value wraps the gradient around: bullet i samples
	// i / n + shots x shift).
	double bullet_color_shift_per_shot = 0.0;
	// Per Shot: shots per trip along the gradient (shot k samples
	// (k % cycle) / (cycle - 1)).
	int bullet_color_cycle_shots = 6;
	std::vector<Color> shot_colors;
	BulletColorMode get_bullet_color_mode() const { return bullet_color_mode; }
	void set_bullet_color_mode(BulletColorMode value);
	Ref<Gradient> get_bullet_color_gradient() const { return bullet_color_gradient; }
	void set_bullet_color_gradient(const Ref<Gradient> &value);
	double get_bullet_color_shift_per_shot() const { return bullet_color_shift_per_shot; }
	void set_bullet_color_shift_per_shot(double value);
	int get_bullet_color_cycle_shots() const { return bullet_color_cycle_shots; }
	void set_bullet_color_cycle_shots(int value);
	// The color at t (0..1) of the gradient, or of the rainbow while it is empty.
	Color sample_bullet_color(double t) const;
	// The colors the next shot of `count` bullets wears (empty while off):
	// the same rolls shoot_once uses, without firing.
	PackedColorArray collect_shot_colors(int count) const;
	// Fills shot_colors for a shot (cleared while off).
	void roll_shot_colors(size_t count);

	// Current retarget countdown in seconds (time until the next interval
	// pass). For tests asserting deterministic stagger across spawners.
	double debug_get_retarget_countdown() const { return homing_retarget_time_left; }

	// Takes ownership of a manually-woken volley (see enable_bullet,
	// which detaches spawner ownership): stamps, hooks the reached
	// forwarder, and tracks it for retargeting. Queues are left alone.
	// Returns false for null or non-live (pooled/outside-tree) instances.
	bool adopt_live_volley(BulletVolley2D *bullets);
	// Script entry (bound): the Variant is read by id, a freed volley is refused.
	bool adopt_live_volley_script(const Variant &volley);
	// Kill-switch: clears homing queues and disables orbiting on every
	// tracked live volley (engine clears, counters stay exact). Returns
	// how many volleys were touched. Retargeting will re-arm them while
	// homing stays on; turn homing off (or clear tracking) to keep them dumb.
	int clear_live_volleys_homing();
	// LIVE CONTROL (bullet_spawner2d_live.cpp): what this spawner changes on
	// the bullets it already fired (every volley it owns, live or parked,
	// homing or not). Each returns how many volleys it changed.
	//
	// Applies the chosen groups as they stand now: graze and sound arm the
	// volleys not armed with the current settings (fired while off, before
	// an edit, or re-armed by a script) and disarm them while off; flocking
	// and splitting arm the volleys fired while off (off already reaches
	// flying bullets); homing arms the volleys fired while off (resolving
	// targets now, with its signals; a parked volley when it wakes) and
	// clears every chase while off, parked volleys included. A volley already
	// armed alike is left alone (spent graze rings stay spent).
	int apply_to_live_volleys(BitField<LiveVolleyGroup> groups = LIVE_ALL);
	// BulletVolley2D::enable_bullet: a parked volley marked by
	// apply_to_live_volleys(LIVE_HOMING) woke; arm it like a fresh shot.
	void arm_homing_after_wake(BulletVolley2D *volley);
	// Every bullet chases `target` now: a Node2D, a Vector2, a group name
	// (each bullet the nearest member) or null (the mouse), with the Homing
	// group's steering even if they were fired without homing. A retarget
	// pass of the Homing group may later replace it (turn retargeting off to
	// keep it). Bad targets error and change nothing.
	int home_live_volleys_to(const Variant &target);
	// Every bullet turns to fly toward `position` (direction curves keep
	// theirs, with one warning per volley).
	int aim_live_volleys_at(const Vector2 &position);
	// Every bullet's speed along its direction (a capped max_speed rises;
	// speed curves keep theirs with a warning). Must be finite.
	int set_live_volleys_speed(double speed);
	// Speed, top speed and acceleration of every bullet times `factor`
	// (>= 0; 0 stops them). Speed curves keep theirs.
	int scale_live_volleys_speed(double factor);
	// Every bullet's exact velocity (curves override direct velocity: the
	// engine warns and no-ops per bullet when a speed curve is assigned).
	// Must be finite.
	int override_live_volleys_velocity(const Vector2 &new_velocity);

	// One plain sentence per steering group (homing, orbiting, flocking,
	// graze, random sizes): what it will do now, why it would do nothing,
	// which features fight; then the setup warnings (bullet_spawner2d_explain.cpp).
	PackedStringArray debug_explain_features() const;

	// Hooks of the PatternSpawner2D shot and lifecycle (bullet_spawner2d.cpp).
	ShotRefusal2D shot_preflight(BulletFactory2D *factory) override;
	ShotRefusal2D shot_prepare() override;
	ShotResult2D fire_shot(std::vector<Transform2D> &transforms) override;
	bool needs_process_owned() const override;
	bool process_owned(double delta) override;
	void restart_owned_clocks() override;
	void on_predelete_owned(BulletFactory2D *factory) override;
	void on_exit_tree_owned() override;
	void on_config_changed_owned() override;
	void shift_owned_shots(const Vector2 &jump) override;
	void append_payload_warnings(PackedStringArray &r_warnings) const override;
	void append_setup_warnings(PackedStringArray &r_warnings) const override;
	real_t preview_collision_ring_radius() const override;
	bool shots_ignore_despawn_bounds() const override;
	const char *payload_key() const override { return "spawn_data"; }
	String payload_class_name() const override { return "BulletVolleyData2D"; }
	bool apply_payload_override(const Variant &value) override;
	Dictionary debug_get_pattern_cache_info() const override;

	void _validate_property(PropertyInfo &p_property) const;

protected:
	static void _bind_methods();

	// The shot's data (shot_prepare -> fire_shot): the spawner-owned copy of
	// spawn_data whose transforms each shot overwrites.
	Ref<BulletVolleyData2D> shot_volley_data;
	bool speed_roll_active() const;
	// apply_to_live_volleys, one group each (ids of the volleys changed).
	void graze_live_volleys(std::unordered_set<uint64_t> &r_changed);
	void sound_live_volleys(std::unordered_set<uint64_t> &r_changed);
	void flock_live_volleys(std::unordered_set<uint64_t> &r_changed);
	void split_live_volleys(std::unordered_set<uint64_t> &r_changed);
	void homing_live_volleys(std::unordered_set<uint64_t> &r_changed);
	static bool volley_has_own_homing_targets(BulletVolley2D *volley);
	static void clear_volley_homing(BulletVolley2D *volley);

	// Homing/orbiting runtime state (never stored).
	// Tracked-volley registry for interval retargeting. VolleyTracker2D
	// owns the ids plus the prune-before-touch invariant (every reader
	// prunes freed/re-homed/inactive volleys first, and every resolved
	// volley is re-validated), so pooled or adopted volleys can never be
	// touched by mistake. Mutable: const readers prune dead entries.
	mutable VolleyTracker2D volley_tracker;
	// Countdown to the next retarget pass. 0.0 means "due on the next
	// tick": arming (or reset_shooting) always re-arms to due-now so the
	// first pass never waits a full interval.
	double homing_retarget_time_left = 0.0;
	// Warn-once latch for empty target resolution (mutable: used by the
	// const resolve_homing_targets). One warning per homing target
	// configuration: shots resolve non-quietly, so a typo'd group or a
	// dead target path is reported, but never once per volley.
	mutable bool homing_empty_targets_warned = false;
	// Cursor for HOMING_SELECT_ROUND_ROBIN across volleys (mutable: used
	// by the const resolve_homing_targets).
	mutable int homing_round_robin_cursor = 0;
	// Lazily created RNG for HOMING_SELECT_RANDOM (mutable: used by the
	// const resolve_homing_targets).
	mutable Ref<RandomNumberGenerator> homing_rng;
	// Reusable per-resolve scratch (mutable: used by the const
	// resolve_homing_targets + collectors). Cleared at each entry; never
	// nested in practice (collectors run once per resolve and neither
	// they nor the selection branch emit or call anything user-
	// overridable, and every bound entry runs sequentially on the main
	// thread), so sharing is safe. Saves 2-4 Array allocations every
	// volley and every retarget pass. If a future change lets user code
	// run mid-resolve, add a reentrancy latch here instead of silently
	// corrupting the outer pass.
	mutable Array homing_candidates_scratch;
	// Candidate-pool reuse across the resolves of ONE shot or ONE
	// retarget pass (see resolve_homing_targets). Scoped by
	// HomingCandidatePass in the .cpp; never left active.
	mutable bool homing_candidate_pass_active = false;
	mutable bool homing_candidate_pass_filled = false;
	mutable Array homing_pool_scratch;
	mutable Array homing_scan_stack;

	// Duplicate cache: shoot_once() must never mutate the user's
	// spawn_data (transforms are overwritten per volley), so the first
	// shot duplicates it and later shots with the same resource reuse the
	// spawner-owned template (transforms overwritten each shot). Cleared
	// on set_spawn_data() and on the resource's changed signal, so
	// in-place inspector edits re-duplicate instead of driving stale data.
	// Mutable: shoot_once() is non-const but resolve paths also touch it.
	mutable Ref<BulletVolleyData2D> cached_volley_template;
	mutable uint64_t cached_spawn_data_id = 0;
	void _on_spawn_data_changed();

	// Retarget clock: an explicit phase verbatim, else a per-instance
	// stagger so spawners enabled together never scan on the same tick.
	void arm_retarget_countdown();

	// True while interval retargeting must keep _process alive: homing on
	// + retarget armed + inside the tree at runtime.
	bool homing_retarget_active() const;
	// Wakes _process when retargeting becomes active (runtime only).
	void update_homing_process_state(bool reset_countdown);

	// Remembers a fresh volley for retargeting (deduped: pooled instances
	// reuse ids) and prunes dead/foreign entries via the tracker.
	void track_live_volley(BulletVolley2D *bullets);
	// The World2D homing targets must live in: the factory's (where the
	// volleys fly), else this spawner's.
	// Scene scan for the node-name source: collects usable Node2Ds under
	// p_node whose name matches homing_node_name per
	// homing_node_name_match_mode and homing_node_name_case_sensitive,
	// honoring homing_filter_group. Never this spawner or anything under it.
	void collect_homing_candidates_by_name(Node *p_node, Array &r_candidates) const;
	// Children scan for the node-children source: collects usable Node2D
	// children of p_parent (whole subtree when recursive), honoring
	// homing_filter_group. Never this spawner or anything under it.
	void collect_homing_candidates_from_children(Node *p_parent, bool recursive, Array &r_candidates) const;
	// Warn-once latch helper for empty resolutions (const: flips the
	// mutable latch). Quiet passes never warn. The latch re-arms only
	// when the homing target configuration changes
	// (clear_empty_homing_targets_warning() from those setters), so a
	// group that empties during play warns at most once.
	void warn_empty_homing_targets_once(const String &message, bool quiet) const;
	void clear_empty_homing_targets_warning() const;
	// Pushes the steering block (smoothing, update interval, reached
	// distance, texture control, auto-pop flags, per-bullet smoothing
	// fan) onto a volley. Shared by volley setup and retarget passes so
	// runtime tuning reaches flying volleys instead of only new ones.
	void apply_steering_to_volley(BulletVolley2D *volley) const;

	// Enables or refreshes orbiting on a volley: bullets whose orbit is
	// not yet enabled get enabled, the rest get radius/direction/texture
	// updated in place (re-enabling would warn and keep stale values).
	// Skipped entirely for DontMove.
	void apply_orbiting_to_volley(BulletVolley2D *volley) const;
	// Applies the homing + orbiting configuration to a freshly spawned
	// (or pool-reused) volley: steering props, resolved targets (pushed
	// before orbiting so rings can lock immediately), orbiting, signal
	// hookup, and live-volley tracking. Called from shoot_once() before
	// volley_fired so handlers observe fully configured bullets.
	// `pre_resolved`: targets the fire-arc gate already approved (one
	// resolution per shot: the volley chases exactly what was approved).
	void apply_volley_homing_and_orbiting(BulletVolley2D *bullets, const Array *pre_resolved = nullptr);
	// Orbiting without homing warns once per configuration (re-armed by
	// on_config_changed()).
	bool orbit_without_homing_warned = false;

	// Arms a freshly fired volley with graze_zones (shoot_once, before the
	// homing signals). Runs no user code.
	void apply_volley_graze(BulletVolley2D *volley);

	// The Flocking settings shared with every volley this spawner armed
	// (created on first arm). Setters push into it (serial bump), so edits
	// reach bullets in flight; orphans keep it.
	std::shared_ptr<FlockConfig2D> flock_shared;
	void apply_flock_config();
	// `flock`, or the factory's default flock while the slot is empty.
	Ref<BulletFlock2D> effective_flock() const;
	// Joins a freshly fired volley to flock (shoot_once, before the homing
	// signals). Runs no user code.
	void apply_volley_flock(BulletVolley2D *volley);
	// The Splitting settings shared with every volley this spawner armed
	// (created on first arm); setters push into it.
	std::shared_ptr<SplitConfig2D> split_shared;
	void apply_split_config();
	// Emitter Bullets: the config shared with every volley armed by this
	// spawner, the default timing, the cycle latch (one error per config).
	std::shared_ptr<EmitterConfig2D> emitter_shared;
	mutable Ref<BulletEmitterData2D> default_emitter;
	bool emitter_cycle_reported = false;
	// True when the template chain (each template's own emitter template, up
	// to 16 steps) leads back to this spawner.
	bool emitter_leads_back() const;
	// Copies the settings into the shared config (template re-resolved).
	void apply_emitter_config();
	// Arms a freshly fired volley (or a live one) with emitter_shared.
	void apply_volley_emitter(BulletVolley2D *volley);
	void emitter_live_volleys(std::unordered_set<uint64_t> &r_changed);
	// Arms a freshly fired volley with split_data; its children copy
	// spawn_data (the shot's data). Runs no user code.
	void apply_volley_split(BulletVolley2D *volley, const Ref<BulletVolleyData2D> &spawn_data);

	// Arms a freshly fired volley with sound_effects (shoot_once, beside the
	// graze arming). Runs no user code.
	void apply_volley_sound(BulletVolley2D *volley);

	// The chain's `changed` (its stages relay theirs): the warnings and the
	// auto-fire error latch follow chain edits like any config setter.
	void connect_split_data(bool connect);
	void _on_split_data_changed();

	// Candidate count of the last round-robin resolution (to advance the
	// cursor by what a reused resolution actually took).
	mutable int homing_round_robin_last_count = 0;

	// Fire cone check: true when any resolved target sits within half
	// the fire arc of the spawner's facing. 0 arc = omnidirectional.
	bool fire_arc_covers_targets(const Array &targets) const;
};
} // namespace BlastBullets2D

BLAST_VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::OrphanedVolleys);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::HomingMode);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::HomingTargetSource);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::HomingTargetSelection);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::HomingRetargetMode);
BLAST_VARIANT_ENUM_CAST(BlastBullets2D::BulletSpawner2D::BulletColorMode);
