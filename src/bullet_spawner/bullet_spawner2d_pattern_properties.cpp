// BulletSpawner2D (split from bullet_spawner2d.cpp, same class):
// Bullet Patterns inspector surface: pattern_source, amount, transform
// knobs and every helper_<shape>_* accessor (validate -> store ->
// on_pattern_changed), plus apply_pattern_preset (batched raw writes).

#include "bullet_spawner/bullet_spawner2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

double BulletSpawner2D::get_pattern_scale() const {
    return pattern_scale;
}

void BulletSpawner2D::set_pattern_scale(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: pattern_scale must be finite, keeping the old value.");
        return;
    }
    pattern_scale = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_transforms_scale() const {
    return transforms_scale;
}

void BulletSpawner2D::set_transforms_scale(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: transforms_scale must be finite, keeping the old value.");
        return;
    }
    transforms_scale = value;
    on_pattern_changed();
}

Vector2 BulletSpawner2D::get_spawn_position_offset() const {
    return spawn_position_offset;
}

void BulletSpawner2D::set_spawn_position_offset(const Vector2 &value) {
    if (!value.is_finite()) {
        UtilityFunctions::push_error("BulletSpawner2D: spawn_position_offset must be finite (NaN/Inf would poison bullet movement), keeping the old value.");
        return;
    }
    spawn_position_offset = value;
    // The preview draws the muzzle offset through the layer pose.
    set_preview_pose(spin_angle_deg);
}

int BulletSpawner2D::get_spawn_position_offset_space() const { return spawn_position_offset_space; }

void BulletSpawner2D::set_spawn_position_offset_space(int value) {
    if (value < SPAWN_OFFSET_GLOBAL || value > SPAWN_OFFSET_LOCAL) {
        UtilityFunctions::push_error("BulletSpawner2D: spawn_position_offset_space must be 0 (Global) or 1 (Local), keeping the old value.");
        return;
    }
    spawn_position_offset_space = value;
    set_preview_pose(spin_angle_deg);
}

BulletSpawner2D::PatternSource BulletSpawner2D::get_pattern_source() const {
    return pattern_source;
}

void BulletSpawner2D::set_pattern_source(PatternSource value) {
    on_config_changed();
    if (value < PATTERN_FROM_CHILDREN || value >= PATTERN_FROM_LAST) {
        UtilityFunctions::push_error("BulletSpawner2D: invalid pattern_source, keeping the old value.");
        return;
    }
    pattern_source = value;
    // The visible helper_* option groups depend on this mode: refresh the
    // inspector immediately, otherwise the new mode's options stay hidden
    // until the selection is re-clicked (see _validate_property).
    notify_property_list_changed();
    update_preview_process_state();
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_bullets_amount() const {
    return helper_bullets_amount;
}

void BulletSpawner2D::set_helper_bullets_amount(int value) {
    if (value < 1) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_bullets_amount must be >= 1, keeping the old value.");
        return;
    }
    // One shoot_once() fans this out to transforms + SoA vectors + physics
    // RIDs: an unchecked typo would freeze/OOM in a single call.
    if (value > kMaxBulletsPerVolley) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_bullets_amount must be <= 10000, keeping the old value.");
        return;
    }
    helper_bullets_amount = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_grid_rows_per_column() const {
    return helper_grid_rows_per_column;
}

void BulletSpawner2D::set_helper_grid_rows_per_column(int value) {
    if (value < 1) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_grid_rows_per_column must be >= 1, keeping the old value.");
        return;
    }
    helper_grid_rows_per_column = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_grid_alignment() const {
    return helper_grid_alignment;
}

void BulletSpawner2D::set_helper_grid_alignment(int value) {
    if (value < (int)BulletPatterns2D::TOP_LEFT || value > (int)BulletPatterns2D::BOTTOM_RIGHT) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_grid_alignment out of range, keeping the old value.");
        return;
    }
    helper_grid_alignment = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_grid_column_offset() const {
    return helper_grid_column_offset;
}

void BulletSpawner2D::set_helper_grid_column_offset(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_grid_column_offset must be finite, keeping the old value.");
        return;
    }
    helper_grid_column_offset = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_grid_row_offset() const {
    return helper_grid_row_offset;
}

void BulletSpawner2D::set_helper_grid_row_offset(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_grid_row_offset must be finite, keeping the old value.");
        return;
    }
    helper_grid_row_offset = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_grid_rotate_with_marker() const {
    return helper_grid_rotate_with_marker;
}

