// BulletSpawner2D (split from bullet_spawner2d.cpp, same class):
// ClassDB registration (_bind_methods: inspector groups/subgroups, enums,
// signals) and per-mode inspector visibility (_validate_property).

#include "bullet_spawner/bullet_spawner2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletSpawner2D::_validate_property(PropertyInfo &p_property) const {
	const String property_name = p_property.name;
	// Spin: knobs hide while spin is off; CONTINUOUS reads only the speed,
	// OSCILLATE only amplitude + frequency (advance_spin).
	if (property_name.begins_with("spin_")) {
		bool show = property_name == "spin_enabled" || spin_enabled;
		if (show && property_name == "spin_speed_deg_per_sec") {
			show = spin_mode == SPIN_CONTINUOUS;
		} else if (show && (property_name == "spin_amplitude_deg" || property_name == "spin_frequency_hz")) {
			show = spin_mode == SPIN_OSCILLATE;
		}
		if (!show) {
			p_property.usage &= ~PROPERTY_USAGE_EDITOR;
		}
		return;
	}
	// Burst / telegraph: tuning knobs appear once their switch is on.
	if (property_name.begins_with("burst_") && property_name != "burst_enabled") {
		if (!burst_enabled) {
			p_property.usage &= ~PROPERTY_USAGE_EDITOR;
		}
		return;
	}
	if (property_name == "telegraph_sec") {
		if (!telegraph_enabled) {
			p_property.usage &= ~PROPERTY_USAGE_EDITOR;
		}
		return;
	}
	// Movement: every knob hides while movement is off; mode-specific knobs
	// only show where they do something.
	if (property_name.begins_with("movement_") || property_name == "inherit_movement_velocity") {
		bool show = movement_enabled || property_name == "movement_enabled";
		if (show) {
			if (property_name == "movement_loops") {
				show = movement_loop_mode != MOVEMENT_LOOP_ONCE;
			} else if (property_name == "movement_duration_sec") {
				show = movement_timing == MOVEMENT_TIMING_DURATION;
			} else if (property_name == "movement_speed") {
				show = movement_timing == MOVEMENT_TIMING_SPEED;
			} else if (property_name == "movement_transition" || property_name == "movement_ease") {
				show = movement_progress_curve.is_null();
			} else if (property_name == "movement_rotation_offset_deg") {
				show = movement_rotate_with_path;
			} else if (property_name == "movement_velocity_inherit_factor") {
				show = inherit_movement_velocity;
			}
		}
		if (!show) {
			p_property.usage &= ~PROPERTY_USAGE_EDITOR;
		}
		return;
	}
	// Graze: everything but the master switch hides while graze is off; the
	// line width also needs a preview toggle, each target source shows only
	// its own knobs.
	if (property_name.begins_with("graze_")) {
		bool show = property_name == "graze_enabled" || graze_enabled;
		if (show && property_name == "graze_preview_line_width") {
			show = graze_show_preview || graze_preview_during_runtime;
		} else if (show && property_name == "graze_node_group") {
			show = graze_target_source == GRAZE_SOURCE_NODE_GROUP;
		} else if (show && property_name == "graze_target_path") {
			show = graze_target_source == GRAZE_SOURCE_NODE_PATH;
		} else if (show && property_name.begins_with("graze_node_name")) {
			show = graze_target_source == GRAZE_SOURCE_NODE_NAME;
		} else if (show && property_name.begins_with("graze_children_")) {
			show = graze_target_source == GRAZE_SOURCE_NODE_CHILDREN;
		} else if (show && property_name == "graze_global_positions") {
			show = graze_target_source == GRAZE_SOURCE_GLOBAL_POSITIONS;
		} else if (show && (property_name == "graze_filter_group" || property_name == "graze_update_interval")) {
			// Points have no group and are read every tick.
			show = graze_target_source != GRAZE_SOURCE_MOUSE && graze_target_source != GRAZE_SOURCE_GLOBAL_POSITIONS;
		}
		if (!show) {
			p_property.usage &= ~PROPERTY_USAGE_EDITOR;
		}
		return;
	}
	// Tidy inspector: hide the preview tuning knobs while both preview
	// switches are off. The toggles + spin props stay always visible.
	if (property_name.begins_with("preview_") && property_name != "show_pattern_preview") {
		if (!show_pattern_preview && !show_preview_during_runtime) {
			p_property.usage &= ~PROPERTY_USAGE_EDITOR;
		}
		return;
	}
	// Homing/orbiting inspector gating: the master switches and mode/source
	// pickers are always visible, everything else appears only when its
	// feature (and source/mode) is active - same idea as the helper_* groups.
	if (property_name.begins_with("homing_") || property_name.begins_with("orbiting_") || property_name.begins_with("shared_homing_")) {
		bool show = true;
		if (property_name.begins_with("homing_")) {
			if (property_name != "homing_enabled" && property_name != "homing_mode" && property_name != "homing_target_source") {
				show = homing_enabled;
			}
			if (show && homing_enabled) {
				const bool multi_source = homing_target_source == HOMING_SOURCE_NODE_GROUP || homing_target_source == HOMING_SOURCE_NODE_NAME || homing_target_source == HOMING_SOURCE_NODE_CHILDREN;
				if (property_name == "homing_node_group") {
					show = homing_target_source == HOMING_SOURCE_NODE_GROUP;
				} else if (property_name == "homing_node_name" || property_name == "homing_node_name_match_mode" || property_name == "homing_node_name_case_sensitive") {
					show = homing_target_source == HOMING_SOURCE_NODE_NAME;
				} else if (property_name == "homing_children_parent_path" || property_name == "homing_children_recursive") {
					show = homing_target_source == HOMING_SOURCE_NODE_CHILDREN;
				} else if (property_name == "homing_target_selection" || property_name == "homing_max_targets" || property_name == "homing_max_detection_range") {
					show = multi_source;
				} else if (property_name == "homing_filter_group") {
					show = multi_source || homing_target_source == HOMING_SOURCE_NODE_PATH;
				} else if (property_name == "homing_global_position") {
					show = homing_target_source == HOMING_SOURCE_GLOBAL_POSITION;
				} else if (property_name == "homing_target_path") {
					show = homing_target_source == HOMING_SOURCE_NODE_PATH;
				} else if (property_name == "homing_per_bullet_smoothing_enabled") {
					show = homing_mode == HOMING_PER_BULLET;
				} else if (property_name == "homing_smoothing_start" || property_name == "homing_smoothing_step") {
					show = homing_mode == HOMING_PER_BULLET && homing_per_bullet_smoothing_enabled;
				} else if (property_name == "homing_retarget_interval_sec" || property_name == "homing_retarget_previous_volleys") {
					show = homing_retarget_mode == HOMING_RETARGET_ON_INTERVAL;
				} else if (property_name == "homing_delay_sec" || property_name == "homing_duration_sec" || property_name == "homing_lose_range_px" || property_name == "homing_fire_arc_deg") {
					show = true;
				} else if (property_name == "homing_random_seed") {
					show = homing_target_selection == HOMING_SELECT_RANDOM;
				} else if (property_name == "homing_retarget_phase") {
					show = homing_retarget_mode == HOMING_RETARGET_ON_INTERVAL;
				}
			}
		} else {
			// Orbiting only works with homing on (it locks onto a homing
			// target): without homing the tunables would arm a dead feature,
			// so they hide until both switches are on.
			if (property_name != "orbiting_enabled") {
				show = homing_enabled && orbiting_enabled;
			}
			if (show && orbiting_enabled) {
				if (property_name == "orbiting_radius") {
					show = !orbiting_radius_linear_enabled;
				} else if (property_name == "orbiting_radius_start" || property_name == "orbiting_radius_step") {
					show = orbiting_radius_linear_enabled;
				} else if (property_name == "orbiting_follow_deadzone") {
					show = orbiting_follow_mode == BulletVolley2D::FollowDeadzone;
				}
			}
		}
		if (!show) {
			p_property.usage &= ~PROPERTY_USAGE_EDITOR;
		}
		return;
	}
	// Only the helper_* option groups are gated (patterns module, driven by
	// the shape registry's knob prefixes); everything else is always shown.
	if (!property_name.begins_with("helper_")) {
		return;
	}
	if (!is_knob_relevant((int)pattern_source, property_name)) {
		p_property.usage &= ~PROPERTY_USAGE_EDITOR;
	}
}

