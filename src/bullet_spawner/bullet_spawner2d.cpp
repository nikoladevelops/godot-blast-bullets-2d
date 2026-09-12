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

// Uniformly scales a spawn transform relative to the generator origin, so
// marker offsets (spread radius) and basis (bullet size) grow together while
// rotation is preserved. Scale 1.0 returns the transform untouched.
static Transform2D scale_spawn_transform(const Transform2D &t, const Vector2 &base_origin, real_t scale) {
    if (scale == 1.0) {
        return t;
    }
    return Transform2D(t.columns[0] * scale, t.columns[1] * scale, base_origin + (t.columns[2] - base_origin) * scale);
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
}

Node2D *BulletSpawner2D::get_transforms_generator() const {
    return resolve_node_path(this, transforms_generator_path, transforms_generator);
}

void BulletSpawner2D::set_transforms_generator(Node2D *generator) {
    assign_node_to_path(this, generator, transforms_generator_path, transforms_generator);
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
        set_process(now_active);
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
        set_process(now_active);
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
}

bool BulletSpawner2D::get_helper_grid_rotate_with_marker() const {
    return helper_grid_rotate_with_marker;
}
void BulletSpawner2D::set_helper_grid_rotate_with_marker(bool value) {
    helper_grid_rotate_with_marker = value;
}

bool BulletSpawner2D::get_helper_grid_random_local_rotation() const {
    return helper_grid_random_local_rotation;
}
void BulletSpawner2D::set_helper_grid_random_local_rotation(bool value) {
    helper_grid_random_local_rotation = value;
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
}

bool BulletSpawner2D::get_helper_ring_rotate_with_marker() const {
    return helper_ring_rotate_with_marker;
}
void BulletSpawner2D::set_helper_ring_rotate_with_marker(bool value) {
    helper_ring_rotate_with_marker = value;
}

bool BulletSpawner2D::get_helper_ring_random_rotation() const {
    return helper_ring_random_rotation;
}
void BulletSpawner2D::set_helper_ring_random_rotation(bool value) {
    helper_ring_random_rotation = value;
}

bool BulletSpawner2D::get_helper_ring_face_outward() const {
    return helper_ring_face_outward;
}
void BulletSpawner2D::set_helper_ring_face_outward(bool value) {
    helper_ring_face_outward = value;
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
}

bool BulletSpawner2D::get_helper_spiral_rotate_with_marker() const {
    return helper_spiral_rotate_with_marker;
}
void BulletSpawner2D::set_helper_spiral_rotate_with_marker(bool value) {
    helper_spiral_rotate_with_marker = value;
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
}

bool BulletSpawner2D::get_helper_line_face_direction() const {
    return helper_line_face_direction;
}
void BulletSpawner2D::set_helper_line_face_direction(bool value) {
    helper_line_face_direction = value;
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
}

Node2D *BulletSpawner2D::get_helper_aimed_target() const {
    return resolve_node_path(this, helper_aimed_target_path, helper_aimed_target);
}

void BulletSpawner2D::set_helper_aimed_target(Node2D *target) {
    assign_node_to_path(this, target, helper_aimed_target_path, helper_aimed_target);
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
}

void BulletSpawner2D::reset_shooting() {
    volleys_fired = 0;
    shoot_time_left = shoot_initial_delay_sec;
    if (is_inside_tree()) {
        set_process(auto_shooting_active());
    }
    // An explicit restart counts as a (re)start whenever it arms shooting.
    if (auto_shooting_active()) {
        emit_signal("shooting_started");
    }
}

