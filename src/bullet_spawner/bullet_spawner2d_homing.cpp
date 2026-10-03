// BulletSpawner2D (split from bullet_spawner2d.cpp, same class):
// Homing + orbiting: target resolution, live-volley tracking/adoption,
// retargeting and the per-volley steering/orbit configuration.

#include "bullet_spawner/bullet_spawner2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

// Scopes one candidate-pool reuse window (one shot or one retarget pass):
// resolve_homing_targets collects the pool on the first call and only
// re-selects afterwards. Restores the previous state on every exit path.
struct HomingCandidatePass {
    bool &active;
    bool &filled;
    bool saved_active;
    bool saved_filled;
    HomingCandidatePass(bool &p_active, bool &p_filled) :
            active(p_active), filled(p_filled), saved_active(p_active), saved_filled(p_filled) {
        if (!saved_active) {
            active = true;
            filled = false;
        }
    }
    ~HomingCandidatePass() {
        active = saved_active;
        filled = saved_active ? filled : saved_filled;
    }
    HomingCandidatePass(const HomingCandidatePass &) = delete;
    HomingCandidatePass &operator=(const HomingCandidatePass &) = delete;
};

double BulletSpawner2D::get_homing_delay_sec() const { return homing_delay_sec; }

void BulletSpawner2D::set_homing_delay_sec(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: homing_delay_sec must be finite and >= 0, keeping the old value.");
        return;
    }
    homing_delay_sec = value;
}

double BulletSpawner2D::get_homing_duration_sec() const { return homing_duration_sec; }

void BulletSpawner2D::set_homing_duration_sec(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: homing_duration_sec must be finite and >= 0 (0 = infinite), keeping the old value.");
        return;
    }
    homing_duration_sec = value;
}

double BulletSpawner2D::get_homing_lose_range_px() const { return homing_lose_range_px; }

void BulletSpawner2D::set_homing_lose_range_px(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: homing_lose_range_px must be finite and >= 0 (0 = unlimited), keeping the old value.");
        return;
    }
    homing_lose_range_px = value;
}

double BulletSpawner2D::get_homing_fire_arc_deg() const { return homing_fire_arc_deg; }

void BulletSpawner2D::set_homing_fire_arc_deg(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: homing_fire_arc_deg must be finite and >= 0 (0 = omnidirectional), keeping the old value.");
        return;
    }
    homing_fire_arc_deg = value;
}

int BulletSpawner2D::get_homing_random_seed() const {
    return homing_random_seed;
}

void BulletSpawner2D::set_homing_random_seed(int value) {
    if (value < 0) {
        UtilityFunctions::push_error("BulletSpawner2D: homing_random_seed must be >= 0 (0 = non-deterministic), keeping the old value.");
        return;
    }
    homing_random_seed = value;
}

double BulletSpawner2D::get_homing_retarget_phase() const {
    return homing_retarget_phase;
}

void BulletSpawner2D::set_homing_retarget_phase(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: homing_retarget_phase must be finite and >= 0, keeping the old value.");
        return;
    }
    homing_retarget_phase = value;
    // A re-tuned phase takes effect on the pending countdown immediately:
    // the phase IS the pending countdown for an armed-but-waiting pass
    // (mirrors update_homing_process_state's reset arm). Stagger is only
    // flattened when the user edits the phase itself, never by unrelated
    // setters (those snapshot was_active and skip the reset).
    homing_retarget_time_left = value;
}

// HOMING + ORBITING (EASY API)

bool BulletSpawner2D::get_homing_enabled() const {
    return homing_enabled;
}

void BulletSpawner2D::set_homing_enabled(bool value) {
    on_config_changed();
    const bool was_active = homing_retarget_active();
    homing_enabled = value;
    notify_property_list_changed();
    update_homing_process_state(!was_active);
}

BulletSpawner2D::HomingMode BulletSpawner2D::get_homing_mode() const {
    return homing_mode;
}

void BulletSpawner2D::set_homing_mode(HomingMode value) {
    if (value != HOMING_SHARED && value != HOMING_PER_BULLET) {
        UtilityFunctions::push_error("BulletSpawner2D: invalid homing_mode, keeping the old value.");
        return;
    }
    homing_mode = value;
    notify_property_list_changed();
}

BulletSpawner2D::HomingTargetSource BulletSpawner2D::get_homing_target_source() const {
    return homing_target_source;
}

void BulletSpawner2D::set_homing_target_source(HomingTargetSource value) {
    if (value < HOMING_SOURCE_NODE_GROUP || value > HOMING_SOURCE_NODE_CHILDREN) {
        UtilityFunctions::push_error("BulletSpawner2D: invalid homing_target_source, keeping the old value.");
        return;
    }
    homing_target_source = value;
    clear_empty_homing_targets_warning();
    notify_property_list_changed();
}

StringName BulletSpawner2D::get_homing_node_group() const {
    return homing_node_group;
}

void BulletSpawner2D::set_homing_node_group(const StringName &value) {
    homing_node_group = value;
    clear_empty_homing_targets_warning();
}

StringName BulletSpawner2D::get_homing_filter_group() const {
    return homing_filter_group;
}

void BulletSpawner2D::set_homing_filter_group(const StringName &value) {
    homing_filter_group = value;
    clear_empty_homing_targets_warning();
}

BulletSpawner2D::HomingTargetSelection BulletSpawner2D::get_homing_target_selection() const {
    return homing_target_selection;
}

void BulletSpawner2D::set_homing_target_selection(HomingTargetSelection value) {
    if (value < HOMING_SELECT_NEAREST || value > HOMING_SELECT_DISTRIBUTE) {
        UtilityFunctions::push_error("BulletSpawner2D: invalid homing_target_selection, keeping the old value.");
        return;
    }
    homing_target_selection = value;
    notify_property_list_changed();
}

int BulletSpawner2D::get_homing_max_targets() const {
    return homing_max_targets;
}

void BulletSpawner2D::set_homing_max_targets(int value) {
    if (value < 1) {
        UtilityFunctions::push_error("BulletSpawner2D: homing_max_targets must be >= 1, keeping the old value.");
        return;
    }
    // Bounded like helper_bullets_amount: the name scan + NEAREST selection
    // run per volley and per retarget pass, so an unbounded value turns a
    // huge scene into a per-interval hitch.
    if (value > kMaxHomingTargets) {
        UtilityFunctions::push_error("BulletSpawner2D: homing_max_targets must be <= 10000, keeping the old value.");
        return;
    }
    homing_max_targets = value;
}

double BulletSpawner2D::get_homing_max_detection_range() const {
    return homing_max_detection_range;
}

void BulletSpawner2D::set_homing_max_detection_range(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: homing_max_detection_range must be finite and >= 0 (0 is unlimited), keeping the old value.");
        return;
    }
    homing_max_detection_range = value;
    clear_empty_homing_targets_warning();
}

Vector2 BulletSpawner2D::get_homing_global_position() const {
    return homing_global_position;
}

