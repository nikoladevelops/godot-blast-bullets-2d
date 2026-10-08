extends BlastTest
## Camera-aware audibility: inside the camera view plays, outside stays silent;
## zoom 2 halves the effective range; margin rescues near-outside events;
## no camera plays everything.


func after_all() -> void:
	# Same headless-audio drain as the other sound suites.
	OS.delay_msec(500)
	await super()


func _camera_sound() -> BulletSoundData2D:
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.min_interval_sec = 0.0
	s.audibility_mode = BulletSoundData2D.SOUND_AUDIBILITY_CAMERA
	return s


func _add_camera(zoom := Vector2.ONE) -> Camera2D:
	var c := Camera2D.new()
	c.position = Vector2.ZERO
	c.zoom = zoom
	add(c)
	c.make_current()
	await idle(1)
	return c


func _logged(sound_id: int) -> Array:
	var out: Array = []
	for e in factory.debug_get_sound_log():
		if int((e as Dictionary).get("sound", 0)) == sound_id:
			out.append(e)
	return out


func test_audibility_setters_reject_and_keep() -> void:
	var s := BulletSoundData2D.new()
	assert_eq(s.audibility_mode, 0, "distance only by default")
	assert_eq(s.camera_margin_px, 200.0, "margin defaults 200")
	assert_false(s.zoom_scales_distance, "zoom scaling off by default")
	s.set_audibility_mode(5)
	expect_error_sequence(["BulletSoundData2D: audibility_mode must be 0 (Distance), 1 (Camera) or 2 (Distance and Camera), keeping the old value."])
	assert_eq(s.audibility_mode, 0, "kept")
	s.set_audibility_mode(2)
	assert_eq(s.audibility_mode, 2, "valid mode applies")
	s.set_camera_margin_px(-1.0)
	expect_error_sequence(["BulletSoundData2D: camera_margin_px must be finite and >= 0, keeping the old value."])
	assert_eq(s.camera_margin_px, 200.0, "kept")


func test_inside_view_plays_outside_silent() -> void:
	godot_listener_at(Vector2.ZERO)
	_add_camera()
	factory.debug_set_sound_log_enabled(true)
	var s := _camera_sound()
	var sid: int = s.get_instance_id()
	assert_true(factory.play_sound(s, Vector2(100, 0)), "inside offered")
	await physics(2)
	assert_false(_logged(sid).is_empty(), "inside the view plays")
	factory.debug_clear_sound_log()
	factory.play_sound(s, Vector2(10000, 0))
	await physics(2)
	assert_true(_logged(sid).is_empty(), "outside the view stays silent")


func test_zoom_halves_effective_range() -> void:
	godot_listener_at(Vector2.ZERO)
	_add_camera(Vector2(2, 2))
	factory.debug_set_sound_log_enabled(true)
	var s := _camera_sound()
	s.audibility_mode = BulletSoundData2D.SOUND_AUDIBILITY_DISTANCE_AND_CAMERA
	s.max_distance = 1000.0
	s.zoom_scales_distance = true # the subject of this test: zoom 2 halves the range
	s.camera_margin_px = 10000.0 # view culling out of the way: this test pins distance scaling
	var sid: int = s.get_instance_id()
	factory.play_sound(s, Vector2(600, 0))
	await physics(2)
	assert_true(_logged(sid).is_empty(), "zoom 2 halves 1000px to 500px: 600 stays silent")
	factory.debug_clear_sound_log()
	assert_true(factory.play_sound(s, Vector2(400, 0)), "400px offered")
	await physics(2)
	assert_false(_logged(sid).is_empty(), "400px still inside the halved range")


func test_margin_rescues_near_outside() -> void:
	godot_listener_at(Vector2.ZERO)
	_add_camera()
	factory.debug_set_sound_log_enabled(true)
	var s := _camera_sound()
	s.camera_margin_px = 500.0
	var sid: int = s.get_instance_id()
	var view_half: float = get_viewport().get_visible_rect().size.x * 0.5
	assert_true(factory.play_sound(s, Vector2(view_half + 100.0, 0)), "near-outside offered")
	await physics(2)
	assert_false(_logged(sid).is_empty(), "margin 500 rescues a 100px-outside event")


