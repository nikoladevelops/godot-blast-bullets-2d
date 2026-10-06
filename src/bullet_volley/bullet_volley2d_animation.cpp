// Appearance: SpriteFrames animation, fade in/out and tint ramps, the frame-color
// override (whiten), instance shader parameters and per-bullet custom data.

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

static bool resolve_sprite_animation_impl(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_requested, StringName &out_anim, bool silent) {
	if (p_sprite_frames.is_null()) {
		if (!silent) {
			UtilityFunctions::push_error("BulletVolley2D: sprite_frames is null. Assign a SpriteFrames resource.");
		}
		return false;
	}
	const PackedStringArray names = p_sprite_frames->get_animation_names();
	if (names.is_empty()) {
		if (!silent) {
			UtilityFunctions::push_error("BulletVolley2D: sprite_frames has no animations.");
		}
		return false;
	}
	const String requested_str = String(p_requested);
	const bool is_auto = requested_str.is_empty() || p_requested == StringName("default");
	// Picks the first animation that actually has frames. An empty "default" (fresh
	// SpriteFrames resources always contain one) must not shadow a populated animation.
	auto first_with_frames = [&]() -> StringName {
		for (int i = 0; i < names.size(); ++i) {
			if (p_sprite_frames->get_frame_count(names[i]) > 0) {
				return names[i];
			}
		}
		return StringName();
	};
	if (is_auto) {
		// Unselected animation: play "default" silently when usable, else first animation
		// with frames, silently.
		if (p_sprite_frames->has_animation(StringName("default")) && p_sprite_frames->get_frame_count(StringName("default")) > 0) {
			out_anim = StringName("default");
			return true;
		}
		const StringName fallback = first_with_frames();
		if (String(fallback).is_empty()) {
			if (!silent) {
				UtilityFunctions::push_error("BulletVolley2D: sprite_frames has no animation with frames.");
			}
			return false;
		}
		out_anim = fallback;
		return true;
	}
	if (p_sprite_frames->has_animation(p_requested) && p_sprite_frames->get_frame_count(p_requested) > 0) {
		out_anim = p_requested;
		return true;
	}
	const StringName fallback = first_with_frames();
	if (String(fallback).is_empty()) {
		if (!silent) {
			UtilityFunctions::push_error("BulletVolley2D: sprite_frames has no animation with frames.");
		}
		return false;
	}
	if (!silent) {
		UtilityFunctions::push_error("BulletVolley2D: missing animation '" + requested_str + "', falling back to '" + String(fallback) + "'.");
	}
	out_anim = fallback;
	return true;
}

void BulletVolley2D::advance_sprite_animation(double delta) {
	const int64_t frame_count = (int64_t)anim_frames.size();
	if (frame_count <= 1 || !is_active || anim_paused || anim_finished) {
		return;
	}
	if (!Math::is_finite(delta) || delta <= 0.0) {
		return;
	}
	if (delta > 0.5) {
		delta = 0.5; // clamp hitch spikes so one tick can't fast-forward whole anims
	}
	anim_frame_time_left -= delta;
	int strides = 0;
	while (anim_frame_time_left <= 0.0) {
		if (++strides > 8) {
			// Anti-spiral: resync timer to current frame instead of looping forever.
			anim_frame_time_left = anim_frame_secs[anim_frame_index] > 0.0 ? anim_frame_secs[anim_frame_index] : 0.0;
			break;
		}
		int next = anim_frame_index + 1;
		if (next >= frame_count) {
			if (anim_loop) {
				next = 0;
			} else {
				anim_frame_index = (int)frame_count - 1;
				anim_frame_time_left = 0.0;
				if (!anim_finished) {
					anim_finished = true;
					// The volley tick emits sprite_animation_finished LIVE right
					// after this call (on `this`: the signal lives on the volley).
					anim_finished_event_pending = true;
				}
				return;
			}
		}
		anim_frame_index = next;
		set_texture(anim_frames[anim_frame_index]);
		anim_frame_time_left += anim_frame_secs[anim_frame_index];
		// Guard against zero-length frames looping forever in one tick.
		if (anim_frame_secs[anim_frame_index] <= 0.0) {
			break;
		}
	}
}