void BulletSpawner2D::set_homing_global_position(const Vector2 &value) {
    if (!value.is_finite()) {
        UtilityFunctions::push_error("BulletSpawner2D: homing_global_position must be finite (NaN/Inf would poison homing), keeping the old value.");
        return;
    }
    homing_global_position = value;
}

NodePath BulletSpawner2D::get_homing_target_path() const {
    return homing_target_path;
}

void BulletSpawner2D::set_homing_target_path(const NodePath &p_path) {
    if (!node_path_type_ok<Node2D>(this, p_path, "homing_target_path", "Node2D")) {
        return;
    }
    homing_target_path = p_path;
    clear_empty_homing_targets_warning();
}

String BulletSpawner2D::get_homing_node_name() const {
    return homing_node_name;
}

void BulletSpawner2D::set_homing_node_name(const String &value) {
    homing_node_name = value;
    clear_empty_homing_targets_warning();
}

BulletSpawner2D::HomingNodeNameMatch BulletSpawner2D::get_homing_node_name_match_mode() const {
    return homing_node_name_match_mode;
}

void BulletSpawner2D::set_homing_node_name_match_mode(HomingNodeNameMatch value) {
    if (value < HOMING_NAME_MATCH_EXACT || value > HOMING_NAME_MATCH_ENDS_WITH) {
        UtilityFunctions::push_error("BulletSpawner2D: invalid homing_node_name_match_mode, keeping the old value.");
        return;
    }
    homing_node_name_match_mode = value;
    clear_empty_homing_targets_warning();
}

bool BulletSpawner2D::get_homing_node_name_case_sensitive() const {
    return homing_node_name_case_sensitive;
}

void BulletSpawner2D::set_homing_node_name_case_sensitive(bool value) {
    homing_node_name_case_sensitive = value;
    clear_empty_homing_targets_warning();
}

NodePath BulletSpawner2D::get_homing_children_parent_path() const {
    return homing_children_parent_path;
}

void BulletSpawner2D::set_homing_children_parent_path(const NodePath &p_path) {
    homing_children_parent_path = p_path;
    clear_empty_homing_targets_warning();
}

bool BulletSpawner2D::get_homing_children_recursive() const {
    return homing_children_recursive;
}

void BulletSpawner2D::set_homing_children_recursive(bool value) {
    homing_children_recursive = value;
    clear_empty_homing_targets_warning();
}

double BulletSpawner2D::get_homing_smoothing() const {
    return homing_smoothing;
}

void BulletSpawner2D::set_homing_smoothing(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: homing_smoothing must be finite and >= 0 (0 snaps instantly), keeping the old value.");
        return;
    }
    homing_smoothing = value;
}

double BulletSpawner2D::get_homing_update_interval() const {
    return homing_update_interval;
}

void BulletSpawner2D::set_homing_update_interval(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: homing_update_interval must be finite and >= 0 (0 refreshes every tick), keeping the old value.");
        return;
    }
    homing_update_interval = value;
}

double BulletSpawner2D::get_homing_distance_before_reached() const {
    return homing_distance_before_reached;
}

void BulletSpawner2D::set_homing_distance_before_reached(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: homing_distance_before_reached must be finite and >= 0, keeping the old value.");
        return;
    }
    homing_distance_before_reached = value;
}

bool BulletSpawner2D::get_homing_take_control_of_texture_rotation() const {
    return homing_take_control_of_texture_rotation;
}

void BulletSpawner2D::set_homing_take_control_of_texture_rotation(bool value) {
    homing_take_control_of_texture_rotation = value;
}

bool BulletSpawner2D::get_homing_auto_pop_after_target_reached() const {
    return homing_auto_pop_after_target_reached;
}

void BulletSpawner2D::set_homing_auto_pop_after_target_reached(bool value) {
    homing_auto_pop_after_target_reached = value;
}

bool BulletSpawner2D::get_homing_per_bullet_smoothing_enabled() const {
    return homing_per_bullet_smoothing_enabled;
}

void BulletSpawner2D::set_homing_per_bullet_smoothing_enabled(bool value) {
    homing_per_bullet_smoothing_enabled = value;
    notify_property_list_changed();
}

double BulletSpawner2D::get_homing_smoothing_start() const {
    return homing_smoothing_start;
}

void BulletSpawner2D::set_homing_smoothing_start(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: homing_smoothing_start must be finite and >= 0, keeping the old value.");
        return;
    }
    homing_smoothing_start = value;
}

double BulletSpawner2D::get_homing_smoothing_step() const {
    return homing_smoothing_step;
}

void BulletSpawner2D::set_homing_smoothing_step(double value) {
    // Any finite step is allowed (even negative): per-bullet results are
    // clamped at 0 when applied, so a downward fan can never poison steering.
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: homing_smoothing_step must be finite, keeping the old value.");
        return;
    }
    homing_smoothing_step = value;
}

BulletSpawner2D::HomingRetargetMode BulletSpawner2D::get_homing_retarget_mode() const {
    return homing_retarget_mode;
}

void BulletSpawner2D::set_homing_retarget_mode(HomingRetargetMode value) {
    if (value != HOMING_RETARGET_OFF && value != HOMING_RETARGET_ON_INTERVAL) {
        UtilityFunctions::push_error("BulletSpawner2D: invalid homing_retarget_mode, keeping the old value.");
        return;
    }
    const bool was_active = homing_retarget_active();
    homing_retarget_mode = value;
    notify_property_list_changed();
    update_homing_process_state(!was_active);
}

double BulletSpawner2D::get_homing_retarget_interval_sec() const {
    return homing_retarget_interval_sec;
}

void BulletSpawner2D::set_homing_retarget_interval_sec(double value) {
    if (!Math::is_finite(value) || !(value > 0.0)) {
        UtilityFunctions::push_error("BulletSpawner2D: homing_retarget_interval_sec must be finite and > 0, keeping the old value.");
        return;
    }
    homing_retarget_interval_sec = value;
    // A longer period than the pending countdown leaves a pass scheduled
    // past the new period: clamp it so the next retarget honors the new
    // interval now instead of one stale (long) wait later. A shorter-or-equal
    // period never touches the pending pass (no stagger flattening).
    if (homing_retarget_active() && homing_retarget_time_left > value) {
        homing_retarget_time_left = value;
    }
}

bool BulletSpawner2D::get_homing_retarget_previous_volleys() const {
    return homing_retarget_previous_volleys;
}

void BulletSpawner2D::set_homing_retarget_previous_volleys(bool value) {
    homing_retarget_previous_volleys = value;
}

bool BulletSpawner2D::get_orbiting_enabled() const {
    return orbiting_enabled;
}

void BulletSpawner2D::set_orbiting_enabled(bool value) {
    on_config_changed();
    orbiting_enabled = value;
    notify_property_list_changed();
    // Orbiting rides on homing targets: toggling it must not leave _process
    // asleep. Editor-guarded: the preview owns processing in the editor.
    if (!Engine::get_singleton()->is_editor_hint() && is_inside_tree()) {
        refresh_process_state();
    }
}

double BulletSpawner2D::get_orbiting_radius() const {
    return orbiting_radius;
}

