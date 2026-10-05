#include "data/bullet_effect_layer_data2d.hpp"

#include <godot_cpp/classes/atlas_texture.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace BlastBullets2D {

// Max animation frames per bake: bounds shard nodes (one per frame) and the
// age-to-frame scan. Longer anims truncate with a warning, never a crash.
static const int EFFECT_MAX_BAKE_FRAMES = 24;

void BulletEffectLayerData2D::invalidate_bake() const {
	bake_valid = false;
	bake_anim = StringName();
	++bake_version;
	bake_frames.clear();
	bake_secs.clear();
	bake_total = 0.0;
}

bool BulletEffectLayerData2D::bake_effect_frames(const StringName &anim, std::vector<Ref<Texture2D>> &out_frames, std::vector<double> &out_secs, double &out_total) const {
	if (bake_valid && bake_anim == anim) {
		out_frames = bake_frames;
		out_secs = bake_secs;
		out_total = bake_total;
		return !bake_frames.empty() && bake_total > 0.0;
	}
	const Ref<SpriteFrames> frames = sprite_frames;
	if (frames.is_null()) {
		UtilityFunctions::push_error("BulletEffectLayerData2D: sprite_frames is null, layer renders nothing. Assign frames to use this layer.");
		return false;
	}
	// Resolve the animation (same rules as the bullet animation: "default"
	// or empty auto-resolves, explicit wrong names fall back once loudly).
	StringName use_anim = anim;
	const PackedStringArray names = frames->get_animation_names();
	if (names.is_empty() || (String(use_anim).is_empty() || use_anim == StringName("default"))) {
		if (frames->has_animation(StringName("default")) && frames->get_frame_count(StringName("default")) > 0) {
			use_anim = StringName("default");
		} else {
			StringName fallback;
			for (int i = 0; i < names.size(); ++i) {
				if (frames->get_frame_count(names[i]) > 0) {
					fallback = names[i];
					break;
				}
			}
			if (String(fallback).is_empty()) {
				UtilityFunctions::push_error("BulletEffectLayerData2D: sprite_frames has no animation with frames.");
				return false;
			}
			use_anim = fallback;
		}
	} else if (!frames->has_animation(use_anim) || frames->get_frame_count(use_anim) <= 0) {
		UtilityFunctions::push_error("BulletEffectLayerData2D: missing animation '" + String(use_anim) + "', layer renders nothing.");
		return false;
	}
	const int count = frames->get_frame_count(use_anim);
	double fps = frames->get_animation_speed(use_anim);
	if (fps <= 0.0) {
		fps = 1.0;
	}
	const int kept = Math::min(count, EFFECT_MAX_BAKE_FRAMES);
	if (count > EFFECT_MAX_BAKE_FRAMES) {
		UtilityFunctions::push_warning("BulletEffectLayerData2D: animation '" + String(use_anim) + "' has " + String::num_int64(count) + " frames, baking the first 24 (one shard node per frame).");
	}
	bool whiten_warned = false;
	std::vector<Ref<Texture2D>> out_f;
	std::vector<double> out_s;
	out_f.reserve(kept);
	out_s.reserve(kept);
	double total = 0.0;
	for (int i = 0; i < kept; ++i) {
		Ref<Texture2D> tex = frames->get_frame_texture(use_anim, i);
		if (tex.is_null()) {
			UtilityFunctions::push_error("BulletEffectLayerData2D: animation '" + String(use_anim) + "' frame " + String::num_int64(i) + " has null texture.");
			return false;
		}
		const float dur = frames->get_frame_duration(use_anim, i);
		const double sec = (dur <= 0.0f ? 0.0 : (double)dur / fps);
		if (override_frame_color) {
			// Exact-color mode: whitened copy (alpha preserved) so the
			// layer tint reads exactly. Unreadable frames fall back to
			// the original with one warning per bake, never a blank.
			Ref<Image> pixels = read_frame_image(tex);
			Ref<Image> white = whiten_image_copy(pixels);
			if (white.is_valid()) {
				Ref<ImageTexture> white_tex;
				white_tex.instantiate();
				white_tex->set_image(white);
				out_f.push_back(white_tex);
			} else {
				if (!whiten_warned) {
					whiten_warned = true;
					UtilityFunctions::push_warning("BulletEffectLayerData2D: override_frame_color could not read a frame of '" + String(use_anim) + "', keeping the original art for unreadable frames.");
				}
				out_f.push_back(tex);
			}
		} else {
			out_f.push_back(tex);
		}
		out_s.push_back(sec);
		total += sec;
	}
	if (out_f.empty() || total <= 0.0) {
		UtilityFunctions::push_error("BulletEffectLayerData2D: baked zero playable time, layer renders nothing.");
		return false;
	}
	bake_frames = out_f;
	bake_secs = out_s;
	bake_total = total;
	bake_anim = anim;
	bake_valid = true;
	out_frames = out_f;
	out_secs = out_s;
	out_total = total;
	return true;
}

