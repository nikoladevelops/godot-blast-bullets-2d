#include "data/bullet_pickup_data2d.hpp"
#include "core/cached_string_names2d.hpp"
#include "core/transform_math2d.hpp"
#include "sprite/sprite_anim2d.hpp"

#include <godot_cpp/classes/circle_shape2d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace BlastBullets2D {

void BulletPickupData2D::accepted_change() {
	cached_volley_data.unref();
	emit_changed();
}

// ---- Setters: reject-and-keep, the standard wording ----------------------------------

#define BLAST_PICKUP_DOUBLE_SETTER(NAME, CONDITION, TEXT)                                                                      \
	void BulletPickupData2D::set_##NAME(double p_value) {                                                                      \
		if (!is_finite_real2d(p_value) || !(CONDITION)) {                                                                      \
			UtilityFunctions::push_error("BulletPickupData2D: " #NAME " must be finite and " TEXT ", keeping the old value."); \
			return;                                                                                                            \
		}                                                                                                                      \
		if (NAME == p_value) {                                                                                                 \
			return;                                                                                                            \
		}                                                                                                                      \
		NAME = p_value;                                                                                                        \
		accepted_change();                                                                                                     \
	}

BLAST_PICKUP_DOUBLE_SETTER(fade_in_sec, p_value >= 0.0, ">= 0")
BLAST_PICKUP_DOUBLE_SETTER(fade_out_sec, p_value >= 0.0, ">= 0")
BLAST_PICKUP_DOUBLE_SETTER(lifetime_sec, p_value >= 0.0, ">= 0")
BLAST_PICKUP_DOUBLE_SETTER(initial_velocity_scale, p_value >= 0.0, ">= 0")
BLAST_PICKUP_DOUBLE_SETTER(scatter_speed_min, p_value >= 0.0, ">= 0")
BLAST_PICKUP_DOUBLE_SETTER(scatter_speed_max, p_value >= 0.0, ">= 0")
BLAST_PICKUP_DOUBLE_SETTER(drag, p_value >= 0.0, ">= 0")
BLAST_PICKUP_DOUBLE_SETTER(magnet_delay_sec, p_value >= 0.0, ">= 0")
BLAST_PICKUP_DOUBLE_SETTER(magnet_acceleration, p_value >= 0.0, ">= 0")
BLAST_PICKUP_DOUBLE_SETTER(magnet_max_speed, p_value >= 0.0, ">= 0")
BLAST_PICKUP_DOUBLE_SETTER(magnet_smoothing, p_value >= 0.0, ">= 0")
BLAST_PICKUP_DOUBLE_SETTER(scale_multiplier, p_value > 0.0, "> 0")
BLAST_PICKUP_DOUBLE_SETTER(scale_random_min, p_value > 0.0, "> 0")
BLAST_PICKUP_DOUBLE_SETTER(scale_random_max, p_value > 0.0, "> 0")
BLAST_PICKUP_DOUBLE_SETTER(collect_radius, p_value > 0.0, "> 0")
BLAST_PICKUP_DOUBLE_SETTER(collision_radius, p_value > 0.0, "> 0")
BLAST_PICKUP_DOUBLE_SETTER(rotation_random_min_degrees, true, "a number")
BLAST_PICKUP_DOUBLE_SETTER(rotation_random_max_degrees, true, "a number")
#undef BLAST_PICKUP_DOUBLE_SETTER

#define BLAST_PICKUP_PLAIN_SETTER(TYPE, NAME)           \
	void BulletPickupData2D::set_##NAME(TYPE p_value) { \
		NAME = p_value;                                 \
		accepted_change();                              \
	}
