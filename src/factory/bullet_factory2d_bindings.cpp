// Godot bindings of BulletFactory2D's own API (spawning, pools, structural calls,
// debugger, stats, signals). The static pattern helpers are bound in
// bullet_factory2d_patterns_bindings.cpp.

#include "factory/bullet_factory2d_internal.hpp"
#include "laser/laser_volley2d.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletFactory2D::reactivate_volley_for_script(const Variant &volley) {
	BulletVolley2D *bullet_multi = nullptr;
	if (!live_argument2d<BulletVolley2D>(volley, "reactivate_volley", "volley", "BulletVolley2D", bullet_multi)) {
		return;
	}
	if (bullet_multi == nullptr) {
		UtilityFunctions::push_error("track_volley_active: volley is null.");
		return;
	}
	track_volley_active(*bullet_multi);
}

LaserVolley2D *BulletFactory2D::spawn_laser_script(const Ref<LaserData2D> &data, const TypedArray<Transform2D> &transforms, const Variant &anchor) {
	Node2D *node = nullptr;
	if (!live_argument2d<Node2D>(anchor, "spawn_laser", "anchor", "Node2D", node)) {
		return nullptr;
	}
	return spawn_laser(data, transforms, node);
}

void BulletFactory2D::free_volley_deferred_script(const Variant &volley) {
	Node *node = nullptr;
	if (live_argument2d<Node>(volley, "free_volley_deferred", "volley", "Node", node)) {
		free_volley_deferred(node);
	}
}

void BulletFactory2D::debug_free_like_release_script(const Variant &object) {
	Object *obj = nullptr;
	if (live_argument2d<Object>(object, "debug_free_like_release", "object", "Object", obj)) {
		debug_free_like_release(obj);
	}
}

Ref<VolleyPoolKey2D> BulletFactory2D::debug_get_pool_bucket_script(const Variant &volley) {
	BulletVolley2D *v = nullptr;
	if (!live_argument2d<BulletVolley2D>(volley, "debug_get_pool_bucket", "volley", "BulletVolley2D", v)) {
		return Ref<VolleyPoolKey2D>();
	}
	return debug_get_pool_bucket(v);
}