void BulletSpawner2D::set_orbiting_radius(double value) {
    if (!Math::is_finite(value) || value < 0.01) {
        UtilityFunctions::push_error("BulletSpawner2D: orbiting_radius must be finite and >= 0.01, keeping the old value.");
        return;
    }
    orbiting_radius = value;
}

BulletVolley2D::OrbitingDirection BulletSpawner2D::get_orbiting_direction() const {
    return orbiting_direction;
}

void BulletSpawner2D::set_orbiting_direction(BulletVolley2D::OrbitingDirection value) {
    if (value != BulletVolley2D::DontMove && value != BulletVolley2D::OrbitLeft && value != BulletVolley2D::OrbitRight && value != BulletVolley2D::OrbitRandom) {
        UtilityFunctions::push_error("BulletSpawner2D: invalid orbiting_direction, keeping the old value.");
        return;
    }
    orbiting_direction = value;
}

BulletVolley2D::OrbitingTextureRotation BulletSpawner2D::get_orbiting_texture_rotation() const {
    return orbiting_texture_rotation;
}

void BulletSpawner2D::set_orbiting_texture_rotation(BulletVolley2D::OrbitingTextureRotation value) {
    if (value < BulletVolley2D::FaceTarget || value > BulletVolley2D::FaceOppositeOrbitingDirection) {
        UtilityFunctions::push_error("BulletSpawner2D: invalid orbiting_texture_rotation, keeping the old value.");
        return;
    }
    orbiting_texture_rotation = value;
}

bool BulletSpawner2D::get_orbiting_radius_linear_enabled() const {
    return orbiting_radius_linear_enabled;
}

void BulletSpawner2D::set_orbiting_radius_linear_enabled(bool value) {
    orbiting_radius_linear_enabled = value;
    notify_property_list_changed();
}

double BulletSpawner2D::get_orbiting_radius_start() const {
    return orbiting_radius_start;
}

void BulletSpawner2D::set_orbiting_radius_start(double value) {
    if (!Math::is_finite(value) || value < 0.01) {
        UtilityFunctions::push_error("BulletSpawner2D: orbiting_radius_start must be finite and >= 0.01, keeping the old value.");
        return;
    }
    orbiting_radius_start = value;
}

double BulletSpawner2D::get_orbiting_radius_step() const {
    return orbiting_radius_step;
}

void BulletSpawner2D::set_orbiting_radius_step(double value) {
    // Any finite step is allowed (even negative): per-bullet results are
    // clamped at 0.01 when applied, so a shrinking fan stays valid.
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: orbiting_radius_step must be finite, keeping the old value.");
        return;
    }
    orbiting_radius_step = value;
}

BulletVolley2D::OrbitingFollowMode BulletSpawner2D::get_orbiting_follow_mode() const {
    return orbiting_follow_mode;
}

void BulletSpawner2D::set_orbiting_follow_mode(BulletVolley2D::OrbitingFollowMode value) {
    if (value != BulletVolley2D::FollowTarget && value != BulletVolley2D::FollowDeadzone && value != BulletVolley2D::Anchored) {
        UtilityFunctions::push_error("BulletSpawner2D: invalid orbiting_follow_mode, keeping the old value.");
        return;
    }
    orbiting_follow_mode = value;
    notify_property_list_changed();
}

double BulletSpawner2D::get_orbiting_follow_deadzone() const {
    return orbiting_follow_deadzone;
}

void BulletSpawner2D::set_orbiting_follow_deadzone(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: orbiting_follow_deadzone must be finite and >= 0, keeping the old value.");
        return;
    }
    orbiting_follow_deadzone = value;
}

BulletVolley2D::OrbitingLockPolicy BulletSpawner2D::get_orbiting_lock_policy() const {
    return orbiting_lock_policy;
}

void BulletSpawner2D::set_orbiting_lock_policy(BulletVolley2D::OrbitingLockPolicy value) {
    if (value != BulletVolley2D::RelockAlways && value != BulletVolley2D::StayLocked && value != BulletVolley2D::RelockOnTargetChange) {
        UtilityFunctions::push_error("BulletSpawner2D: invalid orbiting_lock_policy, keeping the old value.");
        return;
    }
    orbiting_lock_policy = value;
}

bool BulletSpawner2D::get_orbiting_rigid_follow() const {
    return orbiting_rigid_follow;
}

void BulletSpawner2D::set_orbiting_rigid_follow(bool value) {
    orbiting_rigid_follow = value;
}

bool BulletSpawner2D::homing_retarget_active() const {
    return homing_enabled && homing_retarget_mode == HOMING_RETARGET_ON_INTERVAL && !Engine::get_singleton()->is_editor_hint() && is_inside_tree();
}

void BulletSpawner2D::update_homing_process_state(bool reset_countdown) {
    if (homing_retarget_active()) {
        // Phase-stagger the first pass so N spawners don't scene-scan together;
        // zero phase stays due-now for immediate refresh. Reset only when
        // retargeting just armed: re-running it on every setter call would
        // flatten the stagger phases of already-running spawners.
        if (reset_countdown) {
            arm_retarget_countdown();
        }
        set_process(true);
    } else {
        // Sleep when nothing needs the loop. Mirrors the shooting setters so
        // disabling retarget can actually stop _process.
        refresh_process_state_editor_guarded();
    }
}

void BulletSpawner2D::collect_homing_candidates_by_name(Node *p_node, Array &r_candidates) const {
    if (p_node == nullptr) {
        return;
    }
    // Explicit stack instead of recursion: scene trees can be arbitrarily
    // deep and this runs per volley plus per retarget pass. Children are
    // pushed in reverse so they pop in tree order (FIRST selection stays
    // deterministic).
    homing_scan_stack.clear();
    Array &stack = homing_scan_stack;
    stack.push_back(p_node);
    // Pattern folded once per scan, not once per visited node.
    const String pattern = homing_node_name_case_sensitive ? homing_node_name : homing_node_name.to_lower();
    while (!stack.is_empty()) {
        Node *node = Object::cast_to<Node>(stack.pop_back());
        if (node == nullptr) {
            continue;
        }
        // Never descend into a bullet factory: its containers hold every
        // volley, attachment and effect shard (thousands of nodes in a busy
        // scene), none of which is a homing target, and walking them made
        // every name scan scale with the live bullet count.
        if (Object::cast_to<BulletFactory2D>(node) != nullptr) {
            continue;
        }
        // Never this spawner or anything under it (its pattern markers).
        if (node == this) {
            continue;
        }
        Node2D *as_2d = Object::cast_to<Node2D>(node);
        // Never chase ourselves or our own markers: the spawner (and any Node2D
        // markers under it) would otherwise match a broad pattern like "Node2D".
        // Never the preview holder either: it is visualization only (same
        // exclusion as the children spawn markers in the collect path).
        if (as_2d != nullptr && !as_2d->is_queued_for_deletion() && !as_2d->has_meta(PREVIEW_META_KEY)) {
            String node_name = String(as_2d->get_name());
            if (!homing_node_name_case_sensitive) {
                node_name = node_name.to_lower();
            }
            bool match = false;
            switch (homing_node_name_match_mode) {
                case HOMING_NAME_MATCH_CONTAINS:
                    match = node_name.contains(pattern);
                    break;
                case HOMING_NAME_MATCH_STARTS_WITH:
                    match = node_name.begins_with(pattern);
                    break;
                case HOMING_NAME_MATCH_ENDS_WITH:
                    match = node_name.ends_with(pattern);
                    break;
                case HOMING_NAME_MATCH_EXACT:
                default:
                    match = node_name == pattern;
                    break;
            }
            if (match && (homing_filter_group.is_empty() || as_2d->is_in_group(homing_filter_group))) {
                r_candidates.push_back(as_2d);
            }
        }
        for (int i = node->get_child_count() - 1; i >= 0; --i) {
            stack.push_back(node->get_child(i));
        }
    }
}