void BulletSpawner2D::set_helper_grid_rotate_with_marker(bool value) {
    helper_grid_rotate_with_marker = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_grid_random_local_rotation() const {
    return helper_grid_random_local_rotation;
}

void BulletSpawner2D::set_helper_grid_random_local_rotation(bool value) {
    helper_grid_random_local_rotation = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_grid_jitter() const {
    return helper_grid_jitter;
}

void BulletSpawner2D::set_helper_grid_jitter(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_grid_jitter must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_grid_jitter = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_ring_radius() const {
    return helper_ring_radius;
}

void BulletSpawner2D::set_helper_ring_radius(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_ring_radius must be finite, keeping the old value.");
        return;
    }
    // The factory generator rejects negatives at spawn time (empty volley +
    // a spawn-path error). Reject here instead so the inspector never holds
    // a value that cannot spawn.
    if (value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_ring_radius must be >= 0, keeping the old value.");
        return;
    }
    helper_ring_radius = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_ring_start_angle() const {
    return helper_ring_start_angle;
}

void BulletSpawner2D::set_helper_ring_start_angle(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_ring_start_angle must be finite, keeping the old value.");
        return;
    }
    helper_ring_start_angle = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_ring_arc() const {
    return helper_ring_arc;
}

void BulletSpawner2D::set_helper_ring_arc(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_ring_arc must be finite, keeping the old value.");
        return;
    }
    helper_ring_arc = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_ring_rotate_with_marker() const {
    return helper_ring_rotate_with_marker;
}

void BulletSpawner2D::set_helper_ring_rotate_with_marker(bool value) {
    helper_ring_rotate_with_marker = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_ring_random_rotation() const {
    return helper_ring_random_rotation;
}

void BulletSpawner2D::set_helper_ring_random_rotation(bool value) {
    helper_ring_random_rotation = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_ring_face_outward() const {
    return helper_ring_face_outward;
}

void BulletSpawner2D::set_helper_ring_face_outward(bool value) {
    helper_ring_face_outward = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_fan_spread() const {
    return helper_fan_spread;
}

void BulletSpawner2D::set_helper_fan_spread(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_fan_spread must be finite, keeping the old value.");
        return;
    }
    helper_fan_spread = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_fan_direction_angle() const {
    return helper_fan_direction_angle;
}

void BulletSpawner2D::set_helper_fan_direction_angle(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_fan_direction_angle must be finite, keeping the old value.");
        return;
    }
    helper_fan_direction_angle = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_fan_step_offset() const {
    return helper_fan_step_offset;
}

void BulletSpawner2D::set_helper_fan_step_offset(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_fan_step_offset must be finite, keeping the old value.");
        return;
    }
    helper_fan_step_offset = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_spiral_start_radius() const {
    return helper_spiral_start_radius;
}

void BulletSpawner2D::set_helper_spiral_start_radius(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_spiral_start_radius must be finite, keeping the old value.");
        return;
    }
    // The factory generator rejects negatives at spawn time (empty volley +
    // a spawn-path error). Reject here instead so the inspector never holds
    // a value that cannot spawn (same guard as helper_ring_radius).
    if (value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_spiral_start_radius must be >= 0, keeping the old value.");
        return;
    }
    helper_spiral_start_radius = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_spiral_radius_step() const {
    return helper_spiral_radius_step;
}

void BulletSpawner2D::set_helper_spiral_radius_step(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_spiral_radius_step must be finite, keeping the old value.");
        return;
    }
    helper_spiral_radius_step = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_spiral_angle_step() const {
    return helper_spiral_angle_step;
}

void BulletSpawner2D::set_helper_spiral_angle_step(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_spiral_angle_step must be finite, keeping the old value.");
        return;
    }
    helper_spiral_angle_step = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_spiral_rotate_with_marker() const {
    return helper_spiral_rotate_with_marker;
}

void BulletSpawner2D::set_helper_spiral_rotate_with_marker(bool value) {
    helper_spiral_rotate_with_marker = value;
    on_pattern_changed();
}

Vector2 BulletSpawner2D::get_helper_line_direction() const {
    return helper_line_direction;
}

void BulletSpawner2D::set_helper_line_direction(const Vector2 &value) {
    if (!value.is_finite()) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_line_direction must be finite, keeping the old value.");
        return;
    }
    // The factory generator rejects a zero direction at spawn time. Reject
    // here instead so the inspector never holds a value that cannot spawn.
    if (value.length_squared() <= 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_line_direction must be non-zero, keeping the old value.");
        return;
    }
    helper_line_direction = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_line_spacing() const {
    return helper_line_spacing;
}

void BulletSpawner2D::set_helper_line_spacing(double value) {
    if (!Math::is_finite(value) || !(value > 0.0)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_line_spacing must be finite and > 0 (helper_line_reverse flips the order), keeping the old value.");
        return;
    }
    helper_line_spacing = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_line_face_direction() const {
    return helper_line_face_direction;
}

void BulletSpawner2D::set_helper_line_face_direction(bool value) {
    helper_line_face_direction = value;
    on_pattern_changed();
}

NodePath BulletSpawner2D::get_helper_aimed_target_path() const {
    return helper_aimed_target_path;
}

void BulletSpawner2D::set_helper_aimed_target_path(const NodePath &p_path) {
    if (!node_path_type_ok<Node2D>(this, p_path, "helper_aimed_target", "Node2D")) {
        return;
    }
    on_config_changed();
    helper_aimed_target_path = p_path;
    helper_aimed_target = nullptr;
    helper_aimed_target_id = 0;
    if (!helper_aimed_target_path.is_empty() && is_inside_tree()) {
        Node2D *resolved = resolve_node_path(this, helper_aimed_target_path, helper_aimed_target);
        helper_aimed_target_id = resolved != nullptr ? resolved->get_instance_id() : 0;
        if (resolved == nullptr) helper_aimed_target = nullptr;
    }
    on_pattern_changed();
}

Node2D *BulletSpawner2D::get_helper_aimed_target() const {
    return validate_cached_node(this, helper_aimed_target_path, helper_aimed_target, helper_aimed_target_id);
}

void BulletSpawner2D::set_helper_aimed_target(Node2D *target) {
    on_config_changed();
    assign_node_to_path(this, target, helper_aimed_target_path, helper_aimed_target);
    helper_aimed_target_id = target != nullptr ? target->get_instance_id() : 0;
    if (target == nullptr) helper_aimed_target = nullptr;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_aimed_spread() const {
    return helper_aimed_spread;
}

void BulletSpawner2D::set_helper_aimed_spread(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_aimed_spread must be finite, keeping the old value.");
        return;
    }
    helper_aimed_spread = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_aimed_step_offset() const {
    return helper_aimed_step_offset;
}

void BulletSpawner2D::set_helper_aimed_step_offset(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_aimed_step_offset must be finite, keeping the old value.");
        return;
    }
    helper_aimed_step_offset = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_ring_y_scale() const {
    return helper_ring_y_scale;
}

void BulletSpawner2D::set_helper_ring_y_scale(double value) {
    if (!Math::is_finite(value) || !(value > 0.0)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_ring_y_scale must be finite and > 0, keeping the old value.");
        return;
    }
    helper_ring_y_scale = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_ring_facing_offset_deg() const {
    return helper_ring_facing_offset_deg;
}

void BulletSpawner2D::set_helper_ring_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_ring_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_ring_facing_offset_deg = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_fan_centered() const {
    return helper_fan_centered;
}

void BulletSpawner2D::set_helper_fan_centered(bool value) {
    helper_fan_centered = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_fan_angle_jitter() const {
    return helper_fan_angle_jitter;
}

void BulletSpawner2D::set_helper_fan_angle_jitter(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_fan_angle_jitter must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_fan_angle_jitter = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_spiral_facing() const {
    return helper_spiral_facing;
}

void BulletSpawner2D::set_helper_spiral_facing(int value) {
    if (value < (int)BulletPatterns2D::SPIRAL_FACING_TANGENT || value > (int)BulletPatterns2D::SPIRAL_FACING_KEEP_MARKER) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_spiral_facing out of range, keeping the old value.");
        return;
    }
    helper_spiral_facing = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_spiral_facing_offset_deg() const {
    return helper_spiral_facing_offset_deg;
}

void BulletSpawner2D::set_helper_spiral_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_spiral_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_spiral_facing_offset_deg = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_line_anchor() const {
    return helper_line_anchor;
}

void BulletSpawner2D::set_helper_line_anchor(int value) {
    if (value < (int)BulletPatterns2D::LINE_ANCHOR_START || value > (int)BulletPatterns2D::LINE_ANCHOR_END) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_line_anchor out of range, keeping the old value.");
        return;
    }
    helper_line_anchor = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_line_facing() const { return helper_line_facing; }

void BulletSpawner2D::set_helper_line_facing(int value) {
    if (value < 0 || value > 2) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_line_facing must be 0 (along the line), 1 (+90 deg) or 2 (-90 deg), keeping the old value.");
        return;
    }
    helper_line_facing = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_line_reverse() const { return helper_line_reverse; }

void BulletSpawner2D::set_helper_line_reverse(bool value) {
    helper_line_reverse = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_line_slot_offset() const { return helper_line_slot_offset; }

void BulletSpawner2D::set_helper_line_slot_offset(int value) {
    helper_line_slot_offset = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_line_start_offset() const { return helper_line_start_offset; }

void BulletSpawner2D::set_helper_line_start_offset(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_line_start_offset must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_line_start_offset = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_aimed_centered() const {
    return helper_aimed_centered;
}

void BulletSpawner2D::set_helper_aimed_centered(bool value) {
    helper_aimed_centered = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_aimed_prediction() const {
    return helper_aimed_prediction;
}

void BulletSpawner2D::set_helper_aimed_prediction(double value) {
    if (!Math::is_finite(value) || value < 0.0 || value > 1.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_aimed_prediction must be finite in [0, 1] (0 = aim at now, 1 = full lead), keeping the old value.");
        return;
    }
    helper_aimed_prediction = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_aimed_prediction_time() const {
    return helper_aimed_prediction_time;
}

void BulletSpawner2D::set_helper_aimed_prediction_time(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_aimed_prediction_time must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_aimed_prediction_time = value;
    on_pattern_changed();
}

// New danmaku helper setters: same finite/range contract as the existing
// helpers (reject + keep old, rebuild preview on success).
int BulletSpawner2D::get_helper_flower_petals() const {
    return helper_flower_petals;
}

void BulletSpawner2D::set_helper_flower_petals(int value) {
    if (value < 1) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_flower_petals must be >= 1, keeping the old value.");
        return;
    }
    helper_flower_petals = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_flower_radius() const {
    return helper_flower_radius;
}

void BulletSpawner2D::set_helper_flower_radius(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_flower_radius must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_flower_radius = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_flower_petal_spread() const {
    return helper_flower_petal_spread;
}

void BulletSpawner2D::set_helper_flower_petal_spread(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_flower_petal_spread must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_flower_petal_spread = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_flower_petal_sharpness() const {
    return helper_flower_petal_sharpness;
}

void BulletSpawner2D::set_helper_flower_petal_sharpness(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_flower_petal_sharpness must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_flower_petal_sharpness = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_flower_base_rotation() const {
    return helper_flower_base_rotation;
}

void BulletSpawner2D::set_helper_flower_base_rotation(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_flower_base_rotation must be finite, keeping the old value.");
        return;
    }
    helper_flower_base_rotation = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_flower_face_outward() const {
    return helper_flower_face_outward;
}

void BulletSpawner2D::set_helper_flower_face_outward(bool value) {
    helper_flower_face_outward = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_flower_facing_offset_deg() const {
    return helper_flower_facing_offset_deg;
}

void BulletSpawner2D::set_helper_flower_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_flower_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_flower_facing_offset_deg = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_flower_type() const {
    return helper_flower_type;
}

void BulletSpawner2D::set_helper_flower_type(int value) {
    if (value < 0 || value > 4) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_flower_type must be in [0, 4] (FlowerBloom), keeping the old value.");
        return;
    }
    helper_flower_type = value;
    // _validate_property shows per-type knobs.
    notify_property_list_changed();
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_flower_inner_radius_scale() const {
    return helper_flower_inner_radius_scale;
}

void BulletSpawner2D::set_helper_flower_inner_radius_scale(double value) {
    if (!Math::is_finite(value) || value < 0.0 || value >= 1.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_flower_inner_radius_scale must be finite in [0, 1), keeping the old value.");
        return;
    }
    helper_flower_inner_radius_scale = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_flower_spiro_roller() const {
    return helper_flower_spiro_roller;
}

void BulletSpawner2D::set_helper_flower_spiro_roller(double value) {
    if (!Math::is_finite(value) || value <= 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_flower_spiro_roller must be finite and > 0, keeping the old value.");
        return;
    }
    helper_flower_spiro_roller = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_flower_spiro_pen() const {
    return helper_flower_spiro_pen;
}

void BulletSpawner2D::set_helper_flower_spiro_pen(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_flower_spiro_pen must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_flower_spiro_pen = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_flower_super_lobes() const {
    return helper_flower_super_lobes;
}

void BulletSpawner2D::set_helper_flower_super_lobes(double value) {
    if (!Math::is_finite(value) || value < 2.0 || value > 64.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_flower_super_lobes must be finite in [2, 64], keeping the old value.");
        return;
    }
    helper_flower_super_lobes = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_flower_super_fullness() const {
    return helper_flower_super_fullness;
}

void BulletSpawner2D::set_helper_flower_super_fullness(double value) {
    if (!Math::is_finite(value) || value <= 0.0 || value > 8.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_flower_super_fullness must be finite in (0, 8], keeping the old value.");
        return;
    }
    helper_flower_super_fullness = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_ellipse_radius_x() const {
    return helper_ellipse_radius_x;
}

void BulletSpawner2D::set_helper_ellipse_radius_x(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_ellipse_radius_x must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_ellipse_radius_x = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_ellipse_radius_y() const {
    return helper_ellipse_radius_y;
}

void BulletSpawner2D::set_helper_ellipse_radius_y(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_ellipse_radius_y must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_ellipse_radius_y = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_ellipse_rotation() const {
    return helper_ellipse_rotation;
}

void BulletSpawner2D::set_helper_ellipse_rotation(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_ellipse_rotation must be finite, keeping the old value.");
        return;
    }
    helper_ellipse_rotation = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_ellipse_start_angle() const {
    return helper_ellipse_start_angle;
}

void BulletSpawner2D::set_helper_ellipse_start_angle(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_ellipse_start_angle must be finite, keeping the old value.");
        return;
    }
    helper_ellipse_start_angle = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_ellipse_arc() const {
    return helper_ellipse_arc;
}

void BulletSpawner2D::set_helper_ellipse_arc(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_ellipse_arc must be finite, keeping the old value.");
        return;
    }
    helper_ellipse_arc = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_ellipse_mode() const {
    return helper_ellipse_mode;
}

void BulletSpawner2D::set_helper_ellipse_mode(int value) {
    if (value < (int)BulletPatterns2D::ELLIPSE_FULL || value > (int)BulletPatterns2D::ELLIPSE_WALL) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_ellipse_mode out of range, keeping the old value.");
        return;
    }
    helper_ellipse_mode = value;
    // WALL reveals the gap pair in the inspector; mode switches hide it.
    notify_property_list_changed();
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_ellipse_gap_count() const {
    return helper_ellipse_gap_count;
}

void BulletSpawner2D::set_helper_ellipse_gap_count(int value) {
    if (value < 0 || value > kMaxBulletsPerVolley) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_ellipse_gap_count must be between 0 and " + itos(kMaxBulletsPerVolley) + ", keeping the old value.");
        return;
    }
    helper_ellipse_gap_count = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_ellipse_gap_width() const {
    return helper_ellipse_gap_width;
}

void BulletSpawner2D::set_helper_ellipse_gap_width(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_ellipse_gap_width must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_ellipse_gap_width = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_ellipse_face_outward() const {
    return helper_ellipse_face_outward;
}

void BulletSpawner2D::set_helper_ellipse_face_outward(bool value) {
    helper_ellipse_face_outward = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_ellipse_facing_offset_deg() const {
    return helper_ellipse_facing_offset_deg;
}

void BulletSpawner2D::set_helper_ellipse_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_ellipse_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_ellipse_facing_offset_deg = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_rain_band_width() const {
    return helper_rain_band_width;
}

void BulletSpawner2D::set_helper_rain_band_width(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_rain_band_width must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_rain_band_width = value;
    on_pattern_changed();
}

Vector2 BulletSpawner2D::get_helper_rain_direction() const {
    return helper_rain_direction;
}

void BulletSpawner2D::set_helper_rain_direction(const Vector2 &value) {
    if (!value.is_finite()) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_rain_direction must be finite, keeping the old value.");
        return;
    }
    if (value.length_squared() <= 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_rain_direction must be non-zero, keeping the old value.");
        return;
    }
    helper_rain_direction = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_rain_drop_spacing() const {
    return helper_rain_drop_spacing;
}

void BulletSpawner2D::set_helper_rain_drop_spacing(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_rain_drop_spacing must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_rain_drop_spacing = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_rain_jitter() const {
    return helper_rain_jitter;
}

void BulletSpawner2D::set_helper_rain_jitter(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_rain_jitter must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_rain_jitter = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_scatter_burst_radius() const {
    return helper_scatter_burst_radius;
}

void BulletSpawner2D::set_helper_scatter_burst_radius(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_scatter_burst_radius must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_scatter_burst_radius = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_scatter_facing_jitter() const {
    return helper_scatter_facing_jitter;
}

void BulletSpawner2D::set_helper_scatter_facing_jitter(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_scatter_facing_jitter must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_scatter_facing_jitter = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_scatter_seed() const {
    return helper_scatter_seed;
}

void BulletSpawner2D::set_helper_scatter_seed(int value) {
    if (value < 0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_scatter_seed must be >= 0 (0 = non-deterministic), keeping the old value.");
        return;
    }
    helper_scatter_seed = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_scatter_inner_radius() const {
    return helper_scatter_inner_radius;
}

void BulletSpawner2D::set_helper_scatter_inner_radius(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_scatter_inner_radius must be finite and >= 0 (0 = full disc), keeping the old value.");
        return;
    }
    helper_scatter_inner_radius = value;
    on_pattern_changed();
}

Vector2 BulletSpawner2D::get_helper_scatter_direction() const {
    return helper_scatter_direction;
}

void BulletSpawner2D::set_helper_scatter_direction(const Vector2 &value) {
    if (!value.is_finite()) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_scatter_direction must be finite, keeping the old value.");
        return;
    }
    if (value.length_squared() <= 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_scatter_direction must be non-zero, keeping the old value.");
        return;
    }
    helper_scatter_direction = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_scatter_arc() const {
    return helper_scatter_arc;
}

void BulletSpawner2D::set_helper_scatter_arc(double value) {
    if (!Math::is_finite(value) || !(value > 0.0)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_scatter_arc must be finite and > 0 (TAU or more = full circle), keeping the old value.");
        return;
    }
    helper_scatter_arc = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_scatter_facing() const {
    return helper_scatter_facing;
}

void BulletSpawner2D::set_helper_scatter_facing(int value) {
    if (value < 0 || value > 2) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_scatter_facing must be 0 (outward), 1 (random) or 2 (inward), keeping the old value.");
        return;
    }
    helper_scatter_facing = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_star_polygon_vertices() const {
    return helper_star_polygon_vertices;
}

void BulletSpawner2D::set_helper_star_polygon_vertices(int value) {
    if (value < 3) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_star_polygon_vertices must be >= 3, keeping the old value.");
        return;
    }
    helper_star_polygon_vertices = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_star_polygon_radius() const {
    return helper_star_polygon_radius;
}

void BulletSpawner2D::set_helper_star_polygon_radius(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_star_polygon_radius must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_star_polygon_radius = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_star_polygon_vertex_bias() const {
    return helper_star_polygon_vertex_bias;
}

void BulletSpawner2D::set_helper_star_polygon_vertex_bias(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_star_polygon_vertex_bias must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_star_polygon_vertex_bias = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_star_polygon_base_rotation() const {
    return helper_star_polygon_base_rotation;
}

void BulletSpawner2D::set_helper_star_polygon_base_rotation(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_star_polygon_base_rotation must be finite, keeping the old value.");
        return;
    }
    helper_star_polygon_base_rotation = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_star_polygon_face_outward() const {
    return helper_star_polygon_face_outward;
}

void BulletSpawner2D::set_helper_star_polygon_face_outward(bool value) {
    helper_star_polygon_face_outward = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_star_polygon_facing_offset_deg() const {
    return helper_star_polygon_facing_offset_deg;
}

void BulletSpawner2D::set_helper_star_polygon_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_star_polygon_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_star_polygon_facing_offset_deg = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_multispiral_arms() const {
    return helper_multispiral_arms;
}

void BulletSpawner2D::set_helper_multispiral_arms(int value) {
    if (value < 1) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_multispiral_arms must be >= 1, keeping the old value.");
        return;
    }
    helper_multispiral_arms = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_multispiral_start_radius() const {
    return helper_multispiral_start_radius;
}

void BulletSpawner2D::set_helper_multispiral_start_radius(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_multispiral_start_radius must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_multispiral_start_radius = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_multispiral_radius_step() const {
    return helper_multispiral_radius_step;
}

void BulletSpawner2D::set_helper_multispiral_radius_step(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_multispiral_radius_step must be finite, keeping the old value.");
        return;
    }
    helper_multispiral_radius_step = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_multispiral_angle_step() const {
    return helper_multispiral_angle_step;
}

void BulletSpawner2D::set_helper_multispiral_angle_step(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_multispiral_angle_step must be finite, keeping the old value.");
        return;
    }
    helper_multispiral_angle_step = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_multispiral_rotate_with_marker() const {
    return helper_multispiral_rotate_with_marker;
}

void BulletSpawner2D::set_helper_multispiral_rotate_with_marker(bool value) {
    helper_multispiral_rotate_with_marker = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_multispiral_facing() const {
    return helper_multispiral_facing;
}

void BulletSpawner2D::set_helper_multispiral_facing(int value) {
    if (value < (int)BulletPatterns2D::SPIRAL_FACING_TANGENT || value > (int)BulletPatterns2D::SPIRAL_FACING_KEEP_MARKER) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_multispiral_facing out of range, keeping the old value.");
        return;
    }
    helper_multispiral_facing = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_multispiral_facing_offset_deg() const {
    return helper_multispiral_facing_offset_deg;
}

void BulletSpawner2D::set_helper_multispiral_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_multispiral_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_multispiral_facing_offset_deg = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_multispiral_arm_stride() const {
    return helper_multispiral_arm_stride;
}

void BulletSpawner2D::set_helper_multispiral_arm_stride(int value) {
    if (value < 1) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_multispiral_arm_stride must be >= 1, keeping the old value.");
        return;
    }
    helper_multispiral_arm_stride = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_cross_arm_count() const { return helper_cross_arm_count; }

void BulletSpawner2D::set_helper_cross_arm_count(int value) {
    if (value < 1) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_cross_arm_count must be >= 1, keeping the old value.");
        return;
    }
    helper_cross_arm_count = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_cross_arm_length() const { return helper_cross_arm_length; }

void BulletSpawner2D::set_helper_cross_arm_length(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_cross_arm_length must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_cross_arm_length = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_cross_spacing() const { return helper_cross_spacing; }

void BulletSpawner2D::set_helper_cross_spacing(double value) {
    if (!Math::is_finite(value) || value <= 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_cross_spacing must be finite and > 0, keeping the old value.");
        return;
    }
    helper_cross_spacing = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_cross_base_rotation() const { return helper_cross_base_rotation; }

void BulletSpawner2D::set_helper_cross_base_rotation(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_cross_base_rotation must be finite, keeping the old value.");
        return;
    }
    helper_cross_base_rotation = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_cross_face_outward() const { return helper_cross_face_outward; }

void BulletSpawner2D::set_helper_cross_face_outward(bool value) {
    helper_cross_face_outward = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_cross_facing_offset_deg() const { return helper_cross_facing_offset_deg; }

void BulletSpawner2D::set_helper_cross_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_cross_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_cross_facing_offset_deg = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_star_points() const { return helper_star_points; }

void BulletSpawner2D::set_helper_star_points(int value) {
    if (value < 2 || value > kMaxBulletsPerVolley) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_star_points must be between 2 and " + itos(kMaxBulletsPerVolley) + ", keeping the old value.");
        return;
    }
    helper_star_points = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_star_outer_radius() const { return helper_star_outer_radius; }

void BulletSpawner2D::set_helper_star_outer_radius(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_star_outer_radius must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_star_outer_radius = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_star_inner_radius() const { return helper_star_inner_radius; }

void BulletSpawner2D::set_helper_star_inner_radius(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_star_inner_radius must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_star_inner_radius = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_star_base_rotation() const { return helper_star_base_rotation; }

void BulletSpawner2D::set_helper_star_base_rotation(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_star_base_rotation must be finite, keeping the old value.");
        return;
    }
    helper_star_base_rotation = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_star_face_outward() const { return helper_star_face_outward; }

void BulletSpawner2D::set_helper_star_face_outward(bool value) {
    helper_star_face_outward = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_star_facing_offset_deg() const { return helper_star_facing_offset_deg; }

void BulletSpawner2D::set_helper_star_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_star_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_star_facing_offset_deg = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_heart_size() const { return helper_heart_size; }

void BulletSpawner2D::set_helper_heart_size(double value) {
    if (!Math::is_finite(value) || value <= 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_heart_size must be finite and > 0, keeping the old value.");
        return;
    }
    helper_heart_size = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_heart_base_rotation() const { return helper_heart_base_rotation; }

void BulletSpawner2D::set_helper_heart_base_rotation(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_heart_base_rotation must be finite, keeping the old value.");
        return;
    }
    helper_heart_base_rotation = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_heart_face_outward() const { return helper_heart_face_outward; }

void BulletSpawner2D::set_helper_heart_face_outward(bool value) {
    helper_heart_face_outward = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_heart_facing_offset_deg() const { return helper_heart_facing_offset_deg; }

void BulletSpawner2D::set_helper_heart_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_heart_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_heart_facing_offset_deg = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_wave_width() const { return helper_wave_width; }

void BulletSpawner2D::set_helper_wave_width(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_wave_width must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_wave_width = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_wave_amplitude() const { return helper_wave_amplitude; }

void BulletSpawner2D::set_helper_wave_amplitude(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_wave_amplitude must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_wave_amplitude = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_wave_waves() const { return helper_wave_waves; }

void BulletSpawner2D::set_helper_wave_waves(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_wave_waves must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_wave_waves = value;
    on_pattern_changed();
}

Vector2 BulletSpawner2D::get_helper_wave_direction() const { return helper_wave_direction; }

void BulletSpawner2D::set_helper_wave_direction(const Vector2 &value) {
    if (!value.is_finite() || value.length_squared() <= 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_wave_direction must be finite and non-zero, keeping the old value.");
        return;
    }
    helper_wave_direction = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_wave_face_direction() const { return helper_wave_face_direction; }

void BulletSpawner2D::set_helper_wave_face_direction(bool value) {
    helper_wave_face_direction = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_wave_facing_offset_deg() const { return helper_wave_facing_offset_deg; }

void BulletSpawner2D::set_helper_wave_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_wave_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_wave_facing_offset_deg = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_waterfall_columns() const { return helper_waterfall_columns; }

void BulletSpawner2D::set_helper_waterfall_columns(int value) {
    if (value < 1 || value > kMaxGridSlots) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_waterfall_columns must be between 1 and " + itos(kMaxGridSlots) + ", keeping the old value.");
        return;
    }
    helper_waterfall_columns = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_waterfall_column_spacing() const { return helper_waterfall_column_spacing; }

void BulletSpawner2D::set_helper_waterfall_column_spacing(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_waterfall_column_spacing must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_waterfall_column_spacing = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_waterfall_rows() const { return helper_waterfall_rows; }

void BulletSpawner2D::set_helper_waterfall_rows(int value) {
    if (value < 1 || value > kMaxGridSlots) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_waterfall_rows must be between 1 and " + itos(kMaxGridSlots) + ", keeping the old value.");
        return;
    }
    helper_waterfall_rows = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_waterfall_row_spacing() const { return helper_waterfall_row_spacing; }

void BulletSpawner2D::set_helper_waterfall_row_spacing(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_waterfall_row_spacing must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_waterfall_row_spacing = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_waterfall_stagger() const { return helper_waterfall_stagger; }

void BulletSpawner2D::set_helper_waterfall_stagger(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_waterfall_stagger must be finite, keeping the old value.");
        return;
    }
    helper_waterfall_stagger = value;
    on_pattern_changed();
}

Vector2 BulletSpawner2D::get_helper_waterfall_rain_direction() const { return helper_waterfall_rain_direction; }

void BulletSpawner2D::set_helper_waterfall_rain_direction(const Vector2 &value) {
    if (!value.is_finite() || value.length_squared() <= 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_waterfall_rain_direction must be finite and non-zero, keeping the old value.");
        return;
    }
    helper_waterfall_rain_direction = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_waterfall_jitter() const { return helper_waterfall_jitter; }

void BulletSpawner2D::set_helper_waterfall_jitter(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_waterfall_jitter must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_waterfall_jitter = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_waterfall_facing_offset_deg() const { return helper_waterfall_facing_offset_deg; }

void BulletSpawner2D::set_helper_waterfall_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_waterfall_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_waterfall_facing_offset_deg = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_lattice_columns() const { return helper_lattice_columns; }

void BulletSpawner2D::set_helper_lattice_columns(int value) {
    if (value < 1 || value > kMaxGridSlots) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_lattice_columns must be between 1 and " + itos(kMaxGridSlots) + ", keeping the old value.");
        return;
    }
    helper_lattice_columns = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_lattice_rows() const { return helper_lattice_rows; }

void BulletSpawner2D::set_helper_lattice_rows(int value) {
    if (value < 1 || value > kMaxGridSlots) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_lattice_rows must be between 1 and " + itos(kMaxGridSlots) + ", keeping the old value.");
        return;
    }
    helper_lattice_rows = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_lattice_spacing_x() const { return helper_lattice_spacing_x; }

void BulletSpawner2D::set_helper_lattice_spacing_x(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_lattice_spacing_x must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_lattice_spacing_x = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_lattice_spacing_y() const { return helper_lattice_spacing_y; }

void BulletSpawner2D::set_helper_lattice_spacing_y(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_lattice_spacing_y must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_lattice_spacing_y = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_lattice_stagger_rows() const { return helper_lattice_stagger_rows; }

void BulletSpawner2D::set_helper_lattice_stagger_rows(bool value) {
    helper_lattice_stagger_rows = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_lattice_face_outward() const { return helper_lattice_face_outward; }

void BulletSpawner2D::set_helper_lattice_face_outward(bool value) {
    helper_lattice_face_outward = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_lattice_facing_offset_deg() const { return helper_lattice_facing_offset_deg; }

void BulletSpawner2D::set_helper_lattice_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_lattice_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_lattice_facing_offset_deg = value;
    on_pattern_changed();
}

PackedInt32Array BulletSpawner2D::get_helper_skip_indices() const { return helper_skip_indices; }

void BulletSpawner2D::set_helper_skip_indices(const PackedInt32Array &value) {
    helper_skip_indices = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_rose_petals() const { return helper_rose_petals; }

void BulletSpawner2D::set_helper_rose_petals(int value) {
    if (value < 2) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_rose_petals must be >= 2, keeping the old value.");
        return;
    }
    helper_rose_petals = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_rose_radius() const { return helper_rose_radius; }

void BulletSpawner2D::set_helper_rose_radius(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_rose_radius must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_rose_radius = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_rose_lobe_sharpness() const { return helper_rose_lobe_sharpness; }

void BulletSpawner2D::set_helper_rose_lobe_sharpness(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_rose_lobe_sharpness must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_rose_lobe_sharpness = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_rose_base_rotation() const { return helper_rose_base_rotation; }

void BulletSpawner2D::set_helper_rose_base_rotation(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_rose_base_rotation must be finite, keeping the old value.");
        return;
    }
    helper_rose_base_rotation = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_rose_face_outward() const { return helper_rose_face_outward; }

void BulletSpawner2D::set_helper_rose_face_outward(bool value) {
    helper_rose_face_outward = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_rose_facing_offset_deg() const { return helper_rose_facing_offset_deg; }

void BulletSpawner2D::set_helper_rose_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_rose_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_rose_facing_offset_deg = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_counter_spiral_arms() const { return helper_counter_spiral_arms; }

void BulletSpawner2D::set_helper_counter_spiral_arms(int value) {
    if (value < 2) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_counter_spiral_arms must be >= 2 (use multispiral for 1 arm), keeping the old value.");
        return;
    }
    helper_counter_spiral_arms = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_counter_spiral_start_radius() const { return helper_counter_spiral_start_radius; }

void BulletSpawner2D::set_helper_counter_spiral_start_radius(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_counter_spiral_start_radius must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_counter_spiral_start_radius = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_counter_spiral_radius_step() const { return helper_counter_spiral_radius_step; }

void BulletSpawner2D::set_helper_counter_spiral_radius_step(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_counter_spiral_radius_step must be finite, keeping the old value.");
        return;
    }
    helper_counter_spiral_radius_step = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_counter_spiral_angle_step() const { return helper_counter_spiral_angle_step; }

void BulletSpawner2D::set_helper_counter_spiral_angle_step(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_counter_spiral_angle_step must be finite, keeping the old value.");
        return;
    }
    helper_counter_spiral_angle_step = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_counter_spiral_rotate_with_marker() const { return helper_counter_spiral_rotate_with_marker; }

void BulletSpawner2D::set_helper_counter_spiral_rotate_with_marker(bool value) {
    helper_counter_spiral_rotate_with_marker = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_counter_spiral_facing() const { return helper_counter_spiral_facing; }

void BulletSpawner2D::set_helper_counter_spiral_facing(int value) {
    if (value < (int)BulletPatterns2D::SPIRAL_FACING_TANGENT || value > (int)BulletPatterns2D::SPIRAL_FACING_KEEP_MARKER) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_counter_spiral_facing out of range, keeping the old value.");
        return;
    }
    helper_counter_spiral_facing = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_counter_spiral_facing_offset_deg() const { return helper_counter_spiral_facing_offset_deg; }

void BulletSpawner2D::set_helper_counter_spiral_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_counter_spiral_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_counter_spiral_facing_offset_deg = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_counter_spiral_arm_stride() const { return helper_counter_spiral_arm_stride; }

void BulletSpawner2D::set_helper_counter_spiral_arm_stride(int value) {
    if (value < 1) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_counter_spiral_arm_stride must be >= 1, keeping the old value.");
        return;
    }
    helper_counter_spiral_arm_stride = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_counter_spiral_mirror_alternate_arms() const { return helper_counter_spiral_mirror_alternate_arms; }

void BulletSpawner2D::set_helper_counter_spiral_mirror_alternate_arms(bool value) {
    helper_counter_spiral_mirror_alternate_arms = value;
    on_pattern_changed();
}

Vector2 BulletSpawner2D::get_helper_corridor_aim_direction() const { return helper_corridor_aim_direction; }

void BulletSpawner2D::set_helper_corridor_aim_direction(const Vector2 &value) {
    if (!value.is_finite()) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_corridor_aim_direction must be finite, keeping the old value.");
        return;
    }
    if (value.length_squared() <= 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_corridor_aim_direction must be non-zero, keeping the old value.");
        return;
    }
    helper_corridor_aim_direction = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_corridor_width() const { return helper_corridor_width; }

void BulletSpawner2D::set_helper_corridor_width(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_corridor_width must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_corridor_width = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_corridor_gap_width() const { return helper_corridor_gap_width; }

void BulletSpawner2D::set_helper_corridor_gap_width(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_corridor_gap_width must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_corridor_gap_width = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_corridor_face_aim() const { return helper_corridor_face_aim; }

void BulletSpawner2D::set_helper_corridor_face_aim(bool value) {
    helper_corridor_face_aim = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_corridor_facing_offset_deg() const { return helper_corridor_facing_offset_deg; }

void BulletSpawner2D::set_helper_corridor_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_corridor_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_corridor_facing_offset_deg = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_lissajous_size_x() const { return helper_lissajous_size_x; }

void BulletSpawner2D::set_helper_lissajous_size_x(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_lissajous_size_x must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_lissajous_size_x = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_lissajous_size_y() const { return helper_lissajous_size_y; }

void BulletSpawner2D::set_helper_lissajous_size_y(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_lissajous_size_y must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_lissajous_size_y = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_lissajous_freq_x() const { return helper_lissajous_freq_x; }

void BulletSpawner2D::set_helper_lissajous_freq_x(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_lissajous_freq_x must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_lissajous_freq_x = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_lissajous_freq_y() const { return helper_lissajous_freq_y; }

void BulletSpawner2D::set_helper_lissajous_freq_y(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_lissajous_freq_y must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_lissajous_freq_y = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_lissajous_phase() const { return helper_lissajous_phase; }

void BulletSpawner2D::set_helper_lissajous_phase(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_lissajous_phase must be finite, keeping the old value.");
        return;
    }
    helper_lissajous_phase = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_lissajous_face_outward() const { return helper_lissajous_face_outward; }

void BulletSpawner2D::set_helper_lissajous_face_outward(bool value) {
    helper_lissajous_face_outward = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_lissajous_facing_offset_deg() const { return helper_lissajous_facing_offset_deg; }

void BulletSpawner2D::set_helper_lissajous_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_lissajous_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_lissajous_facing_offset_deg = value;
    on_pattern_changed();
}

TypedArray<Transform2D> BulletSpawner2D::get_helper_custom_transforms() const { return helper_custom_transforms; }

void BulletSpawner2D::set_helper_custom_transforms(const TypedArray<Transform2D> &value) {
    on_config_changed();
    // One shoot fires this array into transforms + buffers + physics: the
    // same freeze/OOM rationale as helper_bullets_amount caps it at 10000.
    if (value.size() > kMaxBulletsPerVolley) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_custom_transforms must hold <= 10000 entries, keeping the old value.");
        return;
    }
    for (int i = 0; i < value.size(); ++i) {
        // Type first, conversion second: inspector array edits can hand
        // transient nulls/wrong types across, and a blind Variant cast is
        // what wedged the editor. Finite check rides on the typed value.
        const Variant element = value[i];
        if (element.get_type() != Variant::TRANSFORM2D) {
            UtilityFunctions::push_error("BulletSpawner2D: helper_custom_transforms must hold only Transform2D entries, keeping the old value.");
            return;
        }
        if (!((Transform2D)element).is_finite()) {
            UtilityFunctions::push_error("BulletSpawner2D: helper_custom_transforms must hold finite transforms, keeping the old value.");
            return;
        }
    }
    helper_custom_transforms = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_custom_facing() const { return helper_custom_facing; }

void BulletSpawner2D::set_helper_custom_facing(int value) {
    if (value < 0 || value > 4) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_custom_facing must be 0 (as stored), 1 (face outward), 2 (face inward), 3 (+90 deg) or 4 (-90 deg), keeping the old value.");
        return;
    }
    helper_custom_facing = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_custom_facing_offset_deg() const { return helper_custom_facing_offset_deg; }

void BulletSpawner2D::set_helper_custom_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_custom_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_custom_facing_offset_deg = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_custom_reverse() const { return helper_custom_reverse; }

void BulletSpawner2D::set_helper_custom_reverse(bool value) {
    helper_custom_reverse = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_custom_slot_offset() const { return helper_custom_slot_offset; }

void BulletSpawner2D::set_helper_custom_slot_offset(int value) {
    helper_custom_slot_offset = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_triangle_type() const { return helper_triangle_type; }

void BulletSpawner2D::set_helper_triangle_type(int value) {
    if (value < (int)BulletPatterns2D::TRIANGLE_EQUILATERAL || value > (int)BulletPatterns2D::TRIANGLE_RIGHT) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_triangle_type must be 0 (equilateral), 1 (isosceles) or 2 (right), keeping the old value.");
        return;
    }
    helper_triangle_type = value;
    notify_property_list_changed();
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_triangle_size_a() const { return helper_triangle_size_a; }

void BulletSpawner2D::set_helper_triangle_size_a(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_triangle_size_a must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_triangle_size_a = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_triangle_size_b() const { return helper_triangle_size_b; }

void BulletSpawner2D::set_helper_triangle_size_b(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_triangle_size_b must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_triangle_size_b = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_triangle_rotation() const { return helper_triangle_rotation; }

void BulletSpawner2D::set_helper_triangle_rotation(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_triangle_rotation must be finite, keeping the old value.");
        return;
    }
    helper_triangle_rotation = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_triangle_face_outward() const { return helper_triangle_face_outward; }

void BulletSpawner2D::set_helper_triangle_face_outward(bool value) {
    helper_triangle_face_outward = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_triangle_facing_offset_deg() const { return helper_triangle_facing_offset_deg; }

void BulletSpawner2D::set_helper_triangle_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_triangle_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_triangle_facing_offset_deg = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_trapezoid_base_top() const { return helper_trapezoid_base_top; }

void BulletSpawner2D::set_helper_trapezoid_base_top(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_trapezoid_base_top must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_trapezoid_base_top = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_trapezoid_base_bottom() const { return helper_trapezoid_base_bottom; }

void BulletSpawner2D::set_helper_trapezoid_base_bottom(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_trapezoid_base_bottom must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_trapezoid_base_bottom = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_trapezoid_height() const { return helper_trapezoid_height; }

void BulletSpawner2D::set_helper_trapezoid_height(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_trapezoid_height must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_trapezoid_height = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_trapezoid_rotation() const { return helper_trapezoid_rotation; }

void BulletSpawner2D::set_helper_trapezoid_rotation(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_trapezoid_rotation must be finite, keeping the old value.");
        return;
    }
    helper_trapezoid_rotation = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_trapezoid_face_outward() const { return helper_trapezoid_face_outward; }

void BulletSpawner2D::set_helper_trapezoid_face_outward(bool value) {
    helper_trapezoid_face_outward = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_trapezoid_facing_offset_deg() const { return helper_trapezoid_facing_offset_deg; }

void BulletSpawner2D::set_helper_trapezoid_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_trapezoid_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_trapezoid_facing_offset_deg = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_diamond_diagonal_x() const { return helper_diamond_diagonal_x; }

void BulletSpawner2D::set_helper_diamond_diagonal_x(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_diamond_diagonal_x must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_diamond_diagonal_x = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_diamond_diagonal_y() const { return helper_diamond_diagonal_y; }

void BulletSpawner2D::set_helper_diamond_diagonal_y(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_diamond_diagonal_y must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_diamond_diagonal_y = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_diamond_rotation() const { return helper_diamond_rotation; }

void BulletSpawner2D::set_helper_diamond_rotation(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_diamond_rotation must be finite, keeping the old value.");
        return;
    }
    helper_diamond_rotation = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_diamond_face_outward() const { return helper_diamond_face_outward; }

void BulletSpawner2D::set_helper_diamond_face_outward(bool value) {
    helper_diamond_face_outward = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_diamond_facing_offset_deg() const { return helper_diamond_facing_offset_deg; }

void BulletSpawner2D::set_helper_diamond_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_diamond_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_diamond_facing_offset_deg = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_outline_placement() const { return helper_outline_placement; }

void BulletSpawner2D::set_helper_outline_placement(int value) {
    if (value < (int)BulletPatterns2D::OUTLINE_ON_OUTLINE || value > (int)BulletPatterns2D::OUTLINE_FILL_INSIDE) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_outline_placement must be 0 (on outline), 1 (layers) or 2 (fill inside), keeping the old value.");
        return;
    }
    helper_outline_placement = value;
    // Fill/layer dims only show in their own mode.
    notify_property_list_changed();
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_outline_facing() const { return helper_outline_facing; }

void BulletSpawner2D::set_helper_outline_facing(int value) {
    if (value < (int)BulletPatterns2D::OUTLINE_FACING_NORMAL || value > (int)BulletPatterns2D::OUTLINE_FACING_ALONG_M90) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_outline_facing must be 0 (outline normal), 1 (+90 deg) or 2 (-90 deg), keeping the old value.");
        return;
    }
    helper_outline_facing = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_outline_reverse() const { return helper_outline_reverse; }

void BulletSpawner2D::set_helper_outline_reverse(bool value) {
    helper_outline_reverse = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_outline_slot_offset() const { return helper_outline_slot_offset; }

void BulletSpawner2D::set_helper_outline_slot_offset(int value) {
    helper_outline_slot_offset = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_outline_fill_spacing() const { return helper_outline_fill_spacing; }

void BulletSpawner2D::set_helper_outline_fill_spacing(double value) {
    if (!Math::is_finite(value) || value <= 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_outline_fill_spacing must be finite and > 0, keeping the old value.");
        return;
    }
    helper_outline_fill_spacing = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_outline_fill_stagger() const { return helper_outline_fill_stagger; }

void BulletSpawner2D::set_helper_outline_fill_stagger(bool value) {
    helper_outline_fill_stagger = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_outline_fill_margin() const { return helper_outline_fill_margin; }

void BulletSpawner2D::set_helper_outline_fill_margin(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_outline_fill_margin must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_outline_fill_margin = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_outline_layer_count() const { return helper_outline_layer_count; }

void BulletSpawner2D::set_helper_outline_layer_count(int value) {
    if (value < 1 || value > kMaxOutlineLayers) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_outline_layer_count must be in [1, 64] (1 = single exact layer), keeping the old value.");
        return;
    }
    helper_outline_layer_count = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_outline_layer_scale() const { return helper_outline_layer_scale; }

void BulletSpawner2D::set_helper_outline_layer_scale(double value) {
    if (!Math::is_finite(value) || value <= 0.0 || value > 8.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_outline_layer_scale must be finite in (0, 8] (fractional growth per layer, e.g. 0.2 = 20%), keeping the old value.");
        return;
    }
    helper_outline_layer_scale = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_outline_layer_side() const { return helper_outline_layer_side; }

void BulletSpawner2D::set_helper_outline_layer_side(int value) {
    if (value < (int)BulletPatterns2D::OUTLINE_LAYER_OUTWARD || value > (int)BulletPatterns2D::OUTLINE_LAYER_BOTH) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_outline_layer_side must be 0 (outward), 1 (inward) or 2 (both), keeping the old value.");
        return;
    }
    helper_outline_layer_side = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_outline_layer_fill() const { return helper_outline_layer_fill; }

void BulletSpawner2D::set_helper_outline_layer_fill(int value) {
    if (value < (int)BulletPatterns2D::OUTLINE_LAYER_INTERLEAVED || value > (int)BulletPatterns2D::OUTLINE_LAYER_PINGPONG) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_outline_layer_fill must be 0 (interleaved), 1 (sequential), 2 (outer first) or 3 (ping-pong), keeping the old value.");
        return;
    }
    helper_outline_layer_fill = value;
    // Sequential-family reveals the start-offset control in the inspector.
    notify_property_list_changed();
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_outline_layer_scale_curve() const { return helper_outline_layer_scale_curve; }

void BulletSpawner2D::set_helper_outline_layer_scale_curve(int value) {
    if (value < (int)BulletPatterns2D::OUTLINE_LAYER_CURVE_LINEAR || value > (int)BulletPatterns2D::OUTLINE_LAYER_CURVE_EXPONENTIAL) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_outline_layer_scale_curve must be 0 (linear) or 1 (exponential), keeping the old value.");
        return;
    }
    helper_outline_layer_scale_curve = value;
    on_pattern_changed();
}

PackedFloat32Array BulletSpawner2D::get_helper_outline_layer_scales() const { return helper_outline_layer_scales; }

void BulletSpawner2D::set_helper_outline_layer_scales(const PackedFloat32Array &value) {
    if (value.size() > kMaxOutlineLayers) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_outline_layer_scales holds at most 64 entries, keeping the old value.");
        return;
    }
    for (int i = 0; i < value.size(); ++i) {
        const double s = (double)value[i];
        if (!Math::is_finite(s) || s < 0.05 || s > 64.0) {
            UtilityFunctions::push_error("BulletSpawner2D: helper_outline_layer_scales entries must be finite in [0.05, 64], keeping the old value.");
            return;
        }
    }
    helper_outline_layer_scales = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_outline_layer_twist() const { return helper_outline_layer_twist; }

void BulletSpawner2D::set_helper_outline_layer_twist(int value) {
    helper_outline_layer_twist = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_outline_layer_max_dots() const { return helper_outline_layer_max_dots; }

void BulletSpawner2D::set_helper_outline_layer_max_dots(int value) {
    if (value < 0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_outline_layer_max_dots must be >= 0 (0 = unlimited), keeping the old value.");
        return;
    }
    helper_outline_layer_max_dots = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_outline_layer_start_offset() const { return helper_outline_layer_start_offset; }

void BulletSpawner2D::set_helper_outline_layer_start_offset(int value) {
    if (value < 0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_outline_layer_start_offset must be >= 0 (0 = start on the outline), keeping the old value.");
        return;
    }
    helper_outline_layer_start_offset = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_outline_distribution() const { return helper_outline_distribution; }

void BulletSpawner2D::set_helper_outline_distribution(int value) {
    if (value < (int)BulletPatterns2D::OUTLINE_DISTRIBUTION_LEGACY || value > (int)BulletPatterns2D::OUTLINE_DISTRIBUTION_SYMMETRIC) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_outline_distribution must be 0 (legacy) or 1 (symmetric), keeping the old value.");
        return;
    }
    helper_outline_distribution = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_outline_layer_layout() const { return helper_outline_layer_layout; }

void BulletSpawner2D::set_helper_outline_layer_layout(int value) {
    if (value < (int)BulletPatterns2D::OUTLINE_LAYER_LAYOUT_SHARED_LOOP || value > (int)BulletPatterns2D::OUTLINE_LAYER_LAYOUT_EVEN_PER_LAYER) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_outline_layer_layout must be 0 (shared loop) or 1 (even per layer), keeping the old value.");
        return;
    }
    helper_outline_layer_layout = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_outline_corner_priority() const { return helper_outline_corner_priority; }

void BulletSpawner2D::set_helper_outline_corner_priority(int value) {
    if (value < (int)BulletPatterns2D::OUTLINE_CORNER_PRIORITY_HORIZONTAL || value > (int)BulletPatterns2D::OUTLINE_CORNER_PRIORITY_BALANCED) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_outline_corner_priority must be 0 (horizontal), 1 (vertical) or 2 (balanced), keeping the old value.");
        return;
    }
    helper_outline_corner_priority = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_outline_corner_mode() const { return helper_outline_corner_mode; }

void BulletSpawner2D::set_helper_outline_corner_mode(int value) {
    if (value < (int)BulletPatterns2D::OUTLINE_CORNER_MODE_PIN_CORNERS || value > (int)BulletPatterns2D::OUTLINE_CORNER_MODE_EVEN_ARC) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_outline_corner_mode must be 0 (pin corners) or 1 (even arc), keeping the old value.");
        return;
    }
    helper_outline_corner_mode = value;
    // Even-arc hides the edge-margin control in the inspector.
    notify_property_list_changed();
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_outline_edge_margin() const { return helper_outline_edge_margin; }

void BulletSpawner2D::set_helper_outline_edge_margin(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_outline_edge_margin must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_outline_edge_margin = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_outline_corner_facing() const { return helper_outline_corner_facing; }

void BulletSpawner2D::set_helper_outline_corner_facing(int value) {
    if (value < (int)BulletPatterns2D::OUTLINE_CORNER_FACING_SIDE || value > (int)BulletPatterns2D::OUTLINE_CORNER_FACING_SMOOTH) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_outline_corner_facing must be 0 (side), 1 (miter) or 2 (smooth), keeping the old value.");
        return;
    }
    helper_outline_corner_facing = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_circle_radius() const { return helper_circle_radius; }

void BulletSpawner2D::set_helper_circle_radius(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_circle_radius must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_circle_radius = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_circle_face_outward() const { return helper_circle_face_outward; }

void BulletSpawner2D::set_helper_circle_face_outward(bool value) {
    helper_circle_face_outward = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_circle_facing_offset_deg() const { return helper_circle_facing_offset_deg; }

void BulletSpawner2D::set_helper_circle_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_circle_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_circle_facing_offset_deg = value;
    on_pattern_changed();
}

Vector2 BulletSpawner2D::get_helper_rectangle_size() const { return helper_rectangle_size; }

void BulletSpawner2D::set_helper_rectangle_size(const Vector2 &value) {
    if (!value.is_finite() || value.x < 0.0 || value.y < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_rectangle_size must be finite with sides >= 0, keeping the old value.");
        return;
    }
    helper_rectangle_size = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_rectangle_face_outward() const { return helper_rectangle_face_outward; }

void BulletSpawner2D::set_helper_rectangle_face_outward(bool value) {
    helper_rectangle_face_outward = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_rectangle_facing_offset_deg() const { return helper_rectangle_facing_offset_deg; }

void BulletSpawner2D::set_helper_rectangle_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_rectangle_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_rectangle_facing_offset_deg = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_square_size() const { return helper_square_size; }

void BulletSpawner2D::set_helper_square_size(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_square_size must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_square_size = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_square_face_outward() const { return helper_square_face_outward; }

void BulletSpawner2D::set_helper_square_face_outward(bool value) {
    helper_square_face_outward = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_square_facing_offset_deg() const { return helper_square_facing_offset_deg; }

void BulletSpawner2D::set_helper_square_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_square_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_square_facing_offset_deg = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_polygon_vertices() const { return helper_polygon_vertices; }

void BulletSpawner2D::set_helper_polygon_vertices(int value) {
    if (value < 3 || value > kMaxBulletsPerVolley) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_polygon_vertices must be between 3 and " + itos(kMaxBulletsPerVolley) + ", keeping the old value.");
        return;
    }
    helper_polygon_vertices = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_polygon_radius() const { return helper_polygon_radius; }

void BulletSpawner2D::set_helper_polygon_radius(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_polygon_radius must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_polygon_radius = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_polygon_rotation() const { return helper_polygon_rotation; }

void BulletSpawner2D::set_helper_polygon_rotation(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_polygon_rotation must be finite, keeping the old value.");
        return;
    }
    helper_polygon_rotation = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_polygon_face_outward() const { return helper_polygon_face_outward; }

void BulletSpawner2D::set_helper_polygon_face_outward(bool value) {
    helper_polygon_face_outward = value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_polygon_facing_offset_deg() const { return helper_polygon_facing_offset_deg; }

void BulletSpawner2D::set_helper_polygon_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_polygon_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_polygon_facing_offset_deg = value;
    on_pattern_changed();
}

NodePath BulletSpawner2D::get_helper_path2d_path() const { return helper_path2d_path; }

void BulletSpawner2D::set_helper_path2d_path(const NodePath &p_path) {
    if (!node_path_type_ok<Path2D>(this, p_path, "helper_path2d_path", "Path2D")) {
        return;
    }
    on_config_changed();
    helper_path2d_path = p_path;
    helper_path2d_cache = nullptr;
    helper_path2d_id = 0;
    if (!helper_path2d_path.is_empty() && is_inside_tree()) {
        Node *node = get_node_or_null(helper_path2d_path);
        if (node != nullptr) {
            Node *resolved = resolve_node_path(this, helper_path2d_path, helper_path2d_cache);
            helper_path2d_id = resolved != nullptr ? resolved->get_instance_id() : 0;
            if (resolved == nullptr) helper_path2d_cache = nullptr;
        }
    }
    on_pattern_changed();
}

BulletSpawner2D::Path2DSpace BulletSpawner2D::get_helper_path2d_space() const { return (Path2DSpace)helper_path2d_space; }

void BulletSpawner2D::set_helper_path2d_space(Path2DSpace value) {
    if (value < PATH2D_SPACE_FOLLOW_GENERATOR || value > PATH2D_SPACE_AT_PATH2D) {
        UtilityFunctions::push_error("BulletSpawner2D: invalid helper_path2d_space, keeping the old value.");
        return;
    }
    helper_path2d_space = (int)value;
    on_pattern_changed();
}

Node *BulletSpawner2D::get_helper_path2d_node() const {
    // ObjectDB-first validation: with an empty path or outside the tree the
    // resolve returns the cache untouched, which may dangle after a free.
    return validate_cached_node(this, helper_path2d_path, helper_path2d_cache, helper_path2d_id);
}

void BulletSpawner2D::set_helper_path2d_node(Node *node) {
    on_config_changed();
    assign_node_to_path(this, node, helper_path2d_path, helper_path2d_cache);
    helper_path2d_id = node != nullptr ? node->get_instance_id() : 0;
    if (node == nullptr) helper_path2d_cache = nullptr;
    on_pattern_changed();
}

BulletSpawner2D::Path2DDistribution BulletSpawner2D::get_helper_path2d_distribution() const { return (Path2DDistribution)helper_path2d_distribution; }

void BulletSpawner2D::set_helper_path2d_distribution(Path2DDistribution value) {
    if (value < PATH2D_DISTRIBUTION_FIXED_SPACING || value > PATH2D_DISTRIBUTION_EVEN) {
        UtilityFunctions::push_error("BulletSpawner2D: invalid helper_path2d_distribution, keeping the old value.");
        return;
    }
    helper_path2d_distribution = (int)value;
    // Spacing/anchor/overflow visibility depends on this mode.
    notify_property_list_changed();
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_path2d_spacing() const { return helper_path2d_spacing; }

void BulletSpawner2D::set_helper_path2d_spacing(double value) {
    if (!Math::is_finite(value) || value <= 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_path2d_spacing must be finite and > 0, keeping the old value.");
        return;
    }
    helper_path2d_spacing = value;
    on_pattern_changed();
}

BulletSpawner2D::Path2DOverflow BulletSpawner2D::get_helper_path2d_overflow() const { return (Path2DOverflow)helper_path2d_overflow; }

void BulletSpawner2D::set_helper_path2d_overflow(Path2DOverflow value) {
    if (value < PATH2D_OVERFLOW_CLAMP || value > PATH2D_OVERFLOW_SHRINK_TO_FIT) {
        UtilityFunctions::push_error("BulletSpawner2D: invalid helper_path2d_overflow, keeping the old value.");
        return;
    }
    helper_path2d_overflow = (int)value;
    on_pattern_changed();
}

BulletSpawner2D::Path2DAnchor BulletSpawner2D::get_helper_path2d_anchor() const { return (Path2DAnchor)helper_path2d_anchor; }

void BulletSpawner2D::set_helper_path2d_anchor(Path2DAnchor value) {
    if (value < PATH2D_ANCHOR_START || value > PATH2D_ANCHOR_END) {
        UtilityFunctions::push_error("BulletSpawner2D: invalid helper_path2d_anchor, keeping the old value.");
        return;
    }
    helper_path2d_anchor = (int)value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_path2d_start_offset() const { return helper_path2d_start_offset; }

void BulletSpawner2D::set_helper_path2d_start_offset(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_path2d_start_offset must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_path2d_start_offset = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_path2d_reverse() const { return helper_path2d_reverse; }

void BulletSpawner2D::set_helper_path2d_reverse(bool value) {
    helper_path2d_reverse = value;
    on_pattern_changed();
}

bool BulletSpawner2D::get_helper_path2d_closed() const { return helper_path2d_closed; }

void BulletSpawner2D::set_helper_path2d_closed(bool value) {
    helper_path2d_closed = value;
    on_pattern_changed();
}

BulletSpawner2D::Path2DFacing BulletSpawner2D::get_helper_path2d_facing() const { return (Path2DFacing)helper_path2d_facing; }

void BulletSpawner2D::set_helper_path2d_facing(Path2DFacing value) {
    if (value < PATH2D_FACING_ALONG_PATH || value > PATH2D_FACING_NORMAL_M90) {
        UtilityFunctions::push_error("BulletSpawner2D: invalid helper_path2d_facing, keeping the old value.");
        return;
    }
    helper_path2d_facing = (int)value;
    on_pattern_changed();
}

double BulletSpawner2D::get_helper_path2d_facing_offset_deg() const { return helper_path2d_facing_offset_deg; }

void BulletSpawner2D::set_helper_path2d_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_path2d_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_path2d_facing_offset_deg = value;
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_grid_seed() const {
    return helper_grid_seed;
}

void BulletSpawner2D::set_helper_grid_seed(int value) {
    if (value < 0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_grid_seed must be >= 0 (0 = non-deterministic), keeping the old value.");
        return;
    }
    helper_grid_seed = value;
    // Seeds feed the samplers (and pin the scatter preview), so a
    // changed seed must refresh the gizmo like every other knob.
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_ring_seed() const {
    return helper_ring_seed;
}

void BulletSpawner2D::set_helper_ring_seed(int value) {
    if (value < 0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_ring_seed must be >= 0 (0 = non-deterministic), keeping the old value.");
        return;
    }
    helper_ring_seed = value;
    // Seeds feed the samplers (and pin the scatter preview), so a
    // changed seed must refresh the gizmo like every other knob.
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_fan_seed() const {
    return helper_fan_seed;
}

void BulletSpawner2D::set_helper_fan_seed(int value) {
    if (value < 0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_fan_seed must be >= 0 (0 = non-deterministic), keeping the old value.");
        return;
    }
    helper_fan_seed = value;
    // Seeds feed the samplers (and pin the scatter preview), so a
    // changed seed must refresh the gizmo like every other knob.
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_rain_seed() const {
    return helper_rain_seed;
}

void BulletSpawner2D::set_helper_rain_seed(int value) {
    if (value < 0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_rain_seed must be >= 0 (0 = non-deterministic), keeping the old value.");
        return;
    }
    helper_rain_seed = value;
    // Seeds feed the samplers (and pin the scatter preview), so a
    // changed seed must refresh the gizmo like every other knob.
    on_pattern_changed();
}

int BulletSpawner2D::get_helper_waterfall_seed() const {
    return helper_waterfall_seed;
}

void BulletSpawner2D::set_helper_waterfall_seed(int value) {
    if (value < 0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_waterfall_seed must be >= 0 (0 = non-deterministic), keeping the old value.");
        return;
    }
    helper_waterfall_seed = value;
    // Seeds feed the samplers (and pin the scatter preview), so a
    // changed seed must refresh the gizmo like every other knob.
    on_pattern_changed();
}

void BulletSpawner2D::apply_pattern_preset(int preset) {
    // Out-of-range presets used to fall into `default:` and silently no-op.
    // Fail loud instead so a typo'd sequence entry is visible immediately.
    if (preset < (int)BulletPatterns2D::PATTERN_PRESET_CUSTOM || preset > (int)BulletPatterns2D::PATTERN_PRESET_TERRAIN_CREST) {
        UtilityFunctions::push_error("BulletSpawner2D::apply_pattern_preset: preset out of range, nothing applied.");
        return;
    }
    if (preset == (int)BulletPatterns2D::PATTERN_PRESET_CUSTOM) {
        return; // "no preset": nothing to apply
    }
    // Clean preset: every Bullet Patterns knob (except the Transform subgroup
    // and node/array wiring) and Spin start from their defaults, so the same
    // preset always gives the same pattern. One preview rebuild at the end.
    begin_pattern_batch();
    reset_pattern_knobs_to_defaults();
    // The preset table lives in the patterns module (pattern_presets2d.cpp);
    // ids are range-checked above, so every remaining id writes.
    PatternPresetResult2D applied;
    if (write_preset_knobs(preset, applied)) {
        pattern_source = (PatternSource)applied.source;
    }
    if (applied.spin_enabled) {
        spin_enabled = true;
        spin_speed_deg_per_sec = applied.spin_speed_deg_per_sec;
    }
    notify_property_list_changed();
    on_config_changed();
    on_pattern_changed();
    end_pattern_batch();
    // Presets write members raw (batched: per-write setters would rebuild the
    // preview ~20 times). The one side effect that cannot wait is process
    // state: spin presets must wake _process, or the spin never advances on
    // an otherwise idle spawner. Editor-guarded: the preview owns processing
    // in the editor.
    refresh_process_state_editor_guarded();
}

} // namespace BlastBullets2D
