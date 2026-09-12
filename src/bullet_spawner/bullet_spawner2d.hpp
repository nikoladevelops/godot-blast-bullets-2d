#pragma once

#include "factory/bullet_factory2d.hpp"
#include "godot_cpp/classes/node2d.hpp"
#include "godot_cpp/classes/path2d.hpp"
#include "godot_cpp/classes/wrapped.hpp"
#include "godot_cpp/variant/node_path.hpp"
#include "spawn-data/directional_bullets_data2d.hpp"

namespace BlastBullets2D {
using namespace godot;

class BulletSpawner2D : public Node2D{
    GDCLASS(BulletSpawner2D, Node2D)

    public:
        // Scene-tree reference to the factory. Stored as an unfiltered NodePath
        // so every node in the edited scene is pickable; the typed pointer is
        // resolved on demand with a runtime type check (see get_bullet_factory).
        NodePath bullet_factory_path;
        // Runtime cache of the resolved factory. Not a bound property.
        mutable BulletFactory2D *bullet_factory = nullptr;
        // Plain Node2D reference from the scene tree. Same NodePath pattern as
        // the factory, but type-filtered to Node2D (native types always match).
        NodePath marker_holder_path;
        // Runtime cache of the resolved marker holder. Not a bound property.
        mutable Node2D *marker_holder = nullptr;
        // Scene-tree reference to the Path2D holding the movement pattern.
        // Lives here (not in the data resource) because only Nodes can resolve
        // scene-tree paths. The pattern flags live in DirectionalBulletsData2D.
        NodePath movement_pattern_path;
        // Runtime cache of the resolved pattern path. Not a bound property.
        mutable Path2D *movement_pattern_path_node = nullptr;
        Ref<DirectionalBulletsData2D> spawn_data;

        NodePath get_bullet_factory_path() const;
        void set_bullet_factory_path(const NodePath &p_path);

        BulletFactory2D *get_bullet_factory() const;
        void set_bullet_factory(BulletFactory2D *factory);

        NodePath get_marker_holder_path() const;
        void set_marker_holder_path(const NodePath &p_path);

        Node2D *get_marker_holder() const;
        void set_marker_holder(Node2D *holder);

        NodePath get_movement_pattern_path() const;
        void set_movement_pattern_path(const NodePath &p_path);

        Path2D *get_movement_pattern_path_node() const;
        void set_movement_pattern_path_node(Path2D *path);

        Ref<DirectionalBulletsData2D> get_spawn_data() const;
        void set_spawn_data(const Ref<DirectionalBulletsData2D> &new_spawn_data);


    protected:
	static void _bind_methods();



};
}
