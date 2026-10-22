#pragma once

// The factory's cache of textures the plugin makes from SpriteFrames frames
// (sprite/frame_image2d.hpp reads the pixels as Godot draws them):
// - DRAWABLE: what a mesh draw shows for a frame. A mesh draw (MultiMesh,
//   triangle array) hands the engine texture->get_rid(), and an
//   AtlasTexture's RID is its whole atlas drawn with the mesh's 0..1 UVs, so
//   an AtlasTexture frame is baked into an ImageTexture of exactly what it
//   draws (region, margin box). Any other texture draws itself.
// - WHITENED: override_frame_color's white copy (alpha kept).
// Reading pixels is a GPU readback, so each texture is built once per
// source and reused by every spawn, pool reuse and system.

#include "sprite/frame_image2d.hpp"

#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <cstdint>
#include <unordered_map>

namespace BlastBullets2D {
using namespace godot;

class FrameTextureCache2D {
public:
	// Bounded: cleared wholesale past this many entries per kind (a rebuild
	// costs one readback, never a wrong frame).
	static constexpr size_t MAX_ENTRIES = 4096;

	// What a mesh draw shows for `source`: the texture itself, or for an
	// AtlasTexture its baked region. Null when an AtlasTexture's region
	// cannot be read (the caller keeps the original and warns).
	Ref<Texture2D> drawable(const Ref<Texture2D> &source, FrameImageReader2D &reader);
	// The whitened copy (white, alpha kept) of what `source` draws. Null when
	// its pixels cannot be read (the caller keeps the original art).
	Ref<Texture2D> whitened(const Ref<Texture2D> &source, FrameImageReader2D &reader);
	// The same textures, uncached (a volley without a factory).
	static Ref<Texture2D> build_drawable(const Ref<Texture2D> &source, FrameImageReader2D &reader);
	static Ref<Texture2D> build_whitened(const Ref<Texture2D> &source, FrameImageReader2D &reader);

	void clear();
	// {entries, builds, hits, sheet_reads, unreadable}
	Dictionary debug_stats() const;

private:
	struct Entry {
		Ref<Texture2D> texture; // null: unreadable
		uint64_t signature = 0; // frame_signature2d at build time
	};
	// Keyed by the source's instance id (ids are never reused within a
	// session); an entry whose source changed since (region, margin, atlas,
	// size) is rebuilt. A texture repainted in place keeps its entry until a
	// full BulletFactory2D.reset() clears the cache.
	std::unordered_map<uint64_t, Entry> drawable_entries;
	std::unordered_map<uint64_t, Entry> whitened_entries;
	uint64_t builds = 0;
	uint64_t hits = 0;
	uint64_t sheet_reads = 0;
	uint64_t unreadable = 0;

	Ref<Texture2D> lookup(std::unordered_map<uint64_t, Entry> &entries, const Ref<Texture2D> &source, bool whiten, FrameImageReader2D &reader);
};

} // namespace BlastBullets2D
