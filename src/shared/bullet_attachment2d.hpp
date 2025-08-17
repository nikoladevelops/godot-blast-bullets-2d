#pragma once

#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace BlastBullets2D{
using namespace godot;

    class BulletAttachment2D: public Node2D{
        GDCLASS(BulletAttachment2D, Node2D)

        public:
            // Custom attachment id. It is very important that each BulletAttachment2D scene has a different/unique id. The object pooling logic depends on this
            int attachment_id = 0;

            // Whether the bullet attachment always sticks to the bullet. If the bullet is rotating then the bullet attachment respects that behavior and moves according to it
            bool stick_relative_to_bullet = true;

            // Custom spawn behavior - should be used to set up the BulletAttachment2D in proper state. Executed before _ready when a bullet is in the process of being set up. If you have behavior/data that doesn't require for the node to be in the scene tree, then use this function, otherwise just use _ready. This is the place where you have to set a custom attachment_id so the bullet attaachment can use the object pool correctly. Note that the attachment is not yet inside the scene tree when this method gets called
            GDVIRTUAL0(on_bullet_spawn)

            // Custom disable behavior - executed when the bullet is disabled (when the bullet hits something). Perfect place to set your bullet attachment to be invisible and disable processing for any children nodes
            GDVIRTUAL0(on_bullet_disable)

            // Custom activation behavior - executed when the bullet gets retrieved from the object pool. You should write logic that resets the state of your attachment node as if it's brand new
            GDVIRTUAL0(on_bullet_activate)

            // Custom save behavior. You should create a brand new custom Resource class, fill it with your custom data that should be saved and then return it inside this function
            GDVIRTUAL0R(Ref<Resource>, on_bullet_save)

            // Custom load behavior. When the bullets are being loaded, the same custom resource that you used inside on_bullet_save will be passed as an argument. You should handle loading this custom resource data to ensure the proper loaded state of your bullet attachment. Note that the attachment is not yet inside the scene tree when this method gets called
            GDVIRTUAL1(on_bullet_load, Ref<Resource>)

            // Custom spawn as disabled behavior. This method is used automatically called when the BulletAttachment2D pool is being populated with already disabled nodes. Your task is to provide the necessary logic that spawns the BulletAttachment2D as disabled (set everything to invisible, nothing moves, nothing uses processing etc..). Note that the attachment is not yet inside the scene tree when this method gets called. Note that you have to set the attachment_id here as well
            GDVIRTUAL0(on_bullet_spawn_as_disabled)

            // Easy way of calling these virtual functions from C++

            void call_on_bullet_spawn();
            void call_on_bullet_disable();
            void call_on_bullet_activate();
            Ref<Resource> call_on_bullet_save();
            void call_on_bullet_load(Ref<Resource> custom_data_to_load);
            void call_on_bullet_spawn_as_disabled();

            // Getters and setters

            int get_attachment_id() const;
            void set_attachment_id(int new_attachment_id);

            bool get_stick_relative_to_bullet() const;
            void set_stick_relative_to_bullet(bool new_stick_relative_to_bullet);

        protected:
            static void _bind_methods();
    };
}