void BulletSpawner2D::collect_homing_candidates_from_children(Node *p_parent, bool recursive, Array &r_candidates) const {
    if (p_parent == nullptr) {
        return;
    }
    // Explicit stack instead of recursion (same reason as
    // collect_homing_candidates_by_name): trees can be arbitrarily deep and
    // this runs per volley plus per retarget pass. Reverse-push keeps
    // depth-first pre-order identical to the old recursive walk.
    homing_scan_stack.clear();
    Array &stack = homing_scan_stack;
    for (int i = p_parent->get_child_count() - 1; i >= 0; --i) {
        stack.push_back(p_parent->get_child(i));
    }
    while (!stack.is_empty()) {
        Node *child = Object::cast_to<Node>(stack.pop_back());
        if (child == nullptr) {
            continue;
        }
        Node2D *as_2d = Object::cast_to<Node2D>(child);
        // Never the spawner itself (a parent pointing at our own node would
        // otherwise make the volley chase its emitter).
        // Same preview-holder exclusion as the name scan above: the gizmo
        // must never become a homing target.
        if (as_2d != nullptr && as_2d != this && !as_2d->is_queued_for_deletion() && !as_2d->has_meta(PREVIEW_META_KEY)) {
            if (homing_filter_group.is_empty() || as_2d->is_in_group(homing_filter_group)) {
                r_candidates.push_back(as_2d);
            }
        }
        if (recursive) {
            for (int i = child->get_child_count() - 1; i >= 0; --i) {
                stack.push_back(child->get_child(i));
            }
        }
    }
}

void BulletSpawner2D::warn_empty_homing_targets_once(const String &message, bool quiet) const {
    if (quiet || homing_empty_targets_warned) {
        return;
    }
    homing_empty_targets_warned = true;
    UtilityFunctions::push_warning(message);
}

void BulletSpawner2D::clear_empty_homing_targets_warning() const {
    homing_empty_targets_warned = false;
}

