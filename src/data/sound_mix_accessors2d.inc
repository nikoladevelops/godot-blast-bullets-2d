// The accessors of every SoundMix2D setting, written once for each sound
// resource: included by bullet_sound_data2d.cpp and laser_sound_data2d.cpp
// with SOUND_DATA_CLASS (the class) and SOUND_DATA_NAME (its name, the
// prefix of every error) defined. Every accepted edit emits `changed` once.
// Not formatted (included into a function-level context).

bool SOUND_DATA_CLASS::get_enabled() const { return enabled; }

void SOUND_DATA_CLASS::set_enabled(bool value) {
	if (enabled == value) {
		return;
	}
	enabled = value;
	++mask_epoch;
	emit_changed();
}

Array SOUND_DATA_CLASS::get_streams() const { return streams; }

void SOUND_DATA_CLASS::set_streams(const Array &value) {
	for (int i = 0; i < value.size(); ++i) {
		const Variant &entry = value[i];
		if (entry.get_type() == Variant::NIL) {
			continue;
		}
		if (live_object2d<AudioStream>(entry) == nullptr) {
			UtilityFunctions::push_error(SOUND_DATA_NAME ": streams entries must be AudioStream or null, keeping the old value.");
			return;
		}
	}
	if (streams == value) {
		return;
	}
	streams = value;
	notify_property_list_changed(); // stream_weights gates on the list (_validate_property)
	emit_changed();
}

SOUND_DATA_CLASS::StreamMode SOUND_DATA_CLASS::get_stream_mode() const { return static_cast<StreamMode>(stream_mode); }

void SOUND_DATA_CLASS::set_stream_mode(StreamMode value) {
	if (value < STREAM_RANDOM || value > STREAM_SHUFFLE) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": stream_mode must be 0 (Random), 1 (Weighted), 2 (Sequence) or 3 (Shuffle), keeping the old value.");
		return;
	}
	if (stream_mode == value) {
		return;
	}
	stream_mode = value;
	notify_property_list_changed(); // stream_weights gates on it (_validate_property)
	emit_changed();
}

PackedFloat32Array SOUND_DATA_CLASS::get_stream_weights() const { return stream_weights; }

void SOUND_DATA_CLASS::set_stream_weights(const PackedFloat32Array &value) {
	for (int i = 0; i < value.size(); ++i) {
		if (!Math::is_finite(value[i]) || value[i] < 0.0f) {
			UtilityFunctions::push_error(SOUND_DATA_NAME ": stream_weights must hold finite numbers >= 0 only, keeping the old value.");
			return;
		}
	}
	if (stream_weights == value) {
		return;
	}
	if (!assign_if_changed2d(stream_weights, value)) {
		return;
	}
	emit_changed();
}

double SOUND_DATA_CLASS::get_volume_db() const { return volume_db; }

void SOUND_DATA_CLASS::set_volume_db(double value) {
	// The player turns dB into a linear gain: 1e30 dB is an infinite gain
	// and a huge pair (base + trim) sums to NaN.
	if (!is_finite_real2d(value) || value < -80.0 || value > 80.0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": volume_db must be finite and between -80 and 80, keeping the old value.");
		return;
	}
	if (volume_db == value) {
		return;
	}
	if (!assign_if_changed2d(volume_db, value)) {
		return;
	}
	emit_changed();
}

double SOUND_DATA_CLASS::get_pitch_scale() const { return pitch_scale; }

void SOUND_DATA_CLASS::set_pitch_scale(double value) {
	if (!is_finite_real2d(value) || value < 0.01 || value > SOUND_PITCH_CEILING) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": pitch_scale must be finite and between 0.01 and 1000, keeping the old value.");
		return;
	}
	if (pitch_scale == value) {
		return;
	}
	if (!assign_if_changed2d(pitch_scale, value)) {
		return;
	}
	emit_changed();
}

double SOUND_DATA_CLASS::get_random_pitch() const { return random_pitch; }

void SOUND_DATA_CLASS::set_random_pitch(double value) {
	if (!is_finite_real2d(value) || value < 1.0 || value > 16.0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": random_pitch must be finite and between 1 and 16, keeping the old value.");
		return;
	}
	if (random_pitch == value) {
		return;
	}
	if (!assign_if_changed2d(random_pitch, value)) {
		return;
	}
	emit_changed();
}

