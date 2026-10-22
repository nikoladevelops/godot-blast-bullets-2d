#include "sprite/frame_texture_cache2d.hpp"

#include "data/bullet_effect_layer_data2d.hpp"

#include <godot_cpp/classes/atlas_texture.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>

using namespace godot;

namespace BlastBullets2D {

static Ref<Texture2D> texture_from_image2d(const Ref<Image> &image) {
	Ref<Texture2D> result;
	if (image.is_valid() && !image->is_empty()) {
		result = ImageTexture::create_from_image(image);
	}
	return result;
}

Ref<Texture2D> FrameTextureCache2D::build_drawable(const Ref<Texture2D> &source, FrameImageReader2D &reader) {
	const Ref<AtlasTexture> atlas = source;
	if (atlas.is_null()) {
		return source;
	}
	return texture_from_image2d(reader.read(source));
}

Ref<Texture2D> FrameTextureCache2D::build_whitened(const Ref<Texture2D> &source, FrameImageReader2D &reader) {
	// whiten_image_copy reports an unreadable frame (null image) itself.
	return texture_from_image2d(BulletEffectLayerData2D::whiten_image_copy(reader.read(source)));
}

Ref<Texture2D> FrameTextureCache2D::lookup(std::unordered_map<uint64_t, Entry> &entries, const Ref<Texture2D> &source, bool whiten, FrameImageReader2D &reader) {
	const uint64_t key = (uint64_t)source->get_instance_id();
	const uint64_t signature = frame_signature2d(source);
	auto it = entries.find(key);
	if (it != entries.end() && it->second.signature == signature) {
		++hits;
		return it->second.texture;
	}
	if (it == entries.end() && entries.size() >= MAX_ENTRIES) {
		entries.clear();
	}
	const uint64_t reads_before = reader.sheet_reads;
	Entry entry;
	entry.signature = signature;
	entry.texture = whiten ? build_whitened(source, reader) : build_drawable(source, reader);
	++builds;
	sheet_reads += reader.sheet_reads - reads_before;
	if (entry.texture.is_null()) {
		++unreadable;
	}
	entries[key] = entry;
	return entry.texture;
}

Ref<Texture2D> FrameTextureCache2D::drawable(const Ref<Texture2D> &source, FrameImageReader2D &reader) {
	const Ref<AtlasTexture> atlas = source;
	if (atlas.is_null()) {
		return source; // draws itself: no entry, the texture's identity kept
	}
	return lookup(drawable_entries, source, false, reader);
}

Ref<Texture2D> FrameTextureCache2D::whitened(const Ref<Texture2D> &source, FrameImageReader2D &reader) {
	if (source.is_null()) {
		return Ref<Texture2D>();
	}
	return lookup(whitened_entries, source, true, reader);
}

void FrameTextureCache2D::clear() {
	drawable_entries.clear();
	whitened_entries.clear();
}

Dictionary FrameTextureCache2D::debug_stats() const {
	Dictionary d;
	d["entries"] = (int64_t)(drawable_entries.size() + whitened_entries.size());
	d["builds"] = (int64_t)builds;
	d["hits"] = (int64_t)hits;
	d["sheet_reads"] = (int64_t)sheet_reads;
	d["unreadable"] = (int64_t)unreadable;
	return d;
}

} // namespace BlastBullets2D
