#include "bullet_spawner2d.hpp"
#include "bullets/directional_bullets2d.hpp"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/global_constants.hpp"
#include "godot_cpp/core/class_db.hpp"
#include "godot_cpp/core/math.hpp"
#include "godot_cpp/core/object.hpp"
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
        set_process(now_active || spin_enabled);
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
        set_process(now_active || spin_enabled);
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

bool BulletSpawner2D::get_spin_enabled() const {
    return spin_enabled;
}
void BulletSpawner2D::set_spin_enabled(bool value) {
    spin_enabled = value;
    if (!Engine::get_singleton()->is_editor_hint() && is_inside_tree()) {
        // Spinning needs _process even when auto-shooting is off.
        set_process(spin_enabled || auto_shooting_active());
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
    if (is_inside_tree()) {
        set_process(auto_shooting_active() || spin_enabled);
    }
    // An explicit restart counts as a (re)start whenever it arms shooting.
    if (auto_shooting_active()) {
        emit_signal("shooting_started");
    }
}

TypedArray<Transform2D> BulletSpawner2D::collect_spawn_transforms() const {
    return collect_spawn_transforms_impl(false);
}

TypedArray<Transform2D> BulletSpawner2D::collect_spawn_transforms_impl(bool quiet) const {
    Node2D *base = get_transforms_generator();
    if (base == nullptr) {
        base = const_cast<BulletSpawner2D *>(this);
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

void BulletSpawner2D::rebuild_preview() {
    // Editor visualization only: never build anything at runtime, before the
    // node is inside the tree (scene load sets properties first), or off.
    if (!Engine::get_singleton()->is_editor_hint() || !is_inside_tree()) {
        return;
    }
    if (!show_pattern_preview) {
        // Re-resolve instead of trusting the cache: the editor may have
        // dropped the holder without us, and a stale pointer here would
        // leave a ghost preview behind (or crash on remove_child).
        Node *live = get_node_or_null(NodePath(PREVIEW_HOLDER_NAME));
        if (live != nullptr) {
            remove_child(live);
            memdelete(live);
        }
        preview_holder = nullptr;
        preview_dots_layer = nullptr;
        preview_arrows_layer = nullptr;
        return;
    }
    // Heal from the tree every rebuild: duplicating this node copies the
    // holder and its layers but not the cached pointers, and the editor can
    // drop the nodes without us (undo/redo, scene reload). The cache is only
    // ever assigned from a fresh lookup, never trusted, so a stale pointer
    // can never silently kill the preview.
    preview_holder = Object::cast_to<Node2D>(get_node_or_null(NodePath(PREVIEW_HOLDER_NAME)));
    if (preview_holder == nullptr) {
        // Owner-less on purpose: never written to the scene, never exported.
        preview_holder = memnew(Node2D);
        preview_holder->set_name(PREVIEW_HOLDER_NAME);
        preview_holder->set_meta(PREVIEW_META_KEY, true);
        add_child(preview_holder);
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
}

void BulletSpawner2D::_validate_property(PropertyInfo &p_property) const {
    const String property_name = p_property.name;
    // Tidy inspector: hide the preview tuning knobs while the preview itself
    // is off. The toggle + spin props stay always visible.
    if (property_name.begins_with("preview_") && property_name != "show_pattern_preview") {
        if (!show_pattern_preview) {
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
    volleys_fired += 1;
    emit_signal("volley_fired", bullets, volleys_fired);
    // Exact-equality = transition only: further manual shots past the cap do
    // not re-emit, and the setter path reports its own transition.
    if (max_volleys >= 0 && volleys_fired == max_volleys) {
        // Stop shooting, but stay awake while spinning.
        set_process(spin_enabled);
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
        set_process(false);
        // Editor preview follows the spawner's own transform live.
        set_notify_transform(true);
        rebuild_preview();
        return;
    }
    // Paranoia: the preview holder is owner-less and thus never saved, but
    // drop it if one is somehow present so runtime is never affected.
    preview_holder = nullptr;
    preview_dots_layer = nullptr;
    preview_arrows_layer = nullptr;
    Node *stray = get_node_or_null(NodePath(PREVIEW_HOLDER_NAME));
    if (stray != nullptr) {
        remove_child(stray);
        memdelete(stray);
    }
    volleys_fired = 0;
    shoot_time_left = shoot_initial_delay_sec;
    set_process(auto_shooting_active() || spin_enabled);
    if (auto_shooting_active()) {
        emit_signal("shooting_started");
    }
}

void BulletSpawner2D::_notification(int p_what) {
    if (p_what == NOTIFICATION_LOCAL_TRANSFORM_CHANGED) {
        // Editor: moving/rotating the spawner refreshes the preview.
        // Runtime: rebuild_preview() is a guarded no-op there.
        rebuild_preview();
    }
}

void BulletSpawner2D::_process(double delta) {
    // Self-healing: even if processing gets enabled in the editor somehow
    // (e.g. set_shooting_enabled(true) from an editor dock), never shoot.
    if (Engine::get_singleton()->is_editor_hint()) {
        set_process(false);
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
    if (!auto_shooting_active()) {
        // Sleep when there is nothing to do, but stay awake while spinning.
        set_process(spin_enabled);
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

	ClassDB::bind_method(D_METHOD("get_show_pattern_preview"), &BulletSpawner2D::get_show_pattern_preview);
	ClassDB::bind_method(D_METHOD("set_show_pattern_preview", "value"), &BulletSpawner2D::set_show_pattern_preview);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "show_pattern_preview"), "set_show_pattern_preview", "get_show_pattern_preview");

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
