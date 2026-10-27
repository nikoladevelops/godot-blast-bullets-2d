// BulletSpawner2D registration: the bullet groups (Bullets, Homing,
// Orbiting, Flocking, Splitting), the bullet signals and enums, and their
// inspector gating. The pattern and cadence groups are PatternSpawner2D's
// (pattern_spawner2d_bindings.cpp).

#include "bullet_spawner/pattern_spawner2d_internal.hpp"
#include "core/live_object2d.hpp"

using namespace godot;

namespace BlastBullets2D {

void BulletSpawner2D::_validate_property(PropertyInfo &p_property) const {
	const String property_name = p_property.name;
	// Flocking: the flock and its scope hide while flocking is off.
	if (property_name == "flock" || property_name == "flock_scope") {
		if (!flock_enabled) {
			p_property.usage &= ~PROPERTY_USAGE_EDITOR;
		}
		return;
	}

	// Bullet Colors: everything but the mode hides while it is Off; the
	// shift belongs to Across Pattern, the cycle to Per Shot.
	if (property_name.begins_with("bullet_color_") && property_name != "bullet_color_mode") {
		bool show = bullet_color_mode != COLOR_MODE_OFF;
		if (property_name == "bullet_color_shift_per_shot") {
			show = bullet_color_mode == COLOR_MODE_GRADIENT_ACROSS_PATTERN;
		} else if (property_name == "bullet_color_cycle_shots") {
			show = bullet_color_mode == COLOR_MODE_GRADIENT_PER_SHOT;
		}
		if (!show) {
			p_property.usage &= ~PROPERTY_USAGE_EDITOR;
		}
		return;
	}

	// Emitter Bullets: the timing and the template hide while it is off.
	if (property_name == "emitter" || property_name == "emitter_spawner_path") {
		if (!emitter_enabled) {
			p_property.usage &= ~PROPERTY_USAGE_EDITOR;
		}
		return;
	}

	// Splitting: the chain hides while splitting is off.
	if (property_name == "split_data") {
		if (!split_enabled) {
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
}

bool BulletSpawner2D::adopt_live_volley_script(const Variant &volley) {
	BulletVolley2D *v = nullptr;
	return live_argument2d<BulletVolley2D>(volley, "adopt_live_volley", "volley", "BulletVolley2D", v) && adopt_live_volley(v);
}

void BulletSpawner2D::_bind_methods() {
	// Inspector layout: where the factory and the generator are, what a shot
	// fires (Bullets), where and when (Patterns, Shooting, Time, Spin,
	// Randomization), what the bullets do (Colors .. Emitter Bullets), then the
	// shared Graze / Sound and the editor-facing groups. Names never change
	// here (they are saved); only the order and the headers do.
	const StringName cls = get_class_static();
	bind_spawner_group(cls, SPAWNER_GROUP_SETUP);
	ADD_GROUP("Bullets", "");
	ClassDB::bind_method(D_METHOD("get_spawn_data"), &BulletSpawner2D::get_spawn_data);
	ClassDB::bind_method(D_METHOD("set_spawn_data", "new_spawn_data"), &BulletSpawner2D::set_spawn_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "spawn_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolleyData2D"), "set_spawn_data", "get_spawn_data");
	ClassDB::bind_method(D_METHOD("get_orphaned_volleys"), &BulletSpawner2D::get_orphaned_volleys);
	ClassDB::bind_method(D_METHOD("set_orphaned_volleys", "value"), &BulletSpawner2D::set_orphaned_volleys);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "orphaned_volleys", PROPERTY_HINT_ENUM, "Keep Flying,Hand To Factory,Clear,Remove"), "set_orphaned_volleys", "get_orphaned_volleys");
	ClassDB::bind_method(D_METHOD("convert_live_bullets_to_pickups", "pickup", "target"), &BulletSpawner2D::convert_live_bullets_to_pickups);
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
	ADD_SIGNAL(MethodInfo("retarget_applied",
			PropertyInfo(Variant::INT, "volleys_retargeted")));
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
	ClassDB::bind_method(D_METHOD("get_max_live_bullets"), &BulletSpawner2D::get_max_live_bullets);
	ClassDB::bind_method(D_METHOD("set_max_live_bullets", "value"), &BulletSpawner2D::set_max_live_bullets);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_live_bullets"), "set_max_live_bullets", "get_max_live_bullets");
	ClassDB::bind_method(D_METHOD("get_bullet_speed_scale_min"), &BulletSpawner2D::get_bullet_speed_scale_min);
	ClassDB::bind_method(D_METHOD("set_bullet_speed_scale_min", "value"), &BulletSpawner2D::set_bullet_speed_scale_min);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bullet_speed_scale_min", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_bullet_speed_scale_min", "get_bullet_speed_scale_min");
	ClassDB::bind_method(D_METHOD("get_bullet_speed_scale_max"), &BulletSpawner2D::get_bullet_speed_scale_max);
	ClassDB::bind_method(D_METHOD("set_bullet_speed_scale_max", "value"), &BulletSpawner2D::set_bullet_speed_scale_max);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bullet_speed_scale_max", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_bullet_speed_scale_max", "get_bullet_speed_scale_max");
	ClassDB::bind_method(D_METHOD("get_bullet_life_time_scale_min"), &BulletSpawner2D::get_bullet_life_time_scale_min);
	ClassDB::bind_method(D_METHOD("set_bullet_life_time_scale_min", "value"), &BulletSpawner2D::set_bullet_life_time_scale_min);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bullet_life_time_scale_min", PROPERTY_HINT_RANGE, "0.01,10,0.01,or_greater"), "set_bullet_life_time_scale_min", "get_bullet_life_time_scale_min");
	ClassDB::bind_method(D_METHOD("get_bullet_life_time_scale_max"), &BulletSpawner2D::get_bullet_life_time_scale_max);
	ClassDB::bind_method(D_METHOD("set_bullet_life_time_scale_max", "value"), &BulletSpawner2D::set_bullet_life_time_scale_max);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bullet_life_time_scale_max", PROPERTY_HINT_RANGE, "0.01,10,0.01,or_greater"), "set_bullet_life_time_scale_max", "get_bullet_life_time_scale_max");
	ClassDB::bind_method(D_METHOD("get_active_live_bullet_count"), &BulletSpawner2D::get_active_live_bullet_count);

	// Bullet Colors (bullet_spawner2d_colors.cpp): per-bullet colors picked at
	// shot time; the gradient and the mode knobs hide while the mode is Off.
	bind_spawner_group(cls, SPAWNER_GROUP_PATTERNS);
	bind_spawner_group(cls, SPAWNER_GROUP_SHOOTING);
	bind_spawner_group(cls, SPAWNER_GROUP_TIME);
	bind_spawner_group(cls, SPAWNER_GROUP_SPIN);
	bind_spawner_group(cls, SPAWNER_GROUP_RANDOMIZATION);
	ADD_GROUP("Bullet Colors", "");
	ClassDB::bind_method(D_METHOD("get_bullet_color_mode"), &BulletSpawner2D::get_bullet_color_mode);
	ClassDB::bind_method(D_METHOD("set_bullet_color_mode", "value"), &BulletSpawner2D::set_bullet_color_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bullet_color_mode", PROPERTY_HINT_ENUM, "Off,Gradient Across Pattern,Random From Gradient,Gradient Per Shot"), "set_bullet_color_mode", "get_bullet_color_mode");
	ClassDB::bind_method(D_METHOD("get_bullet_color_gradient"), &BulletSpawner2D::get_bullet_color_gradient);
	ClassDB::bind_method(D_METHOD("set_bullet_color_gradient", "value"), &BulletSpawner2D::set_bullet_color_gradient);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "bullet_color_gradient", PROPERTY_HINT_RESOURCE_TYPE, "Gradient"), "set_bullet_color_gradient", "get_bullet_color_gradient");
	ClassDB::bind_method(D_METHOD("get_bullet_color_shift_per_shot"), &BulletSpawner2D::get_bullet_color_shift_per_shot);
	ClassDB::bind_method(D_METHOD("set_bullet_color_shift_per_shot", "value"), &BulletSpawner2D::set_bullet_color_shift_per_shot);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bullet_color_shift_per_shot", PROPERTY_HINT_RANGE, "-1,1,0.001,or_less,or_greater"), "set_bullet_color_shift_per_shot", "get_bullet_color_shift_per_shot");
	ClassDB::bind_method(D_METHOD("get_bullet_color_cycle_shots"), &BulletSpawner2D::get_bullet_color_cycle_shots);
	ClassDB::bind_method(D_METHOD("set_bullet_color_cycle_shots", "value"), &BulletSpawner2D::set_bullet_color_cycle_shots);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bullet_color_cycle_shots", PROPERTY_HINT_RANGE, "1,64,1,or_greater"), "set_bullet_color_cycle_shots", "get_bullet_color_cycle_shots");
	ClassDB::bind_method(D_METHOD("sample_bullet_color", "t"), &BulletSpawner2D::sample_bullet_color);
	ClassDB::bind_method(D_METHOD("collect_shot_colors", "count"), &BulletSpawner2D::collect_shot_colors);
	BIND_ENUM_CONSTANT(COLOR_MODE_OFF);
	BIND_ENUM_CONSTANT(COLOR_MODE_GRADIENT_ACROSS_PATTERN);
	BIND_ENUM_CONSTANT(COLOR_MODE_RANDOM_FROM_GRADIENT);
	BIND_ENUM_CONSTANT(COLOR_MODE_GRADIENT_PER_SHOT);

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
	ClassDB::bind_method(D_METHOD("get_homing_reach_counts_bullet_size"), &BulletSpawner2D::get_homing_reach_counts_bullet_size);
	ClassDB::bind_method(D_METHOD("set_homing_reach_counts_bullet_size", "value"), &BulletSpawner2D::set_homing_reach_counts_bullet_size);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_reach_counts_bullet_size"), "set_homing_reach_counts_bullet_size", "get_homing_reach_counts_bullet_size");
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
	ClassDB::bind_method(D_METHOD("debug_get_retarget_countdown"), &BulletSpawner2D::debug_get_retarget_countdown);
	ClassDB::bind_method(D_METHOD("adopt_live_volley", "volley"), &BulletSpawner2D::adopt_live_volley_script);
	ClassDB::bind_method(D_METHOD("clear_live_volleys_homing"), &BulletSpawner2D::clear_live_volleys_homing);
	ClassDB::bind_method(D_METHOD("override_live_volleys_velocity", "new_velocity"), &BulletSpawner2D::override_live_volleys_velocity);
	ClassDB::bind_method(D_METHOD("apply_to_live_volleys", "groups"), &BulletSpawner2D::apply_to_live_volleys, DEFVAL(LIVE_ALL));
	ClassDB::bind_method(D_METHOD("home_live_volleys_to", "target"), &BulletSpawner2D::home_live_volleys_to);
	ClassDB::bind_method(D_METHOD("aim_live_volleys_at", "position"), &BulletSpawner2D::aim_live_volleys_at);
	ClassDB::bind_method(D_METHOD("set_live_volleys_speed", "speed"), &BulletSpawner2D::set_live_volleys_speed);
	ClassDB::bind_method(D_METHOD("scale_live_volleys_speed", "factor"), &BulletSpawner2D::scale_live_volleys_speed);
	// Need this in order to expose the enum constants to Godot Engine
	BIND_ENUM_CONSTANT(HOMING_SHARED);
	BIND_ENUM_CONSTANT(HOMING_PER_BULLET);
	BIND_ENUM_CONSTANT(HOMING_SOURCE_NODE_GROUP);
	BIND_ENUM_CONSTANT(HOMING_SOURCE_MOUSE);
	BIND_ENUM_CONSTANT(HOMING_SOURCE_GLOBAL_POSITION);
	BIND_ENUM_CONSTANT(HOMING_SOURCE_NODE_PATH);
	BIND_ENUM_CONSTANT(HOMING_SOURCE_NODE_NAME);
	BIND_ENUM_CONSTANT(HOMING_SOURCE_NODE_CHILDREN);
	BIND_ENUM_CONSTANT(HOMING_SELECT_NEAREST);
	BIND_ENUM_CONSTANT(HOMING_SELECT_RANDOM);
	BIND_ENUM_CONSTANT(HOMING_SELECT_FIRST);
	BIND_ENUM_CONSTANT(HOMING_SELECT_ROUND_ROBIN);
	BIND_ENUM_CONSTANT(HOMING_SELECT_DISTRIBUTE);
	BIND_ENUM_CONSTANT(HOMING_RETARGET_OFF);
	BIND_ENUM_CONSTANT(HOMING_RETARGET_ON_INTERVAL);
	// Flocking (bullet_spawner2d_flock.cpp): fired volleys join flock.
	ClassDB::bind_method(D_METHOD("get_flock_enabled"), &BulletSpawner2D::get_flock_enabled);
	ClassDB::bind_method(D_METHOD("set_flock_enabled", "value"), &BulletSpawner2D::set_flock_enabled);
	ClassDB::bind_method(D_METHOD("get_flock"), &BulletSpawner2D::get_flock);
	ClassDB::bind_method(D_METHOD("set_flock", "value"), &BulletSpawner2D::set_flock);
	ClassDB::bind_method(D_METHOD("get_flock_scope"), &BulletSpawner2D::get_flock_scope);
	ClassDB::bind_method(D_METHOD("set_flock_scope", "value"), &BulletSpawner2D::set_flock_scope);
	ADD_GROUP("Flocking", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "flock_enabled"), "set_flock_enabled", "get_flock_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "flock", PROPERTY_HINT_RESOURCE_TYPE, "BulletFlock2D"), "set_flock", "get_flock");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "flock_scope", PROPERTY_HINT_ENUM, "Per Spawner:0,Shared:1,Per Volley:2"), "set_flock_scope", "get_flock_scope");
	// Splitting (bullet_spawner2d_split.cpp): fired volleys split by
	// split_data. bullet_split fires here once per parent that split, after
	// its children spawned (same payload on the factory for factory volleys).
	ClassDB::bind_method(D_METHOD("get_split_enabled"), &BulletSpawner2D::get_split_enabled);
	ClassDB::bind_method(D_METHOD("set_split_enabled", "value"), &BulletSpawner2D::set_split_enabled);
	ClassDB::bind_method(D_METHOD("get_split_data"), &BulletSpawner2D::get_split_data);
	ClassDB::bind_method(D_METHOD("set_split_data", "value"), &BulletSpawner2D::set_split_data);
	ADD_GROUP("Splitting", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "split_enabled"), "set_split_enabled", "get_split_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "split_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletSplitData2D"), "set_split_data", "get_split_data");
	// Emitter Bullets (bullet_spawner2d_emitter.cpp): flying bullets fire a
	// template spawner's pattern; the timing and the template hide while off.
	ClassDB::bind_method(D_METHOD("get_emitter_enabled"), &BulletSpawner2D::get_emitter_enabled);
	ClassDB::bind_method(D_METHOD("set_emitter_enabled", "value"), &BulletSpawner2D::set_emitter_enabled);
	ClassDB::bind_method(D_METHOD("get_emitter"), &BulletSpawner2D::get_emitter);
	ClassDB::bind_method(D_METHOD("set_emitter", "value"), &BulletSpawner2D::set_emitter);
	ClassDB::bind_method(D_METHOD("get_emitter_spawner_path"), &BulletSpawner2D::get_emitter_spawner_path);
	ClassDB::bind_method(D_METHOD("set_emitter_spawner_path", "value"), &BulletSpawner2D::set_emitter_spawner_path);
	ClassDB::bind_method(D_METHOD("get_emitter_template"), &BulletSpawner2D::get_emitter_template);
	ClassDB::bind_method(D_METHOD("get_effective_emitter"), &BulletSpawner2D::get_effective_emitter);
	ADD_GROUP("Emitter Bullets", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "emitter_enabled"), "set_emitter_enabled", "get_emitter_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "emitter", PROPERTY_HINT_RESOURCE_TYPE, "BulletEmitterData2D"), "set_emitter", "get_emitter");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "emitter_spawner_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "PatternSpawner2D"), "set_emitter_spawner_path", "get_emitter_spawner_path");
	bind_spawner_group(cls, SPAWNER_GROUP_GRAZE);
	bind_spawner_group(cls, SPAWNER_GROUP_SOUND, SpawnerPropertyFilter2D(), "BulletSoundData2D");
	bind_spawner_group(cls, SPAWNER_GROUP_PREVIEW);
	bind_spawner_group(cls, SPAWNER_GROUP_MOVEMENT);
	bind_spawner_group(cls, SPAWNER_GROUP_PERFORMANCE);
	ADD_SIGNAL(MethodInfo("bullet_split",
			PropertyInfo(Variant::VECTOR2, "position"),
			PropertyInfo(Variant::INT, "cause"),
			PropertyInfo(Variant::OBJECT, "children", PROPERTY_HINT_RESOURCE_TYPE, "BulletVolley2D"),
			PropertyInfo(Variant::INT, "depth")));
	// Graze (bullet_spawner2d_graze.cpp). The signals bubble: they fire here
	// first (while this spawner lives), then on the BulletFactory2D, which
	// receives every graze. Same payloads on both. Live inside the factory
	// tick, right after the move: the bullet is alive in the handler.
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
	ClassDB::bind_method(D_METHOD("debug_explain_features"), &BulletSpawner2D::debug_explain_features);
	BIND_ENUM_CONSTANT(ORPHANED_VOLLEYS_KEEP_FLYING);
	BIND_ENUM_CONSTANT(ORPHANED_VOLLEYS_HAND_TO_FACTORY);
	BIND_ENUM_CONSTANT(ORPHANED_VOLLEYS_CLEAR);
	BIND_ENUM_CONSTANT(ORPHANED_VOLLEYS_REMOVE);
	ClassDB::bind_method(D_METHOD("shoot_once"), &BulletSpawner2D::shoot_once);
}

} // namespace BlastBullets2D