BLAST_PICKUP_PLAIN_SETTER(Ref<SpriteFrames>, sprite_frames)
BLAST_PICKUP_PLAIN_SETTER(StringName, animation)
BLAST_PICKUP_PLAIN_SETTER(Ref<Texture2D>, texture)
BLAST_PICKUP_PLAIN_SETTER(Ref<Material>, material)
BLAST_PICKUP_PLAIN_SETTER(Ref<PackedScene>, attachment)
BLAST_PICKUP_PLAIN_SETTER(TypedArray<BulletEffectLayerData2D>, effect_layers)
BLAST_PICKUP_PLAIN_SETTER(Ref<BulletSpeedData2D>, speed_data)
BLAST_PICKUP_PLAIN_SETTER(Ref<BulletCurvesData2D>, curves_data)
BLAST_PICKUP_PLAIN_SETTER(Ref<BulletRotationData2D>, rotation_data)
BLAST_PICKUP_PLAIN_SETTER(bool, collect_on_collision)
BLAST_PICKUP_PLAIN_SETTER(Ref<BulletSoundData2D>, collect_sound)
BLAST_PICKUP_PLAIN_SETTER(Ref<BulletEffectLayerData2D>, collect_effect)
BLAST_PICKUP_PLAIN_SETTER(bool, inherit_custom_data)
BLAST_PICKUP_PLAIN_SETTER(bool, fire_clear_effects)
BLAST_PICKUP_PLAIN_SETTER(bool, follow_source_time_scale)
BLAST_PICKUP_PLAIN_SETTER(bool, obey_despawn_bounds)
BLAST_PICKUP_PLAIN_SETTER(Variant, value)
#undef BLAST_PICKUP_PLAIN_SETTER

// A callable is runtime state (never saved) but still changes behavior only
// for the next conversion: it is read live at collection, no cache involved.
void BulletPickupData2D::set_collect_callback(Callable p_value) {
	collect_callback = p_value;
}

void BulletPickupData2D::set_attachment_offset(Vector2 p_value) {
	if (!Math::is_finite(p_value.x) || !Math::is_finite(p_value.y)) {
		UtilityFunctions::push_error("BulletPickupData2D: attachment_offset must be finite, keeping the old value.");
		return;
	}
	attachment_offset = p_value;
	accepted_change();
}

void BulletPickupData2D::set_texture_size(Vector2 p_value) {
	if (!Math::is_finite(p_value.x) || !Math::is_finite(p_value.y) || p_value.x < 0.0 || p_value.y < 0.0) {
		UtilityFunctions::push_error("BulletPickupData2D: texture_size must be finite with both components >= 0, keeping the old value.");
		return;
	}
	texture_size = p_value;
	accepted_change();
}

void BulletPickupData2D::set_tint(Color p_value) {
	if (!Math::is_finite(p_value.r) || !Math::is_finite(p_value.g) || !Math::is_finite(p_value.b) || !Math::is_finite(p_value.a)) {
		UtilityFunctions::push_error("BulletPickupData2D: tint must be finite, keeping the old value.");
		return;
	}
	tint = p_value;
	accepted_change();
}

void BulletPickupData2D::set_collision_layer(int p_value) {
	if (p_value < 0) {
		UtilityFunctions::push_error("BulletPickupData2D: collision_layer must be >= 0, keeping the old value.");
		return;
	}
	collision_layer = p_value;
	accepted_change();
}

void BulletPickupData2D::set_collision_mask(int p_value) {
	if (p_value < 0) {
		UtilityFunctions::push_error("BulletPickupData2D: collision_mask must be >= 0, keeping the old value.");
		return;
	}
	collision_mask = p_value;
	accepted_change();
}

void BulletPickupData2D::set_z_index(int p_value) {
	if (p_value < -4096 || p_value > 4096) {
		UtilityFunctions::push_error("BulletPickupData2D: z_index must be between -4096 and 4096, keeping the old value.");
		return;
	}
	z_index = p_value;
	accepted_change();
}

void BulletPickupData2D::set_motion_mode(MotionMode p_value) {
	if (p_value < MOTION_BURST_AND_MAGNET || p_value > MOTION_CUSTOM) {
		UtilityFunctions::push_error("BulletPickupData2D: motion_mode must be 0 (Burst And Magnet), 1 (Keep Bullet Motion) or 2 (Custom), keeping the old value.");
		return;
	}
	motion_mode = p_value;
	notify_property_list_changed(); // the burst and custom knobs gate on it
	accepted_change();
}

