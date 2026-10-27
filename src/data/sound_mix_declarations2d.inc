// The accessor declarations of every SoundMix2D setting, inside each sound
// resource's class body (bullet_sound_data2d.hpp, laser_sound_data2d.hpp).
// The definitions: sound_mix_accessors2d.inc. Not formatted.

	bool get_enabled() const;
	void set_enabled(bool value);
	Array get_streams() const;
	void set_streams(const Array &value);
	StreamMode get_stream_mode() const;
	void set_stream_mode(StreamMode value);
	PackedFloat32Array get_stream_weights() const;
	void set_stream_weights(const PackedFloat32Array &value);
	double get_volume_db() const;
	void set_volume_db(double value);
	double get_pitch_scale() const;
	void set_pitch_scale(double value);
	double get_random_pitch() const;
	void set_random_pitch(double value);
	double get_random_volume_offset_db() const;
	void set_random_volume_offset_db(double value);
	double get_random_volume_min_db() const;
	void set_random_volume_min_db(double value);
	double get_random_volume_max_db() const;
	void set_random_volume_max_db(double value);
	double get_random_pitch_min() const;
	void set_random_pitch_min(double value);
	double get_random_pitch_max() const;
	void set_random_pitch_max(double value);
	double get_random_pan() const;
	void set_random_pan(double value);
	double get_trigger_chance() const;
	void set_trigger_chance(double value);
	StringName get_bus() const;
	void set_bus(const StringName &value);
	bool get_positional() const;
	void set_positional(bool value);
	double get_max_distance() const;
	void set_max_distance(double value);
	double get_attenuation() const;
	void set_attenuation(double value);
	double get_panning_strength() const;
	void set_panning_strength(double value);
	int get_area_mask() const;
	void set_area_mask(int value);
	AudibilityMode get_audibility_mode() const;
	void set_audibility_mode(AudibilityMode value);
	NodePath get_camera_path() const;
	void set_camera_path(const NodePath &value);
	double get_camera_margin_px() const;
	void set_camera_margin_px(double value);
	bool get_zoom_scales_distance() const;
	void set_zoom_scales_distance(bool value);
	double get_zoom_gain_db() const;
	void set_zoom_gain_db(double value);
	int get_occlusion_mask() const;
	void set_occlusion_mask(int value);
	double get_occlusion_db() const;
	void set_occlusion_db(double value);
	StringName get_occlusion_bus() const;
	void set_occlusion_bus(const StringName &value);
	double get_min_interval_sec() const;
	void set_min_interval_sec(double value);
	int get_max_voices() const;
	void set_max_voices(int value);
	SoundLimitMode get_when_limit_reached() const;
	void set_when_limit_reached(SoundLimitMode value);
	StealMode get_steal_mode() const;
	void set_steal_mode(StealMode value);
	int get_priority() const;
	void set_priority(int value);
	double get_max_duration_sec() const;
	void set_max_duration_sec(double value);
	double get_volume_min_db() const;
	void set_volume_min_db(double value);
	double get_volume_max_db() const;
	void set_volume_max_db(double value);
	double get_pitch_min() const;
	void set_pitch_min(double value);
	double get_pitch_max() const;
	void set_pitch_max(double value);
	bool get_pitch_follows_time_scale() const;
	void set_pitch_follows_time_scale(bool value);
	double get_fade_out_sec() const;
	void set_fade_out_sec(double value);
	double get_duck_amount_db() const;
	void set_duck_amount_db(double value);

	// Editor mix preview: plays the picked stream with this entry's mix
	// applied (volume, one pitch roll, bus, centered), so pitch/volume edits
	// are audible without running the game. Editor only (else an error);
	// a second call stops the first, one-shots free themselves on finished,
	// looping entries are stopped after 10 s. Never saved anywhere.
	void preview();
	// Same, at a global pose with a volume offset and positioning honored
	// (the spawner's preview path). Editor only like preview(), but also
	// allowed at runtime, where it bypasses the mixer (no interval, voice
	// cap, priority, distance, camera or occlusion rules).
	void preview_at(const Vector2 &global_position, float volume_offset_db, bool use_position) override;
	// This entry's live preview player, or null (tests and the smoke probe
	// read mix state through it).
	AudioStreamPlayer2D *debug_get_preview_player() const;


	// The mix part of _validate_property (validate_sound_mix_property2d).
	void validate_mix_property(PropertyInfo &p_property) const { validate_sound_mix_property2d(*this, p_property); }

	Resource *sound_resource() override { return this; }
	const Resource *sound_resource() const override { return this; }
	int sound_trigger() const override { return trigger; }