double SOUND_DATA_CLASS::get_random_volume_offset_db() const { return random_volume_offset_db; }

void SOUND_DATA_CLASS::set_random_volume_offset_db(double value) {
	if (!is_finite_real2d(value) || value < 0.0 || value > 40.0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": random_volume_offset_db must be finite and between 0 and 40, keeping the old value.");
		return;
	}
	if (random_volume_offset_db == value) {
		return;
	}
	if (!assign_if_changed2d(random_volume_offset_db, value)) {
		return;
	}
	emit_changed();
}

double SOUND_DATA_CLASS::get_random_volume_min_db() const { return random_volume_min_db; }

void SOUND_DATA_CLASS::set_random_volume_min_db(double value) {
	if (!is_finite_real2d(value) || value < -40.0 || value > 40.0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": random_volume_min_db must be finite and between -40 and 40, keeping the old value.");
		return;
	}
	if (random_volume_min_db == value) {
		return;
	}
	if (!assign_if_changed2d(random_volume_min_db, value)) {
		return;
	}
	emit_changed();
}

double SOUND_DATA_CLASS::get_random_volume_max_db() const { return random_volume_max_db; }

void SOUND_DATA_CLASS::set_random_volume_max_db(double value) {
	if (!is_finite_real2d(value) || value < -40.0 || value > 40.0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": random_volume_max_db must be finite and between -40 and 40, keeping the old value.");
		return;
	}
	if (random_volume_max_db == value) {
		return;
	}
	if (!assign_if_changed2d(random_volume_max_db, value)) {
		return;
	}
	emit_changed();
}

double SOUND_DATA_CLASS::get_random_pitch_min() const { return random_pitch_min; }

void SOUND_DATA_CLASS::set_random_pitch_min(double value) {
	if (!is_finite_real2d(value) || value < 0.01 || value > 16.0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": random_pitch_min must be finite and between 0.01 and 16, keeping the old value.");
		return;
	}
	if (random_pitch_min == value) {
		return;
	}
	if (!assign_if_changed2d(random_pitch_min, value)) {
		return;
	}
	emit_changed();
}

double SOUND_DATA_CLASS::get_random_pitch_max() const { return random_pitch_max; }

void SOUND_DATA_CLASS::set_random_pitch_max(double value) {
	if (!is_finite_real2d(value) || value < 0.01 || value > 16.0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": random_pitch_max must be finite and between 0.01 and 16, keeping the old value.");
		return;
	}
	if (random_pitch_max == value) {
		return;
	}
	if (!assign_if_changed2d(random_pitch_max, value)) {
		return;
	}
	emit_changed();
}

double SOUND_DATA_CLASS::get_random_pan() const { return random_pan; }

void SOUND_DATA_CLASS::set_random_pan(double value) {
	if (!is_finite_real2d(value) || value < 0.0 || value > 1.0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": random_pan must be finite and between 0 and 1, keeping the old value.");
		return;
	}
	if (random_pan == value) {
		return;
	}
	if (!assign_if_changed2d(random_pan, value)) {
		return;
	}
	emit_changed();
}

double SOUND_DATA_CLASS::get_trigger_chance() const { return trigger_chance; }

void SOUND_DATA_CLASS::set_trigger_chance(double value) {
	if (!is_finite_real2d(value) || value < 0.0 || value > 1.0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": trigger_chance must be finite and between 0 and 1, keeping the old value.");
		return;
	}
	if (trigger_chance == value) {
		return;
	}
	if (!assign_if_changed2d(trigger_chance, value)) {
		return;
	}
	emit_changed();
}

StringName SOUND_DATA_CLASS::get_bus() const { return bus; }

void SOUND_DATA_CLASS::set_bus(const StringName &value) {
	if (String(value).is_empty()) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": bus must not be empty, keeping the old value.");
		return;
	}
	if (bus == value) {
		return;
	}
	if (!assign_if_changed2d(bus, value)) {
		return;
	}
	emit_changed();
}

bool SOUND_DATA_CLASS::get_positional() const { return positional; }

void SOUND_DATA_CLASS::set_positional(bool value) {
	if (positional == value) {
		return;
	}
	positional = value;
	notify_property_list_changed();
	emit_changed();
}

double SOUND_DATA_CLASS::get_max_distance() const { return max_distance; }

