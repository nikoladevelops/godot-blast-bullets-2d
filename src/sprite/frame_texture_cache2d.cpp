#include "sprite/frame_texture_cache2d.hpp"

#include "data/bullet_effect_layer_data2d.hpp"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>

using namespace godot;

namespace BlastBullets2D {

Ref<Texture2D> FrameTextureCache2D::build_whitened(const Ref<Texture2D> &source) {
	Ref<Texture2D> result;
	const Ref<Image> white = BulletEffectLayerData2D::whiten_image_copy(BulletEffectLayerData2D::read_frame_image(source));
	if (white.is_valid()) {
		Ref<ImageTexture> white_tex;
		white_tex.instantiate();
		white_tex->set_image(white);
		result = white_tex;
	}
	return result;
}

Ref<Texture2D> FrameTextureCache2D::whitened(const Ref<Texture2D> &source) {
	if (source.is_null()) {
		return Ref<Texture2D>();
	}
	const uint64_t key = (uint64_t)source->get_instance_id();
	auto it = whitened_entries.find(key);
	if (it != whitened_entries.end()) {
		++hits;
		return it->second;
	}
	if (whitened_entries.size() >= MAX_ENTRIES) {
		whitened_entries.clear();
	}
	const Ref<Texture2D> result = build_whitened(source);
	++builds;
	if (result.is_null()) {
		++unreadable;
	}
	whitened_entries[key] = result;
	return result;
}

void FrameTextureCache2D::clear() {
	whitened_entries.clear();
}

Dictionary FrameTextureCache2D::debug_stats() const {
	Dictionary d;
	d["entries"] = (int64_t)whitened_entries.size();
	d["builds"] = (int64_t)builds;
	d["hits"] = (int64_t)hits;
	d["unreadable"] = (int64_t)unreadable;
	return d;
}

} // namespace BlastBullets2D
