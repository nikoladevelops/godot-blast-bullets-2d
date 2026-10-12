#include "data/bullet_pickup_data2d.hpp"

#include "data/bullet_speed_data2d.hpp"

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
	void BulletPickupData2D::set_##NAME(double value) {                                                                        \
		if (!Math::is_finite(value) || !(CONDITION)) {                                                                         \
			UtilityFunctions::push_error("BulletPickupData2D: " #NAME " must be finite and " TEXT ", keeping the old value."); \
			return;                                                                                                            \
		}                                                                                                                      \
		if (NAME == value) {                                                                                                   \
			return;                                                                                                            \
		}                                                                                                                      \
		NAME = value;                                                                                                          \
		accepted_change();                                                                                                     \
	}

BLAST_PICKUP_DOUBLE_SETTER(fade_in_sec, value >= 0.0, ">= 0")
BLAST_PICKUP_DOUBLE_SETTER(fade_out_sec, value >= 0.0, ">= 0")
BLAST_PICKUP_DOUBLE_SETTER(lifetime_sec, value >= 0.0, ">= 0")
BLAST_PICKUP_DOUBLE_SETTER(initial_velocity_scale, value >= 0.0, ">= 0")
BLAST_PICKUP_DOUBLE_SETTER(scatter_speed_min, value >= 0.0, ">= 0")
BLAST_PICKUP_DOUBLE_SETTER(scatter_speed_max, value >= 0.0, ">= 0")
BLAST_PICKUP_DOUBLE_SETTER(drag, value >= 0.0, ">= 0")
BLAST_PICKUP_DOUBLE_SETTER(magnet_delay_sec, value >= 0.0, ">= 0")
BLAST_PICKUP_DOUBLE_SETTER(magnet_acceleration, value >= 0.0, ">= 0")
BLAST_PICKUP_DOUBLE_SETTER(magnet_max_speed, value >= 0.0, ">= 0")
BLAST_PICKUP_DOUBLE_SETTER(magnet_smoothing, value >= 0.0, ">= 0")
BLAST_PICKUP_DOUBLE_SETTER(scale_multiplier, value > 0.0, "> 0")
BLAST_PICKUP_DOUBLE_SETTER(collect_radius, value > 0.0, "> 0")
BLAST_PICKUP_DOUBLE_SETTER(collision_radius, value > 0.0, "> 0")
#undef BLAST_PICKUP_DOUBLE_SETTER

#define BLAST_PICKUP_PLAIN_SETTER(TYPE, NAME)         \
	void BulletPickupData2D::set_##NAME(TYPE value) { \
		NAME = value;                                 \
		accepted_change();                            \
	}
BLAST_PICKUP_PLAIN_SETTER(Ref<Texture2D>, texture)
BLAST_PICKUP_PLAIN_SETTER(Ref<SpriteFrames>, sprite_frames)
BLAST_PICKUP_PLAIN_SETTER(Ref<BulletSoundData2D>, collect_sound)
BLAST_PICKUP_PLAIN_SETTER(Ref<BulletEffectLayerData2D>, collect_effect)
BLAST_PICKUP_PLAIN_SETTER(bool, inherit_custom_data)
BLAST_PICKUP_PLAIN_SETTER(bool, fire_clear_effects)
#undef BLAST_PICKUP_PLAIN_SETTER

void BulletPickupData2D::set_value(Variant p_value) {
	value = p_value;
	accepted_change();
}

void BulletPickupData2D::set_texture_size(Vector2 value) {
	if (!Math::is_finite(value.x) || !Math::is_finite(value.y) || value.x < 0.0 || value.y < 0.0) {
		UtilityFunctions::push_error("BulletPickupData2D: texture_size must be finite with both components >= 0, keeping the old value.");
		return;
	}
	texture_size = value;
	accepted_change();
}

void BulletPickupData2D::set_tint(Color value) {
	if (!Math::is_finite(value.r) || !Math::is_finite(value.g) || !Math::is_finite(value.b) || !Math::is_finite(value.a)) {
		UtilityFunctions::push_error("BulletPickupData2D: tint must be finite, keeping the old value.");
		return;
	}
	tint = value;
	accepted_change();
}

void BulletPickupData2D::set_collision_layer(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("BulletPickupData2D: collision_layer must be >= 0, keeping the old value.");
		return;
	}
	collision_layer = value;
	accepted_change();
}

void BulletPickupData2D::set_collision_mask(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("BulletPickupData2D: collision_mask must be >= 0, keeping the old value.");
		return;
	}
	collision_mask = value;
	accepted_change();
}