void SOUND_DATA_CLASS::set_max_distance(double value) {
	if (!is_finite_float2d(value) || value < 1.0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": max_distance must be finite and >= 1, keeping the old value.");
		return;
	}
	if (max_distance == value) {
		return;
	}
	if (!assign_if_changed2d(max_distance, value)) {
		return;
	}
	emit_changed();
}

double SOUND_DATA_CLASS::get_attenuation() const { return attenuation; }

void SOUND_DATA_CLASS::set_attenuation(double value) {
	if (!is_finite_float2d(value) || value < 0.0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": attenuation must be finite and >= 0, keeping the old value.");
		return;
	}
	if (attenuation == value) {
		return;
	}
	if (!assign_if_changed2d(attenuation, value)) {
		return;
	}
	emit_changed();
}

double SOUND_DATA_CLASS::get_panning_strength() const { return panning_strength; }

void SOUND_DATA_CLASS::set_panning_strength(double value) {
	if (!is_finite_float2d(value) || value < 0.0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": panning_strength must be finite and >= 0, keeping the old value.");
		return;
	}
	if (panning_strength == value) {
		return;
	}
	if (!assign_if_changed2d(panning_strength, value)) {
		return;
	}
	emit_changed();
}

int SOUND_DATA_CLASS::get_area_mask() const { return area_mask; }

void SOUND_DATA_CLASS::set_area_mask(int value) {
	if (value < 0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": area_mask must be >= 0, keeping the old value.");
		return;
	}
	if (area_mask == value) {
		return;
	}
	if (!assign_if_changed2d(area_mask, value)) {
		return;
	}
	emit_changed();
}

SOUND_DATA_CLASS::AudibilityMode SOUND_DATA_CLASS::get_audibility_mode() const { return static_cast<AudibilityMode>(audibility_mode); }

void SOUND_DATA_CLASS::set_audibility_mode(AudibilityMode value) {
	if (value < SOUND_AUDIBILITY_DISTANCE || value > SOUND_AUDIBILITY_DISTANCE_AND_CAMERA) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": audibility_mode must be 0 (Distance), 1 (Camera) or 2 (Distance and Camera), keeping the old value.");
		return;
	}
	if (audibility_mode == value) {
		return;
	}
	audibility_mode = value;
	notify_property_list_changed(); // camera_path/margin gate on it (_validate_property)
	emit_changed();
}

NodePath SOUND_DATA_CLASS::get_camera_path() const { return camera_path; }

void SOUND_DATA_CLASS::set_camera_path(const NodePath &value) {
	// Any path stores (empty = viewport camera): the type/viewport checks
	// need scene context, so they run at resolve time with a fallback.
	if (camera_path == value) {
		return;
	}
	if (!assign_if_changed2d(camera_path, value)) {
		return;
	}
	emit_changed();
}

double SOUND_DATA_CLASS::get_camera_margin_px() const { return camera_margin_px; }

void SOUND_DATA_CLASS::set_camera_margin_px(double value) {
	if (!is_finite_real2d(value) || value < 0.0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": camera_margin_px must be finite and >= 0, keeping the old value.");
		return;
	}
	if (camera_margin_px == value) {
		return;
	}
	if (!assign_if_changed2d(camera_margin_px, value)) {
		return;
	}
	emit_changed();
}

bool SOUND_DATA_CLASS::get_zoom_scales_distance() const { return zoom_scales_distance; }

void SOUND_DATA_CLASS::set_zoom_scales_distance(bool value) {
	if (zoom_scales_distance == value) {
		return;
	}
	if (!assign_if_changed2d(zoom_scales_distance, value)) {
		return;
	}
	emit_changed();
}

double SOUND_DATA_CLASS::get_zoom_gain_db() const { return zoom_gain_db; }

void SOUND_DATA_CLASS::set_zoom_gain_db(double value) {
	if (!is_finite_real2d(value) || value < -24.0 || value > 24.0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": zoom_gain_db must be finite and between -24 and 24, keeping the old value.");
		return;
	}
	if (zoom_gain_db == value) {
		return;
	}
	if (!assign_if_changed2d(zoom_gain_db, value)) {
		return;
	}
	emit_changed();
}

int SOUND_DATA_CLASS::get_occlusion_mask() const { return occlusion_mask; }

void SOUND_DATA_CLASS::set_occlusion_mask(int value) {
	if (value < 0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": occlusion_mask must be >= 0 (0 disables occlusion), keeping the old value.");
		return;
	}
	if (occlusion_mask == value) {
		return;
	}
	occlusion_mask = value;
	notify_property_list_changed(); // occlusion_db/bus gate on it (_validate_property)
	emit_changed();
}

