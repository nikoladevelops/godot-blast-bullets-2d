// BulletSpawner2D: what a PatternSpawner2D shot fires when it fires bullets.
// The spawn data, the shot hooks (shot_preflight / shot_prepare / fire_shot),
// the live-bullet budget, the graze and sound arming of each volley and its
// re-arming in flight, the speed roll, pickups and the lifecycle hooks
// (retarget clock, orphan policy). Homing and orbiting live in
// bullet_spawner2d_homing.cpp, flocking and splitting in their own files.

#include "bullet_spawner/pattern_spawner2d_internal.hpp"
#include "core/emit_on2d.hpp"
#include "core/live_object2d.hpp"
#include "core/transform_math2d.hpp"

using namespace godot;

namespace BlastBullets2D {

// Re-resolves a volley by the id captured before user code ran and verifies
// it is still the same live instance owned by the given spawner. Used after
// EVERY synchronous user-code emission on the shoot path (pre_shoot and the
// configuring signals): a handler may have freed the volley (queue_free or a
// factory free/reset) or handed it to another spawner (adopt_live_volley).
// Returns the live volley, or nullptr when the shot must be dropped (no
// count, no volley_fired). Compare BY VALUE: the raw pointer must never be
// trusted after user code ran.
static BulletVolley2D *revalidate_configured_volley(uint64_t volley_id, BulletVolley2D *expected, uint64_t self_id) {
	Object *live = UtilityFunctions::is_instance_id_valid(volley_id) ? ObjectDB::get_instance(ObjectID(volley_id)) : nullptr;
	BulletVolley2D *live_volley = Object::cast_to<BulletVolley2D>(live);
	// is_queued_for_deletion matters: queue_free() is the sanctioned way to
	// kill a volley from a handler, and it leaves every other check passing
	// (same pointer, active, owned) until the end-of-frame flush. Configuring
	// or counting a dying volley would emit volley_fired for a volley that
	// never lives a tick, so a queued-for-deletion volley drops like a freed
	// one (the user's free still happens at flush).
	if (live_volley == nullptr || live_volley != expected || live_volley->is_queued_for_deletion() || !live_volley->is_active || live_volley->owner_spawner_id != self_id) {
		return nullptr;
	}
	return live_volley;
}

// Targets resolved before user code ran (the fire-arc gate): keeps the
// points and the nodes that still pass THE target filter. Each node is read
// back by id, never through the Variant's pointer (it may hold a freed node).
static void keep_live_targets2d(Array &r_targets, const World2D *world) {
	Array live;
	for (int i = 0; i < r_targets.size(); ++i) {
		const Variant &target = r_targets[i];
		if (target.get_type() != Variant::OBJECT) {
			live.push_back(target);
			continue;
		}
		Node2D *node = live_object2d<Node2D>(target);
		Vector2 position;
		if (node != nullptr && target_node_usable2d(node, world, position)) {
			live.push_back(node);
		}
	}
	if (live.size() != r_targets.size()) {
		r_targets = live;
	}
}

Ref<BulletVolleyData2D> BulletSpawner2D::get_spawn_data() const {
	return spawn_data;
}

void BulletSpawner2D::set_spawn_data(const Ref<BulletVolleyData2D> &new_data) {
	on_config_changed();
	if (new_data == spawn_data) {
		return; // same resource: keep the duplicate template and connection
	}
	const Callable on_changed = callable_mp(this, &BulletSpawner2D::_on_spawn_data_changed);
	if (spawn_data.is_valid() && spawn_data->is_connected("changed", on_changed)) {
		spawn_data->disconnect("changed", on_changed);
	}
	spawn_data = new_data;
	// A new resource invalidates the duplicate cache (see header).
	cached_volley_template.unref();
	cached_spawn_data_id = 0;
	if (spawn_data.is_valid()) {
		spawn_data->connect("changed", on_changed);
	}
	// The preview's collision-ring overlay reads spawn_data's shape. Not a
	// geometry change (no pattern_version bump): rings only.
	if (preview_draw_collision_rings) {
		rebuild_preview();
	}
}

void BulletSpawner2D::_on_spawn_data_changed() {
	// In-place edit of the user's resource: drop the cached duplicate so the
	// next shoot_once() re-duplicates fresh data instead of stale template.
	cached_volley_template.unref();
	cached_spawn_data_id = 0;
	if (preview_draw_collision_rings) {
		rebuild_preview();
	}
}

int BulletSpawner2D::get_max_live_bullets() const {
	return max_live_bullets;
}

void BulletSpawner2D::set_max_live_bullets(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("BulletSpawner2D: max_live_bullets must be >= 0 (0 = unlimited), keeping the old value.");
		return;
	}
	max_live_bullets = value;
}