Ref<Resource> BulletVolley2D::bullet_get_custom_data(int bullet_index) const {
	if (!validate_bullet_index(bullet_index, "bullet_get_custom_data")) {
		return Ref<Resource>();
	}
	if (bullet_index < (int)all_bullets_custom_data.size() && all_bullets_custom_data[bullet_index].is_valid()) {
		return all_bullets_custom_data[bullet_index];
	}
	return Ref<Resource>();
}

void BulletVolley2D::bullet_set_custom_data(int bullet_index, const Ref<Resource> &new_custom_data) {
	if (!validate_bullet_index(bullet_index, "bullet_set_custom_data")) {
		return;
	}
	if (bullet_index >= (int)all_bullets_custom_data.size()) {
		return;
	}
	all_bullets_custom_data[bullet_index] = new_custom_data;
}

TypedArray<Resource> BulletVolley2D::all_bullets_get_custom_data(int bullet_index_start, int bullet_index_end_inclusive) {
	return collect_range<TypedArray<Resource>>(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_custom_data", [&](int i) { return bullet_get_custom_data(i); });
}

void BulletVolley2D::all_bullets_set_custom_data(const Ref<Resource> &new_custom_data, int bullet_index_start, int bullet_index_end_inclusive) {
	for_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_custom_data", [&](int i) { bullet_set_custom_data(i, new_custom_data); });
}

void BulletVolley2D::clear_applied_instance_shader_overrides() {
	for (const String &key : applied_instance_shader_keys) {
		if (!key.is_empty()) {
			set_instance_shader_parameter(key, Variant());
		}
	}
	applied_instance_shader_keys.clear();
}

void BulletVolley2D::disconnect_sprite_animation_connections() {
	for (const Dictionary &connection : get_signal_connection_list("sprite_animation_finished")) {
		const Callable callable = connection["callable"];
		disconnect("sprite_animation_finished", callable);
	}
}

double BulletVolley2D::get_fade_in_sec() const {
	return fade_in_sec;
}

void BulletVolley2D::set_fade_in_sec(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolley2D: fade_in_sec must be finite and >= 0, keeping the old value.");
		return;
	}
	fade_in_sec = value;
}

double BulletVolley2D::get_fade_out_sec() const {
	return fade_out_sec;
}

void BulletVolley2D::set_fade_out_sec(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletVolley2D: fade_out_sec must be finite and >= 0, keeping the old value.");
		return;
	}
	fade_out_sec = value;
}

Ref<Gradient> BulletVolley2D::get_modulate_ramp() const {
	return fade_modulate_ramp;
}

void BulletVolley2D::set_modulate_ramp(const Ref<Gradient> &value) {
	fade_modulate_ramp = value;
}

Color BulletVolley2D::get_fade_base_modulate() const {
	return fade_base_modulate;
}

void BulletVolley2D::set_fade_base_modulate(const Color &value) {
	fade_base_modulate = value;
	// No fade/ramp configured: the tick early-returns and would never apply
	// the new base, so write it now (this setter is the documented path).
	if (fade_in_sec <= 0.0 && fade_out_sec <= 0.0 && fade_modulate_ramp.is_null()) {
		set_self_modulate(value);
		fade_applied = value;
		return;
	}
	// Re-arm the change detector so the next tick writes even if the value
	// happens to equal the stale applied snapshot.
	fade_applied = Color(-1, -1, -1, -1);
}

bool BulletVolley2D::get_override_frame_color() const {
	return anim_override_frame_color;
}

void BulletVolley2D::set_override_frame_color(bool value) {
	if (anim_override_frame_color == value) {
		return;
	}
	anim_override_frame_color = value;
	// Live toggle rebuilds the cached frames from the stored source (same
	// path as play_sprite_animation_name). No source yet means nothing to
	// rebuild: the next spawn/enable applies it through the snapshot.
	if (!anim_source.is_null()) {
		rebuild_sprite_animation(anim_source, anim_name);
	}
}

void BulletVolley2D::snapshot_appearance_from_data(const BulletVolleyData2D &data) {
	fade_base_modulate = data.self_modulate;
	fade_in_sec = data.fade_in_sec;
	fade_out_sec = data.fade_out_sec;
	fade_modulate_ramp = data.modulate_ramp;
	anim_override_frame_color = data.override_frame_color;
	if (fade_in_sec > 0.0) {
		// No full-alpha flash before the first tick: start transparent now,
		// the tick below ramps up from here.
		Color start = fade_base_modulate;
		start.a = 0.0;
		set_self_modulate(start);
		fade_applied = start;
	} else {
		fade_applied = fade_base_modulate;
	}
}