double SOUND_DATA_CLASS::get_occlusion_db() const { return occlusion_db; }

void SOUND_DATA_CLASS::set_occlusion_db(double value) {
	if (!is_finite_float2d(value) || value < 0.0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": occlusion_db must be finite and >= 0, keeping the old value.");
		return;
	}
	if (occlusion_db == value) {
		return;
	}
	if (!assign_if_changed2d(occlusion_db, value)) {
		return;
	}
	emit_changed();
}

StringName SOUND_DATA_CLASS::get_occlusion_bus() const { return occlusion_bus; }

void SOUND_DATA_CLASS::set_occlusion_bus(const StringName &value) {
	// Any name stores (empty disables the switch): validity resolves at
	// play time through the bus fallback, like bus itself.
	if (occlusion_bus == value) {
		return;
	}
	if (!assign_if_changed2d(occlusion_bus, value)) {
		return;
	}
	emit_changed();
}

double SOUND_DATA_CLASS::get_min_interval_sec() const { return min_interval_sec; }

void SOUND_DATA_CLASS::set_min_interval_sec(double value) {
	if (!is_finite_real2d(value) || value < 0.0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": min_interval_sec must be finite and >= 0, keeping the old value.");
		return;
	}
	if (min_interval_sec == value) {
		return;
	}
	if (!assign_if_changed2d(min_interval_sec, value)) {
		return;
	}
	emit_changed();
}

int SOUND_DATA_CLASS::get_max_voices() const { return max_voices; }

void SOUND_DATA_CLASS::set_max_voices(int value) {
	if (value < 1 || value > 32) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": max_voices must be between 1 and 32, keeping the old value.");
		return;
	}
	if (max_voices == value) {
		return;
	}
	if (!assign_if_changed2d(max_voices, value)) {
		return;
	}
	emit_changed();
}

SOUND_DATA_CLASS::SoundLimitMode SOUND_DATA_CLASS::get_when_limit_reached() const { return static_cast<SoundLimitMode>(when_limit_reached); }

void SOUND_DATA_CLASS::set_when_limit_reached(SoundLimitMode value) {
	if (value != SOUND_LIMIT_REPLACE_OLDEST && value != SOUND_LIMIT_SKIP_NEW) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": when_limit_reached must be 0 (Replace Oldest) or 1 (Skip New), keeping the old value.");
		return;
	}
	if (when_limit_reached == value) {
		return;
	}
	if (!assign_if_changed2d(when_limit_reached, value)) {
		return;
	}
	emit_changed();
}

int SOUND_DATA_CLASS::get_priority() const { return priority; }

void SOUND_DATA_CLASS::set_priority(int value) {
	if (value < -16 || value > 16) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": priority must be between -16 and 16, keeping the old value.");
		return;
	}
	if (priority == value) {
		return;
	}
	if (!assign_if_changed2d(priority, value)) {
		return;
	}
	emit_changed();
}

SOUND_DATA_CLASS::StealMode SOUND_DATA_CLASS::get_steal_mode() const { return static_cast<StealMode>(steal_mode); }

void SOUND_DATA_CLASS::set_steal_mode(StealMode value) {
	if (value != STEAL_OLDEST && value != STEAL_QUIETEST) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": steal_mode must be 0 (Oldest) or 1 (Quietest), keeping the old value.");
		return;
	}
	if (steal_mode == value) {
		return;
	}
	if (!assign_if_changed2d(steal_mode, value)) {
		return;
	}
	emit_changed();
}

double SOUND_DATA_CLASS::get_max_duration_sec() const { return max_duration_sec; }

void SOUND_DATA_CLASS::set_max_duration_sec(double value) {
	if (!is_finite_real2d(value) || value < 0.0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": max_duration_sec must be finite and >= 0, keeping the old value.");
		return;
	}
	if (max_duration_sec == value) {
		return;
	}
	if (!assign_if_changed2d(max_duration_sec, value)) {
		return;
	}
	emit_changed();
}

double SOUND_DATA_CLASS::get_volume_min_db() const { return volume_min_db; }

