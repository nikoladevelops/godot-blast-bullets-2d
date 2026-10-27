// The binds of every SoundMix2D setting, in inspector order, inside each sound
// resource's _bind_methods (with SOUND_DATA_CLASS defined). Two hooks place
// the resource's own properties: SOUND_MIX_TRIGGER_BINDS() after `enabled`
// (its triggers), SOUND_MIX_LIMIT_BINDS() inside Limits (its own gates).
// Not formatted.

	ClassDB::bind_method(D_METHOD("get_enabled"), &SOUND_DATA_CLASS::get_enabled);
	ClassDB::bind_method(D_METHOD("set_enabled", "value"), &SOUND_DATA_CLASS::set_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "get_enabled");

	SOUND_MIX_TRIGGER_BINDS()
	ClassDB::bind_method(D_METHOD("get_streams"), &SOUND_DATA_CLASS::get_streams);
	ClassDB::bind_method(D_METHOD("set_streams", "streams"), &SOUND_DATA_CLASS::set_streams);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "streams", PROPERTY_HINT_ARRAY_TYPE, "AudioStream"), "set_streams", "get_streams");

	ClassDB::bind_method(D_METHOD("get_stream_mode"), &SOUND_DATA_CLASS::get_stream_mode);
	ClassDB::bind_method(D_METHOD("set_stream_mode", "value"), &SOUND_DATA_CLASS::set_stream_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "stream_mode", PROPERTY_HINT_ENUM, "Random,Weighted,Sequence,Shuffle"), "set_stream_mode", "get_stream_mode");

	ClassDB::bind_method(D_METHOD("get_stream_weights"), &SOUND_DATA_CLASS::get_stream_weights);
	ClassDB::bind_method(D_METHOD("set_stream_weights", "weights"), &SOUND_DATA_CLASS::set_stream_weights);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "stream_weights"), "set_stream_weights", "get_stream_weights");

	ADD_GROUP("Volume and Pitch", "");

	ClassDB::bind_method(D_METHOD("get_volume_db"), &SOUND_DATA_CLASS::get_volume_db);
	ClassDB::bind_method(D_METHOD("set_volume_db", "value"), &SOUND_DATA_CLASS::set_volume_db);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "volume_db", PROPERTY_HINT_RANGE, "-80,80,0.1,suffix:dB"), "set_volume_db", "get_volume_db");

	ClassDB::bind_method(D_METHOD("get_pitch_scale"), &SOUND_DATA_CLASS::get_pitch_scale);
	ClassDB::bind_method(D_METHOD("set_pitch_scale", "value"), &SOUND_DATA_CLASS::set_pitch_scale);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pitch_scale", PROPERTY_HINT_RANGE, "0.01,1000,0.01,exp"), "set_pitch_scale", "get_pitch_scale");

	ClassDB::bind_method(D_METHOD("get_random_pitch"), &SOUND_DATA_CLASS::get_random_pitch);
	ClassDB::bind_method(D_METHOD("set_random_pitch", "value"), &SOUND_DATA_CLASS::set_random_pitch);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "random_pitch", PROPERTY_HINT_RANGE, "1,16,0.01"), "set_random_pitch", "get_random_pitch");

	ClassDB::bind_method(D_METHOD("get_random_volume_offset_db"), &SOUND_DATA_CLASS::get_random_volume_offset_db);
	ClassDB::bind_method(D_METHOD("set_random_volume_offset_db", "value"), &SOUND_DATA_CLASS::set_random_volume_offset_db);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "random_volume_offset_db", PROPERTY_HINT_RANGE, "0,40,0.01,suffix:dB"), "set_random_volume_offset_db", "get_random_volume_offset_db");

	ClassDB::bind_method(D_METHOD("get_random_volume_min_db"), &SOUND_DATA_CLASS::get_random_volume_min_db);
	ClassDB::bind_method(D_METHOD("set_random_volume_min_db", "value"), &SOUND_DATA_CLASS::set_random_volume_min_db);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "random_volume_min_db", PROPERTY_HINT_RANGE, "-40,40,0.01,suffix:dB"), "set_random_volume_min_db", "get_random_volume_min_db");

	ClassDB::bind_method(D_METHOD("get_random_volume_max_db"), &SOUND_DATA_CLASS::get_random_volume_max_db);
	ClassDB::bind_method(D_METHOD("set_random_volume_max_db", "value"), &SOUND_DATA_CLASS::set_random_volume_max_db);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "random_volume_max_db", PROPERTY_HINT_RANGE, "-40,40,0.01,suffix:dB"), "set_random_volume_max_db", "get_random_volume_max_db");

	ClassDB::bind_method(D_METHOD("get_random_pitch_min"), &SOUND_DATA_CLASS::get_random_pitch_min);
	ClassDB::bind_method(D_METHOD("set_random_pitch_min", "value"), &SOUND_DATA_CLASS::set_random_pitch_min);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "random_pitch_min", PROPERTY_HINT_RANGE, "0.01,16,0.01"), "set_random_pitch_min", "get_random_pitch_min");

	ClassDB::bind_method(D_METHOD("get_random_pitch_max"), &SOUND_DATA_CLASS::get_random_pitch_max);
	ClassDB::bind_method(D_METHOD("set_random_pitch_max", "value"), &SOUND_DATA_CLASS::set_random_pitch_max);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "random_pitch_max", PROPERTY_HINT_RANGE, "0.01,16,0.01"), "set_random_pitch_max", "get_random_pitch_max");

	ClassDB::bind_method(D_METHOD("get_random_pan"), &SOUND_DATA_CLASS::get_random_pan);
	ClassDB::bind_method(D_METHOD("set_random_pan", "value"), &SOUND_DATA_CLASS::set_random_pan);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "random_pan", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_random_pan", "get_random_pan");

	ClassDB::bind_method(D_METHOD("get_trigger_chance"), &SOUND_DATA_CLASS::get_trigger_chance);
	ClassDB::bind_method(D_METHOD("set_trigger_chance", "value"), &SOUND_DATA_CLASS::set_trigger_chance);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "trigger_chance", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_trigger_chance", "get_trigger_chance");

	ClassDB::bind_method(D_METHOD("get_bus"), &SOUND_DATA_CLASS::get_bus);
	ClassDB::bind_method(D_METHOD("set_bus", "value"), &SOUND_DATA_CLASS::set_bus);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "bus"), "set_bus", "get_bus");

	ADD_GROUP("Spatial", "");

	ClassDB::bind_method(D_METHOD("get_positional"), &SOUND_DATA_CLASS::get_positional);
	ClassDB::bind_method(D_METHOD("set_positional", "value"), &SOUND_DATA_CLASS::set_positional);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "positional"), "set_positional", "get_positional");

	ClassDB::bind_method(D_METHOD("get_max_distance"), &SOUND_DATA_CLASS::get_max_distance);
	ClassDB::bind_method(D_METHOD("set_max_distance", "value"), &SOUND_DATA_CLASS::set_max_distance);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_distance", PROPERTY_HINT_RANGE, "1,4096,1,or_greater,exp,suffix:px"), "set_max_distance", "get_max_distance");

	ClassDB::bind_method(D_METHOD("get_attenuation"), &SOUND_DATA_CLASS::get_attenuation);
	ClassDB::bind_method(D_METHOD("set_attenuation", "value"), &SOUND_DATA_CLASS::set_attenuation);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "attenuation", PROPERTY_HINT_EXP_EASING, "attenuation"), "set_attenuation", "get_attenuation");

	ClassDB::bind_method(D_METHOD("get_panning_strength"), &SOUND_DATA_CLASS::get_panning_strength);
	ClassDB::bind_method(D_METHOD("set_panning_strength", "value"), &SOUND_DATA_CLASS::set_panning_strength);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "panning_strength", PROPERTY_HINT_RANGE, "0,3,0.01,or_greater"), "set_panning_strength", "get_panning_strength");

	ClassDB::bind_method(D_METHOD("get_area_mask"), &SOUND_DATA_CLASS::get_area_mask);
	ClassDB::bind_method(D_METHOD("set_area_mask", "value"), &SOUND_DATA_CLASS::set_area_mask);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "area_mask", PROPERTY_HINT_LAYERS_2D_PHYSICS), "set_area_mask", "get_area_mask");

	ClassDB::bind_method(D_METHOD("get_audibility_mode"), &SOUND_DATA_CLASS::get_audibility_mode);
	ClassDB::bind_method(D_METHOD("set_audibility_mode", "value"), &SOUND_DATA_CLASS::set_audibility_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "audibility_mode", PROPERTY_HINT_ENUM, "Distance,Camera,Distance and Camera"), "set_audibility_mode", "get_audibility_mode");

	ClassDB::bind_method(D_METHOD("get_camera_path"), &SOUND_DATA_CLASS::get_camera_path);
	ClassDB::bind_method(D_METHOD("set_camera_path", "value"), &SOUND_DATA_CLASS::set_camera_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "camera_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Camera2D"), "set_camera_path", "get_camera_path");

	ClassDB::bind_method(D_METHOD("get_camera_margin_px"), &SOUND_DATA_CLASS::get_camera_margin_px);
	ClassDB::bind_method(D_METHOD("set_camera_margin_px", "value"), &SOUND_DATA_CLASS::set_camera_margin_px);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "camera_margin_px", PROPERTY_HINT_RANGE, "0,4096,1,or_greater,suffix:px"), "set_camera_margin_px", "get_camera_margin_px");

	ClassDB::bind_method(D_METHOD("get_zoom_scales_distance"), &SOUND_DATA_CLASS::get_zoom_scales_distance);
	ClassDB::bind_method(D_METHOD("set_zoom_scales_distance", "value"), &SOUND_DATA_CLASS::set_zoom_scales_distance);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "zoom_scales_distance"), "set_zoom_scales_distance", "get_zoom_scales_distance");

	ClassDB::bind_method(D_METHOD("get_zoom_gain_db"), &SOUND_DATA_CLASS::get_zoom_gain_db);
	ClassDB::bind_method(D_METHOD("set_zoom_gain_db", "value"), &SOUND_DATA_CLASS::set_zoom_gain_db);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "zoom_gain_db", PROPERTY_HINT_RANGE, "-24,24,0.01,suffix:dB"), "set_zoom_gain_db", "get_zoom_gain_db");

	ClassDB::bind_method(D_METHOD("get_occlusion_mask"), &SOUND_DATA_CLASS::get_occlusion_mask);
	ClassDB::bind_method(D_METHOD("set_occlusion_mask", "value"), &SOUND_DATA_CLASS::set_occlusion_mask);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "occlusion_mask", PROPERTY_HINT_LAYERS_2D_PHYSICS), "set_occlusion_mask", "get_occlusion_mask");

	ClassDB::bind_method(D_METHOD("get_occlusion_db"), &SOUND_DATA_CLASS::get_occlusion_db);
	ClassDB::bind_method(D_METHOD("set_occlusion_db", "value"), &SOUND_DATA_CLASS::set_occlusion_db);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "occlusion_db", PROPERTY_HINT_RANGE, "0,24,0.01,or_greater,suffix:dB"), "set_occlusion_db", "get_occlusion_db");
	ClassDB::bind_method(D_METHOD("get_occlusion_bus"), &SOUND_DATA_CLASS::get_occlusion_bus);
	ClassDB::bind_method(D_METHOD("set_occlusion_bus", "value"), &SOUND_DATA_CLASS::set_occlusion_bus);
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "occlusion_bus"), "set_occlusion_bus", "get_occlusion_bus");

	ADD_GROUP("Limits", "");

	ClassDB::bind_method(D_METHOD("get_min_interval_sec"), &SOUND_DATA_CLASS::get_min_interval_sec);
	ClassDB::bind_method(D_METHOD("set_min_interval_sec", "value"), &SOUND_DATA_CLASS::set_min_interval_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_interval_sec", PROPERTY_HINT_RANGE, "0,1,0.001,or_greater,suffix:s"), "set_min_interval_sec", "get_min_interval_sec");

	ClassDB::bind_method(D_METHOD("get_max_voices"), &SOUND_DATA_CLASS::get_max_voices);
	ClassDB::bind_method(D_METHOD("set_max_voices", "value"), &SOUND_DATA_CLASS::set_max_voices);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_voices", PROPERTY_HINT_RANGE, "1,32,1,or_greater"), "set_max_voices", "get_max_voices");

	ClassDB::bind_method(D_METHOD("get_when_limit_reached"), &SOUND_DATA_CLASS::get_when_limit_reached);
	ClassDB::bind_method(D_METHOD("set_when_limit_reached", "value"), &SOUND_DATA_CLASS::set_when_limit_reached);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "when_limit_reached", PROPERTY_HINT_ENUM, "Replace Oldest,Skip New"), "set_when_limit_reached", "get_when_limit_reached");

	ClassDB::bind_method(D_METHOD("get_steal_mode"), &SOUND_DATA_CLASS::get_steal_mode);
	ClassDB::bind_method(D_METHOD("set_steal_mode", "value"), &SOUND_DATA_CLASS::set_steal_mode);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "steal_mode", PROPERTY_HINT_ENUM, "Oldest,Quietest"), "set_steal_mode", "get_steal_mode");

	ClassDB::bind_method(D_METHOD("get_priority"), &SOUND_DATA_CLASS::get_priority);
	ClassDB::bind_method(D_METHOD("set_priority", "value"), &SOUND_DATA_CLASS::set_priority);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "priority", PROPERTY_HINT_RANGE, "-16,16,1,or_greater,or_less"), "set_priority", "get_priority");

	ClassDB::bind_method(D_METHOD("get_max_duration_sec"), &SOUND_DATA_CLASS::get_max_duration_sec);
	ClassDB::bind_method(D_METHOD("set_max_duration_sec", "value"), &SOUND_DATA_CLASS::set_max_duration_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_duration_sec", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s"), "set_max_duration_sec", "get_max_duration_sec");

	SOUND_MIX_LIMIT_BINDS()
	ClassDB::bind_method(D_METHOD("get_volume_min_db"), &SOUND_DATA_CLASS::get_volume_min_db);
	ClassDB::bind_method(D_METHOD("set_volume_min_db", "value"), &SOUND_DATA_CLASS::set_volume_min_db);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "volume_min_db", PROPERTY_HINT_RANGE, "-80,80,0.1,suffix:dB"), "set_volume_min_db", "get_volume_min_db");

	ClassDB::bind_method(D_METHOD("get_volume_max_db"), &SOUND_DATA_CLASS::get_volume_max_db);
	ClassDB::bind_method(D_METHOD("set_volume_max_db", "value"), &SOUND_DATA_CLASS::set_volume_max_db);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "volume_max_db", PROPERTY_HINT_RANGE, "-80,80,0.1,suffix:dB"), "set_volume_max_db", "get_volume_max_db");

	ClassDB::bind_method(D_METHOD("get_pitch_min"), &SOUND_DATA_CLASS::get_pitch_min);
	ClassDB::bind_method(D_METHOD("set_pitch_min", "value"), &SOUND_DATA_CLASS::set_pitch_min);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pitch_min", PROPERTY_HINT_RANGE, "0.01,1000,0.01,exp"), "set_pitch_min", "get_pitch_min");

	ClassDB::bind_method(D_METHOD("get_pitch_max"), &SOUND_DATA_CLASS::get_pitch_max);
	ClassDB::bind_method(D_METHOD("set_pitch_max", "value"), &SOUND_DATA_CLASS::set_pitch_max);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "pitch_max", PROPERTY_HINT_RANGE, "0.01,1000,0.01,exp"), "set_pitch_max", "get_pitch_max");

	ClassDB::bind_method(D_METHOD("get_pitch_follows_time_scale"), &SOUND_DATA_CLASS::get_pitch_follows_time_scale);
	ClassDB::bind_method(D_METHOD("set_pitch_follows_time_scale", "value"), &SOUND_DATA_CLASS::set_pitch_follows_time_scale);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "pitch_follows_time_scale"), "set_pitch_follows_time_scale", "get_pitch_follows_time_scale");

	ClassDB::bind_method(D_METHOD("get_fade_out_sec"), &SOUND_DATA_CLASS::get_fade_out_sec);
	ClassDB::bind_method(D_METHOD("set_fade_out_sec", "value"), &SOUND_DATA_CLASS::set_fade_out_sec);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fade_out_sec", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s"), "set_fade_out_sec", "get_fade_out_sec");

	ClassDB::bind_method(D_METHOD("get_duck_amount_db"), &SOUND_DATA_CLASS::get_duck_amount_db);
	ClassDB::bind_method(D_METHOD("set_duck_amount_db", "value"), &SOUND_DATA_CLASS::set_duck_amount_db);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "duck_amount_db", PROPERTY_HINT_RANGE, "0,24,0.01,or_greater,suffix:dB"), "set_duck_amount_db", "get_duck_amount_db");

	ClassDB::bind_method(D_METHOD("preview"), &SOUND_DATA_CLASS::preview);
	ClassDB::bind_method(D_METHOD("preview_at", "global_position", "volume_offset_db", "use_position"), &SOUND_DATA_CLASS::preview_at);
	ClassDB::bind_method(D_METHOD("debug_get_preview_player"), &SOUND_DATA_CLASS::debug_get_preview_player);