bool BulletEffectLayerData2D::get_enabled() const {
	return enabled;
}
void BulletEffectLayerData2D::set_enabled(bool value) {
	enabled = value;
}

int BulletEffectLayerData2D::get_trigger() const {
	return trigger;
}
void BulletEffectLayerData2D::set_trigger(int value) {
	if (value != EFFECT_TRAIL_FOLLOW && value != EFFECT_ON_SPAWN && value != EFFECT_ON_HIT && value != EFFECT_ON_DESTROY && value != EFFECT_ON_BOUNCE && value != EFFECT_ON_LIFETIME_OVER && value != EFFECT_ON_CLEAR) {
		UtilityFunctions::push_error("BulletEffectLayerData2D: trigger must be 0 (Trail Follow), 1 (On Spawn), 2 (On Hit), 3 (On Destroy), 4 (On Bounce), 5 (On Lifetime Over) or 6 (On Clear), keeping the old value.");
		return;
	}
	trigger = value;
}

Ref<SpriteFrames> BulletEffectLayerData2D::get_sprite_frames() const {
	return sprite_frames;
}
void BulletEffectLayerData2D::set_sprite_frames(const Ref<SpriteFrames> &new_frames) {
	sprite_frames = new_frames;
	invalidate_bake();
}

StringName BulletEffectLayerData2D::get_animation() const {
	return animation;
}
void BulletEffectLayerData2D::set_animation(const StringName &new_animation) {
	animation = new_animation;
	invalidate_bake();
}

Ref<Material> BulletEffectLayerData2D::get_material() const {
	return material;
}
void BulletEffectLayerData2D::set_material(const Ref<Material> &new_material) {
	material = new_material;
}

Color BulletEffectLayerData2D::get_self_modulate() const {
	return self_modulate;
}
void BulletEffectLayerData2D::set_self_modulate(const Color &new_color) {
	// Multiplied into every effect instance tint by fx_refresh_slot_color, so a
	// NaN channel would spread across the layer. The float/vector setters in
	// this file all guard; this color setter was the odd one out.
	// godot-cpp's Color has no is_finite(), so check the channels directly.
	if (!Math::is_finite(new_color.r) || !Math::is_finite(new_color.g) ||
			!Math::is_finite(new_color.b) || !Math::is_finite(new_color.a)) {
		UtilityFunctions::push_error("BulletEffectLayerData2D: self_modulate must be finite, keeping the old value.");
		return;
	}
	self_modulate = new_color;
}

Ref<Gradient> BulletEffectLayerData2D::get_color_ramp() const {
	return color_ramp;
}
void BulletEffectLayerData2D::set_color_ramp(const Ref<Gradient> &new_ramp) {
	color_ramp = new_ramp;
}

int BulletEffectLayerData2D::get_z_index() const {
	return z_index;
}
void BulletEffectLayerData2D::set_z_index(int value) {
	z_index = value;
}

bool BulletEffectLayerData2D::get_z_as_relative() const {
	return z_as_relative;
}
void BulletEffectLayerData2D::set_z_as_relative(bool value) {
	z_as_relative = value;
}

int BulletEffectLayerData2D::get_visibility_layer() const {
	return visibility_layer;
}
void BulletEffectLayerData2D::set_visibility_layer(int value) {
	visibility_layer = value;
}

int BulletEffectLayerData2D::get_light_mask() const {
	return light_mask;
}
void BulletEffectLayerData2D::set_light_mask(int value) {
	light_mask = value;
}

Vector2 BulletEffectLayerData2D::get_offset() const {
	return offset;
}
void BulletEffectLayerData2D::set_offset(const Vector2 &new_offset) {
	if (!new_offset.is_finite()) {
		UtilityFunctions::push_error("BulletEffectLayerData2D: offset must be finite, keeping the old value.");
		return;
	}
	offset = new_offset;
}

