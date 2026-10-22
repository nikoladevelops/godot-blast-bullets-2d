#pragma once

// Pixels of a SpriteFrames frame exactly as Godot draws it, for the textures
// the plugin bakes from frames (AtlasTexture regions, whitened copies).

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/texture2d.hpp>

#include <cstdint>
#include <unordered_map>

namespace BlastBullets2D {
using namespace godot;

// Reads frames for ONE bake: every texture it decodes (a sprite sheet, a
// nested atlas) is kept for the reader's life, so an animation of 29 regions
// of one sheet reads the sheet back from the GPU once. Never keep a reader
// across bakes (a sheet repainted in between would read stale).
class FrameImageReader2D {
public:
	// A baked frame never exceeds the GPU's texture side limit.
	static constexpr int MAX_SIDE = 16384;
	// Deepest AtlasTexture chain followed (an atlas of an atlas of ...).
	static constexpr int MAX_DEPTH = 16;

	// RGBA8 pixels as drawn. A plain texture: its image. An AtlasTexture: its
	// region (rounded; a zero width or height means the atlas's whole width
	// or height, like Godot) placed at margin.position inside a box of
	// region.size + margin.size (transparent around it, cut at the box), its
	// atlas read the same way when that is an AtlasTexture too. Compressed
	// images are decompressed. Null when unreadable: no texture, atlas or
	// image; a region that is non-finite or not fully inside its atlas; a
	// non-finite margin or an empty or oversized box; a format that cannot be
	// decompressed; a chain deeper than MAX_DEPTH. The image is shared with
	// later reads of this reader: copy it before changing it.
	Ref<Image> read(const Ref<Texture2D> &tex);

	// GPU readbacks so far (one per distinct non-atlas texture read).
	uint64_t sheet_reads = 0;

private:
	// Drawn pixels per texture instance id (sheets and atlas frames alike).
	std::unordered_map<uint64_t, Ref<Image>> drawn;

	Ref<Image> read_at_depth(const Ref<Texture2D> &tex, int depth);
	Ref<Image> read_atlas(const Ref<Texture2D> &tex, int depth);
	Ref<Image> decode(const Ref<Texture2D> &tex);
};

// A hash of everything that decides how a frame draws: the texture, and for
// an AtlasTexture chain each atlas, region and margin (bit for bit, NaN
// included). The frame cache rebuilds an entry whose signature moved.
uint64_t frame_signature2d(const Ref<Texture2D> &tex);

} // namespace BlastBullets2D