void BulletSpawner2D::_bind_methods() {
	// Inspector layout: Setup (wiring) -> Bullet Patterns (source, amount,
	// Transform subgroup, one subgroup per shape with its helper_<shape>_
	// prefix stripped, Outline Layers last) -> Shooting -> Spin -> Homing ->
	// Orbiting -> Graze -> Preview -> Movement -> Performance. Property NAMES never
	// change here (they are serialized into .tscn); only the order and the
	// headers do. ADD_PROPERTY must follow its bind_method calls, otherwise
	// ClassDB silently drops the property (the test runner flags that).
	ADD_GROUP("Setup", "");
	ClassDB::bind_method(D_METHOD("get_bullet_factory_path"), &BulletSpawner2D::get_bullet_factory_path);
	ClassDB::bind_method(D_METHOD("set_bullet_factory_path", "path"), &BulletSpawner2D::set_bullet_factory_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "bullet_factory_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "BulletFactory2D"), "set_bullet_factory_path", "get_bullet_factory_path");

	ClassDB::bind_method(D_METHOD("get_bullet_factory"), &BulletSpawner2D::get_bullet_factory);
	ClassDB::bind_method(D_METHOD("set_bullet_factory", "factory"), &BulletSpawner2D::set_bullet_factory);

	ClassDB::bind_method(D_METHOD("get_transforms_generator_path"), &BulletSpawner2D::get_transforms_generator_path);
	ClassDB::bind_method(D_METHOD("set_transforms_generator_path", "path"), &BulletSpawner2D::set_transforms_generator_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "transforms_generator", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node2D"), "set_transforms_generator_path", "get_transforms_generator_path");

	ClassDB::bind_method(D_METHOD("get_transforms_generator"), &BulletSpawner2D::get_transforms_generator);
	ClassDB::bind_method(D_METHOD("set_transforms_generator", "generator"), &BulletSpawner2D::set_transforms_generator);
	ClassDB::bind_method(D_METHOD("get_effective_generator"), &BulletSpawner2D::get_effective_generator);

	ClassDB::bind_method(D_METHOD("get_spawn_data"), &BulletSpawner2D::get_spawn_data);
	ClassDB::bind_method(D_METHOD("set_spawn_data", "new_spawn_data"), &BulletSpawner2D::set_spawn_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "spawn_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolleyData2D"), "set_spawn_data", "get_spawn_data");

	ClassDB::bind_method(D_METHOD("get_orphaned_volleys"), &BulletSpawner2D::get_orphaned_volleys);
	ClassDB::bind_method(D_METHOD("set_orphaned_volleys", "value"), &BulletSpawner2D::set_orphaned_volleys);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "orphaned_volleys", PROPERTY_HINT_ENUM, "Keep Flying,Hand To Factory,Clear,Remove"), "set_orphaned_volleys", "get_orphaned_volleys");

	ADD_GROUP("Bullet Patterns", "");
	// Hand-written accessors of the group (PATTERN_PROPERTY rows); the
	// table-driven knobs bind themselves in the expansion below.
	ClassDB::bind_method(D_METHOD("get_pattern_source"), &BulletSpawner2D::get_pattern_source);
	ClassDB::bind_method(D_METHOD("set_pattern_source", "value"), &BulletSpawner2D::set_pattern_source);
	ClassDB::bind_method(D_METHOD("get_pattern_scale"), &BulletSpawner2D::get_pattern_scale);
	ClassDB::bind_method(D_METHOD("set_pattern_scale", "value"), &BulletSpawner2D::set_pattern_scale);
	ClassDB::bind_method(D_METHOD("get_transforms_scale"), &BulletSpawner2D::get_transforms_scale);
	ClassDB::bind_method(D_METHOD("set_transforms_scale", "value"), &BulletSpawner2D::set_transforms_scale);
	ClassDB::bind_method(D_METHOD("get_spawn_position_offset"), &BulletSpawner2D::get_spawn_position_offset);
	ClassDB::bind_method(D_METHOD("set_spawn_position_offset", "value"), &BulletSpawner2D::set_spawn_position_offset);
	ClassDB::bind_method(D_METHOD("get_spawn_position_offset_space"), &BulletSpawner2D::get_spawn_position_offset_space);
	ClassDB::bind_method(D_METHOD("set_spawn_position_offset_space", "value"), &BulletSpawner2D::set_spawn_position_offset_space);
	BIND_ENUM_CONSTANT(SPAWN_OFFSET_GLOBAL);
	BIND_ENUM_CONSTANT(SPAWN_OFFSET_LOCAL);
	ClassDB::bind_method(D_METHOD("get_helper_aimed_target_path"), &BulletSpawner2D::get_helper_aimed_target_path);
	ClassDB::bind_method(D_METHOD("set_helper_aimed_target_path", "path"), &BulletSpawner2D::set_helper_aimed_target_path);
	ClassDB::bind_method(D_METHOD("get_helper_aimed_target"), &BulletSpawner2D::get_helper_aimed_target);
	ClassDB::bind_method(D_METHOD("set_helper_aimed_target", "target"), &BulletSpawner2D::set_helper_aimed_target);
	ClassDB::bind_method(D_METHOD("get_helper_custom_transforms"), &BulletSpawner2D::get_helper_custom_transforms);
	ClassDB::bind_method(D_METHOD("set_helper_custom_transforms", "value"), &BulletSpawner2D::set_helper_custom_transforms);
	ClassDB::bind_method(D_METHOD("get_helper_path2d_path"), &BulletSpawner2D::get_helper_path2d_path);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_path", "path"), &BulletSpawner2D::set_helper_path2d_path);
	ClassDB::bind_method(D_METHOD("get_helper_path2d_node"), &BulletSpawner2D::get_helper_path2d_node);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_node", "node"), &BulletSpawner2D::set_helper_path2d_node);
	ClassDB::bind_method(D_METHOD("get_helper_path2d_space"), &BulletSpawner2D::get_helper_path2d_space);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_space", "value"), &BulletSpawner2D::set_helper_path2d_space);
	ClassDB::bind_method(D_METHOD("get_helper_path2d_distribution"), &BulletSpawner2D::get_helper_path2d_distribution);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_distribution", "value"), &BulletSpawner2D::set_helper_path2d_distribution);
	ClassDB::bind_method(D_METHOD("get_helper_path2d_overflow"), &BulletSpawner2D::get_helper_path2d_overflow);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_overflow", "value"), &BulletSpawner2D::set_helper_path2d_overflow);
	ClassDB::bind_method(D_METHOD("get_helper_path2d_anchor"), &BulletSpawner2D::get_helper_path2d_anchor);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_anchor", "value"), &BulletSpawner2D::set_helper_path2d_anchor);
	ClassDB::bind_method(D_METHOD("get_helper_path2d_facing"), &BulletSpawner2D::get_helper_path2d_facing);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_facing", "value"), &BulletSpawner2D::set_helper_path2d_facing);
	ClassDB::bind_method(D_METHOD("get_helper_outline_layer_scales"), &BulletSpawner2D::get_helper_outline_layer_scales);
	ClassDB::bind_method(D_METHOD("set_helper_outline_layer_scales", "value"), &BulletSpawner2D::set_helper_outline_layer_scales);

	// Every property of the group, in inspector order, from the knob table.