Array BulletSpawner2D::resolve_homing_targets(bool quiet, bool advance_round_robin) const {
    Array targets;
    if (!is_inside_tree()) {
        if (!quiet) {
            UtilityFunctions::push_error("BulletSpawner2D::resolve_homing_targets: spawner is outside the scene tree, no targets resolved.");
        }
        return targets;
    }
    // The cursor is consumed live (never resolved): pushes refresh it
    // themselves, so there is nothing to snapshot here.
    if (homing_target_source == HOMING_SOURCE_MOUSE) {
        return targets;
    }
    if (homing_target_source == HOMING_SOURCE_GLOBAL_POSITION) {
        if (!homing_global_position.is_finite()) {
            if (!quiet) {
                UtilityFunctions::push_error("BulletSpawner2D::resolve_homing_targets: homing_global_position is not finite, no targets resolved.");
            }
            return targets;
        }
        targets.push_back(homing_global_position);
        return targets;
    }
    if (homing_target_source == HOMING_SOURCE_NODE_PATH) {
        Node2D *target = Object::cast_to<Node2D>(get_node_or_null(homing_target_path));
        // A node queued for deletion dies at the end of this frame: chasing
        // it would only hand the volley a dead target.
        if (target == nullptr || target->is_queued_for_deletion()) {
            warn_empty_homing_targets_once("BulletSpawner2D::resolve_homing_targets: homing_target_path does not point at a live Node2D, volley flies without homing.", quiet);
            return targets;
        }
        if (!homing_filter_group.is_empty() && !target->is_in_group(homing_filter_group)) {
            warn_empty_homing_targets_once("BulletSpawner2D::resolve_homing_targets: homing_target_path node is not in homing_filter_group, volley flies without homing.", quiet);
            return targets;
        }
        targets.push_back(target);
        return targets;
    }
    // Multi-target sources (group, name, children) share one tail below
    // (range cull, empty check, selection): each branch only fills
    // `candidates` (member scratch: cleared here, consumed below).
    Array &candidates = homing_candidates_scratch;
    // Inside one shot / one retarget pass the candidate pool cannot change
    // in a way that matters (no user code runs between the resolves), so
    // collect it once and only re-run the per-volley selection below. The
    // group poll / scene scan was paid per volley before.
    const bool reuse_pass_pool = homing_candidate_pass_active && homing_candidate_pass_filled;
    if (reuse_pass_pool) {
        // candidates already hold this pass's range-culled pool.
    } else {
    homing_candidates_scratch.clear();
    if (homing_target_source == HOMING_SOURCE_NODE_CHILDREN) {
        if (homing_children_parent_path.is_empty()) {
            warn_empty_homing_targets_once("BulletSpawner2D::resolve_homing_targets: Node Children source needs homing_children_parent_path, volley flies without homing.", quiet);
            return targets;
        }
        Node *parent = get_node_or_null(homing_children_parent_path);
        if (parent == nullptr) {
            warn_empty_homing_targets_once("BulletSpawner2D::resolve_homing_targets: homing_children_parent_path does not point at a live node, volley flies without homing.", quiet);
            return targets;
        }
        collect_homing_candidates_from_children(parent, homing_children_recursive, candidates);
    } else {
        // HOMING_SOURCE_NODE_GROUP: poll the tree, keep live Node2Ds (plus
        // the optional allow-list), then pick up to homing_max_targets.
        SceneTree *tree = get_tree();
        if (tree == nullptr) {
            if (!quiet) {
                UtilityFunctions::push_error("BulletSpawner2D::resolve_homing_targets: no SceneTree, no targets resolved.");
            }
            return targets;
        }
        if (homing_target_source == HOMING_SOURCE_NODE_NAME) {
            if (homing_node_name.is_empty()) {
                warn_empty_homing_targets_once("BulletSpawner2D::resolve_homing_targets: homing_node_name is empty, volley flies without homing.", quiet);
                return targets;
            }
            // Scan from the scene root: dynamically spawned enemies are found
            // without any group setup. current_scene may be null (autoload-only
            // setups), the viewport root always exists instead.
            Node *scan_root = tree->get_current_scene();
            if (scan_root == nullptr) {
                scan_root = tree->get_root();
            }
            if (scan_root != nullptr) {
                collect_homing_candidates_by_name(scan_root, candidates);
            }
        } else {
            TypedArray<Node> members = tree->get_nodes_in_group(homing_node_group);
            for (int i = 0; i < members.size(); ++i) {
                Node2D *candidate = Object::cast_to<Node2D>(members[i]);
                if (candidate == nullptr) {
                    continue; // group may hold anything: only Node2Ds can be chased
                }
                // Never chase ourselves (an enemy turret in the enemies group)
                // or a node that dies at the end of this frame.
                if (candidate == this || candidate->is_queued_for_deletion()) {
                    continue;
                }
                if (!homing_filter_group.is_empty() && !candidate->is_in_group(homing_filter_group)) {
                    continue;
                }
                candidates.push_back(candidate);
            }
        }
    }
    } // !reuse_pass_pool
    // Detection range culls all multi-target sources around the spawner.
    // Range and NEAREST measure from the effective generator (the volley's
    // actual muzzle), same as the fire-arc gate: with an external
    // transforms_generator the spawner node can sit far from where bullets
    // spawn.
    Node2D *muzzle = get_effective_generator();
    const Vector2 muzzle_origin = muzzle != nullptr ? muzzle->get_global_position() : get_global_position();
    if (homing_max_detection_range > 0.0 && !candidates.is_empty()) {
        const Vector2 origin = muzzle_origin;
        const real_t max_dist_sq = (real_t)(homing_max_detection_range * homing_max_detection_range);
        Array in_range;
        for (int i = 0; i < candidates.size(); ++i) {
            Node2D *candidate = Object::cast_to<Node2D>(candidates[i]);
            if (candidate != nullptr && candidate->get_global_position().distance_squared_to(origin) <= max_dist_sq) {
                in_range.push_back(candidate);
            }
        }
        candidates = in_range;
    }
    if (homing_candidate_pass_active) {
        homing_candidate_pass_filled = true;
    }
    if (candidates.is_empty()) {
        if (homing_target_source == HOMING_SOURCE_NODE_NAME) {
            warn_empty_homing_targets_once(String("BulletSpawner2D::resolve_homing_targets: no live Node2D named like '") + homing_node_name + "' found, volley flies without homing.", quiet);
        } else if (homing_target_source == HOMING_SOURCE_NODE_CHILDREN) {
            warn_empty_homing_targets_once("BulletSpawner2D::resolve_homing_targets: parent has no live Node2D children to chase, volley flies without homing.", quiet);
        } else {
            warn_empty_homing_targets_once(String("BulletSpawner2D::resolve_homing_targets: no live Node2D found in group '") + String(homing_node_group) + "', volley flies without homing.", quiet);
        }
        return targets;
    }
    // The deque caps at 256 targets per queue (see HomingTargetDeque): clamp
    // the take there too, otherwise a huge max_targets fans thousands of
    // rejected pushes (one error each) every volley and every retarget pass.
    // DISTRIBUTE is unaffected (exactly one target per bullet).
    const int take = MIN(MIN(homing_max_targets, (int)candidates.size()), kMaxHomingDequeTargets);
    // DISTRIBUTE deals one target per bullet across the volley (cycling), so
    // the resolution order here does not matter: spawn and retarget build
    // the deal with i % pool themselves. NEAREST order is returned, same as
    // the default selection.
    //
    // With homing_max_targets == 1 (the DEFAULT) the pool is a single target,
    // so the deal `pool[i % pool.size()]` is `pool[0]` for EVERY bullet and
    // DISTRIBUTE degenerates to HOMING_SHARED - no spreading at all. That is
    // a silent no-op the user cannot see, so warn once. Deliberately NOT
    // auto-bumped: silently changing the pool size would alter targeting for
    // everyone who set DISTRIBUTE with the default.
    if (homing_target_selection == HOMING_SELECT_DISTRIBUTE && homing_max_targets < 2) {
        if (!homing_distribute_degenerate_warned) {
            homing_distribute_degenerate_warned = true;
            UtilityFunctions::push_warning("BulletSpawner2D: homing_target_selection is DISTRIBUTE but homing_max_targets is " + String::num_int64(homing_max_targets) + ", so every bullet chases the same target (identical to SHARED). Raise homing_max_targets to 2+ to spread targets across the volley.");
        }
    } else if (homing_target_selection != HOMING_SELECT_DISTRIBUTE) {
        // Re-arm so switching back into DISTRIBUTE warns again.
        homing_distribute_degenerate_warned = false;
    }
    switch (homing_target_selection) {
        case HOMING_SELECT_FIRST: {
            for (int k = 0; k < take; ++k) {
                targets.push_back(candidates[k]);
            }
            break;
        }
        case HOMING_SELECT_RANDOM: {
            if (homing_rng.is_null()) {
                homing_rng.instantiate();
                if (homing_random_seed > 0) {
                    homing_rng->set_seed((uint64_t)homing_random_seed);
                } else {
                    homing_rng->randomize();
                }
            } else if (homing_random_seed > 0 && homing_rng->get_seed() != (uint64_t)homing_random_seed) {
                // Seed changed at runtime: re-seed so the new value takes effect.
                homing_rng->set_seed((uint64_t)homing_random_seed);
            }
            homing_pool_scratch = candidates.duplicate();
            Array &pool = homing_pool_scratch;
            for (int k = 0; k < take && !pool.is_empty(); ++k) {
                const int idx = (int)homing_rng->randi_range(0, pool.size() - 1);
                targets.push_back(pool[idx]);
                pool.remove_at(idx);
            }
            break;
        }
        case HOMING_SELECT_ROUND_ROBIN: {
            const int n = (int)candidates.size();
            homing_round_robin_last_count = n;
            for (int k = 0; k < take; ++k) {
                targets.push_back(candidates[(homing_round_robin_cursor + k) % n]);
            }
            // Retarget passes peek without consuming: flying volleys keep
            // stable targets while new volleys keep rotating.
            if (advance_round_robin) {
                homing_round_robin_cursor = (homing_round_robin_cursor + take) % n;
            }
            break;
        }
        case HOMING_SELECT_NEAREST:
        default: {
            // Repeated min-extraction: take is tiny (usually 1), so O(n*k)
            // beats sorting and needs no extra includes.
            const Vector2 origin = muzzle_origin;
            homing_pool_scratch = candidates.duplicate();
            Array &pool = homing_pool_scratch;
            for (int k = 0; k < take && !pool.is_empty(); ++k) {
                int best = -1;
                real_t best_dist = 0.0;
                for (int j = 0; j < pool.size(); ++j) {
                    // Defensive: only Node2Ds ever enter candidates, but a
                    // null here must skip, never dereference.
                    Node2D *node = Object::cast_to<Node2D>(pool[j]);
                    if (node == nullptr) {
                        continue;
                    }
                    const real_t d = node->get_global_position().distance_squared_to(origin);
                    if (best < 0 || d < best_dist) {
                        best_dist = d;
                        best = j;
                    }
                }
                if (best < 0) {
                    break; // pool held no live Node2D after all
                }
                targets.push_back(pool[best]);
                pool.remove_at(best);
            }
            break;
        }
    }
    return targets;
}

void BulletSpawner2D::track_live_volley(BulletVolley2D *bullets) {
    volley_tracker.track(bullets, get_instance_id());
}