int BulletSpawner2D::get_active_live_bullet_count() const {
	BulletFactory2D *factory = get_bullet_factory();
	if (factory == nullptr) {
		return 0;
	}
	// Count-all census: attributed by factory-side ownership, so plain
	// (non-homing, untracked) volleys count toward max_live_bullets exactly
	// like tracked homing volleys. (The tracked list stays homing-only for
	// retargeting; the budget must not be blind to plain volleys.)
	return factory->count_active_bullets_owned_by(get_instance_id());
}

void BulletSpawner2D::arm_retarget_countdown() {
	if (homing_retarget_phase > 0.0) {
		homing_retarget_time_left = homing_retarget_phase;
	} else if (homing_retarget_interval_sec > 0.0) {
		homing_retarget_time_left = Math::fposmod((double)get_instance_id() * 0.137, homing_retarget_interval_sec);
	} else {
		homing_retarget_time_left = 0.0;
	}
}

void BulletSpawner2D::apply_volley_graze(BulletVolley2D *volley) {
	if (volley == nullptr || !graze_enabled || graze_zones.is_empty()) {
		return;
	}
	bool any = false;
	const Array zones = spawner_graze_zones(any);
	if (any) {
		// Shared: setting edits and refresh_graze_targets() reach it in flight.
		volley->graze_arm_from_spawner(zones, graze_detector_shared());
	}
}

void BulletSpawner2D::apply_volley_sound(BulletVolley2D *volley) {
	if (volley == nullptr || !sound_enabled || sound_effects.is_empty()) {
		return;
	}
	bool any = false;
	const Array entries = spawner_sound_entries(any);
	if (any) {
		// Shared: listener edits and refresh_sound_listeners() reach it in
		// flight.
		std::shared_ptr<GrazeDetector2D> detector = sound_detector_ref();
		volley->sound_arm_from_spawner(entries, detector, (float)sound_volume_db);
	}
}

Array BulletSpawner2D::convert_live_bullets_to_pickups(const Ref<BulletPickupData2D> &pickup, const Variant &target) {
	BulletFactory2D *factory = get_bullet_factory();
	if (factory == nullptr) {
		UtilityFunctions::push_error("BulletSpawner2D.convert_live_bullets_to_pickups: the spawner has no BulletFactory2D, nothing converted.");
		return Array();
	}
	BulletFactory2D::PickupSelection selection;
	selection.kind = BulletFactory2D::PickupSelection::OWNER;
	selection.owner_spawner_id = get_instance_id();
	return factory->convert_to_pickups_impl("BulletSpawner2D.convert_live_bullets_to_pickups", pickup, target, selection);
}

void BulletSpawner2D::roll_speed_factors(size_t count) {
	shot_speed_factors.clear();
	if (bullet_speed_scale_min == 1.0 && bullet_speed_scale_max == 1.0) {
		return;
	}
	shot_speed_factors.resize(count);
	for (size_t i = 0; i < count; ++i) {
		shot_speed_factors[i] = (real_t)lerp_range(bullet_speed_scale_min, bullet_speed_scale_max, roll_unit(CHANNEL_SPEED, (uint64_t)i));
	}
}