func test_margin_means_screen_pixels_at_any_zoom() -> void:
	godot_listener_at(Vector2.ZERO)
	_add_camera(Vector2(2, 2))
	factory.debug_set_sound_log_enabled(true)
	var s := _camera_sound()
	s.camera_margin_px = 500.0
	var sid: int = s.get_instance_id()
	# Half view in world units at zoom 2; 300 world px past the edge is 600
	# screen px out: beyond a 500 screen-px margin, so it stays silent.
	var edge: float = get_viewport().get_visible_rect().size.x * 0.25
	assert_false(factory.play_sound(s, Vector2(edge + 300.0, 0)), "600 screen px out never takes a slot")
	await physics(2)
	assert_true(_logged(sid).is_empty(), "margin is screen px, not world units")
	factory.debug_clear_sound_log()
	assert_true(factory.play_sound(s, Vector2(edge + 100.0, 0)), "near-outside offered")
	await physics(2)
	assert_false(_logged(sid).is_empty(), "200 screen px out is still rescued")


func test_no_camera_plays_everything() -> void:
	godot_listener_at(Vector2.ZERO)
	factory.debug_set_sound_log_enabled(true)
	var s := _camera_sound()
	var sid: int = s.get_instance_id()
	assert_true(factory.play_sound(s, Vector2(1000, 0)), "inside max_distance offered")
	await physics(2)
	assert_false(_logged(sid).is_empty(), "no camera means no culling")


func test_zoom_gain_follows_the_camera() -> void:
	godot_listener_at(Vector2.ZERO)
	var volumes := {}
	for zoom in [1.0, 2.0, 0.5]:
		_add_camera(Vector2(zoom, zoom))
		var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
		s.volume_db = -6.0
		s.set("zoom_gain_db", 3.0)
		assert_true(factory.play_sound(s, Vector2.ZERO), "offer accepted at zoom %s" % str(zoom))
		await physics(2)
		volumes[zoom] = float((busy_voices()[0] as Dictionary)["volume_db"])
		factory.stop_sounds()
		await idle(1)
	assert_almost_eq(volumes[1.0], -6.0, 0.05, "zoom 1 adds nothing")
	assert_almost_eq(volumes[2.0], -3.0, 0.05, "zoom 2 adds one octave of gain")
	assert_almost_eq(volumes[0.5], -9.0, 0.05, "zoom 0.5 removes one octave")


func test_zoom_gain_off_is_identical_and_clamps_last() -> void:
	godot_listener_at(Vector2.ZERO)
	_add_camera(Vector2(2, 2))
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.volume_db = -6.0
	assert_true(factory.play_sound(s, Vector2.ZERO), "offer accepted")
	await physics(2)
	assert_almost_eq(float((busy_voices()[0] as Dictionary)["volume_db"]), -6.0, 0.05, "default 0 keeps today's mix")
	factory.stop_sounds()
	await idle(1)
	s.set("zoom_gain_db", 24.0)
	s.set("volume_max_db", 0.0)
	assert_true(factory.play_sound(s, Vector2.ZERO), "loud offer accepted")
	await physics(2)
	assert_almost_eq(float((busy_voices()[0] as Dictionary)["volume_db"]), 0.0, 0.05, "gain composes under the ceiling")


func test_zoom_gain_setter_rejects_and_keeps() -> void:
	var s := BulletSoundData2D.new()
	assert_has_method(s, "set_zoom_gain_db", "bound set_zoom_gain_db")
	assert_has_method(s, "get_zoom_gain_db", "bound get_zoom_gain_db")
	assert_eq(s.get("zoom_gain_db"), 0.0, "off by default")
	s.set("zoom_gain_db", NAN)
	expect_error_sequence(["BulletSoundData2D: zoom_gain_db must be finite and between -24 and 24, keeping the old value."])
	assert_eq(s.get("zoom_gain_db"), 0.0, "kept")
	s.set("zoom_gain_db", 25.0)
	expect_error_sequence(["BulletSoundData2D: zoom_gain_db must be finite and between -24 and 24, keeping the old value."])
	s.set("zoom_gain_db", 3.0)
	assert_eq(s.get("zoom_gain_db"), 3.0, "valid value applies")


func _camera_at(pos: Vector2, current: bool) -> Camera2D:
	var c := Camera2D.new()
	c.position = pos
	add(c)
	if current:
		c.make_current()
		await idle(1)
	return c


func test_camera_knobs_gate_on_audibility_mode() -> void:
	var s := BulletSoundData2D.new()
	assert_false(is_editor_visible(s, &"camera_path"), "camera picker hidden for Distance")
	assert_false(is_editor_visible(s, &"camera_margin_px"), "margin hidden for Distance")
	s.audibility_mode = BulletSoundData2D.SOUND_AUDIBILITY_CAMERA
	assert_true(is_editor_visible(s, &"camera_path"), "camera picker for Camera")
	assert_true(is_editor_visible(s, &"camera_margin_px"), "margin for Camera")
	s.audibility_mode = BulletSoundData2D.SOUND_AUDIBILITY_DISTANCE_AND_CAMERA
	assert_true(is_editor_visible(s, &"camera_path"), "camera picker for Both")
	assert_true(is_editor_visible(s, &"camera_margin_px"), "margin for Both")
	assert_true("camera_path" in s, "camera_path exists")


