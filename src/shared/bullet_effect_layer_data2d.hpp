#pragma once

#include <godot_cpp/classes/gradient.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/sprite_frames.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/transform2d.hpp>

#include <vector>

namespace BlastBullets2D {
using namespace godot;

// One stackable sprite-effect layer for a bullet volley. A volley carries an
// array of these (MultiMeshBulletsData2D.effect_layers); each layer renders
// through its own per-frame MultiMesh shards, so layers never fight each
// other and every instance can show a different animation frame.
// Two behaviors: TRAIL_FOLLOW sticks to its bullet every tick, the ON_*
// triggers fire one-shot visuals (spawn flash, hit sparks, destroy
// explosion, bounce sparks) that play once and die by age.
enum EffectTrigger {
	EFFECT_TRAIL_FOLLOW,
	EFFECT_ON_SPAWN,
	EFFECT_ON_HIT,
	EFFECT_ON_DESTROY,
	EFFECT_ON_BOUNCE,
	EFFECT_ON_LIFETIME_OVER
};

class BulletEffectLayerData2D : public Resource {
	GDCLASS(BulletEffectLayerData2D, Resource)

public:
	// Master switch: lets users toggle a layer without removing it.
	bool enabled = true;

	// What this layer does (see EffectTrigger). Trail layers follow their
	// bullet; one-shot layers fire on their event and die by age.
	int trigger = EFFECT_TRAIL_FOLLOW;

	// Frames to play (required: a layer with null frames is inert, warns
	// once at bake, never crashes). Upload any SpriteFrames here.
	Ref<SpriteFrames> sprite_frames;

	// Which animation to play. Empty or "default" auto-resolves silently:
	// "default" if present, else the first animation with frames.
	StringName animation = "default";

	// Per-layer material override. Null renders unshaded default blending.
	Ref<Material> material;

	// Layer-wide tint, exactly like CanvasItem self_modulate (multiplies
	// every instance of this layer). White renders the art untouched.
	Color self_modulate = Color(1, 1, 1, 1);

	// Tint-over-life ramp, sampled per instance by animation age (one-shots)
	// or loop phase (trails), exactly like particle color ramps. Multiplies
	// with self_modulate. Null (default) keeps every instance solid.
	Ref<Gradient> color_ramp;

	// Draw order of this layer's shards. Trail shards are children of the
	// volley, so with z_as_relative (default) the offset tracks the volley
	// z forever (+1 above the bullets, -1 beneath for glows/shadows).
	// One-shot shards live under the factory, where the offset behaves like
	// an absolute z (documented on the member).
	int z_index = 1;
	bool z_as_relative = true;

	// Visibility layer bitmask for this layer's shards (same numbering as
	// the bullet visibility_layer). Lets effects render on chosen canvases.
	int visibility_layer = 1;

	// Light mask bitmask for this layer's shards (same numbering as the
	// bullet light_mask). Lets effects opt out of 2D lights.
	int light_mask = 1;

	// Trail follow offset from the bullet origin, rotated with the bullet.
	// One-shots add it (rotated by the event orientation) to the event pose.
	Vector2 offset = Vector2(0, 0);

	// Static visual rotation in degrees, applied on top of the bullet/event
	// orientation (a 90-degree streak flies sideways to its bullet).
	// Positive spins clockwise (Godot 2D convention). Any value goes.
	double rotation_degrees = 0.0;

	// Continuous spin in degrees per second (pinwheels, rotating sparks,
	// shimmering trails). 0 (default) disables it. Signed: negative spins
	// counter-clockwise. Derived from the running clock, so pooled reuse
	// and animation switches never desync it.
	double spin_degrees_per_sec = 0.0;

	// Uniform visual scale of the layer (multiplies the frame size).
	Vector2 scale = Vector2(1, 1);