ShotRefusal2D BulletSpawner2D::shot_preflight(BulletFactory2D *factory) {
	if (spawn_data.is_null()) {
		return ShotRefusal2D::refuse("BulletSpawner2D::shoot_once: no spawn_data assigned.", StringName("no_spawn_data"), true);
	}
	// Soft live-bullet fuse: pause firing (not erroring) while the cap holds.
	// Checked before any pattern work, so a stalled spawner costs nothing.
	if (max_live_bullets > 0 && get_active_live_bullet_count() >= max_live_bullets) {
		return ShotRefusal2D::refuse(String(), StringName("over_budget"), true);
	}
	return ShotRefusal2D();
}

ShotRefusal2D BulletSpawner2D::shot_prepare() {
	// A spawn reads the data once (the transforms ride the span) and never
	// writes it, so the user's resource goes straight to the factory. Only a
	// split chain keeps the data for later (the children of every split): a
	// splitting shot arms it with a spawner-owned copy, so an edit made after
	// the shot never reaches bullets already flying. The copy is cached until
	// an in-place edit (_on_spawn_data_changed) or a swap (set_spawn_data):
	// duplicating cost ~30 us per shot after every edit.
	if (!split_enabled || split_data.is_null()) {
		shot_volley_data = spawn_data;
		return ShotRefusal2D();
	}
	Ref<BulletVolleyData2D> volley_data;
	const uint64_t spawn_id = spawn_data->get_instance_id();
	if (cached_volley_template.is_valid() && cached_spawn_data_id == spawn_id) {
		volley_data = cached_volley_template;
	} else {
		volley_data = Ref<BulletVolleyData2D>(Object::cast_to<BulletVolleyData2D>(spawn_data->duplicate().ptr()));
		if (volley_data.is_null()) {
			return ShotRefusal2D::refuse("BulletSpawner2D::shoot_once: could not duplicate spawn_data.", StringName("no_spawn_data"), true);
		}
		cached_volley_template = volley_data;
		cached_spawn_data_id = spawn_id;
	}
	shot_volley_data = volley_data;
	return ShotRefusal2D();
}

bool BulletSpawner2D::speed_roll_active() const {
	return bullet_speed_scale_min != 1.0 || bullet_speed_scale_max != 1.0;
}