#define PATTERN_SUBGROUP(TITLE, PREFIX) ADD_SUBGROUP(TITLE, PREFIX);
#define PATTERN_KNOB(PTYPE, CTYPE, VTYPE, NAME, C1, A1, B1, M1, C2, A2, B2, M2, NOTIFY, HINT, HINT_STRING) \
	ClassDB::bind_method(D_METHOD("get_" #NAME), &BulletSpawner2D::get_##NAME);                            \
	ClassDB::bind_method(D_METHOD("set_" #NAME, "value"), &BulletSpawner2D::set_##NAME);                   \
	ADD_PROPERTY(PropertyInfo(Variant::VTYPE, #NAME, HINT, HINT_STRING), "set_" #NAME, "get_" #NAME);
#define PATTERN_PROPERTY(VTYPE, NAME, HINT, HINT_STRING, SETTER, GETTER) \
	ADD_PROPERTY(PropertyInfo(Variant::VTYPE, #NAME, HINT, HINT_STRING), #SETTER, #GETTER);
#include "patterns/pattern_knob_table2d.inc"
#undef PATTERN_SUBGROUP
#undef PATTERN_KNOB
#undef PATTERN_PROPERTY

	// Spawner lifecycle signals. Emitted synchronously where the transition
	// happens (timer tick, setters, reset, _ready): handlers run with live
	// state and follow the same contract as the factory collision signals -
	// game logic is safe directly, structural factory calls must be deferred.
	// HANDLER CONTRACT (pre_shoot/volley_fired especially): the volley handle
	// is only valid for the duration of the emission. To destroy it, use
	// queue_free() (or call_deferred factory free/reset) - never immediate
	// Object.free(); the shoot path keeps touching the instance after the
	// emit. shoot_once() itself must not be called nested (rejected); use
	// shoot_once_deferred() instead.
	// NOTE: PROPERTY_HINT_RESOURCE_TYPE (not NODE_TYPE) carries the class name
	// to ClassDB/--doctool; see the note on the factory signals.
	ADD_SIGNAL(MethodInfo("pre_shoot",
			PropertyInfo(Variant::OBJECT, "volley", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D"),
			PropertyInfo(Variant::INT, "volley_index")));
	ADD_SIGNAL(MethodInfo("volley_fired",
			PropertyInfo(Variant::OBJECT, "volley", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D"),
			PropertyInfo(Variant::INT, "volley_index")));
	ADD_SIGNAL(MethodInfo("volley_skipped",
			PropertyInfo(Variant::STRING_NAME, "reason")));
	ADD_SIGNAL(MethodInfo("volley_telegraphed",
			PropertyInfo(Variant::ARRAY, "aim_transforms")));
	ADD_SIGNAL(MethodInfo("burst_shot_fired",
			PropertyInfo(Variant::INT, "shot_index"),
			PropertyInfo(Variant::BOOL, "mirrored")));
	ADD_SIGNAL(MethodInfo("burst_finished"));
	ADD_SIGNAL(MethodInfo("pattern_list_finished"));
	ADD_SIGNAL(MethodInfo("retarget_applied",
			PropertyInfo(Variant::INT, "volleys_retargeted")));
	ADD_SIGNAL(MethodInfo("shooting_started"));
	ADD_SIGNAL(MethodInfo("shooting_stopped"));
	ADD_SIGNAL(MethodInfo("shooting_finished"));
	// Collision/lifetime signals possessed by this spawner for the volleys it
	// spawned (see owner_spawner_id). Same slim payload shape as the factory
	// typed signals; emitted synchronously (area/body) or deferred
	// (life_time_over) under the same handler contract. A spawner volley
	// NEVER fires factory signals: if this spawner is gone, its bullets'
	// events are dropped instead of falling back to the factory.
	// NOTE: PROPERTY_HINT_RESOURCE_TYPE (not NODE_TYPE) carries the class name
	// to ClassDB/--doctool; see the note on the factory signals.
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
	// Bounce notifications possessed by this spawner for the volleys it
	// spawned (same routing as area_entered/body_entered above: a spawner
	// volley NEVER fires factory signals). Same slim payload and handler
	// contract. A consumed bounce (bounce_hit_consumed) emits BOTH the
	// bounce signal here and the matching area/body_entered signal.
	ADD_SIGNAL(MethodInfo("bounce_area_entered",
			PropertyInfo(Variant::OBJECT, "hit_target_area"),
			PropertyInfo(Variant::OBJECT, "volley", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D"),
			PropertyInfo(Variant::INT, "bullet_index")));
	ADD_SIGNAL(MethodInfo("bounce_body_entered",
			PropertyInfo(Variant::OBJECT, "hit_target_body"),
			PropertyInfo(Variant::OBJECT, "volley", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D"),
			PropertyInfo(Variant::INT, "bullet_index")));
	ClassDB::bind_method(D_METHOD("get_shooting_enabled"), &BulletSpawner2D::get_shooting_enabled);
	ClassDB::bind_method(D_METHOD("set_shooting_enabled", "value"), &BulletSpawner2D::set_shooting_enabled);
	ADD_GROUP("Shooting", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shooting_enabled"), "set_shooting_enabled", "get_shooting_enabled");
	ClassDB::bind_method(D_METHOD("is_shooting_active"), &BulletSpawner2D::is_shooting_active);

	ClassDB::bind_method(D_METHOD("get_shoot_interval_sec"), &BulletSpawner2D::get_shoot_interval_sec);
	ClassDB::bind_method(D_METHOD("set_shoot_interval_sec", "value"), &BulletSpawner2D::set_shoot_interval_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shoot_interval_sec", PROPERTY_HINT_RANGE, "0.005,30,0.001,or_greater"), "set_shoot_interval_sec", "get_shoot_interval_sec");

	ClassDB::bind_method(D_METHOD("get_shoot_initial_delay_sec"), &BulletSpawner2D::get_shoot_initial_delay_sec);
	ClassDB::bind_method(D_METHOD("set_shoot_initial_delay_sec", "value"), &BulletSpawner2D::set_shoot_initial_delay_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shoot_initial_delay_sec"), "set_shoot_initial_delay_sec", "get_shoot_initial_delay_sec");

	ClassDB::bind_method(D_METHOD("get_max_volleys"), &BulletSpawner2D::get_max_volleys);
	ClassDB::bind_method(D_METHOD("set_max_volleys", "value"), &BulletSpawner2D::set_max_volleys);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_volleys"), "set_max_volleys", "get_max_volleys");

	ClassDB::bind_method(D_METHOD("get_reload_jitter_sec"), &BulletSpawner2D::get_reload_jitter_sec);
	ClassDB::bind_method(D_METHOD("set_reload_jitter_sec", "value"), &BulletSpawner2D::set_reload_jitter_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reload_jitter_sec"), "set_reload_jitter_sec", "get_reload_jitter_sec");
	// Seed lives directly under its jitter amount so the pair reads as one feature.
	ClassDB::bind_method(D_METHOD("get_reload_jitter_seed"), &BulletSpawner2D::get_reload_jitter_seed);
	ClassDB::bind_method(D_METHOD("set_reload_jitter_seed", "value"), &BulletSpawner2D::set_reload_jitter_seed);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "reload_jitter_seed"), "set_reload_jitter_seed", "get_reload_jitter_seed");

	ClassDB::bind_method(D_METHOD("get_max_live_bullets"), &BulletSpawner2D::get_max_live_bullets);
	ClassDB::bind_method(D_METHOD("set_max_live_bullets", "value"), &BulletSpawner2D::set_max_live_bullets);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_live_bullets"), "set_max_live_bullets", "get_max_live_bullets");
	ClassDB::bind_method(D_METHOD("get_burst_enabled"), &BulletSpawner2D::get_burst_enabled);
	ClassDB::bind_method(D_METHOD("set_burst_enabled", "value"), &BulletSpawner2D::set_burst_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "burst_enabled"), "set_burst_enabled", "get_burst_enabled");

	ClassDB::bind_method(D_METHOD("get_burst_count"), &BulletSpawner2D::get_burst_count);
	ClassDB::bind_method(D_METHOD("set_burst_count", "value"), &BulletSpawner2D::set_burst_count);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "burst_count"), "set_burst_count", "get_burst_count");

	ClassDB::bind_method(D_METHOD("get_burst_interval_sec"), &BulletSpawner2D::get_burst_interval_sec);
	ClassDB::bind_method(D_METHOD("set_burst_interval_sec", "value"), &BulletSpawner2D::set_burst_interval_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "burst_interval_sec"), "set_burst_interval_sec", "get_burst_interval_sec");

	ClassDB::bind_method(D_METHOD("get_burst_alternate_mirror"), &BulletSpawner2D::get_burst_alternate_mirror);
	ClassDB::bind_method(D_METHOD("set_burst_alternate_mirror", "value"), &BulletSpawner2D::set_burst_alternate_mirror);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "burst_alternate_mirror"), "set_burst_alternate_mirror", "get_burst_alternate_mirror");

	ClassDB::bind_method(D_METHOD("get_telegraph_enabled"), &BulletSpawner2D::get_telegraph_enabled);
	ClassDB::bind_method(D_METHOD("set_telegraph_enabled", "value"), &BulletSpawner2D::set_telegraph_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "telegraph_enabled"), "set_telegraph_enabled", "get_telegraph_enabled");

	ClassDB::bind_method(D_METHOD("get_telegraph_sec"), &BulletSpawner2D::get_telegraph_sec);
	ClassDB::bind_method(D_METHOD("set_telegraph_sec", "value"), &BulletSpawner2D::set_telegraph_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "telegraph_sec"), "set_telegraph_sec", "get_telegraph_sec");

	ClassDB::bind_method(D_METHOD("get_spin_enabled"), &BulletSpawner2D::get_spin_enabled);
	ClassDB::bind_method(D_METHOD("set_spin_enabled", "value"), &BulletSpawner2D::set_spin_enabled);
	ADD_GROUP("Spin", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "spin_enabled"), "set_spin_enabled", "get_spin_enabled");

	ClassDB::bind_method(D_METHOD("get_spin_mode"), &BulletSpawner2D::get_spin_mode);
	ClassDB::bind_method(D_METHOD("set_spin_mode", "value"), &BulletSpawner2D::set_spin_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "spin_mode", PROPERTY_HINT_ENUM, "Continuous,Oscillate"), "set_spin_mode", "get_spin_mode");
	ClassDB::bind_method(D_METHOD("get_spin_speed_deg_per_sec"), &BulletSpawner2D::get_spin_speed_deg_per_sec);
	ClassDB::bind_method(D_METHOD("set_spin_speed_deg_per_sec", "value"), &BulletSpawner2D::set_spin_speed_deg_per_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spin_speed_deg_per_sec"), "set_spin_speed_deg_per_sec", "get_spin_speed_deg_per_sec");

	ClassDB::bind_method(D_METHOD("get_spin_amplitude_deg"), &BulletSpawner2D::get_spin_amplitude_deg);
	ClassDB::bind_method(D_METHOD("set_spin_amplitude_deg", "value"), &BulletSpawner2D::set_spin_amplitude_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spin_amplitude_deg"), "set_spin_amplitude_deg", "get_spin_amplitude_deg");

	ClassDB::bind_method(D_METHOD("get_spin_frequency_hz"), &BulletSpawner2D::get_spin_frequency_hz);
	ClassDB::bind_method(D_METHOD("set_spin_frequency_hz", "value"), &BulletSpawner2D::set_spin_frequency_hz);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spin_frequency_hz"), "set_spin_frequency_hz", "get_spin_frequency_hz");

	ClassDB::bind_method(D_METHOD("get_spin_angle_deg"), &BulletSpawner2D::get_spin_angle_deg);
	ClassDB::bind_method(D_METHOD("reset_spin_angle"), &BulletSpawner2D::reset_spin_angle);

	ClassDB::bind_method(D_METHOD("apply_pattern_preset", "preset"), &BulletSpawner2D::apply_pattern_preset);
	ClassDB::bind_method(D_METHOD("spawn_pattern_list", "entries", "simultaneous", "interval_sec"), &BulletSpawner2D::spawn_pattern_list, DEFVAL(false), DEFVAL(0.25));
	ClassDB::bind_method(D_METHOD("stop_pattern_list"), &BulletSpawner2D::stop_pattern_list);
	ClassDB::bind_method(D_METHOD("is_pattern_list_active"), &BulletSpawner2D::is_pattern_list_active);
	ClassDB::bind_method(D_METHOD("get_burst_shots_left"), &BulletSpawner2D::get_burst_shots_left);
	ClassDB::bind_method(D_METHOD("get_active_live_bullet_count"), &BulletSpawner2D::get_active_live_bullet_count);
	ClassDB::bind_method(D_METHOD("begin_burst"), &BulletSpawner2D::begin_burst);

	// Homing + orbiting signals. Same slim payload shape and handler contract
	// as the spawner lifecycle signals above: volley_homing_configured and
	// homing_targets_resolved fire synchronously inside shoot_once() (after
	// the instance is fully configured, before volley_fired);
	// volley_bullet_homing_target_reached: the owned volley forwards every
	// bullet_homing_target_reached to its owner spawner directly (live,
	// inside the factory tick, right after the volley-level emit).
	// NOTE: PROPERTY_HINT_RESOURCE_TYPE (not NODE_TYPE) carries the class name
	// to ClassDB/--doctool; see the note on the factory signals.
	ADD_SIGNAL(MethodInfo("volley_homing_configured",
			PropertyInfo(Variant::OBJECT, "volley", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D"),
			PropertyInfo(Variant::INT, "volley_index")));
	ADD_SIGNAL(MethodInfo("homing_targets_resolved",
			PropertyInfo(Variant::OBJECT, "volley", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D"),
			PropertyInfo(Variant::ARRAY, "targets")));
	ADD_SIGNAL(MethodInfo("volley_bullet_homing_target_reached",
			PropertyInfo(Variant::OBJECT, "volley", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D"),
			PropertyInfo(Variant::INT, "bullet_index"),
			PropertyInfo(Variant::OBJECT, "target", PROPERTY_HINT_RESOURCE_TYPE, "Node2D"),
			PropertyInfo(Variant::VECTOR2, "target_global_position")));

	ClassDB::bind_method(D_METHOD("get_homing_enabled"), &BulletSpawner2D::get_homing_enabled);
	ClassDB::bind_method(D_METHOD("set_homing_enabled", "value"), &BulletSpawner2D::set_homing_enabled);
	ADD_GROUP("Homing", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_enabled"), "set_homing_enabled", "get_homing_enabled");

	ClassDB::bind_method(D_METHOD("get_homing_mode"), &BulletSpawner2D::get_homing_mode);
	ClassDB::bind_method(D_METHOD("set_homing_mode", "value"), &BulletSpawner2D::set_homing_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "homing_mode", PROPERTY_HINT_ENUM, "Shared,Per Bullet"), "set_homing_mode", "get_homing_mode");

	ClassDB::bind_method(D_METHOD("get_homing_target_source"), &BulletSpawner2D::get_homing_target_source);
	ClassDB::bind_method(D_METHOD("set_homing_target_source", "value"), &BulletSpawner2D::set_homing_target_source);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "homing_target_source", PROPERTY_HINT_ENUM, "Node Group,Mouse,Global Position,Node Path,Node Name,Node Children"), "set_homing_target_source", "get_homing_target_source");

	ClassDB::bind_method(D_METHOD("get_homing_node_group"), &BulletSpawner2D::get_homing_node_group);
	ClassDB::bind_method(D_METHOD("set_homing_node_group", "value"), &BulletSpawner2D::set_homing_node_group);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "homing_node_group"), "set_homing_node_group", "get_homing_node_group");

	ClassDB::bind_method(D_METHOD("get_homing_filter_group"), &BulletSpawner2D::get_homing_filter_group);
	ClassDB::bind_method(D_METHOD("set_homing_filter_group", "value"), &BulletSpawner2D::set_homing_filter_group);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "homing_filter_group"), "set_homing_filter_group", "get_homing_filter_group");

	ClassDB::bind_method(D_METHOD("get_homing_target_selection"), &BulletSpawner2D::get_homing_target_selection);
	ClassDB::bind_method(D_METHOD("set_homing_target_selection", "value"), &BulletSpawner2D::set_homing_target_selection);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "homing_target_selection", PROPERTY_HINT_ENUM, "Nearest,Random,First,Round Robin,Distribute"), "set_homing_target_selection", "get_homing_target_selection");
	// Seed lives directly under its selection mode; only shown for Random (see _validate_property).
	ClassDB::bind_method(D_METHOD("get_homing_random_seed"), &BulletSpawner2D::get_homing_random_seed);
	ClassDB::bind_method(D_METHOD("set_homing_random_seed", "value"), &BulletSpawner2D::set_homing_random_seed);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "homing_random_seed"), "set_homing_random_seed", "get_homing_random_seed");

	ClassDB::bind_method(D_METHOD("get_homing_max_targets"), &BulletSpawner2D::get_homing_max_targets);
	ClassDB::bind_method(D_METHOD("set_homing_max_targets", "value"), &BulletSpawner2D::set_homing_max_targets);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "homing_max_targets"), "set_homing_max_targets", "get_homing_max_targets");

	ClassDB::bind_method(D_METHOD("get_homing_max_detection_range"), &BulletSpawner2D::get_homing_max_detection_range);
	ClassDB::bind_method(D_METHOD("set_homing_max_detection_range", "value"), &BulletSpawner2D::set_homing_max_detection_range);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_max_detection_range"), "set_homing_max_detection_range", "get_homing_max_detection_range");

	ClassDB::bind_method(D_METHOD("get_homing_global_position"), &BulletSpawner2D::get_homing_global_position);
	ClassDB::bind_method(D_METHOD("set_homing_global_position", "value"), &BulletSpawner2D::set_homing_global_position);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "homing_global_position"), "set_homing_global_position", "get_homing_global_position");

	ClassDB::bind_method(D_METHOD("get_homing_target_path"), &BulletSpawner2D::get_homing_target_path);
	ClassDB::bind_method(D_METHOD("set_homing_target_path", "path"), &BulletSpawner2D::set_homing_target_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "homing_target_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node2D"), "set_homing_target_path", "get_homing_target_path");

	ClassDB::bind_method(D_METHOD("get_homing_node_name"), &BulletSpawner2D::get_homing_node_name);
	ClassDB::bind_method(D_METHOD("set_homing_node_name", "value"), &BulletSpawner2D::set_homing_node_name);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "homing_node_name"), "set_homing_node_name", "get_homing_node_name");

	ClassDB::bind_method(D_METHOD("get_homing_node_name_match_mode"), &BulletSpawner2D::get_homing_node_name_match_mode);
	ClassDB::bind_method(D_METHOD("set_homing_node_name_match_mode", "value"), &BulletSpawner2D::set_homing_node_name_match_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "homing_node_name_match_mode", PROPERTY_HINT_ENUM, "Exact,Contains,Starts With,Ends With"), "set_homing_node_name_match_mode", "get_homing_node_name_match_mode");

	ClassDB::bind_method(D_METHOD("get_homing_node_name_case_sensitive"), &BulletSpawner2D::get_homing_node_name_case_sensitive);
	ClassDB::bind_method(D_METHOD("set_homing_node_name_case_sensitive", "value"), &BulletSpawner2D::set_homing_node_name_case_sensitive);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_node_name_case_sensitive"), "set_homing_node_name_case_sensitive", "get_homing_node_name_case_sensitive");

	ClassDB::bind_method(D_METHOD("get_homing_children_parent_path"), &BulletSpawner2D::get_homing_children_parent_path);
	ClassDB::bind_method(D_METHOD("set_homing_children_parent_path", "path"), &BulletSpawner2D::set_homing_children_parent_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "homing_children_parent_path"), "set_homing_children_parent_path", "get_homing_children_parent_path");

	ClassDB::bind_method(D_METHOD("get_homing_children_recursive"), &BulletSpawner2D::get_homing_children_recursive);
	ClassDB::bind_method(D_METHOD("set_homing_children_recursive", "value"), &BulletSpawner2D::set_homing_children_recursive);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_children_recursive"), "set_homing_children_recursive", "get_homing_children_recursive");

	ClassDB::bind_method(D_METHOD("get_homing_smoothing"), &BulletSpawner2D::get_homing_smoothing);
	ClassDB::bind_method(D_METHOD("set_homing_smoothing", "value"), &BulletSpawner2D::set_homing_smoothing);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_smoothing"), "set_homing_smoothing", "get_homing_smoothing");

	ClassDB::bind_method(D_METHOD("get_homing_update_interval"), &BulletSpawner2D::get_homing_update_interval);
	ClassDB::bind_method(D_METHOD("set_homing_update_interval", "value"), &BulletSpawner2D::set_homing_update_interval);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_update_interval"), "set_homing_update_interval", "get_homing_update_interval");

	ClassDB::bind_method(D_METHOD("get_homing_distance_before_reached"), &BulletSpawner2D::get_homing_distance_before_reached);
	ClassDB::bind_method(D_METHOD("set_homing_distance_before_reached", "value"), &BulletSpawner2D::set_homing_distance_before_reached);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_distance_before_reached"), "set_homing_distance_before_reached", "get_homing_distance_before_reached");

	ClassDB::bind_method(D_METHOD("get_homing_take_control_of_texture_rotation"), &BulletSpawner2D::get_homing_take_control_of_texture_rotation);
	ClassDB::bind_method(D_METHOD("set_homing_take_control_of_texture_rotation", "value"), &BulletSpawner2D::set_homing_take_control_of_texture_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_take_control_of_texture_rotation"), "set_homing_take_control_of_texture_rotation", "get_homing_take_control_of_texture_rotation");

	ClassDB::bind_method(D_METHOD("get_homing_auto_pop_after_target_reached"), &BulletSpawner2D::get_homing_auto_pop_after_target_reached);
	ClassDB::bind_method(D_METHOD("set_homing_auto_pop_after_target_reached", "value"), &BulletSpawner2D::set_homing_auto_pop_after_target_reached);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_auto_pop_after_target_reached"), "set_homing_auto_pop_after_target_reached", "get_homing_auto_pop_after_target_reached");

	ClassDB::bind_method(D_METHOD("get_homing_per_bullet_smoothing_enabled"), &BulletSpawner2D::get_homing_per_bullet_smoothing_enabled);
	ClassDB::bind_method(D_METHOD("set_homing_per_bullet_smoothing_enabled", "value"), &BulletSpawner2D::set_homing_per_bullet_smoothing_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_per_bullet_smoothing_enabled"), "set_homing_per_bullet_smoothing_enabled", "get_homing_per_bullet_smoothing_enabled");

	ClassDB::bind_method(D_METHOD("get_homing_smoothing_start"), &BulletSpawner2D::get_homing_smoothing_start);
	ClassDB::bind_method(D_METHOD("set_homing_smoothing_start", "value"), &BulletSpawner2D::set_homing_smoothing_start);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_smoothing_start"), "set_homing_smoothing_start", "get_homing_smoothing_start");

	ClassDB::bind_method(D_METHOD("get_homing_smoothing_step"), &BulletSpawner2D::get_homing_smoothing_step);
	ClassDB::bind_method(D_METHOD("set_homing_smoothing_step", "value"), &BulletSpawner2D::set_homing_smoothing_step);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_smoothing_step"), "set_homing_smoothing_step", "get_homing_smoothing_step");

	ClassDB::bind_method(D_METHOD("get_homing_retarget_mode"), &BulletSpawner2D::get_homing_retarget_mode);
	ClassDB::bind_method(D_METHOD("set_homing_retarget_mode", "value"), &BulletSpawner2D::set_homing_retarget_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "homing_retarget_mode", PROPERTY_HINT_ENUM, "Off,On Interval"), "set_homing_retarget_mode", "get_homing_retarget_mode");

	ClassDB::bind_method(D_METHOD("get_homing_retarget_interval_sec"), &BulletSpawner2D::get_homing_retarget_interval_sec);
	ClassDB::bind_method(D_METHOD("set_homing_retarget_interval_sec", "value"), &BulletSpawner2D::set_homing_retarget_interval_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_retarget_interval_sec"), "set_homing_retarget_interval_sec", "get_homing_retarget_interval_sec");

	ClassDB::bind_method(D_METHOD("get_homing_retarget_phase"), &BulletSpawner2D::get_homing_retarget_phase);
	ClassDB::bind_method(D_METHOD("set_homing_retarget_phase", "value"), &BulletSpawner2D::set_homing_retarget_phase);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_retarget_phase"), "set_homing_retarget_phase", "get_homing_retarget_phase");

	ClassDB::bind_method(D_METHOD("get_homing_retarget_previous_volleys"), &BulletSpawner2D::get_homing_retarget_previous_volleys);
	ClassDB::bind_method(D_METHOD("set_homing_retarget_previous_volleys", "value"), &BulletSpawner2D::set_homing_retarget_previous_volleys);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_retarget_previous_volleys"), "set_homing_retarget_previous_volleys", "get_homing_retarget_previous_volleys");

	ClassDB::bind_method(D_METHOD("get_homing_delay_sec"), &BulletSpawner2D::get_homing_delay_sec);
	ClassDB::bind_method(D_METHOD("set_homing_delay_sec", "value"), &BulletSpawner2D::set_homing_delay_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_delay_sec"), "set_homing_delay_sec", "get_homing_delay_sec");

	ClassDB::bind_method(D_METHOD("get_homing_duration_sec"), &BulletSpawner2D::get_homing_duration_sec);
	ClassDB::bind_method(D_METHOD("set_homing_duration_sec", "value"), &BulletSpawner2D::set_homing_duration_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_duration_sec"), "set_homing_duration_sec", "get_homing_duration_sec");

	ClassDB::bind_method(D_METHOD("get_homing_lose_range_px"), &BulletSpawner2D::get_homing_lose_range_px);
	ClassDB::bind_method(D_METHOD("set_homing_lose_range_px", "value"), &BulletSpawner2D::set_homing_lose_range_px);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_lose_range_px"), "set_homing_lose_range_px", "get_homing_lose_range_px");

	ClassDB::bind_method(D_METHOD("get_homing_fire_arc_deg"), &BulletSpawner2D::get_homing_fire_arc_deg);
	ClassDB::bind_method(D_METHOD("set_homing_fire_arc_deg", "value"), &BulletSpawner2D::set_homing_fire_arc_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_fire_arc_deg"), "set_homing_fire_arc_deg", "get_homing_fire_arc_deg");

	ClassDB::bind_method(D_METHOD("get_orbiting_enabled"), &BulletSpawner2D::get_orbiting_enabled);
	ClassDB::bind_method(D_METHOD("set_orbiting_enabled", "value"), &BulletSpawner2D::set_orbiting_enabled);
	ADD_GROUP("Orbiting", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "orbiting_enabled"), "set_orbiting_enabled", "get_orbiting_enabled");

	ClassDB::bind_method(D_METHOD("get_orbiting_radius"), &BulletSpawner2D::get_orbiting_radius);
	ClassDB::bind_method(D_METHOD("set_orbiting_radius", "value"), &BulletSpawner2D::set_orbiting_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "orbiting_radius"), "set_orbiting_radius", "get_orbiting_radius");

	ClassDB::bind_method(D_METHOD("get_orbiting_direction"), &BulletSpawner2D::get_orbiting_direction);
	ClassDB::bind_method(D_METHOD("set_orbiting_direction", "value"), &BulletSpawner2D::set_orbiting_direction);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "orbiting_direction", PROPERTY_HINT_ENUM, "Dont Move,Orbit Left,Orbit Right,Orbit Random"), "set_orbiting_direction", "get_orbiting_direction");

	ClassDB::bind_method(D_METHOD("get_orbiting_texture_rotation"), &BulletSpawner2D::get_orbiting_texture_rotation);
	ClassDB::bind_method(D_METHOD("set_orbiting_texture_rotation", "value"), &BulletSpawner2D::set_orbiting_texture_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "orbiting_texture_rotation", PROPERTY_HINT_ENUM, "Face Target,Face Opposite Target,Face Orbiting Direction,Face Opposite Orbiting Direction"), "set_orbiting_texture_rotation", "get_orbiting_texture_rotation");

	ClassDB::bind_method(D_METHOD("get_orbiting_radius_linear_enabled"), &BulletSpawner2D::get_orbiting_radius_linear_enabled);
	ClassDB::bind_method(D_METHOD("set_orbiting_radius_linear_enabled", "value"), &BulletSpawner2D::set_orbiting_radius_linear_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "orbiting_radius_linear_enabled"), "set_orbiting_radius_linear_enabled", "get_orbiting_radius_linear_enabled");

	ClassDB::bind_method(D_METHOD("get_orbiting_radius_start"), &BulletSpawner2D::get_orbiting_radius_start);
	ClassDB::bind_method(D_METHOD("set_orbiting_radius_start", "value"), &BulletSpawner2D::set_orbiting_radius_start);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "orbiting_radius_start"), "set_orbiting_radius_start", "get_orbiting_radius_start");

	ClassDB::bind_method(D_METHOD("get_orbiting_radius_step"), &BulletSpawner2D::get_orbiting_radius_step);
	ClassDB::bind_method(D_METHOD("set_orbiting_radius_step", "value"), &BulletSpawner2D::set_orbiting_radius_step);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "orbiting_radius_step"), "set_orbiting_radius_step", "get_orbiting_radius_step");

	ClassDB::bind_method(D_METHOD("get_orbiting_follow_mode"), &BulletSpawner2D::get_orbiting_follow_mode);
	ClassDB::bind_method(D_METHOD("set_orbiting_follow_mode", "value"), &BulletSpawner2D::set_orbiting_follow_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "orbiting_follow_mode", PROPERTY_HINT_ENUM, "Follow Target,Follow Deadzone,Anchored"), "set_orbiting_follow_mode", "get_orbiting_follow_mode");

	ClassDB::bind_method(D_METHOD("get_orbiting_follow_deadzone"), &BulletSpawner2D::get_orbiting_follow_deadzone);
	ClassDB::bind_method(D_METHOD("set_orbiting_follow_deadzone", "value"), &BulletSpawner2D::set_orbiting_follow_deadzone);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "orbiting_follow_deadzone"), "set_orbiting_follow_deadzone", "get_orbiting_follow_deadzone");

	ClassDB::bind_method(D_METHOD("get_orbiting_lock_policy"), &BulletSpawner2D::get_orbiting_lock_policy);
	ClassDB::bind_method(D_METHOD("set_orbiting_lock_policy", "value"), &BulletSpawner2D::set_orbiting_lock_policy);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "orbiting_lock_policy", PROPERTY_HINT_ENUM, "Relock Always,Stay Locked,Relock On Target Change"), "set_orbiting_lock_policy", "get_orbiting_lock_policy");

	ClassDB::bind_method(D_METHOD("get_orbiting_rigid_follow"), &BulletSpawner2D::get_orbiting_rigid_follow);
	ClassDB::bind_method(D_METHOD("set_orbiting_rigid_follow", "value"), &BulletSpawner2D::set_orbiting_rigid_follow);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "orbiting_rigid_follow"), "set_orbiting_rigid_follow", "get_orbiting_rigid_follow");

	ClassDB::bind_method(D_METHOD("resolve_homing_targets", "quiet", "advance_round_robin"), &BulletSpawner2D::resolve_homing_targets, DEFVAL(false), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("retarget_live_volleys"), &BulletSpawner2D::retarget_live_volleys);
	ClassDB::bind_method(D_METHOD("get_live_volley_count"), &BulletSpawner2D::get_live_volley_count);
	ClassDB::bind_method(D_METHOD("get_live_volleys"), &BulletSpawner2D::get_live_volleys);
	ClassDB::bind_method(D_METHOD("get_tracked_volley_count"), &BulletSpawner2D::get_tracked_volley_count);
	ClassDB::bind_method(D_METHOD("forget_tracked_volleys"), &BulletSpawner2D::forget_tracked_volleys);
	ClassDB::bind_method(D_METHOD("clear_active_bullets", "fire_clear_effects"), &BulletSpawner2D::clear_active_bullets, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("debug_get_layer_rings"), &BulletSpawner2D::debug_get_layer_rings);
	ClassDB::bind_method(D_METHOD("debug_get_preview_dot_points"), &BulletSpawner2D::debug_get_preview_dot_points);
	ClassDB::bind_method(D_METHOD("debug_get_preview_track_points"), &BulletSpawner2D::debug_get_preview_track_points);
	ClassDB::bind_method(D_METHOD("debug_get_preview_track_closed"), &BulletSpawner2D::debug_get_preview_track_closed);
	ClassDB::bind_method(D_METHOD("debug_check_layer_coincidence", "tolerance_px"), &BulletSpawner2D::debug_check_layer_coincidence, DEFVAL(1.0));
	ClassDB::bind_method(D_METHOD("debug_get_retarget_countdown"), &BulletSpawner2D::debug_get_retarget_countdown);
	ClassDB::bind_method(D_METHOD("adopt_live_volley", "volley"), &BulletSpawner2D::adopt_live_volley);
	ClassDB::bind_method(D_METHOD("clear_live_volleys_homing"), &BulletSpawner2D::clear_live_volleys_homing);
	ClassDB::bind_method(D_METHOD("override_live_volleys_velocity", "new_velocity"), &BulletSpawner2D::override_live_volleys_velocity);

	// Need this in order to expose the enum constants to Godot Engine
	BIND_ENUM_CONSTANT(HOMING_SHARED);
	BIND_ENUM_CONSTANT(HOMING_PER_BULLET);
	BIND_ENUM_CONSTANT(HOMING_SOURCE_NODE_GROUP);
	BIND_ENUM_CONSTANT(HOMING_SOURCE_MOUSE);
	BIND_ENUM_CONSTANT(HOMING_SOURCE_GLOBAL_POSITION);
	BIND_ENUM_CONSTANT(HOMING_SOURCE_NODE_PATH);
	BIND_ENUM_CONSTANT(HOMING_SOURCE_NODE_NAME);
	BIND_ENUM_CONSTANT(HOMING_SOURCE_NODE_CHILDREN);
	BIND_ENUM_CONSTANT(HOMING_NAME_MATCH_EXACT);
	BIND_ENUM_CONSTANT(HOMING_NAME_MATCH_CONTAINS);
	BIND_ENUM_CONSTANT(HOMING_NAME_MATCH_STARTS_WITH);
	BIND_ENUM_CONSTANT(HOMING_NAME_MATCH_ENDS_WITH);
	BIND_ENUM_CONSTANT(HOMING_SELECT_NEAREST);
	BIND_ENUM_CONSTANT(HOMING_SELECT_RANDOM);
	BIND_ENUM_CONSTANT(HOMING_SELECT_FIRST);
	BIND_ENUM_CONSTANT(HOMING_SELECT_ROUND_ROBIN);
	BIND_ENUM_CONSTANT(HOMING_SELECT_DISTRIBUTE);
	BIND_ENUM_CONSTANT(HOMING_RETARGET_OFF);
	BIND_ENUM_CONSTANT(HOMING_RETARGET_ON_INTERVAL);
	BIND_ENUM_CONSTANT(GRAZE_SOURCE_NODE_GROUP);
	BIND_ENUM_CONSTANT(GRAZE_SOURCE_NODE_PATH);
	BIND_ENUM_CONSTANT(GRAZE_SOURCE_NODE_NAME);
	BIND_ENUM_CONSTANT(GRAZE_SOURCE_NODE_CHILDREN);
	BIND_ENUM_CONSTANT(GRAZE_SOURCE_MOUSE);
	BIND_ENUM_CONSTANT(GRAZE_SOURCE_GLOBAL_POSITIONS);

	// Graze (bullet_spawner2d_graze.cpp). The signals bubble: they fire here
	// first (while this spawner lives), then on the BulletFactory2D, which
	// receives every graze. Same payloads on both. Live inside the factory
	// tick, right after the move: the bullet is alive in the handler.
	ADD_SIGNAL(MethodInfo("bullet_grazed",
			PropertyInfo(Variant::OBJECT, "target", PROPERTY_HINT_RESOURCE_TYPE, "Node2D"),
			PropertyInfo(Variant::OBJECT, "volley", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D"),
			PropertyInfo(Variant::INT, "bullet_index"),
			PropertyInfo(Variant::OBJECT, "zone", PROPERTY_HINT_RESOURCE_TYPE, "BulletGrazeZone2D"),
			PropertyInfo(Variant::INT, "ring_index")));
	ADD_SIGNAL(MethodInfo("bullet_graze_exited",
			PropertyInfo(Variant::OBJECT, "target", PROPERTY_HINT_RESOURCE_TYPE, "Node2D"),
			PropertyInfo(Variant::OBJECT, "volley", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D"),
			PropertyInfo(Variant::INT, "bullet_index"),
			PropertyInfo(Variant::OBJECT, "zone", PROPERTY_HINT_RESOURCE_TYPE, "BulletGrazeZone2D"),
			PropertyInfo(Variant::INT, "deepest_ring_index")));
	ClassDB::bind_method(D_METHOD("get_graze_enabled"), &BulletSpawner2D::get_graze_enabled);
	ClassDB::bind_method(D_METHOD("set_graze_enabled", "value"), &BulletSpawner2D::set_graze_enabled);
	ClassDB::bind_method(D_METHOD("get_graze_zones"), &BulletSpawner2D::get_graze_zones);
	ClassDB::bind_method(D_METHOD("set_graze_zones", "value"), &BulletSpawner2D::set_graze_zones);
	ClassDB::bind_method(D_METHOD("get_graze_show_preview"), &BulletSpawner2D::get_graze_show_preview);
	ClassDB::bind_method(D_METHOD("set_graze_show_preview", "value"), &BulletSpawner2D::set_graze_show_preview);
	ClassDB::bind_method(D_METHOD("get_graze_preview_during_runtime"), &BulletSpawner2D::get_graze_preview_during_runtime);
	ClassDB::bind_method(D_METHOD("set_graze_preview_during_runtime", "value"), &BulletSpawner2D::set_graze_preview_during_runtime);
	ClassDB::bind_method(D_METHOD("get_graze_preview_line_width"), &BulletSpawner2D::get_graze_preview_line_width);
	ClassDB::bind_method(D_METHOD("set_graze_preview_line_width", "value"), &BulletSpawner2D::set_graze_preview_line_width);
	ClassDB::bind_method(D_METHOD("get_graze_target_source"), &BulletSpawner2D::get_graze_target_source);
	ClassDB::bind_method(D_METHOD("set_graze_target_source", "value"), &BulletSpawner2D::set_graze_target_source);
	ClassDB::bind_method(D_METHOD("get_graze_node_group"), &BulletSpawner2D::get_graze_node_group);
	ClassDB::bind_method(D_METHOD("set_graze_node_group", "value"), &BulletSpawner2D::set_graze_node_group);
	ClassDB::bind_method(D_METHOD("get_graze_filter_group"), &BulletSpawner2D::get_graze_filter_group);
	ClassDB::bind_method(D_METHOD("set_graze_filter_group", "value"), &BulletSpawner2D::set_graze_filter_group);
	ClassDB::bind_method(D_METHOD("get_graze_target_path"), &BulletSpawner2D::get_graze_target_path);
	ClassDB::bind_method(D_METHOD("set_graze_target_path", "path"), &BulletSpawner2D::set_graze_target_path);
	ClassDB::bind_method(D_METHOD("get_graze_node_name"), &BulletSpawner2D::get_graze_node_name);
	ClassDB::bind_method(D_METHOD("set_graze_node_name", "value"), &BulletSpawner2D::set_graze_node_name);
	ClassDB::bind_method(D_METHOD("get_graze_node_name_match_mode"), &BulletSpawner2D::get_graze_node_name_match_mode);
	ClassDB::bind_method(D_METHOD("set_graze_node_name_match_mode", "value"), &BulletSpawner2D::set_graze_node_name_match_mode);
	ClassDB::bind_method(D_METHOD("get_graze_node_name_case_sensitive"), &BulletSpawner2D::get_graze_node_name_case_sensitive);
	ClassDB::bind_method(D_METHOD("set_graze_node_name_case_sensitive", "value"), &BulletSpawner2D::set_graze_node_name_case_sensitive);
	ClassDB::bind_method(D_METHOD("get_graze_children_parent_path"), &BulletSpawner2D::get_graze_children_parent_path);
	ClassDB::bind_method(D_METHOD("set_graze_children_parent_path", "path"), &BulletSpawner2D::set_graze_children_parent_path);
	ClassDB::bind_method(D_METHOD("get_graze_children_recursive"), &BulletSpawner2D::get_graze_children_recursive);
	ClassDB::bind_method(D_METHOD("set_graze_children_recursive", "value"), &BulletSpawner2D::set_graze_children_recursive);
	ClassDB::bind_method(D_METHOD("get_graze_global_positions"), &BulletSpawner2D::get_graze_global_positions);
	ClassDB::bind_method(D_METHOD("set_graze_global_positions", "value"), &BulletSpawner2D::set_graze_global_positions);
	ClassDB::bind_method(D_METHOD("get_graze_update_interval"), &BulletSpawner2D::get_graze_update_interval);
	ClassDB::bind_method(D_METHOD("set_graze_update_interval", "value"), &BulletSpawner2D::set_graze_update_interval);
	ClassDB::bind_method(D_METHOD("refresh_graze_targets"), &BulletSpawner2D::refresh_graze_targets);
	ClassDB::bind_method(D_METHOD("resolve_graze_targets"), &BulletSpawner2D::resolve_graze_targets);
	ClassDB::bind_method(D_METHOD("debug_get_graze_preview_circles"), &BulletSpawner2D::debug_get_graze_preview_circles);
	ClassDB::bind_method(D_METHOD("debug_get_graze_preview_stats"), &BulletSpawner2D::debug_get_graze_preview_stats);
	ClassDB::bind_method(D_METHOD("debug_get_graze_detector_stats"), &BulletSpawner2D::debug_get_graze_detector_stats);
	ADD_GROUP("Graze", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "graze_enabled"), "set_graze_enabled", "get_graze_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "graze_zones", PROPERTY_HINT_ARRAY_TYPE, "BulletGrazeZone2D"), "set_graze_zones", "get_graze_zones");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "graze_target_source", PROPERTY_HINT_ENUM, "Node Group,Node Path,Node Name,Node Children,Mouse,Global Positions"), "set_graze_target_source", "get_graze_target_source");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "graze_node_group"), "set_graze_node_group", "get_graze_node_group");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "graze_filter_group"), "set_graze_filter_group", "get_graze_filter_group");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "graze_target_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node2D"), "set_graze_target_path", "get_graze_target_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "graze_node_name"), "set_graze_node_name", "get_graze_node_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "graze_node_name_match_mode", PROPERTY_HINT_ENUM, "Exact,Contains,Starts With,Ends With"), "set_graze_node_name_match_mode", "get_graze_node_name_match_mode");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "graze_node_name_case_sensitive"), "set_graze_node_name_case_sensitive", "get_graze_node_name_case_sensitive");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "graze_children_parent_path"), "set_graze_children_parent_path", "get_graze_children_parent_path");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "graze_children_recursive"), "set_graze_children_recursive", "get_graze_children_recursive");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR2_ARRAY, "graze_global_positions"), "set_graze_global_positions", "get_graze_global_positions");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "graze_update_interval", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s"), "set_graze_update_interval", "get_graze_update_interval");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "graze_show_preview"), "set_graze_show_preview", "get_graze_show_preview");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "graze_preview_during_runtime"), "set_graze_preview_during_runtime", "get_graze_preview_during_runtime");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "graze_preview_line_width", PROPERTY_HINT_RANGE, "0.5,16,0.5,or_greater"), "set_graze_preview_line_width", "get_graze_preview_line_width");

	ClassDB::bind_method(D_METHOD("get_show_pattern_preview"), &BulletSpawner2D::get_show_pattern_preview);
	ClassDB::bind_method(D_METHOD("set_show_pattern_preview", "value"), &BulletSpawner2D::set_show_pattern_preview);
	ADD_GROUP("Preview", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "show_pattern_preview"), "set_show_pattern_preview", "get_show_pattern_preview");

	ClassDB::bind_method(D_METHOD("get_show_preview_during_runtime"), &BulletSpawner2D::get_show_preview_during_runtime);
	ClassDB::bind_method(D_METHOD("set_show_preview_during_runtime", "value"), &BulletSpawner2D::set_show_preview_during_runtime);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "show_preview_during_runtime"), "set_show_preview_during_runtime", "get_show_preview_during_runtime");

	ClassDB::bind_method(D_METHOD("get_preview_dot_color"), &BulletSpawner2D::get_preview_dot_color);
	ClassDB::bind_method(D_METHOD("set_preview_dot_color", "value"), &BulletSpawner2D::set_preview_dot_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "preview_dot_color"), "set_preview_dot_color", "get_preview_dot_color");

	ClassDB::bind_method(D_METHOD("get_preview_arrow_color"), &BulletSpawner2D::get_preview_arrow_color);
	ClassDB::bind_method(D_METHOD("set_preview_arrow_color", "value"), &BulletSpawner2D::set_preview_arrow_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "preview_arrow_color"), "set_preview_arrow_color", "get_preview_arrow_color");

	ClassDB::bind_method(D_METHOD("get_preview_first_dot_color"), &BulletSpawner2D::get_preview_first_dot_color);
	ClassDB::bind_method(D_METHOD("set_preview_first_dot_color", "value"), &BulletSpawner2D::set_preview_first_dot_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "preview_first_dot_color"), "set_preview_first_dot_color", "get_preview_first_dot_color");

	ClassDB::bind_method(D_METHOD("get_preview_path_color"), &BulletSpawner2D::get_preview_path_color);
	ClassDB::bind_method(D_METHOD("set_preview_path_color", "value"), &BulletSpawner2D::set_preview_path_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "preview_path_color"), "set_preview_path_color", "get_preview_path_color");

	ClassDB::bind_method(D_METHOD("get_preview_layer_path_color"), &BulletSpawner2D::get_preview_layer_path_color);
	ClassDB::bind_method(D_METHOD("set_preview_layer_path_color", "value"), &BulletSpawner2D::set_preview_layer_path_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "preview_layer_path_color"), "set_preview_layer_path_color", "get_preview_layer_path_color");

	ClassDB::bind_method(D_METHOD("get_preview_path_width"), &BulletSpawner2D::get_preview_path_width);
	ClassDB::bind_method(D_METHOD("set_preview_path_width", "value"), &BulletSpawner2D::set_preview_path_width);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "preview_path_width"), "set_preview_path_width", "get_preview_path_width");

	ClassDB::bind_method(D_METHOD("get_preview_dot_radius"), &BulletSpawner2D::get_preview_dot_radius);
	ClassDB::bind_method(D_METHOD("set_preview_dot_radius", "value"), &BulletSpawner2D::set_preview_dot_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "preview_dot_radius"), "set_preview_dot_radius", "get_preview_dot_radius");

	ClassDB::bind_method(D_METHOD("get_preview_arrow_gap"), &BulletSpawner2D::get_preview_arrow_gap);
	ClassDB::bind_method(D_METHOD("set_preview_arrow_gap", "value"), &BulletSpawner2D::set_preview_arrow_gap);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "preview_arrow_gap"), "set_preview_arrow_gap", "get_preview_arrow_gap");

	ClassDB::bind_method(D_METHOD("get_preview_arrow_length"), &BulletSpawner2D::get_preview_arrow_length);
	ClassDB::bind_method(D_METHOD("set_preview_arrow_length", "value"), &BulletSpawner2D::set_preview_arrow_length);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "preview_arrow_length"), "set_preview_arrow_length", "get_preview_arrow_length");

	ClassDB::bind_method(D_METHOD("get_preview_arrow_width"), &BulletSpawner2D::get_preview_arrow_width);
	ClassDB::bind_method(D_METHOD("set_preview_arrow_width", "value"), &BulletSpawner2D::set_preview_arrow_width);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "preview_arrow_width"), "set_preview_arrow_width", "get_preview_arrow_width");

	ClassDB::bind_method(D_METHOD("get_preview_arrow_head_length"), &BulletSpawner2D::get_preview_arrow_head_length);
	ClassDB::bind_method(D_METHOD("set_preview_arrow_head_length", "value"), &BulletSpawner2D::set_preview_arrow_head_length);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "preview_arrow_head_length"), "set_preview_arrow_head_length", "get_preview_arrow_head_length");

	ClassDB::bind_method(D_METHOD("get_preview_arrow_head_width"), &BulletSpawner2D::get_preview_arrow_head_width);
	ClassDB::bind_method(D_METHOD("set_preview_arrow_head_width", "value"), &BulletSpawner2D::set_preview_arrow_head_width);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "preview_arrow_head_width"), "set_preview_arrow_head_width", "get_preview_arrow_head_width");

	ClassDB::bind_method(D_METHOD("get_preview_draw_collision_rings"), &BulletSpawner2D::get_preview_draw_collision_rings);
	ClassDB::bind_method(D_METHOD("set_preview_draw_collision_rings", "value"), &BulletSpawner2D::set_preview_draw_collision_rings);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "preview_draw_collision_rings"), "set_preview_draw_collision_rings", "get_preview_draw_collision_rings");
	ClassDB::bind_method(D_METHOD("get_preview_collision_ring_color"), &BulletSpawner2D::get_preview_collision_ring_color);
	ClassDB::bind_method(D_METHOD("set_preview_collision_ring_color", "value"), &BulletSpawner2D::set_preview_collision_ring_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "preview_collision_ring_color"), "set_preview_collision_ring_color", "get_preview_collision_ring_color");
	ClassDB::bind_method(D_METHOD("get_preview_collision_ring_width"), &BulletSpawner2D::get_preview_collision_ring_width);
	ClassDB::bind_method(D_METHOD("set_preview_collision_ring_width", "value"), &BulletSpawner2D::set_preview_collision_ring_width);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "preview_collision_ring_width"), "set_preview_collision_ring_width", "get_preview_collision_ring_width");

	// ---- Movement ----
