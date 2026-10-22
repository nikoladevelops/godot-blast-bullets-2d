#pragma once

// SpriteFrames -> drawable animation, shared by every system that draws one
// (bullet volleys, effect layers, lasers, pickups): ONE animation resolver,
// ONE timing rule, ONE frame loop and ONE frame sampler. Nothing here prints:
// each caller reports the outcome in its own class-prefixed wording.

#include <godot_cpp/classes/sprite_frames.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

namespace BlastBullets2D {
using namespace godot;

class FrameTextureCache2D;

// What resolve_sprite_animation2d found.
enum class SpriteAnimPick2D : uint8_t {
	NULL_FRAMES, // no SpriteFrames
	NO_ANIMATIONS, // a SpriteFrames without a single animation
	NO_FRAMES_ANYWHERE, // every animation is empty
	RESOLVED, // the requested animation, or the silent pick for an empty / "default" name
	FELL_BACK, // an explicit name that is missing or empty: the first animation with frames
};

struct SpriteAnimChoice2D {
	SpriteAnimPick2D pick = SpriteAnimPick2D::NULL_FRAMES;
	StringName anim;
	bool usable() const { return pick == SpriteAnimPick2D::RESOLVED || pick == SpriteAnimPick2D::FELL_BACK; }
};

// The one name rule: empty or "default" plays "default" when it has frames,
// else the first animation (in SpriteFrames order) with frames, silently. An
// explicit name plays when it has frames; a missing or empty one falls back to
// the first animation with frames (the caller reports it). An empty "default"
// (every fresh SpriteFrames has one) never hides a populated animation.
SpriteAnimChoice2D resolve_sprite_animation2d(const Ref<SpriteFrames> &frames, const StringName &requested);

// The one timing rule. Speed: finite and >= 0 as is (0 holds the first frame,
// like AnimatedSprite2D); NaN, INF or negative play at 1 fps and set r_bad.
double sprite_anim_fps2d(double fps, bool &r_bad);
// One frame's seconds at a sanitized speed: duration / fps (0 while held);
// a non-finite duration counts 0 and sets r_bad; <= 0 counts 0.
double sprite_frame_sec2d(double duration, double fps, bool &r_bad);

// One baked animation: drawable frames and their seconds.
struct SpriteAnim2D {
	StringName name;
	std::vector<Ref<Texture2D>> frames;
	std::vector<double> secs;
	// Prefix sums of secs (frame end boundaries), accumulated in order so
	// ends.back() == total bit for bit.
	std::vector<double> ends;
	double total = 0.0;
	bool loop = true;
	// Speed 0: the animation never advances (frame 0 shows).
	bool held = false;

	void clear();
	bool empty() const { return frames.empty(); }
	int size() const { return (int)frames.size(); }
	// Recomputes ends and total from secs.
	void finish_timing();

	// The frame `t` seconds into one pass, looping by total (O(log F)). 0 for
	// a non-positive total or a non-finite t; past the end (float error) the
	// last frame.
	_ALWAYS_INLINE_ int frame_at_looped(double t) const {
		if (ends.empty() || !(total > 0.0) || !std::isfinite(t)) {
			return 0;
		}
		const double local = t - std::floor(t / total) * total;
		size_t lo = 0;
		size_t hi = ends.size();
		while (lo < hi) {
			const size_t mid = lo + (hi - lo) / 2;
			if (local < ends[mid]) {
				hi = mid;
			} else {
				lo = mid + 1;
			}
		}
		return lo < ends.size() ? (int)lo : (int)ends.size() - 1;
	}
	// The frame shown `t` seconds after the animation started: loops when
	// `loop`, else holds the last frame once it is over. Frame 0 before the
	// start, while held, or without playable time.
	int frame_at(double t) const;
	// frame_at as a texture (null without frames).
	Texture2D *texture_at(double t) const;
};

struct SpriteAnimBakeOptions2D {
	// override_frame_color: whitened copies (alpha kept) of every frame.
	bool whiten = false;
	// The factory's frame cache (null: build uncached).
	FrameTextureCache2D *cache = nullptr;
};

// What one bake ran into, for the caller's own texts.
struct SpriteAnimBakeReport2D {
	bool bad_speed = false; // NaN / INF / negative speed: 1 fps used
	std::vector<int> bad_duration_frames; // non-finite durations (counted 0), in order
	int null_frame = -1; // the first frame without a texture: the bake failed there
	bool whiten_unreadable = false; // a frame kept its original art under whiten
};

// The one frame loop: bakes animation `anim` (already resolved) of `frames`
// into `out` under the timing rule, mapping every frame through the frame
// cache. False when a frame has no texture (`out` untouched).
bool bake_sprite_animation2d(const Ref<SpriteFrames> &frames, const StringName &anim, const SpriteAnimBakeOptions2D &options, SpriteAnim2D &out, SpriteAnimBakeReport2D &report);

} // namespace BlastBullets2D