int BulletSpawner2D::get_live_volley_count() const {
    return volley_tracker.count(get_instance_id());
}

void BulletSpawner2D::clear_live_volleys() {
    volley_tracker.clear();
    // Forget targeting rotation too: a manual clear means "forget everything",
    // so the next wave restarts round-robin from the top instead of resuming
    // mid-rotation from volleys that no longer exist.
    homing_round_robin_cursor = 0;
}

Array BulletSpawner2D::get_live_volleys() const {
    // Snapshot prunes first; resolve_live re-checks per id so a volley freed
    // between the two can never be handed out for direct engine calls.
    const PackedInt64Array ids = volley_tracker.snapshot(get_instance_id());
    Array out;
    for (int i = 0; i < ids.size(); ++i) {
        BulletVolley2D *volley = VolleyTracker2D::resolve_live(ids[i], get_instance_id());
        if (volley != nullptr) {
            out.push_back(volley);
        }
    }
    return out;
}

bool BulletSpawner2D::adopt_live_volley(BulletVolley2D *bullets) {
    if (bullets == nullptr) {
        UtilityFunctions::push_error("BulletSpawner2D::adopt_live_volley: instance is null.");
        return false;
    }
    if (!bullets->is_active || !bullets->is_inside_tree()) {
        UtilityFunctions::push_error("BulletSpawner2D::adopt_live_volley: instance is not live (pooled or outside the tree). Wake it first.");
        return false;
    }
    // Same liveness bar as the shoot-path revalidation: adopting a volley
    // that is queued for deletion would stamp, hook, and track an instance
    // that dies at the end of the frame.
    if (bullets->is_queued_for_deletion()) {
        UtilityFunctions::push_error("BulletSpawner2D::adopt_live_volley: instance is queued for deletion.");
        return false;
    }
    // Takes over a manually-woken volley (manual wake detaches the previous
    // owner): stamps, hooks the forwarder, tracks. Queues are left alone.
    // Only the previous spawner's forwarder is dropped: user handlers
    // connected directly on the volley (bullet_homing_target_reached) belong
    // to the game, not to the old spawner, and must survive the handover.
    bullets->owner_spawner_id = get_instance_id();
    for (const Dictionary &connection : bullets->get_signal_connection_list("bullet_homing_target_reached")) {
        const Callable callable = connection["callable"];
        const Object *target = callable.get_object();
        if (target != nullptr && Object::cast_to<BulletSpawner2D>(target) != nullptr && callable.get_method() == StringName("_on_volley_bullet_homing_target_reached")) {
            bullets->disconnect("bullet_homing_target_reached", callable);
        }
    }
    const Callable forward_callable(this, "_on_volley_bullet_homing_target_reached");
    if (!bullets->is_connected("bullet_homing_target_reached", forward_callable)) {
        bullets->connect("bullet_homing_target_reached", forward_callable);
    }
    track_live_volley(bullets);
    return true;
}

int BulletSpawner2D::clear_live_volleys_homing() {
    const PackedInt64Array ids = volley_tracker.snapshot(get_instance_id());
    int done = 0;
    const uint64_t self_id = get_instance_id();
    for (int i = 0; i < ids.size(); ++i) {
        BulletVolley2D *volley = VolleyTracker2D::resolve_live(ids[i], self_id);
        if (volley == nullptr) {
            continue;
        }
        // Engine clears keep every counter exact; orbit is stopped with the
        // same per-bullet guard the retarget path uses (disable warns when
        // already off).
        volley->shared_homing_deque_clear_homing_targets();
        volley->all_bullets_clear_homing_targets();
        const int bullet_count = volley->get_amount_bullets();
        for (int b = 0; b < bullet_count; ++b) {
            if (volley->bullet_is_orbiting_enabled(b)) {
                volley->bullet_disable_orbiting(b);
            }
        }
        ++done;
    }
    return done;
}

int BulletSpawner2D::override_live_volleys_velocity(const Vector2 &new_velocity) {
    if (!new_velocity.is_finite()) {
        UtilityFunctions::push_error("BulletSpawner2D::override_live_volleys_velocity: velocity must be finite.");
        return 0;
    }
    const PackedInt64Array ids = volley_tracker.snapshot(get_instance_id());
    int done = 0;
    const uint64_t self_id = get_instance_id();
    for (int i = 0; i < ids.size(); ++i) {
        BulletVolley2D *volley = VolleyTracker2D::resolve_live(ids[i], self_id);
        if (volley == nullptr) {
            continue;
        }
        // Engine-owned semantics: a speed curve overrides direct velocity
        // (warns per bullet and no-ops), so this visibly works only for
        // curve-free volleys.
        volley->all_bullets_set_velocity(new_velocity);
        ++done;
    }
    return done;
}

