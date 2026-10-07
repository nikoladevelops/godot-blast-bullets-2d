extends BlastTest
## Sound trigger sites: every entry plays exactly where its effect trigger
## fires (hit = survived, destroy = kill only, bounce = every ricochet,
## expiry, clear; teardown silent), delivered graze/homing events sound even
## unconnected, spawner-only triggers never fire on factory volleys.


func after_all() -> void:
	# Same headless-audio drain as the mixer suite: the dummy server releases
	# stopped playbacks on its own mix cycles, not on stop().
	OS.delay_msec(500)
	await super()


func _logged(sound_id: int) -> Array:
	var out: Array = []
	for e in factory.debug_get_sound_log():
		if int((e as Dictionary).get("sound", 0)) == sound_id:
			out.append(e)
	return out


func _hit_data(max_hits: int) -> BulletVolleyData2D:
	var d := H.make_volley_data(1, 300.0, 8.0)
	d.transforms = [Transform2D(0.0, Vector2.ZERO)]
	d.monitorable = true
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = max_hits
	return d


func test_on_shot_fires_at_spawner_position() -> void:
	factory.debug_set_sound_log_enabled(true)
	var sp := make_spawner(H.make_volley_data(2, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	sp.position = Vector2(50, 60)
	sp.sound_enabled = true
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	sp.sound_effects = [s]
	watch_signals(sp)
	assert_true(sp.shoot_once(), "shot fires")
	await physics(2)
	var log := _logged(s.get_instance_id())
	assert_eq(log.size(), 1, "exactly one shot sound")
	assert_almost_eq((log[0] as Dictionary)["position"], Vector2(50, 60), Vector2(0.5, 0.5), "at the spawner position")


func test_on_telegraph_fires_with_the_warning() -> void:
	factory.debug_set_sound_log_enabled(true)
	var sp := make_spawner(H.make_volley_data(5, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 5)
	sp.burst_enabled = true
	sp.burst_count = 3
	sp.burst_interval_sec = 0.05
	sp.telegraph_enabled = true
	sp.telegraph_sec = 0.05
	sp.shoot_interval_sec = 10.0
	sp.sound_enabled = true
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_TELEGRAPH)
	sp.sound_effects = [s]
	watch_signals(sp)
	sp.set_shooting_enabled(true)
	for i in 60:
		await physics(1)
		if get_signal_emit_count(sp, "volley_telegraphed") > 0:
			break
	sp.set_shooting_enabled(false)
	assert_signal_emit_count(sp, "volley_telegraphed", 1, "telegraph fired")
	await physics(2)
	assert_eq(_logged(s.get_instance_id()).size(), 1, "exactly one telegraph sound")


func test_destroy_kill_only_hit_survived_only() -> void:
	factory.debug_set_sound_log_enabled(true)
	make_wall(Vector2(200, 0))
	var hit := H.make_sound(BulletSoundData2D.SOUND_ON_HIT)
	var destroy := H.make_sound(BulletSoundData2D.SOUND_ON_DESTROY)
	var killer: BulletVolley2D = factory.spawn_volley(_hit_data(1))
	killer.sound_set_effects([hit, destroy])
	for i in 90:
		await physics(1)
		if not _logged(destroy.get_instance_id()).is_empty():
			break
	assert_eq(_logged(destroy.get_instance_id()).size(), 1, "lethal hit sounds destroy once")
	assert_true(_logged(hit.get_instance_id()).is_empty(), "lethal hit never sounds hit")
	var survivor: BulletVolley2D = factory.spawn_volley(_hit_data(5))
	survivor.sound_set_effects([hit, destroy])
	for i in 90:
		await physics(1)
		if not _logged(hit.get_instance_id()).is_empty():
			break
	assert_true(not _logged(hit.get_instance_id()).is_empty(), "survived hit sounds hit")
	assert_true(_logged(destroy.get_instance_id()).size() == 1, "survived hit never sounds destroy")


func test_bounce_fires_every_ricochet() -> void:
	factory.debug_set_sound_log_enabled(true)
	make_wall(Vector2(200, 0), Vector2(20, 400), 8, 2)
	var d := H.make_volley_data(1, 300.0, 8.0)
	d.transforms = [Transform2D(0.0, Vector2.ZERO)]
	d.monitorable = true
	d.set_collision_mask_from_array([4])
	d.set_bounce_mask_from_array([4])
	d.bullet_max_collision_count = 5
	d.collision_shape = H.make_circle_shape(6.0)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_BOUNCE)
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.sound_set_effects([s])
	for i in 90:
		await physics(1)
		if not _logged(s.get_instance_id()).is_empty():
			break
	assert_true(not _logged(s.get_instance_id()).is_empty(), "ricochet sounds bounce")


func test_lifetime_over_fires_on_expiry() -> void:
	factory.debug_set_sound_log_enabled(true)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_LIFETIME_OVER)
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(1, 0.0, 0.05))
	v.sound_set_effects([s])
	for i in 12:
		await physics(1)
		if not _logged(s.get_instance_id()).is_empty():
			break
	assert_eq(_logged(s.get_instance_id()).size(), 1, "expiry sounds once")


