#pragma once

#include "data/bullet_effect_layer_data2d.hpp"
#include "data/bullet_rotation_data2d.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include <godot_cpp/classes/canvas_item_material.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/shape2d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/sprite_frames.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include "data/bullet_curves_data2d.hpp"
#include "data/bullet_speed_data2d.hpp"
#include "data/bullet_wobble_data2d.hpp"
#include "godot_cpp/variant/node_path.hpp"

namespace BlastBullets2D {
using namespace godot;

// Everything one volley needs at spawn time: transforms (one per bullet),
// art, collision, lifetime, motion (speed, rotation, curves, wobble,
// gravity, movement patterns), homing and bounce. Fill it in the inspector or
// from code, then hand it to a BulletFactory2D spawn method. One resource can
// be reused for every volley.
class BulletVolleyData2D : public Resource {
	GDCLASS(BulletVolleyData2D, Resource)
public:
	// TEXTURE / ANIMATION RELATED

	// SpriteFrames resource holding all animations. Each frame is a Texture2D
	// (plain texture or AtlasTexture region of a spritesheet). The whole multimesh
	// batch shares one frame at a time; per-frame timing comes from the SpriteFrames
	// animation (speed=fps, per-frame duration multiplier, loop flag).
	Ref<SpriteFrames> sprite_frames;

	// Which SpriteFrames animation to play. Empty or "default" auto-resolves silently:
	// "default" if present, else the first animation. Explicit wrong names fall back
	// to the first animation with a single error.
	StringName animation = "default";

	// QuadMesh size override. Used only if mesh is null. If x/y <= 0 the size is
	// auto-derived from the first frame (AtlasTexture region size, else texture size).
	Vector2 texture_size = Vector2(0, 0);

	// The texture rotation in radians. Change the value of this if you see that your texture is not rotated correctly. Example: If you want to rotate the texture 90 degrees more you would set the value to 90*PI/180
	real_t texture_rotation_radians = 0.0;

	// Whether the rotation of the texture should never change depending on the direction the bullets move in
	bool is_texture_rotation_permanent = false;

	// The Z index of all bullets being spawned
	int z_index = 0;

	// BULLET MOVEMENT RELATED

	// Determines the rotation and position of each bullet. The array amount_bullets determines the amount of projectiles to render.
	TypedArray<Transform2D> transforms;

	// BULLET ROTATION RELATED

	// Spin for each bullet. Entry i rotates bullet i only. Give one entry
	// per bullet (same size as transforms), or leave empty for no rotation.
	// A null or NaN/Inf entry reads as zero spin for that bullet.
	// Note that BulletRotationData2D has a helper static method that you can use to generate random rotation data - BulletRotationData2D.generate_random_data()
	TypedArray<BlastBullets2D::BulletRotationData2D> all_bullet_rotation_data;

	// Wrap short rotation arrays around the volley (slot i reads entry
	// i % size). Off by default.
	bool tile_all_bullet_rotation_data = false;

	// If set to false, it will also rotate the collision shapes according to the BulletRotationData2D that was provided (it might decrease performance a little bit)
	bool rotate_only_textures = true;

	// If set to true, it will stop the rotation when the max rotation speed is reached
	bool stop_rotation_when_max_reached = false;

	// COLLISION RELATED

	// How many times a single bullet can collide before being disabled. If you set to 0 the bullet will never be disabled due to collisions.
	int bullet_max_collision_count = 1;

	// Each bullets collision amount - it can only be set to a value that is <= bullet_max_collision_count (excluding 0 and negative numbers)
	TypedArray<int> bullets_current_collision_count;

	// Wrap short collision-count arrays around the volley. Off by default:
	// bullets without their own entry start at 0 hits. Values still clamp
	// (negatives to 0, at/above max to max - 1).
	bool tile_bullets_current_collision_count = false;

	// The collision layer that all bullets share. Note: pass a bitmask, it's not just a simple int. Use the calculate_bitmask function.
	int collision_layer = 1;

	// The collision mask that all bullets share. Note: pass a bitmask, it's not just a simple int. Use the calculate_bitmask function.
	int collision_mask = 1;

	// The collision shape. Supports RectangleShape2D, CircleShape2D and CapsuleShape2D. For any other shape an error will be printed.
	Ref<Shape2D> collision_shape;

	// Determines the offset of the collision shape (the collision shape is by default at the center of the texture, but with this you are able to control it's position)
	Vector2 collision_shape_offset = Vector2(0, 0);

