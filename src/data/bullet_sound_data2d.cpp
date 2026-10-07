#include "data/bullet_sound_data2d.hpp"

#include "factory/sound_mixer2d.hpp"

#include <godot_cpp/classes/audio_server.hpp>
#include <godot_cpp/classes/audio_stream_player2d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/scene_tree_timer.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace BlastBullets2D {

bool BulletSoundData2D::get_enabled() const { return enabled; }
void BulletSoundData2D::set_enabled(bool value) {
	if (enabled == value) {
		return;
	}
	enabled = value;
	emit_changed();
}

int BulletSoundData2D::get_trigger() const { return trigger; }
void BulletSoundData2D::set_trigger(int value) {
	if (value < SOUND_ON_SHOT || value > SOUND_ON_FLIGHT) {
		UtilityFunctions::push_error("BulletSoundData2D: trigger must be 0 (On Shot), 1 (On Hit), 2 (On Destroy), 3 (On Bounce), 4 (On Lifetime Over), 5 (On Clear), 6 (On Homing Target Reached), 7 (On Graze), 8 (On Graze Exit), 9 (On Telegraph) or 10 (On Flight), keeping the old value.");
		return;
	}
	if (trigger == value) {
		return;
	}
	trigger = value;
	emit_changed();
}

Ref<AudioStream> BulletSoundData2D::get_stream() const { return stream; }
void BulletSoundData2D::set_stream(const Ref<AudioStream> &new_stream) {
	if (stream == new_stream) {
		return;
	}
	stream = new_stream;
	emit_changed();
}

int BulletSoundData2D::get_ring_index() const { return ring_index; }
void BulletSoundData2D::set_ring_index(int value) {
	if (value < -1 || value > 3) {
		UtilityFunctions::push_error("BulletSoundData2D: ring_index must be -1 (any ring) or 0..3, keeping the old value.");
		return;
	}
	if (ring_index == value) {
		return;
	}
	ring_index = value;
	emit_changed();
}

Array BulletSoundData2D::get_streams() const { return streams; }
void BulletSoundData2D::set_streams(const Array &value) {
	for (int i = 0; i < value.size(); ++i) {
		const Variant &entry = value[i];
		if (entry.get_type() == Variant::NIL) {
			continue;
		}
		if (Object::cast_to<AudioStream>(entry.get_type() == Variant::OBJECT ? (Object *)entry : nullptr) == nullptr) {
			UtilityFunctions::push_error("BulletSoundData2D: streams entries must be AudioStream or null, keeping the old value.");
			return;
		}
	}
	if (streams == value) {
		return;
	}
	streams = value;
	emit_changed();
}

int BulletSoundData2D::get_stream_mode() const { return stream_mode; }
void BulletSoundData2D::set_stream_mode(int value) {
	if (value < STREAM_RANDOM || value > STREAM_SHUFFLE) {
		UtilityFunctions::push_error("BulletSoundData2D: stream_mode must be 0 (Random), 1 (Weighted), 2 (Sequence) or 3 (Shuffle), keeping the old value.");
		return;
	}
	if (stream_mode == value) {
		return;
	}
	stream_mode = value;
	emit_changed();
}

PackedFloat32Array BulletSoundData2D::get_stream_weights() const { return stream_weights; }
void BulletSoundData2D::set_stream_weights(const PackedFloat32Array &value) {
	for (int i = 0; i < value.size(); ++i) {
		if (!Math::is_finite(value[i]) || value[i] < 0.0f) {
			UtilityFunctions::push_error("BulletSoundData2D: stream_weights must hold finite numbers >= 0 only, keeping the old value.");
			return;
		}
	}
	if (stream_weights == value) {
		return;
	}
	stream_weights = value;
	emit_changed();
}

double BulletSoundData2D::get_volume_db() const { return volume_db; }
void BulletSoundData2D::set_volume_db(double value) {
	if (!Math::is_finite(value)) {
		UtilityFunctions::push_error("BulletSoundData2D: volume_db must be finite, keeping the old value.");
		return;
	}
	if (volume_db == value) {
		return;
	}
	volume_db = value;
	emit_changed();
}

double BulletSoundData2D::get_pitch_scale() const { return pitch_scale; }
void BulletSoundData2D::set_pitch_scale(double value) {
	if (!Math::is_finite(value) || value < 0.01) {
		UtilityFunctions::push_error("BulletSoundData2D: pitch_scale must be finite and >= 0.01, keeping the old value.");
		return;
	}
	if (pitch_scale == value) {
		return;
	}
	pitch_scale = value;
	emit_changed();
}

