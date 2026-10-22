#pragma once

// The factory's cache of textures the plugin makes from SpriteFrames frames.
// Reading a frame's pixels is a GPU readback, so each copy is built once per
// source and reused by every spawn, pool reuse and system.

#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <cstdint>
#include <unordered_map>

namespace BlastBullets2D {
using namespace godot;

class FrameTextureCache2D {
public:
	// Bounded: cleared wholesale past this many entries (a rebuild costs one
	// readback, never a wrong frame).
	static constexpr size_t MAX_ENTRIES = 4096;

	// The whitened copy (override_frame_color: white, alpha kept) of a frame,
	// built once. Null when the frame's pixels cannot be read (the caller
	// keeps the original art).
	Ref<Texture2D> whitened(const Ref<Texture2D> &source);
	// The same copy, uncached (a volley without a factory).
	static Ref<Texture2D> build_whitened(const Ref<Texture2D> &source);

	void clear();
	// {entries, builds, hits, unreadable}
	Dictionary debug_stats() const;

private:
	// Keyed by the source's instance id (ids are never reused within a
	// session); a null entry remembers an unreadable frame.
	std::unordered_map<uint64_t, Ref<Texture2D>> whitened_entries;
	uint64_t builds = 0;
	uint64_t hits = 0;
	uint64_t unreadable = 0;
};

} // namespace BlastBullets2D