	// If set to true it would mean it can detect bodies. I suggest you do NOT enable it, because it tanks performance, but I left it just in case someone is stubborn and has that need. Instead consider adding an Area2D to the body that you are trying to damage and set up its collision layer correctly so that the bullets can interact with it.
	bool monitorable = false;

	// Available inside the area_entered / body_entered callbacks inside factory.
	Ref<Resource> shared_bullets_custom_data;

	// PER-BULLET CUSTOM DATA

	// Custom data carried per bullet, readable in the factory collision
	// callbacks through the instance (bullet_get_custom_data) alongside the
	// shared value above. Entry i belongs to bullet i and nobody else.
	// Stays strictly separate from shared_bullets_custom_data: bullets
	// without an entry read as null, never as the shared value.
	TypedArray<Resource> all_bullets_custom_data;

	// Wrap short custom-data arrays around the volley. Off by default.
	bool tile_all_bullets_custom_data = false;

	// BULLET ATTACHMENT RELATED

	// Shared attachment scene applied to every bullet at spawn/enable time.
	// Null (default) disables the feature; use the runtime bullet_set_attachment
	// methods for per-bullet attachments instead.
	Ref<PackedScene> shared_bullet_attachment;

	// Offset of the shared attachment relative to the bullet's texture center.
	Vector2 shared_bullet_attachment_offset = Vector2(0, 0);

	// Whether the shared attachment sticks while the bullet rotates.
	bool shared_bullet_attachment_stick_relative_to_bullet = true;

	// SPRITE EFFECT LAYERS

	// Stackable sprite-effect layers (trails, spawn flashes, hit sparks,
	// destroy explosions, bounce sparks). Each entry renders through its
	// own per-frame shards: one extra draw call per animation frame while
	// anything is visible, zero when idle. Empty (default) disables the
	// whole feature. Use BulletEffectLayerData2D.make_*() presets for
	// zero-art effects, or assign custom SpriteFrames per layer.
	TypedArray<BlastBullets2D::BulletEffectLayerData2D> effect_layers;

	// OTHER

	// Light mask. Note: pass a bitmask, it's not just a simple int. Use the calculate_bitmask function.
	int light_mask = 1;

	// Visibility layer. Note: pass a bitmask, it's not just a simple int. Use the calculate_bitmask function.
	int visibility_layer = 1;

	// Whole-volley tint, exactly like CanvasItem self_modulate (multiplies
	// every bullet instance). White (default) renders untouched. Spawn one
	// volley per color for different-colored bullets; reseeded from data on
	// every spawn/enable, so pool reuse never leaks the previous tint.
	Color self_modulate = Color(1, 1, 1, 1);

	// Bullet whiten override (shares the animation rebuild below). When true, bullet frames bake
	// whitened (alpha preserved), so self_modulate tints bullets to the
	// exact dialed color instead of multiplying the source art. Same
	// fallback contract as the effect-layer override: unreadable frames
	// keep the original art with one warning, never a blank.
	bool override_frame_color = false;

	// Fade-in seconds after spawn. 0 (default) disables it.
	double fade_in_sec = 0.0;
	// Fade-out seconds before expiry. 0 (default) disables it. Ignored
	// with infinite lifetimes (nothing expires).
	double fade_out_sec = 0.0;
	// Tint-over-life gradient, sampled by lifetime fraction. Null disables.
	Ref<Gradient> modulate_ramp;

	// How long will the bullets last, before being disabled. Depending on whether the bullets pool has reached its limit, it will either add the bullets to the pool or it will queue_free them.
	double max_life_time = 2.0f;

	// Whether the life_time_over signal will be emitted when the life time of the bullets is over. Tracked by BulletFactory2D
	bool is_life_time_over_signal_enabled = false;

	// Whether the lifetime is infinite
	bool is_life_time_infinite = false;

	// You can assign a custom material that uses a shader. Note that you may also want to provide a custom mesh as well, but if you do so, then the texture_size property won't be used, instead handle scaling in the shader as well.
	Ref<Material> material;

	// Per-instance shader overrides for a ShaderMaterial with instance uniforms.
	// Key = uniform name (String), value = Variant matching the uniform type.
	// Applied in finalize_set_up() via set_instance_shader_parameter() per bullet
	// instance; ignored (and cleared on pool reuse) unless material is a
	// ShaderMaterial. Example: {"glow": Color(1, 0.5, 0), "speed": 2.0}.
	Dictionary instance_shader_parameters;

