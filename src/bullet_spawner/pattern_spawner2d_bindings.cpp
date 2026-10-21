// PatternSpawner2D registration (_bind_methods: inspector groups/subgroups,
// enums, signals) and per-mode inspector visibility (_validate_property).
// The bullet groups are BulletSpawner2D's (bullet_spawner2d_bindings.cpp).

#include "bullet_spawner/pattern_spawner2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

void PatternSpawner2D::_validate_property(PropertyInfo &p_property) const {
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
	if (property_name == "bullet_scale_min" || property_name == "bullet_scale_max") {
		if (bullet_scale_mode != BULLET_SCALE_UNIFORM) {
			p_property.usage &= ~PROPERTY_USAGE_EDITOR;
		}
		return;
	}
	if (property_name == "bullet_scale_min_xy" || property_name == "bullet_scale_max_xy") {
		if (bullet_scale_mode != BULLET_SCALE_PER_AXIS) {
			p_property.usage &= ~PROPERTY_USAGE_EDITOR;
		}
		return;
	}
	if (property_name == "telegraph_sec" || property_name == "telegraph_max_sec") {
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
	// Sound: everything but the master switch hides while sound is off; each
	// listener source shows only its own knobs (Godot Listener needs none).
	if (property_name.begins_with("sound_")) {
		bool show = property_name == "sound_enabled" || sound_enabled;
		if (show && property_name == "sound_listener_node_group") {
			show = sound_listener_source == SOUND_LISTENER_NODE_GROUP;
		} else if (show && property_name == "sound_listener_path") {
			show = sound_listener_source == SOUND_LISTENER_NODE_PATH;
		} else if (show && property_name.begins_with("sound_listener_node_name")) {
			show = sound_listener_source == SOUND_LISTENER_NODE_NAME;
		} else if (show && property_name.begins_with("sound_listener_children_")) {
			show = sound_listener_source == SOUND_LISTENER_NODE_CHILDREN;
		} else if (show && property_name == "sound_listener_global_positions") {
			show = sound_listener_source == SOUND_LISTENER_GLOBAL_POSITIONS;
		} else if (show && (property_name == "sound_listener_filter_group" || property_name == "sound_listener_update_interval")) {
			// Godot Listener and points have no group and need no interval.
			show = sound_listener_source != SOUND_LISTENER_GODOT_LISTENER && sound_listener_source != SOUND_LISTENER_MOUSE && sound_listener_source != SOUND_LISTENER_GLOBAL_POSITIONS;
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
	// Only the helper_* option groups are gated (patterns module, driven by
	// the shape registry's knob prefixes); everything else is always shown.
	if (!property_name.begins_with("helper_")) {
		return;
	}
	if (!is_knob_relevant((int)pattern_source, property_name)) {
		p_property.usage &= ~PROPERTY_USAGE_EDITOR;
	}
}

void PatternSpawner2D::_bind_methods() {
	// Inspector layout: Setup (wiring) -> Bullet Patterns (source, amount,
	// Transform subgroup, one subgroup per shape with its helper_<shape>_
	// prefix stripped, Outline Layers last) -> Shooting -> Spin -> Homing ->
	// Orbiting -> Graze -> Preview -> Movement -> Performance. Property NAMES never
	// change here (they are serialized into .tscn); only the order and the
	// headers do. ADD_PROPERTY must follow its bind_method calls, otherwise
	// ClassDB silently drops the property (the test runner flags that).
	ADD_GROUP("Setup", "");
	ClassDB::bind_method(D_METHOD("get_bullet_factory_path"), &PatternSpawner2D::get_bullet_factory_path);
	ClassDB::bind_method(D_METHOD("set_bullet_factory_path", "path"), &PatternSpawner2D::set_bullet_factory_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "bullet_factory_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "BulletFactory2D"), "set_bullet_factory_path", "get_bullet_factory_path");

	ClassDB::bind_method(D_METHOD("get_bullet_factory"), &PatternSpawner2D::get_bullet_factory);
	ClassDB::bind_method(D_METHOD("set_bullet_factory", "factory"), &PatternSpawner2D::set_bullet_factory);

	ClassDB::bind_method(D_METHOD("get_transforms_generator_path"), &PatternSpawner2D::get_transforms_generator_path);
	ClassDB::bind_method(D_METHOD("set_transforms_generator_path", "path"), &PatternSpawner2D::set_transforms_generator_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "transforms_generator", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node2D"), "set_transforms_generator_path", "get_transforms_generator_path");

	ClassDB::bind_method(D_METHOD("get_transforms_generator"), &PatternSpawner2D::get_transforms_generator);
	ClassDB::bind_method(D_METHOD("set_transforms_generator", "generator"), &PatternSpawner2D::set_transforms_generator);
	ClassDB::bind_method(D_METHOD("get_effective_generator"), &PatternSpawner2D::get_effective_generator);

	ADD_GROUP("Time", "");
	ClassDB::bind_method(D_METHOD("get_process_callback"), &PatternSpawner2D::get_process_callback);
	ClassDB::bind_method(D_METHOD("set_process_callback", "value"), &PatternSpawner2D::set_process_callback);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "process_callback", PROPERTY_HINT_ENUM, "Idle,Physics"), "set_process_callback", "get_process_callback");
	BIND_ENUM_CONSTANT(PROCESS_IDLE);
	BIND_ENUM_CONSTANT(PROCESS_PHYSICS);
	BIND_BITFIELD_FLAG(LIVE_HOMING);
	BIND_BITFIELD_FLAG(LIVE_GRAZE);
	BIND_BITFIELD_FLAG(LIVE_SOUND);
	BIND_BITFIELD_FLAG(LIVE_FLOCK);
	BIND_BITFIELD_FLAG(LIVE_SPLIT);
	BIND_BITFIELD_FLAG(LIVE_EMITTER);
	BIND_BITFIELD_FLAG(LIVE_ALL);
	ClassDB::bind_method(D_METHOD("get_time_scale"), &PatternSpawner2D::get_time_scale);
	ClassDB::bind_method(D_METHOD("set_time_scale", "value"), &PatternSpawner2D::set_time_scale);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time_scale", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_time_scale", "get_time_scale");
	ClassDB::bind_method(D_METHOD("get_time_scale_affects_volleys"), &PatternSpawner2D::get_time_scale_affects_volleys);
	ClassDB::bind_method(D_METHOD("set_time_scale_affects_volleys", "value"), &PatternSpawner2D::set_time_scale_affects_volleys);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "time_scale_affects_volleys"), "set_time_scale_affects_volleys", "get_time_scale_affects_volleys");
	ClassDB::bind_method(D_METHOD("get_process_time_scale"), &PatternSpawner2D::get_process_time_scale);
	ClassDB::bind_method(D_METHOD("get_volley_time_scale"), &PatternSpawner2D::get_volley_time_scale);
	ClassDB::bind_method(D_METHOD("set_volley_time_scale", "value"), &PatternSpawner2D::set_volley_time_scale);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "volley_time_scale", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_volley_time_scale", "get_volley_time_scale");

	ADD_GROUP("Bullet Patterns", "");
	// Hand-written accessors of the group (PATTERN_PROPERTY rows); the
	// table-driven knobs bind themselves in the expansion below.
	ClassDB::bind_method(D_METHOD("get_pattern_source"), &PatternSpawner2D::get_pattern_source);
	ClassDB::bind_method(D_METHOD("set_pattern_source", "value"), &PatternSpawner2D::set_pattern_source);
	ClassDB::bind_method(D_METHOD("get_pattern_scale"), &PatternSpawner2D::get_pattern_scale);
	ClassDB::bind_method(D_METHOD("set_pattern_scale", "value"), &PatternSpawner2D::set_pattern_scale);
	ClassDB::bind_method(D_METHOD("get_transforms_scale"), &PatternSpawner2D::get_transforms_scale);
	ClassDB::bind_method(D_METHOD("set_transforms_scale", "value"), &PatternSpawner2D::set_transforms_scale);
	ClassDB::bind_method(D_METHOD("get_spawn_position_offset"), &PatternSpawner2D::get_spawn_position_offset);
	ClassDB::bind_method(D_METHOD("set_spawn_position_offset", "value"), &PatternSpawner2D::set_spawn_position_offset);
	ClassDB::bind_method(D_METHOD("get_spawn_position_offset_space"), &PatternSpawner2D::get_spawn_position_offset_space);
	ClassDB::bind_method(D_METHOD("set_spawn_position_offset_space", "value"), &PatternSpawner2D::set_spawn_position_offset_space);
	BIND_ENUM_CONSTANT(SPAWN_OFFSET_GLOBAL);
	BIND_ENUM_CONSTANT(SPAWN_OFFSET_LOCAL);
	ClassDB::bind_method(D_METHOD("get_helper_aimed_target_path"), &PatternSpawner2D::get_helper_aimed_target_path);
	ClassDB::bind_method(D_METHOD("set_helper_aimed_target_path", "path"), &PatternSpawner2D::set_helper_aimed_target_path);
	ClassDB::bind_method(D_METHOD("get_helper_aimed_target"), &PatternSpawner2D::get_helper_aimed_target);
	ClassDB::bind_method(D_METHOD("set_helper_aimed_target", "target"), &PatternSpawner2D::set_helper_aimed_target);
	ClassDB::bind_method(D_METHOD("get_helper_custom_transforms"), &PatternSpawner2D::get_helper_custom_transforms);
	ClassDB::bind_method(D_METHOD("set_helper_custom_transforms", "value"), &PatternSpawner2D::set_helper_custom_transforms);
	ClassDB::bind_method(D_METHOD("get_helper_path2d_path"), &PatternSpawner2D::get_helper_path2d_path);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_path", "path"), &PatternSpawner2D::set_helper_path2d_path);
	ClassDB::bind_method(D_METHOD("get_helper_path2d_node"), &PatternSpawner2D::get_helper_path2d_node);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_node", "node"), &PatternSpawner2D::set_helper_path2d_node);
	ClassDB::bind_method(D_METHOD("get_helper_path2d_space"), &PatternSpawner2D::get_helper_path2d_space);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_space", "value"), &PatternSpawner2D::set_helper_path2d_space);
	ClassDB::bind_method(D_METHOD("get_helper_path2d_distribution"), &PatternSpawner2D::get_helper_path2d_distribution);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_distribution", "value"), &PatternSpawner2D::set_helper_path2d_distribution);
	ClassDB::bind_method(D_METHOD("get_helper_path2d_overflow"), &PatternSpawner2D::get_helper_path2d_overflow);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_overflow", "value"), &PatternSpawner2D::set_helper_path2d_overflow);
	ClassDB::bind_method(D_METHOD("get_helper_path2d_anchor"), &PatternSpawner2D::get_helper_path2d_anchor);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_anchor", "value"), &PatternSpawner2D::set_helper_path2d_anchor);
	ClassDB::bind_method(D_METHOD("get_helper_path2d_facing"), &PatternSpawner2D::get_helper_path2d_facing);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_facing", "value"), &PatternSpawner2D::set_helper_path2d_facing);
	ClassDB::bind_method(D_METHOD("get_helper_outline_layer_scales"), &PatternSpawner2D::get_helper_outline_layer_scales);
	ClassDB::bind_method(D_METHOD("set_helper_outline_layer_scales", "value"), &PatternSpawner2D::set_helper_outline_layer_scales);

	// Every property of the group, in inspector order, from the knob table.
#define PATTERN_SUBGROUP(TITLE, PREFIX) ADD_SUBGROUP(TITLE, PREFIX);
#define PATTERN_KNOB(PTYPE, CTYPE, VTYPE, NAME, C1, A1, B1, M1, C2, A2, B2, M2, NOTIFY, HINT, HINT_STRING) \
	ClassDB::bind_method(D_METHOD("get_" #NAME), &PatternSpawner2D::get_##NAME);                           \
	ClassDB::bind_method(D_METHOD("set_" #NAME, "value"), &PatternSpawner2D::set_##NAME);                  \
	ADD_PROPERTY(PropertyInfo(Variant::VTYPE, #NAME, HINT, HINT_STRING), "set_" #NAME, "get_" #NAME);
#define PATTERN_PROPERTY(VTYPE, NAME, HINT, HINT_STRING, SETTER, GETTER) \
	ADD_PROPERTY(PropertyInfo(Variant::VTYPE, #NAME, HINT, HINT_STRING), #SETTER, #GETTER);
#include "patterns/pattern_knob_table2d.inc"
#undef PATTERN_SUBGROUP
#undef PATTERN_KNOB
#undef PATTERN_PROPERTY

	ADD_SIGNAL(MethodInfo("volley_skipped",
			PropertyInfo(Variant::STRING_NAME, "reason")));
	ADD_SIGNAL(MethodInfo("volley_telegraphed",
			PropertyInfo(Variant::ARRAY, "aim_transforms")));
	ADD_SIGNAL(MethodInfo("burst_shot_fired",
			PropertyInfo(Variant::INT, "shot_index"),
			PropertyInfo(Variant::BOOL, "mirrored")));
	ADD_SIGNAL(MethodInfo("burst_finished"));
	ADD_SIGNAL(MethodInfo("pattern_list_finished"));
	ADD_SIGNAL(MethodInfo("shooting_started"));
	ADD_SIGNAL(MethodInfo("shooting_stopped"));
	ADD_SIGNAL(MethodInfo("shooting_finished"));
	ClassDB::bind_method(D_METHOD("get_shooting_enabled"), &PatternSpawner2D::get_shooting_enabled);
	ClassDB::bind_method(D_METHOD("set_shooting_enabled", "value"), &PatternSpawner2D::set_shooting_enabled);
	ADD_GROUP("Shooting", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shooting_enabled"), "set_shooting_enabled", "get_shooting_enabled");
	ClassDB::bind_method(D_METHOD("is_shooting_active"), &PatternSpawner2D::is_shooting_active);

	ClassDB::bind_method(D_METHOD("get_shoot_interval_sec"), &PatternSpawner2D::get_shoot_interval_sec);
	ClassDB::bind_method(D_METHOD("set_shoot_interval_sec", "value"), &PatternSpawner2D::set_shoot_interval_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shoot_interval_sec", PROPERTY_HINT_RANGE, "0.005,30,0.001,or_greater"), "set_shoot_interval_sec", "get_shoot_interval_sec");
	ClassDB::bind_method(D_METHOD("get_shoot_interval_max_sec"), &PatternSpawner2D::get_shoot_interval_max_sec);
	ClassDB::bind_method(D_METHOD("set_shoot_interval_max_sec", "value"), &PatternSpawner2D::set_shoot_interval_max_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shoot_interval_max_sec", PROPERTY_HINT_RANGE, "0,30,0.001,or_greater,suffix:s"), "set_shoot_interval_max_sec", "get_shoot_interval_max_sec");

	ClassDB::bind_method(D_METHOD("get_shoot_outside_despawn_bounds"), &PatternSpawner2D::get_shoot_outside_despawn_bounds);
	ClassDB::bind_method(D_METHOD("set_shoot_outside_despawn_bounds", "value"), &PatternSpawner2D::set_shoot_outside_despawn_bounds);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shoot_outside_despawn_bounds"), "set_shoot_outside_despawn_bounds", "get_shoot_outside_despawn_bounds");

	ClassDB::bind_method(D_METHOD("get_shoot_initial_delay_sec"), &PatternSpawner2D::get_shoot_initial_delay_sec);
	ClassDB::bind_method(D_METHOD("set_shoot_initial_delay_sec", "value"), &PatternSpawner2D::set_shoot_initial_delay_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shoot_initial_delay_sec"), "set_shoot_initial_delay_sec", "get_shoot_initial_delay_sec");

	ClassDB::bind_method(D_METHOD("get_max_volleys"), &PatternSpawner2D::get_max_volleys);
	ClassDB::bind_method(D_METHOD("set_max_volleys", "value"), &PatternSpawner2D::set_max_volleys);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_volleys"), "set_max_volleys", "get_max_volleys");

	ClassDB::bind_method(D_METHOD("get_reload_jitter_sec"), &PatternSpawner2D::get_reload_jitter_sec);
	ClassDB::bind_method(D_METHOD("set_reload_jitter_sec", "value"), &PatternSpawner2D::set_reload_jitter_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reload_jitter_sec"), "set_reload_jitter_sec", "get_reload_jitter_sec");
	// Seed lives directly under its jitter amount so the pair reads as one feature.
	ClassDB::bind_method(D_METHOD("get_reload_jitter_seed"), &PatternSpawner2D::get_reload_jitter_seed);
	ClassDB::bind_method(D_METHOD("set_reload_jitter_seed", "value"), &PatternSpawner2D::set_reload_jitter_seed);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "reload_jitter_seed"), "set_reload_jitter_seed", "get_reload_jitter_seed");

	ClassDB::bind_method(D_METHOD("get_burst_enabled"), &PatternSpawner2D::get_burst_enabled);
	ClassDB::bind_method(D_METHOD("set_burst_enabled", "value"), &PatternSpawner2D::set_burst_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "burst_enabled"), "set_burst_enabled", "get_burst_enabled");

	ClassDB::bind_method(D_METHOD("get_burst_count"), &PatternSpawner2D::get_burst_count);
	ClassDB::bind_method(D_METHOD("set_burst_count", "value"), &PatternSpawner2D::set_burst_count);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "burst_count"), "set_burst_count", "get_burst_count");

	ClassDB::bind_method(D_METHOD("get_burst_interval_sec"), &PatternSpawner2D::get_burst_interval_sec);
	ClassDB::bind_method(D_METHOD("set_burst_interval_sec", "value"), &PatternSpawner2D::set_burst_interval_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "burst_interval_sec"), "set_burst_interval_sec", "get_burst_interval_sec");
	ClassDB::bind_method(D_METHOD("get_burst_interval_max_sec"), &PatternSpawner2D::get_burst_interval_max_sec);
	ClassDB::bind_method(D_METHOD("set_burst_interval_max_sec", "value"), &PatternSpawner2D::set_burst_interval_max_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "burst_interval_max_sec", PROPERTY_HINT_RANGE, "0,30,0.001,or_greater,suffix:s"), "set_burst_interval_max_sec", "get_burst_interval_max_sec");

	ClassDB::bind_method(D_METHOD("get_burst_alternate_mirror"), &PatternSpawner2D::get_burst_alternate_mirror);
	ClassDB::bind_method(D_METHOD("set_burst_alternate_mirror", "value"), &PatternSpawner2D::set_burst_alternate_mirror);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "burst_alternate_mirror"), "set_burst_alternate_mirror", "get_burst_alternate_mirror");

	ClassDB::bind_method(D_METHOD("get_telegraph_enabled"), &PatternSpawner2D::get_telegraph_enabled);
	ClassDB::bind_method(D_METHOD("set_telegraph_enabled", "value"), &PatternSpawner2D::set_telegraph_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "telegraph_enabled"), "set_telegraph_enabled", "get_telegraph_enabled");

	ClassDB::bind_method(D_METHOD("get_telegraph_sec"), &PatternSpawner2D::get_telegraph_sec);
	ClassDB::bind_method(D_METHOD("set_telegraph_sec", "value"), &PatternSpawner2D::set_telegraph_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "telegraph_sec"), "set_telegraph_sec", "get_telegraph_sec");
	ClassDB::bind_method(D_METHOD("get_telegraph_max_sec"), &PatternSpawner2D::get_telegraph_max_sec);
	ClassDB::bind_method(D_METHOD("set_telegraph_max_sec", "value"), &PatternSpawner2D::set_telegraph_max_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "telegraph_max_sec", PROPERTY_HINT_RANGE, "0,30,0.001,or_greater,suffix:s"), "set_telegraph_max_sec", "get_telegraph_max_sec");

	ADD_GROUP("Randomization", "bullet_");
	ClassDB::bind_method(D_METHOD("get_bullet_scale_mode"), &PatternSpawner2D::get_bullet_scale_mode);
	ClassDB::bind_method(D_METHOD("set_bullet_scale_mode", "value"), &PatternSpawner2D::set_bullet_scale_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bullet_scale_mode", PROPERTY_HINT_ENUM, "Off,Uniform,Per Axis"), "set_bullet_scale_mode", "get_bullet_scale_mode");
	ClassDB::bind_method(D_METHOD("get_bullet_scale_min"), &PatternSpawner2D::get_bullet_scale_min);
	ClassDB::bind_method(D_METHOD("set_bullet_scale_min", "value"), &PatternSpawner2D::set_bullet_scale_min);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bullet_scale_min", PROPERTY_HINT_RANGE, "0.01,10,0.01,or_greater"), "set_bullet_scale_min", "get_bullet_scale_min");
	ClassDB::bind_method(D_METHOD("get_bullet_scale_max"), &PatternSpawner2D::get_bullet_scale_max);
	ClassDB::bind_method(D_METHOD("set_bullet_scale_max", "value"), &PatternSpawner2D::set_bullet_scale_max);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bullet_scale_max", PROPERTY_HINT_RANGE, "0.01,10,0.01,or_greater"), "set_bullet_scale_max", "get_bullet_scale_max");
	ClassDB::bind_method(D_METHOD("get_bullet_scale_min_xy"), &PatternSpawner2D::get_bullet_scale_min_xy);
	ClassDB::bind_method(D_METHOD("set_bullet_scale_min_xy", "value"), &PatternSpawner2D::set_bullet_scale_min_xy);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "bullet_scale_min_xy"), "set_bullet_scale_min_xy", "get_bullet_scale_min_xy");
	ClassDB::bind_method(D_METHOD("get_bullet_scale_max_xy"), &PatternSpawner2D::get_bullet_scale_max_xy);
	ClassDB::bind_method(D_METHOD("set_bullet_scale_max_xy", "value"), &PatternSpawner2D::set_bullet_scale_max_xy);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "bullet_scale_max_xy"), "set_bullet_scale_max_xy", "get_bullet_scale_max_xy");
	ClassDB::bind_method(D_METHOD("get_bullet_rotation_min_degrees"), &PatternSpawner2D::get_bullet_rotation_min_degrees);
	ClassDB::bind_method(D_METHOD("set_bullet_rotation_min_degrees", "value"), &PatternSpawner2D::set_bullet_rotation_min_degrees);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bullet_rotation_min_degrees", PROPERTY_HINT_RANGE, "-360,360,0.1,or_less,or_greater,suffix:deg"), "set_bullet_rotation_min_degrees", "get_bullet_rotation_min_degrees");
	ClassDB::bind_method(D_METHOD("get_bullet_rotation_max_degrees"), &PatternSpawner2D::get_bullet_rotation_max_degrees);
	ClassDB::bind_method(D_METHOD("set_bullet_rotation_max_degrees", "value"), &PatternSpawner2D::set_bullet_rotation_max_degrees);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bullet_rotation_max_degrees", PROPERTY_HINT_RANGE, "-360,360,0.1,or_less,or_greater,suffix:deg"), "set_bullet_rotation_max_degrees", "get_bullet_rotation_max_degrees");
	ClassDB::bind_method(D_METHOD("get_bullet_position_jitter_min"), &PatternSpawner2D::get_bullet_position_jitter_min);
	ClassDB::bind_method(D_METHOD("set_bullet_position_jitter_min", "value"), &PatternSpawner2D::set_bullet_position_jitter_min);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bullet_position_jitter_min", PROPERTY_HINT_RANGE, "0,200,0.1,or_greater,suffix:px"), "set_bullet_position_jitter_min", "get_bullet_position_jitter_min");
	ClassDB::bind_method(D_METHOD("get_bullet_position_jitter_max"), &PatternSpawner2D::get_bullet_position_jitter_max);
	ClassDB::bind_method(D_METHOD("set_bullet_position_jitter_max", "value"), &PatternSpawner2D::set_bullet_position_jitter_max);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bullet_position_jitter_max", PROPERTY_HINT_RANGE, "0,200,0.1,or_greater,suffix:px"), "set_bullet_position_jitter_max", "get_bullet_position_jitter_max");
	ClassDB::bind_method(D_METHOD("get_bullet_random_seed"), &PatternSpawner2D::get_bullet_random_seed);
	ClassDB::bind_method(D_METHOD("set_bullet_random_seed", "value"), &PatternSpawner2D::set_bullet_random_seed);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bullet_random_seed"), "set_bullet_random_seed", "get_bullet_random_seed");

	ClassDB::bind_method(D_METHOD("get_spin_enabled"), &PatternSpawner2D::get_spin_enabled);
	ClassDB::bind_method(D_METHOD("set_spin_enabled", "value"), &PatternSpawner2D::set_spin_enabled);
	ADD_GROUP("Spin", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "spin_enabled"), "set_spin_enabled", "get_spin_enabled");

	ClassDB::bind_method(D_METHOD("get_spin_mode"), &PatternSpawner2D::get_spin_mode);
	ClassDB::bind_method(D_METHOD("set_spin_mode", "value"), &PatternSpawner2D::set_spin_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "spin_mode", PROPERTY_HINT_ENUM, "Continuous,Oscillate"), "set_spin_mode", "get_spin_mode");
	ClassDB::bind_method(D_METHOD("get_spin_speed_deg_per_sec"), &PatternSpawner2D::get_spin_speed_deg_per_sec);
	ClassDB::bind_method(D_METHOD("set_spin_speed_deg_per_sec", "value"), &PatternSpawner2D::set_spin_speed_deg_per_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spin_speed_deg_per_sec"), "set_spin_speed_deg_per_sec", "get_spin_speed_deg_per_sec");

	ClassDB::bind_method(D_METHOD("get_spin_amplitude_deg"), &PatternSpawner2D::get_spin_amplitude_deg);
	ClassDB::bind_method(D_METHOD("set_spin_amplitude_deg", "value"), &PatternSpawner2D::set_spin_amplitude_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spin_amplitude_deg"), "set_spin_amplitude_deg", "get_spin_amplitude_deg");

	ClassDB::bind_method(D_METHOD("get_spin_frequency_hz"), &PatternSpawner2D::get_spin_frequency_hz);
	ClassDB::bind_method(D_METHOD("set_spin_frequency_hz", "value"), &PatternSpawner2D::set_spin_frequency_hz);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spin_frequency_hz"), "set_spin_frequency_hz", "get_spin_frequency_hz");

	ClassDB::bind_method(D_METHOD("get_spin_random_start_phase"), &PatternSpawner2D::get_spin_random_start_phase);
	ClassDB::bind_method(D_METHOD("set_spin_random_start_phase", "value"), &PatternSpawner2D::set_spin_random_start_phase);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "spin_random_start_phase"), "set_spin_random_start_phase", "get_spin_random_start_phase");

	ClassDB::bind_method(D_METHOD("get_spin_angle_deg"), &PatternSpawner2D::get_spin_angle_deg);
	ClassDB::bind_method(D_METHOD("set_spin_angle_deg", "degrees"), &PatternSpawner2D::set_spin_angle_deg);
	ClassDB::bind_method(D_METHOD("reset_spin_angle"), &PatternSpawner2D::reset_spin_angle);

	ClassDB::bind_method(D_METHOD("apply_pattern_preset", "preset"), &PatternSpawner2D::apply_pattern_preset);
	ClassDB::bind_method(D_METHOD("spawn_pattern_list", "entries", "simultaneous", "interval_sec"), &PatternSpawner2D::spawn_pattern_list, DEFVAL(false), DEFVAL(0.25));
	ClassDB::bind_method(D_METHOD("shoot_once_with", "overrides"), &PatternSpawner2D::shoot_once_with);
	ClassDB::bind_method(D_METHOD("shoot_from", "global_transform", "extra_spin_deg"), &PatternSpawner2D::shoot_from, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("stop_pattern_list"), &PatternSpawner2D::stop_pattern_list);
	ClassDB::bind_method(D_METHOD("is_pattern_list_active"), &PatternSpawner2D::is_pattern_list_active);
	ClassDB::bind_method(D_METHOD("get_burst_shots_left"), &PatternSpawner2D::get_burst_shots_left);
	ClassDB::bind_method(D_METHOD("begin_burst"), &PatternSpawner2D::begin_burst);

	ClassDB::bind_method(D_METHOD("debug_get_layer_rings"), &PatternSpawner2D::debug_get_layer_rings);
	ClassDB::bind_method(D_METHOD("debug_get_preview_dot_points"), &PatternSpawner2D::debug_get_preview_dot_points);
	ClassDB::bind_method(D_METHOD("debug_get_preview_track_points"), &PatternSpawner2D::debug_get_preview_track_points);
	ClassDB::bind_method(D_METHOD("debug_get_preview_track_closed"), &PatternSpawner2D::debug_get_preview_track_closed);
	ClassDB::bind_method(D_METHOD("debug_check_layer_coincidence", "tolerance_px"), &PatternSpawner2D::debug_check_layer_coincidence, DEFVAL(1.0));

	BIND_ENUM_CONSTANT(HOMING_NAME_MATCH_EXACT);
	BIND_ENUM_CONSTANT(HOMING_NAME_MATCH_CONTAINS);
	BIND_ENUM_CONSTANT(HOMING_NAME_MATCH_STARTS_WITH);
	BIND_ENUM_CONSTANT(HOMING_NAME_MATCH_ENDS_WITH);
	BIND_ENUM_CONSTANT(GRAZE_SOURCE_NODE_GROUP);
	BIND_ENUM_CONSTANT(GRAZE_SOURCE_NODE_PATH);
	BIND_ENUM_CONSTANT(GRAZE_SOURCE_NODE_NAME);
	BIND_ENUM_CONSTANT(GRAZE_SOURCE_NODE_CHILDREN);
	BIND_ENUM_CONSTANT(GRAZE_SOURCE_MOUSE);
	BIND_ENUM_CONSTANT(GRAZE_SOURCE_GLOBAL_POSITIONS);
	BIND_ENUM_CONSTANT(SOUND_LISTENER_GODOT_LISTENER);
	BIND_ENUM_CONSTANT(SOUND_LISTENER_NODE_GROUP);
	BIND_ENUM_CONSTANT(SOUND_LISTENER_NODE_PATH);
	BIND_ENUM_CONSTANT(SOUND_LISTENER_NODE_NAME);
	BIND_ENUM_CONSTANT(SOUND_LISTENER_NODE_CHILDREN);
	BIND_ENUM_CONSTANT(SOUND_LISTENER_MOUSE);
	BIND_ENUM_CONSTANT(SOUND_LISTENER_GLOBAL_POSITIONS);

	ClassDB::bind_method(D_METHOD("get_graze_enabled"), &PatternSpawner2D::get_graze_enabled);
	ClassDB::bind_method(D_METHOD("set_graze_enabled", "value"), &PatternSpawner2D::set_graze_enabled);
	ClassDB::bind_method(D_METHOD("get_graze_zones"), &PatternSpawner2D::get_graze_zones);
	ClassDB::bind_method(D_METHOD("set_graze_zones", "value"), &PatternSpawner2D::set_graze_zones);
	ClassDB::bind_method(D_METHOD("get_graze_show_preview"), &PatternSpawner2D::get_graze_show_preview);
	ClassDB::bind_method(D_METHOD("set_graze_show_preview", "value"), &PatternSpawner2D::set_graze_show_preview);
	ClassDB::bind_method(D_METHOD("get_graze_preview_during_runtime"), &PatternSpawner2D::get_graze_preview_during_runtime);
	ClassDB::bind_method(D_METHOD("set_graze_preview_during_runtime", "value"), &PatternSpawner2D::set_graze_preview_during_runtime);
	ClassDB::bind_method(D_METHOD("get_graze_preview_line_width"), &PatternSpawner2D::get_graze_preview_line_width);
	ClassDB::bind_method(D_METHOD("set_graze_preview_line_width", "value"), &PatternSpawner2D::set_graze_preview_line_width);
	ClassDB::bind_method(D_METHOD("get_graze_target_source"), &PatternSpawner2D::get_graze_target_source);
	ClassDB::bind_method(D_METHOD("set_graze_target_source", "value"), &PatternSpawner2D::set_graze_target_source);
	ClassDB::bind_method(D_METHOD("get_graze_node_group"), &PatternSpawner2D::get_graze_node_group);
	ClassDB::bind_method(D_METHOD("set_graze_node_group", "value"), &PatternSpawner2D::set_graze_node_group);
	ClassDB::bind_method(D_METHOD("get_graze_filter_group"), &PatternSpawner2D::get_graze_filter_group);
	ClassDB::bind_method(D_METHOD("set_graze_filter_group", "value"), &PatternSpawner2D::set_graze_filter_group);
	ClassDB::bind_method(D_METHOD("get_graze_target_path"), &PatternSpawner2D::get_graze_target_path);
	ClassDB::bind_method(D_METHOD("set_graze_target_path", "path"), &PatternSpawner2D::set_graze_target_path);
	ClassDB::bind_method(D_METHOD("get_graze_node_name"), &PatternSpawner2D::get_graze_node_name);
	ClassDB::bind_method(D_METHOD("set_graze_node_name", "value"), &PatternSpawner2D::set_graze_node_name);
	ClassDB::bind_method(D_METHOD("get_graze_node_name_match_mode"), &PatternSpawner2D::get_graze_node_name_match_mode);
	ClassDB::bind_method(D_METHOD("set_graze_node_name_match_mode", "value"), &PatternSpawner2D::set_graze_node_name_match_mode);
	ClassDB::bind_method(D_METHOD("get_graze_node_name_case_sensitive"), &PatternSpawner2D::get_graze_node_name_case_sensitive);
	ClassDB::bind_method(D_METHOD("set_graze_node_name_case_sensitive", "value"), &PatternSpawner2D::set_graze_node_name_case_sensitive);
	ClassDB::bind_method(D_METHOD("get_graze_children_parent_path"), &PatternSpawner2D::get_graze_children_parent_path);
	ClassDB::bind_method(D_METHOD("set_graze_children_parent_path", "path"), &PatternSpawner2D::set_graze_children_parent_path);
	ClassDB::bind_method(D_METHOD("get_graze_children_recursive"), &PatternSpawner2D::get_graze_children_recursive);
	ClassDB::bind_method(D_METHOD("set_graze_children_recursive", "value"), &PatternSpawner2D::set_graze_children_recursive);
	ClassDB::bind_method(D_METHOD("get_graze_global_positions"), &PatternSpawner2D::get_graze_global_positions);
	ClassDB::bind_method(D_METHOD("set_graze_global_positions", "value"), &PatternSpawner2D::set_graze_global_positions);
	ClassDB::bind_method(D_METHOD("get_graze_update_interval"), &PatternSpawner2D::get_graze_update_interval);
	ClassDB::bind_method(D_METHOD("set_graze_update_interval", "value"), &PatternSpawner2D::set_graze_update_interval);
	ClassDB::bind_method(D_METHOD("refresh_graze_targets"), &PatternSpawner2D::refresh_graze_targets);
	ClassDB::bind_method(D_METHOD("resolve_graze_targets"), &PatternSpawner2D::resolve_graze_targets);
	ClassDB::bind_method(D_METHOD("debug_get_graze_preview_circles"), &PatternSpawner2D::debug_get_graze_preview_circles);
	ClassDB::bind_method(D_METHOD("debug_get_graze_preview_stats"), &PatternSpawner2D::debug_get_graze_preview_stats);
	ClassDB::bind_method(D_METHOD("debug_get_graze_detector_stats"), &PatternSpawner2D::debug_get_graze_detector_stats);

	// Sound (bullet_spawner2d_sound.cpp). Every fired volley carries
	// sound_effects; spawner-only entries (On Shot, On Telegraph) offer
	// directly from the spawner at its global position.
	ClassDB::bind_method(D_METHOD("get_sound_enabled"), &PatternSpawner2D::get_sound_enabled);
	ClassDB::bind_method(D_METHOD("set_sound_enabled", "value"), &PatternSpawner2D::set_sound_enabled);
	ClassDB::bind_method(D_METHOD("get_sound_effects"), &PatternSpawner2D::get_sound_effects);
	ClassDB::bind_method(D_METHOD("set_sound_effects", "value"), &PatternSpawner2D::set_sound_effects);
	ClassDB::bind_method(D_METHOD("get_sound_volume_db"), &PatternSpawner2D::get_sound_volume_db);
	ClassDB::bind_method(D_METHOD("set_sound_volume_db", "value"), &PatternSpawner2D::set_sound_volume_db);
	ClassDB::bind_method(D_METHOD("get_sound_listener_source"), &PatternSpawner2D::get_sound_listener_source);
	ClassDB::bind_method(D_METHOD("set_sound_listener_source", "value"), &PatternSpawner2D::set_sound_listener_source);
	ClassDB::bind_method(D_METHOD("get_sound_listener_node_group"), &PatternSpawner2D::get_sound_listener_node_group);
	ClassDB::bind_method(D_METHOD("set_sound_listener_node_group", "value"), &PatternSpawner2D::set_sound_listener_node_group);
	ClassDB::bind_method(D_METHOD("get_sound_listener_filter_group"), &PatternSpawner2D::get_sound_listener_filter_group);
	ClassDB::bind_method(D_METHOD("set_sound_listener_filter_group", "value"), &PatternSpawner2D::set_sound_listener_filter_group);
	ClassDB::bind_method(D_METHOD("get_sound_listener_path"), &PatternSpawner2D::get_sound_listener_path);
	ClassDB::bind_method(D_METHOD("set_sound_listener_path", "path"), &PatternSpawner2D::set_sound_listener_path);
	ClassDB::bind_method(D_METHOD("get_sound_listener_node_name"), &PatternSpawner2D::get_sound_listener_node_name);
	ClassDB::bind_method(D_METHOD("set_sound_listener_node_name", "value"), &PatternSpawner2D::set_sound_listener_node_name);
	ClassDB::bind_method(D_METHOD("get_sound_listener_node_name_match_mode"), &PatternSpawner2D::get_sound_listener_node_name_match_mode);
	ClassDB::bind_method(D_METHOD("set_sound_listener_node_name_match_mode", "value"), &PatternSpawner2D::set_sound_listener_node_name_match_mode);
	ClassDB::bind_method(D_METHOD("get_sound_listener_node_name_case_sensitive"), &PatternSpawner2D::get_sound_listener_node_name_case_sensitive);
	ClassDB::bind_method(D_METHOD("set_sound_listener_node_name_case_sensitive", "value"), &PatternSpawner2D::set_sound_listener_node_name_case_sensitive);
	ClassDB::bind_method(D_METHOD("get_sound_listener_children_parent_path"), &PatternSpawner2D::get_sound_listener_children_parent_path);
	ClassDB::bind_method(D_METHOD("set_sound_listener_children_parent_path", "path"), &PatternSpawner2D::set_sound_listener_children_parent_path);
	ClassDB::bind_method(D_METHOD("get_sound_listener_children_recursive"), &PatternSpawner2D::get_sound_listener_children_recursive);
	ClassDB::bind_method(D_METHOD("set_sound_listener_children_recursive", "value"), &PatternSpawner2D::set_sound_listener_children_recursive);
	ClassDB::bind_method(D_METHOD("get_sound_listener_global_positions"), &PatternSpawner2D::get_sound_listener_global_positions);
	ClassDB::bind_method(D_METHOD("set_sound_listener_global_positions", "value"), &PatternSpawner2D::set_sound_listener_global_positions);
	ClassDB::bind_method(D_METHOD("get_sound_listener_update_interval"), &PatternSpawner2D::get_sound_listener_update_interval);
	ClassDB::bind_method(D_METHOD("set_sound_listener_update_interval", "value"), &PatternSpawner2D::set_sound_listener_update_interval);
	ClassDB::bind_method(D_METHOD("refresh_sound_listeners"), &PatternSpawner2D::refresh_sound_listeners);
	ClassDB::bind_method(D_METHOD("resolve_sound_listeners"), &PatternSpawner2D::resolve_sound_listeners);
	ClassDB::bind_method(D_METHOD("preview_sound_effect", "index"), &PatternSpawner2D::preview_sound_effect);
	ClassDB::bind_method(D_METHOD("debug_get_sound_detector_stats"), &PatternSpawner2D::debug_get_sound_detector_stats);
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

	ClassDB::bind_method(D_METHOD("get_show_pattern_preview"), &PatternSpawner2D::get_show_pattern_preview);
	ClassDB::bind_method(D_METHOD("set_show_pattern_preview", "value"), &PatternSpawner2D::set_show_pattern_preview);
	ADD_GROUP("Sound", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sound_enabled"), "set_sound_enabled", "get_sound_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "sound_effects", PROPERTY_HINT_ARRAY_TYPE, "BulletSoundData2D"), "set_sound_effects", "get_sound_effects");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sound_volume_db", PROPERTY_HINT_RANGE, "-80,24,0.1,suffix:dB"), "set_sound_volume_db", "get_sound_volume_db");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sound_listener_source", PROPERTY_HINT_ENUM, "Godot Listener,Node Group,Node Path,Node Name,Node Children,Mouse,Global Positions"), "set_sound_listener_source", "get_sound_listener_source");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "sound_listener_node_group"), "set_sound_listener_node_group", "get_sound_listener_node_group");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "sound_listener_filter_group"), "set_sound_listener_filter_group", "get_sound_listener_filter_group");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "sound_listener_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node2D"), "set_sound_listener_path", "get_sound_listener_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "sound_listener_node_name"), "set_sound_listener_node_name", "get_sound_listener_node_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sound_listener_node_name_match_mode", PROPERTY_HINT_ENUM, "Exact,Contains,Starts With,Ends With"), "set_sound_listener_node_name_match_mode", "get_sound_listener_node_name_match_mode");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sound_listener_node_name_case_sensitive"), "set_sound_listener_node_name_case_sensitive", "get_sound_listener_node_name_case_sensitive");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "sound_listener_children_parent_path"), "set_sound_listener_children_parent_path", "get_sound_listener_children_parent_path");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sound_listener_children_recursive"), "set_sound_listener_children_recursive", "get_sound_listener_children_recursive");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR2_ARRAY, "sound_listener_global_positions"), "set_sound_listener_global_positions", "get_sound_listener_global_positions");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sound_listener_update_interval", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s"), "set_sound_listener_update_interval", "get_sound_listener_update_interval");

	ADD_GROUP("Preview", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "show_pattern_preview"), "set_show_pattern_preview", "get_show_pattern_preview");

	ClassDB::bind_method(D_METHOD("get_show_preview_during_runtime"), &PatternSpawner2D::get_show_preview_during_runtime);
	ClassDB::bind_method(D_METHOD("set_show_preview_during_runtime", "value"), &PatternSpawner2D::set_show_preview_during_runtime);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "show_preview_during_runtime"), "set_show_preview_during_runtime", "get_show_preview_during_runtime");

	ClassDB::bind_method(D_METHOD("get_preview_dot_color"), &PatternSpawner2D::get_preview_dot_color);
	ClassDB::bind_method(D_METHOD("set_preview_dot_color", "value"), &PatternSpawner2D::set_preview_dot_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "preview_dot_color"), "set_preview_dot_color", "get_preview_dot_color");

	ClassDB::bind_method(D_METHOD("get_preview_arrow_color"), &PatternSpawner2D::get_preview_arrow_color);
	ClassDB::bind_method(D_METHOD("set_preview_arrow_color", "value"), &PatternSpawner2D::set_preview_arrow_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "preview_arrow_color"), "set_preview_arrow_color", "get_preview_arrow_color");

	ClassDB::bind_method(D_METHOD("get_preview_first_dot_color"), &PatternSpawner2D::get_preview_first_dot_color);
	ClassDB::bind_method(D_METHOD("set_preview_first_dot_color", "value"), &PatternSpawner2D::set_preview_first_dot_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "preview_first_dot_color"), "set_preview_first_dot_color", "get_preview_first_dot_color");

	ClassDB::bind_method(D_METHOD("get_preview_path_color"), &PatternSpawner2D::get_preview_path_color);
	ClassDB::bind_method(D_METHOD("set_preview_path_color", "value"), &PatternSpawner2D::set_preview_path_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "preview_path_color"), "set_preview_path_color", "get_preview_path_color");

	ClassDB::bind_method(D_METHOD("get_preview_layer_path_color"), &PatternSpawner2D::get_preview_layer_path_color);
	ClassDB::bind_method(D_METHOD("set_preview_layer_path_color", "value"), &PatternSpawner2D::set_preview_layer_path_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "preview_layer_path_color"), "set_preview_layer_path_color", "get_preview_layer_path_color");

	ClassDB::bind_method(D_METHOD("get_preview_path_width"), &PatternSpawner2D::get_preview_path_width);
	ClassDB::bind_method(D_METHOD("set_preview_path_width", "value"), &PatternSpawner2D::set_preview_path_width);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "preview_path_width"), "set_preview_path_width", "get_preview_path_width");

	ClassDB::bind_method(D_METHOD("get_preview_dot_radius"), &PatternSpawner2D::get_preview_dot_radius);
	ClassDB::bind_method(D_METHOD("set_preview_dot_radius", "value"), &PatternSpawner2D::set_preview_dot_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "preview_dot_radius"), "set_preview_dot_radius", "get_preview_dot_radius");

	ClassDB::bind_method(D_METHOD("get_preview_arrow_gap"), &PatternSpawner2D::get_preview_arrow_gap);
	ClassDB::bind_method(D_METHOD("set_preview_arrow_gap", "value"), &PatternSpawner2D::set_preview_arrow_gap);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "preview_arrow_gap"), "set_preview_arrow_gap", "get_preview_arrow_gap");

	ClassDB::bind_method(D_METHOD("get_preview_arrow_length"), &PatternSpawner2D::get_preview_arrow_length);
	ClassDB::bind_method(D_METHOD("set_preview_arrow_length", "value"), &PatternSpawner2D::set_preview_arrow_length);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "preview_arrow_length"), "set_preview_arrow_length", "get_preview_arrow_length");

	ClassDB::bind_method(D_METHOD("get_preview_arrow_width"), &PatternSpawner2D::get_preview_arrow_width);
	ClassDB::bind_method(D_METHOD("set_preview_arrow_width", "value"), &PatternSpawner2D::set_preview_arrow_width);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "preview_arrow_width"), "set_preview_arrow_width", "get_preview_arrow_width");

	ClassDB::bind_method(D_METHOD("get_preview_arrow_head_length"), &PatternSpawner2D::get_preview_arrow_head_length);
	ClassDB::bind_method(D_METHOD("set_preview_arrow_head_length", "value"), &PatternSpawner2D::set_preview_arrow_head_length);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "preview_arrow_head_length"), "set_preview_arrow_head_length", "get_preview_arrow_head_length");

	ClassDB::bind_method(D_METHOD("get_preview_arrow_head_width"), &PatternSpawner2D::get_preview_arrow_head_width);
	ClassDB::bind_method(D_METHOD("set_preview_arrow_head_width", "value"), &PatternSpawner2D::set_preview_arrow_head_width);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "preview_arrow_head_width"), "set_preview_arrow_head_width", "get_preview_arrow_head_width");

	ClassDB::bind_method(D_METHOD("get_preview_draw_collision_rings"), &PatternSpawner2D::get_preview_draw_collision_rings);
	ClassDB::bind_method(D_METHOD("set_preview_draw_collision_rings", "value"), &PatternSpawner2D::set_preview_draw_collision_rings);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "preview_draw_collision_rings"), "set_preview_draw_collision_rings", "get_preview_draw_collision_rings");
	ClassDB::bind_method(D_METHOD("get_preview_collision_ring_color"), &PatternSpawner2D::get_preview_collision_ring_color);
	ClassDB::bind_method(D_METHOD("set_preview_collision_ring_color", "value"), &PatternSpawner2D::set_preview_collision_ring_color);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "preview_collision_ring_color"), "set_preview_collision_ring_color", "get_preview_collision_ring_color");
	ClassDB::bind_method(D_METHOD("get_preview_collision_ring_width"), &PatternSpawner2D::get_preview_collision_ring_width);
	ClassDB::bind_method(D_METHOD("set_preview_collision_ring_width", "value"), &PatternSpawner2D::set_preview_collision_ring_width);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "preview_collision_ring_width"), "set_preview_collision_ring_width", "get_preview_collision_ring_width");

	// ---- Movement ----
