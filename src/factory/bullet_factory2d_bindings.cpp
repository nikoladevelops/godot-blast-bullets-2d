// Godot bindings of BulletFactory2D's own API (spawning, pools, structural calls,
// debugger, stats, signals). The static pattern helpers are bound in
// bullet_factory2d_patterns_bindings.cpp.

#include "factory/bullet_factory2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

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

	ClassDB::bind_method(D_METHOD("get_is_factory_busy"), &BulletFactory2D::get_is_factory_busy);

	ClassDB::bind_method(D_METHOD("get_physics_space"), &BulletFactory2D::get_physics_space);
	ClassDB::bind_method(D_METHOD("set_physics_space", "new_physics_space"), &BulletFactory2D::set_physics_space);

	ClassDB::bind_method(D_METHOD("get_is_debugger_enabled"), &BulletFactory2D::get_is_debugger_enabled);
	ClassDB::bind_method(D_METHOD("set_is_debugger_enabled", "new_is_enabled"), &BulletFactory2D::set_is_debugger_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_debugger_enabled"), "set_is_debugger_enabled", "get_is_debugger_enabled");

	ClassDB::bind_method(D_METHOD("get_is_factory_processing_bullets"), &BulletFactory2D::get_is_factory_processing_bullets);
	ClassDB::bind_method(D_METHOD("set_is_factory_processing_bullets", "is_processing_enabled"), &BulletFactory2D::set_is_factory_processing_bullets);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_factory_processing_bullets"), "set_is_factory_processing_bullets", "get_is_factory_processing_bullets");

	ClassDB::bind_method(D_METHOD("get_use_physics_interpolation"), &BulletFactory2D::get_use_physics_interpolation);
	ClassDB::bind_method(D_METHOD("set_use_physics_interpolation_editor", "enable"), &BulletFactory2D::set_use_physics_interpolation_editor);
	ClassDB::bind_method(D_METHOD("set_use_physics_interpolation_runtime", "enable"), &BulletFactory2D::set_use_physics_interpolation_runtime);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_physics_interpolation"), "set_use_physics_interpolation_editor", "get_use_physics_interpolation");

	ClassDB::bind_method(D_METHOD("spawn_volley", "spawn_data", "inherited_velocity_offset", "spawner_id"), &BulletFactory2D::spawn_volley, DEFVAL(Vector2(0, 0)), DEFVAL(0));

	ClassDB::bind_method(D_METHOD("reset", "key"), &BulletFactory2D::reset, DEFVAL(Ref<VolleyPoolKey2D>()));

	ClassDB::bind_method(D_METHOD("get_debugger_color"), &BulletFactory2D::get_debugger_color);
	ClassDB::bind_method(D_METHOD("set_debugger_color", "new_color"), &BulletFactory2D::set_debugger_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "debugger_color"), "set_debugger_color", "get_debugger_color");

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
	ClassDB::bind_method(D_METHOD("free_volley_deferred", "volley"), &BulletFactory2D::free_volley_deferred);
	ClassDB::bind_method(D_METHOD("ensure_factory_initialized"), &BulletFactory2D::ensure_factory_initialized);

	// Additional debug methods related

	ClassDB::bind_method(D_METHOD("debug_get_total_bullets_amount"), &BulletFactory2D::debug_get_total_bullets_amount);
	ClassDB::bind_method(D_METHOD("debug_get_active_bullets_amount"), &BulletFactory2D::debug_get_active_bullets_amount);
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
	ClassDB::bind_method(D_METHOD("debug_check_interpolation_status"), &BulletFactory2D::debug_check_interpolation_status);
	ClassDB::bind_method(D_METHOD("debug_get_live_volley_ids", "owner_spawner_id"), &BulletFactory2D::debug_get_live_volley_ids);
	ClassDB::bind_static_method("BulletFactory2D", D_METHOD("debug_expected_pool_key", "spawn_data"), &BulletFactory2D::debug_expected_pool_key);
	ClassDB::bind_method(D_METHOD("debug_assert_no_dangling"), &BulletFactory2D::debug_assert_no_dangling);
	ClassDB::bind_method(D_METHOD("debug_get_pool_bucket", "volley"), &BulletFactory2D::debug_get_pool_bucket);

	ClassDB::bind_method(D_METHOD("get_debugger_max_providers"), &BulletFactory2D::get_debugger_max_providers);
	ClassDB::bind_method(D_METHOD("set_debugger_max_providers", "max_providers"), &BulletFactory2D::set_debugger_max_providers);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "debugger_max_providers", PROPERTY_HINT_RANGE, "0,10000,1"), "set_debugger_max_providers", "get_debugger_max_providers");
	ClassDB::bind_method(D_METHOD("get_debugger_draw_inactive"), &BulletFactory2D::get_debugger_draw_inactive);
	ClassDB::bind_method(D_METHOD("set_debugger_draw_inactive", "draw_inactive"), &BulletFactory2D::set_debugger_draw_inactive);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debugger_draw_inactive"), "set_debugger_draw_inactive", "get_debugger_draw_inactive");

	// Static pattern helpers (helper_generate_transforms_*, helper_sample_outline_*,
	// outline debug inspectors) and their enums: bullet_factory2d_patterns_bindings.cpp.
	bind_pattern_helpers();

	// Typed per-bullet-kind collision signals. Emitted synchronously from the
	// physics tick (Godot-style): handlers run with live instance state, need
	// no casts, and only structural factory calls (reset/free_*/populate_*)
	// must be deferred - the error message says so when it happens.
	// Slim payloads: custom data and transforms are one instance call away
	// (bullet_get_custom_data(), get_bullet_global_transform()).
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
}

} // namespace BlastBullets2D
