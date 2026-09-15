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
#include "godot_cpp/variant/dictionary.hpp"
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
// bullet positions and turns facings together. Scale is preserved: only the
// rotation and the origin offset move. Zero rotation is a no-op.
static Transform2D rotate_spawn_transform(const Transform2D &t, const Vector2 &origin, real_t radians) {
    if (radians == 0.0) {
        return t;
    }
    const Vector2 rotated_offset = (t.get_origin() - origin).rotated(radians);
    Transform2D out(t.get_rotation() + radians, origin + rotated_offset);
    out.set_scale(t.get_scale());
    return out;
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

// Global shoot_once() nesting depth across ALL spawners sharing this module.
// The per-spawner latch stops self-recursion; this stops A->B->A ping-pong
// through signal handlers sharing one factory pool. Incremented alongside the
// latch, decremented at the same single clear-point after volley_fired + cap
// transition. File-local: never exposed, never persisted.
static int g_shoot_once_nesting_depth = 0;

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
        case BulletSpawner2D::TRANSFORMS_FROM_HELPER_FLOWER:
            return "Flower";
        case BulletSpawner2D::TRANSFORMS_FROM_HELPER_ELLIPSE:
            return "Ellipse";
        case BulletSpawner2D::TRANSFORMS_FROM_HELPER_RAIN:
            return "Rain";
        case BulletSpawner2D::TRANSFORMS_FROM_HELPER_SCATTER:
            return "Scatter";
        case BulletSpawner2D::TRANSFORMS_FROM_HELPER_POLYGON:
            return "Polygon";
        case BulletSpawner2D::TRANSFORMS_FROM_HELPER_MULTISPIRAL:
            return "Multi Spiral";
        case BulletSpawner2D::TRANSFORMS_FROM_HELPER_CROSS:
            return "Cross";
        case BulletSpawner2D::TRANSFORMS_FROM_HELPER_STAR:
            return "Star";
        case BulletSpawner2D::TRANSFORMS_FROM_HELPER_HEART:
            return "Heart";
        case BulletSpawner2D::TRANSFORMS_FROM_HELPER_WAVE:
            return "Wave";
        case BulletSpawner2D::TRANSFORMS_FROM_HELPER_WATERFALL:
            return "Waterfall";
        case BulletSpawner2D::TRANSFORMS_FROM_HELPER_LATTICE:
            return "Lattice";
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
        set_process(now_active || spin_enabled || homing_retarget_active() || preview_active() || burst_shots_left > 0 || telegraph_pending);
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
        set_process(now_active || spin_enabled || homing_retarget_active() || preview_active() || burst_shots_left > 0 || telegraph_pending);
    }
    // The shoot_once() cap path emits shooting_finished for the volley that
    // trips the cap. When this setter crosses the same boundary (e.g. a
    // volley_fired handler lowering the cap onto the just-fired count),
    // emitting here too would fire the signal twice for one volley, so the
    // setter only reports resume transitions and leaves the finish report to
    // the shooting path.
    if (!was_active && now_active) {
        emit_signal("shooting_started");
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
        set_process(spin_enabled || auto_shooting_active() || homing_retarget_active() || preview_active() || burst_shots_left > 0 || telegraph_pending);
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
    if (value < TRANSFORMS_FROM_CHILDREN || value > TRANSFORMS_FROM_HELPER_LATTICE) {
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
    // One shoot_once() fans this out to transforms + SoA vectors + physics
    // RIDs: an unchecked typo would freeze/OOM in a single call.
    if (value > 10000) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_bullets_amount must be <= 10000, keeping the old value.");
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
    // The factory generator rejects negatives at spawn time (empty volley +
    // a spawn-path error). Reject here instead so the inspector never holds
    // a value that cannot spawn.
    if (value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_ring_radius must be >= 0, keeping the old value.");
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
    // The factory generator rejects negatives at spawn time (empty volley +
    // a spawn-path error). Reject here instead so the inspector never holds
    // a value that cannot spawn (same guard as helper_ring_radius).
    if (value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_spiral_start_radius must be >= 0, keeping the old value.");
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
    // The factory generator rejects a zero direction at spawn time. Reject
    // here instead so the inspector never holds a value that cannot spawn.
    if (value.length_squared() <= 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_line_direction must be non-zero, keeping the old value.");
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
double BulletSpawner2D::get_helper_aimed_prediction() const {
    return helper_aimed_prediction;
}
void BulletSpawner2D::set_helper_aimed_prediction(double value) {
    if (!Math::is_finite(value) || value < 0.0 || value > 1.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_aimed_prediction must be finite in [0, 1] (0 = aim at now, 1 = full lead), keeping the old value.");
        return;
    }
    helper_aimed_prediction = value;
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
}
int BulletSpawner2D::get_helper_flower_bullets_per_petal() const {
    return helper_flower_bullets_per_petal;
}
void BulletSpawner2D::set_helper_flower_bullets_per_petal(int value) {
    if (value < 1) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_flower_bullets_per_petal must be >= 1, keeping the old value.");
        return;
    }
    helper_flower_bullets_per_petal = value;
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
}
bool BulletSpawner2D::get_helper_flower_face_outward() const {
    return helper_flower_face_outward;
}
void BulletSpawner2D::set_helper_flower_face_outward(bool value) {
    helper_flower_face_outward = value;
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
}
int BulletSpawner2D::get_helper_ellipse_mode() const {
    return helper_ellipse_mode;
}
void BulletSpawner2D::set_helper_ellipse_mode(int value) {
    if (value < (int)BulletFactory2D::ELLIPSE_FULL || value > (int)BulletFactory2D::ELLIPSE_WALL) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_ellipse_mode out of range, keeping the old value.");
        return;
    }
    helper_ellipse_mode = value;
    // WALL reveals the gap pair in the inspector; mode switches hide it.
    notify_property_list_changed();
    rebuild_preview();
}
int BulletSpawner2D::get_helper_ellipse_gap_count() const {
    return helper_ellipse_gap_count;
}
void BulletSpawner2D::set_helper_ellipse_gap_count(int value) {
    if (value < 0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_ellipse_gap_count must be >= 0, keeping the old value.");
        return;
    }
    helper_ellipse_gap_count = value;
    rebuild_preview();
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
    rebuild_preview();
}
bool BulletSpawner2D::get_helper_ellipse_face_outward() const {
    return helper_ellipse_face_outward;
}
void BulletSpawner2D::set_helper_ellipse_face_outward(bool value) {
    helper_ellipse_face_outward = value;
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
}
int BulletSpawner2D::get_helper_polygon_vertices() const {
    return helper_polygon_vertices;
}
void BulletSpawner2D::set_helper_polygon_vertices(int value) {
    if (value < 3) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_polygon_vertices must be >= 3, keeping the old value.");
        return;
    }
    helper_polygon_vertices = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_polygon_radius() const {
    return helper_polygon_radius;
}
void BulletSpawner2D::set_helper_polygon_radius(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_polygon_radius must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_polygon_radius = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_polygon_vertex_bias() const {
    return helper_polygon_vertex_bias;
}
void BulletSpawner2D::set_helper_polygon_vertex_bias(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_polygon_vertex_bias must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_polygon_vertex_bias = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_polygon_base_rotation() const {
    return helper_polygon_base_rotation;
}
void BulletSpawner2D::set_helper_polygon_base_rotation(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_polygon_base_rotation must be finite, keeping the old value.");
        return;
    }
    helper_polygon_base_rotation = value;
    rebuild_preview();
}
bool BulletSpawner2D::get_helper_polygon_face_outward() const {
    return helper_polygon_face_outward;
}
void BulletSpawner2D::set_helper_polygon_face_outward(bool value) {
    helper_polygon_face_outward = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_polygon_facing_offset_deg() const {
    return helper_polygon_facing_offset_deg;
}
void BulletSpawner2D::set_helper_polygon_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_polygon_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_polygon_facing_offset_deg = value;
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
}
bool BulletSpawner2D::get_helper_multispiral_rotate_with_marker() const {
    return helper_multispiral_rotate_with_marker;
}
void BulletSpawner2D::set_helper_multispiral_rotate_with_marker(bool value) {
    helper_multispiral_rotate_with_marker = value;
    rebuild_preview();
}
int BulletSpawner2D::get_helper_multispiral_facing() const {
    return helper_multispiral_facing;
}
void BulletSpawner2D::set_helper_multispiral_facing(int value) {
    if (value < (int)BulletFactory2D::SPIRAL_FACING_TANGENT || value > (int)BulletFactory2D::SPIRAL_FACING_KEEP_MARKER) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_multispiral_facing out of range, keeping the old value.");
        return;
    }
    helper_multispiral_facing = value;
    rebuild_preview();
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
    rebuild_preview();
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
    rebuild_preview();
}
int BulletSpawner2D::get_helper_cross_arm_count() const { return helper_cross_arm_count; }
void BulletSpawner2D::set_helper_cross_arm_count(int value) {
    if (value < 1) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_cross_arm_count must be >= 1, keeping the old value.");
        return;
    }
    helper_cross_arm_count = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_cross_arm_length() const { return helper_cross_arm_length; }
void BulletSpawner2D::set_helper_cross_arm_length(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_cross_arm_length must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_cross_arm_length = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_cross_spacing() const { return helper_cross_spacing; }
void BulletSpawner2D::set_helper_cross_spacing(double value) {
    if (!Math::is_finite(value) || value <= 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_cross_spacing must be finite and > 0, keeping the old value.");
        return;
    }
    helper_cross_spacing = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_cross_base_rotation() const { return helper_cross_base_rotation; }
void BulletSpawner2D::set_helper_cross_base_rotation(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_cross_base_rotation must be finite, keeping the old value.");
        return;
    }
    helper_cross_base_rotation = value;
    rebuild_preview();
}
bool BulletSpawner2D::get_helper_cross_face_outward() const { return helper_cross_face_outward; }
void BulletSpawner2D::set_helper_cross_face_outward(bool value) {
    helper_cross_face_outward = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_cross_facing_offset_deg() const { return helper_cross_facing_offset_deg; }
void BulletSpawner2D::set_helper_cross_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_cross_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_cross_facing_offset_deg = value;
    rebuild_preview();
}
int BulletSpawner2D::get_helper_star_points() const { return helper_star_points; }
void BulletSpawner2D::set_helper_star_points(int value) {
    if (value < 2) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_star_points must be >= 2, keeping the old value.");
        return;
    }
    helper_star_points = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_star_outer_radius() const { return helper_star_outer_radius; }
void BulletSpawner2D::set_helper_star_outer_radius(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_star_outer_radius must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_star_outer_radius = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_star_inner_radius() const { return helper_star_inner_radius; }
void BulletSpawner2D::set_helper_star_inner_radius(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_star_inner_radius must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_star_inner_radius = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_star_base_rotation() const { return helper_star_base_rotation; }
void BulletSpawner2D::set_helper_star_base_rotation(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_star_base_rotation must be finite, keeping the old value.");
        return;
    }
    helper_star_base_rotation = value;
    rebuild_preview();
}
bool BulletSpawner2D::get_helper_star_face_outward() const { return helper_star_face_outward; }
void BulletSpawner2D::set_helper_star_face_outward(bool value) {
    helper_star_face_outward = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_star_facing_offset_deg() const { return helper_star_facing_offset_deg; }
void BulletSpawner2D::set_helper_star_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_star_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_star_facing_offset_deg = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_heart_size() const { return helper_heart_size; }
void BulletSpawner2D::set_helper_heart_size(double value) {
    if (!Math::is_finite(value) || value <= 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_heart_size must be finite and > 0, keeping the old value.");
        return;
    }
    helper_heart_size = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_heart_base_rotation() const { return helper_heart_base_rotation; }
void BulletSpawner2D::set_helper_heart_base_rotation(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_heart_base_rotation must be finite, keeping the old value.");
        return;
    }
    helper_heart_base_rotation = value;
    rebuild_preview();
}
bool BulletSpawner2D::get_helper_heart_face_outward() const { return helper_heart_face_outward; }
void BulletSpawner2D::set_helper_heart_face_outward(bool value) {
    helper_heart_face_outward = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_heart_facing_offset_deg() const { return helper_heart_facing_offset_deg; }
void BulletSpawner2D::set_helper_heart_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_heart_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_heart_facing_offset_deg = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_wave_width() const { return helper_wave_width; }
void BulletSpawner2D::set_helper_wave_width(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_wave_width must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_wave_width = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_wave_amplitude() const { return helper_wave_amplitude; }
void BulletSpawner2D::set_helper_wave_amplitude(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_wave_amplitude must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_wave_amplitude = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_wave_waves() const { return helper_wave_waves; }
void BulletSpawner2D::set_helper_wave_waves(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_wave_waves must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_wave_waves = value;
    rebuild_preview();
}
Vector2 BulletSpawner2D::get_helper_wave_direction() const { return helper_wave_direction; }
void BulletSpawner2D::set_helper_wave_direction(const Vector2 &value) {
    if (!value.is_finite() || value.length_squared() <= 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_wave_direction must be finite and non-zero, keeping the old value.");
        return;
    }
    helper_wave_direction = value;
    rebuild_preview();
}
bool BulletSpawner2D::get_helper_wave_face_direction() const { return helper_wave_face_direction; }
void BulletSpawner2D::set_helper_wave_face_direction(bool value) {
    helper_wave_face_direction = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_wave_facing_offset_deg() const { return helper_wave_facing_offset_deg; }
void BulletSpawner2D::set_helper_wave_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_wave_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_wave_facing_offset_deg = value;
    rebuild_preview();
}
int BulletSpawner2D::get_helper_waterfall_columns() const { return helper_waterfall_columns; }
void BulletSpawner2D::set_helper_waterfall_columns(int value) {
    if (value < 1) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_waterfall_columns must be >= 1, keeping the old value.");
        return;
    }
    helper_waterfall_columns = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_waterfall_column_spacing() const { return helper_waterfall_column_spacing; }
void BulletSpawner2D::set_helper_waterfall_column_spacing(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_waterfall_column_spacing must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_waterfall_column_spacing = value;
    rebuild_preview();
}
int BulletSpawner2D::get_helper_waterfall_rows() const { return helper_waterfall_rows; }
void BulletSpawner2D::set_helper_waterfall_rows(int value) {
    if (value < 1) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_waterfall_rows must be >= 1, keeping the old value.");
        return;
    }
    helper_waterfall_rows = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_waterfall_row_spacing() const { return helper_waterfall_row_spacing; }
void BulletSpawner2D::set_helper_waterfall_row_spacing(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_waterfall_row_spacing must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_waterfall_row_spacing = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_waterfall_stagger() const { return helper_waterfall_stagger; }
void BulletSpawner2D::set_helper_waterfall_stagger(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_waterfall_stagger must be finite, keeping the old value.");
        return;
    }
    helper_waterfall_stagger = value;
    rebuild_preview();
}
Vector2 BulletSpawner2D::get_helper_waterfall_rain_direction() const { return helper_waterfall_rain_direction; }
void BulletSpawner2D::set_helper_waterfall_rain_direction(const Vector2 &value) {
    if (!value.is_finite() || value.length_squared() <= 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_waterfall_rain_direction must be finite and non-zero, keeping the old value.");
        return;
    }
    helper_waterfall_rain_direction = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_waterfall_jitter() const { return helper_waterfall_jitter; }
void BulletSpawner2D::set_helper_waterfall_jitter(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_waterfall_jitter must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_waterfall_jitter = value;
    rebuild_preview();
}
int BulletSpawner2D::get_helper_lattice_columns() const { return helper_lattice_columns; }
void BulletSpawner2D::set_helper_lattice_columns(int value) {
    if (value < 1) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_lattice_columns must be >= 1, keeping the old value.");
        return;
    }
    helper_lattice_columns = value;
    rebuild_preview();
}
int BulletSpawner2D::get_helper_lattice_rows() const { return helper_lattice_rows; }
void BulletSpawner2D::set_helper_lattice_rows(int value) {
    if (value < 1) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_lattice_rows must be >= 1, keeping the old value.");
        return;
    }
    helper_lattice_rows = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_lattice_spacing_x() const { return helper_lattice_spacing_x; }
void BulletSpawner2D::set_helper_lattice_spacing_x(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_lattice_spacing_x must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_lattice_spacing_x = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_lattice_spacing_y() const { return helper_lattice_spacing_y; }
void BulletSpawner2D::set_helper_lattice_spacing_y(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_lattice_spacing_y must be finite and >= 0, keeping the old value.");
        return;
    }
    helper_lattice_spacing_y = value;
    rebuild_preview();
}
bool BulletSpawner2D::get_helper_lattice_stagger_rows() const { return helper_lattice_stagger_rows; }
void BulletSpawner2D::set_helper_lattice_stagger_rows(bool value) {
    helper_lattice_stagger_rows = value;
    rebuild_preview();
}
bool BulletSpawner2D::get_helper_lattice_face_outward() const { return helper_lattice_face_outward; }
void BulletSpawner2D::set_helper_lattice_face_outward(bool value) {
    helper_lattice_face_outward = value;
    rebuild_preview();
}
double BulletSpawner2D::get_helper_lattice_facing_offset_deg() const { return helper_lattice_facing_offset_deg; }
void BulletSpawner2D::set_helper_lattice_facing_offset_deg(double value) {
    if (!Math::is_finite(value)) {
        UtilityFunctions::push_error("BulletSpawner2D: helper_lattice_facing_offset_deg must be finite, keeping the old value.");
        return;
    }
    helper_lattice_facing_offset_deg = value;
    rebuild_preview();
}
PackedInt32Array BulletSpawner2D::get_helper_skip_indices() const { return helper_skip_indices; }
void BulletSpawner2D::set_helper_skip_indices(const PackedInt32Array &value) {
    helper_skip_indices = value;
    rebuild_preview();
}
BulletSpawner2D::HomingTargetPriority BulletSpawner2D::get_homing_target_priority() const { return homing_target_priority; }
void BulletSpawner2D::set_homing_target_priority(HomingTargetPriority value) {
    if (value < HOMING_PRIORITY_NEAREST || value > HOMING_PRIORITY_CUSTOM) {
        UtilityFunctions::push_error("BulletSpawner2D: invalid homing_target_priority, keeping the old value.");
        return;
    }
    homing_target_priority = value;
}
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
double BulletSpawner2D::get_reload_jitter_sec() const { return reload_jitter_sec; }
void BulletSpawner2D::set_reload_jitter_sec(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: reload_jitter_sec must be finite and >= 0, keeping the old value.");
        return;
    }
    reload_jitter_sec = value;
}
// Burst / telegraph / targeting / perf setters: same reject-and-keep
// contract as every other spawner knob (validated setters never leave a
// half-applied value behind).
bool BulletSpawner2D::get_burst_enabled() const {
    return burst_enabled;
}
void BulletSpawner2D::set_burst_enabled(bool value) {
    burst_enabled = value;
    if (!value) {
        burst_shots_left = 0;
        burst_time_left = 0.0;
        telegraph_pending = false;
        telegraph_time_left = 0.0;
    } else if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint()) {
        set_process(true);
    }
}
int BulletSpawner2D::get_burst_count() const {
    return burst_count;
}
void BulletSpawner2D::set_burst_count(int value) {
    if (value < 1) {
        UtilityFunctions::push_error("BulletSpawner2D: burst_count must be >= 1, keeping the old value.");
        return;
    }
    burst_count = value;
}
double BulletSpawner2D::get_burst_interval_sec() const {
    return burst_interval_sec;
}
void BulletSpawner2D::set_burst_interval_sec(double value) {
    if (!Math::is_finite(value) || !(value > 0.0)) {
        UtilityFunctions::push_error("BulletSpawner2D: burst_interval_sec must be finite and > 0, keeping the old value.");
        return;
    }
    burst_interval_sec = value;
}
bool BulletSpawner2D::get_burst_alternate_mirror() const {
    return burst_alternate_mirror;
}
void BulletSpawner2D::set_burst_alternate_mirror(bool value) {
    burst_alternate_mirror = value;
}
bool BulletSpawner2D::get_telegraph_enabled() const {
    return telegraph_enabled;
}
void BulletSpawner2D::set_telegraph_enabled(bool value) {
    telegraph_enabled = value;
    if (!value) {
        telegraph_pending = false;
        telegraph_time_left = 0.0;
    } else if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint()) {
        set_process(true);
    }
}
double BulletSpawner2D::get_telegraph_sec() const {
    return telegraph_sec;
}
void BulletSpawner2D::set_telegraph_sec(double value) {
    if (!Math::is_finite(value) || value < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D: telegraph_sec must be finite and >= 0, keeping the old value.");
        return;
    }
    telegraph_sec = value;
}
Callable BulletSpawner2D::get_homing_target_scorer() const {
    return homing_target_scorer;
}
void BulletSpawner2D::set_homing_target_scorer(const Callable &value) {
    if (!value.is_null() && !value.is_valid()) {
        UtilityFunctions::push_error("BulletSpawner2D: homing_target_scorer must be a valid callable or null, keeping the old value.");
        return;
    }
    homing_target_scorer = value;
}
StringName BulletSpawner2D::get_homing_priority_group() const {
    return homing_priority_group;
}
void BulletSpawner2D::set_homing_priority_group(const StringName &value) {
    homing_priority_group = value;
}
bool BulletSpawner2D::get_homing_fire_requires_target() const {
    return homing_fire_requires_target;
}
void BulletSpawner2D::set_homing_fire_requires_target(bool value) {
    homing_fire_requires_target = value;
}
bool BulletSpawner2D::get_homing_sticky_targets() const {
    return homing_sticky_targets;
}
void BulletSpawner2D::set_homing_sticky_targets(bool value) {
    homing_sticky_targets = value;
}
int BulletSpawner2D::get_pattern_seed() const {
    return pattern_seed;
}
void BulletSpawner2D::set_pattern_seed(int value) {
    if (value < 0) {
        UtilityFunctions::push_error("BulletSpawner2D: pattern_seed must be >= 0 (0 = non-deterministic), keeping the old value.");
        return;
    }
    pattern_seed = value;
}
int BulletSpawner2D::get_max_live_bullets() const {
    return max_live_bullets;
}
void BulletSpawner2D::set_max_live_bullets(int value) {
    if (value < 0) {
        UtilityFunctions::push_error("BulletSpawner2D: max_live_bullets must be >= 0 (0 = unlimited), keeping the old value.");
        return;
    }
    max_live_bullets = value;
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
    homing_retarget_time_left = value;
}
int BulletSpawner2D::get_burst_shots_left() const {
    return burst_shots_left;
}
int BulletSpawner2D::get_active_live_bullet_count() const {
    prune_live_volleys();
    int total = 0;
    const uint64_t self_id = get_instance_id();
    for (int i = 0; i < live_volley_instance_ids.size(); ++i) {
        Object *obj = ObjectDB::get_instance(ObjectID((uint64_t)live_volley_instance_ids[i]));
        DirectionalBullets2D *volley = Object::cast_to<DirectionalBullets2D>(obj);
        if (volley == nullptr || volley->owner_spawner_id != self_id || !volley->is_active) {
            continue;
        }
        total += volley->active_bullets_counter;
    }
    return total;
}
int BulletSpawner2D::get_pooled_volley_count() const {
    BulletFactory2D *factory = get_bullet_factory();
    if (factory == nullptr) {
        return 0;
    }
    return factory->debug_get_bullets_pool_amount(BulletFactory2D::DIRECTIONAL_BULLETS);
}
void BulletSpawner2D::apply_pattern_preset(int preset) {
    switch (preset) {
        case BulletFactory2D::PATTERN_PRESET_RADIAL_DENSE:
            transforms_source = TRANSFORMS_FROM_HELPER_RING;
            helper_bullets_amount = 36;
            helper_ring_radius = 60.0;
            helper_ring_arc = Math::TAU;
            helper_ring_face_outward = true;
            break;
        case BulletFactory2D::PATTERN_PRESET_RADIAL_SPARSE:
            transforms_source = TRANSFORMS_FROM_HELPER_RING;
            helper_bullets_amount = 12;
            helper_ring_radius = 60.0;
            helper_ring_arc = Math::TAU;
            helper_ring_face_outward = true;
            break;
        case BulletFactory2D::PATTERN_PRESET_SPIRAL_3ARM:
            transforms_source = TRANSFORMS_FROM_HELPER_MULTISPIRAL;
            helper_bullets_amount = 30;
            helper_multispiral_arms = 3;
            helper_multispiral_start_radius = 40.0;
            helper_multispiral_radius_step = 18.0;
            helper_multispiral_angle_step = 0.5;
            break;
        case BulletFactory2D::PATTERN_PRESET_AIMED_FAN_NARROW:
            transforms_source = TRANSFORMS_FROM_HELPER_AIMED;
            helper_bullets_amount = 5;
            helper_aimed_spread = 0.25;
            helper_aimed_centered = true;
            break;
        case BulletFactory2D::PATTERN_PRESET_AIMED_FAN_WIDE:
            transforms_source = TRANSFORMS_FROM_HELPER_AIMED;
            helper_bullets_amount = 9;
            helper_aimed_spread = 1.2;
            helper_aimed_centered = true;
            break;
        case BulletFactory2D::PATTERN_PRESET_RING_SLOW:
            transforms_source = TRANSFORMS_FROM_HELPER_RING;
            helper_bullets_amount = 24;
            helper_ring_radius = 220.0;
            helper_ring_arc = Math::TAU;
            helper_ring_face_outward = true;
            break;
        case BulletFactory2D::PATTERN_PRESET_WALL_GAPS:
            transforms_source = TRANSFORMS_FROM_HELPER_ELLIPSE;
            helper_bullets_amount = 40;
            helper_ellipse_radius_x = 260.0;
            helper_ellipse_radius_y = 260.0;
            helper_ellipse_arc = Math::TAU;
            helper_ellipse_mode = (int)BulletFactory2D::ELLIPSE_WALL;
            helper_ellipse_gap_count = 3;
            helper_ellipse_gap_width = 0.35;
            break;
        case BulletFactory2D::PATTERN_PRESET_RAIN:
            transforms_source = TRANSFORMS_FROM_HELPER_RAIN;
            helper_bullets_amount = 24;
            helper_rain_band_width = 700.0;
            helper_rain_direction = Vector2(0, 1);
            helper_rain_drop_spacing = 64.0;
            break;
        case BulletFactory2D::PATTERN_PRESET_FLOWER_6:
            transforms_source = TRANSFORMS_FROM_HELPER_FLOWER;
            helper_bullets_amount = 30;
            helper_flower_petals = 6;
            helper_flower_bullets_per_petal = 5;
            helper_flower_radius = 140.0;
            break;
        case BulletFactory2D::PATTERN_PRESET_SCATTER_BURST:
            transforms_source = TRANSFORMS_FROM_HELPER_SCATTER;
            helper_bullets_amount = 26;
            helper_scatter_burst_radius = 130.0;
            helper_scatter_facing_jitter = 0.5;
            break;
        case BulletFactory2D::PATTERN_PRESET_CROSS_BURST:
            transforms_source = TRANSFORMS_FROM_HELPER_CROSS;
            helper_bullets_amount = 24;
            helper_cross_arm_count = 4;
            helper_cross_arm_length = 150.0;
            helper_cross_spacing = 32.0;
            break;
        case BulletFactory2D::PATTERN_PRESET_STAR_SHELL:
            transforms_source = TRANSFORMS_FROM_HELPER_STAR;
            helper_bullets_amount = 20;
            helper_star_points = 5;
            helper_star_outer_radius = 150.0;
            helper_star_inner_radius = 65.0;
            break;
        case BulletFactory2D::PATTERN_PRESET_HEART_BLOOM:
            transforms_source = TRANSFORMS_FROM_HELPER_HEART;
            helper_bullets_amount = 40;
            helper_heart_size = 150.0;
            break;
        case BulletFactory2D::PATTERN_PRESET_SNAKE_WAVE:
            transforms_source = TRANSFORMS_FROM_HELPER_WAVE;
            helper_bullets_amount = 28;
            helper_wave_width = 600.0;
            helper_wave_amplitude = 48.0;
            helper_wave_waves = 2.0;
            break;
        case BulletFactory2D::PATTERN_PRESET_WATERFALL_CURTAIN:
            transforms_source = TRANSFORMS_FROM_HELPER_WATERFALL;
            helper_bullets_amount = 36;
            helper_waterfall_columns = 12;
            helper_waterfall_rows = 3;
            helper_waterfall_column_spacing = 48.0;
            helper_waterfall_row_spacing = 64.0;
            break;
        case BulletFactory2D::PATTERN_PRESET_PETAL_STORM:
            transforms_source = TRANSFORMS_FROM_HELPER_FLOWER;
            helper_bullets_amount = 48;
            helper_flower_petals = 8;
            helper_flower_bullets_per_petal = 6;
            helper_flower_radius = 170.0;
            helper_flower_petal_spread = 0.7;
            break;
        case BulletFactory2D::PATTERN_PRESET_TWIN_SPIRAL_COUNTER:
            transforms_source = TRANSFORMS_FROM_HELPER_MULTISPIRAL;
            helper_bullets_amount = 40;
            helper_multispiral_arms = 2;
            helper_multispiral_start_radius = 50.0;
            helper_multispiral_radius_step = 15.0;
            helper_multispiral_angle_step = 0.6;
            spin_enabled = true;
            spin_speed_deg_per_sec = -60.0;
            break;
        case BulletFactory2D::PATTERN_PRESET_AIMED_TRAP:
            transforms_source = TRANSFORMS_FROM_HELPER_AIMED;
            helper_bullets_amount = 9;
            helper_aimed_spread = 1.2;
            helper_aimed_centered = true;
            helper_aimed_prediction = 1.0;
            helper_aimed_prediction_time = 0.5;
            break;
        case BulletFactory2D::PATTERN_PRESET_TD_RING_GUARD:
            transforms_source = TRANSFORMS_FROM_HELPER_RING;
            helper_bullets_amount = 16;
            helper_ring_radius = 120.0;
            helper_ring_arc = Math::TAU;
            helper_ring_face_outward = true;
            homing_enabled = true;
            homing_max_detection_range = 600.0;
            break;
        case BulletFactory2D::PATTERN_PRESET_BLOSSOM_FINALE:
            transforms_source = TRANSFORMS_FROM_HELPER_FLOWER;
            helper_bullets_amount = 60;
            helper_flower_petals = 12;
            helper_flower_bullets_per_petal = 5;
            helper_flower_radius = 200.0;
            spin_enabled = true;
            spin_speed_deg_per_sec = 30.0;
            break;
        case BulletFactory2D::PATTERN_PRESET_CUSTOM:
        default:
            return;
    }
    notify_property_list_changed();
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
    // Runtime preview piggy-backs the shooting/spin/retarget loop: enabling
    // it mid-game while that loop sleeps must wake it, or the preview builds
    // once here and never refreshes marker moves afterwards.
    if (!Engine::get_singleton()->is_editor_hint() && is_inside_tree()) {
        set_process(auto_shooting_active() || spin_enabled || homing_retarget_active() || preview_active());
    }
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
    homing_retarget_time_left = homing_retarget_phase;
    // Burst/telegraph chains never survive a restart: a stale mid-burst
    // countdown firing into a reset wave would double-fire volleys.
    burst_shots_left = 0;
    burst_time_left = 0.0;
    burst_mirror_next = false;
    burst_telegraph_done = false;
    burst_firing = false;
    telegraph_pending = false;
    telegraph_time_left = 0.0;
    if (is_inside_tree()) {
        set_process(auto_shooting_active() || spin_enabled || homing_retarget_active() || preview_active());
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
    // Bounded like helper_bullets_amount: the name scan + NEAREST selection
    // run per volley and per retarget pass, so an unbounded value turns a
    // huge scene into a per-interval hitch.
    if (value > 10000) {
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
    // Orbiting rides on homing targets: toggling it must not leave _process
    // asleep. Editor-guarded: the preview owns processing in the editor.
    if (!Engine::get_singleton()->is_editor_hint() && is_inside_tree()) {
        set_process(auto_shooting_active() || spin_enabled || homing_retarget_active() || preview_active() || burst_shots_left > 0 || telegraph_pending);
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
DirectionalBullets2D::OrbitingDirection BulletSpawner2D::get_orbiting_direction() const {
    return orbiting_direction;
}
void BulletSpawner2D::set_orbiting_direction(DirectionalBullets2D::OrbitingDirection value) {
    if (value != DirectionalBullets2D::DontMove && value != DirectionalBullets2D::OrbitLeft && value != DirectionalBullets2D::OrbitRight && value != DirectionalBullets2D::OrbitRandom) {
        UtilityFunctions::push_error("BulletSpawner2D: invalid orbiting_direction, keeping the old value.");
        return;
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
DirectionalBullets2D::OrbitingFollowMode BulletSpawner2D::get_orbiting_follow_mode() const {
    return orbiting_follow_mode;
}
void BulletSpawner2D::set_orbiting_follow_mode(DirectionalBullets2D::OrbitingFollowMode value) {
    if (value != DirectionalBullets2D::FollowTarget && value != DirectionalBullets2D::FollowDeadzone && value != DirectionalBullets2D::Anchored) {
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
DirectionalBullets2D::OrbitingLockPolicy BulletSpawner2D::get_orbiting_lock_policy() const {
    return orbiting_lock_policy;
}
void BulletSpawner2D::set_orbiting_lock_policy(DirectionalBullets2D::OrbitingLockPolicy value) {
    if (value != DirectionalBullets2D::RelockAlways && value != DirectionalBullets2D::StayLocked && value != DirectionalBullets2D::RelockOnTargetChange) {
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

void BulletSpawner2D::update_homing_process_state() {
    if (homing_retarget_active()) {
        // Phase-stagger the first pass so N towers don't scene-scan together;
        // zero phase stays due-now for immediate refresh.
        homing_retarget_time_left = homing_retarget_phase;
        set_process(true);
    } else if (!Engine::get_singleton()->is_editor_hint() && is_inside_tree()) {
        // Sleep when nothing needs the loop. Mirrors the shooting setters so
        // disabling retarget can actually stop _process.
        set_process(auto_shooting_active() || spin_enabled || preview_active() || burst_shots_left > 0 || telegraph_pending);
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
    // Explicit stack instead of recursion (same reason as
    // collect_homing_candidates_by_name): trees can be arbitrarily deep and
    // this runs per volley plus per retarget pass. Reverse-push keeps
    // depth-first pre-order identical to the old recursive walk.
    Array stack;
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
        if (as_2d != nullptr && as_2d != this) {
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
    // Priority pin first (boss / decoy / aggro): resolved before the normal
    // selection and returned directly when non-empty. Falls back below when
    // the group is empty or unset.
    if (!homing_priority_group.is_empty()) {
        SceneTree *priority_tree = get_tree();
        if (priority_tree != nullptr) {
            TypedArray<Node> pinned = priority_tree->get_nodes_in_group(homing_priority_group);
            Array pinned_targets;
            for (int i = 0; i < pinned.size(); ++i) {
                Node2D *candidate = Object::cast_to<Node2D>(pinned[i]);
                if (candidate == nullptr) {
                    continue;
                }
                if (!homing_filter_group.is_empty() && !candidate->is_in_group(homing_filter_group)) {
                    continue;
                }
                if (homing_max_detection_range > 0.0 && candidate->get_global_position().distance_squared_to(get_global_position()) > (real_t)(homing_max_detection_range * homing_max_detection_range)) {
                    continue;
                }
                pinned_targets.push_back(candidate);
                if ((int)pinned_targets.size() >= MIN(homing_max_targets, 256)) {
                    break;
                }
            }
            if (!pinned_targets.is_empty()) {
                return pinned_targets;
            }
        }
    }
    // The deque caps at 256 targets per queue (see HomingTargetDeque): clamp
    // the take there too, otherwise a huge max_targets fans thousands of
    // rejected pushes (one error each) every volley and every retarget pass.
    // DISTRIBUTE is unaffected (exactly one target per bullet).
    const int take = MIN(MIN(homing_max_targets, (int)candidates.size()), 256);
    // DISTRIBUTE deals one target per bullet across the volley (cycling), so
    // the resolution order here does not matter: spawn and retarget build
    // the deal with i % pool themselves. NEAREST order is returned, same as
    // the default selection.
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
                if (pattern_seed > 0) {
                    homing_rng->set_seed((uint64_t)pattern_seed);
                } else {
                    homing_rng->randomize();
                }
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
    // Target priority presets (tower-defense First/Last/Strongest/Weakest/
    // Fastest without GDScript): built-in scoring runs over the same
    // candidate set. CUSTOM keeps the user Callable below. FIRST/LAST read
    // each candidate's "progress" (path followers: PathFollow2D progress,
    // meta, or property); STRONGEST/WEAKEST read "hp"/"health";
    // FASTEST reads get_velocity().length(). Missing data warns once and
    // falls back to nearest for that candidate. RANDOM shuffles via the
    // pattern-seeded RNG. NEAREST keeps the built-in order above.
    if (homing_target_priority != HOMING_PRIORITY_NEAREST && homing_target_priority != HOMING_PRIORITY_CUSTOM && !candidates.is_empty()) {
        Array scored;
        const Vector2 origin = get_global_position();
        Ref<RandomNumberGenerator> prio_rng;
        if (homing_target_priority == HOMING_PRIORITY_RANDOM) {
            prio_rng.instantiate();
            if (pattern_seed != 0) {
                prio_rng->set_seed((uint64_t)pattern_seed);
            } else {
                prio_rng->randomize();
            }
        }
        for (int i = 0; i < candidates.size(); ++i) {
            Node2D *node = Object::cast_to<Node2D>(candidates[i]);
            if (node == nullptr) {
                continue;
            }
            double score = 0.0;
            bool resolved = true;
            switch (homing_target_priority) {
                case HOMING_PRIORITY_FIRST:
                case HOMING_PRIORITY_LAST: {
                    double progress = 0.0;
                    bool has_progress = false;
                    if (node->has_method("get_progress")) {
                        Variant v = node->call("get_progress");
                        if (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) {
                            progress = (double)v;
                            has_progress = true;
                        }
                    }
                    if (!has_progress && node->has_meta("progress")) {
                        Variant v = node->get_meta("progress");
                        if (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) {
                            progress = (double)v;
                            has_progress = true;
                        }
                    }
                    if (!has_progress) {
                        Variant v = node->get("progress");
                        if (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) {
                            progress = (double)v;
                            has_progress = true;
                        }
                    }
                    if (!has_progress) {
                        resolved = false;
                    } else {
                        score = (homing_target_priority == HOMING_PRIORITY_FIRST) ? progress : -progress;
                    }
                    break;
                }
                case HOMING_PRIORITY_STRONGEST:
                case HOMING_PRIORITY_WEAKEST: {
                    double hp = 0.0;
                    bool has_hp = false;
                    const char *keys[4] = { "hp", "health", "hit_points", "max_hp" };
                    for (int k = 0; k < 4 && !has_hp; ++k) {
                        if (node->has_meta(keys[k])) {
                            Variant v = node->get_meta(keys[k]);
                            if (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) {
                                hp = (double)v;
                                has_hp = true;
                            }
                        }
                    }
                    for (int k = 0; k < 4 && !has_hp; ++k) {
                        Variant v = node->get(keys[k]);
                        if (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) {
                            hp = (double)v;
                            has_hp = true;
                        }
                    }
                    if (!has_hp) {
                        resolved = false;
                    } else {
                        score = (homing_target_priority == HOMING_PRIORITY_STRONGEST) ? hp : -hp;
                    }
                    break;
                }
                case HOMING_PRIORITY_FASTEST: {
                    double speed = 0.0;
                    bool has_speed = false;
                    if (node->has_method("get_velocity")) {
                        Variant v = node->call("get_velocity");
                        if (v.get_type() == Variant::VECTOR2) {
                            speed = ((Vector2)v).length();
                            has_speed = true;
                        }
                    }
                    if (!has_speed) {
                        Variant v = node->get("velocity");
                        if (v.get_type() == Variant::VECTOR2) {
                            speed = ((Vector2)v).length();
                            has_speed = true;
                        }
                    }
                    if (!has_speed) {
                        resolved = false;
                    } else {
                        score = speed;
                    }
                    break;
                }
                case HOMING_PRIORITY_RANDOM: {
                    score = prio_rng->randf();
                    break;
                }
                default:
                    break;
            }
            if (!resolved) {
                // Fail open to nearest for this candidate (negative distance
                // keeps the global desc-sort consistent).
                score = -node->get_global_position().distance_squared_to(origin);
            }
            Dictionary entry;
            entry["node"] = node;
            entry["score"] = score;
            entry["dist"] = node->get_global_position().distance_squared_to(origin);
            scored.push_back(entry);
        }
        if (!scored.is_empty()) {
            for (int a = 0; a < scored.size(); ++a) {
                for (int b = a + 1; b < scored.size(); ++b) {
                    Dictionary da = scored[a];
                    Dictionary db = scored[b];
                    const double sa = (double)da["score"];
                    const double sb = (double)db["score"];
                    const double da_dist = (double)da["dist"];
                    const double db_dist = (double)db["dist"];
                    if (sb > sa || (Math::is_equal_approx(sb, sa) && db_dist < da_dist)) {
                        scored[a] = db;
                        scored[b] = da;
                    }
                }
            }
            targets.clear();
            for (int k = 0; k < take && k < scored.size(); ++k) {
                Dictionary entry = scored[k];
                targets.push_back(entry["node"]);
            }
        }
    }
    // Custom scorer (TD First/Last/Strongest/Weakest/Fastest in game code):
    // Callable(candidate: Node2D) -> float, highest score wins. Runs after
    // the built-in selection over the same candidate set; invalid returns
    // fall back to the built-in order for that candidate. Errors are loud
    // once per call (fail-open), never silent drops.
    if (!homing_target_scorer.is_null() && homing_target_scorer.is_valid() && !candidates.is_empty()) {
        Array scored;
        const Vector2 origin = get_global_position();
        for (int i = 0; i < candidates.size(); ++i) {
            Node2D *node = Object::cast_to<Node2D>(candidates[i]);
            if (node == nullptr) {
                continue;
            }
            Variant score_v;
            Variant arg = node;
            Variant *args[1] = { &arg };
            // No CallError surface in godot-cpp: callv throws a runtime error
            // on failure, so guard with validity checks and treat null as
            // "scorer bailed" (fail-open to built-in order for that candidate).
            Variant ret;
            bool call_ok = false;
            if (homing_target_scorer.is_valid() && !homing_target_scorer.is_null()) {
                Array call_args;
                call_args.push_back(node);
                ret = homing_target_scorer.callv(call_args);
                call_ok = ret.get_type() == Variant::FLOAT || ret.get_type() == Variant::INT;
            }
            if (!call_ok) {
                UtilityFunctions::push_error("BulletSpawner2D::resolve_homing_targets: homing_target_scorer must be Callable(Node2D) -> float; keeping built-in order for one candidate.");
                continue;
            }
            score_v = ret;
            Dictionary entry;
            entry["node"] = node;
            entry["score"] = (double)score_v;
            entry["dist"] = node->get_global_position().distance_squared_to(origin);
            scored.push_back(entry);
        }
        if (!scored.is_empty()) {
            // Selection sort by score desc (take is tiny; dist breaks ties
            // toward nearer, matching NEAREST intuition).
            for (int a = 0; a < scored.size(); ++a) {
                for (int b = a + 1; b < scored.size(); ++b) {
                    Dictionary da = scored[a];
                    Dictionary db = scored[b];
                    const double sa = (double)da["score"];
                    const double sb = (double)db["score"];
                    const double da_dist = (double)da["dist"];
                    const double db_dist = (double)db["dist"];
                    if (sb > sa || (Math::is_equal_approx(sb, sa) && db_dist < da_dist)) {
                        scored[a] = db;
                        scored[b] = da;
                    }
                }
            }
            targets.clear();
            for (int k = 0; k < take && k < scored.size(); ++k) {
                Dictionary entry = scored[k];
                targets.push_back(entry["node"]);
            }
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
    // Bound the list: infinite-lifetime volleys never prune, so auto-shoot
    // would grow retarget cost (O(volleys*bullets + volleys*tree)) without
    // limit. Keep the most recent; drop oldest first. Use clear_live_volleys()
    // to reset manually.
    while (live_volley_instance_ids.size() > 256) {
        live_volley_instance_ids.remove_at(0);
    }
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
                if (homing_sticky_targets) {
                    // Focus fire: keep each bullet's dealt target (only fill
                    // emptied queues), instead of re-dealing every pass.
                    const int bullet_count = volley->get_amount_bullets();
                    for (int i = 0; i < bullet_count; ++i) {
                        if (!volley->is_bullet_status_enabled(i)) {
                            continue;
                        }
                        if (!volley->bullet_check_has_homing_targets(i)) {
                            volley->bullet_replace_homing_targets_with_new_target(i, volley_targets[i % volley_targets.size()]);
                        }
                    }
                } else {
                    // Re-deal like at spawn: per-bullet replace (not clear +
                    // assign) so locked rings survive per the lock policy instead
                    // of unlocking on every pass. Skips disabled slots silently.
                    const int bullet_count = volley->get_amount_bullets();
                    for (int i = 0; i < bullet_count; ++i) {
                        if (!volley->is_bullet_status_enabled(i)) {
                            continue;
                        }
                        volley->bullet_replace_homing_targets_with_new_target(i, volley_targets[i % volley_targets.size()]);
                    }
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

void BulletSpawner2D::apply_orbiting_to_volley(DirectionalBullets2D *volley) const {
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
        } else if (orbiting_direction == DirectionalBullets2D::OrbitRandom) {
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
                // between resolution and this log line. Never touch the raw
                // pointer to validate it: get_instance_id()/get_name() on a
                // freed pointer is itself a deref (UAF). The resolved id was
                // captured at resolve time - validate that via ObjectDB, then
                // only touch the pointer when the id still resolves to it.
                uint64_t first_id = 0;
                if (first_node != nullptr) {
                    // Object::cast_to succeeded, so the pointer was live at
                    // cast time; capture the id for the ObjectDB check below.
                    // (Still best-effort within one synchronous function.)
                    first_id = first_node->get_instance_id();
                }
                Object *first_live = first_id != 0 && UtilityFunctions::is_instance_id_valid(first_id) ? ObjectDB::get_instance(ObjectID(first_id)) : nullptr;
                if (first_live != nullptr && first_live == first_node) {
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
            // Predictive lead: blend the live position toward where the
            // target will be after prediction_time at its current velocity
            // (CharacterBody2D-style get_velocity; anything else aims live).
            Vector2 aim_pos = target->get_global_transform().get_origin();
            if (helper_aimed_prediction > 0.0 && helper_aimed_prediction_time > 0.0) {
                Vector2 target_vel = Vector2(0, 0);
                bool has_vel = false;
                if (target->has_method("get_velocity")) {
                    Variant v = target->call("get_velocity");
                    if (v.get_type() == Variant::VECTOR2) {
                        target_vel = v;
                        has_vel = target_vel.is_finite();
                    }
                }
                if (has_vel) {
                    aim_pos += target_vel * (real_t)(helper_aimed_prediction_time * helper_aimed_prediction);
                }
            }
            raw = BulletFactory2D::helper_generate_transforms_aimed(helper_bullets_amount, marker, aim_pos, helper_aimed_spread, helper_aimed_step_offset, helper_aimed_centered);
            break;
        }
        case TRANSFORMS_FROM_HELPER_FLOWER:
            raw = BulletFactory2D::helper_generate_transforms_flower(helper_bullets_amount, marker, helper_flower_petals, helper_flower_bullets_per_petal, helper_flower_radius, helper_flower_petal_spread, helper_flower_petal_sharpness, helper_flower_base_rotation, helper_flower_face_outward, helper_flower_facing_offset_deg);
            break;
        case TRANSFORMS_FROM_HELPER_ELLIPSE:
            raw = BulletFactory2D::helper_generate_transforms_ellipse(helper_bullets_amount, marker, helper_ellipse_radius_x, helper_ellipse_radius_y, helper_ellipse_rotation, helper_ellipse_start_angle, helper_ellipse_arc, (BulletFactory2D::EllipseMode)helper_ellipse_mode, helper_ellipse_gap_count, helper_ellipse_gap_width, helper_ellipse_face_outward, helper_ellipse_facing_offset_deg);
            break;
        case TRANSFORMS_FROM_HELPER_RAIN:
            raw = BulletFactory2D::helper_generate_transforms_rain(helper_bullets_amount, marker, helper_rain_band_width, helper_rain_direction, helper_rain_drop_spacing, helper_rain_jitter);
            break;
        case TRANSFORMS_FROM_HELPER_SCATTER: {
            // pattern_seed is the volley default; the per-pattern seed wins
            // when set (replays pin one pattern without freezing the rest).
            const uint64_t scatter_seed = helper_scatter_seed > 0 ? (uint64_t)helper_scatter_seed : (pattern_seed > 0 ? (uint64_t)pattern_seed : 0);
            raw = BulletFactory2D::helper_generate_transforms_scatter(helper_bullets_amount, marker, helper_scatter_burst_radius, helper_scatter_facing_jitter, scatter_seed);
            break;
        }
        case TRANSFORMS_FROM_HELPER_POLYGON:
            raw = BulletFactory2D::helper_generate_transforms_polygon(helper_bullets_amount, marker, helper_polygon_vertices, helper_polygon_radius, helper_polygon_vertex_bias, helper_polygon_base_rotation, helper_polygon_face_outward, helper_polygon_facing_offset_deg);
            break;
        case TRANSFORMS_FROM_HELPER_MULTISPIRAL:
            raw = BulletFactory2D::helper_generate_transforms_multispiral(helper_bullets_amount, marker, helper_multispiral_arms, helper_multispiral_start_radius, helper_multispiral_radius_step, helper_multispiral_angle_step, helper_multispiral_rotate_with_marker, (BulletFactory2D::SpiralFacingMode)helper_multispiral_facing, helper_multispiral_facing_offset_deg, helper_multispiral_arm_stride);
            break;
        case TRANSFORMS_FROM_HELPER_CROSS:
            raw = BulletFactory2D::helper_generate_transforms_cross(helper_bullets_amount, marker, helper_cross_arm_count, helper_cross_arm_length, helper_cross_spacing, helper_cross_base_rotation, helper_cross_face_outward, helper_cross_facing_offset_deg);
            break;
        case TRANSFORMS_FROM_HELPER_STAR:
            raw = BulletFactory2D::helper_generate_transforms_star(helper_bullets_amount, marker, helper_star_points, helper_star_outer_radius, helper_star_inner_radius, helper_star_base_rotation, helper_star_face_outward, helper_star_facing_offset_deg);
            break;
        case TRANSFORMS_FROM_HELPER_HEART:
            raw = BulletFactory2D::helper_generate_transforms_heart(helper_bullets_amount, marker, helper_heart_size, helper_heart_base_rotation, helper_heart_face_outward, helper_heart_facing_offset_deg);
            break;
        case TRANSFORMS_FROM_HELPER_WAVE:
            raw = BulletFactory2D::helper_generate_transforms_wave(helper_bullets_amount, marker, helper_wave_width, helper_wave_amplitude, helper_wave_waves, helper_wave_direction, helper_wave_face_direction, helper_wave_facing_offset_deg);
            break;
        case TRANSFORMS_FROM_HELPER_WATERFALL:
            raw = BulletFactory2D::helper_generate_transforms_waterfall(helper_bullets_amount, marker, helper_waterfall_columns, helper_waterfall_column_spacing, helper_waterfall_rows, helper_waterfall_row_spacing, helper_waterfall_stagger, helper_waterfall_rain_direction, helper_waterfall_jitter);
            break;
        case TRANSFORMS_FROM_HELPER_LATTICE:
            raw = BulletFactory2D::helper_generate_transforms_lattice(helper_bullets_amount, marker, helper_lattice_columns, helper_lattice_rows, helper_lattice_spacing_x, helper_lattice_spacing_y, helper_lattice_stagger_rows, helper_lattice_face_outward, helper_lattice_facing_offset_deg);
            break;
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
    // Burst mirror flips chirality every other burst volley (fan/spiral
    // rhythm without scripting): negate the spin contribution for this
    // volley only — the stored spin_angle_deg keeps advancing untouched.
    real_t spin_radians = Math::deg_to_rad((real_t)spin_angle_deg);
    if (burst_alternate_mirror && burst_mirror_next) {
        spin_radians = -spin_radians;
    }
    const real_t scale = (real_t)transforms_scale;
    TypedArray<Transform2D> transforms;
    for (int i = 0; i < raw.size(); ++i) {
        Transform2D t = raw[i];
        t = rotate_spawn_transform(t, base_origin, spin_radians);
        transforms.push_back(scale_spawn_transform(t, base_origin, scale));
    }
    // Negative space last (post spin/scale so indexes match the preview):
    // carve dodge doors or bullet text out of any helper layout.
    if (!helper_skip_indices.is_empty() && transforms_source >= TRANSFORMS_FROM_HELPER_GRID) {
        transforms = BulletFactory2D::helper_apply_skip_indices(transforms, helper_skip_indices);
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
    // Cleanup first: older versions drew the preview with Line2D strips
    // ("Dots"/"Ticks"). Remove any leftover before adopting/creating the
    // plain Node2D layers, so a healed holder can never clash on child names
    // and upgraded scenes keep no stray-segment nodes around.
    TypedArray<Node> stray_lines = preview_holder->find_children("*", "Line2D", false, false);
    for (int i = 0; i < stray_lines.size(); i++) {
        Node *stray = Object::cast_to<Node>(stray_lines[i]);
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
        Node *old_dots = preview_holder->get_node_or_null(NodePath("Dots"));
        if (old_dots != nullptr) {
            preview_holder->remove_child(old_dots);
            memdelete(old_dots);
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
        Node *old_arrows = preview_holder->get_node_or_null(NodePath("Arrows"));
        if (old_arrows != nullptr) {
            preview_holder->remove_child(old_arrows);
            memdelete(old_arrows);
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
                } else if (property_name == "homing_target_scorer" || property_name == "homing_priority_group" || property_name == "homing_fire_requires_target" || property_name == "homing_sticky_targets") {
                    show = multi_source;
                } else if (property_name == "homing_target_priority") {
                    show = multi_source;
                } else if (property_name == "homing_delay_sec" || property_name == "homing_duration_sec" || property_name == "homing_lose_range_px" || property_name == "homing_fire_arc_deg") {
                    show = true;
                } else if (property_name == "reload_jitter_sec") {
                    show = true;
                } else if (property_name == "homing_retarget_phase") {
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
                } else if (property_name == "orbiting_follow_deadzone") {
                    show = orbiting_follow_mode == DirectionalBullets2D::FollowDeadzone;
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
    } else if (property_name.begins_with("helper_flower_")) {
        relevant = transforms_source == TRANSFORMS_FROM_HELPER_FLOWER;
    } else if (property_name.begins_with("helper_ellipse_")) {
        relevant = transforms_source == TRANSFORMS_FROM_HELPER_ELLIPSE;
        // WALL-only knobs: hiding them outside WALL keeps the ellipse group
        // tight (gap_count = 0 alone already disables gaps silently).
        if (relevant && (property_name == "helper_ellipse_gap_count" || property_name == "helper_ellipse_gap_width")) {
            relevant = helper_ellipse_mode == (int)BulletFactory2D::ELLIPSE_WALL;
        }
    } else if (property_name.begins_with("helper_rain_")) {
        relevant = transforms_source == TRANSFORMS_FROM_HELPER_RAIN;
    } else if (property_name.begins_with("helper_scatter_")) {
        relevant = transforms_source == TRANSFORMS_FROM_HELPER_SCATTER;
    } else if (property_name.begins_with("helper_polygon_")) {
        relevant = transforms_source == TRANSFORMS_FROM_HELPER_POLYGON;
    } else if (property_name.begins_with("helper_multispiral_")) {
        relevant = transforms_source == TRANSFORMS_FROM_HELPER_MULTISPIRAL;
    } else if (property_name.begins_with("helper_cross_")) {
        relevant = transforms_source == TRANSFORMS_FROM_HELPER_CROSS;
    } else if (property_name.begins_with("helper_star_")) {
        relevant = transforms_source == TRANSFORMS_FROM_HELPER_STAR;
    } else if (property_name.begins_with("helper_heart_")) {
        relevant = transforms_source == TRANSFORMS_FROM_HELPER_HEART;
    } else if (property_name.begins_with("helper_wave_")) {
        relevant = transforms_source == TRANSFORMS_FROM_HELPER_WAVE;
    } else if (property_name.begins_with("helper_waterfall_")) {
        relevant = transforms_source == TRANSFORMS_FROM_HELPER_WATERFALL;
    } else if (property_name.begins_with("helper_lattice_")) {
        relevant = transforms_source == TRANSFORMS_FROM_HELPER_LATTICE;
    } else if (property_name == "helper_skip_indices") {
        relevant = transforms_source >= TRANSFORMS_FROM_HELPER_GRID;
    } else if (property_name == "helper_bullets_amount") {
        relevant = transforms_source >= TRANSFORMS_FROM_HELPER_GRID;
    }
    if (!relevant) {
        p_property.usage &= ~PROPERTY_USAGE_EDITOR;
    }
}

bool BulletSpawner2D::shoot_once() {
    // No nesting: the configuring signals fired below AND volley_fired run
    // user code synchronously, and a nested shoot_once() from such a handler
    // would recurse without bound and race volleys_fired past the cap.
    // The per-spawner latch stops self-recursion; the global depth stops
    // cross-spawner A->B->A ping-pong through one shared factory pool.
    // The latch is taken at entry (not just around the configuring emits):
    // the early volley_skipped emits below also run user code, and a handler
    // calling shoot_once() from there must be rejected the same way.
    // Error string names all guarded signals (kept as-is, truthful).
    if (shoot_once_reentrant_guard || g_shoot_once_nesting_depth > 0) {
        UtilityFunctions::push_error("BulletSpawner2D::shoot_once: re-entrant call from inside a spawner signal handler is not allowed (pre_shoot, homing_targets_resolved, volley_homing_configured, volley_fired, volley_skipped). Use call_deferred(\"shoot_once\") instead.");
        return false;
    }
    // Take the latch for the whole body: every emit below (volley_skipped,
    // pre_shoot, configuring signals, volley_fired) runs user code that must
    // not nest. All returns below must go through the single clear-point at
    // the end (or the pre-emit failure clear), never a bare return.
    shoot_once_reentrant_guard = true;
    ++g_shoot_once_nesting_depth;
    // Clear helper: every early failure below reports + signals + clears both
    // latch and depth, then returns false. Keeps the paths identical so a new
    // early return can never leak the latch (permanent shoot lockout) or the
    // depth (permanent cross-spawner lockout). Signals run under the latch,
    // so a volley_skipped handler calling shoot_once() is rejected instead
    // of recursing.
    auto fail_early = [&](const char *message, const StringName &skip_reason, bool with_signal) -> bool {
        if (message != nullptr) {
            UtilityFunctions::push_error(message);
        }
        if (with_signal) {
            emit_signal("volley_skipped", skip_reason);
        }
        shoot_once_reentrant_guard = false;
        --g_shoot_once_nesting_depth;
        if (g_shoot_once_nesting_depth < 0) {
            g_shoot_once_nesting_depth = 0;
        }
        return false;
    };
    BulletFactory2D *factory = get_bullet_factory();
    if (factory == nullptr) {
        return fail_early("BulletSpawner2D::shoot_once: no BulletFactory2D assigned (bullet_factory_path).", StringName(), false);
    }
    if (spawn_data.is_null()) {
        return fail_early("BulletSpawner2D::shoot_once: no spawn_data assigned.", StringName(), false);
    }
    // Duplicate per volley: the user's resource must never be mutated (its
    // transforms get overwritten below), so shared .tres files stay safe.
    Ref<DirectionalBulletsData2D> volley_data(Object::cast_to<DirectionalBulletsData2D>(spawn_data->duplicate().ptr()));
    if (volley_data.is_null()) {
        return fail_early("BulletSpawner2D::shoot_once: could not duplicate spawn_data.", StringName(), false);
    }
    volley_data->set_transforms(collect_spawn_transforms());
    if (volley_data->get_transforms().is_empty()) {
        // Fail loud with the mode name: the generic factory "no transforms"
        // error alone never says which source misfired (unset aimed target,
        // rejected helper input, ...). The cause was already reported above.
        // Reported as a skipped volley (not just an error) so games can fall
        // back instead of log-diving.
        return fail_early(String(String("BulletSpawner2D::shoot_once: transforms_source ") + transforms_source_name(transforms_source) + " produced no transforms, volley skipped.").utf8().get_data(), StringName("no_transforms"), true);
    }
    // Tower-defense hold-fire: no target, no shot, no volley counted. Quiet
    // by design (towers idle most of the time); the skip signal carries it.
    // The fire arc gates the same way: a target outside the turret cone
    // holds fire instead of wasting a volley off-arc.
    if (homing_enabled && homing_fire_requires_target && homing_target_source != HOMING_SOURCE_MOUSE) {
        Array precheck = resolve_homing_targets(true, false);
        if (precheck.is_empty()) {
            return fail_early(nullptr, StringName("no_target"), true);
        }
        if (homing_fire_arc_deg > 0.0 && !fire_arc_covers_targets(precheck)) {
            return fail_early(nullptr, StringName("off_arc"), true);
        }
    }
    // Soft live-bullet fuse: pause firing (not erroring) while the cap holds.
    // Reports as skipped so budget governors can observe the stall.
    if (max_live_bullets > 0 && get_active_live_bullet_count() >= max_live_bullets) {
        return fail_early(nullptr, StringName("over_budget"), true);
    }
    DirectionalBullets2D *bullets = factory->spawn_controllable_directional_bullets(volley_data);
    if (bullets == nullptr) {
        // Factory already reported why (busy/teardown/bad data): clear and
        // report, no skip signal (nothing about the request was skippable).
        shoot_once_reentrant_guard = false;
        --g_shoot_once_nesting_depth;
        if (g_shoot_once_nesting_depth < 0) {
            g_shoot_once_nesting_depth = 0;
        }
        return false;
    }
    // Tag the instance (fresh or pooled): from here on its area_entered,
    // body_entered and life_time_over signals are possessed by this spawner
    // instead of the factory. Pool reuse resets the tag, so this stamp covers
    // every spawn path through this function.
    bullets->owner_spawner_id = get_instance_id();
    // Capture the id BEFORE user code runs: the configuring signals below
    // execute handlers synchronously, and a handler may free or re-home this
    // volley. The raw pointer must not be touched again without validation.
    // (Latch/depth were already taken at entry; no re-take here.)
    const uint64_t volley_id = bullets->get_instance_id();
    const uint64_t self_id = get_instance_id();
    // Mutable last-chance hook: handlers may tweak the duplicated volley data
    // (speed ramp, count scale by phase/difficulty) before it spawns. The
    // volley instance is already stamped, so ownership checks still apply.
    emit_signal("pre_shoot", bullets, volleys_fired + 1);
    // Homing/orbiting runs on the same stamp: the instance is fully
    // configured before volley_fired, so handlers observe live behavior.
    // The latch stays up through volley_fired below (not just the configuring
    // signals): every emit on this path runs user code synchronously.
    apply_volley_homing_and_orbiting(bullets);
    // Re-validate: handlers of pre_shoot/homing_targets_resolved/
    // volley_homing_configured ran above and may have freed this volley
    // (factory reset/free_*, queue_free) or handed it to another owner
    // (adopt_live_volley). Dereferencing the raw pointer now would be
    // use-after-free: resolve by id and compare BY VALUE.
    Object *live = UtilityFunctions::is_instance_id_valid(volley_id) ? ObjectDB::get_instance(ObjectID(volley_id)) : nullptr;
    DirectionalBullets2D *live_volley = Object::cast_to<DirectionalBullets2D>(live);
    if (live_volley == nullptr || live_volley != bullets || !live_volley->is_active || live_volley->owner_spawner_id != self_id) {
        // Volley is gone or foreign: counting it or emitting volley_fired for it
        // would lie about ownership and hand out a dead pointer. The spawn
        // itself succeeded (bullets were created), but this shot is dropped.
        // Pre-emit failure: clear both latch and global depth (nothing emitted).
        shoot_once_reentrant_guard = false;
        --g_shoot_once_nesting_depth;
        if (g_shoot_once_nesting_depth < 0) {
            g_shoot_once_nesting_depth = 0;
        }
        return false;
    }
    volleys_fired += 1;
    // The latch stays up through this emit AND the cap transition below: a
    // volley_fired (or shooting_finished) handler calling shoot_once() (same
    // or cross spawner) would otherwise recurse without bound past the
    // max_volleys cap. Single clear-point right after, so sequential
    // (non-nested) shots are unaffected. All pre-emit failure paths above
    // return while the guard is either unset or already cleared.
    emit_signal("volley_fired", bullets, volleys_fired);
    // Exact-equality = transition only: further manual shots past the cap do
    // not re-emit, and the setter path reports its own transition.
    // NOTE: a shooting_finished handler runs while the latch is still up, so
    // a nested shoot_once() from there is rejected the same way.
    if (max_volleys >= 0 && volleys_fired == max_volleys) {
        // Stop shooting, but stay awake while spinning, retargeting, bursting,
        // telegraphing, or previewing (same keep-awake set as everywhere else).
        set_process(spin_enabled || preview_active() || homing_retarget_active() || burst_shots_left > 0 || telegraph_pending);
        emit_signal("shooting_finished");
    }
    shoot_once_reentrant_guard = false;
    --g_shoot_once_nesting_depth;
    if (g_shoot_once_nesting_depth < 0) {
        g_shoot_once_nesting_depth = 0;
    }
    return true;
}

// Deferred variant for signal handlers / physics callbacks: queues the shot
// so it runs after the current emission / physics step instead of nesting.
bool BulletSpawner2D::shoot_once_deferred() {
    if (!is_inside_tree()) {
        UtilityFunctions::push_error("BulletSpawner2D::shoot_once_deferred: spawner is not inside the tree, shot dropped.");
        return false;
    }
    call_deferred("shoot_once");
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
        // reset the round-robin cursor, and re-arm the retarget pass (with
        // the stagger phase) so a scene change starts clean instead of
        // inheriting stale rotation.
        live_volley_instance_ids.clear();
        homing_round_robin_cursor = 0;
        homing_retarget_time_left = homing_retarget_phase;
        // Burst/telegraph never survive a tree exit: countdowns firing after
        // re-entry would double-fire into the new scene.
        burst_shots_left = 0;
        burst_time_left = 0.0;
        burst_mirror_next = false;
        burst_telegraph_done = false;
        burst_firing = false;
        telegraph_pending = false;
        telegraph_time_left = 0.0;
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
    if (delta <= 0.0) {
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
            const int retargeted = retarget_live_volleys();
            homing_retarget_time_left = homing_retarget_interval_sec;
            if (retargeted > 0) {
                emit_signal("retarget_applied", retargeted);
            }
        }
    }
    // Telegraph countdown: warn first, fire after. Transforms re-collect at
    // fire time, so markers that move during the warning still aim right.
    // Telegraph and burst are mutually exclusive states: a stale burst
    // countdown can never run while a telegraph is pending (begin_burst
    // always clears the telegraph), so only one branch fires per tick.
    if (telegraph_pending) {
        telegraph_time_left -= delta;
        if (telegraph_time_left <= 0.0) {
            telegraph_pending = false;
            fire_burst_volley();
        }
        // Telegraph never sleeps the loop: the countdown owns it.
        set_process(true);
        return;
    }
    // Burst chain countdown: mid-burst shots ignore the main interval.
    if (burst_shots_left > 0) {
        burst_time_left -= delta;
        if (burst_time_left <= 0.0) {
            fire_burst_volley();
        }
        set_process(true);
        return;
    }
    // Pattern-list sequencer: entries fire in order (interval apart) until
    // the queue drains, then the list goes idle. Runs even with auto-
    // shooting off (manual boss-phase driver).
    if (pattern_list_active) {
        pattern_list_time_left -= delta;
        if (pattern_list_time_left <= 0.0) {
            fire_pattern_list_entry();
            if (pattern_list_active) {
                pattern_list_time_left = pattern_list_interval_sec;
            }
        }
        set_process(true);
        return;
    }
    if (!auto_shooting_active()) {
        // Sleep when there is nothing to do, but stay awake while spinning,
        // retargeting, bursting, telegraphing, sequencing, or while the
        // runtime preview loop must keep refreshing.
        set_process(spin_enabled || preview_active() || homing_retarget_active() || burst_shots_left > 0 || telegraph_pending || pattern_list_active);
        return;
    }
    shoot_time_left -= delta;
    if (shoot_time_left > 0.0) {
        return;
    }
    // Trigger pull: burst mode fans out from here, plain mode fires once.
    // Telegraph inserts the warning first (fire happens on countdown).
    if (burst_enabled && burst_count > 1) {
        begin_burst();
    } else if (telegraph_enabled && telegraph_sec > 0.0 && !telegraph_pending) {
        begin_telegraph();
    } else {
        shoot_once(); // errors, if any, are per-attempt (interval-gated, no spam storm)
    }
    shoot_time_left = next_shoot_interval_sec();
}

void BulletSpawner2D::begin_burst() {
    // A fresh trigger always owns the phrasing: stale telegraphs from an
    // interrupted trigger never fire into the new burst. Plain-burst mode
    // with count 1 collapses to a single immediate shot (no chain to drain).
    telegraph_pending = false;
    telegraph_time_left = 0.0;
    burst_telegraph_done = false;
    if (!burst_enabled || burst_count <= 1) {
        burst_shots_left = 0;
        fire_burst_volley();
        return;
    }
    burst_shots_left = burst_count;
    burst_time_left = 0.0;
    set_process(true);
}

void BulletSpawner2D::begin_telegraph() {
    // Snapshot the aim for the warning signal; the actual fire re-collects,
    // so this is advisory (preview/telegraph visuals), never stale logic.
    TypedArray<Transform2D> aim = collect_spawn_transforms_impl(true);
    telegraph_pending = true;
    telegraph_time_left = telegraph_sec;
    emit_signal("volley_telegraphed", aim);
    set_process(true);
}

void BulletSpawner2D::fire_burst_volley() {
    if (burst_enabled && burst_count > 1) {
        if (burst_shots_left <= 0) {
            return;
        }
        if (telegraph_enabled && telegraph_sec > 0.0 && !telegraph_pending && !burst_telegraph_done && burst_shots_left == burst_count) {
            // First burst shot warns once; done-flag stops the expiry from
            // re-firing the warning in a loop instead of firing the shot.
            burst_telegraph_done = true;
            begin_telegraph();
            return;
        }
        // Mirror alternates every burst shot: even shots fire mirrored, odd
        // shots plain (the classic reverse-the-angle rhythm). The toggle
        // flips only on alternate mode; plain bursts never touch the flag.
        const bool mirrored = burst_alternate_mirror && (burst_shots_left % 2 == 0);
        burst_mirror_next = mirrored;
        burst_firing = true;
        const bool fired = shoot_once();
        burst_firing = false;
        if (fired) {
            emit_signal("burst_shot_fired", burst_count - burst_shots_left + 1, mirrored);
        }
        burst_mirror_next = false;
        --burst_shots_left;
        burst_time_left = burst_interval_sec;
        if (burst_shots_left <= 0) {
            burst_telegraph_done = false;
            emit_signal("burst_finished");
        }
        set_process(true);
        return;
    }
    // Plain telegraphed shot (also the manual-begin_burst escape hatch when
    // burst mode is off: a hand-started chain with no burst config must fire
    // exactly once, not loop forever on a stale burst_shots_left).
    telegraph_pending = false;
    burst_shots_left = 0;
    burst_telegraph_done = false;
    burst_mirror_next = false;
    burst_firing = true;
    shoot_once();
    burst_firing = false;
    set_process(auto_shooting_active() || spin_enabled || homing_retarget_active() || preview_active() || pattern_list_active);
}

double BulletSpawner2D::next_shoot_interval_sec() const {
    // Reload jitter: +/- uniform jitter around the base interval (seeded by
    // pattern_seed for replays; non-deterministic at 0). Clamped to a small
    // positive floor so a huge jitter can never invert or stall the timer.
    if (reload_jitter_sec <= 0.0 || !Math::is_finite(reload_jitter_sec)) {
        return shoot_interval_sec;
    }
    Ref<RandomNumberGenerator> rng = homing_rng;
    if (rng.is_null()) {
        rng.instantiate();
    }
    if (pattern_seed != 0) {
        rng->set_seed((uint64_t)pattern_seed + (uint64_t)volleys_fired);
    } else {
        rng->randomize();
    }
    const double jitter = rng->randf_range(-reload_jitter_sec, reload_jitter_sec);
    return Math::max(0.05, shoot_interval_sec + jitter);
}

int BulletSpawner2D::spawn_pattern_list(const Array &entries, bool simultaneous, double interval_sec) {
    if (entries.is_empty()) {
        UtilityFunctions::push_error("BulletSpawner2D::spawn_pattern_list: entries is empty, nothing queued.");
        return 0;
    }
    if (!Math::is_finite(interval_sec) || interval_sec < 0.0) {
        UtilityFunctions::push_error("BulletSpawner2D::spawn_pattern_list: interval_sec must be finite and >= 0, keeping simultaneous fire.");
        interval_sec = 0.0;
    }
    if (!is_inside_tree()) {
        UtilityFunctions::push_error("BulletSpawner2D::spawn_pattern_list: spawner is outside the scene tree.");
        return 0;
    }
    pattern_list_entries = entries.duplicate();
    pattern_list_simultaneous = simultaneous;
    pattern_list_interval_sec = interval_sec;
    pattern_list_cursor = 0;
    pattern_list_time_left = 0.0;
    pattern_list_active = true;
    if (simultaneous) {
        // One tick, N shots: each entry overrides the live source, fires via
        // the guarded shoot_once() path (homing/orbit/signals consistent),
        // then restores. A bad entry skips with an error, never aborts.
        const TransformsSource saved_source = transforms_source;
        const int saved_amount = helper_bullets_amount;
        Ref<DirectionalBulletsData2D> saved_data = spawn_data;
        int fired = 0;
        for (int i = 0; i < pattern_list_entries.size(); ++i) {
            if (apply_pattern_list_entry(pattern_list_entries[i])) {
                if (shoot_once()) {
                    ++fired;
                }
            }
            transforms_source = saved_source;
            helper_bullets_amount = saved_amount;
            spawn_data = saved_data;
        }
        pattern_list_active = false;
        pattern_list_entries.clear();
        pattern_list_cursor = 0;
        notify_property_list_changed();
        rebuild_preview();
        return fired;
    }
    set_process(true);
    return pattern_list_entries.size();
}

void BulletSpawner2D::stop_pattern_list() {
    pattern_list_active = false;
    pattern_list_entries.clear();
    pattern_list_cursor = 0;
    pattern_list_time_left = 0.0;
    set_process(auto_shooting_active() || spin_enabled || homing_retarget_active() || preview_active() || burst_shots_left > 0 || telegraph_pending);
}

bool BulletSpawner2D::is_pattern_list_active() const {
    return pattern_list_active;
}

bool BulletSpawner2D::apply_pattern_list_entry(const Variant &entry) {
    if (entry.get_type() != Variant::DICTIONARY) {
        UtilityFunctions::push_error("BulletSpawner2D::spawn_pattern_list: every entry must be a Dictionary, skipping one.");
        return false;
    }
    const Dictionary dict = entry;
    if (dict.has("preset")) {
        Variant v = dict["preset"];
        if (v.get_type() == Variant::INT) {
            apply_pattern_preset((int)v);
        } else {
            UtilityFunctions::push_error("BulletSpawner2D::spawn_pattern_list: entry 'preset' must be an int, skipping preset.");
        }
    }
    if (dict.has("transforms_source")) {
        Variant v = dict["transforms_source"];
        if (v.get_type() == Variant::INT) {
            const int src = (int)v;
            if (src < (int)TRANSFORMS_FROM_CHILDREN || src > (int)TRANSFORMS_FROM_HELPER_LATTICE) {
                UtilityFunctions::push_error("BulletSpawner2D::spawn_pattern_list: entry 'transforms_source' out of range, keeping current.");
            } else {
                set_transforms_source((TransformsSource)src);
            }
        } else {
            UtilityFunctions::push_error("BulletSpawner2D::spawn_pattern_list: entry 'transforms_source' must be an int, keeping current.");
        }
    }
    if (dict.has("helper_bullets_amount")) {
        Variant v = dict["helper_bullets_amount"];
        if (v.get_type() == Variant::INT) {
            set_helper_bullets_amount((int)v);
        } else {
            UtilityFunctions::push_error("BulletSpawner2D::spawn_pattern_list: entry 'helper_bullets_amount' must be an int, keeping current.");
        }
    }
    if (dict.has("spawn_data")) {
        Variant v = dict["spawn_data"];
        Ref<DirectionalBulletsData2D> override_data = v;
        if (override_data.is_valid()) {
            Ref<DirectionalBulletsData2D> saved = spawn_data;
            spawn_data = override_data;
            // Restored by the caller after shoot_once(); stash on the entry
            // is unnecessary since simultaneous/simulated paths restore below.
            (void)saved;
        } else {
            UtilityFunctions::push_error("BulletSpawner2D::spawn_pattern_list: entry 'spawn_data' must be a DirectionalBulletsData2D, keeping current.");
        }
    }
    return true;
}

void BulletSpawner2D::fire_pattern_list_entry() {
    if (!pattern_list_active || pattern_list_cursor < 0 || pattern_list_cursor >= pattern_list_entries.size()) {
        stop_pattern_list();
        return;
    }
    // Sequential mode restores the base source after every shot so entries
    // are overrides, not permanent inspector edits... except the source
    // itself stays (boss phases visibly advance). Amount restores too.
    const TransformsSource saved_source = transforms_source;
    const int saved_amount = helper_bullets_amount;
    Ref<DirectionalBulletsData2D> saved_data = spawn_data;
    Variant entry = pattern_list_entries[pattern_list_cursor];
    ++pattern_list_cursor;
    bool entry_ok = apply_pattern_list_entry(entry);
    // spawn_data override from the entry (if any) is live now.
    Ref<DirectionalBulletsData2D> entry_data = spawn_data;
    if (entry_ok) {
        shoot_once();
    }
    spawn_data = saved_data;
    helper_bullets_amount = saved_amount;
    (void)entry_data;
    if (pattern_list_cursor >= pattern_list_entries.size()) {
        // Keep the last entry's source (phase advanced), drop the queue.
        stop_pattern_list();
        notify_property_list_changed();
        rebuild_preview();
        emit_signal("pattern_list_finished");
    } else {
        transforms_source = saved_source;
        notify_property_list_changed();
        rebuild_preview();
    }
}

bool BulletSpawner2D::fire_arc_covers_targets(const Array &targets) const {
    if (homing_fire_arc_deg <= 0.0 || !Math::is_finite(homing_fire_arc_deg)) {
        return true;
    }
    if (targets.is_empty() || !is_inside_tree()) {
        return false;
    }
    const Vector2 origin = get_global_position();
    const real_t facing = get_global_rotation();
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
	ADD_SIGNAL(MethodInfo("pre_shoot",
		PropertyInfo(Variant::OBJECT, "directional_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBullets2D"),
		PropertyInfo(Variant::INT, "volley_index")));
	ADD_SIGNAL(MethodInfo("volley_fired",
		PropertyInfo(Variant::OBJECT, "directional_bullets_instance", PROPERTY_HINT_RESOURCE_TYPE, "DirectionalBullets2D"),
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
	ADD_PROPERTY(PropertyInfo(Variant::INT, "transforms_source", PROPERTY_HINT_ENUM, "From Children,From Self,Grid,Ring,Fan,Spiral,Line,Aimed,Flower,Ellipse,Rain,Scatter,Polygon,Multi Spiral,Cross,Star,Heart,Wave,Waterfall,Lattice"), "set_transforms_source", "get_transforms_source");

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

	ClassDB::bind_method(D_METHOD("get_helper_aimed_prediction"), &BulletSpawner2D::get_helper_aimed_prediction);
	ClassDB::bind_method(D_METHOD("set_helper_aimed_prediction", "value"), &BulletSpawner2D::set_helper_aimed_prediction);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_aimed_prediction"), "set_helper_aimed_prediction", "get_helper_aimed_prediction");

	ClassDB::bind_method(D_METHOD("get_helper_aimed_prediction_time"), &BulletSpawner2D::get_helper_aimed_prediction_time);
	ClassDB::bind_method(D_METHOD("set_helper_aimed_prediction_time", "value"), &BulletSpawner2D::set_helper_aimed_prediction_time);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_aimed_prediction_time"), "set_helper_aimed_prediction_time", "get_helper_aimed_prediction_time");

	ClassDB::bind_method(D_METHOD("get_helper_flower_petals"), &BulletSpawner2D::get_helper_flower_petals);
	ClassDB::bind_method(D_METHOD("set_helper_flower_petals", "value"), &BulletSpawner2D::set_helper_flower_petals);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_flower_petals"), "set_helper_flower_petals", "get_helper_flower_petals");

	ClassDB::bind_method(D_METHOD("get_helper_flower_bullets_per_petal"), &BulletSpawner2D::get_helper_flower_bullets_per_petal);
	ClassDB::bind_method(D_METHOD("set_helper_flower_bullets_per_petal", "value"), &BulletSpawner2D::set_helper_flower_bullets_per_petal);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_flower_bullets_per_petal"), "set_helper_flower_bullets_per_petal", "get_helper_flower_bullets_per_petal");

	ClassDB::bind_method(D_METHOD("get_helper_flower_radius"), &BulletSpawner2D::get_helper_flower_radius);
	ClassDB::bind_method(D_METHOD("set_helper_flower_radius", "value"), &BulletSpawner2D::set_helper_flower_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_flower_radius"), "set_helper_flower_radius", "get_helper_flower_radius");

	ClassDB::bind_method(D_METHOD("get_helper_flower_petal_spread"), &BulletSpawner2D::get_helper_flower_petal_spread);
	ClassDB::bind_method(D_METHOD("set_helper_flower_petal_spread", "value"), &BulletSpawner2D::set_helper_flower_petal_spread);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_flower_petal_spread"), "set_helper_flower_petal_spread", "get_helper_flower_petal_spread");

	ClassDB::bind_method(D_METHOD("get_helper_flower_petal_sharpness"), &BulletSpawner2D::get_helper_flower_petal_sharpness);
	ClassDB::bind_method(D_METHOD("set_helper_flower_petal_sharpness", "value"), &BulletSpawner2D::set_helper_flower_petal_sharpness);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_flower_petal_sharpness"), "set_helper_flower_petal_sharpness", "get_helper_flower_petal_sharpness");

	ClassDB::bind_method(D_METHOD("get_helper_flower_base_rotation"), &BulletSpawner2D::get_helper_flower_base_rotation);
	ClassDB::bind_method(D_METHOD("set_helper_flower_base_rotation", "value"), &BulletSpawner2D::set_helper_flower_base_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_flower_base_rotation"), "set_helper_flower_base_rotation", "get_helper_flower_base_rotation");

	ClassDB::bind_method(D_METHOD("get_helper_flower_face_outward"), &BulletSpawner2D::get_helper_flower_face_outward);
	ClassDB::bind_method(D_METHOD("set_helper_flower_face_outward", "value"), &BulletSpawner2D::set_helper_flower_face_outward);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_flower_face_outward"), "set_helper_flower_face_outward", "get_helper_flower_face_outward");

	ClassDB::bind_method(D_METHOD("get_helper_flower_facing_offset_deg"), &BulletSpawner2D::get_helper_flower_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_flower_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_flower_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_flower_facing_offset_deg"), "set_helper_flower_facing_offset_deg", "get_helper_flower_facing_offset_deg");

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

	ClassDB::bind_method(D_METHOD("get_helper_scatter_burst_radius"), &BulletSpawner2D::get_helper_scatter_burst_radius);
	ClassDB::bind_method(D_METHOD("set_helper_scatter_burst_radius", "value"), &BulletSpawner2D::set_helper_scatter_burst_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_scatter_burst_radius"), "set_helper_scatter_burst_radius", "get_helper_scatter_burst_radius");

	ClassDB::bind_method(D_METHOD("get_helper_scatter_facing_jitter"), &BulletSpawner2D::get_helper_scatter_facing_jitter);
	ClassDB::bind_method(D_METHOD("set_helper_scatter_facing_jitter", "value"), &BulletSpawner2D::set_helper_scatter_facing_jitter);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_scatter_facing_jitter"), "set_helper_scatter_facing_jitter", "get_helper_scatter_facing_jitter");

	ClassDB::bind_method(D_METHOD("get_helper_scatter_seed"), &BulletSpawner2D::get_helper_scatter_seed);
	ClassDB::bind_method(D_METHOD("set_helper_scatter_seed", "value"), &BulletSpawner2D::set_helper_scatter_seed);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_scatter_seed"), "set_helper_scatter_seed", "get_helper_scatter_seed");

	ClassDB::bind_method(D_METHOD("get_helper_polygon_vertices"), &BulletSpawner2D::get_helper_polygon_vertices);
	ClassDB::bind_method(D_METHOD("set_helper_polygon_vertices", "value"), &BulletSpawner2D::set_helper_polygon_vertices);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "helper_polygon_vertices"), "set_helper_polygon_vertices", "get_helper_polygon_vertices");

	ClassDB::bind_method(D_METHOD("get_helper_polygon_radius"), &BulletSpawner2D::get_helper_polygon_radius);
	ClassDB::bind_method(D_METHOD("set_helper_polygon_radius", "value"), &BulletSpawner2D::set_helper_polygon_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_polygon_radius"), "set_helper_polygon_radius", "get_helper_polygon_radius");

	ClassDB::bind_method(D_METHOD("get_helper_polygon_vertex_bias"), &BulletSpawner2D::get_helper_polygon_vertex_bias);
	ClassDB::bind_method(D_METHOD("set_helper_polygon_vertex_bias", "value"), &BulletSpawner2D::set_helper_polygon_vertex_bias);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_polygon_vertex_bias"), "set_helper_polygon_vertex_bias", "get_helper_polygon_vertex_bias");

	ClassDB::bind_method(D_METHOD("get_helper_polygon_base_rotation"), &BulletSpawner2D::get_helper_polygon_base_rotation);
	ClassDB::bind_method(D_METHOD("set_helper_polygon_base_rotation", "value"), &BulletSpawner2D::set_helper_polygon_base_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_polygon_base_rotation"), "set_helper_polygon_base_rotation", "get_helper_polygon_base_rotation");

	ClassDB::bind_method(D_METHOD("get_helper_polygon_face_outward"), &BulletSpawner2D::get_helper_polygon_face_outward);
	ClassDB::bind_method(D_METHOD("set_helper_polygon_face_outward", "value"), &BulletSpawner2D::set_helper_polygon_face_outward);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_polygon_face_outward"), "set_helper_polygon_face_outward", "get_helper_polygon_face_outward");

	ClassDB::bind_method(D_METHOD("get_helper_polygon_facing_offset_deg"), &BulletSpawner2D::get_helper_polygon_facing_offset_deg);
	ClassDB::bind_method(D_METHOD("set_helper_polygon_facing_offset_deg", "value"), &BulletSpawner2D::set_helper_polygon_facing_offset_deg);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_polygon_facing_offset_deg"), "set_helper_polygon_facing_offset_deg", "get_helper_polygon_facing_offset_deg");

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

	ClassDB::bind_method(D_METHOD("get_helper_skip_indices"), &BulletSpawner2D::get_helper_skip_indices);
	ClassDB::bind_method(D_METHOD("set_helper_skip_indices", "value"), &BulletSpawner2D::set_helper_skip_indices);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "helper_skip_indices"), "set_helper_skip_indices", "get_helper_skip_indices");

	ClassDB::bind_method(D_METHOD("get_homing_target_priority"), &BulletSpawner2D::get_homing_target_priority);
	ClassDB::bind_method(D_METHOD("set_homing_target_priority", "value"), &BulletSpawner2D::set_homing_target_priority);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "homing_target_priority", PROPERTY_HINT_ENUM, "Nearest,First,Last,Strongest,Weakest,Fastest,Random,Custom"), "set_homing_target_priority", "get_homing_target_priority");

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

	ClassDB::bind_method(D_METHOD("get_reload_jitter_sec"), &BulletSpawner2D::get_reload_jitter_sec);
	ClassDB::bind_method(D_METHOD("set_reload_jitter_sec", "value"), &BulletSpawner2D::set_reload_jitter_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reload_jitter_sec"), "set_reload_jitter_sec", "get_reload_jitter_sec");

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

	ClassDB::bind_method(D_METHOD("get_homing_target_scorer"), &BulletSpawner2D::get_homing_target_scorer);
	ClassDB::bind_method(D_METHOD("set_homing_target_scorer", "value"), &BulletSpawner2D::set_homing_target_scorer);
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "homing_target_scorer"), "set_homing_target_scorer", "get_homing_target_scorer");

	ClassDB::bind_method(D_METHOD("get_homing_priority_group"), &BulletSpawner2D::get_homing_priority_group);
	ClassDB::bind_method(D_METHOD("set_homing_priority_group", "value"), &BulletSpawner2D::set_homing_priority_group);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "homing_priority_group"), "set_homing_priority_group", "get_homing_priority_group");

	ClassDB::bind_method(D_METHOD("get_homing_fire_requires_target"), &BulletSpawner2D::get_homing_fire_requires_target);
	ClassDB::bind_method(D_METHOD("set_homing_fire_requires_target", "value"), &BulletSpawner2D::set_homing_fire_requires_target);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_fire_requires_target"), "set_homing_fire_requires_target", "get_homing_fire_requires_target");

	ClassDB::bind_method(D_METHOD("get_homing_sticky_targets"), &BulletSpawner2D::get_homing_sticky_targets);
	ClassDB::bind_method(D_METHOD("set_homing_sticky_targets", "value"), &BulletSpawner2D::set_homing_sticky_targets);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "homing_sticky_targets"), "set_homing_sticky_targets", "get_homing_sticky_targets");

	ClassDB::bind_method(D_METHOD("get_pattern_seed"), &BulletSpawner2D::get_pattern_seed);
	ClassDB::bind_method(D_METHOD("set_pattern_seed", "value"), &BulletSpawner2D::set_pattern_seed);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "pattern_seed"), "set_pattern_seed", "get_pattern_seed");

	ClassDB::bind_method(D_METHOD("get_max_live_bullets"), &BulletSpawner2D::get_max_live_bullets);
	ClassDB::bind_method(D_METHOD("set_max_live_bullets", "value"), &BulletSpawner2D::set_max_live_bullets);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_live_bullets"), "set_max_live_bullets", "get_max_live_bullets");

	ClassDB::bind_method(D_METHOD("get_homing_retarget_phase"), &BulletSpawner2D::get_homing_retarget_phase);
	ClassDB::bind_method(D_METHOD("set_homing_retarget_phase", "value"), &BulletSpawner2D::set_homing_retarget_phase);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "homing_retarget_phase"), "set_homing_retarget_phase", "get_homing_retarget_phase");

	ClassDB::bind_method(D_METHOD("apply_pattern_preset", "preset"), &BulletSpawner2D::apply_pattern_preset);
	ClassDB::bind_method(D_METHOD("spawn_pattern_list", "entries", "simultaneous", "interval_sec"), &BulletSpawner2D::spawn_pattern_list, DEFVAL(false), DEFVAL(0.25));
	ClassDB::bind_method(D_METHOD("stop_pattern_list"), &BulletSpawner2D::stop_pattern_list);
	ClassDB::bind_method(D_METHOD("is_pattern_list_active"), &BulletSpawner2D::is_pattern_list_active);
	ClassDB::bind_method(D_METHOD("next_shoot_interval_sec"), &BulletSpawner2D::next_shoot_interval_sec);
	ClassDB::bind_method(D_METHOD("get_burst_shots_left"), &BulletSpawner2D::get_burst_shots_left);
	ClassDB::bind_method(D_METHOD("get_active_live_bullet_count"), &BulletSpawner2D::get_active_live_bullet_count);
	ClassDB::bind_method(D_METHOD("get_pooled_volley_count"), &BulletSpawner2D::get_pooled_volley_count);
	ClassDB::bind_method(D_METHOD("begin_burst"), &BulletSpawner2D::begin_burst);
	ClassDB::bind_method(D_METHOD("begin_telegraph"), &BulletSpawner2D::begin_telegraph);
	ClassDB::bind_method(D_METHOD("fire_burst_volley"), &BulletSpawner2D::fire_burst_volley);

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
	BIND_ENUM_CONSTANT(HOMING_PRIORITY_NEAREST);
	BIND_ENUM_CONSTANT(HOMING_PRIORITY_FIRST);
	BIND_ENUM_CONSTANT(HOMING_PRIORITY_LAST);
	BIND_ENUM_CONSTANT(HOMING_PRIORITY_STRONGEST);
	BIND_ENUM_CONSTANT(HOMING_PRIORITY_WEAKEST);
	BIND_ENUM_CONSTANT(HOMING_PRIORITY_FASTEST);
	BIND_ENUM_CONSTANT(HOMING_PRIORITY_RANDOM);
	BIND_ENUM_CONSTANT(HOMING_PRIORITY_CUSTOM);
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
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_FLOWER);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_ELLIPSE);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_RAIN);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_SCATTER);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_POLYGON);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_MULTISPIRAL);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_CROSS);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_STAR);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_HEART);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_WAVE);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_WATERFALL);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_LATTICE);

	// Need this in order to expose the enum constants to Godot Engine
	BIND_ENUM_CONSTANT(SPIN_CONTINUOUS);
	BIND_ENUM_CONSTANT(SPIN_OSCILLATE);

	ClassDB::bind_method(D_METHOD("get_volleys_fired"), &BulletSpawner2D::get_volleys_fired);
	ClassDB::bind_method(D_METHOD("collect_spawn_transforms"), &BulletSpawner2D::collect_spawn_transforms);
	ClassDB::bind_method(D_METHOD("shoot_once"), &BulletSpawner2D::shoot_once);
	ClassDB::bind_method(D_METHOD("shoot_once_deferred"), &BulletSpawner2D::shoot_once_deferred);
	ClassDB::bind_method(D_METHOD("reset_shooting"), &BulletSpawner2D::reset_shooting);

}

}