ShotResult2D BulletSpawner2D::fire_shot(std::vector<Transform2D> &transforms) {
	ShotResult2D result;
	auto refuse_shot = [&result](const String &message, const StringName &reason, bool with_signal) -> ShotResult2D {
		result.refusal = ShotRefusal2D::refuse(message, reason, with_signal);
		return result;
	};
	const Ref<BulletVolleyData2D> volley_data = shot_volley_data;
	BulletFactory2D *factory = nullptr;
	// The per-bullet speed roll, after the placement rolls (same draw order).
	if (speed_roll_active()) {
		roll_speed_factors(transforms.size());
	} else if (!shot_speed_factors.empty()) {
		shot_speed_factors.clear();
	}
	if (life_time_roll_active()) {
		roll_life_time_factors(transforms.size());
	} else if (!shot_life_factors.empty()) {
		shot_life_factors.clear();
	}
	// Bullet Colors: one per bullet, carried to the factory with the transforms.
	roll_shot_colors(transforms.size());
	// Fire-cone gate (homing only): no target inside half the fire arc means
	// this volley would chase nothing it can see, so the shot is skipped
	// BEFORE spawning (never a half-built volley to tear down). Empty
	// resolutions still fire plain volleys (same as missing-target homing);
	// the mouse source resolves nothing, so the cone does not apply to it.
	// The gate resolves once without consuming round-robin or extra random
	// draws; a passing shot reuses exactly these targets for the volley.
	Array arc_targets;
	bool arc_resolved = false;
	if (homing_enabled && homing_target_source != HOMING_SOURCE_MOUSE && Math::is_finite(homing_fire_arc_deg) && homing_fire_arc_deg > 0.0) {
		arc_targets = resolve_homing_targets(false, false); // warns once per configuration
		arc_resolved = true;
		if (!arc_targets.is_empty() && !fire_arc_covers_targets(arc_targets)) {
			return refuse_shot(String(), StringName("outside_fire_arc"), true);
		}
		if (homing_target_selection == HOMING_SELECT_ROUND_ROBIN && homing_round_robin_last_count > 0) {
			homing_round_robin_cursor = (homing_round_robin_cursor + (int)arc_targets.size()) % homing_round_robin_last_count;
		}
	}
	// Configure-then-attach: the spawner id is pre-stamped inside the factory
	// spawn (before physics space, shapes, tree entry, and activation), so the
	// volley is never observable as factory-owned. The re-stamp below is kept
	// as belt-and-braces for any path that could not carry the id through.
	// Moving spawners can hand their velocity to the volley (bullets keep the
	// turret's momentum). Zero unless inherit_movement_velocity is on.
	// A shoot_from shot inherits what its caller passed (an emitter: its
	// bullet's velocity), never the spawner's own motion.
	Vector2 inherited_velocity = shot_pose_active ? shot_pose_velocity : Vector2();
	if (!shot_pose_active && inherit_movement_velocity && movement_enabled) {
		inherited_velocity = movement_velocity * (real_t)movement_velocity_inherit_factor;
		if (!inherited_velocity.is_finite()) {
			inherited_velocity = Vector2();
		}
	}
	// The collect ran the aimed target's get_velocity (user code), which
	// may have freed the factory: look it up again.
	factory = get_bullet_factory();
	if (factory == nullptr) {
		return refuse_shot("BulletSpawner2D::shoot_once: no BulletFactory2D assigned (bullet_factory_path).", StringName("no_factory"), true);
	}
	BulletVolley2D *bullets = factory->spawn_volley_span(volley_data, transforms.data(), (int)transforms.size(), inherited_velocity, get_instance_id(), shot_colors.empty() ? nullptr : shot_colors.data());
	if (bullets == nullptr) {
		// The factory already reported why (busy, teardown, bad data).
		return refuse_shot(String(), StringName("factory_refused"), !refused_free_during_shot);
	}
	if (refused_free_during_shot) {
		// An attachment callback of the new volley freed this spawner (the
		// free waits for the latch, see refused_free_during_shot): the dying
		// spawner configures nothing and reports nothing. Its PREDELETE
		// applies orphaned_volleys to the volley like to every other.
		return refuse_shot(String(), StringName("dropped"), false);
	}
	// Tag the instance (fresh or pooled): from here on its area_entered,
	// body_entered and life_time_over signals are possessed by this spawner
	// instead of the factory. Pool reuse resets the tag, so this stamp covers
	// every spawn path through this function. Already stamped pre-activation
	// by the factory call above; re-assert here in case that ever changes.
	bullets->owner_spawner_id = get_instance_id();
	if (!shot_speed_factors.empty()) {
		bullets->apply_speed_factors(shot_speed_factors.data(), (int)shot_speed_factors.size());
	}
	if (!shot_life_factors.empty()) {
		bullets->apply_life_time_factors(shot_life_factors.data(), (int)shot_life_factors.size());
	}
	// Capture the id BEFORE user code runs: the configuring signals below
	// execute handlers synchronously, and a handler may free or re-home this
	// volley. The raw pointer must not be touched again without validation.
	// (Latch/depth were already taken at entry; no re-take here.)
	const uint64_t volley_id = bullets->get_instance_id();
	const uint64_t self_id = get_instance_id();
	// Graze, sound, flock and split arm BEFORE the first signal (no user code
	// runs here): every handler, pre_shoot included, sees an armed volley,
	// and what a pre_shoot handler arms instead sticks.
	apply_volley_graze(bullets);
	apply_volley_sound(bullets);
	apply_volley_flock(bullets);
	apply_volley_split(bullets, volley_data);
	apply_volley_emitter(bullets);
	// Last-chance hook on the freshly spawned, stamped volley (before the
	// muzzle offset, homing and orbit are configured): handlers may tweak the
	// live instance (speeds, custom data) or drop it via queue_free().
	emit_on2d(this, "pre_shoot", bullets, volleys_fired + 1);
	// G1: a pre_shoot handler may have freed this volley (factory reset/free_*,
	// queue_free) or handed it to another owner (adopt_live_volley). Feeding a
	// dead/foreign pointer into the configuring step below would be
	// use-after-free (or silently overwrite the new owner's steering), so
	// revalidate BEFORE configuring, not just before volley_fired.
	if (shot_owner_dying()) {
		// A pre_shoot handler freed this spawner: the free was refused
		// (emit guard) and finishes at the end of the frame. A dying spawner
		// configures nothing and reports nothing.
		return refuse_shot(String(), StringName("dropped"), false);
	}
	if (revalidate_configured_volley(volley_id, bullets, self_id) == nullptr) {
		// Same drop semantics as below: the spawn succeeded but this shot is
		// dropped (no count, no volley_fired).
		return refuse_shot(String(), StringName("dropped"), true);
	}
	// Homing/orbiting runs on the same stamp: the instance is fully
	// configured before volley_fired, so handlers observe live behavior.
	// The latch stays up through volley_fired below (not just the configuring
	// signals): every emit on this path runs user code synchronously.
	// Muzzle offset first (every volley, even plain ones): runs through the
	// engine teleport path so shapes, attachments, and interpolation stay
	// consistent, and before homing/orbit seeding so locks form at the final
	// positions instead of re-converging on the first tick.
	if (spawn_position_offset != Vector2(0, 0)) {
		bullets->teleport_shift_all_bullets(resolve_spawn_offset_global());
	}
	if (arc_resolved) {
		// The gate's targets crossed the spawn's attachment callbacks and
		// pre_shoot: a handler may have freed one of them.
		keep_live_targets2d(arc_targets, target_world());
	}
	apply_volley_homing_and_orbiting(bullets, arc_resolved ? &arc_targets : nullptr);
	// Re-validate: handlers of homing_targets_resolved/volley_homing_configured
	// ran above and may have freed this volley or handed it to another owner.
	// Dereferencing the raw pointer now would be use-after-free: resolve by id
	// and compare BY VALUE.
	if (shot_owner_dying()) {
		return refuse_shot(String(), StringName("dropped"), false); // freed by a homing handler (see above)
	}
	if (revalidate_configured_volley(volley_id, bullets, self_id) == nullptr) {
		// Volley is gone or foreign: counting it or emitting volley_fired for it
		// would lie about ownership and hand out a dead pointer. The spawn
		// itself succeeded (bullets were created), but this shot is dropped.
		return refuse_shot(String(), StringName("dropped"), true);
	}
	volleys_fired += 1;
	// The latch stays up through this emit AND the cap transition below: a
	// volley_fired (or shooting_finished) handler calling shoot_once() (same
	// or cross spawner) would otherwise recurse without bound past the
	// max_volleys cap. Single clear-point right after, so sequential
	// (non-nested) shots are unaffected. All pre-emit failure paths above
	// return while the guard is either unset or already cleared.
	// Read before the emit: a volley_fired handler may free the volley.
	const int fired_amount = bullets->get_amount_bullets();
	const uint32_t cadence = cadence_serial;
	emit_on2d(this, "volley_fired", bullets, volleys_fired);
	// A handler that reset the wave or armed a new budget owns the cadence:
	// this shot neither spends that budget nor reports a stale cap.
	result.cadence_replaced = cadence != cadence_serial;
	result.amount = fired_amount;
	return result;
}