void BulletVolley2D::tick_volley_fade() {
	if (fade_in_sec <= 0.0 && fade_out_sec <= 0.0 && fade_modulate_ramp.is_null()) {
		return;
	}
	if (!Math::is_finite(curves_elapsed_time)) {
		return;
	}
	const double age = curves_elapsed_time;
	double alpha = 1.0;
	if (fade_in_sec > 0.0 && age < fade_in_sec) {
		alpha = age / fade_in_sec;
	}
	Color target = fade_base_modulate;
	// Fade-out and the ramp need a lifetime fraction: infinite volleys
	// never expire, so only fade-in applies there (documented).
	if (!is_life_time_infinite && max_life_time > 0.0 && Math::is_finite(current_life_time)) {
		if (fade_out_sec > 0.0 && current_life_time < fade_out_sec) {
			const double out_alpha = current_life_time / fade_out_sec;
			if (out_alpha < alpha) {
				alpha = out_alpha;
			}
		}
		if (fade_modulate_ramp.is_valid()) {
			const double pos = Math::clamp(age / max_life_time, 0.0, 1.0);
			target = fade_base_modulate * fade_modulate_ramp->sample((float)pos);
		}
	}
	if (alpha < 0.0) {
		alpha = 0.0;
	} else if (alpha > 1.0) {
		alpha = 1.0;
	}
	target.a *= (float)alpha;
	if (target == fade_applied) {
		return;
	}
	set_self_modulate(target);
	fade_applied = target;
}

bool BulletVolley2D::resolve_sprite_animation(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_requested, StringName &out_anim) {
	return resolve_sprite_animation_impl(p_sprite_frames, p_requested, out_anim, false);
}

bool BulletVolley2D::resolve_sprite_animation_quiet(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_requested, StringName &out_anim) {
	return resolve_sprite_animation_impl(p_sprite_frames, p_requested, out_anim, true);
}

bool BulletVolley2D::rebuild_sprite_animation(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_animation) {
	StringName anim;
	if (!resolve_sprite_animation(p_sprite_frames, p_animation, anim)) {
		return false; // error already reported, previous animation untouched
	}
	const int count = p_sprite_frames->get_frame_count(anim);
	if (count <= 0) {
		UtilityFunctions::push_error("BulletVolley2D: animation '" + String(anim) + "' has no frames.");
		return false;
	}
	double fps = p_sprite_frames->get_animation_speed(anim);
	if (!Math::is_finite(fps) || fps <= 0.0) {
		UtilityFunctions::push_error("BulletVolley2D: animation '" + String(anim) + "' has invalid speed, using 1 fps.");
		fps = 1.0;
	}
	std::vector<Ref<Texture2D>> frames;
	std::vector<double> secs;
	frames.reserve(count);
	secs.reserve(count);
	bool whiten_warned = false;
	for (int i = 0; i < count; ++i) {
		Ref<Texture2D> tex = p_sprite_frames->get_frame_texture(anim, i);
		if (tex.is_null()) {
			UtilityFunctions::push_error("BulletVolley2D: animation '" + String(anim) + "' frame " + String::num_int64(i) + " has null texture.");
			return false; // previous cache untouched (swap only on success below)
		}
		const float dur = p_sprite_frames->get_frame_duration(anim, i);
		if (!Math::is_finite((double)dur)) {
			UtilityFunctions::push_error("BulletVolley2D: animation '" + String(anim) + "' frame " + String::num_int64(i) + " has non-finite duration, using 0.");
		}
		if (anim_override_frame_color) {
			// Exact-color bullets: whitened copy (alpha preserved) so the
			// volley tint reads exactly. Same fallback contract as the
			// effect-layer override: unreadable frames keep the original
			// art with one warning per rebuild, never a blank.
			// Factory-cached: the whiten reads pixels back from the GPU, so
			// rebuilding it on every spawn/pool reuse was a per-frame stall.
			Ref<Texture2D> white_tex;
			if (bullet_factory != nullptr) {
				white_tex = bullet_factory->get_whitened_frame(tex);
			} else {
				Ref<Image> white = BulletEffectLayerData2D::whiten_image_copy(BulletEffectLayerData2D::read_frame_image(tex));
				if (white.is_valid()) {
					Ref<ImageTexture> fresh;
					fresh.instantiate();
					fresh->set_image(white);
					white_tex = fresh;
				}
			}
			if (white_tex.is_valid()) {
				frames.push_back(white_tex);
			} else {
				if (!whiten_warned) {
					whiten_warned = true;
					UtilityFunctions::push_warning("BulletVolley2D: override_frame_color could not read a frame of '" + String(anim) + "', keeping the original art for unreadable frames.");
				}
				frames.push_back(tex);
			}
		} else {
			frames.push_back(tex);
		}
		secs.push_back((!Math::is_finite((double)dur) || dur <= 0.0f) ? 0.0 : (double)dur / fps);
	}
	anim_source = p_sprite_frames;
	anim_name = anim;
	anim_frames.swap(frames);
	anim_frame_secs.swap(secs);
	anim_loop = p_sprite_frames->get_animation_loop(anim);
	anim_paused = false;
	anim_finished = false;
	anim_frame_index = 0;
	anim_frame_time_left = anim_frame_secs[0];
	set_texture(anim_frames[0]);
	return true;
}

