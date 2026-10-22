#include "sprite/frame_image2d.hpp"

#include <godot_cpp/classes/atlas_texture.hpp>
#include <godot_cpp/core/math.hpp>

#include <cstring>

using namespace godot;

namespace BlastBullets2D {

Ref<Image> FrameImageReader2D::read(const Ref<Texture2D> &tex) {
	return read_at_depth(tex, 0);
}

Ref<Image> FrameImageReader2D::read_at_depth(const Ref<Texture2D> &tex, int depth) {
	if (tex.is_null() || depth > MAX_DEPTH) {
		return Ref<Image>();
	}
	const uint64_t id = (uint64_t)tex->get_instance_id();
	auto it = drawn.find(id);
	if (it != drawn.end()) {
		return it->second;
	}
	const Ref<AtlasTexture> atlas = tex;
	const Ref<Image> out = atlas.is_valid() ? read_atlas(tex, depth) : decode(tex);
	drawn[id] = out;
	return out;
}

Ref<Image> FrameImageReader2D::decode(const Ref<Texture2D> &tex) {
	Ref<Image> out;
	const Ref<Image> source = tex->get_image();
	++sheet_reads;
	if (source.is_null() || source->is_empty()) {
		return out;
	}
	// A copy: the texture may hand out its own stored image.
	out = source->duplicate();
	if (out.is_null()) {
		return out;
	}
	if (out->is_compressed() && (out->decompress() != OK || out->is_compressed())) {
		return Ref<Image>(); // no decoder for this format
	}
	if (out->get_format() != Image::FORMAT_RGBA8) {
		out->convert(Image::FORMAT_RGBA8);
	}
	return out;
}

Ref<Image> FrameImageReader2D::read_atlas(const Ref<Texture2D> &tex, int depth) {
	const Ref<AtlasTexture> atlas = tex;
	const Ref<Texture2D> base = atlas->get_atlas();
	if (base.is_null()) {
		return Ref<Image>();
	}
	// Rounded in double BEFORE any int cast: a huge or non-finite rect
	// overflows the cast or x + w (UB; it once slipped past an int check).
	// NaN fails every comparison below.
	const Rect2 region = atlas->get_region();
	const Rect2 margin = atlas->get_margin();
	const double rx = Math::round((double)region.position.x);
	const double ry = Math::round((double)region.position.y);
	const double mx = Math::round((double)margin.position.x);
	const double my = Math::round((double)margin.position.y);
	const double mw = Math::round((double)margin.size.x);
	const double mh = Math::round((double)margin.size.y);
	const double side = (double)MAX_SIDE;
	if (!Math::is_finite(rx) || !Math::is_finite(ry) || !Math::is_finite(region.size.x) || !Math::is_finite(region.size.y) ||
			!(Math::abs(mx) <= side) || !(Math::abs(my) <= side) || !(Math::abs(mw) <= side) || !(Math::abs(mh) <= side)) {
		return Ref<Image>();
	}
	const Ref<Image> sheet = read_at_depth(base, depth + 1);
	if (sheet.is_null() || sheet->is_empty()) {
		return Ref<Image>();
	}
	const double sheet_w = (double)sheet->get_width();
	const double sheet_h = (double)sheet->get_height();
	// Godot reads a zero region width or height as the atlas's whole one.
	const double rw = region.size.x == 0.0f ? sheet_w : Math::round((double)region.size.x);
	const double rh = region.size.y == 0.0f ? sheet_h : Math::round((double)region.size.y);
	if (!(rw > 0.0) || !(rh > 0.0) || !(rx >= 0.0) || !(ry >= 0.0) || !(rx + rw <= sheet_w) || !(ry + rh <= sheet_h)) {
		return Ref<Image>();
	}
	const double box_w = rw + mw;
	const double box_h = rh + mh;
	if (!(box_w >= 1.0) || !(box_h >= 1.0) || !(box_w <= side) || !(box_h <= side)) {
		return Ref<Image>();
	}
	Ref<Image> out = Image::create_empty((int)box_w, (int)box_h, false, Image::FORMAT_RGBA8);
	if (out.is_null()) {
		return out;
	}
	// The region at the margin's offset; blit_rect cuts it at the box.
	out->blit_rect(sheet, Rect2i((int)rx, (int)ry, (int)rw, (int)rh), Vector2i((int)mx, (int)my));
	if (sheet->has_mipmaps()) {
		out->generate_mipmaps();
	}
	return out;
}

static uint64_t signature_mix2d(uint64_t h, uint64_t v) {
	h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
	return h;
}

static uint64_t signature_mix_rect2d(uint64_t h, const Rect2 &r) {
	const real_t parts[4] = { r.position.x, r.position.y, r.size.x, r.size.y };
	for (real_t part : parts) {
		uint64_t bits = 0;
		std::memcpy(&bits, &part, sizeof(part));
		h = signature_mix2d(h, bits);
	}
	return h;
}

uint64_t frame_signature2d(const Ref<Texture2D> &tex) {
	uint64_t h = 0x84222325CBF29CE4ull;
	Ref<Texture2D> link = tex;
	for (int depth = 0; depth <= FrameImageReader2D::MAX_DEPTH && link.is_valid(); ++depth) {
		h = signature_mix2d(h, (uint64_t)link->get_instance_id());
		const Ref<AtlasTexture> atlas = link;
		if (atlas.is_null()) {
			h = signature_mix2d(h, ((uint64_t)(uint32_t)link->get_width() << 32) | (uint32_t)link->get_height());
			break;
		}
		h = signature_mix_rect2d(signature_mix_rect2d(h, atlas->get_region()), atlas->get_margin());
		link = atlas->get_atlas();
	}
	return h;
}

} // namespace BlastBullets2D