	// Custom mesh, if it isn't provided then a Quadmesh will be generated and it will use the texture_size. If you DO provide a mesh then you should handle the scaling of the bullets yourself using a shader for best quality.
	Ref<Mesh> mesh;

	// Used to acquire a bitmask from an array of integer values. Useful when setting the collision layer and collision mask. Example: you want your bullets to be in collision layer 1,2,3,7, you would pass an array of these numbers and the value that gets returned is the value you need to set to the collision_layer. Pass ONLY POSITIVE NUMBERS (NEVER PASS NEGATIVE OR ZERO)
	static int calculate_bitmask(const TypedArray<int> &numbers);

	// GETTERS AND SETTERS

	bool get_is_life_time_infinite() const;
	void set_is_life_time_infinite(bool value);

	TypedArray<Transform2D> get_transforms() const;
	void set_transforms(const TypedArray<Transform2D> &new_transforms);

	Ref<SpriteFrames> get_sprite_frames() const;
	void set_sprite_frames(const Ref<SpriteFrames> &new_sprite_frames);

	StringName get_animation() const;
	void set_animation(const StringName &new_animation);

	Vector2 get_texture_size() const;
	void set_texture_size(Vector2 new_texture_size);

	real_t get_texture_rotation_radians() const;
	void set_texture_rotation_radians(real_t new_texture_rotation_radians);

	int get_collision_layer() const;
	void set_collision_layer(int new_collision_layer);
	void set_collision_layer_from_array(const TypedArray<int> &numbers);

	int get_collision_mask() const;
	void set_collision_mask(int new_collision_mask);
	void set_collision_mask_from_array(const TypedArray<int> &numbers);

	Ref<Shape2D> get_collision_shape() const;
	void set_collision_shape(const Ref<Shape2D> &new_shape);

	Vector2 get_collision_shape_offset() const;
	void set_collision_shape_offset(const Vector2 &new_collision_shape_offset);

	bool get_monitorable() const;
	void set_monitorable(bool new_monitorable);

	Ref<Resource> get_shared_bullets_custom_data() const;
	void set_shared_bullets_custom_data(const Ref<Resource> &new_shared_bullets_custom_data);

	TypedArray<Resource> get_all_bullets_custom_data() const;
	void set_all_bullets_custom_data(const TypedArray<Resource> &new_custom_data);

	bool get_tile_all_bullets_custom_data() const;
	void set_tile_all_bullets_custom_data(bool value);

	Ref<PackedScene> get_shared_bullet_attachment() const;
	void set_shared_bullet_attachment(const Ref<PackedScene> &new_attachment);

	Vector2 get_shared_bullet_attachment_offset() const;
	void set_shared_bullet_attachment_offset(const Vector2 &new_offset);

	bool get_shared_bullet_attachment_stick_relative_to_bullet() const;
	void set_shared_bullet_attachment_stick_relative_to_bullet(bool value);

	TypedArray<BlastBullets2D::BulletEffectLayerData2D> get_effect_layers() const;
	void set_effect_layers(const TypedArray<BlastBullets2D::BulletEffectLayerData2D> &new_layers);

	double get_max_life_time() const;
	void set_max_life_time(double new_max_life_time);

	Ref<Material> get_material() const;
	void set_material(const Ref<Material> &new_material);

	Ref<Mesh> get_mesh() const;
	void set_mesh(const Ref<Mesh> &new_mesh);

	TypedArray<BulletRotationData2D> get_all_bullet_rotation_data() const;
	void set_all_bullet_rotation_data(const TypedArray<BulletRotationData2D> &new_data);

	bool get_tile_all_bullet_rotation_data() const;
	void set_tile_all_bullet_rotation_data(bool value);

	bool get_rotate_only_textures() const;
	void set_rotate_only_textures(bool new_rotate_only_textures);

	bool get_is_texture_rotation_permanent() const;
	void set_is_texture_rotation_permanent(bool new_is_texture_rotation_permanent);

	int get_z_index() const;
	void set_z_index(int new_z_index);

	int get_light_mask() const;
	void set_light_mask(int new_light_mask);
	void set_light_mask_from_array(const TypedArray<int> &numbers);

	int get_visibility_layer() const;
	void set_visibility_layer(int new_visibility_layer);
	void set_visibility_layer_from_array(const TypedArray<int> &numbers);

