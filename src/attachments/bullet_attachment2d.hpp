#pragma once

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/variant/utility_functions.hpp>

namespace BlastBullets2D {
using namespace godot;

class BulletAttachmentObjectPool2D;

class BulletAttachment2D : public Node2D {
	GDCLASS(BulletAttachment2D, Node2D)

public:
	// Pool tracking so a manually freed pooled attachment removes itself instead of
	// dangling. Managed by BulletAttachmentObjectPool2D::push/pop/free and the factory
	// teardown path; never set these by hand.
	BulletAttachmentObjectPool2D *home_pool = nullptr;
	uint32_t home_pooling_id = 0;
	bool is_pooled = false;

	// Source scene this instance was instantiated from. Stamped at pop-time
	// creation so the pool-pop path can verify a 32-bit key collision never
	// hands out a foreign scene's node. Saved across pooling; never set by
	// hand.
	Ref<PackedScene> source_scene;

	// Owner tracking (managed by BulletVolley2D, never set by hand): which
	// multimesh slot currently owns this ACTIVE attachment. Lets PREDELETE of a
	// manually freed active attachment drop its slot instead of leaving a dangling
	// pointer in the multimesh's attachments array.
	uint64_t owner_volley_id = 0;
	int owner_bullet_index = -1;

private:
	bool apply_time_scale = true;
	double applied_time_scale = 1.0;
	void scale_descendants(double scale);

public:
	void _notification(int p_what);
	// Custom spawn behavior - should be used to set up the BulletAttachment2D in proper state. Executed before _ready when a bullet is in the process of being set up. If you have behavior/data that doesn't require for the node to be in the scene tree, then use this function, otherwise just use _ready. Pooling is keyed by the PackedScene automatically, so there is nothing to set up for pooling here. Note that the attachment is not yet inside the scene tree when this method gets called
	GDVIRTUAL0(on_bullet_spawn)

	// Custom disable behavior - executed when the bullet is disabled (when the bullet hits something). Perfect place to set your bullet attachment to be invisible and disable processing for any children nodes
	GDVIRTUAL0(on_bullet_disable)

	// Custom activation behavior - executed when the bullet gets retrieved from the object pool. You should write logic that resets the state of your attachment node as if it's brand new
	GDVIRTUAL0(on_bullet_enable)

	// Custom spawn as disabled behavior. This method is used automatically called when the BulletAttachment2D pool is being populated with already disabled nodes. Your task is to provide the necessary logic that spawns the BulletAttachment2D as disabled (set everything to invisible, nothing moves, nothing uses processing etc..). Note that the attachment is not yet inside the scene tree when this method gets called.
	GDVIRTUAL0(on_spawn_in_pool)

	// Easy way of calling these virtual functions from C++

	void call_on_bullet_spawn();
	void call_on_bullet_disable();
	void call_on_bullet_enable();
	void call_on_spawn_in_pool();

	// ---- Time scale (see BulletVolley2D::apply_attachment_time_scale) ----
	// The volley that owns this attachment tells it the scale its bullet runs
	// at. With apply_time_scale (default on) the plugin multiplies the
	// speed_scale of every descendant GPUParticles2D, CPUParticles2D,
	// AnimatedSprite2D and AnimationPlayer (originals remembered in node meta
	// and restored exactly at 1.0); on_time_scale_changed tells the script in
	// both cases so custom nodes can follow too.
	GDVIRTUAL1(on_time_scale_changed, double)
	void call_on_time_scale_changed(double scale);
	bool get_apply_time_scale() const { return apply_time_scale; }
	void set_apply_time_scale(bool value);
	double get_applied_time_scale() const { return applied_time_scale; }
	// Plugin entry: non-finite or negative scales are ignored, an unchanged
	// scale is free.
	void set_time_scale_factor(double scale);
	// Scripts: re-applies the current scale to the descendants (after adding
	// nodes at runtime).
	void refresh_time_scale();

protected:
	static void _bind_methods();
};
} //namespace BlastBullets2D
