#include "sprite/sprite_anim2d.hpp"

#include "core/cached_string_names2d.hpp"
#include "sprite/frame_texture_cache2d.hpp"

#include <godot_cpp/core/math.hpp>

using namespace godot;

namespace BlastBullets2D {

SpriteAnimChoice2D resolve_sprite_animation2d(const Ref<SpriteFrames> &frames, const StringName &requested) {
	SpriteAnimChoice2D choice;
	if (frames.is_null()) {
		choice.pick = SpriteAnimPick2D::NULL_FRAMES;
		return choice;
	}
	const PackedStringArray names = frames->get_animation_names();
	if (names.is_empty()) {
		choice.pick = SpriteAnimPick2D::NO_ANIMATIONS;
		return choice;
	}
	const StringName &default_name = CachedStringNames2D::get().default_animation;
	const bool is_auto = requested.is_empty() || requested == default_name;
	if (is_auto && frames->has_animation(default_name) && frames->get_frame_count(default_name) > 0) {
		choice.pick = SpriteAnimPick2D::RESOLVED;
		choice.anim = default_name;
		return choice;
	}
	if (!is_auto && frames->has_animation(requested) && frames->get_frame_count(requested) > 0) {
		choice.pick = SpriteAnimPick2D::RESOLVED;
		choice.anim = requested;
		return choice;
	}
	for (int i = 0; i < names.size(); ++i) {
		const StringName name = names[i];
		if (frames->get_frame_count(name) > 0) {
			choice.pick = is_auto ? SpriteAnimPick2D::RESOLVED : SpriteAnimPick2D::FELL_BACK;
			choice.anim = name;
			return choice;
		}
	}
	choice.pick = SpriteAnimPick2D::NO_FRAMES_ANYWHERE;
	return choice;
}

double sprite_anim_fps2d(double fps, bool &r_bad) {
	if (!Math::is_finite(fps) || fps < 0.0) {
		r_bad = true;
		return 1.0;
	}
	return fps;
}

double sprite_frame_sec2d(double duration, double fps, bool &r_bad) {
	if (!Math::is_finite(duration)) {
		r_bad = true;
		return 0.0;
	}
	if (duration <= 0.0 || !(fps > 0.0)) {
		return 0.0;
	}
	return duration / fps;
}

void SpriteAnim2D::clear() {
	name = StringName();
	frames.clear();
	secs.clear();
	ends.clear();
	total = 0.0;
	loop = true;
	held = false;
}

void SpriteAnim2D::finish_timing() {
	ends.clear();
	ends.reserve(secs.size());
	double acc = 0.0;
	for (double s : secs) {
		acc += s;
		ends.push_back(acc);
	}
	total = acc;
}

int SpriteAnim2D::frame_at(double t) const {
	if (frames.empty() || held || !(total > 0.0) || !Math::is_finite(t) || t <= 0.0) {
		return 0;
	}
	if (!loop && t >= total) {
		return (int)frames.size() - 1;
	}
	return frame_at_looped(t);
}

Texture2D *SpriteAnim2D::texture_at(double t) const {
	if (frames.empty()) {
		return nullptr;
	}
	return frames[frame_at(t)].ptr();
}

bool bake_sprite_animation2d(const Ref<SpriteFrames> &frames, const StringName &anim, const SpriteAnimBakeOptions2D &options, SpriteAnim2D &out, SpriteAnimBakeReport2D &report) {
	if (frames.is_null()) {
		return false;
	}
	const int count = frames->get_frame_count(anim);
	const double fps = sprite_anim_fps2d(frames->get_animation_speed(anim), report.bad_speed);
	SpriteAnim2D baked;
	baked.name = anim;
	baked.loop = frames->get_animation_loop(anim);
	baked.held = fps == 0.0;
	baked.frames.reserve(count);
	baked.secs.reserve(count);
	for (int i = 0; i < count; ++i) {
		const Ref<Texture2D> tex = frames->get_frame_texture(anim, i);
		if (tex.is_null()) {
			report.null_frame = i;
			return false;
		}
		bool bad_duration = false;
		baked.secs.push_back(sprite_frame_sec2d((double)frames->get_frame_duration(anim, i), fps, bad_duration));
		if (bad_duration) {
			report.bad_duration_frames.push_back(i);
		}
		if (!options.whiten) {
			baked.frames.push_back(tex);
			continue;
		}
		const Ref<Texture2D> white = options.cache != nullptr ? options.cache->whitened(tex) : FrameTextureCache2D::build_whitened(tex);
		if (white.is_valid()) {
			baked.frames.push_back(white);
		} else {
			report.whiten_unreadable = true;
			baked.frames.push_back(tex);
		}
	}
	baked.finish_timing();
	out = std::move(baked);
	return true;
}

} // namespace BlastBullets2D