double BulletSoundData2D::get_random_pitch() const { return random_pitch; }
void BulletSoundData2D::set_random_pitch(double value) {
	if (!Math::is_finite(value) || value < 1.0 || value > 16.0) {
		UtilityFunctions::push_error("BulletSoundData2D: random_pitch must be finite and between 1 and 16, keeping the old value.");
		return;
	}
	if (random_pitch == value) {
		return;
	}
	random_pitch = value;
	emit_changed();
}

double BulletSoundData2D::get_random_volume_offset_db() const { return random_volume_offset_db; }
void BulletSoundData2D::set_random_volume_offset_db(double value) {
	if (!Math::is_finite(value) || value < 0.0 || value > 40.0) {
		UtilityFunctions::push_error("BulletSoundData2D: random_volume_offset_db must be finite and between 0 and 40, keeping the old value.");
		return;
	}
	if (random_volume_offset_db == value) {
		return;
	}
	random_volume_offset_db = value;
	emit_changed();
}

double BulletSoundData2D::get_trigger_chance() const { return trigger_chance; }
void BulletSoundData2D::set_trigger_chance(double value) {
	if (!Math::is_finite(value) || value < 0.0 || value > 1.0) {
		UtilityFunctions::push_error("BulletSoundData2D: trigger_chance must be finite and between 0 and 1, keeping the old value.");
		return;
	}
	if (trigger_chance == value) {
		return;
	}
	trigger_chance = value;
	emit_changed();
}

StringName BulletSoundData2D::get_bus() const { return bus; }
void BulletSoundData2D::set_bus(const StringName &value) {
	if (String(value).is_empty()) {
		UtilityFunctions::push_error("BulletSoundData2D: bus must not be empty, keeping the old value.");
		return;
	}
	if (bus == value) {
		return;
	}
	bus = value;
	emit_changed();
}

bool BulletSoundData2D::get_positional() const { return positional; }
void BulletSoundData2D::set_positional(bool value) {
	if (positional == value) {
		return;
	}
	positional = value;
	notify_property_list_changed();
	emit_changed();
}

double BulletSoundData2D::get_max_distance() const { return max_distance; }
void BulletSoundData2D::set_max_distance(double value) {
	if (!Math::is_finite(value) || value < 1.0) {
		UtilityFunctions::push_error("BulletSoundData2D: max_distance must be finite and >= 1, keeping the old value.");
		return;
	}
	if (max_distance == value) {
		return;
	}
	max_distance = value;
	emit_changed();
}

double BulletSoundData2D::get_attenuation() const { return attenuation; }
void BulletSoundData2D::set_attenuation(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletSoundData2D: attenuation must be finite and >= 0, keeping the old value.");
		return;
	}
	if (attenuation == value) {
		return;
	}
	attenuation = value;
	emit_changed();
}

double BulletSoundData2D::get_panning_strength() const { return panning_strength; }
void BulletSoundData2D::set_panning_strength(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletSoundData2D: panning_strength must be finite and >= 0, keeping the old value.");
		return;
	}
	if (panning_strength == value) {
		return;
	}
	panning_strength = value;
	emit_changed();
}

int BulletSoundData2D::get_area_mask() const { return area_mask; }
void BulletSoundData2D::set_area_mask(int value) {
	if (value < 0) {
		UtilityFunctions::push_error("BulletSoundData2D: area_mask must be >= 0, keeping the old value.");
		return;
	}
	if (area_mask == value) {
		return;
	}
	area_mask = value;
	emit_changed();
}

double BulletSoundData2D::get_min_interval_sec() const { return min_interval_sec; }
void BulletSoundData2D::set_min_interval_sec(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletSoundData2D: min_interval_sec must be finite and >= 0, keeping the old value.");
		return;
	}
	if (min_interval_sec == value) {
		return;
	}
	min_interval_sec = value;
	emit_changed();
}

int BulletSoundData2D::get_max_voices() const { return max_voices; }
void BulletSoundData2D::set_max_voices(int value) {
	if (value < 1 || value > 32) {
		UtilityFunctions::push_error("BulletSoundData2D: max_voices must be between 1 and 32, keeping the old value.");
		return;
	}
	if (max_voices == value) {
		return;
	}
	max_voices = value;
	emit_changed();
}

int BulletSoundData2D::get_when_limit_reached() const { return when_limit_reached; }
void BulletSoundData2D::set_when_limit_reached(int value) {
	if (value != SOUND_LIMIT_REPLACE_OLDEST && value != SOUND_LIMIT_SKIP_NEW) {
		UtilityFunctions::push_error("BulletSoundData2D: when_limit_reached must be 0 (Replace Oldest) or 1 (Skip New), keeping the old value.");
		return;
	}
	if (when_limit_reached == value) {
		return;
	}
	when_limit_reached = value;
	emit_changed();
}

