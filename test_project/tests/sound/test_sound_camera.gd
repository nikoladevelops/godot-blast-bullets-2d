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