// Inspector: each motion mode shows only the knobs it reads.
void BulletPickupData2D::_validate_property(PropertyInfo &p_property) const {
	const String name = p_property.name;
	bool show = true;
	if (name == "initial_velocity_scale" || name == "scatter_speed_min" || name == "scatter_speed_max" || name == "drag" || name == "magnet_acceleration" || name == "magnet_max_speed") {
		show = motion_mode == MOTION_BURST_AND_MAGNET;
	} else if (name == "speed_data" || name == "curves_data") {
		show = motion_mode == MOTION_CUSTOM;
	}
	if (!show) {
		p_property.usage &= ~PROPERTY_USAGE_EDITOR;
	}
}

// ---- The pickup volley's spawn data ------------------------------------------------------

Ref<BulletVolleyData2D> BulletPickupData2D::get_volley_data() {
	if (cached_volley_data.is_valid()) {
		return cached_volley_data;
	}
	Ref<BulletVolleyData2D> d;
	d.instantiate();
	// Look: an animation of the SpriteFrames wins over the plain texture; with
	// neither, a small white disc stands in so a pickup is never invisible.
	Ref<SpriteFrames> frames = sprite_frames;
	StringName played = animation;
	Vector2 size = texture_size;
	bool usable = false;
	if (frames.is_valid() && !frames->get_animation_names().is_empty()) {
		// The shared name rule (sprite/sprite_anim2d.hpp), in warnings: a
		// pickup always shows something. An animation chosen by name that
		// exists without frames keeps the placeholder.
		const StringName &default_name = CachedStringNames2D::get().default_animation;
		const bool is_auto = played.is_empty() || played == default_name;
		const bool missing = !is_auto && !frames->has_animation(played);
		if (missing) {
			UtilityFunctions::push_warning(String("BulletPickupData2D: animation \"") + String(played) + String("\" does not exist in sprite_frames, playing the first animation."));
		}
		const SpriteAnimChoice2D choice = resolve_sprite_animation2d(frames, played);
		const bool chosen_empty = !is_auto && !missing && frames->get_frame_count(played) == 0;
		if (!chosen_empty && choice.usable()) {
			usable = true;
			played = choice.anim;
			if (size.x <= 0.0 || size.y <= 0.0) {
				size = frame_draw_size2d(frames->get_frame_texture(played, 0), Vector2(16, 16));
			}
		} else {
			const StringName shown = chosen_empty ? played : (frames->has_animation(default_name) ? default_name : StringName(frames->get_animation_names()[0]));
			UtilityFunctions::push_warning(String("BulletPickupData2D: animation \"") + String(shown) + String("\" of sprite_frames has no frames, using the texture or the placeholder."));
		}
	}
	if (!usable) {
		Ref<Texture2D> tex = texture;
		if (tex.is_null()) {
			Ref<Image> image = Image::create(16, 16, false, Image::FORMAT_RGBA8);
			for (int y = 0; y < 16; ++y) {
				for (int x = 0; x < 16; ++x) {
					const real_t dx = x - 7.5f;
					const real_t dy = y - 7.5f;
					image->set_pixel(x, y, dx * dx + dy * dy <= 56.25f ? Color(1, 1, 1, 1) : Color(1, 1, 1, 0));
				}
			}
			tex = ImageTexture::create_from_image(image);
		}
		frames.instantiate();
		frames->add_frame(StringName("default"), tex);
		played = StringName("default");
		if (size.x <= 0.0 || size.y <= 0.0) {
			size = tex->get_size();
		}
	}
	d->set_sprite_frames(frames);
	d->set_animation(played);
	d->set_texture_size(size);
	d->set_self_modulate(tint);
	// Fades cannot outlast the lifetime (the volley data would refuse them).
	if (lifetime_sec > 0.0) {
		d->set_is_life_time_infinite(false);
		d->set_max_life_time(lifetime_sec);
		d->set_fade_in_sec(Math::min(fade_in_sec, lifetime_sec));
		d->set_fade_out_sec(Math::min(fade_out_sec, lifetime_sec));
	} else {
		d->set_is_life_time_infinite(true);
		d->set_fade_in_sec(fade_in_sec);
		d->set_fade_out_sec(0.0);
	}
	d->set_is_life_time_over_signal_enabled(false);
	if (material.is_valid()) {
		d->set_material(material);
	}
	d->set_z_index(z_index);
	d->set_ignore_despawn_bounds(!obey_despawn_bounds);
	if (attachment.is_valid()) {
		d->set_shared_bullet_attachment(attachment);
		d->set_shared_bullet_attachment_offset(attachment_offset);
	}
	if (!effect_layers.is_empty()) {
		d->set_effect_layers(effect_layers);
	}
	// Motion. The burst/magnet phases need linear drag; the other modes never
	// drag. A custom speed or curve resource is shared as authored.
	d->set_linear_drag(motion_mode == MOTION_BURST_AND_MAGNET ? drag : 0.0);
	if (motion_mode == MOTION_CUSTOM) {
		if (speed_data.is_valid()) {
			d->set_shared_bullet_speed_data(speed_data);
		} else {
			Ref<BulletSpeedData2D> speed;
			speed.instantiate();
			d->set_shared_bullet_speed_data(speed);
		}
		if (curves_data.is_valid()) {
			d->set_shared_bullet_curves_data(curves_data);
		}
	} else {
		Ref<BulletSpeedData2D> speed;
		speed.instantiate();
		d->set_shared_bullet_speed_data(speed);
	}
	if (rotation_data.is_valid()) {
		d->set_shared_bullet_rotation_data(rotation_data);
	}
	d->set_homing_delay_sec(magnet_delay_sec);
	d->set_homing_distance_before_reached(collect_radius);
	d->set_homing_smoothing(magnet_smoothing);
	// Homing only turns a bullet that lets it own the facing; a pickup has no other owner.
	d->set_homing_take_control_of_texture_rotation(true);
	d->set_collision_layer(collision_layer);
	d->set_collision_mask(collision_mask);
	if (collision_layer != 0 || collision_mask != 0) {
		Ref<CircleShape2D> shape;
		shape.instantiate();
		shape->set_radius((real_t)collision_radius);
		d->set_collision_shape(shape);
		d->set_monitorable(true);
		// A touch never kills a pickup: collecting by touch consumes the
		// contact before it is counted, and otherwise the normal area/body
		// signals report it while the pickup keeps flying (0 = never
		// disabled by collisions).
		d->set_bullet_max_collision_count(0);
	} else {
		d->set_emit_collision_signals(false);
	}
	cached_volley_data = d;
	return d;
}