int BulletSoundData2D::get_priority() const { return priority; }
void BulletSoundData2D::set_priority(int value) {
	if (value < -16 || value > 16) {
		UtilityFunctions::push_error("BulletSoundData2D: priority must be between -16 and 16, keeping the old value.");
		return;
	}
	if (priority == value) {
		return;
	}
	priority = value;
	emit_changed();
}

int BulletSoundData2D::get_steal_mode() const { return steal_mode; }
void BulletSoundData2D::set_steal_mode(int value) {
	if (value != STEAL_OLDEST && value != STEAL_QUIETEST) {
		UtilityFunctions::push_error("BulletSoundData2D: steal_mode must be 0 (Oldest) or 1 (Quietest), keeping the old value.");
		return;
	}
	if (steal_mode == value) {
		return;
	}
	steal_mode = value;
	emit_changed();
}

double BulletSoundData2D::get_max_duration_sec() const { return max_duration_sec; }
void BulletSoundData2D::set_max_duration_sec(double value) {
	if (!Math::is_finite(value) || value < 0.0) {
		UtilityFunctions::push_error("BulletSoundData2D: max_duration_sec must be finite and >= 0, keeping the old value.");
		return;
	}
	if (max_duration_sec == value) {
		return;
	}
	max_duration_sec = value;
	emit_changed();
}

void BulletSoundData2D::preview() {
	if (Engine::get_singleton() == nullptr || !Engine::get_singleton()->is_editor_hint()) {
		UtilityFunctions::push_error("BulletSoundData2D.preview: editor only, nothing plays.");
		return;
	}
	preview_at(Vector2(), 0.0f, false);
}

void BulletSoundData2D::preview_at(const Vector2 &global_position, float volume_offset_db, bool use_position) {
	SoundMixer2D::preview_play(this, global_position, volume_offset_db, use_position);
}

AudioStreamPlayer2D *BulletSoundData2D::debug_get_preview_player() const {
	SceneTree *tree = SceneTree::get_singleton();
	if (tree == nullptr || tree->get_root() == nullptr) {
		return nullptr;
	}
	Node *node = tree->get_root()->get_node_or_null(NodePath("BlastSoundPreview_" + String::num_uint64(get_instance_id())));
	return Object::cast_to<AudioStreamPlayer2D>(node);
}