	Color get_self_modulate() const;
	void set_self_modulate(const Color &new_self_modulate);

	bool get_override_frame_color() const;
	void set_override_frame_color(bool value);

	// Fade-in seconds after spawn (0 disables). The volley starts at
	// transparent base tint and reaches full tint at fade_in_sec. Must be
	// finite and >= 0. Works with infinite lifetimes (age since spawn).
	double get_fade_in_sec() const;
	void set_fade_in_sec(double value);

	// Fade-out seconds before expiry (0 disables). Needs a finite lifetime:
	// infinite volleys never expire, so fade_out is ignored there
	// (documented, no warning). Must be finite and >= 0.
	double get_fade_out_sec() const;
	void set_fade_out_sec(double value);

	// Tint-over-life gradient, sampled by lifetime fraction (0 at spawn,
	// 1 at expiry) and multiplied with the base tint and the fades above.
	// Null (default) disables it. Infinite lifetimes ignore it (no
	// fraction exists); use fade_in_sec for endless volleys.
	Ref<Gradient> get_modulate_ramp() const;
	void set_modulate_ramp(const Ref<Gradient> &value);

	Dictionary get_instance_shader_parameters() const;
	void set_instance_shader_parameters(const Dictionary &new_instance_shader_parameters);

	bool get_is_life_time_over_signal_enabled() const;
	void set_is_life_time_over_signal_enabled(bool new_is_life_time_over_signal_enabled);

	bool get_stop_rotation_when_max_reached() const;
	void set_stop_rotation_when_max_reached(bool new_stop_rotation_when_max_reached);

	int get_bullet_max_collision_count() const;
	void set_bullet_max_collision_count(int new_max_collision_amount);

	TypedArray<int> get_bullets_current_collision_count() const;
	void set_bullets_current_collision_count(const TypedArray<int> &arr);

	bool get_tile_bullets_current_collision_count() const;
	void set_tile_bullets_current_collision_count(bool value);


public:
	// How per-bullet arrays resolve. Entry i belongs to bullet i and nobody
	// else. Each bullet falls back independently: a valid per-bullet entry
	// wins for that bullet, otherwise the shared value fills in when it is
	// set, otherwise the feature default (off, zero, null — see each array).
	// An array shorter than the volley only covers its own indices; the rest
	// fall back. Longer arrays ignore the extras. Mismatches warn once per
	// spawn. Each array has its own tile_* checkbox (off by default) that
	// restores the old wrap-around (slot i reads entry i % size) for that
	// array only.

	// Speed for each bullet. The array MUST have one entry per bullet
	// (same size as transforms) unless you use shared_bullet_speed_data or
	// tile_all_bullet_speed_data. Entry i drives bullet i only.
	// Note that BulletSpeedData2D has a helper static method that you can use to generate random speed data - BulletSpeedData2D.generate_random_data(). Negative speed flies backwards on purpose.
	TypedArray<BulletSpeedData2D> all_bullet_speed_data;

	// Wrap short speed arrays around the volley (slot i reads entry
	// i % size). Off by default. Check it to fan 2 entries across 10
	// bullets as A,B,A,B... instead of covering only bullets 0 and 1.
	bool tile_all_bullet_speed_data = false;

	// Whether each bullet's direction should be adjusted based on the rotation data provided by the user (bullet rotates, so it now moves in that direction)
	bool adjust_direction_based_on_rotation = false;

	// SHARED SPEED / ROTATION RELATED

	// Fallback speed for bullets whose own entry is missing or invalid
	// (null entry, or NaN/Inf values). A valid per-bullet entry always wins
	// for its bullet; this only fills the gaps. Null (default) means no
	// fallback — uncovered bullets fly at speed 0.
	// Example: volley of 3, speeds [200, null, 200], shared 150 → bullets
	// fly at 200, 150, 200.
	Ref<BulletSpeedData2D> shared_bullet_speed_data;

	// Same fallback deal as shared speed, for spin. Null (default) means no
	// fallback — uncovered bullets don't rotate.
	Ref<BulletRotationData2D> shared_bullet_rotation_data;

	// Fallback curves, resolved per channel. A bullet that carries its own
	// x-curve uses it even when this also defines x; this only covers the
	// channels the bullet lacks. Null (default) means no fallback.
	// Example: shared defines x + speed, bullet 2 carries only a y-curve →
	// bullet 2 steers with its own y plus the shared x and speed.
	Ref<BulletCurvesData2D> shared_bullet_curves_data;