void BulletFactory2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_is_tearing_down"), &BulletFactory2D::get_is_tearing_down);

	ClassDB::bind_method(D_METHOD("reactivate_volley", "volley"), &BulletFactory2D::reactivate_volley_for_script);

	ClassDB::bind_method(D_METHOD("teleport_shift_all_bullets", "shift_amount"), &BulletFactory2D::teleport_shift_all_bullets);

	// Sprite effect layers (manual hatch + introspection). Volley-driven
	// triggers need no factory calls: spawn/hit/destroy/bounce events route
	// automatically from the volley when its data configures layers.
	ClassDB::bind_method(D_METHOD("spawn_layer_effect", "layer", "at"), &BulletFactory2D::spawn_layer_effect);
	ClassDB::bind_method(D_METHOD("clear_sprite_effects"), &BulletFactory2D::clear_sprite_effects);
	ClassDB::bind_method(D_METHOD("get_active_effect_count"), &BulletFactory2D::get_active_effect_count);
	ClassDB::bind_method(D_METHOD("debug_get_effect_state"), &BulletFactory2D::debug_get_effect_state);
	ClassDB::bind_method(D_METHOD("debug_get_frame_cache_stats"), &BulletFactory2D::debug_get_frame_cache_stats);
	ClassDB::bind_method(D_METHOD("debug_read_frame", "texture", "whiten"), &BulletFactory2D::debug_read_frame, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("debug_set_effect_log_enabled", "enabled"), &BulletFactory2D::debug_set_effect_log_enabled);
	ClassDB::bind_method(D_METHOD("debug_get_effect_log"), &BulletFactory2D::debug_get_effect_log);
	ClassDB::bind_method(D_METHOD("debug_clear_effect_log"), &BulletFactory2D::debug_clear_effect_log);
	ClassDB::bind_method(D_METHOD("debug_set_bullet_event_log_enabled", "enabled"), &BulletFactory2D::debug_set_bullet_event_log_enabled);
	ClassDB::bind_method(D_METHOD("debug_get_bullet_event_log"), &BulletFactory2D::debug_get_bullet_event_log);
	ClassDB::bind_method(D_METHOD("debug_clear_bullet_event_log"), &BulletFactory2D::debug_clear_bullet_event_log);
	ClassDB::bind_method(D_METHOD("debug_get_bullet_event_log_dropped"), &BulletFactory2D::debug_get_bullet_event_log_dropped);

	ClassDB::bind_method(D_METHOD("get_is_factory_busy"), &BulletFactory2D::get_is_factory_busy);

	ClassDB::bind_method(D_METHOD("get_physics_space"), &BulletFactory2D::get_physics_space);
	ClassDB::bind_method(D_METHOD("set_physics_space", "new_physics_space"), &BulletFactory2D::set_physics_space);

	ClassDB::bind_method(D_METHOD("get_is_debugger_enabled"), &BulletFactory2D::get_is_debugger_enabled);
	ClassDB::bind_method(D_METHOD("set_is_debugger_enabled", "new_is_enabled"), &BulletFactory2D::set_is_debugger_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_debugger_enabled"), "set_is_debugger_enabled", "get_is_debugger_enabled");

	ClassDB::bind_method(D_METHOD("get_is_factory_processing_bullets"), &BulletFactory2D::get_is_factory_processing_bullets);
	ClassDB::bind_method(D_METHOD("set_is_factory_processing_bullets", "is_processing_enabled"), &BulletFactory2D::set_is_factory_processing_bullets);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_factory_processing_bullets"), "set_is_factory_processing_bullets", "get_is_factory_processing_bullets");

	ClassDB::bind_method(D_METHOD("get_physics_interpolation"), &BulletFactory2D::get_physics_interpolation);
	ClassDB::bind_method(D_METHOD("set_physics_interpolation", "value"), &BulletFactory2D::set_physics_interpolation);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "physics_interpolation", PROPERTY_HINT_ENUM, "Off,On,Follow Project"), "set_physics_interpolation", "get_physics_interpolation");
	ClassDB::bind_method(D_METHOD("is_physics_interpolation_active"), &BulletFactory2D::is_physics_interpolation_active);
	BIND_ENUM_CONSTANT(INTERPOLATION_OFF);
	BIND_ENUM_CONSTANT(INTERPOLATION_ON);
	BIND_ENUM_CONSTANT(INTERPOLATION_FOLLOW_PROJECT);

	ClassDB::bind_method(D_METHOD("spawn_volley", "spawn_data", "inherited_velocity_offset", "spawner_id"), &BulletFactory2D::spawn_volley, DEFVAL(Vector2(0, 0)), DEFVAL(0));

	ClassDB::bind_method(D_METHOD("reset", "key"), &BulletFactory2D::reset, DEFVAL(Ref<VolleyPoolKey2D>()));

	ClassDB::bind_method(D_METHOD("get_debugger_color"), &BulletFactory2D::get_debugger_color);
	ClassDB::bind_method(D_METHOD("set_debugger_color", "new_color"), &BulletFactory2D::set_debugger_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "debugger_color"), "set_debugger_color", "get_debugger_color");
	ClassDB::bind_method(D_METHOD("get_debugger_z_index"), &BulletFactory2D::get_debugger_z_index);
	ClassDB::bind_method(D_METHOD("set_debugger_z_index", "value"), &BulletFactory2D::set_debugger_z_index);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "debugger_z_index", PROPERTY_HINT_RANGE, "-4096,4096,1"), "set_debugger_z_index", "get_debugger_z_index");

	ClassDB::bind_method(D_METHOD("populate_bullets_pool", "key", "spawn_data", "instance_count"), &BulletFactory2D::populate_bullets_pool);
	ClassDB::bind_method(D_METHOD("free_bullets_pool", "key"), &BulletFactory2D::free_bullets_pool, DEFVAL(Ref<VolleyPoolKey2D>()));

	ClassDB::bind_method(D_METHOD("populate_attachments_pool", "attachment_scene", "amount_attachments"), &BulletFactory2D::populate_attachments_pool);
	ClassDB::bind_method(D_METHOD("free_attachments_pool"), &BulletFactory2D::free_attachments_pool);
	ClassDB::bind_method(D_METHOD("free_attachments_pool_for_scene", "attachment_scene"), &BulletFactory2D::free_attachments_pool_for_scene);

	ClassDB::bind_method(D_METHOD("free_active_bullets", "key"), &BulletFactory2D::free_active_bullets, DEFVAL(Ref<VolleyPoolKey2D>()));
	ClassDB::bind_method(D_METHOD("clear_active_bullets", "key"), &BulletFactory2D::clear_active_bullets, DEFVAL(Ref<VolleyPoolKey2D>()));
	ClassDB::bind_method(D_METHOD("free_disabled_bullets", "key"), &BulletFactory2D::free_disabled_bullets, DEFVAL(Ref<VolleyPoolKey2D>()));

	// Deferred structural wrappers: safe from physics callbacks / sweeps.
	ClassDB::bind_method(D_METHOD("reset_deferred", "key"), &BulletFactory2D::reset_deferred, DEFVAL(Ref<VolleyPoolKey2D>()));
	ClassDB::bind_method(D_METHOD("_flush_structural_calls"), &BulletFactory2D::_flush_structural_calls);
	ClassDB::bind_method(D_METHOD("free_active_bullets_deferred", "key"), &BulletFactory2D::free_active_bullets_deferred, DEFVAL(Ref<VolleyPoolKey2D>()));
	ClassDB::bind_method(D_METHOD("clear_active_bullets_deferred", "key"), &BulletFactory2D::clear_active_bullets_deferred, DEFVAL(Ref<VolleyPoolKey2D>()));
	ClassDB::bind_method(D_METHOD("free_disabled_bullets_deferred", "key"), &BulletFactory2D::free_disabled_bullets_deferred, DEFVAL(Ref<VolleyPoolKey2D>()));
	ClassDB::bind_method(D_METHOD("free_bullets_pool_deferred", "key"), &BulletFactory2D::free_bullets_pool_deferred, DEFVAL(Ref<VolleyPoolKey2D>()));
	ClassDB::bind_method(D_METHOD("populate_bullets_pool_deferred", "key", "spawn_data", "instance_count"), &BulletFactory2D::populate_bullets_pool_deferred);
	ClassDB::bind_method(D_METHOD("free_attachments_pool_deferred"), &BulletFactory2D::free_attachments_pool_deferred);
	ClassDB::bind_method(D_METHOD("populate_attachments_pool_deferred", "attachment_scene", "amount_attachments"), &BulletFactory2D::populate_attachments_pool_deferred);
	ClassDB::bind_method(D_METHOD("free_attachments_pool_for_scene_deferred", "attachment_scene"), &BulletFactory2D::free_attachments_pool_for_scene_deferred);
	ClassDB::bind_method(D_METHOD("free_volley_deferred", "volley"), &BulletFactory2D::free_volley_deferred_script);
	ClassDB::bind_method(D_METHOD("ensure_factory_initialized"), &BulletFactory2D::ensure_factory_initialized);

	// Additional debug methods related

	ClassDB::bind_method(D_METHOD("debug_get_total_bullets_amount"), &BulletFactory2D::debug_get_total_bullets_amount);
	ClassDB::bind_method(D_METHOD("debug_get_active_volleys_amount"), &BulletFactory2D::debug_get_active_volleys_amount);
	ClassDB::bind_method(D_METHOD("debug_get_bullets_pool_amount"), &BulletFactory2D::debug_get_bullets_pool_amount);

	ClassDB::bind_method(
			D_METHOD("debug_get_bullets_pool_info"),
			&BulletFactory2D::debug_get_bullets_pool_info);

	ClassDB::bind_method(D_METHOD("debug_get_total_attachments_amount"), &BulletFactory2D::debug_get_total_attachments_amount);
	ClassDB::bind_method(D_METHOD("debug_get_active_attachments_amount"), &BulletFactory2D::debug_get_active_attachments_amount);
	ClassDB::bind_method(D_METHOD("debug_get_attachments_pool_amount"), &BulletFactory2D::debug_get_attachments_pool_amount);

	ClassDB::bind_method(
			D_METHOD("debug_get_attachments_pool_info"),
			&BulletFactory2D::debug_get_attachments_pool_info);
	ClassDB::bind_method(D_METHOD("debug_get_attachment_scene_memory_count"), &BulletFactory2D::debug_get_attachment_scene_memory_count);

	ClassDB::bind_method(D_METHOD("debug_get_factory_state"), &BulletFactory2D::debug_get_factory_state);
	ClassDB::bind_method(D_METHOD("debug_get_pool_hit_stats"), &BulletFactory2D::debug_get_pool_hit_stats);
	ClassDB::bind_method(D_METHOD("get_frame_stats"), &BulletFactory2D::get_frame_stats);
	ClassDB::bind_method(D_METHOD("reset_frame_stats"), &BulletFactory2D::reset_frame_stats);
	ClassDB::bind_method(D_METHOD("get_active_bullet_count"), &BulletFactory2D::get_active_bullet_count);
	ClassDB::bind_method(D_METHOD("set_register_performance_monitors", "value"), &BulletFactory2D::set_register_performance_monitors);
	ClassDB::bind_method(D_METHOD("get_register_performance_monitors"), &BulletFactory2D::get_register_performance_monitors);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "register_performance_monitors"), "set_register_performance_monitors", "get_register_performance_monitors");
	ClassDB::bind_method(D_METHOD("debug_reset_pool_stats"), &BulletFactory2D::debug_reset_pool_stats);
	ClassDB::bind_static_method("BulletFactory2D", D_METHOD("debug_validate_spawn_data", "spawn_data"), &BulletFactory2D::debug_validate_spawn_data);
	ClassDB::bind_static_method("BulletFactory2D", D_METHOD("debug_free_like_release", "object"), &BulletFactory2D::debug_free_like_release_script);
	ClassDB::bind_method(D_METHOD("debug_check_interpolation_status"), &BulletFactory2D::debug_check_interpolation_status);
	ClassDB::bind_method(D_METHOD("debug_get_live_volley_ids", "owner_spawner_id"), &BulletFactory2D::debug_get_live_volley_ids);
	ClassDB::bind_static_method("BulletFactory2D", D_METHOD("debug_expected_pool_key", "spawn_data"), &BulletFactory2D::debug_expected_pool_key);
	ClassDB::bind_method(D_METHOD("debug_assert_no_dangling"), &BulletFactory2D::debug_assert_no_dangling);
	ClassDB::bind_method(D_METHOD("debug_advance_time", "delta"), &BulletFactory2D::debug_advance_time);
	ClassDB::bind_method(D_METHOD("debug_get_pool_bucket", "volley"), &BulletFactory2D::debug_get_pool_bucket_script);

	ClassDB::bind_method(D_METHOD("get_debugger_max_providers"), &BulletFactory2D::get_debugger_max_providers);
	ClassDB::bind_method(D_METHOD("set_debugger_max_providers", "max_providers"), &BulletFactory2D::set_debugger_max_providers);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "debugger_max_providers", PROPERTY_HINT_RANGE, "0,10000,1"), "set_debugger_max_providers", "get_debugger_max_providers");
	ClassDB::bind_method(D_METHOD("get_debugger_draw_inactive"), &BulletFactory2D::get_debugger_draw_inactive);
	ClassDB::bind_method(D_METHOD("set_debugger_draw_inactive", "draw_inactive"), &BulletFactory2D::set_debugger_draw_inactive);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debugger_draw_inactive"), "set_debugger_draw_inactive", "get_debugger_draw_inactive");

	// Static pattern helpers (helper_generate_transforms_*, helper_sample_outline_*,
	// outline debug inspectors) and their enums: bullet_factory2d_patterns_bindings.cpp.

	// Typed per-bullet-kind collision signals. Emitted synchronously from the
	// physics tick (Godot-style): handlers run with live instance state, need
	// no casts, and only structural factory calls (reset/free_*/populate_*)
	// must be deferred - the error message says so when it happens.
	// Slim payloads: custom data and transforms are one instance call away
	// (bullet_get_custom_data(), get_bullet_transform()).
	// NOTE: signal args use PROPERTY_HINT_RESOURCE_TYPE (not NODE_TYPE) so the
	// class name reaches ClassDB and --doctool; with NODE_TYPE the class_name
	// stays empty and docs downgrade the params to Object. This mirrors how
	// the engine declares e.g. Area2D.area_entered.

	ADD_SIGNAL(MethodInfo("area_entered",
			PropertyInfo(Variant::OBJECT, "hit_target_area"),
			PropertyInfo(Variant::OBJECT, "volley", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D"),
			PropertyInfo(Variant::INT, "bullet_index")));

	ADD_SIGNAL(MethodInfo("body_entered",
			PropertyInfo(Variant::OBJECT, "hit_target_body"),
			PropertyInfo(Variant::OBJECT, "volley", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D"),
			PropertyInfo(Variant::INT, "bullet_index")));

	ADD_SIGNAL(MethodInfo("life_time_over",
			PropertyInfo(Variant::OBJECT, "volley", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D"),
			PropertyInfo(Variant::ARRAY, "bullet_indexes", PROPERTY_HINT_ARRAY_TYPE, "int")));

	// Bounce notifications: slim payload like the collision signals (custom
	// data and transforms stay one instance call away). Emitted synchronously
	// from the physics tick under the same handler contract (queue_free /
	// call_deferred factory calls only, never immediate free()). A bounce
	// that also consumes the hit (bounce_hit_consumed) emits BOTH the bounce
	// signal here and the matching area/body_entered signal.
	ADD_SIGNAL(MethodInfo("bounce_area_entered",
			PropertyInfo(Variant::OBJECT, "hit_target_area"),
			PropertyInfo(Variant::OBJECT, "volley", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D"),
			PropertyInfo(Variant::INT, "bullet_index")));

	ADD_SIGNAL(MethodInfo("bounce_body_entered",
			PropertyInfo(Variant::OBJECT, "hit_target_body"),
			PropertyInfo(Variant::OBJECT, "volley", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D"),
			PropertyInfo(Variant::INT, "bullet_index")));

	ADD_SIGNAL(MethodInfo("reset_finished"));

	// Graze notifications (see GrazeZone2D): EVERY graze in the game
	// reaches the factory, whoever owns the volley (a spawner's own graze
	// signals fire first while it lives), so one connection here counts
	// graze for bullets of spawners that were already freed. Emitted live
	// from the volley tick (right after the move), same handler contract as
	// the collision signals.
	ADD_SIGNAL(MethodInfo("bullet_grazed",
			PropertyInfo(Variant::OBJECT, "target", PROPERTY_HINT_RESOURCE_TYPE, "Node2D"),
			PropertyInfo(Variant::OBJECT, "volley", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D"),
			PropertyInfo(Variant::INT, "bullet_index"),
			PropertyInfo(Variant::OBJECT, "zone", PROPERTY_HINT_RESOURCE_TYPE, "GrazeZone2D"),
			PropertyInfo(Variant::INT, "ring_index")));
	ADD_SIGNAL(MethodInfo("bullet_graze_exited",
			PropertyInfo(Variant::OBJECT, "target", PROPERTY_HINT_RESOURCE_TYPE, "Node2D"),
			PropertyInfo(Variant::OBJECT, "volley", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D"),
			PropertyInfo(Variant::INT, "bullet_index"),
			PropertyInfo(Variant::OBJECT, "zone", PROPERTY_HINT_RESOURCE_TYPE, "GrazeZone2D"),
			PropertyInfo(Variant::INT, "deepest_ring_index")));
	ClassDB::bind_method(D_METHOD("debug_get_graze_targets", "group"), &BulletFactory2D::debug_get_graze_targets);
	ClassDB::bind_method(D_METHOD("debug_get_graze_stats"), &BulletFactory2D::debug_get_graze_stats);
	ClassDB::bind_method(D_METHOD("debug_get_flock_stats"), &BulletFactory2D::debug_get_flock_stats);
	ClassDB::bind_method(D_METHOD("get_default_flock"), &BulletFactory2D::get_default_flock);
	ClassDB::bind_method(D_METHOD("debug_set_graze_slab_min_targets", "value"), &BulletFactory2D::debug_set_graze_slab_min_targets);

	// Splitting (bullet_factory2d_split.cpp): splits recorded during a sweep
	// spawn right after it. bullet_split fires once per parent that split
	// (factory volleys here; spawner volleys on their spawner).
	ClassDB::bind_method(D_METHOD("get_split_max_children_per_sweep"), &BulletFactory2D::get_split_max_children_per_sweep);
	ClassDB::bind_method(D_METHOD("set_split_max_children_per_sweep", "value"), &BulletFactory2D::set_split_max_children_per_sweep);
	ClassDB::bind_method(D_METHOD("get_split_max_pending"), &BulletFactory2D::get_split_max_pending);
	ClassDB::bind_method(D_METHOD("set_split_max_pending", "value"), &BulletFactory2D::set_split_max_pending);
	ClassDB::bind_method(D_METHOD("get_pending_split_count"), &BulletFactory2D::get_pending_split_count);
	ClassDB::bind_method(D_METHOD("debug_get_split_stats"), &BulletFactory2D::debug_get_split_stats);
	ADD_GROUP("Splitting", "split_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "split_max_children_per_sweep", PROPERTY_HINT_RANGE, "0,10000,1,or_greater"), "set_split_max_children_per_sweep", "get_split_max_children_per_sweep");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "split_max_pending", PROPERTY_HINT_RANGE, "0,1000000,1,or_greater"), "set_split_max_pending", "get_split_max_pending");
	// Lasers (bullet_factory2d_lasers.cpp): factory-owned lasers signal here;
	// a LaserSpawner2D's lasers signal on that spawner (the same signals).
	ClassDB::bind_method(D_METHOD("spawn_laser", "data", "transforms", "anchor"), &BulletFactory2D::spawn_laser_script, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("get_active_lasers"), &BulletFactory2D::get_active_lasers);
	ClassDB::bind_method(D_METHOD("get_active_laser_count"), &BulletFactory2D::get_active_laser_count);
	ClassDB::bind_method(D_METHOD("clear_active_lasers", "fade"), &BulletFactory2D::clear_active_lasers, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("end_lasers_in_circle", "center", "radius", "fade"), &BulletFactory2D::end_lasers_in_circle, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("end_lasers_in_rect", "rect", "fade"), &BulletFactory2D::end_lasers_in_rect, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("populate_lasers_pool", "beam_count", "amount"), &BulletFactory2D::populate_lasers_pool);
	ClassDB::bind_method(D_METHOD("populate_lasers_pool_deferred", "beam_count", "amount"), &BulletFactory2D::populate_lasers_pool_deferred);
	ClassDB::bind_method(D_METHOD("free_lasers_pool", "beam_count"), &BulletFactory2D::free_lasers_pool, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("free_lasers_pool_deferred", "beam_count"), &BulletFactory2D::free_lasers_pool_deferred, DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("debug_get_laser_pool_amount"), &BulletFactory2D::debug_get_laser_pool_amount);
	ADD_SIGNAL(MethodInfo("laser_phase_changed",
			PropertyInfo(Variant::OBJECT, "laser", PROPERTY_HINT_RESOURCE_TYPE, "LaserVolley2D"),
			PropertyInfo(Variant::INT, "phase")));
	ADD_SIGNAL(MethodInfo("laser_pierce_started",
			PropertyInfo(Variant::OBJECT, "target"),
			PropertyInfo(Variant::OBJECT, "laser", PROPERTY_HINT_RESOURCE_TYPE, "LaserVolley2D"),
			PropertyInfo(Variant::INT, "beam_index"),
			PropertyInfo(Variant::VECTOR2, "position")));
	ADD_SIGNAL(MethodInfo("laser_pierce_ended",
			PropertyInfo(Variant::OBJECT, "target"),
			PropertyInfo(Variant::OBJECT, "laser", PROPERTY_HINT_RESOURCE_TYPE, "LaserVolley2D"),
			PropertyInfo(Variant::INT, "beam_index")));
	ADD_SIGNAL(MethodInfo("laser_hit",
			PropertyInfo(Variant::OBJECT, "target"),
			PropertyInfo(Variant::OBJECT, "laser", PROPERTY_HINT_RESOURCE_TYPE, "LaserVolley2D"),
			PropertyInfo(Variant::INT, "beam_index"),
			PropertyInfo(Variant::VECTOR2, "position")));
	ADD_SIGNAL(MethodInfo("laser_wall_hit",
			PropertyInfo(Variant::OBJECT, "laser", PROPERTY_HINT_RESOURCE_TYPE, "LaserVolley2D"),
			PropertyInfo(Variant::INT, "beam_index"),
			PropertyInfo(Variant::VECTOR2, "position"),
			PropertyInfo(Variant::VECTOR2, "normal")));
	ADD_SIGNAL(MethodInfo("laser_finished",
			PropertyInfo(Variant::OBJECT, "laser", PROPERTY_HINT_RESOURCE_TYPE, "LaserVolley2D")));
	// A bolt or curvy head left the despawn bounds (spent there).
	ADD_SIGNAL(MethodInfo("laser_despawned",
			PropertyInfo(Variant::OBJECT, "laser", PROPERTY_HINT_RESOURCE_TYPE, "LaserVolley2D"),
			PropertyInfo(Variant::INT, "beam_index"),
			PropertyInfo(Variant::VECTOR2, "position")));
	// Laser grazes bubble like bullet grazes: the owner spawner first, then
	// always here.
	ADD_SIGNAL(MethodInfo("laser_grazed",
			PropertyInfo(Variant::OBJECT, "target"),
			PropertyInfo(Variant::OBJECT, "laser", PROPERTY_HINT_RESOURCE_TYPE, "LaserVolley2D"),
			PropertyInfo(Variant::INT, "beam_index"),
			PropertyInfo(Variant::OBJECT, "zone", PROPERTY_HINT_RESOURCE_TYPE, "GrazeZone2D"),
			PropertyInfo(Variant::INT, "ring_index")));
	ADD_SIGNAL(MethodInfo("laser_graze_exited",
			PropertyInfo(Variant::OBJECT, "target"),
			PropertyInfo(Variant::OBJECT, "laser", PROPERTY_HINT_RESOURCE_TYPE, "LaserVolley2D"),
			PropertyInfo(Variant::INT, "beam_index"),
			PropertyInfo(Variant::OBJECT, "zone", PROPERTY_HINT_RESOURCE_TYPE, "GrazeZone2D"),
			PropertyInfo(Variant::INT, "deepest_ring_index")));

	// Emitter bullets (bullet_factory2d_emitter.cpp): shots recorded during a
	// sweep fire through their template spawners right after the splits.
	ClassDB::bind_method(D_METHOD("get_emitter_max_shots_per_sweep"), &BulletFactory2D::get_emitter_max_shots_per_sweep);
	ClassDB::bind_method(D_METHOD("set_emitter_max_shots_per_sweep", "value"), &BulletFactory2D::set_emitter_max_shots_per_sweep);
	ClassDB::bind_method(D_METHOD("get_emitter_max_pending"), &BulletFactory2D::get_emitter_max_pending);
	ClassDB::bind_method(D_METHOD("set_emitter_max_pending", "value"), &BulletFactory2D::set_emitter_max_pending);
	ClassDB::bind_method(D_METHOD("get_pending_emit_count"), &BulletFactory2D::get_pending_emit_count);
	ClassDB::bind_method(D_METHOD("debug_get_emitter_stats"), &BulletFactory2D::debug_get_emitter_stats);
	ADD_GROUP("Emitter Bullets", "emitter_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "emitter_max_shots_per_sweep", PROPERTY_HINT_RANGE, "0,10000,1,or_greater"), "set_emitter_max_shots_per_sweep", "get_emitter_max_shots_per_sweep");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "emitter_max_pending", PROPERTY_HINT_RANGE, "0,1000000,1,or_greater"), "set_emitter_max_pending", "get_emitter_max_pending");

	ADD_SIGNAL(MethodInfo("bullet_split",
			PropertyInfo(Variant::VECTOR2, "position"),
			PropertyInfo(Variant::INT, "cause"),
			PropertyInfo(Variant::OBJECT, "children", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D"),
			PropertyInfo(Variant::INT, "depth")));

	ADD_GROUP("Sound", "sound_");

	// Sound triggers (SoundMixer2D): trigger sites offer, the sweep's winners
	// play at the end of _physics_process. Volley and spawner entries route
	// automatically when armed; play_sound is the manual hatch.
	ClassDB::bind_method(D_METHOD("play_sound", "sound", "global_position", "listener_group"), &BulletFactory2D::play_sound, DEFVAL(StringName()));
	ClassDB::bind_method(D_METHOD("stop_sounds"), &BulletFactory2D::stop_sounds);
	ClassDB::bind_method(D_METHOD("get_sound_max_voices"), &BulletFactory2D::get_sound_max_voices);
	ClassDB::bind_method(D_METHOD("set_sound_max_voices", "value"), &BulletFactory2D::set_sound_max_voices);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sound_max_voices", PROPERTY_HINT_RANGE, "1,256,1,or_greater"), "set_sound_max_voices", "get_sound_max_voices");

	// Bullet world: speed of time, play-field bounds, area clears.
	ADD_GROUP("Time", "time_");
	ClassDB::bind_method(D_METHOD("get_time_scale"), &BulletFactory2D::get_time_scale);
	ClassDB::bind_method(D_METHOD("set_time_scale", "value"), &BulletFactory2D::set_time_scale);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time_scale", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_time_scale", "get_time_scale");

	ADD_GROUP("Despawn Bounds", "despawn_bounds_");
	ClassDB::bind_method(D_METHOD("get_despawn_bounds_enabled"), &BulletFactory2D::get_despawn_bounds_enabled);
	ClassDB::bind_method(D_METHOD("set_despawn_bounds_enabled", "value"), &BulletFactory2D::set_despawn_bounds_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "despawn_bounds_enabled"), "set_despawn_bounds_enabled", "get_despawn_bounds_enabled");
	ClassDB::bind_method(D_METHOD("get_despawn_bounds_rect"), &BulletFactory2D::get_despawn_bounds_rect);
	ClassDB::bind_method(D_METHOD("set_despawn_bounds_rect", "value"), &BulletFactory2D::set_despawn_bounds_rect);
	ADD_PROPERTY(PropertyInfo(Variant::RECT2, "despawn_bounds_rect"), "set_despawn_bounds_rect", "get_despawn_bounds_rect");
	ClassDB::bind_method(D_METHOD("get_despawn_bounds_color"), &BulletFactory2D::get_despawn_bounds_color);
	ClassDB::bind_method(D_METHOD("set_despawn_bounds_color", "value"), &BulletFactory2D::set_despawn_bounds_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "despawn_bounds_color"), "set_despawn_bounds_color", "get_despawn_bounds_color");
	ClassDB::bind_method(D_METHOD("get_despawn_bounds_preview"), &BulletFactory2D::get_despawn_bounds_preview);
	ClassDB::bind_method(D_METHOD("set_despawn_bounds_preview", "value"), &BulletFactory2D::set_despawn_bounds_preview);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "despawn_bounds_preview"), "set_despawn_bounds_preview", "get_despawn_bounds_preview");
	ClassDB::bind_method(D_METHOD("get_despawn_bounds_preview_during_runtime"), &BulletFactory2D::get_despawn_bounds_preview_during_runtime);
	ClassDB::bind_method(D_METHOD("set_despawn_bounds_preview_during_runtime", "value"), &BulletFactory2D::set_despawn_bounds_preview_during_runtime);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "despawn_bounds_preview_during_runtime"), "set_despawn_bounds_preview_during_runtime", "get_despawn_bounds_preview_during_runtime");
	ClassDB::bind_method(D_METHOD("get_despawn_bounds_preview_z_index"), &BulletFactory2D::get_despawn_bounds_preview_z_index);
	ClassDB::bind_method(D_METHOD("set_despawn_bounds_preview_z_index", "value"), &BulletFactory2D::set_despawn_bounds_preview_z_index);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "despawn_bounds_preview_z_index", PROPERTY_HINT_RANGE, "-4096,4096,1"), "set_despawn_bounds_preview_z_index", "get_despawn_bounds_preview_z_index");
	ClassDB::bind_method(D_METHOD("debug_get_laser_debug_boxes"), &BulletFactory2D::debug_get_laser_debug_boxes);
	ClassDB::bind_method(D_METHOD("debug_get_despawn_bounds_preview"), &BulletFactory2D::debug_get_despawn_bounds_preview);

	ClassDB::bind_method(D_METHOD("clear_bullets_in_circle", "center", "radius"), &BulletFactory2D::clear_bullets_in_circle);
	ClassDB::bind_method(D_METHOD("clear_bullets_in_rect", "rect"), &BulletFactory2D::clear_bullets_in_rect);
	ClassDB::bind_method(D_METHOD("count_bullets_in_circle", "center", "radius"), &BulletFactory2D::count_bullets_in_circle);
	ClassDB::bind_method(D_METHOD("count_bullets_in_rect", "rect"), &BulletFactory2D::count_bullets_in_rect);
	ClassDB::bind_method(D_METHOD("get_bullets_in_circle", "center", "radius"), &BulletFactory2D::get_bullets_in_circle);
	ClassDB::bind_method(D_METHOD("get_bullets_in_rect", "rect"), &BulletFactory2D::get_bullets_in_rect);
	ClassDB::bind_method(D_METHOD("get_active_volleys"), &BulletFactory2D::get_active_volleys);

	// Bullets that left through the despawn bounds, once per volley and tick
	// (positions are the poses they left from). Live, inside the sweep: a
	// handler may spawn (coins, effects) but not free the volley.
	ADD_SIGNAL(MethodInfo("bullets_despawned",
			PropertyInfo(Variant::PACKED_VECTOR2_ARRAY, "positions"),
			PropertyInfo(Variant::OBJECT, "volley", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D"),
			PropertyInfo(Variant::ARRAY, "bullet_indexes", PROPERTY_HINT_ARRAY_TYPE, "int")));

	// Pickups: bullets to coins/gems that are pulled to a target.
	ClassDB::bind_method(D_METHOD("convert_all_bullets_to_pickups", "pickup", "target"), &BulletFactory2D::convert_all_bullets_to_pickups);
	ClassDB::bind_method(D_METHOD("convert_bullets_in_circle_to_pickups", "center", "radius", "pickup", "target"), &BulletFactory2D::convert_bullets_in_circle_to_pickups);
	ClassDB::bind_method(D_METHOD("convert_bullets_in_rect_to_pickups", "rect", "pickup", "target"), &BulletFactory2D::convert_bullets_in_rect_to_pickups);
	ClassDB::bind_method(D_METHOD("convert_all_bullets_to_pickups_deferred", "pickup", "target"), &BulletFactory2D::convert_all_bullets_to_pickups_deferred);
	ClassDB::bind_method(D_METHOD("populate_pickup_pool", "pickup", "bullet_count", "instance_count"), &BulletFactory2D::populate_pickup_pool);
	ClassDB::bind_method(D_METHOD("populate_pickup_pool_deferred", "pickup", "bullet_count", "instance_count"), &BulletFactory2D::populate_pickup_pool_deferred);
	// A pickup reached its target (the bullet is still alive in the handler,
	// then it leaves). `target` is null for a point or the mouse.
	ADD_SIGNAL(MethodInfo("pickup_collected",
			PropertyInfo(Variant::OBJECT, "target", PROPERTY_HINT_RESOURCE_TYPE, "Node2D"),
			PropertyInfo(Variant::VECTOR2, "position"),
			PropertyInfo(Variant::OBJECT, "volley", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D"),
			PropertyInfo(Variant::INT, "bullet_index"),
			PropertyInfo(Variant::NIL, "value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT)));
	// After a conversion call: how many bullets became pickups and the new volleys.
	ADD_SIGNAL(MethodInfo("bullets_converted",
			PropertyInfo(Variant::INT, "count"),
			PropertyInfo(Variant::ARRAY, "volleys", PROPERTY_HINT_ARRAY_TYPE, "BulletVolley2D")));
	// Once per volley after an area clear: where the cleared bullets were
	// (global positions, parallel to bullet_indexes). The volley may already
	// be pooled when this fires (positions are the payload).
	ADD_SIGNAL(MethodInfo("bullets_cleared",
			PropertyInfo(Variant::PACKED_VECTOR2_ARRAY, "positions"),
			PropertyInfo(Variant::OBJECT, "volley", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D"),
			PropertyInfo(Variant::ARRAY, "bullet_indexes", PROPERTY_HINT_ARRAY_TYPE, "int")));
	ClassDB::bind_method(D_METHOD("debug_set_sound_log_enabled", "enabled"), &BulletFactory2D::debug_set_sound_log_enabled);
	ClassDB::bind_method(D_METHOD("debug_seed_cosmetic_rng", "seed"), &BulletFactory2D::debug_seed_cosmetic_rng);
	ClassDB::bind_method(D_METHOD("debug_get_sound_log"), &BulletFactory2D::debug_get_sound_log);
	ClassDB::bind_method(D_METHOD("debug_clear_sound_log"), &BulletFactory2D::debug_clear_sound_log);
	ClassDB::bind_method(D_METHOD("debug_set_sound_offer_log_enabled", "enabled"), &BulletFactory2D::debug_set_sound_offer_log_enabled);
	ClassDB::bind_method(D_METHOD("debug_get_sound_offer_log"), &BulletFactory2D::debug_get_sound_offer_log);
	ClassDB::bind_method(D_METHOD("debug_clear_sound_offer_log"), &BulletFactory2D::debug_clear_sound_offer_log);
	ClassDB::bind_method(D_METHOD("debug_get_sound_voices"), &BulletFactory2D::debug_get_sound_voices);
	ClassDB::bind_method(D_METHOD("debug_get_sound_stats"), &BulletFactory2D::debug_get_sound_stats);
	ClassDB::bind_method(D_METHOD("debug_stop_sound_voices"), &BulletFactory2D::debug_stop_sound_voices);
	ClassDB::bind_method(D_METHOD("debug_explain_sound", "sound", "global_position", "listener_group", "volley_amount"), &BulletFactory2D::debug_explain_sound, DEFVAL(StringName()), DEFVAL(1));
}

} // namespace BlastBullets2D