void BulletSpawner2D::set_bullet_life_time_scale_min(double value) {
	if (!is_finite_real2d(value) || !(value > 0.0)) {
		UtilityFunctions::push_error("BulletSpawner2D: bullet_life_time_scale_min must be finite and > 0, keeping the old value.");
		return;
	}
	bullet_life_time_scale_min = value;
}

void BulletSpawner2D::set_bullet_life_time_scale_max(double value) {
	if (!is_finite_real2d(value) || !(value > 0.0)) {
		UtilityFunctions::push_error("BulletSpawner2D: bullet_life_time_scale_max must be finite and > 0, keeping the old value.");
		return;
	}
	bullet_life_time_scale_max = value;
}

void BulletSpawner2D::roll_life_time_factors(size_t count) {
	shot_life_factors.resize(count);
	for (size_t i = 0; i < count; ++i) {
		shot_life_factors[i] = (real_t)lerp_range(bullet_life_time_scale_min, bullet_life_time_scale_max, roll_unit(CHANNEL_LIFE_TIME, (uint64_t)i));
	}
}

void BulletSpawner2D::set_bullet_speed_scale_min(double value) {
	if (!Math::is_finite(value) || !(value >= 0.0)) {
		UtilityFunctions::push_error("BulletSpawner2D: bullet_speed_scale_min must be finite and >= 0, keeping the old value.");
		return;
	}
	bullet_speed_scale_min = value;
}

