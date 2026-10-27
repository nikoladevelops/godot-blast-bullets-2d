#pragma once

#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/sprite_frames.hpp>

namespace BlastBullets2D {
using namespace godot;

// How one point animation of a laser looks (LaserData2D.charge_look,
// muzzle_look, tip_look, pierce_look, wall_impact_look): which SpriteFrames
// animation plays and how fast (the shared rules of sprite/sprite_anim2d.hpp),
// where it sits around its point (offset, rotation, scale; turned with the
// beam while align_to_beam), its tint, and the canvas item it draws on (its
// own z order, relative to the laser by default, and material). Every
// accepted change emits `changed`; a LaserData2D holding the look passes it
// on, so lasers in flight pick the edit up.
class LaserSpriteLook2D : public Resource {
	GDCLASS(LaserSpriteLook2D, Resource)

public:
	Ref<SpriteFrames> frames;
	StringName animation; // empty = "default", else the first animation with frames
	double speed_scale = 1.0;
	Vector2 scale = Vector2(1, 1);
	Vector2 offset; // px, in the look's frame (along / across the beam while aligned)
	double rotation_degrees = 0.0;
	bool align_to_beam = true;
	Color modulate = Color(1, 1, 1, 1); // times LaserData2D.modulate
	int z_index = 0;
	bool z_as_relative = true;
	Ref<Material> material;
	bool visible = true;

	Ref<SpriteFrames> get_frames() const { return frames; }
	void set_frames(const Ref<SpriteFrames> &value);
	StringName get_animation() const { return animation; }
	void set_animation(const StringName &value);
	double get_speed_scale() const { return speed_scale; }
	void set_speed_scale(double value);
	Vector2 get_scale() const { return scale; }
	void set_scale(const Vector2 &value);
	Vector2 get_offset() const { return offset; }
	void set_offset(const Vector2 &value);
	double get_rotation_degrees() const { return rotation_degrees; }
	void set_rotation_degrees(double value);
	bool get_align_to_beam() const { return align_to_beam; }
	void set_align_to_beam(bool value);
	Color get_modulate() const { return modulate; }
	void set_modulate(const Color &value);
	int get_z_index() const { return z_index; }
	void set_z_index(int value);
	bool get_z_as_relative() const { return z_as_relative; }
	void set_z_as_relative(bool value);
	Ref<Material> get_material() const { return material; }
	void set_material(const Ref<Material> &value);
	bool get_visible() const { return visible; }
	void set_visible(bool value);

	// The look's pose at `point` on a beam heading `beam_angle`.
	Transform2D pose_at(const Vector2 &point, real_t beam_angle) const;

protected:
	static void _bind_methods();
};

} // namespace BlastBullets2D
