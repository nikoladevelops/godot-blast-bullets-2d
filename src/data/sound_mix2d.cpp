#include "data/sound_mix2d.hpp"

#include "core/live_object2d.hpp"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/audio_stream.hpp>

namespace BlastBullets2D {

void validate_sound_mix_property2d(const SoundMix2D &mix, PropertyInfo &p_property) {
	const String name = p_property.name;
	if (name == "stream_weights") {
		// Weights only mean something for Weighted picks from a playable list.
		if (mix.stream_mode != SoundMix2D::STREAM_WEIGHTED) {
			p_property.usage &= ~PROPERTY_USAGE_EDITOR;
			return;
		}
		for (int i = 0; i < mix.streams.size(); ++i) {
			const Variant entry = mix.streams[i];
			if (live_object2d<AudioStream>(entry) != nullptr) {
				return;
			}
		}
		p_property.usage &= ~PROPERTY_USAGE_EDITOR;
		return;
	}
	if (name == "occlusion_db" || name == "occlusion_bus") {
		// Both only mean something while the raycast can run.
		if (mix.occlusion_mask == 0) {
			p_property.usage &= ~PROPERTY_USAGE_EDITOR;
		}
		if (name == "occlusion_db") {
			return;
		}
		// occlusion_bus falls through to the live bus list below (a hidden
		// property still carries its hint).
	}
	if (name == "camera_path" || name == "camera_margin_px") {
		// Both only mean something when the camera takes part in audibility.
		if (mix.audibility_mode != SoundMix2D::SOUND_AUDIBILITY_CAMERA && mix.audibility_mode != SoundMix2D::SOUND_AUDIBILITY_DISTANCE_AND_CAMERA) {
			p_property.usage &= ~PROPERTY_USAGE_EDITOR;
		}
		return;
	}
	if (name == "max_distance" || name == "attenuation" || name == "panning_strength" || name == "area_mask") {
		if (!mix.positional) {
			p_property.usage &= ~PROPERTY_USAGE_EDITOR;
		}
		return;
	}
	if (name == "bus" || name == "occlusion_bus") {
		AudioServer *server = AudioServer::get_singleton();
		if (server == nullptr) {
			return;
		}
		String buses;
		const int count = server->get_bus_count();
		for (int i = 0; i < count; ++i) {
			if (i > 0) {
				buses += ",";
			}
			buses += server->get_bus_name(i);
		}
		if (!buses.is_empty()) {
			p_property.hint = PROPERTY_HINT_ENUM;
			p_property.hint_string = buses;
		}
	}
}

} // namespace BlastBullets2D