double BulletEffectLayerData2D::get_rotation_degrees() const {
	return rotation_degrees;
}
void BulletEffectLayerData2D::set_rotation_degrees(double value) {
	if (!Math::is_finite(value)) {
		UtilityFunctions::push_error("BulletEffectLayerData2D: rotation_degrees must be finite, keeping the old value.");
		return;
	}
	rotation_degrees = value;
}

double BulletEffectLayerData2D::get_spin_degrees_per_sec() const {
	return spin_degrees_per_sec;
}
void BulletEffectLayerData2D::set_spin_degrees_per_sec(double value) {
	if (!Math::is_finite(value)) {
		UtilityFunctions::push_error("BulletEffectLayerData2D: spin_degrees_per_sec must be finite, keeping the old value.");
		return;
	}
	spin_degrees_per_sec = value;
}

Vector2 BulletEffectLayerData2D::get_scale() const {
	return scale;
}
void BulletEffectLayerData2D::set_scale(const Vector2 &new_scale) {
	if (!new_scale.is_finite()) {
		UtilityFunctions::push_error("BulletEffectLayerData2D: scale must be finite, keeping the old value.");
		return;
	}
	scale = new_scale;
}

bool BulletEffectLayerData2D::get_random_start_frame() const {
	return random_start_frame;
}
void BulletEffectLayerData2D::set_random_start_frame(bool value) {
	random_start_frame = value;
}

bool BulletEffectLayerData2D::get_randomize_rotation() const {
	return randomize_rotation;
}
void BulletEffectLayerData2D::set_randomize_rotation(bool value) {
	randomize_rotation = value;
}

real_t BulletEffectLayerData2D::get_random_scale_min() const {
	return random_scale_min;
}
void BulletEffectLayerData2D::set_random_scale_min(real_t value) {
	if (!Math::is_finite(value) || value < 0.0 || value > random_scale_max) {
		UtilityFunctions::push_error("BulletEffectLayerData2D: random_scale_min must be finite in [0, random_scale_max], keeping the old value.");
		return;
	}
	random_scale_min = value;
}

real_t BulletEffectLayerData2D::get_random_scale_max() const {
	return random_scale_max;
}
void BulletEffectLayerData2D::set_random_scale_max(real_t value) {
	if (!Math::is_finite(value) || value < random_scale_min) {
		UtilityFunctions::push_error("BulletEffectLayerData2D: random_scale_max must be finite and >= random_scale_min, keeping the old value.");
		return;
	}
	random_scale_max = value;
}

real_t BulletEffectLayerData2D::get_trigger_chance() const {
	return trigger_chance;
}
void BulletEffectLayerData2D::set_trigger_chance(real_t value) {
	if (!Math::is_finite(value) || value < 0.0 || value > 1.0) {
		UtilityFunctions::push_error("BulletEffectLayerData2D: trigger_chance must be finite in [0, 1], keeping the old value.");
		return;
	}
	trigger_chance = value;
}

int BulletEffectLayerData2D::get_max_instances() const {
	return max_instances;
}
void BulletEffectLayerData2D::set_max_instances(int value) {
	// 4096 is the hard ring cap the factory applies at bake time: a larger
	// value would be silently clamped, so it is refused here instead.
	if (value < 0 || value > 4096) {
		UtilityFunctions::push_error("BulletEffectLayerData2D: max_instances must be in [0, 4096] (0 = auto), keeping the old value.");
		return;
	}
	max_instances = value;
}

double BulletEffectLayerData2D::get_fade_in_sec() const {
	return fade_in_sec;
}
void BulletEffectLayerData2D::set_fade_in_sec(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletEffectLayerData2D: fade_in_sec must be finite and >= 0, keeping the old value.");
		return;
	}
	fade_in_sec = value;
}
double BulletEffectLayerData2D::get_fade_out_sec() const {
	return fade_out_sec;
}
void BulletEffectLayerData2D::set_fade_out_sec(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletEffectLayerData2D: fade_out_sec must be finite and >= 0, keeping the old value.");
		return;
	}
	fade_out_sec = value;
}
bool BulletEffectLayerData2D::get_override_frame_color() const {
	return override_frame_color;
}
void BulletEffectLayerData2D::set_override_frame_color(bool value) {
	if (override_frame_color == value) {
		return;
	}
	override_frame_color = value;
	// Whitening changes the baked textures themselves, so consumers
	// holding baked output must rebake (bumps the manual-hatch version).
	invalidate_bake();
}

