#pragma once

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/variant/utility_functions.hpp>

namespace BlastBullets2D {
using namespace godot;

class BulletAttachment2D : public Node2D {
	GDCLASS(BulletAttachment2D, Node2D)

public:
	// Custom spawn behavior - should be used to set up the BulletAttachment2D in proper state. Executed before _ready when a bullet is in the process of being set up. If you have behavior/data that doesn't require for the node to be in the scene tree, then use this function, otherwise just use _ready. This is the place where you have to set a custom attachment_id so the bullet attaachment can use the object pool correctly. Note that the attachment is not yet inside the scene tree when this method gets called
	GDVIRTUAL0(on_bullet_spawn)

	// Custom disable behavior - executed when the bullet is disabled (when the bullet hits something). Perfect place to set your bullet attachment to be invisible and disable processing for any children nodes
	GDVIRTUAL0(on_bullet_disable)

	// Custom activation behavior - executed when the bullet gets retrieved from the object pool. You should write logic that resets the state of your attachment node as if it's brand new
	GDVIRTUAL0(on_bullet_enable)

	// Custom spawn as disabled behavior. This method is used automatically called when the BulletAttachment2D pool is being populated with already disabled nodes. Your task is to provide the necessary logic that spawns the BulletAttachment2D as disabled (set everything to invisible, nothing moves, nothing uses processing etc..). Note that the attachment is not yet inside the scene tree when this method gets called. Note that you have to set the attachment_id here as well
	GDVIRTUAL0(on_spawn_in_pool)

	// Easy way of calling these virtual functions from C++

	void call_on_bullet_spawn();
	void call_on_bullet_disable();
	void call_on_bullet_enable();
	void call_on_spawn_in_pool();

protected:
	static void _bind_methods();
};
} //namespace BlastBullets2D