func test_pinned_camera_wins_over_current() -> void:
	godot_listener_at(Vector2.ZERO)
	var a := await _camera_at(Vector2.ZERO, false)
	var b := await _camera_at(Vector2(3000, 0), true)
	factory.debug_set_sound_log_enabled(true)
	var s := _camera_sound()
	s.camera_path = factory.get_path_to(a)
	var sid: int = s.get_instance_id()
	assert_true(factory.play_sound(s, Vector2(100, 0)), "inside pinned view offered")
	await physics(2)
	assert_false(_logged(sid).is_empty(), "pinned camera wins over the current one")
	factory.stop_sounds()
	await idle(1)
	factory.debug_clear_sound_log()
	s.camera_path = factory.get_path_to(b)
	assert_false(factory.play_sound(s, Vector2(100, 0)), "outside pinned view refused")
	await physics(2)
	assert_true(_logged(sid).is_empty(), "pinned camera culled it")
	factory.debug_clear_sound_log()
	s.camera_path = NodePath()
	assert_false(factory.play_sound(s, Vector2(100, 0)), "empty path follows the current camera")
	await physics(2)
	assert_true(_logged(sid).is_empty(), "current camera still culls it")


func test_bad_and_freed_paths_fall_back() -> void:
	godot_listener_at(Vector2.ZERO)
	_add_camera()
	factory.debug_set_sound_log_enabled(true)
	var bad := _camera_sound()
	bad.camera_path = NodePath("Nope/Node")
	assert_true(factory.play_sound(bad, Vector2(100, 0)), "bad path still offers inside the view")
	expect_warning_sequence(["BulletSoundData2D: camera_path does not point to a Camera2D in this viewport, using the viewport camera."])
	await physics(2)
	assert_false(_logged(bad.get_instance_id()).is_empty(), "bad path falls back to the viewport camera")
	assert_true(factory.play_sound(bad, Vector2(100, 0)), "second offer accepted")
	await physics(2)
	var fresh := 0
	for err in get_errors():
		if not err.handled and err.is_push_warning():
			fresh += 1
	assert_eq(fresh, 0, "bad-path warning fires once")


func test_freed_pinned_camera_falls_back() -> void:
	godot_listener_at(Vector2.ZERO)
	var pinned := await _camera_at(Vector2.ZERO, true)
	factory.debug_set_sound_log_enabled(true)
	var s := _camera_sound()
	s.camera_path = factory.get_path_to(pinned)
	pinned.queue_free()
	await idle(1)
	assert_null(get_viewport().get_camera_2d(), "only camera freed: none current")
	assert_true(factory.play_sound(s, Vector2(1000, 0)), "freed camera falls back without crashing")
	await physics(2)
	assert_false(_logged(s.get_instance_id()).is_empty(), "no current camera means no culling")


func test_rotated_camera_culls_exact_view() -> void:
	godot_listener_at(Vector2.ZERO)
	var c := Camera2D.new()
	c.position = Vector2.ZERO
	c.rotation = PI / 4.0
	add(c)
	c.make_current()
	await idle(1)
	factory.debug_set_sound_log_enabled(true)
	var s := _camera_sound()
	var sid: int = s.get_instance_id()
	assert_true(factory.play_sound(s, Vector2(500, 0)), "inside both offered")
	await physics(2)
	assert_false(_logged(sid).is_empty(), "inside the rotated view plays")
	factory.debug_clear_sound_log()
	assert_false(factory.play_sound(s, Vector2(760, 760)), "corner culled by rotation")
	await physics(2)
	assert_true(_logged(sid).is_empty(), "axis box alone does not admit it")


func _pinned_degenerate_voice(z: Vector2) -> void:
	# Pinned but never current (and disabled): the engine canvas never uses
	# the degenerate zoom, so canvas noise stays out of the mixer's reads.
	var c := Camera2D.new()
	c.position = Vector2.ZERO
	c.enabled = false
	add(c)
	await idle(1)
	c.zoom = z
	await idle(1)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	s.volume_db = -6.0
	s.audibility_mode = BulletSoundData2D.SOUND_AUDIBILITY_DISTANCE_AND_CAMERA
	s.max_distance = 1000.0
	s.zoom_scales_distance = true
	s.set("zoom_gain_db", 3.0)
	s.camera_path = factory.get_path_to(c)
	assert_true(factory.play_sound(s, Vector2(100, 0)), "zoom %s offers" % str(z))
	await physics(2)
	assert_eq(busy_voices().size(), 1, "zoom %s still voices" % str(z))
	assert_almost_eq(float((busy_voices()[0] as Dictionary)["volume_db"]), -6.0, 0.05, "zoom %s adds nothing" % str(z))
	factory.stop_sounds()
	await idle(1)
	c.queue_free()
	await idle(1)