int BulletSpawner2D::retarget_live_volleys() {
    // Snapshot prunes first; resolve is the expensive part (group poll /
    // scene scan), so skip it when no live volley could consume the result.
    const PackedInt64Array tracked_ids = volley_tracker.snapshot(get_instance_id());
    if (!homing_enabled || !is_inside_tree() || tracked_ids.is_empty()) {
        return 0;
    }
    // One candidate pool for the whole pass; per-volley modes (random,
    // round-robin, distribute) only re-run the selection.
    HomingCandidatePass candidate_pass(homing_candidate_pass_active, homing_candidate_pass_filled);
    const bool use_mouse = homing_target_source == HOMING_SOURCE_MOUSE;
    // RANDOM rolls, ROUND_ROBIN rotates, and DISTRIBUTE deals per volley at
    // spawn time; a single shared array would hand every volley an identical
    // queue, so those modes re-resolve inside the loop. Round-robin peeks
    // without consuming the cursor: flying volleys keep stable targets while
    // new volleys rotate.
    const bool per_volley_resolve = !use_mouse && (homing_target_selection == HOMING_SELECT_RANDOM || homing_target_selection == HOMING_SELECT_ROUND_ROBIN || homing_target_selection == HOMING_SELECT_DISTRIBUTE);
    Array shared_targets;
    if (!use_mouse && !per_volley_resolve) {
        // Quiet: an empty group mid-flight keeps the old queues instead of
        // wiping them with a warning every interval.
        shared_targets = resolve_homing_targets(true, false);
        if (shared_targets.is_empty()) {
            return 0;
        }
    }
    // Newest tracked volley is last (spawn order preserved by prune).
    const int loop_start = homing_retarget_previous_volleys ? 0 : (int)tracked_ids.size() - 1;
    int done = 0;
    const uint64_t self_id = get_instance_id();
    BulletFactory2D *factory = get_bullet_factory();
    for (int i = loop_start; i < (int)tracked_ids.size(); ++i) {
        BulletVolley2D *volley = VolleyTracker2D::resolve_live(tracked_ids[i], self_id);
        if (volley == nullptr) {
            continue;
        }
        // Factory-scoped retargeting: after set_bullet_factory() points
        // elsewhere, old-factory volleys keep their last steering instead of
        // being re-aimed by a spawner that no longer spawns into their world.
        // Matches the max_live_bullets fuse, which counts the current factory
        // only — ownership alone would steer volleys the budget can't see.
        if (volley->bullet_factory != factory) {
            continue;
        }
        Array volley_targets = shared_targets;
        if (per_volley_resolve) {
            // The resolve below runs AFTER the volley was validated above: a
            // handler could adopt/free this volley mid-loop. Capture
            // the id and re-resolve afterwards; a changed volley is skipped
            // instead of steered through a stale pointer.
            const uint64_t volley_id = volley->get_instance_id();
            volley_targets = resolve_homing_targets(true, false);
            if (volley_targets.is_empty()) {
                continue;
            }
            volley = VolleyTracker2D::resolve_live((int64_t)volley_id, self_id);
            if (volley == nullptr) {
                continue;
            }
        }
        // Skip partially-disabled volleys for the queue rewrite below: the
        // tick only trims active bullets, so pushing targets onto disabled
        // slots inflates homing counters and leaks mouse targets that never
        // drain. Fully-disabled volleys are already pruned above; this
        // covers the partial case (some bullets dead, volley still active).
        {
            bool any_enabled = false;
            const int bullet_count = volley->get_amount_bullets();
            for (int b = 0; b < bullet_count; ++b) {
                if (volley->is_bullet_status_enabled(b)) {
                    any_enabled = true;
                    break;
                }
            }
            if (!any_enabled) {
                continue;
            }
        }
        // Re-apply live tuning: without this, changing smoothing, interval,
        // reached distance, auto-pop, or orbit settings mid-fight would only
        // ever affect future volleys.
        apply_steering_to_volley(volley);
        if (homing_mode == HOMING_SHARED) {
            if (use_mouse) {
                volley->shared_homing_deque_clear_homing_targets();
                volley->shared_homing_deque_push_back_mouse_position_target();
            } else {
                volley->shared_homing_deque_replace_homing_targets_with_new_target_array(volley_targets);
            }
        } else {
            if (use_mouse) {
                volley->all_bullets_replace_homing_targets_with_mouse();
            } else if (homing_target_selection == HOMING_SELECT_DISTRIBUTE) {
                // Re-deal like at spawn: per-bullet replace (not clear +
                // assign) so locked rings survive per the lock policy instead
                // of unlocking on every pass. Skips disabled slots silently.
                // Own index name (not the outer volley-loop i it shadows):
                // shadowing compiles today but invites a real bug on the
                // next edit touching either loop.
                const int bullet_count = volley->get_amount_bullets();
                for (int bi = 0; bi < bullet_count; ++bi) {
                    if (!volley->is_bullet_status_enabled(bi)) {
                        continue;
                    }
                    volley->bullet_replace_homing_targets_with_new_target(bi, volley_targets[bi % volley_targets.size()]);
                }
            } else {
                volley->all_bullets_replace_homing_targets_with_new_target_array(volley_targets);
            }
        }
        if (orbiting_enabled && homing_enabled) {
            apply_orbiting_to_volley(volley);
        } else if (homing_enabled) {
            // Toggle switched off after spawn: stop live rings instead of
            // orbiting forever. Guarded per bullet: disabling an already
            // disabled orbit warns, and this runs every pass.
            const int bullet_count = volley->get_amount_bullets();
            for (int i = 0; i < bullet_count; ++i) {
                if (volley->bullet_is_orbiting_enabled(i)) {
                    volley->bullet_disable_orbiting(i);
                }
            }
        }
        ++done;
    }
    return done;
}

void BulletSpawner2D::_on_volley_bullet_homing_target_reached(Object *volley, int bullet_index, Object *target, const Vector2 &target_global_position) {
    emit_signal("volley_bullet_homing_target_reached", volley, bullet_index, target, target_global_position);
}

void BulletSpawner2D::apply_steering_to_volley(BulletVolley2D *volley) const {
    volley->set_homing_smoothing((real_t)homing_smoothing);
    volley->set_homing_update_interval((real_t)homing_update_interval);
    volley->set_homing_distance_before_reached((real_t)homing_distance_before_reached);
    volley->set_homing_take_control_of_texture_rotation(homing_take_control_of_texture_rotation);
    // One switch for both queue kinds: the volley only reads the one that
    // matches its homing mode.
    volley->set_bullet_homing_auto_pop_after_target_reached(homing_auto_pop_after_target_reached);
    volley->set_shared_homing_deque_auto_pop_after_target_reached(homing_auto_pop_after_target_reached);
    volley->set_homing_delay_sec((real_t)homing_delay_sec);
    volley->set_homing_duration_sec((real_t)homing_duration_sec);
    volley->set_homing_lose_range_px((real_t)homing_lose_range_px);
    if (homing_per_bullet_smoothing_enabled) {
        const int bullet_count = volley->get_amount_bullets();
        for (int i = 0; i < bullet_count; ++i) {
            // A negative step fans downward: clamp per bullet so the
            // engine setter never sees an invalid value.
            const double per_bullet = MAX(homing_smoothing_start + homing_smoothing_step * i, 0.0);
            volley->bullet_set_homing_smoothing(i, (real_t)per_bullet);
        }
    }
}

void BulletSpawner2D::apply_orbiting_to_volley(BulletVolley2D *volley) const {
    // No direction is skipped: DontMove = escort (fixed ring slot, follows the
    // target without circling), armed through the same engine path below.
    // Disabled bullets are skipped: they own no orbit state (disable clears
    // it), and enabling there would inflate the orbiting counter for a slot
    // the tick never moves.
    const int bullet_count = volley->get_amount_bullets();
    for (int i = 0; i < bullet_count; ++i) {
        if (!volley->is_bullet_status_enabled(i)) {
            continue;
        }
        // Negative linear steps fan downward: clamp per bullet so the
        // engine never sees an invalid radius (it errors per bullet).
        const double radius = orbiting_radius_linear_enabled
                ? MAX(orbiting_radius_start + orbiting_radius_step * i, 0.01)
                : orbiting_radius;
        if (!volley->bullet_is_orbiting_enabled(i)) {
            volley->bullet_enable_orbiting(i, (real_t)radius, orbiting_direction, orbiting_texture_rotation, orbiting_follow_mode, (real_t)orbiting_follow_deadzone, orbiting_lock_policy, orbiting_rigid_follow);
        } else if (orbiting_direction == BulletVolley2D::OrbitRandom) {
            // OrbitRandom rolls once per bullet at enable time: re-rolling on
            // every retarget pass would flip circling directions mid-flight,
            // so live bullets keep their rolled direction here.
            volley->bullet_set_orbiting_radius(i, (real_t)radius);
            volley->bullet_set_orbiting_texture_rotation(i, orbiting_texture_rotation);
            volley->bullet_set_orbiting_follow_mode(i, orbiting_follow_mode);
            volley->bullet_set_orbiting_follow_deadzone(i, (real_t)orbiting_follow_deadzone);
            volley->bullet_set_orbiting_lock_policy(i, orbiting_lock_policy);
            volley->bullet_set_orbiting_rigid_follow(i, orbiting_rigid_follow);
        } else {
            // No-op writes keep the engine lock (radius/direction setters skip
            // identical values), so a retarget pass with unchanged tuning no
            // longer causes the 1-frame fly-to-rim flicker.
            volley->bullet_set_orbiting_radius(i, (real_t)radius);
            volley->bullet_set_orbiting_direction(i, orbiting_direction);
            volley->bullet_set_orbiting_texture_rotation(i, orbiting_texture_rotation);
            volley->bullet_set_orbiting_follow_mode(i, orbiting_follow_mode);
            volley->bullet_set_orbiting_follow_deadzone(i, (real_t)orbiting_follow_deadzone);
            volley->bullet_set_orbiting_lock_policy(i, orbiting_lock_policy);
            volley->bullet_set_orbiting_rigid_follow(i, orbiting_rigid_follow);
        }
    }
}