#define BS_BIND_PROP(m_name)                                                        \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &BulletSpawner2D::get_##m_name); \
	ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &BulletSpawner2D::set_##m_name);
	BS_BIND_PROP(movement_enabled)
	BS_BIND_PROP(movement_path)
	BS_BIND_PROP(movement_space)
	BS_BIND_PROP(movement_loop_mode)
	BS_BIND_PROP(movement_direction)
	BS_BIND_PROP(movement_loops)
	BS_BIND_PROP(movement_timing)
	BS_BIND_PROP(movement_duration_sec)
	BS_BIND_PROP(movement_speed)
	BS_BIND_PROP(movement_transition)
	BS_BIND_PROP(movement_ease)
	BS_BIND_PROP(movement_progress_curve)
	BS_BIND_PROP(movement_start_ratio)
	BS_BIND_PROP(movement_start_delay_sec)
	BS_BIND_PROP(movement_endpoint_pause_sec)
	BS_BIND_PROP(movement_rotate_with_path)
	BS_BIND_PROP(movement_rotation_offset_deg)
	BS_BIND_PROP(movement_cubic_sampling)
	BS_BIND_PROP(movement_autostart)
	BS_BIND_PROP(inherit_movement_velocity)
	BS_BIND_PROP(movement_velocity_inherit_factor)