void BulletPickupData2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_volley_data"), &BulletPickupData2D::get_volley_data);
	BIND_ENUM_CONSTANT(MOTION_BURST_AND_MAGNET);
	BIND_ENUM_CONSTANT(MOTION_KEEP_BULLET_MOTION);
	BIND_ENUM_CONSTANT(MOTION_CUSTOM);

	ADD_GROUP("Look", "");
	ClassDB::bind_method(D_METHOD("get_sprite_frames"), &BulletPickupData2D::get_sprite_frames);
	ClassDB::bind_method(D_METHOD("set_sprite_frames", "value"), &BulletPickupData2D::set_sprite_frames);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sprite_frames", PROPERTY_HINT_RESOURCE_TYPE, "SpriteFrames"), "set_sprite_frames", "get_sprite_frames");
	ClassDB::bind_method(D_METHOD("get_animation"), &BulletPickupData2D::get_animation);
	ClassDB::bind_method(D_METHOD("set_animation", "value"), &BulletPickupData2D::set_animation);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "animation"), "set_animation", "get_animation");
	ClassDB::bind_method(D_METHOD("get_texture"), &BulletPickupData2D::get_texture);
	ClassDB::bind_method(D_METHOD("set_texture", "value"), &BulletPickupData2D::set_texture);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_texture", "get_texture");
	ClassDB::bind_method(D_METHOD("get_texture_size"), &BulletPickupData2D::get_texture_size);
	ClassDB::bind_method(D_METHOD("set_texture_size", "value"), &BulletPickupData2D::set_texture_size);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "texture_size"), "set_texture_size", "get_texture_size");
	ClassDB::bind_method(D_METHOD("get_scale_multiplier"), &BulletPickupData2D::get_scale_multiplier);
	ClassDB::bind_method(D_METHOD("set_scale_multiplier", "value"), &BulletPickupData2D::set_scale_multiplier);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "scale_multiplier", PROPERTY_HINT_RANGE, "0.01,10,0.01,or_greater"), "set_scale_multiplier", "get_scale_multiplier");
	ClassDB::bind_method(D_METHOD("get_scale_random_min"), &BulletPickupData2D::get_scale_random_min);
	ClassDB::bind_method(D_METHOD("set_scale_random_min", "value"), &BulletPickupData2D::set_scale_random_min);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "scale_random_min", PROPERTY_HINT_RANGE, "0.01,10,0.01,or_greater"), "set_scale_random_min", "get_scale_random_min");
	ClassDB::bind_method(D_METHOD("get_scale_random_max"), &BulletPickupData2D::get_scale_random_max);
	ClassDB::bind_method(D_METHOD("set_scale_random_max", "value"), &BulletPickupData2D::set_scale_random_max);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "scale_random_max", PROPERTY_HINT_RANGE, "0.01,10,0.01,or_greater"), "set_scale_random_max", "get_scale_random_max");
	ClassDB::bind_method(D_METHOD("get_rotation_random_min_degrees"), &BulletPickupData2D::get_rotation_random_min_degrees);
	ClassDB::bind_method(D_METHOD("set_rotation_random_min_degrees", "value"), &BulletPickupData2D::set_rotation_random_min_degrees);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rotation_random_min_degrees", PROPERTY_HINT_RANGE, "-360,360,0.1,or_less,or_greater,suffix:deg"), "set_rotation_random_min_degrees", "get_rotation_random_min_degrees");
	ClassDB::bind_method(D_METHOD("get_rotation_random_max_degrees"), &BulletPickupData2D::get_rotation_random_max_degrees);
	ClassDB::bind_method(D_METHOD("set_rotation_random_max_degrees", "value"), &BulletPickupData2D::set_rotation_random_max_degrees);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rotation_random_max_degrees", PROPERTY_HINT_RANGE, "-360,360,0.1,or_less,or_greater,suffix:deg"), "set_rotation_random_max_degrees", "get_rotation_random_max_degrees");
	ClassDB::bind_method(D_METHOD("get_tint"), &BulletPickupData2D::get_tint);
	ClassDB::bind_method(D_METHOD("set_tint", "value"), &BulletPickupData2D::set_tint);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "tint"), "set_tint", "get_tint");
	ClassDB::bind_method(D_METHOD("get_fade_in_sec"), &BulletPickupData2D::get_fade_in_sec);
	ClassDB::bind_method(D_METHOD("set_fade_in_sec", "value"), &BulletPickupData2D::set_fade_in_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fade_in_sec", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s"), "set_fade_in_sec", "get_fade_in_sec");
	ClassDB::bind_method(D_METHOD("get_fade_out_sec"), &BulletPickupData2D::get_fade_out_sec);
	ClassDB::bind_method(D_METHOD("set_fade_out_sec", "value"), &BulletPickupData2D::set_fade_out_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fade_out_sec", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s"), "set_fade_out_sec", "get_fade_out_sec");
	ClassDB::bind_method(D_METHOD("get_lifetime_sec"), &BulletPickupData2D::get_lifetime_sec);
	ClassDB::bind_method(D_METHOD("set_lifetime_sec", "value"), &BulletPickupData2D::set_lifetime_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lifetime_sec", PROPERTY_HINT_RANGE, "0,120,0.1,or_greater,suffix:s"), "set_lifetime_sec", "get_lifetime_sec");
	ClassDB::bind_method(D_METHOD("get_material"), &BulletPickupData2D::get_material);
	ClassDB::bind_method(D_METHOD("set_material", "value"), &BulletPickupData2D::set_material);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_material", "get_material");
	ClassDB::bind_method(D_METHOD("get_z_index"), &BulletPickupData2D::get_z_index);
	ClassDB::bind_method(D_METHOD("set_z_index", "value"), &BulletPickupData2D::set_z_index);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "z_index", PROPERTY_HINT_RANGE, "-4096,4096,1"), "set_z_index", "get_z_index");
	ClassDB::bind_method(D_METHOD("get_attachment"), &BulletPickupData2D::get_attachment);
	ClassDB::bind_method(D_METHOD("set_attachment", "value"), &BulletPickupData2D::set_attachment);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "attachment", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"), "set_attachment", "get_attachment");
	ClassDB::bind_method(D_METHOD("get_attachment_offset"), &BulletPickupData2D::get_attachment_offset);
	ClassDB::bind_method(D_METHOD("set_attachment_offset", "value"), &BulletPickupData2D::set_attachment_offset);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "attachment_offset"), "set_attachment_offset", "get_attachment_offset");
	ClassDB::bind_method(D_METHOD("get_effect_layers"), &BulletPickupData2D::get_effect_layers);
	ClassDB::bind_method(D_METHOD("set_effect_layers", "value"), &BulletPickupData2D::set_effect_layers);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "effect_layers", PROPERTY_HINT_ARRAY_TYPE, "BulletEffectLayerData2D"), "set_effect_layers", "get_effect_layers");

	ADD_GROUP("Motion", "");
	ClassDB::bind_method(D_METHOD("get_motion_mode"), &BulletPickupData2D::get_motion_mode);
	ClassDB::bind_method(D_METHOD("set_motion_mode", "value"), &BulletPickupData2D::set_motion_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "motion_mode", PROPERTY_HINT_ENUM, "Burst And Magnet,Keep Bullet Motion,Custom"), "set_motion_mode", "get_motion_mode");
	ClassDB::bind_method(D_METHOD("get_speed_data"), &BulletPickupData2D::get_speed_data);
	ClassDB::bind_method(D_METHOD("set_speed_data", "value"), &BulletPickupData2D::set_speed_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "speed_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletSpeedData2D"), "set_speed_data", "get_speed_data");
	ClassDB::bind_method(D_METHOD("get_curves_data"), &BulletPickupData2D::get_curves_data);
	ClassDB::bind_method(D_METHOD("set_curves_data", "value"), &BulletPickupData2D::set_curves_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "curves_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletCurvesData2D"), "set_curves_data", "get_curves_data");
	ClassDB::bind_method(D_METHOD("get_rotation_data"), &BulletPickupData2D::get_rotation_data);
	ClassDB::bind_method(D_METHOD("set_rotation_data", "value"), &BulletPickupData2D::set_rotation_data);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "rotation_data", PROPERTY_HINT_RESOURCE_TYPE, "BulletRotationData2D"), "set_rotation_data", "get_rotation_data");

	ADD_GROUP("Burst Out", "");
	ClassDB::bind_method(D_METHOD("get_initial_velocity_scale"), &BulletPickupData2D::get_initial_velocity_scale);
	ClassDB::bind_method(D_METHOD("set_initial_velocity_scale", "value"), &BulletPickupData2D::set_initial_velocity_scale);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "initial_velocity_scale", PROPERTY_HINT_RANGE, "0,2,0.01,or_greater"), "set_initial_velocity_scale", "get_initial_velocity_scale");
	ClassDB::bind_method(D_METHOD("get_scatter_speed_min"), &BulletPickupData2D::get_scatter_speed_min);
	ClassDB::bind_method(D_METHOD("set_scatter_speed_min", "value"), &BulletPickupData2D::set_scatter_speed_min);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "scatter_speed_min", PROPERTY_HINT_RANGE, "0,1000,1,or_greater,suffix:px/s"), "set_scatter_speed_min", "get_scatter_speed_min");
	ClassDB::bind_method(D_METHOD("get_scatter_speed_max"), &BulletPickupData2D::get_scatter_speed_max);
	ClassDB::bind_method(D_METHOD("set_scatter_speed_max", "value"), &BulletPickupData2D::set_scatter_speed_max);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "scatter_speed_max", PROPERTY_HINT_RANGE, "0,1000,1,or_greater,suffix:px/s"), "set_scatter_speed_max", "get_scatter_speed_max");
	ClassDB::bind_method(D_METHOD("get_drag"), &BulletPickupData2D::get_drag);
	ClassDB::bind_method(D_METHOD("set_drag", "value"), &BulletPickupData2D::set_drag);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "drag", PROPERTY_HINT_RANGE, "0,20,0.01,or_greater"), "set_drag", "get_drag");

	ADD_GROUP("Magnet", "magnet_");
	ClassDB::bind_method(D_METHOD("get_magnet_delay_sec"), &BulletPickupData2D::get_magnet_delay_sec);
	ClassDB::bind_method(D_METHOD("set_magnet_delay_sec", "value"), &BulletPickupData2D::set_magnet_delay_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "magnet_delay_sec", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s"), "set_magnet_delay_sec", "get_magnet_delay_sec");
	ClassDB::bind_method(D_METHOD("get_magnet_acceleration"), &BulletPickupData2D::get_magnet_acceleration);
	ClassDB::bind_method(D_METHOD("set_magnet_acceleration", "value"), &BulletPickupData2D::set_magnet_acceleration);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "magnet_acceleration", PROPERTY_HINT_RANGE, String::utf8("0,10000,1,or_greater,suffix:px/s²")), "set_magnet_acceleration", "get_magnet_acceleration");
	ClassDB::bind_method(D_METHOD("get_magnet_max_speed"), &BulletPickupData2D::get_magnet_max_speed);
	ClassDB::bind_method(D_METHOD("set_magnet_max_speed", "value"), &BulletPickupData2D::set_magnet_max_speed);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "magnet_max_speed", PROPERTY_HINT_RANGE, "0,10000,1,or_greater,suffix:px/s"), "set_magnet_max_speed", "get_magnet_max_speed");
	ClassDB::bind_method(D_METHOD("get_magnet_smoothing"), &BulletPickupData2D::get_magnet_smoothing);
	ClassDB::bind_method(D_METHOD("set_magnet_smoothing", "value"), &BulletPickupData2D::set_magnet_smoothing);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "magnet_smoothing", PROPERTY_HINT_RANGE, "0,1,0.01,or_greater"), "set_magnet_smoothing", "get_magnet_smoothing");

	ADD_GROUP("Collection", "");
	ClassDB::bind_method(D_METHOD("get_collect_radius"), &BulletPickupData2D::get_collect_radius);
	ClassDB::bind_method(D_METHOD("set_collect_radius", "value"), &BulletPickupData2D::set_collect_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "collect_radius", PROPERTY_HINT_RANGE, "0.1,200,0.1,or_greater,suffix:px"), "set_collect_radius", "get_collect_radius");
	ClassDB::bind_method(D_METHOD("get_collect_on_collision"), &BulletPickupData2D::get_collect_on_collision);
	ClassDB::bind_method(D_METHOD("set_collect_on_collision", "value"), &BulletPickupData2D::set_collect_on_collision);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "collect_on_collision"), "set_collect_on_collision", "get_collect_on_collision");
	ClassDB::bind_method(D_METHOD("get_value"), &BulletPickupData2D::get_value);
	ClassDB::bind_method(D_METHOD("set_value", "value"), &BulletPickupData2D::set_value);
	ADD_PROPERTY(PropertyInfo(Variant::NIL, "value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT | PROPERTY_USAGE_DEFAULT), "set_value", "get_value");
	ClassDB::bind_method(D_METHOD("get_collect_sound"), &BulletPickupData2D::get_collect_sound);
	ClassDB::bind_method(D_METHOD("set_collect_sound", "value"), &BulletPickupData2D::set_collect_sound);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "collect_sound", PROPERTY_HINT_RESOURCE_TYPE, "BulletSoundData2D"), "set_collect_sound", "get_collect_sound");
	ClassDB::bind_method(D_METHOD("get_collect_effect"), &BulletPickupData2D::get_collect_effect);
	ClassDB::bind_method(D_METHOD("set_collect_effect", "value"), &BulletPickupData2D::set_collect_effect);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "collect_effect", PROPERTY_HINT_RESOURCE_TYPE, "BulletEffectLayerData2D"), "set_collect_effect", "get_collect_effect");
	ClassDB::bind_method(D_METHOD("get_collect_callback"), &BulletPickupData2D::get_collect_callback);
	ClassDB::bind_method(D_METHOD("set_collect_callback", "value"), &BulletPickupData2D::set_collect_callback);
	ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "collect_callback", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "set_collect_callback", "get_collect_callback");

	ADD_GROUP("Collision", "collision_");
	ClassDB::bind_method(D_METHOD("get_collision_layer"), &BulletPickupData2D::get_collision_layer);
	ClassDB::bind_method(D_METHOD("set_collision_layer", "value"), &BulletPickupData2D::set_collision_layer);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_layer", PROPERTY_HINT_LAYERS_2D_PHYSICS), "set_collision_layer", "get_collision_layer");
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &BulletPickupData2D::get_collision_mask);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "value"), &BulletPickupData2D::set_collision_mask);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_2D_PHYSICS), "set_collision_mask", "get_collision_mask");
	ClassDB::bind_method(D_METHOD("get_collision_radius"), &BulletPickupData2D::get_collision_radius);
	ClassDB::bind_method(D_METHOD("set_collision_radius", "value"), &BulletPickupData2D::set_collision_radius);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "collision_radius", PROPERTY_HINT_RANGE, "0.1,200,0.1,or_greater,suffix:px"), "set_collision_radius", "get_collision_radius");

	ADD_GROUP("Conversion", "");
	ClassDB::bind_method(D_METHOD("get_inherit_custom_data"), &BulletPickupData2D::get_inherit_custom_data);
	ClassDB::bind_method(D_METHOD("set_inherit_custom_data", "value"), &BulletPickupData2D::set_inherit_custom_data);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "inherit_custom_data"), "set_inherit_custom_data", "get_inherit_custom_data");
	ClassDB::bind_method(D_METHOD("get_fire_clear_effects"), &BulletPickupData2D::get_fire_clear_effects);
	ClassDB::bind_method(D_METHOD("set_fire_clear_effects", "value"), &BulletPickupData2D::set_fire_clear_effects);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fire_clear_effects"), "set_fire_clear_effects", "get_fire_clear_effects");
	ClassDB::bind_method(D_METHOD("get_follow_source_time_scale"), &BulletPickupData2D::get_follow_source_time_scale);
	ClassDB::bind_method(D_METHOD("set_follow_source_time_scale", "value"), &BulletPickupData2D::set_follow_source_time_scale);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "follow_source_time_scale"), "set_follow_source_time_scale", "get_follow_source_time_scale");
	ClassDB::bind_method(D_METHOD("get_obey_despawn_bounds"), &BulletPickupData2D::get_obey_despawn_bounds);
	ClassDB::bind_method(D_METHOD("set_obey_despawn_bounds", "value"), &BulletPickupData2D::set_obey_despawn_bounds);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "obey_despawn_bounds"), "set_obey_despawn_bounds", "get_obey_despawn_bounds");
}

} // namespace BlastBullets2D