void BulletSpawner2D::set_bullet_speed_scale_max(double value) {
	if (!Math::is_finite(value) || !(value >= 0.0)) {
		UtilityFunctions::push_error("BulletSpawner2D: bullet_speed_scale_max must be finite and >= 0, keeping the old value.");
		return;
	}
	bullet_speed_scale_max = value;
}

// ---- PatternSpawner2D hooks ---------------------------------------------------

bool BulletSpawner2D::needs_process_owned() const {
	return homing_retarget_active();
}

bool BulletSpawner2D::process_owned(double delta) {
	// Interval retargeting runs independently of shooting too: manual
	// shoot_once() volleys keep chasing fresh targets while auto-shooting
	// stays off.
	if (homing_retarget_active()) {
		homing_retarget_time_left -= delta;
		if (homing_retarget_time_left <= 0.0) {
			const int retargeted = retarget_live_volleys();
			homing_retarget_time_left = homing_retarget_interval_sec;
			if (retargeted > 0) {
				emit_on2d(this, "retarget_applied", retargeted);
				if (shot_owner_dying()) {
					return false; // its handler freed this spawner
				}
			}
		}
	}
	return true;
}

void BulletSpawner2D::restart_owned_clocks() {
	// Retarget stagger (volley-time resolution already armed each shot, so a
	// delayed first pass is benign).
	arm_retarget_countdown();
}

void BulletSpawner2D::on_predelete_owned(BulletFactory2D *factory) {
	// Freed for good: the orphaned_volleys policy for every volley this
	// spawner still owns.
	const String path = is_inside_tree() ? String(get_path()) : String(get_name());
	factory->apply_orphan_policy(get_instance_id(), orphaned_volleys, path);
}

void BulletSpawner2D::on_exit_tree_owned() {
	// Homing scratch Arrays can retain references to freed scene nodes
	// (candidates/scan results): drop them.
	homing_candidates_scratch.clear();
	homing_pool_scratch.clear();
	homing_scan_stack.clear();
}

void BulletSpawner2D::on_config_changed_owned() {
	orbit_without_homing_warned = false;
}

void BulletSpawner2D::shift_owned_shots(const Vector2 &jump) {
	BulletFactory2D *factory = get_bullet_factory();
	if (factory == nullptr) {
		return;
	}
	std::vector<BulletVolley2D *> owned;
	factory->collect_volleys_owned_by(get_instance_id(), owned);
	// Ids first: a shift moves attachments, whose node callbacks are user code.
	std::vector<uint64_t> ids;
	ids.reserve(owned.size());
	for (BulletVolley2D *volley : owned) {
		ids.push_back(volley->get_instance_id());
	}
	const uint64_t self_id = get_instance_id();
	for (size_t k = 0; k < owned.size(); ++k) {
		BulletVolley2D *volley = owned[k];
		if (ObjectDB::get_instance(ObjectID(ids[k])) != volley || volley->is_queued_for_deletion() || volley->owner_spawner_id != self_id || volley->is_pooled()) {
			continue;
		}
		volley->teleport_shift_all_bullets(jump);
	}
}