TypedArray<Transform2D> BulletSpawner2D::collect_spawn_transforms() const {
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
            raw = BulletFactory2D::helper_generate_transforms_grid(helper_bullets_amount, marker, helper_grid_rows_per_column, (BulletFactory2D::Alignment)helper_grid_alignment, helper_grid_column_offset, helper_grid_row_offset, helper_grid_rotate_with_marker, helper_grid_random_local_rotation);
            break;
        case TRANSFORMS_FROM_HELPER_RING:
            raw = BulletFactory2D::helper_generate_transforms_ring(helper_bullets_amount, marker, helper_ring_radius, helper_ring_start_angle, helper_ring_arc, helper_ring_rotate_with_marker, helper_ring_random_rotation, helper_ring_face_outward);
            break;
        case TRANSFORMS_FROM_HELPER_FAN:
            raw = BulletFactory2D::helper_generate_transforms_fan(helper_bullets_amount, marker, helper_fan_spread, helper_fan_direction_angle, helper_fan_step_offset);
            break;
        case TRANSFORMS_FROM_HELPER_SPIRAL:
            raw = BulletFactory2D::helper_generate_transforms_spiral(helper_bullets_amount, marker, helper_spiral_start_radius, helper_spiral_radius_step, helper_spiral_angle_step, helper_spiral_rotate_with_marker);
            break;
        case TRANSFORMS_FROM_HELPER_LINE:
            raw = BulletFactory2D::helper_generate_transforms_line(helper_bullets_amount, marker, helper_line_direction, helper_line_spacing, helper_line_face_direction);
            break;
        case TRANSFORMS_FROM_HELPER_AIMED: {
            Node2D *target = get_helper_aimed_target();
            if (target == nullptr) {
                UtilityFunctions::push_error("BulletSpawner2D::collect_spawn_transforms: no aimed target assigned (helper_aimed_target_path).");
                break;
            }
            raw = BulletFactory2D::helper_generate_transforms_aimed(helper_bullets_amount, marker, target->get_global_transform().get_origin(), helper_aimed_spread, helper_aimed_step_offset);
            break;
        }
        case TRANSFORMS_FROM_CHILDREN:
        default: {
            bool collected = false;
            for (int i = 0; i < base->get_child_count(); ++i) {
                Node2D *as_2d = Object::cast_to<Node2D>(base->get_child(i));
                if (as_2d != nullptr) {
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
    // Single scale pass for every source (identity when transforms_scale is 1.0).
    const real_t scale = (real_t)transforms_scale;
    TypedArray<Transform2D> transforms;
    for (int i = 0; i < raw.size(); ++i) {
        Transform2D t = raw[i];
        transforms.push_back(scale_spawn_transform(t, base_origin, scale));
    }
    return transforms;
}

void BulletSpawner2D::_validate_property(PropertyInfo &p_property) const {
    const String property_name = p_property.name;
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
        set_process(false);
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
        return;
    }
    volleys_fired = 0;
    shoot_time_left = shoot_initial_delay_sec;
    set_process(auto_shooting_active());
    if (auto_shooting_active()) {
        emit_signal("shooting_started");
    }
}

void BulletSpawner2D::_process(double delta) {
    // Self-healing: even if processing gets enabled in the editor somehow
    // (e.g. set_shooting_enabled(true) from an editor dock), never shoot.
    if (Engine::get_singleton()->is_editor_hint()) {
        set_process(false);
        return;
    }
    if (!auto_shooting_active()) {
        set_process(false);
        return;
    }
    if (!Math::is_finite(delta) || delta < 0.0) {
        return;
    }
    if (delta > 0.5) {
        delta = 0.5; // clamp hitch spikes so one stall can't fast-forward volleys
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

	ClassDB::bind_method(D_METHOD("get_helper_fan_spread"), &BulletSpawner2D::get_helper_fan_spread);
	ClassDB::bind_method(D_METHOD("set_helper_fan_spread", "value"), &BulletSpawner2D::set_helper_fan_spread);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_fan_spread"), "set_helper_fan_spread", "get_helper_fan_spread");

	ClassDB::bind_method(D_METHOD("get_helper_fan_direction_angle"), &BulletSpawner2D::get_helper_fan_direction_angle);
	ClassDB::bind_method(D_METHOD("set_helper_fan_direction_angle", "value"), &BulletSpawner2D::set_helper_fan_direction_angle);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_fan_direction_angle"), "set_helper_fan_direction_angle", "get_helper_fan_direction_angle");

	ClassDB::bind_method(D_METHOD("get_helper_fan_step_offset"), &BulletSpawner2D::get_helper_fan_step_offset);
	ClassDB::bind_method(D_METHOD("set_helper_fan_step_offset", "value"), &BulletSpawner2D::set_helper_fan_step_offset);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_fan_step_offset"), "set_helper_fan_step_offset", "get_helper_fan_step_offset");

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

	ClassDB::bind_method(D_METHOD("get_helper_line_direction"), &BulletSpawner2D::get_helper_line_direction);
	ClassDB::bind_method(D_METHOD("set_helper_line_direction", "value"), &BulletSpawner2D::set_helper_line_direction);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "helper_line_direction"), "set_helper_line_direction", "get_helper_line_direction");

	ClassDB::bind_method(D_METHOD("get_helper_line_spacing"), &BulletSpawner2D::get_helper_line_spacing);
	ClassDB::bind_method(D_METHOD("set_helper_line_spacing", "value"), &BulletSpawner2D::set_helper_line_spacing);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "helper_line_spacing"), "set_helper_line_spacing", "get_helper_line_spacing");

	ClassDB::bind_method(D_METHOD("get_helper_line_face_direction"), &BulletSpawner2D::get_helper_line_face_direction);
	ClassDB::bind_method(D_METHOD("set_helper_line_face_direction", "value"), &BulletSpawner2D::set_helper_line_face_direction);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "helper_line_face_direction"), "set_helper_line_face_direction", "get_helper_line_face_direction");

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

	// Need this in order to expose the enum constants to Godot Engine
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_CHILDREN);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_SELF);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_GRID);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_RING);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_FAN);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_SPIRAL);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_LINE);
	BIND_ENUM_CONSTANT(TRANSFORMS_FROM_HELPER_AIMED);

	ClassDB::bind_method(D_METHOD("get_volleys_fired"), &BulletSpawner2D::get_volleys_fired);
	ClassDB::bind_method(D_METHOD("collect_spawn_transforms"), &BulletSpawner2D::collect_spawn_transforms);
	ClassDB::bind_method(D_METHOD("shoot_once"), &BulletSpawner2D::shoot_once);
	ClassDB::bind_method(D_METHOD("reset_shooting"), &BulletSpawner2D::reset_shooting);

}

}