	// Desync: one-shot slots start at a random animation age, trail bullets
	// at a random loop phase. Same-frame kills then read as a volley, not a
	// slideshow copy. Off by default so timing stays exact unless asked.
	bool random_start_frame = false;

	// Extra one-shot chaos: spin each fired slot randomly (full 360).
	bool randomize_rotation = false;

	// Random uniform scale range per fired slot. (1, 1) disables it.
	real_t random_scale_min = 1.0;
	real_t random_scale_max = 1.0;

	// Fire probability per event. Stack three explosion layers at 0.5 and
	// every kill looks different. 0 never fires, 1 always fires.
	real_t trigger_chance = 1.0;

	// One-shot slot ring size: oldest slot is recycled past this. Bounds
	// worst-case memory per layer; trail layers ignore it (one slot per
	// bullet by construction). 0 means auto: max(32, 2x the spawning
	// volley's bullet count), so one effect per bullet plus margin with
	// zero config (manual hatch without a volley falls back to 64).
	int max_instances = 0;

	// Bakes frames/secs/total for animation. Cached on the layer (keyed by
	// animation); setters clear the cache. Returns false (with one error)
	// when the layer can never render.
	bool bake_effect_frames(const StringName &anim, std::vector<Ref<Texture2D>> &out_frames, std::vector<double> &out_secs, double &out_total) const;
	// Content generation, bumped on every invalidation: consumers holding
	// baked output detect layer edits through it.
	uint64_t get_bake_version() const { return bake_version; }

	bool get_enabled() const;
	void set_enabled(bool value);

	int get_trigger() const;
	void set_trigger(int value);

	Ref<SpriteFrames> get_sprite_frames() const;
	void set_sprite_frames(const Ref<SpriteFrames> &new_frames);

	StringName get_animation() const;
	void set_animation(const StringName &new_animation);

	Ref<Material> get_material() const;
	void set_material(const Ref<Material> &new_material);

	Color get_self_modulate() const;
	void set_self_modulate(const Color &new_color);

	Ref<Gradient> get_color_ramp() const;
	void set_color_ramp(const Ref<Gradient> &new_ramp);

	int get_z_index() const;
	void set_z_index(int value);

	bool get_z_as_relative() const;
	void set_z_as_relative(bool value);

	int get_visibility_layer() const;
	void set_visibility_layer(int value);

	int get_light_mask() const;
	void set_light_mask(int value);

	Vector2 get_offset() const;
	void set_offset(const Vector2 &new_offset);

	double get_rotation_degrees() const;
	void set_rotation_degrees(double value);

	double get_spin_degrees_per_sec() const;
	void set_spin_degrees_per_sec(double value);

	Vector2 get_scale() const;
	void set_scale(const Vector2 &new_scale);

	bool get_random_start_frame() const;
	void set_random_start_frame(bool value);

	bool get_randomize_rotation() const;
	void set_randomize_rotation(bool value);

	real_t get_random_scale_min() const;
	void set_random_scale_min(real_t value);

	real_t get_random_scale_max() const;
	void set_random_scale_max(real_t value);

	real_t get_trigger_chance() const;
	void set_trigger_chance(real_t value);

	int get_max_instances() const;
	void set_max_instances(int value);

protected:
	static void _bind_methods();

private:
	// Bake cache (mutable: bake() is const so volley/factory hot paths can
	// share one layer without casts). Cleared by every setter below, and
	// keyed by animation: the same layer can bake several animations over
	// its life (play_effect_animation), so a stale animation never serves.
	mutable bool bake_valid = false;
	mutable StringName bake_anim;
	// Content generation: bumped on every invalidation so consumers holding
	// baked output (manual hatch) can detect layer edits and rebake instead
	// of serving stale frames.
	mutable uint64_t bake_version = 0;
	mutable std::vector<Ref<Texture2D>> bake_frames;
	mutable std::vector<double> bake_secs;
	mutable double bake_total = 0.0;
	void invalidate_bake() const;
};
} //namespace BlastBullets2D
