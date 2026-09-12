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
    TypedArray<Transform2D> transforms;
    Node2D *base = get_transforms_generator();
    if (base == nullptr) {
        base = const_cast<BulletSpawner2D *>(this);
    }
    const Vector2 base_origin = base->get_global_transform().get_origin();
    const real_t scale = (real_t)transforms_scale;
    bool collected = false;
    for (int i = 0; i < base->get_child_count(); ++i) {
        Node2D *as_2d = Object::cast_to<Node2D>(base->get_child(i));
        if (as_2d != nullptr) {
            transforms.push_back(scale_spawn_transform(as_2d->get_global_transform(), base_origin, scale));
            collected = true;
        }
    }
    if (!collected) {
        transforms.push_back(scale_spawn_transform(base->get_global_transform(), base_origin, scale));
    }
    return transforms;
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
    DirectionalBullets2D *bullets = factory->spawn_controllable_directional_bullets(volley_data);
    if (bullets == nullptr) {
        return false; // Factory already reported why (busy/teardown/bad data).
    }
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
	ADD_SIGNAL(MethodInfo("volley_fired",
		PropertyInfo(Variant::OBJECT, "directional_bullets_instance", PROPERTY_HINT_NODE_TYPE, "DirectionalBullets2D"),
		PropertyInfo(Variant::INT, "volley_index")));
	ADD_SIGNAL(MethodInfo("shooting_started"));
	ADD_SIGNAL(MethodInfo("shooting_stopped"));
	ADD_SIGNAL(MethodInfo("shooting_finished"));

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

	ClassDB::bind_method(D_METHOD("get_volleys_fired"), &BulletSpawner2D::get_volleys_fired);
	ClassDB::bind_method(D_METHOD("collect_spawn_transforms"), &BulletSpawner2D::collect_spawn_transforms);
	ClassDB::bind_method(D_METHOD("shoot_once"), &BulletSpawner2D::shoot_once);
	ClassDB::bind_method(D_METHOD("reset_shooting"), &BulletSpawner2D::reset_shooting);

}

}