	// SHARED MOVEMENT PATTERN RELATED

	// Path to a Path2D in the scene tree whose Curve2D is applied as the
	// movement pattern of every bullet at spawn/enable time. A Path2D node
	// cannot live inside a Resource, so the path is stored instead and
	// resolved through the BulletFactory2D (absolute paths always work).
	// Empty (default) disables the feature.
	NodePath shared_movement_pattern_path;

	// Whether the shared movement pattern rotates bullets to face their direction of travel.
	bool shared_movement_pattern_face_movement_direction = false;

	// Whether the shared movement pattern repeats instead of stopping at the end of the curve.
	bool shared_movement_pattern_repeat = true;

	// PER-BULLET CURVES / PATTERNS (runtime-owned; this data seeds them here).
	// Entry i seeds bullet i only. A null entry means "no curves for this
	// bullet" — it falls back to shared_bullet_curves_data per channel.

	// Curves for each bullet, applied at spawn/enable time. Entry i affects
	// bullet i only. Short arrays leave the tail bullets on the shared
	// fallback (or plain ballistics when that is unset too).
	TypedArray<BulletCurvesData2D> all_bullet_curves_data;

	// Wrap short curve arrays around the volley. Off by default.
	bool tile_all_bullet_curves_data = false;

	// PER-BULLET MOVEMENT PATTERN RELATED

	// Pattern path for each bullet, applied at spawn/enable time. Each entry
	// must point at a Path2D; its Curve2D is extracted when the volley
	// spawns. Entry i drives bullet i only. A bullet with its own valid
	// pattern always uses it; the shared path only covers bullets without
	// one. Empty or bad entries fall back per bullet.
	// The face/repeat flags below follow their own arrays (same rule).
	TypedArray<NodePath> all_bullet_movement_pattern_paths;

	// Wrap short pattern-path arrays around the volley. Off by default.
	bool tile_all_bullet_movement_pattern_paths = false;

	// Face flag for each bullet's own pattern. Entry i belongs to bullet i;
	// bullets without an entry use shared_movement_pattern_face_movement_direction.
	TypedArray<bool> all_bullet_movement_pattern_face_movement_directions;

	// Wrap short face-flag arrays around the volley. Off by default.
	bool tile_all_bullet_movement_pattern_face_movement_directions = false;

	// Repeat flag for each bullet's own pattern. Entry i belongs to bullet i;
	// bullets without an entry use shared_movement_pattern_repeat.
	TypedArray<bool> all_bullet_movement_pattern_repeats;

	// Wrap short repeat-flag arrays around the volley. Off by default.
	bool tile_all_bullet_movement_pattern_repeats = false;

	// WOBBLE (sine/cos flight modulation; editor-friendly danmaku staple).
	// Fallback wobble for bullets whose own entry is missing, null, or
	// disabled. A bullet with an active per-bullet seed always uses it.
	// Null (default) means no fallback — uncovered bullets fly straight.
	// Example: shared gentle weave + one strong per-bullet seed on bullet 2
	// → bullet 2 snakes hard, the rest weave gently.
	Ref<BulletWobbleData2D> shared_bullet_wobble_data;

	// Wobble seed for each bullet, applied at spawn/enable time. Entry i
	// affects bullet i only. Null entries and entries with enabled = false
	// fall back to shared_bullet_wobble_data for that bullet.
	TypedArray<BulletWobbleData2D> all_bullet_wobble_data;

	// Wrap short wobble arrays around the volley. Off by default.
	bool tile_all_bullet_wobble_data = false;

	// GRAVITY / DRAG (2D sideview + tower-defense shells).
	// Constant acceleration added to every bullet each tick (px/s^2).
	// (0, 0) disables. Must stay finite.
	Vector2 gravity = Vector2(0, 0);

	// Per-bullet gravity for each bullet. Entry i pulls bullet i only.
	// Empty array (default) means every bullet uses gravity above.
	// A non-finite entry is treated as (0, 0) for that bullet only.
	// Example: gravity (0, 1000) + entries [(0,0), (800,0)] on 3 bullets →
	// bullet 0 falls, bullet 1 drifts right, bullet 2 falls (no entry 2).
	TypedArray<Vector2> all_bullet_gravity;

