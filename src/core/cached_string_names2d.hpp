#pragma once

#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/string_name.hpp>

namespace BlastBullets2D {
using namespace godot;

// Hot-path StringNames, owned by the module lifetime instead of function-local
// statics. A static StringName is destroyed at library unload (atexit),
// after the engine has torn down its StringName table: a godot-cpp exit /
// hot-reload hazard. Created in initialize_*, freed in uninitialize_*.
struct CachedStringNames2D {
	const StringName linear_velocity = StringName("linear_velocity");
	const StringName velocity = StringName("velocity");
	const StringName constant_linear_velocity = StringName("constant_linear_velocity");
	// Signal / deferred-method / property names used on per-record and
	// per-expiry paths (a string literal builds a StringName = an interning
	// lookup across the extension boundary on EVERY call).
	const StringName area_entered = StringName("area_entered");
	const StringName body_entered = StringName("body_entered");
	const StringName bounce_area_entered = StringName("bounce_area_entered");
	const StringName bounce_body_entered = StringName("bounce_body_entered");
	const StringName collision_layer = StringName("collision_layer");
	const StringName life_time_over = StringName("life_time_over");
	const StringName sprite_animation_finished = StringName("sprite_animation_finished");
	const StringName bullet_homing_target_reached = StringName("bullet_homing_target_reached");
	const StringName m_do_emit_life_time_over = StringName("_do_emit_life_time_over");
	const StringName m_do_deferred_bullet_disable_attachments = StringName("_do_deferred_bullet_disable_attachments");
	const StringName m_do_finish_lifetime_hold = StringName("_do_finish_lifetime_hold");
	const StringName m_do_emit_homing_target_reached = StringName("_do_emit_homing_target_reached");
	const StringName m_do_shared_auto_pop_front_target = StringName("_do_shared_auto_pop_front_target");
	const StringName m_do_auto_pop_front_target = StringName("_do_auto_pop_front_target");
	const StringName m_do_execute_stored_callable_safely = StringName("_do_execute_stored_callable_safely");
	const StringName m_do_emit_sprite_animation_finished = StringName("_do_emit_sprite_animation_finished");
	const StringName m_do_attach_time_based_function = StringName("_do_attach_time_based_function");
	const StringName m_do_detach_time_based_function = StringName("_do_detach_time_based_function");
	const StringName m_do_detach_all_time_based_functions = StringName("_do_detach_all_time_based_functions");

	static inline CachedStringNames2D *singleton = nullptr;

	static void create() {
		if (singleton == nullptr) {
			singleton = memnew(CachedStringNames2D);
		}
	}
	static void destroy() {
		if (singleton != nullptr) {
			memdelete(singleton);
			singleton = nullptr;
		}
	}
	static const CachedStringNames2D &get() {
		// Lazily recreated if used before init (never expected): still owned
		// and freed by destroy(), never a static.
		create();
		return *singleton;
	}
};

} //namespace BlastBullets2D