void BulletSpawner2D::apply_volley_homing_and_orbiting(BulletVolley2D *bullets, const Array *pre_resolved) {
    if (bullets == nullptr) {
        return;
    }
    if (!homing_enabled && !orbiting_enabled) {
        return;
    }
    // NOTE: spawn_position_offset is applied by shoot_once() for EVERY volley
    // (not here): it used to live below, so plain non-homing, non-orbiting
    // spawners silently ignored the muzzle offset.
    // Orbiting locks onto a homing target: without homing there is nothing
    // to orbit, so skip it outright instead of arming a dead feature.
    // The volley instance id names the affected volley: with several
    // spawners firing, a bare warning cannot tell which volley flew straight.
    if (!homing_enabled) {
        // Orbiting alone: warn once per configuration and leave the volley
        // plain (no homing signals: nothing was resolved or configured).
        if (!orbit_without_homing_warned) {
            orbit_without_homing_warned = true;
            UtilityFunctions::push_warning("BulletSpawner2D: orbiting_enabled needs homing_enabled (orbiting locks onto a homing target); volleys fly without orbiting.");
        }
        return;
    }
    // Flat post-spawn nudge (muzzle offsets, whole-volley follows) moved to
    // shoot_once() so it applies to every volley, not just homing/orbiting
    // ones. Kept FIRST there (before homing/orbit seeding) so orbit locks
    // and homing caches form at the final positions.
    Array resolved_targets;
    if (homing_enabled) {
        apply_steering_to_volley(bullets);
        // The volley instance is freshly spawned/enabled, so both deques are
        // empty: replace (clear + push) keeps that invariant explicit and
        // stays correct even if the pool ever changes.
        if (homing_target_source == HOMING_SOURCE_MOUSE) {
            if (homing_mode == HOMING_SHARED) {
                bullets->shared_homing_deque_clear_homing_targets();
                bullets->shared_homing_deque_push_back_mouse_position_target();
            } else {
                bullets->all_bullets_replace_homing_targets_with_mouse();
            }
        } else {
            // Warn-once: a missing enemy roster is reported once per homing
            // configuration, never once per volley; the volley flies plain.
            resolved_targets = (pre_resolved != nullptr) ? *pre_resolved : resolve_homing_targets(false);
            if (!resolved_targets.is_empty()) {
                if (homing_mode == HOMING_SHARED) {
                    bullets->shared_homing_deque_replace_homing_targets_with_new_target_array(resolved_targets);
                } else if (homing_target_selection == HOMING_SELECT_DISTRIBUTE) {
                    // Deal the pool 1:1 across bullets (cycling): bullet i
                    // chases pool[i % pool]. Sized exactly to the volley so
                    // the engine assign validation always passes.
                    Array deal;
                    const int bullet_count = bullets->get_amount_bullets();
                    for (int i = 0; i < bullet_count; ++i) {
                        deal.push_back(resolved_targets[i % resolved_targets.size()]);
                    }
                    bullets->all_bullets_assign_homing_targets_array(deal);
                } else {
                    bullets->all_bullets_replace_homing_targets_with_new_target_array(resolved_targets);
                }
            }
            // Empty = plain volley: flies straight, and a later retarget
            // pass can still pick up targets.
        }
    }
    // Targets go in first so freshly spawned bullets can lock onto their
    // ring on the very first tick instead of flying straight for a frame.
    if (orbiting_enabled && homing_enabled) {
        apply_orbiting_to_volley(bullets);
    }
    // Re-hooked every volley: enabling (pool reuse) disconnects all
    // bullet_homing_target_reached handlers, so a stale connection can never
    // survive here, and is_connected guards the double-spawn edge anyway.
    const Callable forward_callable(this, "_on_volley_bullet_homing_target_reached");
    if (!bullets->is_connected("bullet_homing_target_reached", forward_callable)) {
        bullets->connect("bullet_homing_target_reached", forward_callable);
    }
    // Only homing volleys are worth tracking: without homing, retargeting
    // can never touch them, so tracking would only grow the list.
    if (homing_enabled) {
        track_live_volley(bullets);
    }
    emit_signal("homing_targets_resolved", bullets, resolved_targets);
    // volleys_fired still holds the previous count here (shoot_once bumps it
    // right after), so +1 names the volley being configured.
    emit_signal("volley_homing_configured", bullets, volleys_fired + 1);
}

bool BulletSpawner2D::fire_arc_covers_targets(const Array &targets) const {
    if (homing_fire_arc_deg <= 0.0 || !Math::is_finite(homing_fire_arc_deg)) {
        return true;
    }
    if (targets.is_empty() || !is_inside_tree()) {
        return false;
    }
    // Origin and facing come from the EFFECTIVE GENERATOR (the volley's
    // actual muzzle), not the spawner node: with an external
    // transforms_generator the two differ, and gating on the spawner frame
    // would skip valid shots or admit invalid ones.
    Node2D *base = get_effective_generator();
    if (base == nullptr || !is_inside_tree()) {
        return false;
    }
    const Vector2 origin = base->get_global_position();
    // The volley's actual facing: generator rotation plus the emitter spin
    // (negated on a mirrored burst shot, like the pattern itself).
    real_t spin = Math::deg_to_rad((real_t)spin_angle_deg);
    if (burst_alternate_mirror && burst_mirror_next) {
        spin = -spin;
    }
    const real_t facing = base->get_global_rotation() + spin;
    const real_t half_arc = Math::deg_to_rad((real_t)homing_fire_arc_deg) * 0.5;
    for (int i = 0; i < targets.size(); ++i) {
        Vector2 pos = Vector2(0, 0);
        bool has_pos = false;
        if (Node2D *node = Object::cast_to<Node2D>(targets[i])) {
            pos = node->get_global_position();
            has_pos = pos.is_finite();
        } else if (targets[i].get_type() == Variant::VECTOR2) {
            pos = targets[i];
            has_pos = pos.is_finite();
        }
        if (!has_pos) {
            continue;
        }
        const Vector2 diff = pos - origin;
        if (diff.length_squared() <= 0.0) {
            return true;
        }
        real_t delta = diff.angle() - facing;
        while (delta > Math::PI) {
            delta -= Math::TAU;
        }
        while (delta < -Math::PI) {
            delta += Math::TAU;
        }
        if (Math::abs(delta) <= half_arc) {
            return true;
        }
    }
    return false;
}

} // namespace BlastBullets2D
