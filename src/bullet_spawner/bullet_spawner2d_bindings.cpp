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
    // Only the helper_* option groups are gated; everything else is always shown.
    if (!property_name.begins_with("helper_")) {
        return;
    }
    bool relevant = false;
    if (property_name.begins_with("helper_grid_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_GRID;
    } else if (property_name.begins_with("helper_ring_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_RING;
    } else if (property_name.begins_with("helper_fan_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_FAN;
    } else if (property_name.begins_with("helper_spiral_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_SPIRAL;
    } else if (property_name.begins_with("helper_line_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_LINE;
    } else if (property_name.begins_with("helper_aimed_") || property_name == "helper_aimed_target") {
        // The corridor wall reuses the aimed target when one is set, so the
        // shared target knob stays visible in both modes.
        relevant = pattern_source == PATTERN_FROM_HELPER_AIMED || pattern_source == PATTERN_FROM_HELPER_CORRIDOR;
        if (property_name != "helper_aimed_target") {
            relevant = pattern_source == PATTERN_FROM_HELPER_AIMED;
        }
    } else if (property_name.begins_with("helper_flower_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_FLOWER;
        if (relevant) {
            // Clamped defensively: the setter validates, but a raw/desynced
            // value must degrade to hiding knobs, never to a silent wrong set.
            const int ftype = Math::clamp(helper_flower_type, 0, 4);
            // Common bloom knobs (always relevant for FLOWER).
            if (property_name == "helper_flower_type" ||
                    property_name == "helper_flower_radius" ||
                    property_name == "helper_flower_base_rotation" ||
                    property_name == "helper_flower_face_outward" ||
                    property_name == "helper_flower_facing_offset_deg") {
                // keep relevant = true
            } else if (property_name == "helper_flower_petals" ||
                    property_name == "helper_flower_petal_spread") {
                relevant = (ftype == BulletPatterns2D::FLOWER_FAN);
            } else if (property_name == "helper_flower_petal_sharpness") {
                relevant = (ftype == BulletPatterns2D::FLOWER_FAN ||
                        ftype == BulletPatterns2D::FLOWER_RHODONEA);
            } else if (property_name == "helper_flower_inner_radius_scale") {
                relevant = (ftype == BulletPatterns2D::FLOWER_RHODONEA ||
                        ftype == BulletPatterns2D::FLOWER_PHYLLOTAXIS ||
                        ftype == BulletPatterns2D::FLOWER_SUPERFORMULA);
            } else if (property_name == "helper_flower_spiro_roller" ||
                    property_name == "helper_flower_spiro_pen") {
                relevant = (ftype == BulletPatterns2D::FLOWER_SPIROGRAPH);
            } else if (property_name == "helper_flower_super_lobes" ||
                    property_name == "helper_flower_super_fullness") {
                relevant = (ftype == BulletPatterns2D::FLOWER_SUPERFORMULA);
            } else {
                relevant = false;
            }
        }
    } else if (property_name.begins_with("helper_ellipse_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_ELLIPSE;
        // WALL-only knobs: hiding them outside WALL keeps the ellipse group
        // tight (gap_count = 0 alone already disables gaps silently).
        if (relevant && (property_name == "helper_ellipse_gap_count" || property_name == "helper_ellipse_gap_width")) {
            relevant = helper_ellipse_mode == (int)BulletPatterns2D::ELLIPSE_WALL;
        }
    } else if (property_name.begins_with("helper_rain_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_RAIN;
    } else if (property_name.begins_with("helper_scatter_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_SCATTER;
    } else if (property_name.begins_with("helper_star_polygon_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_STAR_POLYGON;
    } else if (property_name.begins_with("helper_multispiral_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_MULTISPIRAL;
    } else if (property_name.begins_with("helper_cross_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_CROSS;
    } else if (property_name.begins_with("helper_star_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_STAR;
    } else if (property_name.begins_with("helper_heart_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_HEART;
    } else if (property_name.begins_with("helper_wave_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_WAVE;
    } else if (property_name.begins_with("helper_waterfall_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_WATERFALL;
    } else if (property_name.begins_with("helper_lattice_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_LATTICE;
    } else if (property_name.begins_with("helper_rose_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_ROSE;
    } else if (property_name.begins_with("helper_counter_spiral_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_COUNTER_SPIRAL;
    } else if (property_name.begins_with("helper_corridor_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_CORRIDOR;
    } else if (property_name.begins_with("helper_lissajous_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_LISSAJOUS;
    } else if (property_name.begins_with("helper_custom_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_CUSTOM;
    } else if (property_name.begins_with("helper_triangle_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_TRIANGLE;
        // size_b is unused by the equilateral kind.
        if (relevant && property_name == "helper_triangle_size_b") {
            relevant = helper_triangle_type != (int)BulletPatterns2D::TRIANGLE_EQUILATERAL;
        }
    } else if (property_name.begins_with("helper_trapezoid_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_TRAPEZOID;
    } else if (property_name.begins_with("helper_diamond_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_DIAMOND;
    } else if (property_name.begins_with("helper_circle_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_CIRCLE;
    } else if (property_name.begins_with("helper_rectangle_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_RECTANGLE;
    } else if (property_name.begins_with("helper_square_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_SQUARE;
    } else if (property_name.begins_with("helper_polygon_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_POLYGON;
    } else if (property_name.begins_with("helper_path2d_")) {
        relevant = pattern_source == PATTERN_FROM_HELPER_PATH2D;
        // Fixed-run knobs only make sense in Fixed Spacing mode.
        if (relevant && (property_name == "helper_path2d_spacing" || property_name == "helper_path2d_overflow" || property_name == "helper_path2d_anchor")) {
            relevant = helper_path2d_distribution == PATH2D_DISTRIBUTION_FIXED_SPACING;
        }
    } else if (property_name.begins_with("helper_outline_")) {
        // Shared outline layout for the closed-loop shapes (scatter and open
        // modes never see these). Fill dims only make sense while filling,
        // layer dims only while layering, and the slot order knobs only
        // while riding outlines (fill grids have no slot order).
        relevant = supports_outline_layout(pattern_source);
        if (relevant && (property_name == "helper_outline_fill_spacing" || property_name == "helper_outline_fill_stagger" || property_name == "helper_outline_fill_margin")) {
            relevant = helper_outline_placement == (int)BulletPatterns2D::OUTLINE_FILL_INSIDE;
        } else if (relevant && (property_name == "helper_outline_layer_count" || property_name == "helper_outline_layer_scale" || property_name == "helper_outline_layer_side" || property_name == "helper_outline_layer_fill" || property_name == "helper_outline_layer_scale_curve" || property_name == "helper_outline_layer_scales" || property_name == "helper_outline_layer_twist" || property_name == "helper_outline_layer_max_dots" || property_name == "helper_outline_layer_layout")) {
            relevant = helper_outline_placement == (int)BulletPatterns2D::OUTLINE_LAYERS;
        } else if (relevant && property_name == "helper_outline_layer_start_offset") {
            relevant = helper_outline_placement == (int)BulletPatterns2D::OUTLINE_LAYERS && (helper_outline_layer_fill == (int)BulletPatterns2D::OUTLINE_LAYER_SEQUENTIAL || helper_outline_layer_fill == (int)BulletPatterns2D::OUTLINE_LAYER_OUTER_FIRST);
        } else if (relevant && (property_name == "helper_outline_reverse" || property_name == "helper_outline_slot_offset")) {
            relevant = helper_outline_placement == (int)BulletPatterns2D::OUTLINE_ON_OUTLINE || helper_outline_placement == (int)BulletPatterns2D::OUTLINE_LAYERS;
        } else if (relevant && property_name == "helper_outline_distribution") {
            // Distribution only steers corner-anchored apportionment (smooth
            // loops resample evenly regardless): hide it where it is a dead
            // knob instead of implying control it does not have.
            relevant = (helper_outline_placement == (int)BulletPatterns2D::OUTLINE_ON_OUTLINE || helper_outline_placement == (int)BulletPatterns2D::OUTLINE_LAYERS) && supports_corner_layout(pattern_source);
        } else if (relevant && (property_name == "helper_outline_corner_priority" || property_name == "helper_outline_corner_mode")) {
            relevant = (helper_outline_placement == (int)BulletPatterns2D::OUTLINE_ON_OUTLINE || helper_outline_placement == (int)BulletPatterns2D::OUTLINE_LAYERS) && supports_corner_layout(pattern_source);
        } else if (relevant && property_name == "helper_outline_edge_margin") {
            relevant = (helper_outline_placement == (int)BulletPatterns2D::OUTLINE_ON_OUTLINE || helper_outline_placement == (int)BulletPatterns2D::OUTLINE_LAYERS) && supports_corner_layout(pattern_source) && helper_outline_corner_mode == (int)BulletPatterns2D::OUTLINE_CORNER_MODE_PIN_CORNERS;
        }
    } else if (property_name == "helper_skip_indices") {
        relevant = pattern_source >= PATTERN_FROM_HELPER_GRID;
    } else if (property_name == "helper_bullets_amount") {
        // Custom counts from its stored array, so the shared count would be
        // a dead knob there: hide it instead of confusing.
        relevant = pattern_source >= PATTERN_FROM_HELPER_GRID && pattern_source != PATTERN_FROM_HELPER_CUSTOM;
    }
    if (!relevant) {
        p_property.usage &= ~PROPERTY_USAGE_EDITOR;
    }
}

void BulletSpawner2D::_bind_methods() {
    // Inspector layout: Setup (wiring) -> Bullet Patterns (source, amount,
    // Transform subgroup, one subgroup per shape with its helper_<shape>_
    // prefix stripped, Outline Layers last) -> Shooting -> Spin -> Homing ->
    // Orbiting -> Preview -> Movement -> Performance. Property NAMES never
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
	ClassDB::bind_method(D_METHOD("get_pattern_source"), &BulletSpawner2D::get_pattern_source);
	ClassDB::bind_method(D_METHOD("set_pattern_source", "value"), &BulletSpawner2D::set_pattern_source);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "pattern_source", PROPERTY_HINT_ENUM, pattern_source_hint()), "set_pattern_source", "get_pattern_source");

	ClassDB::bind_method(D_METHOD("get_helper_bullets_amount"), &BulletSpawner2D::get_helper_bullets_amount);
	ClassDB::bind_method(D_METHOD("set_helper_bullets_amount", "value"), &BulletSpawner2D::set_helper_bullets_amount);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_bullets_amount"), "set_helper_bullets_amount", "get_helper_bullets_amount");

	ADD_SUBGROUP("Transform", "");
	ClassDB::bind_method(D_METHOD("get_pattern_scale"), &BulletSpawner2D::get_pattern_scale);
	ClassDB::bind_method(D_METHOD("set_pattern_scale", "value"), &BulletSpawner2D::set_pattern_scale);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pattern_scale"), "set_pattern_scale", "get_pattern_scale");

	ClassDB::bind_method(D_METHOD("get_transforms_scale"), &BulletSpawner2D::get_transforms_scale);
	ClassDB::bind_method(D_METHOD("set_transforms_scale", "value"), &BulletSpawner2D::set_transforms_scale);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "transforms_scale"), "set_transforms_scale", "get_transforms_scale");

	ClassDB::bind_method(D_METHOD("get_spawn_position_offset"), &BulletSpawner2D::get_spawn_position_offset);
	ClassDB::bind_method(D_METHOD("set_spawn_position_offset", "value"), &BulletSpawner2D::set_spawn_position_offset);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "spawn_position_offset"), "set_spawn_position_offset", "get_spawn_position_offset");
	ClassDB::bind_method(D_METHOD("get_spawn_position_offset_space"), &BulletSpawner2D::get_spawn_position_offset_space);
	ClassDB::bind_method(D_METHOD("set_spawn_position_offset_space", "value"), &BulletSpawner2D::set_spawn_position_offset_space);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "spawn_position_offset_space", PROPERTY_HINT_ENUM, "Global:0,Local:1"), "set_spawn_position_offset_space", "get_spawn_position_offset_space");
	BIND_ENUM_CONSTANT(SPAWN_OFFSET_GLOBAL);
	BIND_ENUM_CONSTANT(SPAWN_OFFSET_LOCAL);

	ClassDB::bind_method(D_METHOD("get_helper_skip_indices"), &BulletSpawner2D::get_helper_skip_indices);
	ClassDB::bind_method(D_METHOD("set_helper_skip_indices", "value"), &BulletSpawner2D::set_helper_skip_indices);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "helper_skip_indices"), "set_helper_skip_indices", "get_helper_skip_indices");

	ADD_SUBGROUP("Grid", "helper_grid_");
	ClassDB::bind_method(D_METHOD("get_helper_grid_rows_per_column"), &BulletSpawner2D::get_helper_grid_rows_per_column);
	ClassDB::bind_method(D_METHOD("set_helper_grid_rows_per_column", "value"), &BulletSpawner2D::set_helper_grid_rows_per_column);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_grid_rows_per_column"), "set_helper_grid_rows_per_column", "get_helper_grid_rows_per_column");

	ClassDB::bind_method(D_METHOD("get_helper_grid_alignment"), &BulletSpawner2D::get_helper_grid_alignment);
	ClassDB::bind_method(D_METHOD("set_helper_grid_alignment", "value"), &BulletSpawner2D::set_helper_grid_alignment);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_grid_alignment", PROPERTY_HINT_ENUM, "Top Left,Top Center,Top Right,Center Left,Center,Center Right,Bottom Left,Bottom Center,Bottom Right"), "set_helper_grid_alignment", "get_helper_grid_alignment");

	ClassDB::bind_method(D_METHOD("get_helper_grid_column_offset"), &BulletSpawner2D::get_helper_grid_column_offset);
	ClassDB::bind_method(D_METHOD("set_helper_grid_column_offset", "value"), &BulletSpawner2D::set_helper_grid_column_offset);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_grid_column_offset"), "set_helper_grid_column_offset", "get_helper_grid_column_offset");

	ClassDB::bind_method(D_METHOD("get_helper_grid_row_offset"), &BulletSpawner2D::get_helper_grid_row_offset);
	ClassDB::bind_method(D_METHOD("set_helper_grid_row_offset", "value"), &BulletSpawner2D::set_helper_grid_row_offset);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_grid_row_offset"), "set_helper_grid_row_offset", "get_helper_grid_row_offset");

	ClassDB::bind_method(D_METHOD("get_helper_grid_rotate_with_marker"), &BulletSpawner2D::get_helper_grid_rotate_with_marker);
	ClassDB::bind_method(D_METHOD("set_helper_grid_rotate_with_marker", "value"), &BulletSpawner2D::set_helper_grid_rotate_with_marker);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_grid_rotate_with_marker"), "set_helper_grid_rotate_with_marker", "get_helper_grid_rotate_with_marker");

	ClassDB::bind_method(D_METHOD("get_helper_grid_random_local_rotation"), &BulletSpawner2D::get_helper_grid_random_local_rotation);
	ClassDB::bind_method(D_METHOD("set_helper_grid_random_local_rotation", "value"), &BulletSpawner2D::set_helper_grid_random_local_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_grid_random_local_rotation"), "set_helper_grid_random_local_rotation", "get_helper_grid_random_local_rotation");

	ClassDB::bind_method(D_METHOD("get_helper_grid_jitter"), &BulletSpawner2D::get_helper_grid_jitter);
	ClassDB::bind_method(D_METHOD("set_helper_grid_jitter", "value"), &BulletSpawner2D::set_helper_grid_jitter);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_grid_jitter"), "set_helper_grid_jitter", "get_helper_grid_jitter");
	ClassDB::bind_method(D_METHOD("get_helper_grid_seed"), &BulletSpawner2D::get_helper_grid_seed);
	ClassDB::bind_method(D_METHOD("set_helper_grid_seed", "value"), &BulletSpawner2D::set_helper_grid_seed);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_grid_seed"), "set_helper_grid_seed", "get_helper_grid_seed");

	ADD_SUBGROUP("Ring", "helper_ring_");
	ClassDB::bind_method(D_METHOD("get_helper_ring_radius"), &BulletSpawner2D::get_helper_ring_radius);
	ClassDB::bind_method(D_METHOD("set_helper_ring_radius", "value"), &BulletSpawner2D::set_helper_ring_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_ring_radius"), "set_helper_ring_radius", "get_helper_ring_radius");

	ClassDB::bind_method(D_METHOD("get_helper_ring_start_angle"), &BulletSpawner2D::get_helper_ring_start_angle);
	ClassDB::bind_method(D_METHOD("set_helper_ring_start_angle", "value"), &BulletSpawner2D::set_helper_ring_start_angle);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_ring_start_angle"), "set_helper_ring_start_angle", "get_helper_ring_start_angle");

	ClassDB::bind_method(D_METHOD("get_helper_ring_arc"), &BulletSpawner2D::get_helper_ring_arc);
	ClassDB::bind_method(D_METHOD("set_helper_ring_arc", "value"), &BulletSpawner2D::set_helper_ring_arc);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_ring_arc"), "set_helper_ring_arc", "get_helper_ring_arc");

	ClassDB::bind_method(D_METHOD("get_helper_ring_rotate_with_marker"), &BulletSpawner2D::get_helper_ring_rotate_with_marker);
	ClassDB::bind_method(D_METHOD("set_helper_ring_rotate_with_marker", "value"), &BulletSpawner2D::set_helper_ring_rotate_with_marker);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_ring_rotate_with_marker"), "set_helper_ring_rotate_with_marker", "get_helper_ring_rotate_with_marker");

	ClassDB::bind_method(D_METHOD("get_helper_ring_random_rotation"), &BulletSpawner2D::get_helper_ring_random_rotation);
	ClassDB::bind_method(D_METHOD("set_helper_ring_random_rotation", "value"), &BulletSpawner2D::set_helper_ring_random_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_ring_random_rotation"), "set_helper_ring_random_rotation", "get_helper_ring_random_rotation");
	ClassDB::bind_method(D_METHOD("get_helper_ring_seed"), &BulletSpawner2D::get_helper_ring_seed);
	ClassDB::bind_method(D_METHOD("set_helper_ring_seed", "value"), &BulletSpawner2D::set_helper_ring_seed);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_ring_seed"), "set_helper_ring_seed", "get_helper_ring_seed");

	ClassDB::bind_method(D_METHOD("get_helper_ring_face_outward"), &BulletSpawner2D::get_helper_ring_face_outward);
	ClassDB::bind_method(D_METHOD("set_helper_ring_face_outward", "value"), &BulletSpawner2D::set_helper_ring_face_outward);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_ring_face_outward"), "set_helper_ring_face_outward", "get_helper_ring_face_outward");

	ClassDB::bind_method(D_METHOD("get_helper_ring_y_scale"), &BulletSpawner2D::get_helper_ring_y_scale);
	ClassDB::bind_method(D_METHOD("set_helper_ring_y_scale", "value"), &BulletSpawner2D::set_helper_ring_y_scale);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_ring_y_scale"), "set_helper_ring_y_scale", "get_helper_ring_y_scale");

	ClassDB::bind_method(D_METHOD("get_helper_ring_facing_offset_deg"), &BulletSpawner2D::get_helper_ring_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_ring_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_ring_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_ring_facing_offset_deg"), "set_helper_ring_facing_offset_deg", "get_helper_ring_facing_offset_deg");

	ADD_SUBGROUP("Fan", "helper_fan_");
	ClassDB::bind_method(D_METHOD("get_helper_fan_spread"), &BulletSpawner2D::get_helper_fan_spread);
	ClassDB::bind_method(D_METHOD("set_helper_fan_spread", "value"), &BulletSpawner2D::set_helper_fan_spread);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_fan_spread"), "set_helper_fan_spread", "get_helper_fan_spread");

	ClassDB::bind_method(D_METHOD("get_helper_fan_direction_angle"), &BulletSpawner2D::get_helper_fan_direction_angle);
	ClassDB::bind_method(D_METHOD("set_helper_fan_direction_angle", "value"), &BulletSpawner2D::set_helper_fan_direction_angle);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_fan_direction_angle"), "set_helper_fan_direction_angle", "get_helper_fan_direction_angle");

	ClassDB::bind_method(D_METHOD("get_helper_fan_step_offset"), &BulletSpawner2D::get_helper_fan_step_offset);
	ClassDB::bind_method(D_METHOD("set_helper_fan_step_offset", "value"), &BulletSpawner2D::set_helper_fan_step_offset);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_fan_step_offset"), "set_helper_fan_step_offset", "get_helper_fan_step_offset");

	ClassDB::bind_method(D_METHOD("get_helper_fan_centered"), &BulletSpawner2D::get_helper_fan_centered);
	ClassDB::bind_method(D_METHOD("set_helper_fan_centered", "value"), &BulletSpawner2D::set_helper_fan_centered);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_fan_centered"), "set_helper_fan_centered", "get_helper_fan_centered");

	ClassDB::bind_method(D_METHOD("get_helper_fan_angle_jitter"), &BulletSpawner2D::get_helper_fan_angle_jitter);
	ClassDB::bind_method(D_METHOD("set_helper_fan_angle_jitter", "value"), &BulletSpawner2D::set_helper_fan_angle_jitter);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_fan_angle_jitter"), "set_helper_fan_angle_jitter", "get_helper_fan_angle_jitter");
	ClassDB::bind_method(D_METHOD("get_helper_fan_seed"), &BulletSpawner2D::get_helper_fan_seed);
	ClassDB::bind_method(D_METHOD("set_helper_fan_seed", "value"), &BulletSpawner2D::set_helper_fan_seed);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_fan_seed"), "set_helper_fan_seed", "get_helper_fan_seed");

	ADD_SUBGROUP("Spiral", "helper_spiral_");
	ClassDB::bind_method(D_METHOD("get_helper_spiral_start_radius"), &BulletSpawner2D::get_helper_spiral_start_radius);
	ClassDB::bind_method(D_METHOD("set_helper_spiral_start_radius", "value"), &BulletSpawner2D::set_helper_spiral_start_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_spiral_start_radius"), "set_helper_spiral_start_radius", "get_helper_spiral_start_radius");

	ClassDB::bind_method(D_METHOD("get_helper_spiral_radius_step"), &BulletSpawner2D::get_helper_spiral_radius_step);
	ClassDB::bind_method(D_METHOD("set_helper_spiral_radius_step", "value"), &BulletSpawner2D::set_helper_spiral_radius_step);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_spiral_radius_step"), "set_helper_spiral_radius_step", "get_helper_spiral_radius_step");

	ClassDB::bind_method(D_METHOD("get_helper_spiral_angle_step"), &BulletSpawner2D::get_helper_spiral_angle_step);
	ClassDB::bind_method(D_METHOD("set_helper_spiral_angle_step", "value"), &BulletSpawner2D::set_helper_spiral_angle_step);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_spiral_angle_step"), "set_helper_spiral_angle_step", "get_helper_spiral_angle_step");

	ClassDB::bind_method(D_METHOD("get_helper_spiral_rotate_with_marker"), &BulletSpawner2D::get_helper_spiral_rotate_with_marker);
	ClassDB::bind_method(D_METHOD("set_helper_spiral_rotate_with_marker", "value"), &BulletSpawner2D::set_helper_spiral_rotate_with_marker);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_spiral_rotate_with_marker"), "set_helper_spiral_rotate_with_marker", "get_helper_spiral_rotate_with_marker");

	ClassDB::bind_method(D_METHOD("get_helper_spiral_facing"), &BulletSpawner2D::get_helper_spiral_facing);
	ClassDB::bind_method(D_METHOD("set_helper_spiral_facing", "value"), &BulletSpawner2D::set_helper_spiral_facing);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_spiral_facing", PROPERTY_HINT_ENUM, "Tangent,Radial Outward,Toward Center,Keep Marker"), "set_helper_spiral_facing", "get_helper_spiral_facing");

	ClassDB::bind_method(D_METHOD("get_helper_spiral_facing_offset_deg"), &BulletSpawner2D::get_helper_spiral_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_spiral_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_spiral_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_spiral_facing_offset_deg"), "set_helper_spiral_facing_offset_deg", "get_helper_spiral_facing_offset_deg");

	ADD_SUBGROUP("Line", "helper_line_");
	ClassDB::bind_method(D_METHOD("get_helper_line_direction"), &BulletSpawner2D::get_helper_line_direction);
	ClassDB::bind_method(D_METHOD("set_helper_line_direction", "value"), &BulletSpawner2D::set_helper_line_direction);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "helper_line_direction"), "set_helper_line_direction", "get_helper_line_direction");

	ClassDB::bind_method(D_METHOD("get_helper_line_spacing"), &BulletSpawner2D::get_helper_line_spacing);
	ClassDB::bind_method(D_METHOD("set_helper_line_spacing", "value"), &BulletSpawner2D::set_helper_line_spacing);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_line_spacing"), "set_helper_line_spacing", "get_helper_line_spacing");

	ClassDB::bind_method(D_METHOD("get_helper_line_face_direction"), &BulletSpawner2D::get_helper_line_face_direction);
	ClassDB::bind_method(D_METHOD("set_helper_line_face_direction", "value"), &BulletSpawner2D::set_helper_line_face_direction);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_line_face_direction"), "set_helper_line_face_direction", "get_helper_line_face_direction");

	ClassDB::bind_method(D_METHOD("get_helper_line_anchor"), &BulletSpawner2D::get_helper_line_anchor);
	ClassDB::bind_method(D_METHOD("set_helper_line_anchor", "value"), &BulletSpawner2D::set_helper_line_anchor);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_line_anchor", PROPERTY_HINT_ENUM, "Start,Center,End"), "set_helper_line_anchor", "get_helper_line_anchor");

	ClassDB::bind_method(D_METHOD("get_helper_line_facing"), &BulletSpawner2D::get_helper_line_facing);
	ClassDB::bind_method(D_METHOD("set_helper_line_facing", "value"), &BulletSpawner2D::set_helper_line_facing);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_line_facing", PROPERTY_HINT_ENUM, "Along the Line,+90 Degrees,-90 Degrees"), "set_helper_line_facing", "get_helper_line_facing");

	ClassDB::bind_method(D_METHOD("get_helper_line_reverse"), &BulletSpawner2D::get_helper_line_reverse);
	ClassDB::bind_method(D_METHOD("set_helper_line_reverse", "value"), &BulletSpawner2D::set_helper_line_reverse);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_line_reverse"), "set_helper_line_reverse", "get_helper_line_reverse");

	ClassDB::bind_method(D_METHOD("get_helper_line_slot_offset"), &BulletSpawner2D::get_helper_line_slot_offset);
	ClassDB::bind_method(D_METHOD("set_helper_line_slot_offset", "value"), &BulletSpawner2D::set_helper_line_slot_offset);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_line_slot_offset"), "set_helper_line_slot_offset", "get_helper_line_slot_offset");

	ClassDB::bind_method(D_METHOD("get_helper_line_start_offset"), &BulletSpawner2D::get_helper_line_start_offset);
	ClassDB::bind_method(D_METHOD("set_helper_line_start_offset", "value"), &BulletSpawner2D::set_helper_line_start_offset);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_line_start_offset"), "set_helper_line_start_offset", "get_helper_line_start_offset");

	ADD_SUBGROUP("Aimed", "helper_aimed_");
	ClassDB::bind_method(D_METHOD("get_helper_aimed_target_path"), &BulletSpawner2D::get_helper_aimed_target_path);
	ClassDB::bind_method(D_METHOD("set_helper_aimed_target_path", "path"), &BulletSpawner2D::set_helper_aimed_target_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "helper_aimed_target", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node2D"), "set_helper_aimed_target_path", "get_helper_aimed_target_path");

	ClassDB::bind_method(D_METHOD("get_helper_aimed_target"), &BulletSpawner2D::get_helper_aimed_target);
	ClassDB::bind_method(D_METHOD("set_helper_aimed_target", "target"), &BulletSpawner2D::set_helper_aimed_target);

	ClassDB::bind_method(D_METHOD("get_helper_aimed_spread"), &BulletSpawner2D::get_helper_aimed_spread);
	ClassDB::bind_method(D_METHOD("set_helper_aimed_spread", "value"), &BulletSpawner2D::set_helper_aimed_spread);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_aimed_spread"), "set_helper_aimed_spread", "get_helper_aimed_spread");

	ClassDB::bind_method(D_METHOD("get_helper_aimed_step_offset"), &BulletSpawner2D::get_helper_aimed_step_offset);
	ClassDB::bind_method(D_METHOD("set_helper_aimed_step_offset", "value"), &BulletSpawner2D::set_helper_aimed_step_offset);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_aimed_step_offset"), "set_helper_aimed_step_offset", "get_helper_aimed_step_offset");

	ClassDB::bind_method(D_METHOD("get_helper_aimed_centered"), &BulletSpawner2D::get_helper_aimed_centered);
	ClassDB::bind_method(D_METHOD("set_helper_aimed_centered", "value"), &BulletSpawner2D::set_helper_aimed_centered);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_aimed_centered"), "set_helper_aimed_centered", "get_helper_aimed_centered");

	ClassDB::bind_method(D_METHOD("get_helper_aimed_prediction"), &BulletSpawner2D::get_helper_aimed_prediction);
	ClassDB::bind_method(D_METHOD("set_helper_aimed_prediction", "value"), &BulletSpawner2D::set_helper_aimed_prediction);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_aimed_prediction"), "set_helper_aimed_prediction", "get_helper_aimed_prediction");

	ClassDB::bind_method(D_METHOD("get_helper_aimed_prediction_time"), &BulletSpawner2D::get_helper_aimed_prediction_time);
	ClassDB::bind_method(D_METHOD("set_helper_aimed_prediction_time", "value"), &BulletSpawner2D::set_helper_aimed_prediction_time);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_aimed_prediction_time"), "set_helper_aimed_prediction_time", "get_helper_aimed_prediction_time");

	ADD_SUBGROUP("Flower", "helper_flower_");
	// NOTE: the bloom-kind selector is intentionally FIRST so it is the first
	// thing a user configures; the per-type knobs grouped right after it are
	// shown/hidden by _validate_property based on the chosen kind.
	ClassDB::bind_method(D_METHOD("get_helper_flower_type"), &BulletSpawner2D::get_helper_flower_type);
	ClassDB::bind_method(D_METHOD("set_helper_flower_type", "value"), &BulletSpawner2D::set_helper_flower_type);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_flower_type", PropertyHint::PROPERTY_HINT_ENUM, "FAN,RHODONEA,PHYLLOTAXIS,SPIROGRAPH,SUPERFORMULA"), "set_helper_flower_type", "get_helper_flower_type");

	ClassDB::bind_method(D_METHOD("get_helper_flower_radius"), &BulletSpawner2D::get_helper_flower_radius);
	ClassDB::bind_method(D_METHOD("set_helper_flower_radius", "value"), &BulletSpawner2D::set_helper_flower_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_flower_radius"), "set_helper_flower_radius", "get_helper_flower_radius");

	ClassDB::bind_method(D_METHOD("get_helper_flower_base_rotation"), &BulletSpawner2D::get_helper_flower_base_rotation);
	ClassDB::bind_method(D_METHOD("set_helper_flower_base_rotation", "value"), &BulletSpawner2D::set_helper_flower_base_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_flower_base_rotation"), "set_helper_flower_base_rotation", "get_helper_flower_base_rotation");

	ClassDB::bind_method(D_METHOD("get_helper_flower_face_outward"), &BulletSpawner2D::get_helper_flower_face_outward);
	ClassDB::bind_method(D_METHOD("set_helper_flower_face_outward", "value"), &BulletSpawner2D::set_helper_flower_face_outward);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_flower_face_outward"), "set_helper_flower_face_outward", "get_helper_flower_face_outward");

	ClassDB::bind_method(D_METHOD("get_helper_flower_facing_offset_deg"), &BulletSpawner2D::get_helper_flower_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_flower_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_flower_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_flower_facing_offset_deg"), "set_helper_flower_facing_offset_deg", "get_helper_flower_facing_offset_deg");

	// RHODONEA / PHYLLOTAXIS / SUPERFORMULA: core-hole lift.
	ClassDB::bind_method(D_METHOD("get_helper_flower_inner_radius_scale"), &BulletSpawner2D::get_helper_flower_inner_radius_scale);
	ClassDB::bind_method(D_METHOD("set_helper_flower_inner_radius_scale", "value"), &BulletSpawner2D::set_helper_flower_inner_radius_scale);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_flower_inner_radius_scale", PropertyHint::PROPERTY_HINT_RANGE, "0,0.999,0.001"), "set_helper_flower_inner_radius_scale", "get_helper_flower_inner_radius_scale");

	// FAN: lobe count + per-lobe fan controls.
	ClassDB::bind_method(D_METHOD("get_helper_flower_petals"), &BulletSpawner2D::get_helper_flower_petals);
	ClassDB::bind_method(D_METHOD("set_helper_flower_petals", "value"), &BulletSpawner2D::set_helper_flower_petals);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_flower_petals"), "set_helper_flower_petals", "get_helper_flower_petals");

	ClassDB::bind_method(D_METHOD("get_helper_flower_petal_spread"), &BulletSpawner2D::get_helper_flower_petal_spread);
	ClassDB::bind_method(D_METHOD("set_helper_flower_petal_spread", "value"), &BulletSpawner2D::set_helper_flower_petal_spread);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_flower_petal_spread"), "set_helper_flower_petal_spread", "get_helper_flower_petal_spread");

	// FAN + RHODONEA: waist pinch between lobes.
	ClassDB::bind_method(D_METHOD("get_helper_flower_petal_sharpness"), &BulletSpawner2D::get_helper_flower_petal_sharpness);
	ClassDB::bind_method(D_METHOD("set_helper_flower_petal_sharpness", "value"), &BulletSpawner2D::set_helper_flower_petal_sharpness);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_flower_petal_sharpness"), "set_helper_flower_petal_sharpness", "get_helper_flower_petal_sharpness");

	// SPIROGRAPH: hypotrochoid roller radius r (> 0) and pen offset d (>= 0).
	ClassDB::bind_method(D_METHOD("get_helper_flower_spiro_roller"), &BulletSpawner2D::get_helper_flower_spiro_roller);
	ClassDB::bind_method(D_METHOD("set_helper_flower_spiro_roller", "value"), &BulletSpawner2D::set_helper_flower_spiro_roller);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_flower_spiro_roller", PropertyHint::PROPERTY_HINT_RANGE, "0.5,2048,0.5,greater_than,0"), "set_helper_flower_spiro_roller", "get_helper_flower_spiro_roller");

	ClassDB::bind_method(D_METHOD("get_helper_flower_spiro_pen"), &BulletSpawner2D::get_helper_flower_spiro_pen);
	ClassDB::bind_method(D_METHOD("set_helper_flower_spiro_pen", "value"), &BulletSpawner2D::set_helper_flower_spiro_pen);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_flower_spiro_pen", PropertyHint::PROPERTY_HINT_RANGE, "0,2048,0.5"), "set_helper_flower_spiro_pen", "get_helper_flower_spiro_pen");

	// SUPERFORMULA: lobe count m and fullness exponent.
	ClassDB::bind_method(D_METHOD("get_helper_flower_super_lobes"), &BulletSpawner2D::get_helper_flower_super_lobes);
	ClassDB::bind_method(D_METHOD("set_helper_flower_super_lobes", "value"), &BulletSpawner2D::set_helper_flower_super_lobes);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_flower_super_lobes", PropertyHint::PROPERTY_HINT_RANGE, "2,64,1"), "set_helper_flower_super_lobes", "get_helper_flower_super_lobes");

	ClassDB::bind_method(D_METHOD("get_helper_flower_super_fullness"), &BulletSpawner2D::get_helper_flower_super_fullness);
	ClassDB::bind_method(D_METHOD("set_helper_flower_super_fullness", "value"), &BulletSpawner2D::set_helper_flower_super_fullness);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_flower_super_fullness", PropertyHint::PROPERTY_HINT_RANGE, "0.05,8,0.05,greater_than,0"), "set_helper_flower_super_fullness", "get_helper_flower_super_fullness");

	ADD_SUBGROUP("Ellipse", "helper_ellipse_");
	ClassDB::bind_method(D_METHOD("get_helper_ellipse_radius_x"), &BulletSpawner2D::get_helper_ellipse_radius_x);
	ClassDB::bind_method(D_METHOD("set_helper_ellipse_radius_x", "value"), &BulletSpawner2D::set_helper_ellipse_radius_x);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_ellipse_radius_x"), "set_helper_ellipse_radius_x", "get_helper_ellipse_radius_x");

	ClassDB::bind_method(D_METHOD("get_helper_ellipse_radius_y"), &BulletSpawner2D::get_helper_ellipse_radius_y);
	ClassDB::bind_method(D_METHOD("set_helper_ellipse_radius_y", "value"), &BulletSpawner2D::set_helper_ellipse_radius_y);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_ellipse_radius_y"), "set_helper_ellipse_radius_y", "get_helper_ellipse_radius_y");

	ClassDB::bind_method(D_METHOD("get_helper_ellipse_rotation"), &BulletSpawner2D::get_helper_ellipse_rotation);
	ClassDB::bind_method(D_METHOD("set_helper_ellipse_rotation", "value"), &BulletSpawner2D::set_helper_ellipse_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_ellipse_rotation"), "set_helper_ellipse_rotation", "get_helper_ellipse_rotation");

	ClassDB::bind_method(D_METHOD("get_helper_ellipse_start_angle"), &BulletSpawner2D::get_helper_ellipse_start_angle);
	ClassDB::bind_method(D_METHOD("set_helper_ellipse_start_angle", "value"), &BulletSpawner2D::set_helper_ellipse_start_angle);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_ellipse_start_angle"), "set_helper_ellipse_start_angle", "get_helper_ellipse_start_angle");

	ClassDB::bind_method(D_METHOD("get_helper_ellipse_arc"), &BulletSpawner2D::get_helper_ellipse_arc);
	ClassDB::bind_method(D_METHOD("set_helper_ellipse_arc", "value"), &BulletSpawner2D::set_helper_ellipse_arc);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_ellipse_arc"), "set_helper_ellipse_arc", "get_helper_ellipse_arc");

	ClassDB::bind_method(D_METHOD("get_helper_ellipse_mode"), &BulletSpawner2D::get_helper_ellipse_mode);
	ClassDB::bind_method(D_METHOD("set_helper_ellipse_mode", "value"), &BulletSpawner2D::set_helper_ellipse_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_ellipse_mode", PROPERTY_HINT_ENUM, "Full,Arc,Wall"), "set_helper_ellipse_mode", "get_helper_ellipse_mode");

	ClassDB::bind_method(D_METHOD("get_helper_ellipse_gap_count"), &BulletSpawner2D::get_helper_ellipse_gap_count);
	ClassDB::bind_method(D_METHOD("set_helper_ellipse_gap_count", "value"), &BulletSpawner2D::set_helper_ellipse_gap_count);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_ellipse_gap_count"), "set_helper_ellipse_gap_count", "get_helper_ellipse_gap_count");

	ClassDB::bind_method(D_METHOD("get_helper_ellipse_gap_width"), &BulletSpawner2D::get_helper_ellipse_gap_width);
	ClassDB::bind_method(D_METHOD("set_helper_ellipse_gap_width", "value"), &BulletSpawner2D::set_helper_ellipse_gap_width);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_ellipse_gap_width"), "set_helper_ellipse_gap_width", "get_helper_ellipse_gap_width");

	ClassDB::bind_method(D_METHOD("get_helper_ellipse_face_outward"), &BulletSpawner2D::get_helper_ellipse_face_outward);
	ClassDB::bind_method(D_METHOD("set_helper_ellipse_face_outward", "value"), &BulletSpawner2D::set_helper_ellipse_face_outward);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_ellipse_face_outward"), "set_helper_ellipse_face_outward", "get_helper_ellipse_face_outward");

	ClassDB::bind_method(D_METHOD("get_helper_ellipse_facing_offset_deg"), &BulletSpawner2D::get_helper_ellipse_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_ellipse_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_ellipse_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_ellipse_facing_offset_deg"), "set_helper_ellipse_facing_offset_deg", "get_helper_ellipse_facing_offset_deg");

	ADD_SUBGROUP("Rain", "helper_rain_");
	ClassDB::bind_method(D_METHOD("get_helper_rain_band_width"), &BulletSpawner2D::get_helper_rain_band_width);
	ClassDB::bind_method(D_METHOD("set_helper_rain_band_width", "value"), &BulletSpawner2D::set_helper_rain_band_width);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_rain_band_width"), "set_helper_rain_band_width", "get_helper_rain_band_width");

	ClassDB::bind_method(D_METHOD("get_helper_rain_direction"), &BulletSpawner2D::get_helper_rain_direction);
	ClassDB::bind_method(D_METHOD("set_helper_rain_direction", "value"), &BulletSpawner2D::set_helper_rain_direction);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "helper_rain_direction"), "set_helper_rain_direction", "get_helper_rain_direction");

	ClassDB::bind_method(D_METHOD("get_helper_rain_drop_spacing"), &BulletSpawner2D::get_helper_rain_drop_spacing);
	ClassDB::bind_method(D_METHOD("set_helper_rain_drop_spacing", "value"), &BulletSpawner2D::set_helper_rain_drop_spacing);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_rain_drop_spacing"), "set_helper_rain_drop_spacing", "get_helper_rain_drop_spacing");

	ClassDB::bind_method(D_METHOD("get_helper_rain_jitter"), &BulletSpawner2D::get_helper_rain_jitter);
	ClassDB::bind_method(D_METHOD("set_helper_rain_jitter", "value"), &BulletSpawner2D::set_helper_rain_jitter);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_rain_jitter"), "set_helper_rain_jitter", "get_helper_rain_jitter");
	ClassDB::bind_method(D_METHOD("get_helper_rain_seed"), &BulletSpawner2D::get_helper_rain_seed);
	ClassDB::bind_method(D_METHOD("set_helper_rain_seed", "value"), &BulletSpawner2D::set_helper_rain_seed);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_rain_seed"), "set_helper_rain_seed", "get_helper_rain_seed");

	ADD_SUBGROUP("Scatter", "helper_scatter_");
	ClassDB::bind_method(D_METHOD("get_helper_scatter_burst_radius"), &BulletSpawner2D::get_helper_scatter_burst_radius);
	ClassDB::bind_method(D_METHOD("set_helper_scatter_burst_radius", "value"), &BulletSpawner2D::set_helper_scatter_burst_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_scatter_burst_radius"), "set_helper_scatter_burst_radius", "get_helper_scatter_burst_radius");

	ClassDB::bind_method(D_METHOD("get_helper_scatter_facing_jitter"), &BulletSpawner2D::get_helper_scatter_facing_jitter);
	ClassDB::bind_method(D_METHOD("set_helper_scatter_facing_jitter", "value"), &BulletSpawner2D::set_helper_scatter_facing_jitter);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_scatter_facing_jitter"), "set_helper_scatter_facing_jitter", "get_helper_scatter_facing_jitter");

	ClassDB::bind_method(D_METHOD("get_helper_scatter_seed"), &BulletSpawner2D::get_helper_scatter_seed);
	ClassDB::bind_method(D_METHOD("set_helper_scatter_seed", "value"), &BulletSpawner2D::set_helper_scatter_seed);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_scatter_seed"), "set_helper_scatter_seed", "get_helper_scatter_seed");

	ClassDB::bind_method(D_METHOD("get_helper_scatter_inner_radius"), &BulletSpawner2D::get_helper_scatter_inner_radius);
	ClassDB::bind_method(D_METHOD("set_helper_scatter_inner_radius", "value"), &BulletSpawner2D::set_helper_scatter_inner_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_scatter_inner_radius"), "set_helper_scatter_inner_radius", "get_helper_scatter_inner_radius");

	ClassDB::bind_method(D_METHOD("get_helper_scatter_direction"), &BulletSpawner2D::get_helper_scatter_direction);
	ClassDB::bind_method(D_METHOD("set_helper_scatter_direction", "value"), &BulletSpawner2D::set_helper_scatter_direction);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "helper_scatter_direction"), "set_helper_scatter_direction", "get_helper_scatter_direction");

	ClassDB::bind_method(D_METHOD("get_helper_scatter_arc"), &BulletSpawner2D::get_helper_scatter_arc);
	ClassDB::bind_method(D_METHOD("set_helper_scatter_arc", "value"), &BulletSpawner2D::set_helper_scatter_arc);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_scatter_arc"), "set_helper_scatter_arc", "get_helper_scatter_arc");

	ClassDB::bind_method(D_METHOD("get_helper_scatter_facing"), &BulletSpawner2D::get_helper_scatter_facing);
	ClassDB::bind_method(D_METHOD("set_helper_scatter_facing", "value"), &BulletSpawner2D::set_helper_scatter_facing);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_scatter_facing", PROPERTY_HINT_ENUM, "Outward,Random,Inward"), "set_helper_scatter_facing", "get_helper_scatter_facing");

	ADD_SUBGROUP("Star Polygon", "helper_star_polygon_");
	ClassDB::bind_method(D_METHOD("get_helper_star_polygon_vertices"), &BulletSpawner2D::get_helper_star_polygon_vertices);
	ClassDB::bind_method(D_METHOD("set_helper_star_polygon_vertices", "value"), &BulletSpawner2D::set_helper_star_polygon_vertices);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_star_polygon_vertices"), "set_helper_star_polygon_vertices", "get_helper_star_polygon_vertices");

	ClassDB::bind_method(D_METHOD("get_helper_star_polygon_radius"), &BulletSpawner2D::get_helper_star_polygon_radius);
	ClassDB::bind_method(D_METHOD("set_helper_star_polygon_radius", "value"), &BulletSpawner2D::set_helper_star_polygon_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_star_polygon_radius"), "set_helper_star_polygon_radius", "get_helper_star_polygon_radius");

	ClassDB::bind_method(D_METHOD("get_helper_star_polygon_vertex_bias"), &BulletSpawner2D::get_helper_star_polygon_vertex_bias);
	ClassDB::bind_method(D_METHOD("set_helper_star_polygon_vertex_bias", "value"), &BulletSpawner2D::set_helper_star_polygon_vertex_bias);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_star_polygon_vertex_bias"), "set_helper_star_polygon_vertex_bias", "get_helper_star_polygon_vertex_bias");

	ClassDB::bind_method(D_METHOD("get_helper_star_polygon_base_rotation"), &BulletSpawner2D::get_helper_star_polygon_base_rotation);
	ClassDB::bind_method(D_METHOD("set_helper_star_polygon_base_rotation", "value"), &BulletSpawner2D::set_helper_star_polygon_base_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_star_polygon_base_rotation"), "set_helper_star_polygon_base_rotation", "get_helper_star_polygon_base_rotation");

	ClassDB::bind_method(D_METHOD("get_helper_star_polygon_face_outward"), &BulletSpawner2D::get_helper_star_polygon_face_outward);
	ClassDB::bind_method(D_METHOD("set_helper_star_polygon_face_outward", "value"), &BulletSpawner2D::set_helper_star_polygon_face_outward);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_star_polygon_face_outward"), "set_helper_star_polygon_face_outward", "get_helper_star_polygon_face_outward");

	ClassDB::bind_method(D_METHOD("get_helper_star_polygon_facing_offset_deg"), &BulletSpawner2D::get_helper_star_polygon_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_star_polygon_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_star_polygon_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_star_polygon_facing_offset_deg"), "set_helper_star_polygon_facing_offset_deg", "get_helper_star_polygon_facing_offset_deg");

	ADD_SUBGROUP("Multi Spiral", "helper_multispiral_");
	ClassDB::bind_method(D_METHOD("get_helper_multispiral_arms"), &BulletSpawner2D::get_helper_multispiral_arms);
	ClassDB::bind_method(D_METHOD("set_helper_multispiral_arms", "value"), &BulletSpawner2D::set_helper_multispiral_arms);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_multispiral_arms"), "set_helper_multispiral_arms", "get_helper_multispiral_arms");

	ClassDB::bind_method(D_METHOD("get_helper_multispiral_start_radius"), &BulletSpawner2D::get_helper_multispiral_start_radius);
	ClassDB::bind_method(D_METHOD("set_helper_multispiral_start_radius", "value"), &BulletSpawner2D::set_helper_multispiral_start_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_multispiral_start_radius"), "set_helper_multispiral_start_radius", "get_helper_multispiral_start_radius");

	ClassDB::bind_method(D_METHOD("get_helper_multispiral_radius_step"), &BulletSpawner2D::get_helper_multispiral_radius_step);
	ClassDB::bind_method(D_METHOD("set_helper_multispiral_radius_step", "value"), &BulletSpawner2D::set_helper_multispiral_radius_step);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_multispiral_radius_step"), "set_helper_multispiral_radius_step", "get_helper_multispiral_radius_step");

	ClassDB::bind_method(D_METHOD("get_helper_multispiral_angle_step"), &BulletSpawner2D::get_helper_multispiral_angle_step);
	ClassDB::bind_method(D_METHOD("set_helper_multispiral_angle_step", "value"), &BulletSpawner2D::set_helper_multispiral_angle_step);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_multispiral_angle_step"), "set_helper_multispiral_angle_step", "get_helper_multispiral_angle_step");

	ClassDB::bind_method(D_METHOD("get_helper_multispiral_rotate_with_marker"), &BulletSpawner2D::get_helper_multispiral_rotate_with_marker);
	ClassDB::bind_method(D_METHOD("set_helper_multispiral_rotate_with_marker", "value"), &BulletSpawner2D::set_helper_multispiral_rotate_with_marker);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_multispiral_rotate_with_marker"), "set_helper_multispiral_rotate_with_marker", "get_helper_multispiral_rotate_with_marker");

	ClassDB::bind_method(D_METHOD("get_helper_multispiral_facing"), &BulletSpawner2D::get_helper_multispiral_facing);
	ClassDB::bind_method(D_METHOD("set_helper_multispiral_facing", "value"), &BulletSpawner2D::set_helper_multispiral_facing);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_multispiral_facing", PROPERTY_HINT_ENUM, "Tangent,Radial Outward,Toward Center,Keep Marker"), "set_helper_multispiral_facing", "get_helper_multispiral_facing");

	ClassDB::bind_method(D_METHOD("get_helper_multispiral_facing_offset_deg"), &BulletSpawner2D::get_helper_multispiral_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_multispiral_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_multispiral_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_multispiral_facing_offset_deg"), "set_helper_multispiral_facing_offset_deg", "get_helper_multispiral_facing_offset_deg");

	ClassDB::bind_method(D_METHOD("get_helper_multispiral_arm_stride"), &BulletSpawner2D::get_helper_multispiral_arm_stride);
	ClassDB::bind_method(D_METHOD("set_helper_multispiral_arm_stride", "value"), &BulletSpawner2D::set_helper_multispiral_arm_stride);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_multispiral_arm_stride"), "set_helper_multispiral_arm_stride", "get_helper_multispiral_arm_stride");

	ADD_SUBGROUP("Cross", "helper_cross_");
	ClassDB::bind_method(D_METHOD("get_helper_cross_arm_count"), &BulletSpawner2D::get_helper_cross_arm_count);
	ClassDB::bind_method(D_METHOD("set_helper_cross_arm_count", "value"), &BulletSpawner2D::set_helper_cross_arm_count);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_cross_arm_count"), "set_helper_cross_arm_count", "get_helper_cross_arm_count");

	ClassDB::bind_method(D_METHOD("get_helper_cross_arm_length"), &BulletSpawner2D::get_helper_cross_arm_length);
	ClassDB::bind_method(D_METHOD("set_helper_cross_arm_length", "value"), &BulletSpawner2D::set_helper_cross_arm_length);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_cross_arm_length"), "set_helper_cross_arm_length", "get_helper_cross_arm_length");

	ClassDB::bind_method(D_METHOD("get_helper_cross_spacing"), &BulletSpawner2D::get_helper_cross_spacing);
	ClassDB::bind_method(D_METHOD("set_helper_cross_spacing", "value"), &BulletSpawner2D::set_helper_cross_spacing);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_cross_spacing"), "set_helper_cross_spacing", "get_helper_cross_spacing");

	ClassDB::bind_method(D_METHOD("get_helper_cross_base_rotation"), &BulletSpawner2D::get_helper_cross_base_rotation);
	ClassDB::bind_method(D_METHOD("set_helper_cross_base_rotation", "value"), &BulletSpawner2D::set_helper_cross_base_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_cross_base_rotation"), "set_helper_cross_base_rotation", "get_helper_cross_base_rotation");

	ClassDB::bind_method(D_METHOD("get_helper_cross_face_outward"), &BulletSpawner2D::get_helper_cross_face_outward);
	ClassDB::bind_method(D_METHOD("set_helper_cross_face_outward", "value"), &BulletSpawner2D::set_helper_cross_face_outward);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_cross_face_outward"), "set_helper_cross_face_outward", "get_helper_cross_face_outward");

	ClassDB::bind_method(D_METHOD("get_helper_cross_facing_offset_deg"), &BulletSpawner2D::get_helper_cross_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_cross_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_cross_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_cross_facing_offset_deg"), "set_helper_cross_facing_offset_deg", "get_helper_cross_facing_offset_deg");

	ADD_SUBGROUP("Star", "helper_star_");
	ClassDB::bind_method(D_METHOD("get_helper_star_points"), &BulletSpawner2D::get_helper_star_points);
	ClassDB::bind_method(D_METHOD("set_helper_star_points", "value"), &BulletSpawner2D::set_helper_star_points);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_star_points"), "set_helper_star_points", "get_helper_star_points");

	ClassDB::bind_method(D_METHOD("get_helper_star_outer_radius"), &BulletSpawner2D::get_helper_star_outer_radius);
	ClassDB::bind_method(D_METHOD("set_helper_star_outer_radius", "value"), &BulletSpawner2D::set_helper_star_outer_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_star_outer_radius"), "set_helper_star_outer_radius", "get_helper_star_outer_radius");

	ClassDB::bind_method(D_METHOD("get_helper_star_inner_radius"), &BulletSpawner2D::get_helper_star_inner_radius);
	ClassDB::bind_method(D_METHOD("set_helper_star_inner_radius", "value"), &BulletSpawner2D::set_helper_star_inner_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_star_inner_radius"), "set_helper_star_inner_radius", "get_helper_star_inner_radius");

	ClassDB::bind_method(D_METHOD("get_helper_star_base_rotation"), &BulletSpawner2D::get_helper_star_base_rotation);
	ClassDB::bind_method(D_METHOD("set_helper_star_base_rotation", "value"), &BulletSpawner2D::set_helper_star_base_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_star_base_rotation"), "set_helper_star_base_rotation", "get_helper_star_base_rotation");

	ClassDB::bind_method(D_METHOD("get_helper_star_face_outward"), &BulletSpawner2D::get_helper_star_face_outward);
	ClassDB::bind_method(D_METHOD("set_helper_star_face_outward", "value"), &BulletSpawner2D::set_helper_star_face_outward);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_star_face_outward"), "set_helper_star_face_outward", "get_helper_star_face_outward");

	ClassDB::bind_method(D_METHOD("get_helper_star_facing_offset_deg"), &BulletSpawner2D::get_helper_star_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_star_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_star_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_star_facing_offset_deg"), "set_helper_star_facing_offset_deg", "get_helper_star_facing_offset_deg");

	ADD_SUBGROUP("Heart", "helper_heart_");
	ClassDB::bind_method(D_METHOD("get_helper_heart_size"), &BulletSpawner2D::get_helper_heart_size);
	ClassDB::bind_method(D_METHOD("set_helper_heart_size", "value"), &BulletSpawner2D::set_helper_heart_size);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_heart_size"), "set_helper_heart_size", "get_helper_heart_size");

	ClassDB::bind_method(D_METHOD("get_helper_heart_base_rotation"), &BulletSpawner2D::get_helper_heart_base_rotation);
	ClassDB::bind_method(D_METHOD("set_helper_heart_base_rotation", "value"), &BulletSpawner2D::set_helper_heart_base_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_heart_base_rotation"), "set_helper_heart_base_rotation", "get_helper_heart_base_rotation");

	ClassDB::bind_method(D_METHOD("get_helper_heart_face_outward"), &BulletSpawner2D::get_helper_heart_face_outward);
	ClassDB::bind_method(D_METHOD("set_helper_heart_face_outward", "value"), &BulletSpawner2D::set_helper_heart_face_outward);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_heart_face_outward"), "set_helper_heart_face_outward", "get_helper_heart_face_outward");

	ClassDB::bind_method(D_METHOD("get_helper_heart_facing_offset_deg"), &BulletSpawner2D::get_helper_heart_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_heart_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_heart_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_heart_facing_offset_deg"), "set_helper_heart_facing_offset_deg", "get_helper_heart_facing_offset_deg");

	ADD_SUBGROUP("Wave", "helper_wave_");
	ClassDB::bind_method(D_METHOD("get_helper_wave_width"), &BulletSpawner2D::get_helper_wave_width);
	ClassDB::bind_method(D_METHOD("set_helper_wave_width", "value"), &BulletSpawner2D::set_helper_wave_width);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_wave_width"), "set_helper_wave_width", "get_helper_wave_width");

	ClassDB::bind_method(D_METHOD("get_helper_wave_amplitude"), &BulletSpawner2D::get_helper_wave_amplitude);
	ClassDB::bind_method(D_METHOD("set_helper_wave_amplitude", "value"), &BulletSpawner2D::set_helper_wave_amplitude);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_wave_amplitude"), "set_helper_wave_amplitude", "get_helper_wave_amplitude");

	ClassDB::bind_method(D_METHOD("get_helper_wave_waves"), &BulletSpawner2D::get_helper_wave_waves);
	ClassDB::bind_method(D_METHOD("set_helper_wave_waves", "value"), &BulletSpawner2D::set_helper_wave_waves);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_wave_waves"), "set_helper_wave_waves", "get_helper_wave_waves");

	ClassDB::bind_method(D_METHOD("get_helper_wave_direction"), &BulletSpawner2D::get_helper_wave_direction);
	ClassDB::bind_method(D_METHOD("set_helper_wave_direction", "value"), &BulletSpawner2D::set_helper_wave_direction);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "helper_wave_direction"), "set_helper_wave_direction", "get_helper_wave_direction");

	ClassDB::bind_method(D_METHOD("get_helper_wave_face_direction"), &BulletSpawner2D::get_helper_wave_face_direction);
	ClassDB::bind_method(D_METHOD("set_helper_wave_face_direction", "value"), &BulletSpawner2D::set_helper_wave_face_direction);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_wave_face_direction"), "set_helper_wave_face_direction", "get_helper_wave_face_direction");

	ClassDB::bind_method(D_METHOD("get_helper_wave_facing_offset_deg"), &BulletSpawner2D::get_helper_wave_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_wave_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_wave_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_wave_facing_offset_deg"), "set_helper_wave_facing_offset_deg", "get_helper_wave_facing_offset_deg");

	ADD_SUBGROUP("Waterfall", "helper_waterfall_");
	ClassDB::bind_method(D_METHOD("get_helper_waterfall_columns"), &BulletSpawner2D::get_helper_waterfall_columns);
	ClassDB::bind_method(D_METHOD("set_helper_waterfall_columns", "value"), &BulletSpawner2D::set_helper_waterfall_columns);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_waterfall_columns"), "set_helper_waterfall_columns", "get_helper_waterfall_columns");

	ClassDB::bind_method(D_METHOD("get_helper_waterfall_column_spacing"), &BulletSpawner2D::get_helper_waterfall_column_spacing);
	ClassDB::bind_method(D_METHOD("set_helper_waterfall_column_spacing", "value"), &BulletSpawner2D::set_helper_waterfall_column_spacing);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_waterfall_column_spacing"), "set_helper_waterfall_column_spacing", "get_helper_waterfall_column_spacing");

	ClassDB::bind_method(D_METHOD("get_helper_waterfall_rows"), &BulletSpawner2D::get_helper_waterfall_rows);
	ClassDB::bind_method(D_METHOD("set_helper_waterfall_rows", "value"), &BulletSpawner2D::set_helper_waterfall_rows);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_waterfall_rows"), "set_helper_waterfall_rows", "get_helper_waterfall_rows");

	ClassDB::bind_method(D_METHOD("get_helper_waterfall_row_spacing"), &BulletSpawner2D::get_helper_waterfall_row_spacing);
	ClassDB::bind_method(D_METHOD("set_helper_waterfall_row_spacing", "value"), &BulletSpawner2D::set_helper_waterfall_row_spacing);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_waterfall_row_spacing"), "set_helper_waterfall_row_spacing", "get_helper_waterfall_row_spacing");

	ClassDB::bind_method(D_METHOD("get_helper_waterfall_stagger"), &BulletSpawner2D::get_helper_waterfall_stagger);
	ClassDB::bind_method(D_METHOD("set_helper_waterfall_stagger", "value"), &BulletSpawner2D::set_helper_waterfall_stagger);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_waterfall_stagger"), "set_helper_waterfall_stagger", "get_helper_waterfall_stagger");

	ClassDB::bind_method(D_METHOD("get_helper_waterfall_rain_direction"), &BulletSpawner2D::get_helper_waterfall_rain_direction);
	ClassDB::bind_method(D_METHOD("set_helper_waterfall_rain_direction", "value"), &BulletSpawner2D::set_helper_waterfall_rain_direction);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "helper_waterfall_rain_direction"), "set_helper_waterfall_rain_direction", "get_helper_waterfall_rain_direction");

	ClassDB::bind_method(D_METHOD("get_helper_waterfall_jitter"), &BulletSpawner2D::get_helper_waterfall_jitter);
	ClassDB::bind_method(D_METHOD("set_helper_waterfall_jitter", "value"), &BulletSpawner2D::set_helper_waterfall_jitter);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_waterfall_jitter"), "set_helper_waterfall_jitter", "get_helper_waterfall_jitter");
	ClassDB::bind_method(D_METHOD("get_helper_waterfall_seed"), &BulletSpawner2D::get_helper_waterfall_seed);
	ClassDB::bind_method(D_METHOD("set_helper_waterfall_seed", "value"), &BulletSpawner2D::set_helper_waterfall_seed);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_waterfall_seed"), "set_helper_waterfall_seed", "get_helper_waterfall_seed");

	ClassDB::bind_method(D_METHOD("get_helper_waterfall_facing_offset_deg"), &BulletSpawner2D::get_helper_waterfall_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_waterfall_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_waterfall_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_waterfall_facing_offset_deg"), "set_helper_waterfall_facing_offset_deg", "get_helper_waterfall_facing_offset_deg");

	ADD_SUBGROUP("Lattice", "helper_lattice_");
	ClassDB::bind_method(D_METHOD("get_helper_lattice_columns"), &BulletSpawner2D::get_helper_lattice_columns);
	ClassDB::bind_method(D_METHOD("set_helper_lattice_columns", "value"), &BulletSpawner2D::set_helper_lattice_columns);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_lattice_columns"), "set_helper_lattice_columns", "get_helper_lattice_columns");

	ClassDB::bind_method(D_METHOD("get_helper_lattice_rows"), &BulletSpawner2D::get_helper_lattice_rows);
	ClassDB::bind_method(D_METHOD("set_helper_lattice_rows", "value"), &BulletSpawner2D::set_helper_lattice_rows);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_lattice_rows"), "set_helper_lattice_rows", "get_helper_lattice_rows");

	ClassDB::bind_method(D_METHOD("get_helper_lattice_spacing_x"), &BulletSpawner2D::get_helper_lattice_spacing_x);
	ClassDB::bind_method(D_METHOD("set_helper_lattice_spacing_x", "value"), &BulletSpawner2D::set_helper_lattice_spacing_x);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_lattice_spacing_x"), "set_helper_lattice_spacing_x", "get_helper_lattice_spacing_x");

	ClassDB::bind_method(D_METHOD("get_helper_lattice_spacing_y"), &BulletSpawner2D::get_helper_lattice_spacing_y);
	ClassDB::bind_method(D_METHOD("set_helper_lattice_spacing_y", "value"), &BulletSpawner2D::set_helper_lattice_spacing_y);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_lattice_spacing_y"), "set_helper_lattice_spacing_y", "get_helper_lattice_spacing_y");

	ClassDB::bind_method(D_METHOD("get_helper_lattice_stagger_rows"), &BulletSpawner2D::get_helper_lattice_stagger_rows);
	ClassDB::bind_method(D_METHOD("set_helper_lattice_stagger_rows", "value"), &BulletSpawner2D::set_helper_lattice_stagger_rows);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_lattice_stagger_rows"), "set_helper_lattice_stagger_rows", "get_helper_lattice_stagger_rows");

	ClassDB::bind_method(D_METHOD("get_helper_lattice_face_outward"), &BulletSpawner2D::get_helper_lattice_face_outward);
	ClassDB::bind_method(D_METHOD("set_helper_lattice_face_outward", "value"), &BulletSpawner2D::set_helper_lattice_face_outward);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_lattice_face_outward"), "set_helper_lattice_face_outward", "get_helper_lattice_face_outward");

	ClassDB::bind_method(D_METHOD("get_helper_lattice_facing_offset_deg"), &BulletSpawner2D::get_helper_lattice_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_lattice_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_lattice_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_lattice_facing_offset_deg"), "set_helper_lattice_facing_offset_deg", "get_helper_lattice_facing_offset_deg");

	ADD_SUBGROUP("Rose", "helper_rose_");
	ClassDB::bind_method(D_METHOD("get_helper_rose_petals"), &BulletSpawner2D::get_helper_rose_petals);
	ClassDB::bind_method(D_METHOD("set_helper_rose_petals", "value"), &BulletSpawner2D::set_helper_rose_petals);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_rose_petals"), "set_helper_rose_petals", "get_helper_rose_petals");

	ClassDB::bind_method(D_METHOD("get_helper_rose_radius"), &BulletSpawner2D::get_helper_rose_radius);
	ClassDB::bind_method(D_METHOD("set_helper_rose_radius", "value"), &BulletSpawner2D::set_helper_rose_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_rose_radius"), "set_helper_rose_radius", "get_helper_rose_radius");

	ClassDB::bind_method(D_METHOD("get_helper_rose_lobe_sharpness"), &BulletSpawner2D::get_helper_rose_lobe_sharpness);
	ClassDB::bind_method(D_METHOD("set_helper_rose_lobe_sharpness", "value"), &BulletSpawner2D::set_helper_rose_lobe_sharpness);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_rose_lobe_sharpness"), "set_helper_rose_lobe_sharpness", "get_helper_rose_lobe_sharpness");

	ClassDB::bind_method(D_METHOD("get_helper_rose_base_rotation"), &BulletSpawner2D::get_helper_rose_base_rotation);
	ClassDB::bind_method(D_METHOD("set_helper_rose_base_rotation", "value"), &BulletSpawner2D::set_helper_rose_base_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_rose_base_rotation"), "set_helper_rose_base_rotation", "get_helper_rose_base_rotation");

	ClassDB::bind_method(D_METHOD("get_helper_rose_face_outward"), &BulletSpawner2D::get_helper_rose_face_outward);
	ClassDB::bind_method(D_METHOD("set_helper_rose_face_outward", "value"), &BulletSpawner2D::set_helper_rose_face_outward);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_rose_face_outward"), "set_helper_rose_face_outward", "get_helper_rose_face_outward");

	ClassDB::bind_method(D_METHOD("get_helper_rose_facing_offset_deg"), &BulletSpawner2D::get_helper_rose_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_rose_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_rose_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_rose_facing_offset_deg"), "set_helper_rose_facing_offset_deg", "get_helper_rose_facing_offset_deg");

	ADD_SUBGROUP("Counter Spiral", "helper_counter_spiral_");
	ClassDB::bind_method(D_METHOD("get_helper_counter_spiral_arms"), &BulletSpawner2D::get_helper_counter_spiral_arms);
	ClassDB::bind_method(D_METHOD("set_helper_counter_spiral_arms", "value"), &BulletSpawner2D::set_helper_counter_spiral_arms);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_counter_spiral_arms"), "set_helper_counter_spiral_arms", "get_helper_counter_spiral_arms");

	ClassDB::bind_method(D_METHOD("get_helper_counter_spiral_start_radius"), &BulletSpawner2D::get_helper_counter_spiral_start_radius);
	ClassDB::bind_method(D_METHOD("set_helper_counter_spiral_start_radius", "value"), &BulletSpawner2D::set_helper_counter_spiral_start_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_counter_spiral_start_radius"), "set_helper_counter_spiral_start_radius", "get_helper_counter_spiral_start_radius");

	ClassDB::bind_method(D_METHOD("get_helper_counter_spiral_radius_step"), &BulletSpawner2D::get_helper_counter_spiral_radius_step);
	ClassDB::bind_method(D_METHOD("set_helper_counter_spiral_radius_step", "value"), &BulletSpawner2D::set_helper_counter_spiral_radius_step);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_counter_spiral_radius_step"), "set_helper_counter_spiral_radius_step", "get_helper_counter_spiral_radius_step");

	ClassDB::bind_method(D_METHOD("get_helper_counter_spiral_angle_step"), &BulletSpawner2D::get_helper_counter_spiral_angle_step);
	ClassDB::bind_method(D_METHOD("set_helper_counter_spiral_angle_step", "value"), &BulletSpawner2D::set_helper_counter_spiral_angle_step);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_counter_spiral_angle_step"), "set_helper_counter_spiral_angle_step", "get_helper_counter_spiral_angle_step");

	ClassDB::bind_method(D_METHOD("get_helper_counter_spiral_rotate_with_marker"), &BulletSpawner2D::get_helper_counter_spiral_rotate_with_marker);
	ClassDB::bind_method(D_METHOD("set_helper_counter_spiral_rotate_with_marker", "value"), &BulletSpawner2D::set_helper_counter_spiral_rotate_with_marker);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_counter_spiral_rotate_with_marker"), "set_helper_counter_spiral_rotate_with_marker", "get_helper_counter_spiral_rotate_with_marker");

	ClassDB::bind_method(D_METHOD("get_helper_counter_spiral_facing"), &BulletSpawner2D::get_helper_counter_spiral_facing);
	ClassDB::bind_method(D_METHOD("set_helper_counter_spiral_facing", "value"), &BulletSpawner2D::set_helper_counter_spiral_facing);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_counter_spiral_facing", PROPERTY_HINT_ENUM, "Tangent,Radial Outward,Toward Center,Keep Marker"), "set_helper_counter_spiral_facing", "get_helper_counter_spiral_facing");

	ClassDB::bind_method(D_METHOD("get_helper_counter_spiral_facing_offset_deg"), &BulletSpawner2D::get_helper_counter_spiral_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_counter_spiral_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_counter_spiral_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_counter_spiral_facing_offset_deg"), "set_helper_counter_spiral_facing_offset_deg", "get_helper_counter_spiral_facing_offset_deg");

	ClassDB::bind_method(D_METHOD("get_helper_counter_spiral_arm_stride"), &BulletSpawner2D::get_helper_counter_spiral_arm_stride);
	ClassDB::bind_method(D_METHOD("set_helper_counter_spiral_arm_stride", "value"), &BulletSpawner2D::set_helper_counter_spiral_arm_stride);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_counter_spiral_arm_stride"), "set_helper_counter_spiral_arm_stride", "get_helper_counter_spiral_arm_stride");

	ClassDB::bind_method(D_METHOD("get_helper_counter_spiral_mirror_alternate_arms"), &BulletSpawner2D::get_helper_counter_spiral_mirror_alternate_arms);
	ClassDB::bind_method(D_METHOD("set_helper_counter_spiral_mirror_alternate_arms", "value"), &BulletSpawner2D::set_helper_counter_spiral_mirror_alternate_arms);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_counter_spiral_mirror_alternate_arms"), "set_helper_counter_spiral_mirror_alternate_arms", "get_helper_counter_spiral_mirror_alternate_arms");

	ADD_SUBGROUP("Corridor", "helper_corridor_");
	ClassDB::bind_method(D_METHOD("get_helper_corridor_aim_direction"), &BulletSpawner2D::get_helper_corridor_aim_direction);
	ClassDB::bind_method(D_METHOD("set_helper_corridor_aim_direction", "value"), &BulletSpawner2D::set_helper_corridor_aim_direction);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "helper_corridor_aim_direction"), "set_helper_corridor_aim_direction", "get_helper_corridor_aim_direction");

	ClassDB::bind_method(D_METHOD("get_helper_corridor_width"), &BulletSpawner2D::get_helper_corridor_width);
	ClassDB::bind_method(D_METHOD("set_helper_corridor_width", "value"), &BulletSpawner2D::set_helper_corridor_width);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_corridor_width"), "set_helper_corridor_width", "get_helper_corridor_width");

	ClassDB::bind_method(D_METHOD("get_helper_corridor_gap_width"), &BulletSpawner2D::get_helper_corridor_gap_width);
	ClassDB::bind_method(D_METHOD("set_helper_corridor_gap_width", "value"), &BulletSpawner2D::set_helper_corridor_gap_width);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_corridor_gap_width"), "set_helper_corridor_gap_width", "get_helper_corridor_gap_width");

	ClassDB::bind_method(D_METHOD("get_helper_corridor_face_aim"), &BulletSpawner2D::get_helper_corridor_face_aim);
	ClassDB::bind_method(D_METHOD("set_helper_corridor_face_aim", "value"), &BulletSpawner2D::set_helper_corridor_face_aim);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_corridor_face_aim"), "set_helper_corridor_face_aim", "get_helper_corridor_face_aim");

	ClassDB::bind_method(D_METHOD("get_helper_corridor_facing_offset_deg"), &BulletSpawner2D::get_helper_corridor_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_corridor_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_corridor_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_corridor_facing_offset_deg"), "set_helper_corridor_facing_offset_deg", "get_helper_corridor_facing_offset_deg");

	ADD_SUBGROUP("Lissajous", "helper_lissajous_");
	ClassDB::bind_method(D_METHOD("get_helper_lissajous_size_x"), &BulletSpawner2D::get_helper_lissajous_size_x);
	ClassDB::bind_method(D_METHOD("set_helper_lissajous_size_x", "value"), &BulletSpawner2D::set_helper_lissajous_size_x);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_lissajous_size_x"), "set_helper_lissajous_size_x", "get_helper_lissajous_size_x");

	ClassDB::bind_method(D_METHOD("get_helper_lissajous_size_y"), &BulletSpawner2D::get_helper_lissajous_size_y);
	ClassDB::bind_method(D_METHOD("set_helper_lissajous_size_y", "value"), &BulletSpawner2D::set_helper_lissajous_size_y);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_lissajous_size_y"), "set_helper_lissajous_size_y", "get_helper_lissajous_size_y");

	ClassDB::bind_method(D_METHOD("get_helper_lissajous_freq_x"), &BulletSpawner2D::get_helper_lissajous_freq_x);
	ClassDB::bind_method(D_METHOD("set_helper_lissajous_freq_x", "value"), &BulletSpawner2D::set_helper_lissajous_freq_x);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_lissajous_freq_x"), "set_helper_lissajous_freq_x", "get_helper_lissajous_freq_x");

	ClassDB::bind_method(D_METHOD("get_helper_lissajous_freq_y"), &BulletSpawner2D::get_helper_lissajous_freq_y);
	ClassDB::bind_method(D_METHOD("set_helper_lissajous_freq_y", "value"), &BulletSpawner2D::set_helper_lissajous_freq_y);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_lissajous_freq_y"), "set_helper_lissajous_freq_y", "get_helper_lissajous_freq_y");

	ClassDB::bind_method(D_METHOD("get_helper_lissajous_phase"), &BulletSpawner2D::get_helper_lissajous_phase);
	ClassDB::bind_method(D_METHOD("set_helper_lissajous_phase", "value"), &BulletSpawner2D::set_helper_lissajous_phase);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_lissajous_phase"), "set_helper_lissajous_phase", "get_helper_lissajous_phase");

	ClassDB::bind_method(D_METHOD("get_helper_lissajous_face_outward"), &BulletSpawner2D::get_helper_lissajous_face_outward);
	ClassDB::bind_method(D_METHOD("set_helper_lissajous_face_outward", "value"), &BulletSpawner2D::set_helper_lissajous_face_outward);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_lissajous_face_outward"), "set_helper_lissajous_face_outward", "get_helper_lissajous_face_outward");

	ClassDB::bind_method(D_METHOD("get_helper_lissajous_facing_offset_deg"), &BulletSpawner2D::get_helper_lissajous_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_lissajous_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_lissajous_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_lissajous_facing_offset_deg"), "set_helper_lissajous_facing_offset_deg", "get_helper_lissajous_facing_offset_deg");

	ADD_SUBGROUP("Custom", "helper_custom_");
	ClassDB::bind_method(D_METHOD("get_helper_custom_transforms"), &BulletSpawner2D::get_helper_custom_transforms);
	ClassDB::bind_method(D_METHOD("set_helper_custom_transforms", "value"), &BulletSpawner2D::set_helper_custom_transforms);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "helper_custom_transforms", PROPERTY_HINT_ARRAY_TYPE, "Transform2D"), "set_helper_custom_transforms", "get_helper_custom_transforms");

	ClassDB::bind_method(D_METHOD("get_helper_custom_facing"), &BulletSpawner2D::get_helper_custom_facing);
	ClassDB::bind_method(D_METHOD("set_helper_custom_facing", "value"), &BulletSpawner2D::set_helper_custom_facing);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_custom_facing", PROPERTY_HINT_ENUM, "As Stored,Face Outward,Face Inward,+90 Degrees,-90 Degrees"), "set_helper_custom_facing", "get_helper_custom_facing");

	ClassDB::bind_method(D_METHOD("get_helper_custom_facing_offset_deg"), &BulletSpawner2D::get_helper_custom_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_custom_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_custom_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_custom_facing_offset_deg"), "set_helper_custom_facing_offset_deg", "get_helper_custom_facing_offset_deg");

	ClassDB::bind_method(D_METHOD("get_helper_custom_reverse"), &BulletSpawner2D::get_helper_custom_reverse);
	ClassDB::bind_method(D_METHOD("set_helper_custom_reverse", "value"), &BulletSpawner2D::set_helper_custom_reverse);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_custom_reverse"), "set_helper_custom_reverse", "get_helper_custom_reverse");

	ClassDB::bind_method(D_METHOD("get_helper_custom_slot_offset"), &BulletSpawner2D::get_helper_custom_slot_offset);
	ClassDB::bind_method(D_METHOD("set_helper_custom_slot_offset", "value"), &BulletSpawner2D::set_helper_custom_slot_offset);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_custom_slot_offset"), "set_helper_custom_slot_offset", "get_helper_custom_slot_offset");

	ADD_SUBGROUP("Triangle", "helper_triangle_");
	ClassDB::bind_method(D_METHOD("get_helper_triangle_type"), &BulletSpawner2D::get_helper_triangle_type);
	ClassDB::bind_method(D_METHOD("set_helper_triangle_type", "value"), &BulletSpawner2D::set_helper_triangle_type);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_triangle_type", PROPERTY_HINT_ENUM, "Equilateral,Isosceles,Right"), "set_helper_triangle_type", "get_helper_triangle_type");

	ClassDB::bind_method(D_METHOD("get_helper_triangle_size_a"), &BulletSpawner2D::get_helper_triangle_size_a);
	ClassDB::bind_method(D_METHOD("set_helper_triangle_size_a", "value"), &BulletSpawner2D::set_helper_triangle_size_a);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_triangle_size_a"), "set_helper_triangle_size_a", "get_helper_triangle_size_a");

	ClassDB::bind_method(D_METHOD("get_helper_triangle_size_b"), &BulletSpawner2D::get_helper_triangle_size_b);
	ClassDB::bind_method(D_METHOD("set_helper_triangle_size_b", "value"), &BulletSpawner2D::set_helper_triangle_size_b);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_triangle_size_b"), "set_helper_triangle_size_b", "get_helper_triangle_size_b");

	ClassDB::bind_method(D_METHOD("get_helper_triangle_rotation"), &BulletSpawner2D::get_helper_triangle_rotation);
	ClassDB::bind_method(D_METHOD("set_helper_triangle_rotation", "value"), &BulletSpawner2D::set_helper_triangle_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_triangle_rotation"), "set_helper_triangle_rotation", "get_helper_triangle_rotation");

	ClassDB::bind_method(D_METHOD("get_helper_triangle_face_outward"), &BulletSpawner2D::get_helper_triangle_face_outward);
	ClassDB::bind_method(D_METHOD("set_helper_triangle_face_outward", "value"), &BulletSpawner2D::set_helper_triangle_face_outward);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_triangle_face_outward"), "set_helper_triangle_face_outward", "get_helper_triangle_face_outward");

	ClassDB::bind_method(D_METHOD("get_helper_triangle_facing_offset_deg"), &BulletSpawner2D::get_helper_triangle_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_triangle_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_triangle_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_triangle_facing_offset_deg"), "set_helper_triangle_facing_offset_deg", "get_helper_triangle_facing_offset_deg");

	ADD_SUBGROUP("Trapezoid", "helper_trapezoid_");
	ClassDB::bind_method(D_METHOD("get_helper_trapezoid_base_top"), &BulletSpawner2D::get_helper_trapezoid_base_top);
	ClassDB::bind_method(D_METHOD("set_helper_trapezoid_base_top", "value"), &BulletSpawner2D::set_helper_trapezoid_base_top);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_trapezoid_base_top"), "set_helper_trapezoid_base_top", "get_helper_trapezoid_base_top");

	ClassDB::bind_method(D_METHOD("get_helper_trapezoid_base_bottom"), &BulletSpawner2D::get_helper_trapezoid_base_bottom);
	ClassDB::bind_method(D_METHOD("set_helper_trapezoid_base_bottom", "value"), &BulletSpawner2D::set_helper_trapezoid_base_bottom);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_trapezoid_base_bottom"), "set_helper_trapezoid_base_bottom", "get_helper_trapezoid_base_bottom");

	ClassDB::bind_method(D_METHOD("get_helper_trapezoid_height"), &BulletSpawner2D::get_helper_trapezoid_height);
	ClassDB::bind_method(D_METHOD("set_helper_trapezoid_height", "value"), &BulletSpawner2D::set_helper_trapezoid_height);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_trapezoid_height"), "set_helper_trapezoid_height", "get_helper_trapezoid_height");

	ClassDB::bind_method(D_METHOD("get_helper_trapezoid_rotation"), &BulletSpawner2D::get_helper_trapezoid_rotation);
	ClassDB::bind_method(D_METHOD("set_helper_trapezoid_rotation", "value"), &BulletSpawner2D::set_helper_trapezoid_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_trapezoid_rotation"), "set_helper_trapezoid_rotation", "get_helper_trapezoid_rotation");

	ClassDB::bind_method(D_METHOD("get_helper_trapezoid_face_outward"), &BulletSpawner2D::get_helper_trapezoid_face_outward);
	ClassDB::bind_method(D_METHOD("set_helper_trapezoid_face_outward", "value"), &BulletSpawner2D::set_helper_trapezoid_face_outward);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_trapezoid_face_outward"), "set_helper_trapezoid_face_outward", "get_helper_trapezoid_face_outward");

	ClassDB::bind_method(D_METHOD("get_helper_trapezoid_facing_offset_deg"), &BulletSpawner2D::get_helper_trapezoid_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_trapezoid_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_trapezoid_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_trapezoid_facing_offset_deg"), "set_helper_trapezoid_facing_offset_deg", "get_helper_trapezoid_facing_offset_deg");

	ADD_SUBGROUP("Diamond", "helper_diamond_");
	ClassDB::bind_method(D_METHOD("get_helper_diamond_diagonal_x"), &BulletSpawner2D::get_helper_diamond_diagonal_x);
	ClassDB::bind_method(D_METHOD("set_helper_diamond_diagonal_x", "value"), &BulletSpawner2D::set_helper_diamond_diagonal_x);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_diamond_diagonal_x"), "set_helper_diamond_diagonal_x", "get_helper_diamond_diagonal_x");

	ClassDB::bind_method(D_METHOD("get_helper_diamond_diagonal_y"), &BulletSpawner2D::get_helper_diamond_diagonal_y);
	ClassDB::bind_method(D_METHOD("set_helper_diamond_diagonal_y", "value"), &BulletSpawner2D::set_helper_diamond_diagonal_y);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_diamond_diagonal_y"), "set_helper_diamond_diagonal_y", "get_helper_diamond_diagonal_y");

	ClassDB::bind_method(D_METHOD("get_helper_diamond_rotation"), &BulletSpawner2D::get_helper_diamond_rotation);
	ClassDB::bind_method(D_METHOD("set_helper_diamond_rotation", "value"), &BulletSpawner2D::set_helper_diamond_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_diamond_rotation"), "set_helper_diamond_rotation", "get_helper_diamond_rotation");

	ClassDB::bind_method(D_METHOD("get_helper_diamond_face_outward"), &BulletSpawner2D::get_helper_diamond_face_outward);
	ClassDB::bind_method(D_METHOD("set_helper_diamond_face_outward", "value"), &BulletSpawner2D::set_helper_diamond_face_outward);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_diamond_face_outward"), "set_helper_diamond_face_outward", "get_helper_diamond_face_outward");

	ClassDB::bind_method(D_METHOD("get_helper_diamond_facing_offset_deg"), &BulletSpawner2D::get_helper_diamond_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_diamond_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_diamond_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_diamond_facing_offset_deg"), "set_helper_diamond_facing_offset_deg", "get_helper_diamond_facing_offset_deg");

	ADD_SUBGROUP("Path2D", "helper_path2d_");
	ClassDB::bind_method(D_METHOD("get_helper_path2d_path"), &BulletSpawner2D::get_helper_path2d_path);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_path", "path"), &BulletSpawner2D::set_helper_path2d_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "helper_path2d_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Path2D"), "set_helper_path2d_path", "get_helper_path2d_path");

	ClassDB::bind_method(D_METHOD("get_helper_path2d_node"), &BulletSpawner2D::get_helper_path2d_node);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_node", "node"), &BulletSpawner2D::set_helper_path2d_node);

	ClassDB::bind_method(D_METHOD("get_helper_path2d_space"), &BulletSpawner2D::get_helper_path2d_space);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_space", "value"), &BulletSpawner2D::set_helper_path2d_space);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_path2d_space", PROPERTY_HINT_ENUM, "Follow Generator,At Path2D Node"), "set_helper_path2d_space", "get_helper_path2d_space");

	ClassDB::bind_method(D_METHOD("get_helper_path2d_distribution"), &BulletSpawner2D::get_helper_path2d_distribution);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_distribution", "value"), &BulletSpawner2D::set_helper_path2d_distribution);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_path2d_distribution", PROPERTY_HINT_ENUM, "Fixed Spacing,Spread Evenly"), "set_helper_path2d_distribution", "get_helper_path2d_distribution");

	ClassDB::bind_method(D_METHOD("get_helper_path2d_spacing"), &BulletSpawner2D::get_helper_path2d_spacing);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_spacing", "value"), &BulletSpawner2D::set_helper_path2d_spacing);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_path2d_spacing"), "set_helper_path2d_spacing", "get_helper_path2d_spacing");

	ClassDB::bind_method(D_METHOD("get_helper_path2d_overflow"), &BulletSpawner2D::get_helper_path2d_overflow);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_overflow", "value"), &BulletSpawner2D::set_helper_path2d_overflow);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_path2d_overflow", PROPERTY_HINT_ENUM, "Clamp,Wrap,Auto Fit"), "set_helper_path2d_overflow", "get_helper_path2d_overflow");

	ClassDB::bind_method(D_METHOD("get_helper_path2d_anchor"), &BulletSpawner2D::get_helper_path2d_anchor);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_anchor", "value"), &BulletSpawner2D::set_helper_path2d_anchor);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_path2d_anchor", PROPERTY_HINT_ENUM, "Path Start,Centered,Path End"), "set_helper_path2d_anchor", "get_helper_path2d_anchor");

	ClassDB::bind_method(D_METHOD("get_helper_path2d_start_offset"), &BulletSpawner2D::get_helper_path2d_start_offset);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_start_offset", "value"), &BulletSpawner2D::set_helper_path2d_start_offset);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_path2d_start_offset"), "set_helper_path2d_start_offset", "get_helper_path2d_start_offset");

	ClassDB::bind_method(D_METHOD("get_helper_path2d_reverse"), &BulletSpawner2D::get_helper_path2d_reverse);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_reverse", "value"), &BulletSpawner2D::set_helper_path2d_reverse);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_path2d_reverse"), "set_helper_path2d_reverse", "get_helper_path2d_reverse");

	ClassDB::bind_method(D_METHOD("get_helper_path2d_closed"), &BulletSpawner2D::get_helper_path2d_closed);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_closed", "value"), &BulletSpawner2D::set_helper_path2d_closed);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_path2d_closed"), "set_helper_path2d_closed", "get_helper_path2d_closed");

	ClassDB::bind_method(D_METHOD("get_helper_path2d_facing"), &BulletSpawner2D::get_helper_path2d_facing);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_facing", "value"), &BulletSpawner2D::set_helper_path2d_facing);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_path2d_facing", PROPERTY_HINT_ENUM, "Along Path,Normal +90,Normal -90"), "set_helper_path2d_facing", "get_helper_path2d_facing");

	ClassDB::bind_method(D_METHOD("get_helper_path2d_facing_offset_deg"), &BulletSpawner2D::get_helper_path2d_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_path2d_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_path2d_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_path2d_facing_offset_deg"), "set_helper_path2d_facing_offset_deg", "get_helper_path2d_facing_offset_deg");

	ADD_SUBGROUP("Circle", "helper_circle_");
	ClassDB::bind_method(D_METHOD("get_helper_circle_radius"), &BulletSpawner2D::get_helper_circle_radius);
	ClassDB::bind_method(D_METHOD("set_helper_circle_radius", "value"), &BulletSpawner2D::set_helper_circle_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_circle_radius"), "set_helper_circle_radius", "get_helper_circle_radius");

	ClassDB::bind_method(D_METHOD("get_helper_circle_face_outward"), &BulletSpawner2D::get_helper_circle_face_outward);
	ClassDB::bind_method(D_METHOD("set_helper_circle_face_outward", "value"), &BulletSpawner2D::set_helper_circle_face_outward);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_circle_face_outward"), "set_helper_circle_face_outward", "get_helper_circle_face_outward");

	ClassDB::bind_method(D_METHOD("get_helper_circle_facing_offset_deg"), &BulletSpawner2D::get_helper_circle_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_circle_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_circle_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_circle_facing_offset_deg"), "set_helper_circle_facing_offset_deg", "get_helper_circle_facing_offset_deg");

	ADD_SUBGROUP("Rectangle", "helper_rectangle_");
	ClassDB::bind_method(D_METHOD("get_helper_rectangle_size"), &BulletSpawner2D::get_helper_rectangle_size);
	ClassDB::bind_method(D_METHOD("set_helper_rectangle_size", "value"), &BulletSpawner2D::set_helper_rectangle_size);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "helper_rectangle_size"), "set_helper_rectangle_size", "get_helper_rectangle_size");

	ClassDB::bind_method(D_METHOD("get_helper_rectangle_face_outward"), &BulletSpawner2D::get_helper_rectangle_face_outward);
	ClassDB::bind_method(D_METHOD("set_helper_rectangle_face_outward", "value"), &BulletSpawner2D::set_helper_rectangle_face_outward);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_rectangle_face_outward"), "set_helper_rectangle_face_outward", "get_helper_rectangle_face_outward");

	ClassDB::bind_method(D_METHOD("get_helper_rectangle_facing_offset_deg"), &BulletSpawner2D::get_helper_rectangle_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_rectangle_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_rectangle_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_rectangle_facing_offset_deg"), "set_helper_rectangle_facing_offset_deg", "get_helper_rectangle_facing_offset_deg");

	ADD_SUBGROUP("Square", "helper_square_");
	ClassDB::bind_method(D_METHOD("get_helper_square_size"), &BulletSpawner2D::get_helper_square_size);
	ClassDB::bind_method(D_METHOD("set_helper_square_size", "value"), &BulletSpawner2D::set_helper_square_size);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_square_size"), "set_helper_square_size", "get_helper_square_size");

	ClassDB::bind_method(D_METHOD("get_helper_square_face_outward"), &BulletSpawner2D::get_helper_square_face_outward);
	ClassDB::bind_method(D_METHOD("set_helper_square_face_outward", "value"), &BulletSpawner2D::set_helper_square_face_outward);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_square_face_outward"), "set_helper_square_face_outward", "get_helper_square_face_outward");

	ClassDB::bind_method(D_METHOD("get_helper_square_facing_offset_deg"), &BulletSpawner2D::get_helper_square_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_square_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_square_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_square_facing_offset_deg"), "set_helper_square_facing_offset_deg", "get_helper_square_facing_offset_deg");

	ADD_SUBGROUP("Polygon", "helper_polygon_");
	ClassDB::bind_method(D_METHOD("get_helper_polygon_vertices"), &BulletSpawner2D::get_helper_polygon_vertices);
	ClassDB::bind_method(D_METHOD("set_helper_polygon_vertices", "value"), &BulletSpawner2D::set_helper_polygon_vertices);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_polygon_vertices", PROPERTY_HINT_RANGE, "3,64,1"), "set_helper_polygon_vertices", "get_helper_polygon_vertices");

	ClassDB::bind_method(D_METHOD("get_helper_polygon_radius"), &BulletSpawner2D::get_helper_polygon_radius);
	ClassDB::bind_method(D_METHOD("set_helper_polygon_radius", "value"), &BulletSpawner2D::set_helper_polygon_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_polygon_radius"), "set_helper_polygon_radius", "get_helper_polygon_radius");

	ClassDB::bind_method(D_METHOD("get_helper_polygon_rotation"), &BulletSpawner2D::get_helper_polygon_rotation);
	ClassDB::bind_method(D_METHOD("set_helper_polygon_rotation", "value"), &BulletSpawner2D::set_helper_polygon_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_polygon_rotation"), "set_helper_polygon_rotation", "get_helper_polygon_rotation");

	ClassDB::bind_method(D_METHOD("get_helper_polygon_face_outward"), &BulletSpawner2D::get_helper_polygon_face_outward);
	ClassDB::bind_method(D_METHOD("set_helper_polygon_face_outward", "value"), &BulletSpawner2D::set_helper_polygon_face_outward);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_polygon_face_outward"), "set_helper_polygon_face_outward", "get_helper_polygon_face_outward");

	ClassDB::bind_method(D_METHOD("get_helper_polygon_facing_offset_deg"), &BulletSpawner2D::get_helper_polygon_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_polygon_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_polygon_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_polygon_facing_offset_deg"), "set_helper_polygon_facing_offset_deg", "get_helper_polygon_facing_offset_deg");

	ADD_SUBGROUP("Outline Layers", "helper_outline_");
	ClassDB::bind_method(D_METHOD("get_helper_outline_placement"), &BulletSpawner2D::get_helper_outline_placement);
	ClassDB::bind_method(D_METHOD("set_helper_outline_placement", "value"), &BulletSpawner2D::set_helper_outline_placement);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_outline_placement", PROPERTY_HINT_ENUM, "On Outline,Layers,Fill Inside"), "set_helper_outline_placement", "get_helper_outline_placement");

	ClassDB::bind_method(D_METHOD("get_helper_outline_layer_count"), &BulletSpawner2D::get_helper_outline_layer_count);
	ClassDB::bind_method(D_METHOD("set_helper_outline_layer_count", "value"), &BulletSpawner2D::set_helper_outline_layer_count);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_outline_layer_count"), "set_helper_outline_layer_count", "get_helper_outline_layer_count");

	ClassDB::bind_method(D_METHOD("get_helper_outline_layer_scale"), &BulletSpawner2D::get_helper_outline_layer_scale);
	ClassDB::bind_method(D_METHOD("set_helper_outline_layer_scale", "value"), &BulletSpawner2D::set_helper_outline_layer_scale);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_outline_layer_scale"), "set_helper_outline_layer_scale", "get_helper_outline_layer_scale");

	ClassDB::bind_method(D_METHOD("get_helper_outline_layer_side"), &BulletSpawner2D::get_helper_outline_layer_side);
	ClassDB::bind_method(D_METHOD("set_helper_outline_layer_side", "value"), &BulletSpawner2D::set_helper_outline_layer_side);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_outline_layer_side", PROPERTY_HINT_ENUM, "Outward,Inward,Both"), "set_helper_outline_layer_side", "get_helper_outline_layer_side");

	ClassDB::bind_method(D_METHOD("get_helper_outline_layer_fill"), &BulletSpawner2D::get_helper_outline_layer_fill);
	ClassDB::bind_method(D_METHOD("set_helper_outline_layer_fill", "value"), &BulletSpawner2D::set_helper_outline_layer_fill);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_outline_layer_fill", PROPERTY_HINT_ENUM, "Interleaved,Sequential,Outer First,Ping Pong"), "set_helper_outline_layer_fill", "get_helper_outline_layer_fill");
	ClassDB::bind_method(D_METHOD("get_helper_outline_layer_scale_curve"), &BulletSpawner2D::get_helper_outline_layer_scale_curve);
	ClassDB::bind_method(D_METHOD("set_helper_outline_layer_scale_curve", "value"), &BulletSpawner2D::set_helper_outline_layer_scale_curve);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_outline_layer_scale_curve", PROPERTY_HINT_ENUM, "Linear,Exponential"), "set_helper_outline_layer_scale_curve", "get_helper_outline_layer_scale_curve");
	ClassDB::bind_method(D_METHOD("get_helper_outline_layer_scales"), &BulletSpawner2D::get_helper_outline_layer_scales);
	ClassDB::bind_method(D_METHOD("set_helper_outline_layer_scales", "value"), &BulletSpawner2D::set_helper_outline_layer_scales);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "helper_outline_layer_scales"), "set_helper_outline_layer_scales", "get_helper_outline_layer_scales");
	ClassDB::bind_method(D_METHOD("get_helper_outline_layer_twist"), &BulletSpawner2D::get_helper_outline_layer_twist);
	ClassDB::bind_method(D_METHOD("set_helper_outline_layer_twist", "value"), &BulletSpawner2D::set_helper_outline_layer_twist);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_outline_layer_twist"), "set_helper_outline_layer_twist", "get_helper_outline_layer_twist");
	ClassDB::bind_method(D_METHOD("get_helper_outline_layer_max_dots"), &BulletSpawner2D::get_helper_outline_layer_max_dots);
	ClassDB::bind_method(D_METHOD("set_helper_outline_layer_max_dots", "value"), &BulletSpawner2D::set_helper_outline_layer_max_dots);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_outline_layer_max_dots"), "set_helper_outline_layer_max_dots", "get_helper_outline_layer_max_dots");
	ClassDB::bind_method(D_METHOD("get_helper_outline_layer_start_offset"), &BulletSpawner2D::get_helper_outline_layer_start_offset);
	ClassDB::bind_method(D_METHOD("set_helper_outline_layer_start_offset", "value"), &BulletSpawner2D::set_helper_outline_layer_start_offset);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_outline_layer_start_offset"), "set_helper_outline_layer_start_offset", "get_helper_outline_layer_start_offset");

	ClassDB::bind_method(D_METHOD("get_helper_outline_distribution"), &BulletSpawner2D::get_helper_outline_distribution);
	ClassDB::bind_method(D_METHOD("set_helper_outline_distribution", "value"), &BulletSpawner2D::set_helper_outline_distribution);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_outline_distribution", PROPERTY_HINT_ENUM, "Legacy,Symmetric"), "set_helper_outline_distribution", "get_helper_outline_distribution");

	ClassDB::bind_method(D_METHOD("get_helper_outline_layer_layout"), &BulletSpawner2D::get_helper_outline_layer_layout);
	ClassDB::bind_method(D_METHOD("set_helper_outline_layer_layout", "value"), &BulletSpawner2D::set_helper_outline_layer_layout);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_outline_layer_layout", PROPERTY_HINT_ENUM, "Shared Loop,Even Per Layer"), "set_helper_outline_layer_layout", "get_helper_outline_layer_layout");

	ClassDB::bind_method(D_METHOD("get_helper_outline_corner_priority"), &BulletSpawner2D::get_helper_outline_corner_priority);
	ClassDB::bind_method(D_METHOD("set_helper_outline_corner_priority", "value"), &BulletSpawner2D::set_helper_outline_corner_priority);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_outline_corner_priority", PROPERTY_HINT_ENUM, "Horizontal Sides,Vertical Sides,Balanced"), "set_helper_outline_corner_priority", "get_helper_outline_corner_priority");

	ClassDB::bind_method(D_METHOD("get_helper_outline_corner_mode"), &BulletSpawner2D::get_helper_outline_corner_mode);
	ClassDB::bind_method(D_METHOD("set_helper_outline_corner_mode", "value"), &BulletSpawner2D::set_helper_outline_corner_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_outline_corner_mode", PROPERTY_HINT_ENUM, "Pin Corners,Even Arc"), "set_helper_outline_corner_mode", "get_helper_outline_corner_mode");

	ClassDB::bind_method(D_METHOD("get_helper_outline_edge_margin"), &BulletSpawner2D::get_helper_outline_edge_margin);
	ClassDB::bind_method(D_METHOD("set_helper_outline_edge_margin", "value"), &BulletSpawner2D::set_helper_outline_edge_margin);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_outline_edge_margin"), "set_helper_outline_edge_margin", "get_helper_outline_edge_margin");

	ClassDB::bind_method(D_METHOD("get_helper_outline_corner_facing"), &BulletSpawner2D::get_helper_outline_corner_facing);
	ClassDB::bind_method(D_METHOD("set_helper_outline_corner_facing", "value"), &BulletSpawner2D::set_helper_outline_corner_facing);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_outline_corner_facing", PROPERTY_HINT_ENUM, "Side,Miter,Smooth"), "set_helper_outline_corner_facing", "get_helper_outline_corner_facing");

	ClassDB::bind_method(D_METHOD("get_helper_outline_facing"), &BulletSpawner2D::get_helper_outline_facing);
	ClassDB::bind_method(D_METHOD("set_helper_outline_facing", "value"), &BulletSpawner2D::set_helper_outline_facing);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_outline_facing", PROPERTY_HINT_ENUM, "Outline Normal,+90 Degrees,-90 Degrees"), "set_helper_outline_facing", "get_helper_outline_facing");

	ClassDB::bind_method(D_METHOD("get_helper_outline_reverse"), &BulletSpawner2D::get_helper_outline_reverse);
	ClassDB::bind_method(D_METHOD("set_helper_outline_reverse", "value"), &BulletSpawner2D::set_helper_outline_reverse);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_outline_reverse"), "set_helper_outline_reverse", "get_helper_outline_reverse");

	ClassDB::bind_method(D_METHOD("get_helper_outline_slot_offset"), &BulletSpawner2D::get_helper_outline_slot_offset);
	ClassDB::bind_method(D_METHOD("set_helper_outline_slot_offset", "value"), &BulletSpawner2D::set_helper_outline_slot_offset);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_outline_slot_offset"), "set_helper_outline_slot_offset", "get_helper_outline_slot_offset");

	ClassDB::bind_method(D_METHOD("get_helper_outline_fill_spacing"), &BulletSpawner2D::get_helper_outline_fill_spacing);
	ClassDB::bind_method(D_METHOD("set_helper_outline_fill_spacing", "value"), &BulletSpawner2D::set_helper_outline_fill_spacing);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_outline_fill_spacing"), "set_helper_outline_fill_spacing", "get_helper_outline_fill_spacing");

	ClassDB::bind_method(D_METHOD("get_helper_outline_fill_stagger"), &BulletSpawner2D::get_helper_outline_fill_stagger);
	ClassDB::bind_method(D_METHOD("set_helper_outline_fill_stagger", "value"), &BulletSpawner2D::set_helper_outline_fill_stagger);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_outline_fill_stagger"), "set_helper_outline_fill_stagger", "get_helper_outline_fill_stagger");

	ClassDB::bind_method(D_METHOD("get_helper_outline_fill_margin"), &BulletSpawner2D::get_helper_outline_fill_margin);
	ClassDB::bind_method(D_METHOD("set_helper_outline_fill_margin", "value"), &BulletSpawner2D::set_helper_outline_fill_margin);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_outline_fill_margin"), "set_helper_outline_fill_margin", "get_helper_outline_fill_margin");

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
#define BS_BIND_PROP(m_name) \
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
