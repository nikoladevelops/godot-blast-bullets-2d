#include "bullet_spawner2d.hpp"
#include "godot_cpp/classes/global_constants.hpp"
#include "godot_cpp/core/class_db.hpp"
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

NodePath BulletSpawner2D::get_marker_holder_path() const {
    return marker_holder_path;
}

void BulletSpawner2D::set_marker_holder_path(const NodePath &p_path) {
    marker_holder_path = p_path;
    marker_holder = nullptr;
    if (!marker_holder_path.is_empty() && is_inside_tree()) {
        Node *node = get_node_or_null(marker_holder_path);
        if (node != nullptr && Object::cast_to<Node2D>(node) == nullptr) {
            UtilityFunctions::push_warning("BulletSpawner2D: assigned marker_holder node is not a Node2D.");
        } else {
            resolve_node_path(this, marker_holder_path, marker_holder);
        }
    }
}

Node2D *BulletSpawner2D::get_marker_holder() const {
    return resolve_node_path(this, marker_holder_path, marker_holder);
}

void BulletSpawner2D::set_marker_holder(Node2D *holder) {
    assign_node_to_path(this, holder, marker_holder_path, marker_holder);
}

NodePath BulletSpawner2D::get_movement_pattern_path() const {
    return movement_pattern_path;
}

void BulletSpawner2D::set_movement_pattern_path(const NodePath &p_path) {
    movement_pattern_path = p_path;
    movement_pattern_path_node = nullptr;
    if (!movement_pattern_path.is_empty() && is_inside_tree()) {
        Node *node = get_node_or_null(movement_pattern_path);
        if (node != nullptr && Object::cast_to<Path2D>(node) == nullptr) {
            UtilityFunctions::push_warning("BulletSpawner2D: assigned movement_pattern_path node is not a Path2D.");
        } else {
            resolve_node_path(this, movement_pattern_path, movement_pattern_path_node);
        }
    }
}

Path2D *BulletSpawner2D::get_movement_pattern_path_node() const {
    return resolve_node_path(this, movement_pattern_path, movement_pattern_path_node);
}

void BulletSpawner2D::set_movement_pattern_path_node(Path2D *path) {
    assign_node_to_path(this, path, movement_pattern_path, movement_pattern_path_node);
}

Ref<BulletSpawnerData2D> BulletSpawner2D::get_spawn_data() const {
    return spawn_data;
}
void BulletSpawner2D::set_spawn_data(const Ref<BulletSpawnerData2D> &new_data) {
    spawn_data = new_data;
}


void BulletSpawner2D::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_bullet_factory_path"), &BulletSpawner2D::get_bullet_factory_path);
    ClassDB::bind_method(D_METHOD("set_bullet_factory_path", "path"), &BulletSpawner2D::set_bullet_factory_path);
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "bullet_factory_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "BulletFactory2D"), "set_bullet_factory_path", "get_bullet_factory_path");

    ClassDB::bind_method(D_METHOD("get_bullet_factory"), &BulletSpawner2D::get_bullet_factory);
    ClassDB::bind_method(D_METHOD("set_bullet_factory", "factory"), &BulletSpawner2D::set_bullet_factory);

    ClassDB::bind_method(D_METHOD("get_marker_holder_path"), &BulletSpawner2D::get_marker_holder_path);
    ClassDB::bind_method(D_METHOD("set_marker_holder_path", "path"), &BulletSpawner2D::set_marker_holder_path);
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "marker_holder", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node2D"), "set_marker_holder_path", "get_marker_holder_path");

    ClassDB::bind_method(D_METHOD("get_marker_holder"), &BulletSpawner2D::get_marker_holder);
    ClassDB::bind_method(D_METHOD("set_marker_holder", "holder"), &BulletSpawner2D::set_marker_holder);

    ClassDB::bind_method(D_METHOD("get_movement_pattern_path"), &BulletSpawner2D::get_movement_pattern_path);
    ClassDB::bind_method(D_METHOD("set_movement_pattern_path", "path"), &BulletSpawner2D::set_movement_pattern_path);
    ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "movement_pattern_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Path2D"), "set_movement_pattern_path", "get_movement_pattern_path");

    ClassDB::bind_method(D_METHOD("get_movement_pattern_path_node"), &BulletSpawner2D::get_movement_pattern_path_node);
    ClassDB::bind_method(D_METHOD("set_movement_pattern_path_node", "path"), &BulletSpawner2D::set_movement_pattern_path_node);

    ClassDB::bind_method(D_METHOD("get_spawn_data"), &BulletSpawner2D::get_spawn_data);
    ClassDB::bind_method(D_METHOD("set_spawn_data", "new_spawn_data"), &BulletSpawner2D::set_spawn_data);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "spawn_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletSpawnerData2D"), "set_spawn_data", "get_spawn_data");

}

}