void BulletSoundData2D::_validate_property(PropertyInfo &p_property) const {
	const String name = p_property.name;
	if (name == "max_distance" || name == "attenuation" || name == "panning_strength" || name == "area_mask") {
		if (!positional) {
			p_property.usage &= ~PROPERTY_USAGE_EDITOR;
		}
		return;
	}
	if (name == "bus") {
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

void BulletSoundData2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_enabled"), &BulletSoundData2D::get_enabled);
	ClassDB::bind_method(D_METHOD("set_enabled", "value"), &BulletSoundData2D::set_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "get_enabled");

	ClassDB::bind_method(D_METHOD("get_trigger"), &BulletSoundData2D::get_trigger);
	ClassDB::bind_method(D_METHOD("set_trigger", "value"), &BulletSoundData2D::set_trigger);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "trigger", PROPERTY_HINT_ENUM, "On Shot,On Hit,On Destroy,On Bounce,On Lifetime Over,On Clear,On Homing Target Reached,On Graze,On Graze Exit,On Telegraph,On Flight"), "set_trigger", "get_trigger");

	ClassDB::bind_method(D_METHOD("get_ring_index"), &BulletSoundData2D::get_ring_index);
	ClassDB::bind_method(D_METHOD("set_ring_index", "value"), &BulletSoundData2D::set_ring_index);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ring_index", PROPERTY_HINT_RANGE, "-1,3,1"), "set_ring_index", "get_ring_index");

	ClassDB::bind_method(D_METHOD("get_stream"), &BulletSoundData2D::get_stream);
	ClassDB::bind_method(D_METHOD("set_stream", "stream"), &BulletSoundData2D::set_stream);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "stream", PROPERTY_HINT_RESOURCE_TYPE, "AudioStream"), "set_stream", "get_stream");

	ClassDB::bind_method(D_METHOD("get_streams"), &BulletSoundData2D::get_streams);
	ClassDB::bind_method(D_METHOD("set_streams", "streams"), &BulletSoundData2D::set_streams);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "streams", PROPERTY_HINT_ARRAY_TYPE, "AudioStream"), "set_streams", "get_streams");

	ClassDB::bind_method(D_METHOD("get_stream_mode"), &BulletSoundData2D::get_stream_mode);
	ClassDB::bind_method(D_METHOD("set_stream_mode", "value"), &BulletSoundData2D::set_stream_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "stream_mode", PROPERTY_HINT_ENUM, "Random,Weighted,Sequence,Shuffle"), "set_stream_mode", "get_stream_mode");

	ClassDB::bind_method(D_METHOD("get_stream_weights"), &BulletSoundData2D::get_stream_weights);
	ClassDB::bind_method(D_METHOD("set_stream_weights", "weights"), &BulletSoundData2D::set_stream_weights);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "stream_weights"), "set_stream_weights", "get_stream_weights");

	ADD_GROUP("Volume and Pitch", "");

	ClassDB::bind_method(D_METHOD("get_volume_db"), &BulletSoundData2D::get_volume_db);
	ClassDB::bind_method(D_METHOD("set_volume_db", "value"), &BulletSoundData2D::set_volume_db);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "volume_db", PROPERTY_HINT_RANGE, "-80,80,0.1,suffix:dB"), "set_volume_db", "get_volume_db");

	ClassDB::bind_method(D_METHOD("get_pitch_scale"), &BulletSoundData2D::get_pitch_scale);
	ClassDB::bind_method(D_METHOD("set_pitch_scale", "value"), &BulletSoundData2D::set_pitch_scale);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pitch_scale", PROPERTY_HINT_RANGE, "0.01,4,0.01,or_greater"), "set_pitch_scale", "get_pitch_scale");

	ClassDB::bind_method(D_METHOD("get_random_pitch"), &BulletSoundData2D::get_random_pitch);
	ClassDB::bind_method(D_METHOD("set_random_pitch", "value"), &BulletSoundData2D::set_random_pitch);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "random_pitch", PROPERTY_HINT_RANGE, "1,16,0.01"), "set_random_pitch", "get_random_pitch");

	ClassDB::bind_method(D_METHOD("get_random_volume_offset_db"), &BulletSoundData2D::get_random_volume_offset_db);
	ClassDB::bind_method(D_METHOD("set_random_volume_offset_db", "value"), &BulletSoundData2D::set_random_volume_offset_db);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "random_volume_offset_db", PROPERTY_HINT_RANGE, "0,40,0.01,suffix:dB"), "set_random_volume_offset_db", "get_random_volume_offset_db");

	ClassDB::bind_method(D_METHOD("get_trigger_chance"), &BulletSoundData2D::get_trigger_chance);
	ClassDB::bind_method(D_METHOD("set_trigger_chance", "value"), &BulletSoundData2D::set_trigger_chance);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "trigger_chance", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_trigger_chance", "get_trigger_chance");

	ClassDB::bind_method(D_METHOD("get_bus"), &BulletSoundData2D::get_bus);
	ClassDB::bind_method(D_METHOD("set_bus", "value"), &BulletSoundData2D::set_bus);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "bus"), "set_bus", "get_bus");

	ADD_GROUP("Spatial", "");

	ClassDB::bind_method(D_METHOD("get_positional"), &BulletSoundData2D::get_positional);
	ClassDB::bind_method(D_METHOD("set_positional", "value"), &BulletSoundData2D::set_positional);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "positional"), "set_positional", "get_positional");

	ClassDB::bind_method(D_METHOD("get_max_distance"), &BulletSoundData2D::get_max_distance);
	ClassDB::bind_method(D_METHOD("set_max_distance", "value"), &BulletSoundData2D::set_max_distance);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_distance", PROPERTY_HINT_RANGE, "1,4096,1,or_greater,exp,suffix:px"), "set_max_distance", "get_max_distance");

	ClassDB::bind_method(D_METHOD("get_attenuation"), &BulletSoundData2D::get_attenuation);
	ClassDB::bind_method(D_METHOD("set_attenuation", "value"), &BulletSoundData2D::set_attenuation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "attenuation", PROPERTY_HINT_EXP_EASING, "attenuation"), "set_attenuation", "get_attenuation");

	ClassDB::bind_method(D_METHOD("get_panning_strength"), &BulletSoundData2D::get_panning_strength);
	ClassDB::bind_method(D_METHOD("set_panning_strength", "value"), &BulletSoundData2D::set_panning_strength);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "panning_strength", PROPERTY_HINT_RANGE, "0,3,0.01,or_greater"), "set_panning_strength", "get_panning_strength");

	ClassDB::bind_method(D_METHOD("get_area_mask"), &BulletSoundData2D::get_area_mask);
	ClassDB::bind_method(D_METHOD("set_area_mask", "value"), &BulletSoundData2D::set_area_mask);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "area_mask", PROPERTY_HINT_LAYERS_2D_PHYSICS), "set_area_mask", "get_area_mask");

	ADD_GROUP("Limits", "");

	ClassDB::bind_method(D_METHOD("get_min_interval_sec"), &BulletSoundData2D::get_min_interval_sec);
	ClassDB::bind_method(D_METHOD("set_min_interval_sec", "value"), &BulletSoundData2D::set_min_interval_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_interval_sec", PROPERTY_HINT_RANGE, "0,1,0.001,or_greater,suffix:s"), "set_min_interval_sec", "get_min_interval_sec");

	ClassDB::bind_method(D_METHOD("get_max_voices"), &BulletSoundData2D::get_max_voices);
	ClassDB::bind_method(D_METHOD("set_max_voices", "value"), &BulletSoundData2D::set_max_voices);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_voices", PROPERTY_HINT_RANGE, "1,32,1,or_greater"), "set_max_voices", "get_max_voices");

	ClassDB::bind_method(D_METHOD("get_when_limit_reached"), &BulletSoundData2D::get_when_limit_reached);
	ClassDB::bind_method(D_METHOD("set_when_limit_reached", "value"), &BulletSoundData2D::set_when_limit_reached);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "when_limit_reached", PROPERTY_HINT_ENUM, "Replace Oldest,Skip New"), "set_when_limit_reached", "get_when_limit_reached");

	ClassDB::bind_method(D_METHOD("get_steal_mode"), &BulletSoundData2D::get_steal_mode);
	ClassDB::bind_method(D_METHOD("set_steal_mode", "value"), &BulletSoundData2D::set_steal_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "steal_mode", PROPERTY_HINT_ENUM, "Oldest,Quietest"), "set_steal_mode", "get_steal_mode");

	ClassDB::bind_method(D_METHOD("get_priority"), &BulletSoundData2D::get_priority);
	ClassDB::bind_method(D_METHOD("set_priority", "value"), &BulletSoundData2D::set_priority);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "priority", PROPERTY_HINT_RANGE, "-16,16,1,or_greater,or_less"), "set_priority", "get_priority");

	ClassDB::bind_method(D_METHOD("get_max_duration_sec"), &BulletSoundData2D::get_max_duration_sec);
	ClassDB::bind_method(D_METHOD("set_max_duration_sec", "value"), &BulletSoundData2D::set_max_duration_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_duration_sec", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s"), "set_max_duration_sec", "get_max_duration_sec");

	ClassDB::bind_method(D_METHOD("preview"), &BulletSoundData2D::preview);
	ClassDB::bind_method(D_METHOD("preview_at", "global_position", "volume_offset_db", "use_position"), &BulletSoundData2D::preview_at);
	ClassDB::bind_method(D_METHOD("debug_get_preview_player"), &BulletSoundData2D::debug_get_preview_player);

	BIND_ENUM_CONSTANT(SOUND_ON_SHOT);
	BIND_ENUM_CONSTANT(SOUND_ON_HIT);
	BIND_ENUM_CONSTANT(SOUND_ON_DESTROY);
	BIND_ENUM_CONSTANT(SOUND_ON_BOUNCE);
	BIND_ENUM_CONSTANT(SOUND_ON_LIFETIME_OVER);
	BIND_ENUM_CONSTANT(SOUND_ON_CLEAR);
	BIND_ENUM_CONSTANT(SOUND_ON_HOMING_TARGET_REACHED);
	BIND_ENUM_CONSTANT(SOUND_ON_GRAZE);
	BIND_ENUM_CONSTANT(SOUND_ON_GRAZE_EXIT);
	BIND_ENUM_CONSTANT(SOUND_ON_TELEGRAPH);
	BIND_ENUM_CONSTANT(SOUND_ON_FLIGHT);
	BIND_ENUM_CONSTANT(SOUND_LIMIT_REPLACE_OLDEST);
	BIND_ENUM_CONSTANT(SOUND_LIMIT_SKIP_NEW);
	BIND_ENUM_CONSTANT(STEAL_OLDEST);
	BIND_ENUM_CONSTANT(STEAL_QUIETEST);
	BIND_ENUM_CONSTANT(STREAM_RANDOM);
	BIND_ENUM_CONSTANT(STREAM_WEIGHTED);
	BIND_ENUM_CONSTANT(STREAM_SEQUENCE);
	BIND_ENUM_CONSTANT(STREAM_SHUFFLE);
}
} //namespace BlastBullets2D