#define BS_BIND_PROP(m_name)                                                         \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &PatternSpawner2D::get_##m_name); \
	ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &PatternSpawner2D::set_##m_name);
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
	ClassDB::bind_method(D_METHOD("get_movement_path_node"), &PatternSpawner2D::get_movement_path_node);
	ClassDB::bind_method(D_METHOD("set_movement_path_node", "node"), &PatternSpawner2D::set_movement_path_node);
	ClassDB::bind_method(D_METHOD("get_setup_warnings"), &PatternSpawner2D::get_setup_warnings);
	ClassDB::bind_method(D_METHOD("movement_play"), &PatternSpawner2D::movement_play);
	ClassDB::bind_method(D_METHOD("movement_pause"), &PatternSpawner2D::movement_pause);
	ClassDB::bind_method(D_METHOD("movement_stop", "reset_to_start"), &PatternSpawner2D::movement_stop, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("movement_seek", "ratio"), &PatternSpawner2D::movement_seek);
	ClassDB::bind_method(D_METHOD("movement_reverse"), &PatternSpawner2D::movement_reverse);
	ClassDB::bind_method(D_METHOD("teleport", "global_position", "carry_live_bullets"), &PatternSpawner2D::teleport, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("is_movement_playing"), &PatternSpawner2D::is_movement_playing);
	ClassDB::bind_method(D_METHOD("get_movement_progress"), &PatternSpawner2D::get_movement_progress);
	ClassDB::bind_method(D_METHOD("get_movement_leg"), &PatternSpawner2D::get_movement_leg);
	ClassDB::bind_method(D_METHOD("get_movement_velocity"), &PatternSpawner2D::get_movement_velocity);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("debug_ease", "t", "transition", "ease"), &PatternSpawner2D::debug_ease);
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

	ClassDB::bind_method(D_METHOD("get_pattern_cache_mode"), &PatternSpawner2D::get_pattern_cache_mode);
	ClassDB::bind_method(D_METHOD("set_pattern_cache_mode", "value"), &PatternSpawner2D::set_pattern_cache_mode);
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
	BIND_ENUM_CONSTANT(BULLET_SCALE_OFF);
	BIND_ENUM_CONSTANT(BULLET_SCALE_UNIFORM);
	BIND_ENUM_CONSTANT(BULLET_SCALE_PER_AXIS);

	ClassDB::bind_method(D_METHOD("get_volleys_fired"), &PatternSpawner2D::get_volleys_fired);
	ClassDB::bind_method(D_METHOD("collect_spawn_transforms"), &PatternSpawner2D::collect_spawn_transforms);
	ClassDB::bind_method(D_METHOD("debug_collect_spawn_transforms_uncached"), &PatternSpawner2D::debug_collect_spawn_transforms_uncached);
	ClassDB::bind_method(D_METHOD("debug_get_pattern_cache_info"), &PatternSpawner2D::debug_get_pattern_cache_info);
	ClassDB::bind_method(D_METHOD("debug_get_preview_stats"), &PatternSpawner2D::debug_get_preview_stats);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("debug_set_pattern_cache_verify", "enabled"), &PatternSpawner2D::debug_set_pattern_cache_verify);
	ClassDB::bind_static_method(get_class_static(), D_METHOD("debug_get_pattern_cache_verify"), &PatternSpawner2D::debug_get_pattern_cache_verify);
	BIND_ENUM_CONSTANT(PATTERN_CACHE_AUTO);
	BIND_ENUM_CONSTANT(PATTERN_CACHE_OFF);
	ClassDB::bind_method(D_METHOD("shoot_once_deferred"), &PatternSpawner2D::shoot_once_deferred);
	ClassDB::bind_method(D_METHOD("reset_shooting"), &PatternSpawner2D::reset_shooting);
	ClassDB::bind_method(D_METHOD("fire_n_volleys", "n"), &PatternSpawner2D::fire_n_volleys);
	ClassDB::bind_method(D_METHOD("pause_shooting"), &PatternSpawner2D::pause_shooting);
	ClassDB::bind_method(D_METHOD("resume_shooting"), &PatternSpawner2D::resume_shooting);
	ClassDB::bind_method(D_METHOD("is_shooting_paused"), &PatternSpawner2D::is_shooting_paused);
	ClassDB::bind_method(D_METHOD("get_shoot_time_left"), &PatternSpawner2D::get_shoot_time_left);
	ClassDB::bind_method(D_METHOD("get_burst_time_left"), &PatternSpawner2D::get_burst_time_left);
	ClassDB::bind_method(D_METHOD("is_telegraph_pending"), &PatternSpawner2D::is_telegraph_pending);
	ClassDB::bind_method(D_METHOD("get_telegraph_time_left"), &PatternSpawner2D::get_telegraph_time_left);
	ClassDB::bind_method(D_METHOD("get_pattern_list_time_left"), &PatternSpawner2D::get_pattern_list_time_left);
	ClassDB::bind_method(D_METHOD("volleys_remaining"), &PatternSpawner2D::volleys_remaining);
}

} // namespace BlastBullets2D