void BulletSpawner2D::append_payload_warnings(PackedStringArray &out) const {
	if (spawn_data.is_null()) {
		out.push_back("No spawn_data: assign a BulletVolleyData2D (speed, art, collision) to fire.");
	} else if (spawn_data->sprite_frames.is_null() && spawn_data->mesh.is_null()) {
		out.push_back("spawn_data has no sprite_frames (or mesh): bullets will be invisible.");
	}
}

void BulletSpawner2D::append_setup_warnings(PackedStringArray &out) const {
	if (orbiting_enabled && !homing_enabled) {
		out.push_back("orbiting_enabled needs homing_enabled (orbiting locks onto a homing target).");
	}
	if (homing_enabled) {
		if (homing_target_source == HOMING_SOURCE_NODE_GROUP && homing_node_group.is_empty()) {
			out.push_back("homing_target_source is Node Group but homing_node_group is empty: volleys fly without homing.");
		} else if (homing_target_source == HOMING_SOURCE_NODE_NAME && homing_node_name.is_empty()) {
			out.push_back("homing_target_source is Node Name but homing_node_name is empty: volleys fly without homing.");
		} else if (homing_target_source == HOMING_SOURCE_NODE_PATH && homing_target_path.is_empty()) {
			out.push_back("homing_target_source is Node Path but homing_target_path is empty: volleys fly without homing.");
		} else if (homing_target_source == HOMING_SOURCE_NODE_CHILDREN && homing_children_parent_path.is_empty()) {
			out.push_back("homing_target_source is Node Children but homing_children_parent_path is empty: volleys fly without homing.");
		}
	}
	if (flock_enabled) {
		// The spawner steers its volleys with ITS homing_smoothing (the spawn
		// data's never reaches them); rotation data or a movement pattern
		// make homing aim the direction every tick.
		if (homing_enabled && (homing_smoothing <= 0.0 || (spawn_data.is_valid() && (spawn_data->shared_bullet_rotation_data.is_valid() || !spawn_data->all_bullet_rotation_data.is_empty() || !spawn_data->shared_movement_pattern_path.is_empty() || !spawn_data->all_bullet_movement_pattern_paths.is_empty())))) {
			out.push_back("Flocking with homing that re-aims every tick (homing_smoothing 0, rotation data or a movement pattern): the flock can only nudge the bullets. Give homing a turn rate (homing_smoothing > 0, no rotation data or pattern) so the swarm flocks on its way.");
		}
	}
	if (split_enabled) {
		if (split_data.is_null()) {
			out.push_back("split_enabled is on but split_data is empty: nothing splits. Assign a BulletSplitData2D.");
		} else if (split_data->stages.is_empty()) {
			out.push_back("split_data has no stages: nothing splits. Add a BulletSplitStage2D per generation.");
		} else {
			// How many bullets one fired bullet can become over the chain:
			// named (never capped) when it outgrows five full patterns.
			double per_parent = 1.0;
			double total = 0.0;
			const int generations = split_data->get_generation_count();
			for (int g = 0; g < generations; ++g) {
				const Ref<BulletSplitStage2D> stage = split_data->get_stage_for_depth(g);
				if (stage.is_null()) {
					if (g < split_data->stages.size()) {
						out.push_back("split_data.stages[" + String::num_int64(g) + "] is empty: generation " + String::num_int64(g) + " and every later one never split.");
					}
					break;
				}
				// A parent that survives its splits may split up to
				// max_splits_per_bullet times.
				const double splits = stage->parent_survives ? (double)Math::max(1, stage->max_splits_per_bullet) : 1.0;
				per_parent *= (double)stage->count * splits;
				total += per_parent;
				if (stage->parent_survives && stage->max_splits_per_bullet == 0 && (stage->split_on_bounce || stage->split_on_non_lethal_hit || stage->split_on_homing_reached || stage->split_on_graze)) {
					out.push_back("split_data.stages[" + String::num_int64(g) + "] keeps the parent and has no max_splits_per_bullet: a bullet splits again on every bounce, surviving hit, homing reach or graze.");
				}
			}
			if (total > 5.0 * (double)pattern_max_bullets()) {
				const String amount = total > 1e12 ? String("more than 1000000000000") : String::num_int64((int64_t)total);
				out.push_back("split_data can turn one fired bullet into " + amount + " bullets over its generations (more than five full patterns): expect heavy frames. Lower the stage counts or max_depth, or spread the spawns with BulletFactory2D.split_max_children_per_sweep.");
			}
		}
	}
	if (emitter_enabled) {
		const PatternSpawner2D *tpl = get_emitter_template();
		if (emitter_spawner_path.is_empty()) {
			out.push_back("emitter_enabled is on but emitter_spawner_path is empty: the bullets have no pattern to fire. Point it at a BulletSpawner2D (or LaserSpawner2D) with its shooting off.");
		} else if (tpl == nullptr) {
			if (is_inside_tree()) {
				out.push_back("emitter_spawner_path does not point at a BulletSpawner2D or LaserSpawner2D: the bullets have no pattern to fire.");
			}
		} else if (emitter_leads_back()) {
			out.push_back("emitter_spawner_path leads back to this spawner (an emitter cycle): its bullets do not emit.");
		} else if (tpl->get_shooting_enabled()) {
			out.push_back("The emitter template " + String(tpl->get_name()) + " also shoots on its own: turn its shooting off if it should only fire from the bullets.");
		}
	}
}