func test_clear_paths_sound_teardown_stays_silent() -> void:
	factory.debug_set_sound_log_enabled(true)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_CLEAR)
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(2, 0.0, 30.0))
	v.sound_set_effects([s])
	assert_true(v.clear_bullet(0), "one cleared")
	await physics(2)
	assert_eq(_logged(s.get_instance_id()).size(), 1, "single clear sounds once")
	assert_eq(v.clear_all_bullets(), 1, "rest cleared")
	await physics(2)
	assert_eq(_logged(s.get_instance_id()).size(), 2, "clear-all sounds per bullet")
	var before: int = _logged(s.get_instance_id()).size()
	var w: BulletVolley2D = factory.spawn_volley(H.make_volley_data(1, 0.0, 30.0))
	w.sound_set_effects([s])
	await idle(1)
	factory.free_active_bullets()
	await idle(1)
	factory.reset()
	await idle(1)
	assert_eq(_logged(s.get_instance_id()).size(), before, "teardown sounds nothing")


func test_homing_reached_fires() -> void:
	factory.debug_set_sound_log_enabled(true)
	var target := make_graze_target(Vector2(400, 0))
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(1, 400.0, 30.0))
	v.set_homing_smoothing(8.0)
	v.set_homing_take_control_of_texture_rotation(true)
	v.set_homing_distance_before_reached(30.0)
	v.bullet_homing_push_back_node2d_target(0, target)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_HOMING_TARGET_REACHED)
	v.sound_set_effects([s])
	for i in 120:
		await physics(1)
		if not _logged(s.get_instance_id()).is_empty():
			break
	assert_eq(_logged(s.get_instance_id()).size(), 1, "reached sounds once")


func test_graze_and_exit_fire() -> void:
	factory.debug_set_sound_log_enabled(true)
	make_graze_target(Vector2(100, 0), &"g")
	var zone := H.make_graze_zone([24.0])
	var v := graze_volley(H.transforms_at([Vector2(-100, 0)]), 200.0)
	assert_true(v.graze_set_zones([zone], &"g"), "zones armed")
	var enter := H.make_sound(BulletSoundData2D.SOUND_ON_GRAZE)
	var exit := H.make_sound(BulletSoundData2D.SOUND_ON_GRAZE_EXIT)
	v.sound_set_effects([enter, exit])
	for i in 120:
		await physics(1)
		if not _logged(enter.get_instance_id()).is_empty():
			break
	assert_eq(_logged(enter.get_instance_id()).size(), 1, "graze sounds once")
	for i in 120:
		await physics(1)
		if not _logged(exit.get_instance_id()).is_empty():
			break
	assert_eq(_logged(exit.get_instance_id()).size(), 1, "exit sounds once")


func test_stacking_plays_every_matching_entry() -> void:
	factory.debug_set_sound_log_enabled(true)
	var sp := make_spawner(H.make_volley_data(2, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	sp.sound_enabled = true
	var a := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	var b := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	sp.sound_effects = [a, b]
	watch_signals(sp)
	assert_true(sp.shoot_once(), "shot fires")
	await physics(2)
	assert_eq(_logged(a.get_instance_id()).size(), 1, "first entry plays")
	assert_eq(_logged(b.get_instance_id()).size(), 1, "second entry plays")


func test_silent_entries_play_nothing() -> void:
	factory.debug_set_sound_log_enabled(true)
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(1, 0.0, 30.0))
	var disabled := H.make_sound(BulletSoundData2D.SOUND_ON_CLEAR)
	disabled.enabled = false
	var streamless := BulletSoundData2D.new()
	streamless.trigger = BulletSoundData2D.SOUND_ON_CLEAR
	streamless.min_interval_sec = 0.0
	var live := H.make_sound(BulletSoundData2D.SOUND_ON_CLEAR)
	v.sound_set_effects([disabled, null, streamless, live])
	assert_true(v.clear_bullet(0), "cleared")
	await physics(2)
	expect_warning_sequence(["BulletSoundData2D: stream is empty, the sound plays nothing."])
	assert_eq(_logged(live.get_instance_id()).size(), 1, "live entry plays")


func test_factory_volley_never_fires_spawner_only_triggers() -> void:
	factory.debug_set_sound_log_enabled(true)
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(1, 0.0, 30.0))
	var shot := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	var telegraph := H.make_sound(BulletSoundData2D.SOUND_ON_TELEGRAPH)
	v.sound_set_effects([shot, telegraph])
	await physics(5)
	assert_true(v.is_sound_armed(), "entries arm (mask set)")
	assert_true(_logged(shot.get_instance_id()).is_empty(), "no shot sound on a factory volley")
	assert_true(_logged(telegraph.get_instance_id()).is_empty(), "no telegraph sound on a factory volley")


func test_reused_volley_never_plays_a_previous_life() -> void:
	factory.debug_set_sound_log_enabled(true)
	var old := H.make_sound(BulletSoundData2D.SOUND_ON_CLEAR)
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(1, 0.0, 30.0))
	v.sound_set_effects([old])
	assert_eq(v.clear_all_bullets(), 1, "drained to the pool")
	await physics(2)
	assert_eq(_logged(old.get_instance_id()).size(), 1, "old life sounded")
	var w: BulletVolley2D = factory.spawn_volley(H.make_volley_data(1, 0.0, 30.0))
	assert_true(w.sound_get_effects().is_empty(), "reused volley carries nothing")
	assert_true(w.clear_bullet(0), "cleared")
	await physics(2)
	assert_eq(_logged(old.get_instance_id()).size(), 1, "previous life stays silent")