func test_zero_zoom_pinned_reads_as_one() -> void:
	godot_listener_at(Vector2.ZERO)
	_add_camera()
	await _pinned_degenerate_voice(Vector2.ZERO)
	# Godot reacts exactly once to a zero-zoom camera living in the tree
	# (entry/assignment-time canvas check); the mixer's own reads stay
	# guarded and correct, as the asserts above prove.
	expect_error_sequence(['Condition "Math::is_zero_approx(p_zoom.x) || Math::is_zero_approx(p_zoom.y)" is true.'])


func test_degenerate_zoom_reads_as_one() -> void:
	godot_listener_at(Vector2.ZERO)
	_add_camera()
	for z in [Vector2(-2, -2), Vector2(INF, INF), Vector2(NAN, NAN)]:
		await _pinned_degenerate_voice(z)
	expect_no_errors("non-zero degenerate zooms stay fully quiet")


func test_explain_names_the_gate() -> void:
	assert_has_method(factory, "debug_explain_sound", "bound explainer")
	if not factory.has_method("debug_explain_sound"):
		return
	godot_listener_at(Vector2.ZERO)
	_add_camera()
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	var ok: Dictionary = factory.debug_explain_sound(s, Vector2(100, 0))
	assert_true(bool(ok["audible"]), "inside everything voices")
	assert_eq(String(ok["blocked_by"]), "none", "no gate")
	assert_true(bool(ok["in_camera_view"]), "view reported")
	assert_true(bool(ok["interval_open"]), "interval open")
	var far: Dictionary = factory.debug_explain_sound(s, Vector2(5000, 0))
	assert_false(bool(far["audible"]), "past range stays silent")
	assert_eq(String(far["blocked_by"]), "distance", "distance gate named")
	assert_gt(float(far["distance"]), float(far["effective_max_distance"]), "numbers tell the story")


func test_explain_covers_view_zoom_interval_and_chance() -> void:
	assert_has_method(factory, "debug_explain_sound", "bound explainer")
	if not factory.has_method("debug_explain_sound"):
		return
	godot_listener_at(Vector2.ZERO)
	_add_camera(Vector2(2, 2))
	var s := _camera_sound()
	var out: Dictionary = factory.debug_explain_sound(s, Vector2(1000, 0))
	assert_eq(String(out["blocked_by"]), "camera_view", "view gate named")
	var z: Dictionary = factory.debug_explain_sound(s, Vector2.ZERO)
	assert_almost_eq(float(z["zoom"]), 2.0, 0.01, "zoom reported")
	var gated := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	gated.min_interval_sec = 100.0
	assert_true(factory.play_sound(gated, Vector2.ZERO), "first consumes the interval")
	await physics(2)
	var held: Dictionary = factory.debug_explain_sound(gated, Vector2.ZERO)
	assert_false(bool(held["interval_open"]), "interval reported closed")
	var chanceless := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	chanceless.trigger_chance = 0.0
	var no: Dictionary = factory.debug_explain_sound(chanceless, Vector2.ZERO)
	assert_eq(String(no["blocked_by"]), "chance_zero", "zero chance named")
	assert_eq(float(no["trigger_chance"]), 0.0, "chance reported")


func test_explain_refusals_and_bad_path() -> void:
	assert_has_method(factory, "debug_explain_sound", "bound explainer")
	if not factory.has_method("debug_explain_sound"):
		return
	godot_listener_at(Vector2.ZERO)
	_add_camera()
	var disabled := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	disabled.enabled = false
	assert_eq(String(factory.debug_explain_sound(disabled, Vector2.ZERO)["blocked_by"]), "disabled", "disabled named")
	var streamless := BulletSoundData2D.new()
	assert_eq(String(factory.debug_explain_sound(streamless, Vector2.ZERO)["blocked_by"]), "streamless", "streamless named")
	var bad := _camera_sound()
	bad.camera_path = NodePath("Nope/Node")
	var fb: Dictionary = factory.debug_explain_sound(bad, Vector2(100, 0))
	expect_warning_sequence(["BulletSoundData2D: camera_path does not point to a Camera2D in this viewport, using the viewport camera."])
	assert_eq(String(fb["camera"]), "viewport", "fallback reported honestly")
	assert_true(bool(fb["audible"]), "falls back to measuring")