// Whiten override cap: per-pixel loop over huge sheets would stall the
// baking frame. Over the cap the original frame is kept with one warning.
static const int EFFECT_WHITEN_MAX_SIDE = 512;

Ref<Image> BulletEffectLayerData2D::whiten_image_copy(const Ref<Image> &src) {
	Ref<Image> out;
	if (src.is_null() || src->is_empty()) {
		UtilityFunctions::push_error("BulletEffectLayerData2D.whiten_image_copy: source image is null or empty.");
		return out;
	}
	if (src->get_width() > EFFECT_WHITEN_MAX_SIDE || src->get_height() > EFFECT_WHITEN_MAX_SIDE) {
		UtilityFunctions::push_error("BulletEffectLayerData2D.whiten_image_copy: image exceeds 512px per side, refusing.");
		return out;
	}
	Ref<Image> rgba = src->duplicate();
	if (rgba.is_null()) {
		UtilityFunctions::push_error("BulletEffectLayerData2D.whiten_image_copy: could not duplicate source image.");
		return out;
	}
	rgba->convert(Image::FORMAT_RGBA8);
	// One pass over the raw RGBA8 bytes (mip levels included) instead of a
	// get_pixel/set_pixel extension call pair per pixel (~500k calls for a
	// 512x512 frame). Alpha bytes are untouched.
	PackedByteArray data = rgba->get_data();
	uint8_t *bytes = data.ptrw();
	const int64_t byte_count = data.size();
	for (int64_t k = 0; k + 3 < byte_count; k += 4) {
		bytes[k] = 255;
		bytes[k + 1] = 255;
		bytes[k + 2] = 255;
	}
	rgba->set_data(rgba->get_width(), rgba->get_height(), rgba->has_mipmaps(), Image::FORMAT_RGBA8, data);
	return rgba;
}

// Reads frame pixels for the whiten override: plain textures directly,
// atlas frames through the base image plus region blit. Returns null when
// the pixels are unreachable (caller falls back to the original frame).
Ref<Image> BulletEffectLayerData2D::read_frame_image(const Ref<Texture2D> &tex) {
	Ref<Image> out;
	if (tex.is_null()) {
		return out;
	}
	if (const Ref<AtlasTexture> atlas = tex; atlas.is_valid()) {
		const Ref<Texture2D> base = atlas->get_atlas();
		if (base.is_null()) {
			return out;
		}
		Ref<Image> base_img = base->get_image();
		if (base_img.is_null() || base_img->is_empty()) {
			return out;
		}
		const Rect2 region = atlas->get_region();
		const int x = (int)Math::round(region.position.x);
		const int y = (int)Math::round(region.position.y);
		const int w = (int)Math::round(region.size.x);
		const int h = (int)Math::round(region.size.y);
		if (w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > base_img->get_width() || y + h > base_img->get_height()) {
			return out;
		}
		base_img = base_img->duplicate();
		if (base_img.is_null()) {
			return out;
		}
		base_img->convert(Image::FORMAT_RGBA8);
		Ref<Image> clip = Image::create_empty(w, h, false, Image::FORMAT_RGBA8);
		if (clip.is_null()) {
			return out;
		}
		clip->blit_rect(base_img, Rect2i(x, y, w, h), Vector2i(0, 0));
		return clip;
	}
	return tex->get_image();
}