	// Wrap short gravity arrays around the volley. Off by default.
	bool tile_all_bullet_gravity = false;

	// Gravity time window over volley life (seconds since spawn, measured on
	// curves_elapsed_time): gravity only integrates inside
	// [delay, delay + duration]. delay 0 = immediate; duration 0 = infinite.
	// Lets shells fly straight first, then drop (or drop, then glide).
	// Both must stay finite and >= 0.
	double gravity_delay_sec = 0.0;
	double gravity_duration_sec = 0.0;

	// Linear drag applied to speed each tick: speed -= speed * drag * delta.
	// 0 disables. Must stay finite and >= 0.
	double linear_drag = 0.0;

	// HOMING STEERING (spawn-time seed; every value below also exists as a
	// live BulletVolley2D setter for runtime tuning). Pool reuse
	// re-seeds these on every enable, so direct BulletFactory2D.spawn_* users
	// no longer lose steering on the first reuse. BulletSpawner2D users can
	// ignore them: apply_steering_to_volley overwrites them per volley.

	// Shared homing turn agility (0 snaps instantly). Must stay finite and >= 0.
	double homing_smoothing = 0.0;

	// Seconds between homing target position refreshes (0 = every tick).
	// Must stay finite and >= 0.
	double homing_update_interval = 0.0;

	// Distance in pixels at which a bullet counts as having reached its
	// target. Must stay finite and >= 0.
	double homing_distance_before_reached = 5.0;

	// Whether homing steers the bullet facing (leave off when a movement
	// pattern, rotation data, or orbiting texture mode already owns it).
	bool homing_take_control_of_texture_rotation = false;

	// Per-bullet queue: pop the front target when this bullet reaches it.
	bool bullet_homing_auto_pop_after_target_reached = false;

	// Shared queue: one deferred pop when any bullet reaches the front target.
	bool shared_homing_deque_auto_pop_after_target_reached = false;

	// When > 0, homing steering only begins after this many seconds of
	// straight flight (classic aimed-then-homing). Must stay finite and >= 0.
	double homing_delay_sec = 0.0;

	// When > 0, homing steering stops after this many seconds (lets fast
	// players escape). 0 = infinite. Must stay finite and >= 0.
	double homing_duration_sec = 0.0;

	// When > 0, homing steering pauses while the bullet is farther than this
	// from its target (BLAST-style homing range). 0 = unlimited.
	// Must stay finite and >= 0.
	double homing_lose_range_px = 0.0;

	// BOUNCE / RICOCHET (spawn-time seed; every value below also exists as a
	// live BulletVolley2D property for runtime tuning). Pool reuse
	// re-seeds these on every enable, so direct BulletFactory2D.spawn_* users
	// keep bouncing across reuses without a spawner.

	// Bounce behavior kinds. SIMPLE_RADIAL reflects across the radial axis
	// (bullet pos minus target pos): cheap, robust, looks right on round
	// targets. PRECISE_SHAPE resolves the target's CollisionShape2D and
	// reflects across the analytic surface normal (flat walls bounce flat):
	// slightly costlier, runs only on bounce ticks, falls back to radial
	// when the target has no usable shape.
	enum BounceMode {
		BOUNCE_SIMPLE_RADIAL = 0,
		BOUNCE_PRECISE_SHAPE = 1
	};

	// Bitmask of target collision layers that trigger a bounce. Checked
	// against the collided object's collision_layer and takes precedence
	// over the normal collision path. 0 (default) disables bouncing.
	// NOTE: these bits must overlap collision_mask, otherwise the bullet
	// never detects those targets at all (warned once per spawn).
	int bounce_mask = 0;

	// Bounce off TileMapLayer walls. Tilemaps report no collision_layer
	// (their physics bodies are internal), so they never match bounce_mask
	// and take the normal path by default. True opts them in: any non-zero
	// bounce_mask then bounces off tilemap cells with a head-on reflection
	// (the exact cell surface is unknowable from the record, so the radial
	// normal would misread far cells). False (default) keeps them lethal.
	bool bounce_tilemap_layers = false;

	// Speed multiplier applied on every bounce. 1.0 (default) is perfectly
	// elastic, < 1 damps (0.5 halves the speed), 0 dead-stops the bullet in
	// place (still alive), > 1 is super-elastic, with no upper cap (the
	// overflow guards refuse the bounce instead of storing Inf).
	double bounce_strength = 1.0;