#undef BS_BIND_PROP
	ClassDB::bind_method(D_METHOD("get_movement_path_node"), &BulletSpawner2D::get_movement_path_node);
	ClassDB::bind_method(D_METHOD("set_movement_path_node", "node"), &BulletSpawner2D::set_movement_path_node);
	ClassDB::bind_method(D_METHOD("get_setup_warnings"), &BulletSpawner2D::get_setup_warnings);
	ClassDB::bind_method(D_METHOD("movement_play"), &BulletSpawner2D::movement_play);
	ClassDB::bind_method(D_METHOD("movement_pause"), &BulletSpawner2D::movement_pause);
	ClassDB::bind_method(D_METHOD("movement_stop", "reset_to_start"), &BulletSpawner2D::movement_stop, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("movement_seek", "ratio"), &BulletSpawner2D::movement_seek);
	ClassDB::bind_method(D_METHOD("movement_reverse"), &BulletSpawner2D::movement_reverse);
	ClassDB::bind_method(D_METHOD("is_movement_playing"), &BulletSpawner2D::is_movement_playing);
	ClassDB::bind_method(D_METHOD("get_movement_progress"), &BulletSpawner2D::get_movement_progress);
	ClassDB::bind_method(D_METHOD("get_movement_leg"), &BulletSpawner2D::get_movement_leg);
	ClassDB::bind_method(D_METHOD("get_movement_velocity"), &BulletSpawner2D::get_movement_velocity);
	ClassDB::bind_static_method("BulletSpawner2D", D_METHOD("debug_ease", "t", "transition", "ease"), &BulletSpawner2D::debug_ease);
	BIND_ENUM_CONSTANT(MOVEMENT_SPACE_ATTACH);
	BIND_ENUM_CONSTANT(MOVEMENT_SPACE_RELATIVE_TO_START);
	BIND_ENUM_CONSTANT(MOVEMENT_LOOP_ONCE);
	BIND_ENUM_CONSTANT(MOVEMENT_LOOP_LOOP);
	BIND_ENUM_CONSTANT(MOVEMENT_LOOP_PING_PONG);
	BIND_ENUM_CONSTANT(MOVEMENT_DIRECTION_FORWARD);
	BIND_ENUM_CONSTANT(MOVEMENT_DIRECTION_REVERSE);
	BIND_ENUM_CONSTANT(MOVEMENT_TIMING_DURATION);
	BIND_ENUM_CONSTANT(MOVEMENT_TIMING_SPEED);
	ADD_SIGNAL(MethodInfo("movement_started"));
	ADD_SIGNAL(MethodInfo("movement_finished"));
	ADD_SIGNAL(MethodInfo("movement_loop_completed", PropertyInfo(Variant::INT, "leg_index")));
	ADD_SIGNAL(MethodInfo("movement_endpoint_reached", PropertyInfo(Variant::BOOL, "at_end")));
	ADD_GROUP("Movement", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "movement_enabled"), "set_movement_enabled", "get_movement_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "movement_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Path2D"), "set_movement_path", "get_movement_path");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "movement_autostart"), "set_movement_autostart", "get_movement_autostart");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "movement_space", PROPERTY_HINT_ENUM, "Attach:0,Relative To Start:1"), "set_movement_space", "get_movement_space");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "movement_loop_mode", PROPERTY_HINT_ENUM, "Once:0,Loop:1,Ping Pong:2"), "set_movement_loop_mode", "get_movement_loop_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "movement_loops", PROPERTY_HINT_RANGE, "0,1000,1,or_greater"), "set_movement_loops", "get_movement_loops");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "movement_direction", PROPERTY_HINT_ENUM, "Forward:0,Reverse:1"), "set_movement_direction", "get_movement_direction");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "movement_timing", PROPERTY_HINT_ENUM, "Duration:0,Speed:1"), "set_movement_timing", "get_movement_timing");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "movement_duration_sec", PROPERTY_HINT_RANGE, "0.01,60,0.01,or_greater,suffix:s"), "set_movement_duration_sec", "get_movement_duration_sec");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "movement_speed", PROPERTY_HINT_RANGE, "1,4000,1,or_greater,suffix:px/s"), "set_movement_speed", "get_movement_speed");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "movement_transition", PROPERTY_HINT_ENUM, "Linear:0,Sine:1,Quint:2,Quart:3,Quad:4,Expo:5,Elastic:6,Cubic:7,Circ:8,Bounce:9,Back:10,Spring:11"), "set_movement_transition", "get_movement_transition");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "movement_ease", PROPERTY_HINT_ENUM, "In:0,Out:1,In Out:2,Out In:3"), "set_movement_ease", "get_movement_ease");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "movement_progress_curve", PROPERTY_HINT_RESOURCE_TYPE, "Curve"), "set_movement_progress_curve", "get_movement_progress_curve");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "movement_start_ratio", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_movement_start_ratio", "get_movement_start_ratio");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "movement_start_delay_sec", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s"), "set_movement_start_delay_sec", "get_movement_start_delay_sec");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "movement_endpoint_pause_sec", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s"), "set_movement_endpoint_pause_sec", "get_movement_endpoint_pause_sec");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "movement_rotate_with_path"), "set_movement_rotate_with_path", "get_movement_rotate_with_path");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "movement_rotation_offset_deg", PROPERTY_HINT_RANGE, "-360,360,0.1,or_less,or_greater,suffix:deg"), "set_movement_rotation_offset_deg", "get_movement_rotation_offset_deg");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "movement_cubic_sampling"), "set_movement_cubic_sampling", "get_movement_cubic_sampling");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "inherit_movement_velocity"), "set_inherit_movement_velocity", "get_inherit_movement_velocity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "movement_velocity_inherit_factor", PROPERTY_HINT_RANGE, "-2,2,0.01,or_less,or_greater"), "set_movement_velocity_inherit_factor", "get_movement_velocity_inherit_factor");

	ClassDB::bind_method(D_METHOD("get_pattern_cache_mode"), &BulletSpawner2D::get_pattern_cache_mode);
	ClassDB::bind_method(D_METHOD("set_pattern_cache_mode", "value"), &BulletSpawner2D::set_pattern_cache_mode);
	ADD_GROUP("Performance", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "pattern_cache_mode", PROPERTY_HINT_ENUM, "Auto:0,Off:1"), "set_pattern_cache_mode", "get_pattern_cache_mode");

	// Need this in order to expose the enum constants to Godot Engine
	BIND_ENUM_CONSTANT(PATTERN_FROM_CHILDREN);
	BIND_ENUM_CONSTANT(PATTERN_FROM_SELF);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_GRID);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_RING);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_FAN);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_SPIRAL);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_LINE);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_AIMED);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_FLOWER);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_ELLIPSE);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_RAIN);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_SCATTER);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_STAR_POLYGON);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_MULTISPIRAL);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_CROSS);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_STAR);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_HEART);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_WAVE);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_WATERFALL);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_LATTICE);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_ROSE);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_COUNTER_SPIRAL);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_CORRIDOR);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_LISSAJOUS);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_CUSTOM);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_CIRCLE);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_RECTANGLE);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_SQUARE);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_POLYGON);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_PATH2D);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_TRIANGLE);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_TRAPEZOID);
	BIND_ENUM_CONSTANT(PATTERN_FROM_HELPER_DIAMOND);
	BIND_ENUM_CONSTANT(PATTERN_FROM_LAST);
	BIND_ENUM_CONSTANT(PATH2D_DISTRIBUTION_FIXED_SPACING);
	BIND_ENUM_CONSTANT(PATH2D_DISTRIBUTION_EVEN);
	BIND_ENUM_CONSTANT(PATH2D_OVERFLOW_CLAMP);
	BIND_ENUM_CONSTANT(PATH2D_OVERFLOW_WRAP);
	BIND_ENUM_CONSTANT(PATH2D_OVERFLOW_SHRINK_TO_FIT);
	BIND_ENUM_CONSTANT(PATH2D_ANCHOR_START);
	BIND_ENUM_CONSTANT(PATH2D_ANCHOR_CENTER);
	BIND_ENUM_CONSTANT(PATH2D_ANCHOR_END);
	BIND_ENUM_CONSTANT(PATH2D_FACING_ALONG_PATH);
	BIND_ENUM_CONSTANT(PATH2D_SPACE_FOLLOW_GENERATOR);
	BIND_ENUM_CONSTANT(PATH2D_SPACE_AT_PATH2D);
	BIND_ENUM_CONSTANT(PATH2D_FACING_NORMAL_P90);
	BIND_ENUM_CONSTANT(PATH2D_FACING_NORMAL_M90);

	// Need this in order to expose the enum constants to Godot Engine
	BIND_ENUM_CONSTANT(SPIN_CONTINUOUS);
	BIND_ENUM_CONSTANT(SPIN_OSCILLATE);
	BIND_ENUM_CONSTANT(ORPHANED_VOLLEYS_KEEP_FLYING);
	BIND_ENUM_CONSTANT(ORPHANED_VOLLEYS_HAND_TO_FACTORY);
	BIND_ENUM_CONSTANT(ORPHANED_VOLLEYS_CLEAR);
	BIND_ENUM_CONSTANT(ORPHANED_VOLLEYS_REMOVE);

	ClassDB::bind_method(D_METHOD("get_volleys_fired"), &BulletSpawner2D::get_volleys_fired);
	ClassDB::bind_method(D_METHOD("collect_spawn_transforms"), &BulletSpawner2D::collect_spawn_transforms);
	ClassDB::bind_method(D_METHOD("debug_collect_spawn_transforms_uncached"), &BulletSpawner2D::debug_collect_spawn_transforms_uncached);
	ClassDB::bind_method(D_METHOD("debug_get_pattern_cache_info"), &BulletSpawner2D::debug_get_pattern_cache_info);
	ClassDB::bind_method(D_METHOD("debug_get_preview_stats"), &BulletSpawner2D::debug_get_preview_stats);
	ClassDB::bind_static_method("BulletSpawner2D", D_METHOD("debug_set_pattern_cache_verify", "enabled"), &BulletSpawner2D::debug_set_pattern_cache_verify);
	ClassDB::bind_static_method("BulletSpawner2D", D_METHOD("debug_get_pattern_cache_verify"), &BulletSpawner2D::debug_get_pattern_cache_verify);
	BIND_ENUM_CONSTANT(PATTERN_CACHE_AUTO);
	BIND_ENUM_CONSTANT(PATTERN_CACHE_OFF);
	ClassDB::bind_method(D_METHOD("shoot_once"), &BulletSpawner2D::shoot_once);
	ClassDB::bind_method(D_METHOD("shoot_once_deferred"), &BulletSpawner2D::shoot_once_deferred);
	ClassDB::bind_method(D_METHOD("reset_shooting"), &BulletSpawner2D::reset_shooting);
	ClassDB::bind_method(D_METHOD("fire_n_volleys", "n"), &BulletSpawner2D::fire_n_volleys);
	ClassDB::bind_method(D_METHOD("pause_shooting"), &BulletSpawner2D::pause_shooting);
	ClassDB::bind_method(D_METHOD("resume_shooting"), &BulletSpawner2D::resume_shooting);
	ClassDB::bind_method(D_METHOD("is_shooting_paused"), &BulletSpawner2D::is_shooting_paused);
	ClassDB::bind_method(D_METHOD("volleys_remaining"), &BulletSpawner2D::volleys_remaining);
}

} // namespace BlastBullets2D