void BulletEffectLayerData2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_enabled"), &BulletEffectLayerData2D::get_enabled);
	ClassDB::bind_method(D_METHOD("set_enabled", "value"), &BulletEffectLayerData2D::set_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "get_enabled");

	ClassDB::bind_method(D_METHOD("get_trigger"), &BulletEffectLayerData2D::get_trigger);
	ClassDB::bind_method(D_METHOD("set_trigger", "value"), &BulletEffectLayerData2D::set_trigger);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "trigger", PROPERTY_HINT_ENUM, "Trail Follow,On Spawn,On Hit,On Destroy,On Bounce,On Lifetime Over,On Clear"), "set_trigger", "get_trigger");

	ClassDB::bind_method(D_METHOD("get_sprite_frames"), &BulletEffectLayerData2D::get_sprite_frames);
	ClassDB::bind_method(D_METHOD("set_sprite_frames", "new_frames"), &BulletEffectLayerData2D::set_sprite_frames);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sprite_frames", PROPERTY_HINT_RESOURCE_TYPE, "SpriteFrames"), "set_sprite_frames", "get_sprite_frames");

	ClassDB::bind_method(D_METHOD("get_animation"), &BulletEffectLayerData2D::get_animation);
	ClassDB::bind_method(D_METHOD("set_animation", "new_animation"), &BulletEffectLayerData2D::set_animation);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "animation"), "set_animation", "get_animation");

	ClassDB::bind_method(D_METHOD("get_material"), &BulletEffectLayerData2D::get_material);
	ClassDB::bind_method(D_METHOD("set_material", "new_material"), &BulletEffectLayerData2D::set_material);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "ShaderMaterial,CanvasItemMaterial"), "set_material", "get_material");

	ClassDB::bind_method(D_METHOD("get_self_modulate"), &BulletEffectLayerData2D::get_self_modulate);
	ClassDB::bind_method(D_METHOD("set_self_modulate", "new_color"), &BulletEffectLayerData2D::set_self_modulate);
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "self_modulate"), "set_self_modulate", "get_self_modulate");

	ClassDB::bind_method(D_METHOD("get_color_ramp"), &BulletEffectLayerData2D::get_color_ramp);
	ClassDB::bind_method(D_METHOD("set_color_ramp", "new_ramp"), &BulletEffectLayerData2D::set_color_ramp);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "color_ramp", PROPERTY_HINT_RESOURCE_TYPE, "Gradient"), "set_color_ramp", "get_color_ramp");

	ClassDB::bind_method(D_METHOD("get_fade_in_sec"), &BulletEffectLayerData2D::get_fade_in_sec);
	ClassDB::bind_method(D_METHOD("set_fade_in_sec", "value"), &BulletEffectLayerData2D::set_fade_in_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fade_in_sec", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_fade_in_sec", "get_fade_in_sec");

	ClassDB::bind_method(D_METHOD("get_fade_out_sec"), &BulletEffectLayerData2D::get_fade_out_sec);
	ClassDB::bind_method(D_METHOD("set_fade_out_sec", "value"), &BulletEffectLayerData2D::set_fade_out_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fade_out_sec", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater"), "set_fade_out_sec", "get_fade_out_sec");

	ClassDB::bind_method(D_METHOD("get_override_frame_color"), &BulletEffectLayerData2D::get_override_frame_color);
	ClassDB::bind_method(D_METHOD("set_override_frame_color", "value"), &BulletEffectLayerData2D::set_override_frame_color);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "override_frame_color"), "set_override_frame_color", "get_override_frame_color");

	ClassDB::bind_method(D_METHOD("get_z_index"), &BulletEffectLayerData2D::get_z_index);
	ClassDB::bind_method(D_METHOD("set_z_index", "value"), &BulletEffectLayerData2D::set_z_index);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "z_index"), "set_z_index", "get_z_index");

	ClassDB::bind_method(D_METHOD("get_z_as_relative"), &BulletEffectLayerData2D::get_z_as_relative);
	ClassDB::bind_method(D_METHOD("set_z_as_relative", "value"), &BulletEffectLayerData2D::set_z_as_relative);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "z_as_relative"), "set_z_as_relative", "get_z_as_relative");

	ClassDB::bind_method(D_METHOD("get_visibility_layer"), &BulletEffectLayerData2D::get_visibility_layer);
	ClassDB::bind_method(D_METHOD("set_visibility_layer", "value"), &BulletEffectLayerData2D::set_visibility_layer);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "visibility_layer", PROPERTY_HINT_LAYERS_2D_RENDER), "set_visibility_layer", "get_visibility_layer");

	ClassDB::bind_method(D_METHOD("get_light_mask"), &BulletEffectLayerData2D::get_light_mask);
	ClassDB::bind_method(D_METHOD("set_light_mask", "value"), &BulletEffectLayerData2D::set_light_mask);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "light_mask", PROPERTY_HINT_LAYERS_2D_RENDER), "set_light_mask", "get_light_mask");

	ClassDB::bind_method(D_METHOD("get_offset"), &BulletEffectLayerData2D::get_offset);
	ClassDB::bind_method(D_METHOD("set_offset", "new_offset"), &BulletEffectLayerData2D::set_offset);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "offset"), "set_offset", "get_offset");

	ClassDB::bind_method(D_METHOD("get_rotation_degrees"), &BulletEffectLayerData2D::get_rotation_degrees);
	ClassDB::bind_method(D_METHOD("set_rotation_degrees", "value"), &BulletEffectLayerData2D::set_rotation_degrees);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rotation_degrees"), "set_rotation_degrees", "get_rotation_degrees");

	ClassDB::bind_method(D_METHOD("get_spin_degrees_per_sec"), &BulletEffectLayerData2D::get_spin_degrees_per_sec);
	ClassDB::bind_method(D_METHOD("set_spin_degrees_per_sec", "value"), &BulletEffectLayerData2D::set_spin_degrees_per_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spin_degrees_per_sec"), "set_spin_degrees_per_sec", "get_spin_degrees_per_sec");

	ClassDB::bind_method(D_METHOD("get_scale"), &BulletEffectLayerData2D::get_scale);
	ClassDB::bind_method(D_METHOD("set_scale", "new_scale"), &BulletEffectLayerData2D::set_scale);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "scale"), "set_scale", "get_scale");

	ClassDB::bind_method(D_METHOD("get_random_start_frame"), &BulletEffectLayerData2D::get_random_start_frame);
	ClassDB::bind_method(D_METHOD("set_random_start_frame", "value"), &BulletEffectLayerData2D::set_random_start_frame);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "random_start_frame"), "set_random_start_frame", "get_random_start_frame");

	ClassDB::bind_method(D_METHOD("get_randomize_rotation"), &BulletEffectLayerData2D::get_randomize_rotation);
	ClassDB::bind_method(D_METHOD("set_randomize_rotation", "value"), &BulletEffectLayerData2D::set_randomize_rotation);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "randomize_rotation"), "set_randomize_rotation", "get_randomize_rotation");

	ClassDB::bind_method(D_METHOD("get_random_scale_min"), &BulletEffectLayerData2D::get_random_scale_min);
	ClassDB::bind_method(D_METHOD("set_random_scale_min", "value"), &BulletEffectLayerData2D::set_random_scale_min);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "random_scale_min"), "set_random_scale_min", "get_random_scale_min");

	ClassDB::bind_method(D_METHOD("get_random_scale_max"), &BulletEffectLayerData2D::get_random_scale_max);
	ClassDB::bind_method(D_METHOD("set_random_scale_max", "value"), &BulletEffectLayerData2D::set_random_scale_max);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "random_scale_max"), "set_random_scale_max", "get_random_scale_max");

	ClassDB::bind_method(D_METHOD("get_trigger_chance"), &BulletEffectLayerData2D::get_trigger_chance);
	ClassDB::bind_method(D_METHOD("set_trigger_chance", "value"), &BulletEffectLayerData2D::set_trigger_chance);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "trigger_chance", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_trigger_chance", "get_trigger_chance");

	ClassDB::bind_method(D_METHOD("get_max_instances"), &BulletEffectLayerData2D::get_max_instances);
	ClassDB::bind_method(D_METHOD("set_max_instances", "value"), &BulletEffectLayerData2D::set_max_instances);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_instances", PROPERTY_HINT_RANGE, "0,4096,1"), "set_max_instances", "get_max_instances");

	ClassDB::bind_static_method("BulletEffectLayerData2D", D_METHOD("whiten_image_copy", "src"), &BulletEffectLayerData2D::whiten_image_copy);

	BIND_ENUM_CONSTANT(EFFECT_TRAIL_FOLLOW);
	BIND_ENUM_CONSTANT(EFFECT_ON_SPAWN);
	BIND_ENUM_CONSTANT(EFFECT_ON_HIT);
	BIND_ENUM_CONSTANT(EFFECT_ON_DESTROY);
	BIND_ENUM_CONSTANT(EFFECT_ON_BOUNCE);
	BIND_ENUM_CONSTANT(EFFECT_ON_LIFETIME_OVER);
	BIND_ENUM_CONSTANT(EFFECT_ON_CLEAR);
}
} //namespace BlastBullets2D

VARIANT_ENUM_CAST(BlastBullets2D::EffectTrigger);