	// Whether a moving target feeds its velocity into the bounce, split by
	// how it moves relative to the bullet. Same-direction targets (a
	// pusher catching the bullet from behind) surge it forward when
	// bounce_push_assist is on; oncoming targets (a head-on charger)
	// amplify the rebound through relative reflection when
	// bounce_charge_amplify is on. Both true (default) is the full smart
	// physics; turning either off falls back to the absolute reflection
	// for its case, so bounce_strength alone decides that outcome no
	// matter how the target moves. The repeat guard always applies.
	bool bounce_push_assist = true;
	bool bounce_charge_amplify = true;

	// Whether a bounce also counts as a normal hit (increments the collision
	// counter and can disable the bullet at bullet_max_collision_count).
	// False (default) means free bounces that never kill the bullet.
	bool bounce_hit_consumed = false;

	// How many times a single bullet may bounce before bounces stop working
	// for it (further hits take the normal collision path). 0 (default) =
	// unlimited (lifetime still bounds it). Must stay >= 0.
	int bounce_max_count = 0;

	// Which normal estimator to use (see BounceMode). Default radial.
	int bounce_mode = BOUNCE_SIMPLE_RADIAL;

	// Whether a bounce re-aims the bullet visual at the reflected heading.
	// Turn off when something else owns the facing (rotation data, patterns).
	bool bounce_rotate_texture = true;

	// Max visual turn rate in radians/sec toward the reflected heading.
	// 0 (default) snaps instantly (right for bullets); > 0 slews smoothly
	// like homing_smoothing. Ballistics always reflect instantly, only the
	// visual lags. Ignored while adjust_direction_based_on_rotation is on
	// (the visual owns ballistics there, so it must snap).
	double bounce_rotation_smooth = 0.0;

	// Random scatter in degrees applied symmetrically around the reflected
	// heading (+/- randomness per bounce). 0 (default) = exact reflection.
	// Must stay finite in [0, 180].
	double bounce_randomness_deg = 0.0;

	// Quiet period in seconds after a bounce during which further bounce
	// records for that bullet are swallowed (lets it escape the overlap).
	// Prevents double-flip when a target carries both a body and an area.
	// Must stay finite in [0, 1]. Default 0.05.
	double bounce_cooldown_sec = 0.05;

	// Same-target debounce window in seconds, applied at spawn time (and
	// on pool reuse). Cooldown only buys escape time, so a bullet still
	// touching the same object when it expires would bounce again and
	// again. Inside this window a re-hit against the just-bounced object
	// never re-bounces (other targets bounce freely). 0 disables it.
	// Must stay finite and >= 0. Default 0.15.
	double bounce_debounce_sec = 0.15;

	double get_homing_smoothing() const;
	void set_homing_smoothing(double value);

	double get_homing_update_interval() const;
	void set_homing_update_interval(double value);

	double get_homing_distance_before_reached() const;
	void set_homing_distance_before_reached(double value);

	bool get_homing_take_control_of_texture_rotation() const;
	void set_homing_take_control_of_texture_rotation(bool value);

	bool get_bullet_homing_auto_pop_after_target_reached() const;
	void set_bullet_homing_auto_pop_after_target_reached(bool value);

	bool get_shared_homing_deque_auto_pop_after_target_reached() const;
	void set_shared_homing_deque_auto_pop_after_target_reached(bool value);

	TypedArray<BulletSpeedData2D> get_all_bullet_speed_data() const;
	void set_all_bullet_speed_data(const TypedArray<BulletSpeedData2D> &new_data);

	bool get_tile_all_bullet_speed_data() const;
	void set_tile_all_bullet_speed_data(bool value);

	bool get_adjust_direction_based_on_rotation() const;
	void set_adjust_direction_based_on_rotation(bool new_adjust_direction_based_on_rotation);

	Ref<BulletSpeedData2D> get_shared_bullet_speed_data() const;
	void set_shared_bullet_speed_data(const Ref<BulletSpeedData2D> &new_speed_data);

	Ref<BulletRotationData2D> get_shared_bullet_rotation_data() const;
	void set_shared_bullet_rotation_data(const Ref<BulletRotationData2D> &new_rotation_data);

	Ref<BulletCurvesData2D> get_shared_bullet_curves_data() const;
	void set_shared_bullet_curves_data(const Ref<BulletCurvesData2D> &new_curves_data);