real_t BulletSpawner2D::preview_collision_ring_radius() const {
	if (spawn_data.is_null() || spawn_data->collision_shape.is_null()) {
		return 0.0;
	}
	float ring_r = 0.0f;
	if (const CircleShape2D *c = Object::cast_to<CircleShape2D>(spawn_data->collision_shape.ptr())) {
		const float r = c->get_radius();
		if (r > 0.0f && Math::is_finite(r)) {
			ring_r = r;
		}
	} else if (const RectangleShape2D *rc = Object::cast_to<RectangleShape2D>(spawn_data->collision_shape.ptr())) {
		const Vector2 s = rc->get_size();
		if (s.x > 0.0f && s.y > 0.0f && s.is_finite()) {
			ring_r = 0.5f * (float)Math::min(s.x, s.y);
		}
	} else if (const CapsuleShape2D *cap = Object::cast_to<CapsuleShape2D>(spawn_data->collision_shape.ptr())) {
		const float h = cap->get_height();
		if (h > 0.0f && Math::is_finite(h)) {
			ring_r = 0.5f * h;
		}
	}
	return ring_r;
}

bool BulletSpawner2D::shots_ignore_despawn_bounds() const {
	// Bullets that ignore the bounds are never culled, so there is nothing to
	// protect them from.
	return spawn_data.is_valid() && spawn_data->get_ignore_despawn_bounds();
}

bool BulletSpawner2D::apply_payload_override(const Variant &value) {
	Ref<BulletVolleyData2D> override_data = value;
	if (override_data.is_null()) {
		return false;
	}
	// Through the setter, not a raw member write: the setter swaps the
	// "changed" connection to the override resource and invalidates the
	// duplicate cache. A raw assignment left the OLD resource still
	// connected (so its edits kept invalidating our cache) and the
	// override with NO connection (so its runtime edits were invisible).
	set_spawn_data(override_data);
	return true;
}

Dictionary BulletSpawner2D::debug_get_pattern_cache_info() const {
	Dictionary d = PatternSpawner2D::debug_get_pattern_cache_info();
	// Spawn-data duplicate cache (shoot_once reuses one spawner-owned copy;
	// resource swaps and in-place edits invalidate it).
	d["template_valid"] = cached_volley_template.is_valid();
	const uint64_t live_id = spawn_data.is_valid() ? spawn_data->get_instance_id() : 0;
	d["spawn_id_match"] = cached_volley_template.is_valid() && cached_spawn_data_id == live_id && live_id != 0;
	return d;
}

} // namespace BlastBullets2D
