#include "bullet_spawner2d.hpp"
#include "bullets/directional_bullets2d.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/global_constants.hpp"
#include "godot_cpp/classes/scene_tree.hpp"
#include "godot_cpp/classes/window.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/core/object.hpp"
#include "godot_cpp/variant/callable.hpp"
#include "godot_cpp/variant/utility_functions.hpp"



using namespace godot;

namespace BlastBullets2D {

// Resolves a stored NodePath to a typed node. When inside the tree the path is
// authoritative: a missing target clears the cache instead of serving a stale
// pointer. Outside the tree the previously cached pointer is returned.
template <typename T>
static T *resolve_node_path(const Node *self, const NodePath &p_path, T *&r_cache) {
    if (self->is_inside_tree() && !p_path.is_empty()) {
        Node *node = self->get_node_or_null(p_path);
        if (node == nullptr) {
            r_cache = nullptr;
            return nullptr;
        }
        T *typed = Object::cast_to<T>(node);
        r_cache = typed;
        return typed;
    }
    return r_cache;
}

template <typename T>
static void assign_node_to_path(const Node *self, T *node, NodePath &r_path, T *&r_cache) {
    r_cache = node;
    if (node != nullptr && self->is_inside_tree() && node->is_inside_tree()) {
        r_path = self->get_path_to(node);
    } else if (node == nullptr) {
        r_path = NodePath();
    }
}

// Rotates a spawn transform around an origin, so a spinning emitter orbits
// bullet positions and turns facings together. Zero rotation is a no-op.
static Transform2D rotate_spawn_transform(const Transform2D &t, const Vector2 &origin, real_t radians) {
    if (radians == 0.0) {
        return t;
    }
    const Vector2 rotated_offset = (t.get_origin() - origin).rotated(radians);
    return Transform2D(t.get_rotation() + radians, origin + rotated_offset);
}
// Uniformly scales a spawn transform relative to the generator origin, so
// marker offsets (spread radius) and basis (bullet size) grow together while
// rotation is preserved. Scale 1.0 returns the transform untouched.
static Transform2D scale_spawn_transform(const Transform2D &t, const Vector2 &base_origin, real_t scale) {
    if (scale == 1.0) {
        return t;
    }
    return Transform2D(t.columns[0] * scale, t.columns[1] * scale, base_origin + (t.columns[2] - base_origin) * scale);
}

// Metadata tag + node name for the editor-only pattern preview holder.
// The children-mode collection skips anything carrying the tag, so the
// preview can never become a spawn marker. The tilde sorts it last and marks
// it as internal. The holder is owner-less: never saved, never exported.
static const char *PREVIEW_META_KEY = "blastbullets_pattern_preview";
static const char *PREVIEW_HOLDER_NAME = "~BlastBulletsPatternPreview";

// Self-repainting preview layer: rebuild_preview() stores a snapshot via the
// setters below, the engine re-invokes _draw() on every repaint (zoom, pan,
// selection, idle refresh), so the gizmo can never vanish between rebuilds.
void PatternPreviewLayer2D::_bind_methods() {
}

void PatternPreviewLayer2D::set_dots_data(const PackedVector2Array &p_dots, const Color &p_color, float p_radius) {
    dots = p_dots;
    dot_color = p_color;
    dot_radius = p_radius;
    queue_redraw();
}

void PatternPreviewLayer2D::set_arrows_data(const PackedVector2Array &p_tails, const PackedVector2Array &p_dirs, const Color &p_color, float p_length, float p_width, float p_head_length, float p_head_width) {
    arrow_tails = p_tails;
    arrow_dirs = p_dirs;
    arrow_color = p_color;
    arrow_length = p_length;
    arrow_width = p_width;
    arrow_head_length = p_head_length;
    arrow_head_width = p_head_width;
    queue_redraw();
}

void PatternPreviewLayer2D::_draw() {
    if (kind == LAYER_DOTS) {
        for (int i = 0; i < dots.size(); i++) {
            draw_circle(dots[i], dot_radius, dot_color);
        }
        return;
    }
    // One true arrow silhouette per bullet: the shaft ends exactly where the
    // head begins (clamped, never inverted), and the filled triangular head
    // sits forward of that joint. No overlap means no nub poking past the
    // tip and no line showing through the triangle.
    const int count = MIN(arrow_tails.size(), arrow_dirs.size());
    for (int i = 0; i < count; i++) {
        const Vector2 dir = arrow_dirs[i];
        const Vector2 tail = arrow_tails[i];
        const Vector2 tip = tail + dir * arrow_length;
        const float head_len = MIN(arrow_head_length, arrow_length);
        const bool has_head = head_len > 0.0f && arrow_head_width > 0.0f;
        const Vector2 head_base = has_head ? tip - dir * head_len : tip;
        if (arrow_length > 0.0f && arrow_width > 0.0f) {
            draw_line(tail, head_base, arrow_color, arrow_width, false);
        }
        if (has_head) {
            const Vector2 perp = dir.orthogonal() * (arrow_head_width * 0.5f);
            draw_colored_polygon(PackedVector2Array({ tip, head_base + perp, head_base - perp }), arrow_color);
        }
    }
}

// Human-readable mode name for error messages (mirrors the transforms_source enum hint).
static const char *transforms_source_name(BulletSpawner2D::TransformsSource source) {
    switch (source) {
        case BulletSpawner2D::TRANSFORMS_FROM_CHILDREN:
            return "From Children";
        case BulletSpawner2D::TRANSFORMS_FROM_SELF:
            return "From Self";
        case BulletSpawner2D::TRANSFORMS_FROM_HELPER_GRID:
            return "Grid";
        case BulletSpawner2D::TRANSFORMS_FROM_HELPER_RING:
            return "Ring";
        case BulletSpawner2D::TRANSFORMS_FROM_HELPER_FAN:
            return "Fan";
        case BulletSpawner2D::TRANSFORMS_FROM_HELPER_SPIRAL:
            return "Spiral";
        case BulletSpawner2D::TRANSFORMS_FROM_HELPER_LINE:
            return "Line";
        case BulletSpawner2D::TRANSFORMS_FROM_HELPER_AIMED:
            return "Aimed";
        default:
            return "unknown";
    }
}

NodePath BulletSpawner2D::get_bullet_factory_path() const {
    return bullet_factory_path;
}

void BulletSpawner2D::set_bullet_factory_path(const NodePath &p_path) {
    bullet_factory_path = p_path;
    bullet_factory = nullptr;
    if (!bullet_factory_path.is_empty() && is_inside_tree()) {
        Node *node = get_node_or_null(bullet_factory_path);
        if (node != nullptr && Object::cast_to<BulletFactory2D>(node) == nullptr) {
            UtilityFunctions::push_warning("BulletSpawner2D: assigned bullet_factory node is not a BulletFactory2D.");
        } else {
            resolve_node_path(this, bullet_factory_path, bullet_factory);
        }
    }
}

BulletFactory2D *BulletSpawner2D::get_bullet_factory() const {
    return resolve_node_path(this, bullet_factory_path, bullet_factory);
}

void BulletSpawner2D::set_bullet_factory(BulletFactory2D *factory) {
    assign_node_to_path(this, factory, bullet_factory_path, bullet_factory);
}

NodePath BulletSpawner2D::get_transforms_generator_path() const {
    return transforms_generator_path;
}

void BulletSpawner2D::set_transforms_generator_path(const NodePath &p_path) {
    transforms_generator_path = p_path;
    transforms_generator = nullptr;
    if (!transforms_generator_path.is_empty() && is_inside_tree()) {
        Node *node = get_node_or_null(transforms_generator_path);
        if (node != nullptr && Object::cast_to<Node2D>(node) == nullptr) {
            UtilityFunctions::push_warning("BulletSpawner2D: assigned transforms generator node is not a Node2D.");
        } else {
            resolve_node_path(this, transforms_generator_path, transforms_generator);
        }
    }
    rebuild_preview();
}

Node2D *BulletSpawner2D::get_transforms_generator() const {
    return resolve_node_path(this, transforms_generator_path, transforms_generator);
}

void BulletSpawner2D::set_transforms_generator(Node2D *generator) {
    assign_node_to_path(this, generator, transforms_generator_path, transforms_generator);
    rebuild_preview();
}

// Anchor for every transforms_source mode. Empty/unresolvable path falls back
// to the spawner itself. A generator outside the tree is ignored while the
// spawner is inside it (its global transform is invalid), so callers can
// always use the result for global-space work when inside the tree.
Node2D *BulletSpawner2D::get_effective_generator() const {
    Node2D *base = get_transforms_generator();
    if (base == nullptr) {
        return const_cast<BulletSpawner2D *>(this);
    }
    if (is_inside_tree() && !base->is_inside_tree()) {
        return const_cast<BulletSpawner2D *>(this);
    }
    return base;
}

Ref<DirectionalBulletsData2D> BulletSpawner2D::get_spawn_data() const {
    return spawn_data;
}
void BulletSpawner2D::set_spawn_data(const Ref<DirectionalBulletsData2D> &new_data) {
    spawn_data = new_data;
}

bool BulletSpawner2D::auto_shooting_active() const {
    return shooting_enabled && (max_volleys < 0 || volleys_fired < max_volleys);
}

bool BulletSpawner2D::get_shooting_enabled() const {
    return shooting_enabled;
}
void BulletSpawner2D::set_shooting_enabled(bool value) {
    const bool was_active = auto_shooting_active();
    shooting_enabled = value;
    const bool now_active = auto_shooting_active();
    if (is_inside_tree()) {
        set_process(now_active || spin_enabled || homing_retarget_active());
    }
    if (!was_active && now_active) {
        emit_signal("shooting_started");
    } else if (was_active && !now_active) {
        emit_signal("shooting_stopped");
    }
}

double BulletSpawner2D::get_shoot_interval_sec() const {
    return shoot_interval_sec;
}
void BulletSpawner2D::set_shoot_interval_sec(double value) {
    if (!Math::is_finite(value) || !(value > 0.0)) {
        UtilityFunctions::push_error("BulletSpawner2D: shoot_interval_sec must be finite and > 0, keeping the old value.");
        return;
    }
    shoot_interval_sec = value;
}

double BulletSpawner2D::get_shoot_initial_delay_sec() const {
    return shoot_initial_delay_sec;
}
void BulletSpawner2D::set_shoot_initial_delay_sec(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: shoot_initial_delay_sec must be finite and >= 0, keeping the old value.");
        return;
    }
    shoot_initial_delay_sec = value;
}

int BulletSpawner2D::get_max_volleys() const {
    return max_volleys;
}
void BulletSpawner2D::set_max_volleys(int value) {
    if (value < -1) {
        UtilityFunctions::push_error("BulletSpawner2D: max_volleys must be -1 (infinite) or >= 0, keeping the old value.");
        return;
    }
    // Raising the cap resumes a stopped spawner; lowering it below the count
    // stops it on the next opportunity.
    const bool was_active = auto_shooting_active();
    max_volleys = value;
    const bool now_active = auto_shooting_active();
    if (is_inside_tree()) {
        set_process(now_active || spin_enabled || homing_retarget_active());
    }
    if (!was_active && now_active) {
        emit_signal("shooting_started");
    } else if (was_active && !now_active) {
        emit_signal("shooting_finished");
    }
}