	NodePath get_shared_movement_pattern_path() const;
	void set_shared_movement_pattern_path(const NodePath &new_path);

	bool get_shared_movement_pattern_face_movement_direction() const;
	void set_shared_movement_pattern_face_movement_direction(bool value);

	bool get_shared_movement_pattern_repeat() const;
	void set_shared_movement_pattern_repeat(bool value);

	TypedArray<BulletCurvesData2D> get_all_bullet_curves_data() const;
	void set_all_bullet_curves_data(const TypedArray<BulletCurvesData2D> &new_data);

	bool get_tile_all_bullet_curves_data() const;
	void set_tile_all_bullet_curves_data(bool value);

	TypedArray<NodePath> get_all_bullet_movement_pattern_paths() const;
	void set_all_bullet_movement_pattern_paths(const TypedArray<NodePath> &new_paths);

	bool get_tile_all_bullet_movement_pattern_paths() const;
	void set_tile_all_bullet_movement_pattern_paths(bool value);

	TypedArray<bool> get_all_bullet_movement_pattern_face_movement_directions() const;
	void set_all_bullet_movement_pattern_face_movement_directions(const TypedArray<bool> &new_flags);

	bool get_tile_all_bullet_movement_pattern_face_movement_directions() const;
	void set_tile_all_bullet_movement_pattern_face_movement_directions(bool value);

	TypedArray<bool> get_all_bullet_movement_pattern_repeats() const;
	void set_all_bullet_movement_pattern_repeats(const TypedArray<bool> &new_flags);

	bool get_tile_all_bullet_movement_pattern_repeats() const;
	void set_tile_all_bullet_movement_pattern_repeats(bool value);

	Ref<BulletWobbleData2D> get_shared_bullet_wobble_data() const;
	void set_shared_bullet_wobble_data(const Ref<BulletWobbleData2D> &new_wobble_data);

	TypedArray<BulletWobbleData2D> get_all_bullet_wobble_data() const;
	void set_all_bullet_wobble_data(const TypedArray<BulletWobbleData2D> &new_data);

	bool get_tile_all_bullet_wobble_data() const;
	void set_tile_all_bullet_wobble_data(bool value);

	Vector2 get_gravity() const;
	void set_gravity(const Vector2 &value);

	TypedArray<Vector2> get_all_bullet_gravity() const;
	void set_all_bullet_gravity(const TypedArray<Vector2> &new_data);

	bool get_tile_all_bullet_gravity() const;
	void set_tile_all_bullet_gravity(bool value);

	double get_gravity_delay_sec() const;
	void set_gravity_delay_sec(double value);

	double get_gravity_duration_sec() const;
	void set_gravity_duration_sec(double value);

	double get_linear_drag() const;
	void set_linear_drag(double value);

	double get_homing_delay_sec() const;
	void set_homing_delay_sec(double value);

	double get_homing_duration_sec() const;
	void set_homing_duration_sec(double value);

	double get_homing_lose_range_px() const;
	void set_homing_lose_range_px(double value);

	int get_bounce_mask() const;
	void set_bounce_mask(int value);
	void set_bounce_mask_from_array(const TypedArray<int> &numbers);
	bool get_bounce_tilemap_layers() const;
	void set_bounce_tilemap_layers(bool value);

	double get_bounce_strength() const;
	void set_bounce_strength(double value);

	bool get_bounce_push_assist() const;
	void set_bounce_push_assist(bool value);

	bool get_bounce_charge_amplify() const;
	void set_bounce_charge_amplify(bool value);

	bool get_bounce_hit_consumed() const;
	void set_bounce_hit_consumed(bool value);

	int get_bounce_max_count() const;
	void set_bounce_max_count(int value);

	int get_bounce_mode() const;
	void set_bounce_mode(int value);

	bool get_bounce_rotate_texture() const;
	void set_bounce_rotate_texture(bool value);

	double get_bounce_rotation_smooth() const;
	void set_bounce_rotation_smooth(double value);

	double get_bounce_randomness_deg() const;
	void set_bounce_randomness_deg(double value);

	double get_bounce_cooldown_sec() const;
	void set_bounce_cooldown_sec(double value);

	double get_bounce_debounce_sec() const;
	void set_bounce_debounce_sec(double value);

protected:
	static void _bind_methods();
};
} //namespace BlastBullets2D

VARIANT_ENUM_CAST(BlastBullets2D::BulletVolleyData2D::BounceMode);