void SOUND_DATA_CLASS::set_volume_min_db(double value) {
	// The player turns dB into a linear gain: 1e30 dB is an infinite gain
	// and a huge pair (base + trim) sums to NaN.
	if (!is_finite_real2d(value) || value < -80.0 || value > 80.0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": volume_min_db must be finite and between -80 and 80, keeping the old value.");
		return;
	}
	if (volume_min_db == value) {
		return;
	}
	if (!assign_if_changed2d(volume_min_db, value)) {
		return;
	}
	emit_changed();
}

double SOUND_DATA_CLASS::get_volume_max_db() const { return volume_max_db; }

void SOUND_DATA_CLASS::set_volume_max_db(double value) {
	// The player turns dB into a linear gain: 1e30 dB is an infinite gain
	// and a huge pair (base + trim) sums to NaN.
	if (!is_finite_real2d(value) || value < -80.0 || value > 80.0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": volume_max_db must be finite and between -80 and 80, keeping the old value.");
		return;
	}
	if (volume_max_db == value) {
		return;
	}
	if (!assign_if_changed2d(volume_max_db, value)) {
		return;
	}
	emit_changed();
}

double SOUND_DATA_CLASS::get_pitch_min() const { return pitch_min; }

void SOUND_DATA_CLASS::set_pitch_min(double value) {
	if (!is_finite_real2d(value) || value < 0.01 || value > SOUND_PITCH_CEILING) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": pitch_min must be finite and between 0.01 and 1000, keeping the old value.");
		return;
	}
	if (pitch_min == value) {
		return;
	}
	if (!assign_if_changed2d(pitch_min, value)) {
		return;
	}
	emit_changed();
}

double SOUND_DATA_CLASS::get_pitch_max() const { return pitch_max; }

void SOUND_DATA_CLASS::set_pitch_max(double value) {
	if (!is_finite_real2d(value) || value < 0.01 || value > SOUND_PITCH_CEILING) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": pitch_max must be finite and between 0.01 and 1000, keeping the old value.");
		return;
	}
	if (pitch_max == value) {
		return;
	}
	if (!assign_if_changed2d(pitch_max, value)) {
		return;
	}
	emit_changed();
}

double SOUND_DATA_CLASS::get_fade_out_sec() const { return fade_out_sec; }

void SOUND_DATA_CLASS::set_fade_out_sec(double value) {
	if (!is_finite_real2d(value) || value < 0.0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": fade_out_sec must be finite and >= 0, keeping the old value.");
		return;
	}
	if (fade_out_sec == value) {
		return;
	}
	if (!assign_if_changed2d(fade_out_sec, value)) {
		return;
	}
	emit_changed();
}

double SOUND_DATA_CLASS::get_duck_amount_db() const { return duck_amount_db; }

void SOUND_DATA_CLASS::set_duck_amount_db(double value) {
	if (!is_finite_float2d(value) || value < 0.0) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ": duck_amount_db must be finite and >= 0, keeping the old value.");
		return;
	}
	if (duck_amount_db == value) {
		return;
	}
	if (!assign_if_changed2d(duck_amount_db, value)) {
		return;
	}
	emit_changed();
}

bool SOUND_DATA_CLASS::get_pitch_follows_time_scale() const { return pitch_follows_time_scale; }

void SOUND_DATA_CLASS::set_pitch_follows_time_scale(bool value) {
	if (pitch_follows_time_scale == value) {
		return;
	}
	if (!assign_if_changed2d(pitch_follows_time_scale, value)) {
		return;
	}
	emit_changed();
}

void SOUND_DATA_CLASS::preview() {
	if (Engine::get_singleton() == nullptr || !Engine::get_singleton()->is_editor_hint()) {
		UtilityFunctions::push_error(SOUND_DATA_NAME ".preview: editor only, nothing plays.");
		return;
	}
	preview_at(Vector2(), 0.0f, false);
}

void SOUND_DATA_CLASS::preview_at(const Vector2 &global_position, float volume_offset_db, bool use_position) {
	SoundMixer2D::preview_play(this, global_position, volume_offset_db, use_position);
}

AudioStreamPlayer2D *SOUND_DATA_CLASS::debug_get_preview_player() const {
	SceneTree *tree = SceneTree::get_singleton();
	if (tree == nullptr || tree->get_root() == nullptr) {
		return nullptr;
	}
	Node *node = tree->get_root()->get_node_or_null(NodePath("BlastSoundPreview_" + String::num_uint64(get_instance_id())));
	return Object::cast_to<AudioStreamPlayer2D>(node);
}