int BulletSpawner2D::get_volleys_fired() const {
    return volleys_fired;
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
    rebuild_preview();
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
}

bool BulletSpawner2D::get_spin_enabled() const {
    return spin_enabled;
}
void BulletSpawner2D::set_spin_enabled(bool value) {
    spin_enabled = value;
    if (!Engine::get_singleton()->is_editor_hint() && is_inside_tree()) {
        // Spinning needs _process even when auto-shooting is off.
        set_process(spin_enabled || auto_shooting_active() || homing_retarget_active());
    }
}
double BulletSpawner2D::get_spin_speed_deg_per_sec() const {
    return spin_speed_deg_per_sec;
}
void BulletSpawner2D::set_spin_speed_deg_per_sec(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: spin_speed_deg_per_sec must be finite, keeping the old value.");
        return;
    }
    spin_speed_deg_per_sec = value;
}
BulletSpawner2D::SpinMode BulletSpawner2D::get_spin_mode() const {
    return spin_mode;
}
void BulletSpawner2D::set_spin_mode(SpinMode value) {
    if (value < SPIN_CONTINUOUS || value > SPIN_OSCILLATE) {
        UtilityFunctions::push_error("BulletSpawner2D: invalid spin_mode, keeping the old value.");
        return;
    }
    spin_mode = value;
}
double BulletSpawner2D::get_spin_amplitude_deg() const {
    return spin_amplitude_deg;
}
void BulletSpawner2D::set_spin_amplitude_deg(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: spin_amplitude_deg must be finite and >= 0, keeping the old value.");
        return;
    }
    spin_amplitude_deg = value;
}
double BulletSpawner2D::get_spin_frequency_hz() const {
    return spin_frequency_hz;
}
void BulletSpawner2D::set_spin_frequency_hz(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: spin_frequency_hz must be finite and >= 0, keeping the old value.");
        return;
    }
    spin_frequency_hz = value;
}
double BulletSpawner2D::get_spin_angle_deg() const {
    return spin_angle_deg;
}
bool BulletSpawner2D::is_spinning() const {
    return spin_enabled && !Engine::get_singleton()->is_editor_hint();
}
void BulletSpawner2D::start_spinning() {
    set_spin_enabled(true);
}
void BulletSpawner2D::stop_spinning() {
    set_spin_enabled(false);
}
void BulletSpawner2D::reset_spin_angle() {
    spin_angle_deg = 0.0;
    spin_time_sec = 0.0;
}
void BulletSpawner2D::advance_spin(double delta) {
    if (spin_mode == SPIN_OSCILLATE) {
        spin_time_sec += delta;
        spin_angle_deg = spin_amplitude_deg * Math::sin(Math::TAU * spin_frequency_hz * spin_time_sec);
    } else {
        spin_angle_deg = Math::fposmod(spin_angle_deg + spin_speed_deg_per_sec * delta, 360.0);
    }
}

BulletSpawner2D::TransformsSource BulletSpawner2D::get_transforms_source() const {
    return transforms_source;
}
void BulletSpawner2D::set_transforms_source(TransformsSource value) {
    if (value < TRANSFORMS_FROM_CHILDREN || value > TRANSFORMS_FROM_HELPER_AIMED) {
        UtilityFunctions::push_error("BulletSpawner2D: invalid transforms_source, keeping the old value.");
        return;
    }
    transforms_source = value;
    // The visible helper_* option groups depend on this mode: refresh the
    // inspector immediately, otherwise the new mode's options stay hidden
    // until the selection is re-clicked (see _validate_property).
    notify_property_list_changed();
    update_preview_process_state();
    rebuild_preview();
}

int BulletSpawner2D::get_helper_bullets_amount() const {
    return helper_bullets_amount;
}
void BulletSpawner2D::set_helper_bullets_amount(int value) {
    if (value < 1) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_bullets_amount must be >= 1, keeping the old value.");
        return;
    }
    helper_bullets_amount = value;
    rebuild_preview();
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
    rebuild_preview();
}

int BulletSpawner2D::get_helper_grid_alignment() const {
    return helper_grid_alignment;
}
void BulletSpawner2D::set_helper_grid_alignment(int value) {
    if (value < (int)BulletFactory2D::TOP_LEFT || value > (int)BulletFactory2D::BOTTOM_RIGHT) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_grid_alignment out of range, keeping the old value.");
        return;
    }
    helper_grid_alignment = value;
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
}

bool BulletSpawner2D::get_helper_grid_rotate_with_marker() const {
    return helper_grid_rotate_with_marker;
}
void BulletSpawner2D::set_helper_grid_rotate_with_marker(bool value) {
    helper_grid_rotate_with_marker = value;
    rebuild_preview();
}

bool BulletSpawner2D::get_helper_grid_random_local_rotation() const {
    return helper_grid_random_local_rotation;
}
void BulletSpawner2D::set_helper_grid_random_local_rotation(bool value) {
    helper_grid_random_local_rotation = value;
    rebuild_preview();
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
    rebuild_preview();
}

double BulletSpawner2D::get_helper_ring_radius() const {
    return helper_ring_radius;
}
void BulletSpawner2D::set_helper_ring_radius(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_ring_radius must be finite, keeping the old value.");
        return;
    }
    helper_ring_radius = value;
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
}

bool BulletSpawner2D::get_helper_ring_rotate_with_marker() const {
    return helper_ring_rotate_with_marker;
}
void BulletSpawner2D::set_helper_ring_rotate_with_marker(bool value) {
    helper_ring_rotate_with_marker = value;
    rebuild_preview();
}

bool BulletSpawner2D::get_helper_ring_random_rotation() const {
    return helper_ring_random_rotation;
}
void BulletSpawner2D::set_helper_ring_random_rotation(bool value) {
    helper_ring_random_rotation = value;
    rebuild_preview();
}

bool BulletSpawner2D::get_helper_ring_face_outward() const {
    return helper_ring_face_outward;
}
void BulletSpawner2D::set_helper_ring_face_outward(bool value) {
    helper_ring_face_outward = value;
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
}

double BulletSpawner2D::get_helper_spiral_start_radius() const {
    return helper_spiral_start_radius;
}
void BulletSpawner2D::set_helper_spiral_start_radius(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_spiral_start_radius must be finite, keeping the old value.");
        return;
    }
    helper_spiral_start_radius = value;
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
}

bool BulletSpawner2D::get_helper_spiral_rotate_with_marker() const {
    return helper_spiral_rotate_with_marker;
}
void BulletSpawner2D::set_helper_spiral_rotate_with_marker(bool value) {
    helper_spiral_rotate_with_marker = value;
    rebuild_preview();
}

Vector2 BulletSpawner2D::get_helper_line_direction() const {
    return helper_line_direction;
}
void BulletSpawner2D::set_helper_line_direction(const Vector2 &value) {
    if (!value.is_finite()) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_line_direction must be finite, keeping the old value.");
        return;
    }
    helper_line_direction = value;
    rebuild_preview();
}

double BulletSpawner2D::get_helper_line_spacing() const {
    return helper_line_spacing;
}
void BulletSpawner2D::set_helper_line_spacing(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_line_spacing must be finite, keeping the old value.");
        return;
    }
    helper_line_spacing = value;
    rebuild_preview();
}

bool BulletSpawner2D::get_helper_line_face_direction() const {
    return helper_line_face_direction;
}
void BulletSpawner2D::set_helper_line_face_direction(bool value) {
    helper_line_face_direction = value;
    rebuild_preview();
}

NodePath BulletSpawner2D::get_helper_aimed_target_path() const {
    return helper_aimed_target_path;
}

void BulletSpawner2D::set_helper_aimed_target_path(const NodePath &p_path) {
    helper_aimed_target_path = p_path;
    helper_aimed_target = nullptr;
    if (!helper_aimed_target_path.is_empty() && is_inside_tree()) {
        Node *node = get_node_or_null(helper_aimed_target_path);
        if (node != nullptr && Object::cast_to<Node2D>(node) == nullptr) {
            UtilityFunctions::push_warning("BulletSpawner2D: assigned aimed target node is not a Node2D.");
        } else {
            resolve_node_path(this, helper_aimed_target_path, helper_aimed_target);
        }
    }
    rebuild_preview();
}

Node2D *BulletSpawner2D::get_helper_aimed_target() const {
    return resolve_node_path(this, helper_aimed_target_path, helper_aimed_target);
}

void BulletSpawner2D::set_helper_aimed_target(Node2D *target) {
    assign_node_to_path(this, target, helper_aimed_target_path, helper_aimed_target);
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
}

double BulletSpawner2D::get_helper_ring_y_scale() const {
    return helper_ring_y_scale;
}
void BulletSpawner2D::set_helper_ring_y_scale(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_ring_y_scale must be finite, keeping the old value.");
        return;
    }
    helper_ring_y_scale = value;
    rebuild_preview();
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
    rebuild_preview();
}
bool BulletSpawner2D::get_helper_fan_centered() const {
    return helper_fan_centered;
}
void BulletSpawner2D::set_helper_fan_centered(bool value) {
    helper_fan_centered = value;
    rebuild_preview();
}
int BulletSpawner2D::get_helper_spiral_facing() const {
    return helper_spiral_facing;
}
void BulletSpawner2D::set_helper_spiral_facing(int value) {
    if (value < (int)BulletFactory2D::SPIRAL_FACING_TANGENT || value > (int)BulletFactory2D::SPIRAL_FACING_KEEP_MARKER) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_spiral_facing out of range, keeping the old value.");
        return;
    }
    helper_spiral_facing = value;
    rebuild_preview();
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
    rebuild_preview();
}
int BulletSpawner2D::get_helper_line_anchor() const {
    return helper_line_anchor;
}
void BulletSpawner2D::set_helper_line_anchor(int value) {
    if (value < (int)BulletFactory2D::LINE_ANCHOR_START || value > (int)BulletFactory2D::LINE_ANCHOR_END) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_line_anchor out of range, keeping the old value.");
        return;
    }
    helper_line_anchor = value;
    rebuild_preview();
}
bool BulletSpawner2D::get_helper_line_perpendicular() const {
    return helper_line_perpendicular;
}
void BulletSpawner2D::set_helper_line_perpendicular(bool value) {
    helper_line_perpendicular = value;
    rebuild_preview();
}
bool BulletSpawner2D::get_helper_aimed_centered() const {
    return helper_aimed_centered;
}
void BulletSpawner2D::set_helper_aimed_centered(bool value) {
    helper_aimed_centered = value;
    rebuild_preview();
}
bool BulletSpawner2D::get_show_pattern_preview() const {
    return show_pattern_preview;
}
void BulletSpawner2D::set_show_pattern_preview(bool value) {
    show_pattern_preview = value;
    update_preview_process_state();
    rebuild_preview();
}
bool BulletSpawner2D::get_show_preview_during_runtime() const {
    return show_preview_during_runtime;
}
void BulletSpawner2D::set_show_preview_during_runtime(bool value) {
    show_preview_during_runtime = value;
    update_preview_process_state();
    rebuild_preview();
}
Color BulletSpawner2D::get_preview_dot_color() const {
    return preview_dot_color;
}
void BulletSpawner2D::set_preview_dot_color(const Color &value) {
    preview_dot_color = value;
    rebuild_preview();
}
Color BulletSpawner2D::get_preview_arrow_color() const {
    return preview_arrow_color;
}
void BulletSpawner2D::set_preview_arrow_color(const Color &value) {
    preview_arrow_color = value;
    rebuild_preview();
}
double BulletSpawner2D::get_preview_dot_radius() const {
    return preview_dot_radius;
}
void BulletSpawner2D::set_preview_dot_radius(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: preview_dot_radius must be finite and >= 0, keeping the old value.");
        return;
    }
    preview_dot_radius = value;
    rebuild_preview();
}
double BulletSpawner2D::get_preview_arrow_gap() const {
    return preview_arrow_gap;
}
void BulletSpawner2D::set_preview_arrow_gap(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: preview_arrow_gap must be finite and >= 0, keeping the old value.");
        return;
    }
    preview_arrow_gap = value;
    rebuild_preview();
}
double BulletSpawner2D::get_preview_arrow_length() const {
    return preview_arrow_length;
}
void BulletSpawner2D::set_preview_arrow_length(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: preview_arrow_length must be finite and >= 0, keeping the old value.");
        return;
    }
    preview_arrow_length = value;
    rebuild_preview();
}
double BulletSpawner2D::get_preview_arrow_width() const {
    return preview_arrow_width;
}
void BulletSpawner2D::set_preview_arrow_width(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: preview_arrow_width must be finite and >= 0, keeping the old value.");
        return;
    }
    preview_arrow_width = value;
    rebuild_preview();
}
double BulletSpawner2D::get_preview_arrow_head_length() const {
    return preview_arrow_head_length;
}
void BulletSpawner2D::set_preview_arrow_head_length(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: preview_arrow_head_length must be finite and >= 0, keeping the old value.");
        return;
    }
    preview_arrow_head_length = value;
    rebuild_preview();
}
double BulletSpawner2D::get_preview_arrow_head_width() const {
    return preview_arrow_head_width;
}
void BulletSpawner2D::set_preview_arrow_head_width(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: preview_arrow_head_width must be finite and >= 0, keeping the old value.");
        return;
    }
    preview_arrow_head_width = value;
    rebuild_preview();
}