bool BulletVolley2D::play_sprite_animation(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_animation) {
	if (p_sprite_frames.is_null()) {
		UtilityFunctions::push_error("BulletVolley2D play_sprite_animation: sprite_frames is null.");
		return false;
	}
	// Empty follows the same auto-resolve rules as spawn ("default" if present,
	// else the first animation with frames) instead of erroring.
	return rebuild_sprite_animation(p_sprite_frames, p_animation);
}

bool BulletVolley2D::play_sprite_animation_name(const StringName &p_animation) {
	if (anim_source.is_null()) {
		UtilityFunctions::push_error("BulletVolley2D play_sprite_animation_name: no SpriteFrames cached yet, call play_sprite_animation first.");
		return false;
	}
	// Empty follows the same auto-resolve rules as spawn.
	return rebuild_sprite_animation(anim_source, p_animation);
}

bool BulletVolley2D::restart_sprite_animation() {
	if (anim_frames.empty()) {
		UtilityFunctions::push_error("BulletVolley2D restart_sprite_animation: no baked animation to restart.");
		return false;
	}
	anim_frame_index = 0;
	anim_frame_time_left = anim_frame_secs[0];
	anim_paused = false;
	anim_finished = false;
	set_texture(anim_frames[0]);
	return true;
}

Vector2 BulletVolley2D::resolve_quad_size(const Ref<SpriteFrames> &p_sprite_frames, const StringName &p_animation, Vector2 override_size) {
	if (override_size.is_finite() && override_size.x > 0.0f && override_size.y > 0.0f) {
		return override_size;
	}
	if (override_size != Vector2(0, 0) && (!override_size.is_finite() || override_size.x <= 0.0f || override_size.y <= 0.0f)) {
		WarnOnce2D::warn(0, 3u, (int64_t)(override_size.x * 1024.0f), (int64_t)(override_size.y * 1024.0f), "BulletVolley2D: texture_size override is non-finite or non-positive, deriving size from the first frame.");
	}
	// Silent fallback: rebuild_sprite_animation owns all error reporting (spawn calls
	// both, so resolving loudly here would print every failure twice).
	StringName anim;
	if (!resolve_sprite_animation_quiet(p_sprite_frames, p_animation, anim)) {
		return Vector2(32, 32);
	}
	if (p_sprite_frames->get_frame_count(anim) > 0) {
		if (const Ref<Texture2D> tex = p_sprite_frames->get_frame_texture(anim, 0); tex.is_valid()) {
			if (const Ref<AtlasTexture> atlas = tex; atlas.is_valid()) {
				const Vector2 region = atlas->get_region().size;
				if (region.is_finite() && region.x > 0.0f && region.y > 0.0f) {
					return region;
				}
			}
			const Vector2 size = tex->get_size();
			if (size.is_finite() && size.x > 0.0f && size.y > 0.0f) {
				return size;
			}
		}
	}
	return Vector2(32, 32);
}

} // namespace BlastBullets2D