// ---- The pickup volley's spawn data ------------------------------------------------------

Ref<BulletVolleyData2D> BulletPickupData2D::get_volley_data() {
	if (cached_volley_data.is_valid()) {
		return cached_volley_data;
	}
	Ref<BulletVolleyData2D> d;
	d.instantiate();
	// Look: the animation wins over the plain texture; with neither, a small
	// white disc stands in so a pickup is never invisible.
	Ref<SpriteFrames> frames = sprite_frames;
	Vector2 size = texture_size;
	if (frames.is_null() || frames->get_animation_names().is_empty()) {
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
		if (size.x <= 0.0 || size.y <= 0.0) {
			size = tex->get_size();
		}
	} else if (size.x <= 0.0 || size.y <= 0.0) {
		const StringName first = frames->get_animation_names()[0];
		Ref<Texture2D> frame = frames->get_frame_count(first) > 0 ? frames->get_frame_texture(first, 0) : Ref<Texture2D>();
		size = frame.is_valid() ? frame->get_size() : Vector2(16, 16);
	}
	d->set_sprite_frames(frames);
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
	d->set_linear_drag(drag);
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
	} else {
		d->set_emit_collision_signals(false);
	}
	// Speed lives in the per-bullet arrays the conversion seeds; the shared
	// entry only needs to exist.
	Ref<BulletSpeedData2D> speed;
	speed.instantiate();
	d->set_shared_bullet_speed_data(speed);
	cached_volley_data = d;
	return d;
}

void BulletPickupData2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_volley_data"), &BulletPickupData2D::get_volley_data);
	ADD_GROUP("Look", "");
	ClassDB::bind_method(D_METHOD("get_texture"), &BulletPickupData2D::get_texture);
	ClassDB::bind_method(D_METHOD("set_texture", "value"), &BulletPickupData2D::set_texture);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_texture", "get_texture");
	ClassDB::bind_method(D_METHOD("get_sprite_frames"), &BulletPickupData2D::get_sprite_frames);
	ClassDB::bind_method(D_METHOD("set_sprite_frames", "value"), &BulletPickupData2D::set_sprite_frames);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sprite_frames", PROPERTY_HINT_RESOURCE_TYPE, "SpriteFrames"), "set_sprite_frames", "get_sprite_frames");
	ClassDB::bind_method(D_METHOD("get_texture_size"), &BulletPickupData2D::get_texture_size);
	ClassDB::bind_method(D_METHOD("set_texture_size", "value"), &BulletPickupData2D::set_texture_size);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "texture_size"), "set_texture_size", "get_texture_size");
	ClassDB::bind_method(D_METHOD("get_scale_multiplier"), &BulletPickupData2D::get_scale_multiplier);
	ClassDB::bind_method(D_METHOD("set_scale_multiplier", "value"), &BulletPickupData2D::set_scale_multiplier);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "scale_multiplier", PROPERTY_HINT_RANGE, "0.01,10,0.01,or_greater"), "set_scale_multiplier", "get_scale_multiplier");
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
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "magnet_acceleration", PROPERTY_HINT_RANGE, "0,10000,1,or_greater,suffix:px/s²"), "set_magnet_acceleration", "get_magnet_acceleration");
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
	ClassDB::bind_method(D_METHOD("get_value"), &BulletPickupData2D::get_value);
	ClassDB::bind_method(D_METHOD("set_value", "value"), &BulletPickupData2D::set_value);
	ADD_PROPERTY(PropertyInfo(Variant::NIL, "value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT | PROPERTY_USAGE_DEFAULT), "set_value", "get_value");
	ClassDB::bind_method(D_METHOD("get_collect_sound"), &BulletPickupData2D::get_collect_sound);
	ClassDB::bind_method(D_METHOD("set_collect_sound", "value"), &BulletPickupData2D::set_collect_sound);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "collect_sound", PROPERTY_HINT_RESOURCE_TYPE, "BulletSoundData2D"), "set_collect_sound", "get_collect_sound");
	ClassDB::bind_method(D_METHOD("get_collect_effect"), &BulletPickupData2D::get_collect_effect);
	ClassDB::bind_method(D_METHOD("set_collect_effect", "value"), &BulletPickupData2D::set_collect_effect);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "collect_effect", PROPERTY_HINT_RESOURCE_TYPE, "BulletEffectLayerData2D"), "set_collect_effect", "get_collect_effect");

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
}

} // namespace BlastBullets2D