void BulletSpawner2D::reset_shooting() {
    volleys_fired = 0;
    shoot_time_left = shoot_initial_delay_sec;
    // Due-now (0.0): the next tick runs a pass immediately instead of
    // waiting a full interval on stale membership data.
    homing_retarget_time_left = 0.0;
    if (is_inside_tree()) {
        set_process(auto_shooting_active() || spin_enabled || homing_retarget_active());
    }
    // An explicit restart counts as a (re)start whenever it arms shooting.
    if (auto_shooting_active()) {
        emit_signal("shooting_started");
    }
}

// HOMING + ORBITING (EASY API)

bool BulletSpawner2D::get_homing_enabled() const {
    return homing_enabled;
}
void BulletSpawner2D::set_homing_enabled(bool value) {
    homing_enabled = value;
    notify_property_list_changed();
    update_homing_process_state();
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
    notify_property_list_changed();
}
StringName BulletSpawner2D::get_homing_node_group() const {
    return homing_node_group;
}
void BulletSpawner2D::set_homing_node_group(const StringName &value) {
    homing_node_group = value;
}
StringName BulletSpawner2D::get_homing_filter_group() const {
    return homing_filter_group;
}
void BulletSpawner2D::set_homing_filter_group(const StringName &value) {
    homing_filter_group = value;
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
}
int BulletSpawner2D::get_homing_max_targets() const {
    return homing_max_targets;
}
void BulletSpawner2D::set_homing_max_targets(int value) {
    if (value < 1) {
        UtilityFunctions::push_error("BulletSpawner2D: homing_max_targets must be >= 1, keeping the old value.");
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
    homing_target_path = p_path;
}
String BulletSpawner2D::get_homing_node_name() const {
    return homing_node_name;
}
void BulletSpawner2D::set_homing_node_name(const String &value) {
    homing_node_name = value;
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
}
bool BulletSpawner2D::get_homing_node_name_case_sensitive() const {
    return homing_node_name_case_sensitive;
}
void BulletSpawner2D::set_homing_node_name_case_sensitive(bool value) {
    homing_node_name_case_sensitive = value;
}
NodePath BulletSpawner2D::get_homing_children_parent_path() const {
    return homing_children_parent_path;
}
void BulletSpawner2D::set_homing_children_parent_path(const NodePath &p_path) {
    homing_children_parent_path = p_path;
}
bool BulletSpawner2D::get_homing_children_recursive() const {
    return homing_children_recursive;
}
void BulletSpawner2D::set_homing_children_recursive(bool value) {
    homing_children_recursive = value;
}
bool BulletSpawner2D::get_homing_debug_log_volleys() const {
    return homing_debug_log_volleys;
}
void BulletSpawner2D::set_homing_debug_log_volleys(bool value) {
    homing_debug_log_volleys = value;
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
bool BulletSpawner2D::get_adjust_direction_based_on_rotation() const {
    return adjust_direction_based_on_rotation;
}
void BulletSpawner2D::set_adjust_direction_based_on_rotation(bool value) {
    adjust_direction_based_on_rotation = value;
}
bool BulletSpawner2D::get_homing_auto_pop_after_target_reached() const {
    return homing_auto_pop_after_target_reached;
}
void BulletSpawner2D::set_homing_auto_pop_after_target_reached(bool value) {
    homing_auto_pop_after_target_reached = value;
}
bool BulletSpawner2D::get_shared_homing_auto_pop_after_target_reached() const {
    return shared_homing_auto_pop_after_target_reached;
}
void BulletSpawner2D::set_shared_homing_auto_pop_after_target_reached(bool value) {
    shared_homing_auto_pop_after_target_reached = value;
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
    homing_retarget_mode = value;
    notify_property_list_changed();
    update_homing_process_state();
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
    orbiting_enabled = value;
    notify_property_list_changed();
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
DirectionalBullets2D::OrbitingDirection BulletSpawner2D::get_orbiting_direction() const {
    return orbiting_direction;
}
void BulletSpawner2D::set_orbiting_direction(DirectionalBullets2D::OrbitingDirection value) {
    if (value != DirectionalBullets2D::DontMove && value != DirectionalBullets2D::OrbitLeft && value != DirectionalBullets2D::OrbitRight) {
        UtilityFunctions::push_error("BulletSpawner2D: invalid orbiting_direction, keeping the old value.");
        return;
    }
    if (value == DirectionalBullets2D::DontMove) {
        // Warn here, once, instead of per volley: DontMove keeps the bullet
        // course, so orbiting is skipped wherever it is applied.
        UtilityFunctions::push_warning("BulletSpawner2D: orbiting_direction is Dont Move (bullets keep their course), orbiting will be skipped.");
    }
    orbiting_direction = value;
}
DirectionalBullets2D::OrbitingTextureRotation BulletSpawner2D::get_orbiting_texture_rotation() const {
    return orbiting_texture_rotation;
}
void BulletSpawner2D::set_orbiting_texture_rotation(DirectionalBullets2D::OrbitingTextureRotation value) {
    if (value < DirectionalBullets2D::FaceTarget || value > DirectionalBullets2D::FaceOppositeOrbitingDirection) {
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

bool BulletSpawner2D::homing_retarget_active() const {
    return homing_enabled && homing_retarget_mode == HOMING_RETARGET_ON_INTERVAL && !Engine::get_singleton()->is_editor_hint() && is_inside_tree();
}

void BulletSpawner2D::update_homing_process_state() {
    if (homing_retarget_active()) {
        // Due-now: a freshly armed retargeter refreshes on the next tick.
        homing_retarget_time_left = 0.0;
        set_process(true);
    } else if (!Engine::get_singleton()->is_editor_hint() && is_inside_tree()) {
        // Sleep when nothing needs the loop. Mirrors the shooting setters so
        // disabling retarget can actually stop _process.
        set_process(auto_shooting_active() || spin_enabled || preview_active());
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
    Array stack;
    stack.push_back(p_node);
    while (!stack.is_empty()) {
        Node *node = Object::cast_to<Node>(stack.pop_back());
        if (node == nullptr) {
            continue;
        }
        Node2D *as_2d = Object::cast_to<Node2D>(node);
        // Never chase ourselves or our own markers: the spawner (and any Node2D
        // markers under it) would otherwise match a broad pattern like "Node2D".
        if (as_2d != nullptr && as_2d != this) {
            String node_name = String(as_2d->get_name());
            String pattern = homing_node_name;
            if (!homing_node_name_case_sensitive) {
                node_name = node_name.to_lower();
                pattern = pattern.to_lower();
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
    for (int i = 0; i < p_parent->get_child_count(); ++i) {
        Node *child = p_parent->get_child(i);
        Node2D *as_2d = Object::cast_to<Node2D>(child);
        // Never the spawner itself (a parent pointing at our own node would
        // otherwise make the volley chase its emitter).
        if (as_2d != nullptr && as_2d != this) {
            if (homing_filter_group.is_empty() || as_2d->is_in_group(homing_filter_group)) {
                r_candidates.push_back(as_2d);
            }
        }
        if (recursive) {
            collect_homing_candidates_from_children(child, true, r_candidates);
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
        clear_empty_homing_targets_warning();
        return targets;
    }
    if (homing_target_source == HOMING_SOURCE_NODE_PATH) {
        Node2D *target = Object::cast_to<Node2D>(get_node_or_null(homing_target_path));
        if (target == nullptr) {
            warn_empty_homing_targets_once("BulletSpawner2D::resolve_homing_targets: homing_target_path does not point at a live Node2D, volley flies without homing.", quiet);
            return targets;
        }
        if (!homing_filter_group.is_empty() && !target->is_in_group(homing_filter_group)) {
            warn_empty_homing_targets_once("BulletSpawner2D::resolve_homing_targets: homing_target_path node is not in homing_filter_group, volley flies without homing.", quiet);
            return targets;
        }
        targets.push_back(target);
        clear_empty_homing_targets_warning();
        return targets;
    }
    // Multi-target sources (group, name, children) share one tail below
    // (range cull, empty check, selection): each branch only fills
    // `candidates`.
    Array candidates;
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
                if (!homing_filter_group.is_empty() && !candidate->is_in_group(homing_filter_group)) {
                    continue;
                }
                candidates.push_back(candidate);
            }
        }
    }
    // Detection range culls all multi-target sources around the spawner.
    if (homing_max_detection_range > 0.0 && !candidates.is_empty()) {
        const Vector2 origin = get_global_position();
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
    clear_empty_homing_targets_warning();
    const int take = MIN(homing_max_targets, (int)candidates.size());
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
                homing_rng->randomize();
            }
            Array pool = candidates.duplicate();
            for (int k = 0; k < take && !pool.is_empty(); ++k) {
                const int idx = (int)homing_rng->randi_range(0, pool.size() - 1);
                targets.push_back(pool[idx]);
                pool.remove_at(idx);
            }
            break;
        }
        case HOMING_SELECT_ROUND_ROBIN: {
            const int n = (int)candidates.size();
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
            const Vector2 origin = get_global_position();
            Array pool = candidates.duplicate();
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

void BulletSpawner2D::track_live_volley(DirectionalBullets2D *bullets) {
    if (bullets == nullptr) {
        return;
    }
    const int64_t id = (int64_t)bullets->get_instance_id();
    if (!live_volley_instance_ids.has(id)) {
        live_volley_instance_ids.push_back(id);
    }
    prune_live_volleys();
}

void BulletSpawner2D::prune_live_volleys() const {
    PackedInt64Array kept;
    const uint64_t self_id = get_instance_id();
    for (int i = 0; i < live_volley_instance_ids.size(); ++i) {
        Object *obj = ObjectDB::get_instance(ObjectID((uint64_t)live_volley_instance_ids[i]));
        DirectionalBullets2D *volley = Object::cast_to<DirectionalBullets2D>(obj);
        // Freed instances (teardown), pooled instances re-owned by another
        // spawner (owner tag changed), and fully-disabled instances (a new
        // enable wipes homing anyway) are dropped, never touched. The
        // is_active gate matters: disable keeps owner_spawner_id, so without
        // it retargets would arm dead queues and inflate homing counters.
        if (volley != nullptr && volley->owner_spawner_id == self_id && volley->is_active) {
            kept.push_back(live_volley_instance_ids[i]);
        }
    }
    live_volley_instance_ids = kept;
}

int BulletSpawner2D::get_live_volley_count() const {
    prune_live_volleys();
    return live_volley_instance_ids.size();
}

void BulletSpawner2D::clear_live_volleys() {
    live_volley_instance_ids.clear();
}

Array BulletSpawner2D::get_live_volleys() const {
    prune_live_volleys();
    Array out;
    for (int i = 0; i < live_volley_instance_ids.size(); ++i) {
        Object *obj = ObjectDB::get_instance(ObjectID((uint64_t)live_volley_instance_ids[i]));
        DirectionalBullets2D *volley = Object::cast_to<DirectionalBullets2D>(obj);
        // Prune just filtered, but re-check cheaply: never hand out a
        // foreign or inactive instance for direct engine calls.
        if (volley != nullptr && volley->owner_spawner_id == get_instance_id() && volley->is_active) {
            out.push_back(volley);
        }
    }
    return out;
}

bool BulletSpawner2D::adopt_live_volley(DirectionalBullets2D *bullets) {
    if (bullets == nullptr) {
        UtilityFunctions::push_error("BulletSpawner2D::adopt_live_volley: instance is null.");
        return false;
    }
    if (!bullets->is_active || !bullets->is_inside_tree()) {
        UtilityFunctions::push_error("BulletSpawner2D::adopt_live_volley: instance is not live (pooled or outside the tree). Wake it first.");
        return false;
    }
    // Takes over a manually-woken volley (manual wake detaches the previous
    // owner): stamps, hooks the forwarder, tracks. Queues are left alone.
    bullets->owner_spawner_id = get_instance_id();
    const Callable forward_callable(this, "_on_volley_bullet_homing_target_reached");
    if (!bullets->is_connected("bullet_homing_target_reached", forward_callable)) {
        bullets->connect("bullet_homing_target_reached", forward_callable);
    }
    track_live_volley(bullets);
    return true;
}

int BulletSpawner2D::clear_live_volleys_homing() {
    prune_live_volleys();
    int done = 0;
    const uint64_t self_id = get_instance_id();
    for (int i = 0; i < live_volley_instance_ids.size(); ++i) {
        Object *obj = ObjectDB::get_instance(ObjectID((uint64_t)live_volley_instance_ids[i]));
        DirectionalBullets2D *volley = Object::cast_to<DirectionalBullets2D>(obj);
        if (volley == nullptr || volley->owner_spawner_id != self_id || !volley->is_active) {
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
    prune_live_volleys();
    int done = 0;
    const uint64_t self_id = get_instance_id();
    for (int i = 0; i < live_volley_instance_ids.size(); ++i) {
        Object *obj = ObjectDB::get_instance(ObjectID((uint64_t)live_volley_instance_ids[i]));
        DirectionalBullets2D *volley = Object::cast_to<DirectionalBullets2D>(obj);
        if (volley == nullptr || volley->owner_spawner_id != self_id || !volley->is_active) {
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
    prune_live_volleys();
    // Resolve is the expensive part (group poll / scene scan): skip it when
    // no live volley could consume the result.
    if (!homing_enabled || !is_inside_tree() || live_volley_instance_ids.is_empty()) {
        return 0;
    }
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
    const int loop_start = homing_retarget_previous_volleys ? 0 : (int)live_volley_instance_ids.size() - 1;
    int done = 0;
    const uint64_t self_id = get_instance_id();
    for (int i = loop_start; i < (int)live_volley_instance_ids.size(); ++i) {
        Object *obj = ObjectDB::get_instance(ObjectID((uint64_t)live_volley_instance_ids[i]));
        DirectionalBullets2D *volley = Object::cast_to<DirectionalBullets2D>(obj);
        if (volley == nullptr || volley->owner_spawner_id != self_id || !volley->is_active) {
            continue;
        }
        Array volley_targets = shared_targets;
        if (per_volley_resolve) {
            volley_targets = resolve_homing_targets(true, false);
            if (volley_targets.is_empty()) {
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
                // Re-deal like at spawn: clear first, because assign
                // push-backs onto existing queues (a replace would grow them
                // every pass).
                Array deal;
                const int bullet_count = volley->get_amount_bullets();
                for (int i = 0; i < bullet_count; ++i) {
                    deal.push_back(volley_targets[i % volley_targets.size()]);
                }
                volley->all_bullets_clear_homing_targets();
                volley->all_bullets_assign_homing_targets_array(deal);
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

void BulletSpawner2D::_on_volley_bullet_homing_target_reached(Object *directional_bullets_instance, int bullet_index, Object *target, const Vector2 &target_global_position) {
    emit_signal("volley_bullet_homing_target_reached", directional_bullets_instance, bullet_index, target, target_global_position);
}

void BulletSpawner2D::apply_steering_to_volley(DirectionalBullets2D *volley) const {
    volley->set_homing_smoothing((real_t)homing_smoothing);
    volley->set_homing_update_interval((real_t)homing_update_interval);
    volley->set_homing_distance_before_reached((real_t)homing_distance_before_reached);
    volley->set_homing_take_control_of_texture_rotation(homing_take_control_of_texture_rotation);
    volley->set_adjust_direction_based_on_rotation(adjust_direction_based_on_rotation);
    volley->set_bullet_homing_auto_pop_after_target_reached(homing_auto_pop_after_target_reached);
    volley->set_shared_homing_deque_auto_pop_after_target_reached(shared_homing_auto_pop_after_target_reached);
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

void BulletSpawner2D::apply_orbiting_to_volley(DirectionalBullets2D *volley) const {
    if (orbiting_direction == DirectionalBullets2D::DontMove) {
        return; // The setter already warned: enabling it would be a silent no-op.
    }
    const int bullet_count = volley->get_amount_bullets();
    for (int i = 0; i < bullet_count; ++i) {
        // Negative linear steps fan downward: clamp per bullet so the
        // engine never sees an invalid radius (it errors per bullet).
        const double radius = orbiting_radius_linear_enabled
                ? MAX(orbiting_radius_start + orbiting_radius_step * i, 0.01)
                : orbiting_radius;
        if (!volley->bullet_is_orbiting_enabled(i)) {
            volley->bullet_enable_orbiting(i, (real_t)radius, orbiting_direction, orbiting_texture_rotation);
        } else {
            volley->bullet_set_orbiting_radius(i, (real_t)radius);
            volley->bullet_set_orbiting_direction(i, orbiting_direction);
            volley->bullet_set_orbiting_texture_rotation(i, orbiting_texture_rotation);
        }
    }
}

void BulletSpawner2D::apply_volley_homing_and_orbiting(DirectionalBullets2D *bullets) {
    if (bullets == nullptr) {
        return;
    }
    if (!homing_enabled && !orbiting_enabled) {
        return;
    }
    // Orbiting locks onto a homing target: without homing there is nothing
    // to orbit, so skip it outright instead of arming a dead feature.
    if (orbiting_enabled && !homing_enabled) {
        UtilityFunctions::push_warning("BulletSpawner2D: orbiting_enabled needs homing_enabled (orbiting locks onto a homing target). Volley flies without orbiting.");
    }
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
            // Quiet + warn-once: a missing enemy roster must not spam once
            // per volley while the spawner keeps firing plain bullets.
            resolved_targets = resolve_homing_targets(true);
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
    // Flat post-spawn nudge (muzzle offsets, whole-volley follows). Runs
    // through the engine teleport path so shapes, attachments, and
    // interpolation stay consistent.
    if (spawn_position_offset != Vector2(0, 0)) {
        bullets->teleport_shift_all_bullets(spawn_position_offset);
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
    if (homing_debug_log_volleys && homing_enabled) {
        String first_desc = "mouse cursor";
        if (homing_target_source != HOMING_SOURCE_MOUSE) {
            if (resolved_targets.is_empty()) {
                first_desc = "none (plain volley)";
            } else {
                const Variant &first = resolved_targets[0];
                Node2D *first_node = Object::cast_to<Node2D>(first);
                // The target may have been freed by a re-entrant handler
                // between resolution and this log line: same guard pattern
                // as is_tracked_node_alive(), describe instead of touching.
                if (first_node != nullptr && UtilityFunctions::is_instance_id_valid(first_node->get_instance_id())) {
                    first_desc = String("'") + String(first_node->get_name()) + "' at " + UtilityFunctions::str(first_node->get_global_position());
                } else if (first_node != nullptr) {
                    first_desc = "target freed mid-volley";
                } else {
                    first_desc = UtilityFunctions::str(first);
                }
            }
        }
        UtilityFunctions::print("BulletSpawner2D: volley ", volleys_fired + 1, " homing targets: ", resolved_targets.size(), ", first: ", first_desc);
    }
    emit_signal("homing_targets_resolved", bullets, resolved_targets);
    // volleys_fired still holds the previous count here (shoot_once bumps it
    // right after), so +1 names the volley being configured.
    emit_signal("volley_homing_configured", bullets, volleys_fired + 1);
}

TypedArray<Transform2D> BulletSpawner2D::collect_spawn_transforms() const {
    return collect_spawn_transforms_impl(false);
}

TypedArray<Transform2D> BulletSpawner2D::collect_spawn_transforms_impl(bool quiet) const {
    Node2D *base = get_effective_generator();
    if (base == nullptr || !base->is_inside_tree()) {
        // Outside the tree there is no valid global transform; report loudly
        // for real shots, stay silent for the preview.
        if (!quiet) {
            UtilityFunctions::push_error("BulletSpawner2D::collect_spawn_transforms: spawner is outside the scene tree.");
        }
        return TypedArray<Transform2D>();
    }
    const Vector2 base_origin = base->get_global_transform().get_origin();
    const Transform2D marker = base->get_global_transform();
    TypedArray<Transform2D> raw;
    switch (transforms_source) {
        case TRANSFORMS_FROM_SELF:
            raw.push_back(marker);
            break;
        case TRANSFORMS_FROM_HELPER_GRID:
            raw = BulletFactory2D::helper_generate_transforms_grid(helper_bullets_amount, marker, helper_grid_rows_per_column, (BulletFactory2D::Alignment)helper_grid_alignment, helper_grid_column_offset, helper_grid_row_offset, helper_grid_rotate_with_marker, helper_grid_random_local_rotation, helper_grid_jitter);
            break;
        case TRANSFORMS_FROM_HELPER_RING:
            raw = BulletFactory2D::helper_generate_transforms_ring(helper_bullets_amount, marker, helper_ring_radius, helper_ring_start_angle, helper_ring_arc, helper_ring_rotate_with_marker, helper_ring_random_rotation, helper_ring_face_outward, helper_ring_y_scale, helper_ring_facing_offset_deg);
            break;
        case TRANSFORMS_FROM_HELPER_FAN:
            raw = BulletFactory2D::helper_generate_transforms_fan(helper_bullets_amount, marker, helper_fan_spread, helper_fan_direction_angle, helper_fan_step_offset, helper_fan_centered);
            break;
        case TRANSFORMS_FROM_HELPER_SPIRAL:
            raw = BulletFactory2D::helper_generate_transforms_spiral(helper_bullets_amount, marker, helper_spiral_start_radius, helper_spiral_radius_step, helper_spiral_angle_step, helper_spiral_rotate_with_marker, (BulletFactory2D::SpiralFacingMode)helper_spiral_facing, helper_spiral_facing_offset_deg);
            break;
        case TRANSFORMS_FROM_HELPER_LINE:
            raw = BulletFactory2D::helper_generate_transforms_line(helper_bullets_amount, marker, helper_line_direction, helper_line_spacing, helper_line_face_direction, (BulletFactory2D::LineAnchor)helper_line_anchor, helper_line_perpendicular);
            break;
        case TRANSFORMS_FROM_HELPER_AIMED: {
            Node2D *target = get_helper_aimed_target();
            if (target == nullptr) {
                if (!quiet) {
                    UtilityFunctions::push_error("BulletSpawner2D::collect_spawn_transforms: no aimed target assigned (helper_aimed_target_path).");
                }
                break;
            }
            raw = BulletFactory2D::helper_generate_transforms_aimed(helper_bullets_amount, marker, target->get_global_transform().get_origin(), helper_aimed_spread, helper_aimed_step_offset, helper_aimed_centered);
            break;
        }
        case TRANSFORMS_FROM_CHILDREN:
        default: {
            bool collected = false;
            for (int i = 0; i < base->get_child_count(); ++i) {
                Node2D *as_2d = Object::cast_to<Node2D>(base->get_child(i));
                // The editor preview holder is a Node2D child too, but it is
                // visualization only and must never become a spawn marker.
                if (as_2d != nullptr && !as_2d->has_meta(PREVIEW_META_KEY)) {
                    raw.push_back(as_2d->get_global_transform());
                    collected = true;
                }
            }
            if (!collected) {
                raw.push_back(marker);
            }
            break;
        }
    }
    // Spin first, then scale: both pivot around the generator origin, so a
    // spinning emitter orbits positions and turns facings together while the
    // scale pass keeps working exactly as before (identity at 1.0).
    const real_t spin_radians = Math::deg_to_rad((real_t)spin_angle_deg);
    const real_t scale = (real_t)transforms_scale;
    TypedArray<Transform2D> transforms;
    for (int i = 0; i < raw.size(); ++i) {
        Transform2D t = raw[i];
        t = rotate_spawn_transform(t, base_origin, spin_radians);
        transforms.push_back(scale_spawn_transform(t, base_origin, scale));
    }
    return transforms;
}

bool BulletSpawner2D::preview_allowed_here() const {
    if (!is_inside_tree()) {
        return false;
    }
    if (Engine::get_singleton()->is_editor_hint()) {
        return true;
    }
    return show_preview_during_runtime;
}

bool BulletSpawner2D::preview_active() const {
    return show_pattern_preview && preview_allowed_here();
}

// Snapshots the effective generator (same fallback as the collect path) plus
// the aimed target, storing ids and global transforms. Never assumes an old
// pointer: everything is freshly resolved from paths here.
void BulletSpawner2D::snapshot_preview_sources() {
    tracked_self_global = get_global_transform();
    tracked_has_self = true;
    tracked_base = get_effective_generator();
    if (tracked_base == nullptr) {
        tracked_base_id = 0;
        tracked_has_base_global = false;
    } else {
        tracked_base_id = tracked_base->get_instance_id();
        tracked_base_global = tracked_base->get_global_transform();
        tracked_has_base_global = true;
    }
    tracked_spin_angle = spin_angle_deg;
    tracked_target = nullptr;
    tracked_target_id = 0;
    tracked_has_target_origin = false;
    if (transforms_source == TRANSFORMS_FROM_HELPER_AIMED) {
        Node2D *target = get_helper_aimed_target();
        if (target != nullptr) {
            tracked_target = target;
            tracked_target_id = target->get_instance_id();
            tracked_target_origin = target->get_global_transform();
            tracked_has_target_origin = true;
        }
    }
    tracked_marker_origins.clear();
    tracked_marker_rots.clear();
    tracked_marker_ids.clear();
    tracked_child_count = -1;
    if (!is_inside_tree() || tracked_base == nullptr) {
        return;
    }
    // Marker slots: origin + rotation + id per child index. A non-Node2D
    // child and a freed-then-reused slot both compare unequal (dirty), which
    // triggers a rebuild that re-resolves - never a dereference of garbage.
    tracked_child_count = tracked_base->get_child_count();
    tracked_marker_origins.resize(tracked_child_count);
    tracked_marker_rots.resize(tracked_child_count);
    // Two int64 entries per child (low/high 32 bits): instance ids don't fit
    // a double bit-exactly, and float packing would alias distinct objects.
    tracked_marker_ids.resize(tracked_child_count * 2);
    for (int i = 0; i < tracked_child_count; i++) {
        Node2D *marker = Object::cast_to<Node2D>(tracked_base->get_child(i));
        if (marker == nullptr) {
            tracked_marker_origins[i] = Vector2(Math::INF, Math::INF);
            tracked_marker_rots[i] = Math::INF;
            tracked_marker_ids[i * 2] = 0;
            tracked_marker_ids[i * 2 + 1] = 0;
            continue;
        }
        tracked_marker_origins[i] = marker->get_global_transform().get_origin();
        tracked_marker_rots[i] = marker->get_global_rotation();
        const uint64_t id = marker->get_instance_id();
        tracked_marker_ids[i * 2] = (int64_t)(id & 0xFFFFFFFFu);
        tracked_marker_ids[i * 2 + 1] = (int64_t)(id >> 32);
    }
}

void BulletSpawner2D::rebuild_preview() {
    // Preview may exist in the editor (toggle decides) and at runtime only
    // when the user opted in via show_preview_during_runtime. Never before
    // the node is inside the tree (scene load sets properties first).
    if (!preview_allowed_here()) {
        return;
    }
    Node2D *effective_base = get_effective_generator();
    if (effective_base == nullptr) {
        return;
    }
    // Removes a preview holder child from a parent without trusting caches.
    // Returns true when something was removed.
    auto remove_holder_from = [&](Node *parent) -> bool {
        if (parent == nullptr) {
            return false;
        }
        Node *live = parent->get_node_or_null(NodePath(PREVIEW_HOLDER_NAME));
        if (live == nullptr) {
            return false;
        }
        parent->remove_child(live);
        memdelete(live);
        return true;
    };
    if (!show_pattern_preview) {
        remove_holder_from(this);
        if (effective_base != this) {
            remove_holder_from(effective_base);
        }
        // The generator may have been retargeted earlier: also clear a holder
        // left under the previously tracked base.
        if (tracked_base != nullptr && tracked_base != this && tracked_base != effective_base &&
                is_tracked_node_alive(tracked_base, tracked_base_id)) {
            remove_holder_from(tracked_base);
        }
        preview_holder = nullptr;
        preview_dots_layer = nullptr;
        preview_arrows_layer = nullptr;
        return;
    }
    // Preview lives under the effective generator so patterns are drawn where
    // they spawn. Heal from the tree every rebuild: never trust cached
    // pointers (undo/redo, duplication, scene reload can drop nodes).
    preview_holder = Object::cast_to<Node2D>(effective_base->get_node_or_null(NodePath(PREVIEW_HOLDER_NAME)));
    if (preview_holder == nullptr) {
        // Generator was retargeted: drop the stale holder under the spawner
        // (or under the previously tracked base) so only one preview exists.
        remove_holder_from(this);
        if (tracked_base != nullptr && tracked_base != this && tracked_base != effective_base &&
                is_tracked_node_alive(tracked_base, tracked_base_id)) {
            remove_holder_from(tracked_base);
        }
        preview_holder = Object::cast_to<Node2D>(effective_base->get_node_or_null(NodePath(PREVIEW_HOLDER_NAME)));
    }
    if (preview_holder == nullptr) {
        // Owner-less on purpose: never written to the scene, never exported.
        preview_holder = memnew(Node2D);
        preview_holder->set_name(PREVIEW_HOLDER_NAME);
        preview_holder->set_meta(PREVIEW_META_KEY, true);
        effective_base->add_child(preview_holder);
    } else if (preview_holder->get_parent() != effective_base) {
        // Healed a holder from the wrong parent (e.g. duplicated subtree).
        Node *old_parent = preview_holder->get_parent();
        if (old_parent != nullptr) {
            old_parent->remove_child(preview_holder);
        }
        effective_base->add_child(preview_holder);
    }
    // Legacy cleanup first: older versions drew the preview with Line2D strips
    // ("Dots"/"Ticks"). Remove any leftover before adopting/creating the
    // plain Node2D layers, so a healed holder can never clash on child names
    // and upgraded scenes keep no stray-segment nodes around.
    TypedArray<Node> legacy = preview_holder->find_children("*", "Line2D", false, false);
    for (int i = 0; i < legacy.size(); i++) {
        Node *stray = Object::cast_to<Node>(legacy[i]);
        if (stray != nullptr) {
            preview_holder->remove_child(stray);
            memdelete(stray);
        }
    }
    // Layers are likewise re-resolved from the tree every rebuild, so an
    // externally removed layer is recreated instead of silently killing the
    // preview. Only 3 nodes total regardless of bullet count.
    preview_dots_layer = Object::cast_to<PatternPreviewLayer2D>(preview_holder->get_node_or_null(NodePath("Dots")));
    if (preview_dots_layer == nullptr) {
        // Wrong-type leftover (plain Node2D from the RenderingServer build):
        // drop it so the typed layer can take the canonical name.
        Node *legacy_dots = preview_holder->get_node_or_null(NodePath("Dots"));
        if (legacy_dots != nullptr) {
            preview_holder->remove_child(legacy_dots);
            memdelete(legacy_dots);
        }
        preview_dots_layer = memnew(PatternPreviewLayer2D);
        preview_dots_layer->set_name("Dots");
        preview_dots_layer->kind = PatternPreviewLayer2D::LAYER_DOTS;
        preview_dots_layer->set_z_index(4000);
        preview_holder->add_child(preview_dots_layer);
    }
    preview_arrows_layer = Object::cast_to<PatternPreviewLayer2D>(preview_holder->get_node_or_null(NodePath("Arrows")));
    if (preview_arrows_layer == nullptr) {
        // Same wrong-type migration as Dots above.
        Node *legacy_arrows = preview_holder->get_node_or_null(NodePath("Arrows"));
        if (legacy_arrows != nullptr) {
            preview_holder->remove_child(legacy_arrows);
            memdelete(legacy_arrows);
        }
        preview_arrows_layer = memnew(PatternPreviewLayer2D);
        preview_arrows_layer->set_name("Arrows");
        preview_arrows_layer->kind = PatternPreviewLayer2D::LAYER_ARROWS;
        preview_arrows_layer->set_z_index(4000);
        preview_holder->add_child(preview_arrows_layer);
    }
    // Quiet collect: the preview must visualize, never scold (e.g. aimed
    // without a target simply draws nothing instead of erroring per rebuild).
    const TypedArray<Transform2D> transforms = collect_spawn_transforms_impl(true);
    const Transform2D holder_global = preview_holder->get_global_transform();
    const Transform2D to_local = holder_global.affine_inverse();
    const real_t holder_rotation = holder_global.get_rotation();
    // Snapshot into the layers: _draw() repaints this data on every engine
    // redraw by itself, so zoom/pan/selection/idle can never wipe the gizmo.
    // (One-shot RenderingServer canvas_item_add_* calls cannot do this: the
    // engine owns the command list and drops it on the next repaint.)
    PackedVector2Array dots;
    dots.resize(transforms.size());
    PackedVector2Array tails;
    tails.resize(transforms.size());
    PackedVector2Array dirs;
    dirs.resize(transforms.size());
    for (int i = 0; i < transforms.size(); ++i) {
        const Transform2D t = transforms[i];
        const Vector2 p = to_local.xform(t.get_origin());
        dots[i] = p;
        // Holder-local facing: strip the holder rotation so moving/rotating
        // the spawner does not skew the arrow. The tail starts outside the
        // dot (radius + gap); _draw() builds the shaft + head from there.
        const Vector2 dir = Vector2(1.0, 0.0).rotated(t.get_rotation() - holder_rotation);
        tails[i] = p + dir * (real_t)(preview_dot_radius + preview_arrow_gap);
        dirs[i] = dir;
    }
    preview_dots_layer->set_dots_data(dots, preview_dot_color, (float)preview_dot_radius);
    preview_arrows_layer->set_arrows_data(tails, dirs, preview_arrow_color, (float)preview_arrow_length, (float)preview_arrow_width, (float)preview_arrow_head_length, (float)preview_arrow_head_width);
    // The snapshot must match what was just drawn: dirty-checks compare
    // against this, so snap AFTER the collect, not before.
    snapshot_preview_sources();
}

// Compares live source state against the snapshot. Crash-safe by
// construction: cached pointers are NEVER dereferenced here. Everything is
// freshly resolved from paths (resolve_node_path / get_child); the stored
// ids only participate in integer comparisons. A freed node (or a slot
// reused by an unrelated object, caught via the id check) reports dirty and
// the rebuild re-resolves - never a touch of garbage memory.
bool BulletSpawner2D::preview_sources_dirty() {
    // No snapshot yet (first frames): dirty so the loop rebuilds + snaps.
    if (!tracked_has_self) {
        return true;
    }
    if (!is_inside_tree()) {
        return false;
    }
    if (get_global_transform() != tracked_self_global) {
        return true;
    }
    Node2D *base = get_effective_generator();
    if (base == nullptr) {
        return true;
    }
    if (base->get_instance_id() != tracked_base_id || !is_tracked_node_alive(base, tracked_base_id)) {
        return true;
    }
    if (!tracked_has_base_global || base->get_global_transform() != tracked_base_global) {
        return true;
    }
    if (spin_angle_deg != tracked_spin_angle) {
        return true;
    }
    if (transforms_source == TRANSFORMS_FROM_HELPER_AIMED) {
        Node2D *target = get_helper_aimed_target();
        const uint64_t target_id = target != nullptr ? target->get_instance_id() : 0;
        if (target_id != tracked_target_id) {
            return true;
        }
        if (target != nullptr) {
            if (!is_tracked_node_alive(target, tracked_target_id)) {
                return true;
            }
            if (tracked_has_target_origin && target->get_global_transform() != tracked_target_origin) {
                return true;
            }
        } else if (tracked_has_target_origin) {
            return true;
        }
    } else if (tracked_target_id != 0 || tracked_has_target_origin) {
        return true;
    }
    const int count = base->get_child_count();
    if (count != tracked_child_count) {
        return true;
    }
    if (tracked_marker_origins.size() != count || tracked_marker_rots.size() != count || tracked_marker_ids.size() != count * 2) {
        return true;
    }
    for (int i = 0; i < count; i++) {
        Node2D *marker = Object::cast_to<Node2D>(base->get_child(i));
        if (marker == nullptr) {
            if (tracked_marker_origins[i] != Vector2(Math::INF, Math::INF)) {
                return true;
            }
            continue;
        }
        // Identity first (catches freed + slot-reused objects), then pose:
        // a marker rotated exactly in place keeps its origin, so the
        // rotation check is what catches your inspector-rotation bug.
        const uint64_t id = marker->get_instance_id();
        if (tracked_marker_ids[i * 2] != (int64_t)(id & 0xFFFFFFFFu) || tracked_marker_ids[i * 2 + 1] != (int64_t)(id >> 32)) {
            return true;
        }
        if (marker->get_global_transform().get_origin() != tracked_marker_origins[i] || marker->get_global_rotation() != tracked_marker_rots[i]) {
            return true;
        }
    }
    return false;
}

void BulletSpawner2D::update_preview_process_state() {
    // Editor: processing runs only while the preview is on, so inspector
    // rotations, gizmo drags, and add/remove marker refresh live.
    // Runtime: never touched here - shooting/spinning own _process, and the
    // runtime preview piggy-backs that loop via preview_sources_dirty().
    if (Engine::get_singleton()->is_editor_hint() && is_inside_tree()) {
        if (show_pattern_preview) {
            set_process(true);
        } else {
            set_process(false);
        }
    }
}

void BulletSpawner2D::_validate_property(PropertyInfo &p_property) const {
    const String property_name = p_property.name;
    // Tidy inspector: hide the preview tuning knobs while both preview
    // switches are off. The toggles + spin props stay always visible.
    if (property_name.begins_with("preview_") && property_name != "show_pattern_preview") {
        if (!show_pattern_preview && !show_preview_during_runtime) {
            p_property.usage &= ~PROPERTY_USAGE_EDITOR;
        }
        return;
    }
    // Steering-group member under a movement name: only meaningful while the
    // homing steering block runs.
    if (property_name == "adjust_direction_based_on_rotation") {
        if (!homing_enabled) {
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
                } else if (property_name == "homing_auto_pop_after_target_reached") {
                    show = homing_mode == HOMING_PER_BULLET;
                } else if (property_name == "homing_per_bullet_smoothing_enabled") {
                    show = homing_mode == HOMING_PER_BULLET;
                } else if (property_name == "homing_smoothing_start" || property_name == "homing_smoothing_step") {
                    show = homing_mode == HOMING_PER_BULLET && homing_per_bullet_smoothing_enabled;
                } else if (property_name == "homing_retarget_interval_sec" || property_name == "homing_retarget_previous_volleys") {
                    show = homing_retarget_mode == HOMING_RETARGET_ON_INTERVAL;
                }
            }
        } else if (property_name == "shared_homing_auto_pop_after_target_reached") {
            show = homing_enabled && homing_mode == HOMING_SHARED;
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
        relevant = transforms_source == TRANSFORMS_FROM_HELPER_GRID;
    } else if (property_name.begins_with("helper_ring_")) {
        relevant = transforms_source == TRANSFORMS_FROM_HELPER_RING;
    } else if (property_name.begins_with("helper_fan_")) {
        relevant = transforms_source == TRANSFORMS_FROM_HELPER_FAN;
    } else if (property_name.begins_with("helper_spiral_")) {
        relevant = transforms_source == TRANSFORMS_FROM_HELPER_SPIRAL;
    } else if (property_name.begins_with("helper_line_")) {
        relevant = transforms_source == TRANSFORMS_FROM_HELPER_LINE;
    } else if (property_name.begins_with("helper_aimed_")) {
        relevant = transforms_source == TRANSFORMS_FROM_HELPER_AIMED;
    } else if (property_name == "helper_bullets_amount") {
        relevant = transforms_source >= TRANSFORMS_FROM_HELPER_GRID;
    }
    if (!relevant) {
        p_property.usage &= ~PROPERTY_USAGE_EDITOR;
    }
}

bool BulletSpawner2D::shoot_once() {
    BulletFactory2D *factory = get_bullet_factory();
    if (factory == nullptr) {
        UtilityFunctions::push_error("BulletSpawner2D::shoot_once: no BulletFactory2D assigned (bullet_factory_path).");
        return false;
    }
    if (spawn_data.is_null()) {
        UtilityFunctions::push_error("BulletSpawner2D::shoot_once: no spawn_data assigned.");
        return false;
    }
    // Duplicate per volley: the user's resource must never be mutated (its
    // transforms get overwritten below), so shared .tres files stay safe.
    Ref<DirectionalBulletsData2D> volley_data(Object::cast_to<DirectionalBulletsData2D>(spawn_data->duplicate().ptr()));
    if (volley_data.is_null()) {
        UtilityFunctions::push_error("BulletSpawner2D::shoot_once: could not duplicate spawn_data.");
        return false;
    }
    volley_data->set_transforms(collect_spawn_transforms());
    if (volley_data->get_transforms().is_empty()) {
        // Fail loud with the mode name: the generic factory "no transforms"
        // error alone never says which source misfired (unset aimed target,
        // rejected helper input, ...). The cause was already reported above.
        UtilityFunctions::push_error(String("BulletSpawner2D::shoot_once: transforms_source ") + transforms_source_name(transforms_source) + " produced no transforms, volley skipped.");
        return false;
    }
    DirectionalBullets2D *bullets = factory->spawn_controllable_directional_bullets(volley_data);
    if (bullets == nullptr) {
        return false; // Factory already reported why (busy/teardown/bad data).
    }
    // Tag the instance (fresh or pooled): from here on its area_entered,
    // body_entered and life_time_over signals are possessed by this spawner
    // instead of the factory. Pool reuse resets the tag, so this stamp covers
    // every spawn path through this function.
    bullets->owner_spawner_id = get_instance_id();
    // Homing/orbiting runs on the same stamp: the instance is fully
    // configured before volley_fired, so handlers observe live behavior.
    apply_volley_homing_and_orbiting(bullets);
    volleys_fired += 1;
    emit_signal("volley_fired", bullets, volleys_fired);
    // Exact-equality = transition only: further manual shots past the cap do
    // not re-emit, and the setter path reports its own transition.
    if (max_volleys >= 0 && volleys_fired == max_volleys) {
        // Stop shooting, but stay awake while spinning or previewing.
        set_process(spin_enabled || preview_active() || homing_retarget_active());
        emit_signal("shooting_finished");
    }
    return true;
}

void BulletSpawner2D::_ready() {
    // Same contract as BulletFactory2D::_ready: never arm shooting in the
    // editor. Extension nodes still get _ready/_process there, and without
    // this the shoot timer would fire against a factory that is deliberately
    // never ready outside the running game.
    if (Engine::get_singleton()->is_editor_hint()) {
        // Editor preview: self-transform notice for instant self moves +
        // the tracked-sources loop for markers/generator/target/children.
        set_notify_transform(true);
        update_preview_process_state();
        rebuild_preview();
        return;
    }
    // Runtime: the preview holder is owner-less and thus never saved, but a
    // stray may exist after "Play Scene" from a dirty editor state. Keep it
    // only when the user opted into a runtime preview; else drop it so
    // runtime is never affected.
    if (!preview_active()) {
        preview_holder = nullptr;
        preview_dots_layer = nullptr;
        preview_arrows_layer = nullptr;
        Node *stray = get_node_or_null(NodePath(PREVIEW_HOLDER_NAME));
        if (stray != nullptr) {
            remove_child(stray);
            memdelete(stray);
        }
        Node2D *base = get_effective_generator();
        if (base != nullptr && base != this) {
            Node *stray_base = base->get_node_or_null(NodePath(PREVIEW_HOLDER_NAME));
            if (stray_base != nullptr) {
                base->remove_child(stray_base);
                memdelete(stray_base);
            }
        }
    } else {
        set_notify_transform(true);
        rebuild_preview();
    }
    volleys_fired = 0;
    shoot_time_left = shoot_initial_delay_sec;
    if (preview_active()) {
        set_process(true);
    } else {
        set_process(auto_shooting_active() || spin_enabled || homing_retarget_active());
    }
    if (auto_shooting_active()) {
        emit_signal("shooting_started");
    }
}

void BulletSpawner2D::_notification(int p_what) {
    if (p_what == NOTIFICATION_LOCAL_TRANSFORM_CHANGED) {
        // Moving/rotating the spawner itself refreshes instantly.
        rebuild_preview();
    } else if (p_what == NOTIFICATION_CHILD_ORDER_CHANGED) {
        // Instant refresh for marker changes under this spawner. Markers under
        // an external generator are picked up by the _process dirty-check
        // instead (no notification arrives here for another node's children).
        if (is_inside_tree()) {
            rebuild_preview();
            // Rebuild already re-snapshots; the loop below stays in sync.
        }
    } else if (p_what == NOTIFICATION_EXIT_TREE) {
        // Leaving the tree (scene change, quit): drop caches so a later
        // _ready starts clean. The holder is a child and frees itself; only
        // null the pointers, never memdelete detached nodes here.
        preview_holder = nullptr;
        preview_dots_layer = nullptr;
        preview_arrows_layer = nullptr;
        tracked_base = nullptr;
        tracked_base_id = 0;
        tracked_has_base_global = false;
        tracked_spin_angle = 0.0;
        tracked_target = nullptr;
        tracked_target_id = 0;
        tracked_has_target_origin = false;
        tracked_marker_origins.clear();
        tracked_marker_rots.clear();
        tracked_marker_ids.clear();
        tracked_child_count = -1;
        tracked_has_self = false;
        // Homing: forget tracked volleys (plain ids, no ownership of nodes),
        // reset the round-robin cursor, and re-arm the retarget pass so a
        // scene change starts clean instead of inheriting stale rotation.
        live_volley_instance_ids.clear();
        homing_round_robin_cursor = 0;
        homing_retarget_time_left = 0.0;
    }
}

void BulletSpawner2D::_process(double delta) {
    // Live-refresh loop for the preview: any tracked source move/add/remove
    // rebuilds at once (inspector rotation, gizmo drag, new marker, undo).
    // Crash-safe: preview_sources_dirty() never dereferences a stale node.
    if (preview_active() && preview_sources_dirty()) {
        rebuild_preview();
    }
    // Self-healing: even if processing gets enabled in the editor somehow
    // (e.g. set_shooting_enabled(true) from an editor dock), never shoot.
    if (Engine::get_singleton()->is_editor_hint()) {
        return;
    }
    if (!Math::is_finite(delta) || delta < 0.0) {
        return;
    }
    if (delta > 0.5) {
        delta = 0.5; // clamp hitch spikes so one stall can't fast-forward volleys
    }
    // Spin runs independently of shooting: a silent rotating emitter is valid.
    if (spin_enabled) {
        advance_spin(delta);
    }
    // Interval retargeting runs independently of shooting too: manual
    // shoot_once() volleys keep chasing fresh targets while auto-shooting
    // stays off.
    if (homing_retarget_active()) {
        homing_retarget_time_left -= delta;
        if (homing_retarget_time_left <= 0.0) {
            retarget_live_volleys();
            homing_retarget_time_left = homing_retarget_interval_sec;
        }
    }
    if (!auto_shooting_active()) {
        // Sleep when there is nothing to do, but stay awake while spinning,
        // retargeting, or while the runtime preview loop must keep refreshing.
        set_process(spin_enabled || preview_active() || homing_retarget_active());
        return;
    }
    shoot_time_left -= delta;
    if (shoot_time_left > 0.0) {
        return;
    }
    shoot_once(); // errors, if any, are per-attempt (interval-gated, no spam storm)
    shoot_time_left = shoot_interval_sec;
}


void BulletSpawner2D::_bind_methods() {
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
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "spawn_data", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBulletsData2D"), "set_spawn_data", "get_spawn_data");

	// Spawner lifecycle signals. Emitted synchronously where the transition
	// happens (timer tick, setters, reset, _ready): handlers run with live
	// state and follow the same contract as the factory collision signals -
	// game logic is safe directly, structural factory calls must be deferred.
	// NOTE: PROPERTY_HINT_RESOURCE_TYPE (not NODE_TYPE) carries the class name
	// to ClassDB/--doctool; see the note on the factory signals.
	ADD_SIGNAL(MethodInfo("volley_fired",
		PropertyInfo(Variant::OBJECT, "directional_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBullets2D"),
		PropertyInfo(Variant::INT, "volley_index")));
	ADD_SIGNAL(MethodInfo("shooting_started"));
	ADD_SIGNAL(MethodInfo("shooting_stopped"));
	ADD_SIGNAL(MethodInfo("shooting_finished"));

	// Collision/lifetime signals possessed by this spawner for the volleys it
	// spawned (see owner_spawner_id). Same slim payload shape as the factory
	// typed signals; emitted synchronously (area/body) or deferred
	// (life_time_over) under the same handler contract. If this spawner is
	// gone, its bullets gracefully fall back to the factory signals.
	// NOTE: PROPERTY_HINT_RESOURCE_TYPE (not NODE_TYPE) carries the class name
	// to ClassDB/--doctool; see the note on the factory signals.
	ADD_SIGNAL(MethodInfo("area_entered",
		PropertyInfo(Variant::OBJECT, "hit_target_area"),
		PropertyInfo(Variant::OBJECT, "directional_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBullets2D"),
		PropertyInfo(Variant::INT, "bullet_index")));
	ADD_SIGNAL(MethodInfo("body_entered",
		PropertyInfo(Variant::OBJECT, "hit_target_body"),
		PropertyInfo(Variant::OBJECT, "directional_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBullets2D"),
		PropertyInfo(Variant::INT, "bullet_index")));
	ADD_SIGNAL(MethodInfo("life_time_over",
		PropertyInfo(Variant::OBJECT, "directional_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBullets2D"),
		PropertyInfo(Variant::ARRAY, "bullet_indexes", PROPERTY_HINT_ARRAY_TYPE, "int")));

	ClassDB::bind_method(D_METHOD("get_shooting_enabled"), &BulletSpawner2D::get_shooting_enabled);
	ClassDB::bind_method(D_METHOD("set_shooting_enabled", "value"), &BulletSpawner2D::set_shooting_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shooting_enabled"), "set_shooting_enabled", "get_shooting_enabled");

	ClassDB::bind_method(D_METHOD("get_shoot_interval_sec"), &BulletSpawner2D::get_shoot_interval_sec);
	ClassDB::bind_method(D_METHOD("set_shoot_interval_sec", "value"), &BulletSpawner2D::set_shoot_interval_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shoot_interval_sec"), "set_shoot_interval_sec", "get_shoot_interval_sec");

	ClassDB::bind_method(D_METHOD("get_shoot_initial_delay_sec"), &BulletSpawner2D::get_shoot_initial_delay_sec);
	ClassDB::bind_method(D_METHOD("set_shoot_initial_delay_sec", "value"), &BulletSpawner2D::set_shoot_initial_delay_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shoot_initial_delay_sec"), "set_shoot_initial_delay_sec", "get_shoot_initial_delay_sec");

	ClassDB::bind_method(D_METHOD("get_max_volleys"), &BulletSpawner2D::get_max_volleys);
	ClassDB::bind_method(D_METHOD("set_max_volleys", "value"), &BulletSpawner2D::set_max_volleys);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_volleys"), "set_max_volleys", "get_max_volleys");

	ClassDB::bind_method(D_METHOD("get_transforms_scale"), &BulletSpawner2D::get_transforms_scale);
	ClassDB::bind_method(D_METHOD("set_transforms_scale", "value"), &BulletSpawner2D::set_transforms_scale);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "transforms_scale"), "set_transforms_scale", "get_transforms_scale");

	ClassDB::bind_method(D_METHOD("get_spawn_position_offset"), &BulletSpawner2D::get_spawn_position_offset);
	ClassDB::bind_method(D_METHOD("set_spawn_position_offset", "value"), &BulletSpawner2D::set_spawn_position_offset);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "spawn_position_offset"), "set_spawn_position_offset", "get_spawn_position_offset");

	ClassDB::bind_method(D_METHOD("get_spin_enabled"), &BulletSpawner2D::get_spin_enabled);
	ClassDB::bind_method(D_METHOD("set_spin_enabled", "value"), &BulletSpawner2D::set_spin_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "spin_enabled"), "set_spin_enabled", "get_spin_enabled");

	ClassDB::bind_method(D_METHOD("get_spin_speed_deg_per_sec"), &BulletSpawner2D::get_spin_speed_deg_per_sec);
	ClassDB::bind_method(D_METHOD("set_spin_speed_deg_per_sec", "value"), &BulletSpawner2D::set_spin_speed_deg_per_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spin_speed_deg_per_sec"), "set_spin_speed_deg_per_sec", "get_spin_speed_deg_per_sec");

	ClassDB::bind_method(D_METHOD("get_spin_mode"), &BulletSpawner2D::get_spin_mode);
	ClassDB::bind_method(D_METHOD("set_spin_mode", "value"), &BulletSpawner2D::set_spin_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "spin_mode", PROPERTY_HINT_ENUM, "Continuous,Oscillate"), "set_spin_mode", "get_spin_mode");

	ClassDB::bind_method(D_METHOD("get_spin_amplitude_deg"), &BulletSpawner2D::get_spin_amplitude_deg);
	ClassDB::bind_method(D_METHOD("set_spin_amplitude_deg", "value"), &BulletSpawner2D::set_spin_amplitude_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spin_amplitude_deg"), "set_spin_amplitude_deg", "get_spin_amplitude_deg");

	ClassDB::bind_method(D_METHOD("get_spin_frequency_hz"), &BulletSpawner2D::get_spin_frequency_hz);
	ClassDB::bind_method(D_METHOD("set_spin_frequency_hz", "value"), &BulletSpawner2D::set_spin_frequency_hz);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spin_frequency_hz"), "set_spin_frequency_hz", "get_spin_frequency_hz");

	ClassDB::bind_method(D_METHOD("get_spin_angle_deg"), &BulletSpawner2D::get_spin_angle_deg);
	ClassDB::bind_method(D_METHOD("is_spinning"), &BulletSpawner2D::is_spinning);
	ClassDB::bind_method(D_METHOD("start_spinning"), &BulletSpawner2D::start_spinning);
	ClassDB::bind_method(D_METHOD("stop_spinning"), &BulletSpawner2D::stop_spinning);
	ClassDB::bind_method(D_METHOD("reset_spin_angle"), &BulletSpawner2D::reset_spin_angle);

	ClassDB::bind_method(D_METHOD("get_transforms_source"), &BulletSpawner2D::get_transforms_source);
	ClassDB::bind_method(D_METHOD("set_transforms_source", "value"), &BulletSpawner2D::set_transforms_source);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "transforms_source", PROPERTY_HINT_ENUM, "From Children,From Self,Grid,Ring,Fan,Spiral,Line,Aimed"), "set_transforms_source", "get_transforms_source");

	ClassDB::bind_method(D_METHOD("get_helper_bullets_amount"), &BulletSpawner2D::get_helper_bullets_amount);
	ClassDB::bind_method(D_METHOD("set_helper_bullets_amount", "value"), &BulletSpawner2D::set_helper_bullets_amount);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_bullets_amount"), "set_helper_bullets_amount", "get_helper_bullets_amount");

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

	ClassDB::bind_method(D_METHOD("get_helper_ring_face_outward"), &BulletSpawner2D::get_helper_ring_face_outward);
	ClassDB::bind_method(D_METHOD("set_helper_ring_face_outward", "value"), &BulletSpawner2D::set_helper_ring_face_outward);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_ring_face_outward"), "set_helper_ring_face_outward", "get_helper_ring_face_outward");

	ClassDB::bind_method(D_METHOD("get_helper_ring_y_scale"), &BulletSpawner2D::get_helper_ring_y_scale);
	ClassDB::bind_method(D_METHOD("set_helper_ring_y_scale", "value"), &BulletSpawner2D::set_helper_ring_y_scale);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_ring_y_scale"), "set_helper_ring_y_scale", "get_helper_ring_y_scale");

	ClassDB::bind_method(D_METHOD("get_helper_ring_facing_offset_deg"), &BulletSpawner2D::get_helper_ring_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_ring_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_ring_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_ring_facing_offset_deg"), "set_helper_ring_facing_offset_deg", "get_helper_ring_facing_offset_deg");

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

	ClassDB::bind_method(D_METHOD("get_helper_line_perpendicular"), &BulletSpawner2D::get_helper_line_perpendicular);
	ClassDB::bind_method(D_METHOD("set_helper_line_perpendicular", "value"), &BulletSpawner2D::set_helper_line_perpendicular);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_line_perpendicular"), "set_helper_line_perpendicular", "get_helper_line_perpendicular");

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

	// Homing + orbiting signals. Same slim payload shape and handler contract
	// as the spawner lifecycle signals above: volley_homing_configured and
	// homing_targets_resolved fire synchronously inside shoot_once() (after
	// the instance is fully configured, before volley_fired);
	// volley_bullet_homing_target_reached is a synchronous forward of the
	// instance's deferred bullet_homing_target_reached.
	// NOTE: PROPERTY_HINT_RESOURCE_TYPE (not NODE_TYPE) carries the class name
	// to ClassDB/--doctool; see the note on the factory signals.
	ADD_SIGNAL(MethodInfo("volley_homing_configured",
		PropertyInfo(Variant::OBJECT, "directional_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBullets2D"),
		PropertyInfo(Variant::INT, "volley_index")));
	ADD_SIGNAL(MethodInfo("homing_targets_resolved",
		PropertyInfo(Variant::OBJECT, "directional_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBullets2D"),
		PropertyInfo(Variant::ARRAY, "targets")));
	ADD_SIGNAL(MethodInfo("volley_bullet_homing_target_reached",
		PropertyInfo(Variant::OBJECT, "directional_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBullets2D"),
		PropertyInfo(Variant::INT, "bullet_index"),
		PropertyInfo(Variant::OBJECT, "target", PROPERTY_HINT_RESOURCE_TYPE, "Node2D"),
		PropertyInfo(Variant::VECTOR2, "target_global_position")));

	ClassDB::bind_method(D_METHOD("get_homing_enabled"), &BulletSpawner2D::get_homing_enabled);
	ClassDB::bind_method(D_METHOD("set_homing_enabled", "value"), &BulletSpawner2D::set_homing_enabled);
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

	ClassDB::bind_method(D_METHOD("get_adjust_direction_based_on_rotation"), &BulletSpawner2D::get_adjust_direction_based_on_rotation);
	ClassDB::bind_method(D_METHOD("set_adjust_direction_based_on_rotation", "value"), &BulletSpawner2D::set_adjust_direction_based_on_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "adjust_direction_based_on_rotation"), "set_adjust_direction_based_on_rotation", "get_adjust_direction_based_on_rotation");

	ClassDB::bind_method(D_METHOD("get_homing_auto_pop_after_target_reached"), &BulletSpawner2D::get_homing_auto_pop_after_target_reached);
	ClassDB::bind_method(D_METHOD("set_homing_auto_pop_after_target_reached", "value"), &BulletSpawner2D::set_homing_auto_pop_after_target_reached);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_auto_pop_after_target_reached"), "set_homing_auto_pop_after_target_reached", "get_homing_auto_pop_after_target_reached");

	ClassDB::bind_method(D_METHOD("get_shared_homing_auto_pop_after_target_reached"), &BulletSpawner2D::get_shared_homing_auto_pop_after_target_reached);
	ClassDB::bind_method(D_METHOD("set_shared_homing_auto_pop_after_target_reached", "value"), &BulletSpawner2D::set_shared_homing_auto_pop_after_target_reached);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shared_homing_auto_pop_after_target_reached"), "set_shared_homing_auto_pop_after_target_reached", "get_shared_homing_auto_pop_after_target_reached");

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

	ClassDB::bind_method(D_METHOD("get_homing_retarget_previous_volleys"), &BulletSpawner2D::get_homing_retarget_previous_volleys);
	ClassDB::bind_method(D_METHOD("set_homing_retarget_previous_volleys", "value"), &BulletSpawner2D::set_homing_retarget_previous_volleys);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_retarget_previous_volleys"), "set_homing_retarget_previous_volleys", "get_homing_retarget_previous_volleys");

	ClassDB::bind_method(D_METHOD("get_homing_debug_log_volleys"), &BulletSpawner2D::get_homing_debug_log_volleys);
	ClassDB::bind_method(D_METHOD("set_homing_debug_log_volleys", "value"), &BulletSpawner2D::set_homing_debug_log_volleys);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_debug_log_volleys"), "set_homing_debug_log_volleys", "get_homing_debug_log_volleys");

	ClassDB::bind_method(D_METHOD("get_orbiting_enabled"), &BulletSpawner2D::get_orbiting_enabled);
	ClassDB::bind_method(D_METHOD("set_orbiting_enabled", "value"), &BulletSpawner2D::set_orbiting_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "orbiting_enabled"), "set_orbiting_enabled", "get_orbiting_enabled");

	ClassDB::bind_method(D_METHOD("get_orbiting_radius"), &BulletSpawner2D::get_orbiting_radius);
	ClassDB::bind_method(D_METHOD("set_orbiting_radius", "value"), &BulletSpawner2D::set_orbiting_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "orbiting_radius"), "set_orbiting_radius", "get_orbiting_radius");

	ClassDB::bind_method(D_METHOD("get_orbiting_direction"), &BulletSpawner2D::get_orbiting_direction);
	ClassDB::bind_method(D_METHOD("set_orbiting_direction", "value"), &BulletSpawner2D::set_orbiting_direction);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "orbiting_direction", PROPERTY_HINT_ENUM, "Dont Move,Orbit Left,Orbit Right"), "set_orbiting_direction", "get_orbiting_direction");

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

	ClassDB::bind_method(D_METHOD("resolve_homing_targets", "quiet", "advance_round_robin"), &BulletSpawner2D::resolve_homing_targets, DEFVAL(false), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("retarget_live_volleys"), &BulletSpawner2D::retarget_live_volleys);
	ClassDB::bind_method(D_METHOD("get_live_volley_count"), &BulletSpawner2D::get_live_volley_count);
	ClassDB::bind_method(D_METHOD("get_live_volleys"), &BulletSpawner2D::get_live_volleys);
	ClassDB::bind_method(D_METHOD("clear_live_volleys"), &BulletSpawner2D::clear_live_volleys);
	ClassDB::bind_method(D_METHOD("adopt_live_volley", "directional_bullets_instance"), &BulletSpawner2D::adopt_live_volley);
	ClassDB::bind_method(D_METHOD("clear_live_volleys_homing"), &BulletSpawner2D::clear_live_volleys_homing);
	ClassDB::bind_method(D_METHOD("override_live_volleys_velocity", "new_velocity"), &BulletSpawner2D::override_live_volleys_velocity);
	ClassDB::bind_method(D_METHOD("_on_volley_bullet_homing_target_reached", "directional_bullets_instance", "bullet_index", "target", "target_global_position"), &BulletSpawner2D::_on_volley_bullet_homing_target_reached);

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

	// Need this in order to expose the enum constants to Godot Engine
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_CHILDREN);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_SELF);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_GRID);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_RING);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_FAN);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_SPIRAL);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_LINE);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_AIMED);

	// Need this in order to expose the enum constants to Godot Engine
	BIND_ENUM_CONSTANT(SPIN_CONTINUOUS);
	BIND_ENUM_CONSTANT(SPIN_OSCILLATE);

	ClassDB::bind_method(D_METHOD("get_volleys_fired"), &BulletSpawner2D::get_volleys_fired);
	ClassDB::bind_method(D_METHOD("collect_spawn_transforms"), &BulletSpawner2D::collect_spawn_transforms);
	ClassDB::bind_method(D_METHOD("shoot_once"), &BulletSpawner2D::shoot_once);
	ClassDB::bind_method(D_METHOD("reset_shooting"), &BulletSpawner2D::reset_shooting);

}

}
