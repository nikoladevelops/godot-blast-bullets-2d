extends BlastTest
## Sound listeners: node listeners offset the voice (G + (P - L)), nearest
## wins with tree-order ties, empty sources fall back to Godot's listener,
## busy voices follow moving nodes, point listeners stay fixed, orphans and
## flying volleys keep the spawner's settings.


func after_all() -> void:
	# Same headless-audio drain as the other sound suites.
	OS.delay_msec(500)
	await super()


func _spawner_with_listener(source: int) -> BulletSpawner2D:
	var sp := make_spawner(H.make_volley_data(1, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 1)
	sp.sound_enabled = true
	sp.sound_listener_source = source
	return sp


func test_node_listener_offsets_voice() -> void:
	godot_listener_at(Vector2.ZERO)
	var sp := _spawner_with_listener(BulletSpawner2D.SOUND_LISTENER_NODE_GROUP)
	sp.sound_listener_node_group = &"listeners"
	sp.position = Vector2(150, 0)
	make_listener(Vector2(100, 0))
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	sp.sound_effects = [s]
	watch_signals(sp)
	assert_true(sp.shoot_once(), "shot fires")
	await physics(2)
	assert_eq(busy_voices().size(), 1, "voice busy")
	assert_almost_eq((busy_voices()[0] as Dictionary)["position"], Vector2(50, 0), Vector2(0.5, 0.5), "voice at G + (P - L)")


func test_nearest_wins_tree_order_breaks_ties() -> void:
	godot_listener_at(Vector2.ZERO)
	var sp := _spawner_with_listener(BulletSpawner2D.SOUND_LISTENER_NODE_GROUP)
	sp.sound_listener_node_group = &"listeners"
	sp.position = Vector2(200, 0)
	make_listener(Vector2(150, 0))
	make_listener(Vector2(0, 0))
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	sp.sound_effects = [s]
	watch_signals(sp)
	assert_true(sp.shoot_once(), "shot fires")
	await physics(2)
	assert_almost_eq((busy_voices()[0] as Dictionary)["position"], Vector2(50, 0), Vector2(0.5, 0.5), "nearest listener wins")
	factory.stop_sounds()
	await idle(1)
	var twin_a := make_listener(Vector2(100, 0), &"twins")
	var twin_b := make_listener(Vector2(100, 0), &"twins")
	sp.sound_listener_node_group = &"twins"
	assert_true(sp.shoot_once(), "second shot fires")
	await physics(2)
	assert_almost_eq((busy_voices()[0] as Dictionary)["position"], Vector2(100, 0), Vector2(0.5, 0.5), "tied listeners share the pose")
	twin_b.position = Vector2(120, 0) # only the second twin moves
	await physics(2)
	assert_almost_eq((busy_voices()[0] as Dictionary)["position"], Vector2(100, 0), Vector2(0.5, 0.5), "anchored to the first in tree order")


func test_empty_source_falls_back_to_godot() -> void:
	godot_listener_at(Vector2.ZERO)
	var sp := _spawner_with_listener(BulletSpawner2D.SOUND_LISTENER_NODE_GROUP)
	sp.sound_listener_node_group = &"nobody_here"
	sp.position = Vector2(150, 0)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	sp.sound_effects = [s]
	watch_signals(sp)
	assert_true(sp.shoot_once(), "shot fires")
	await physics(2)
	assert_eq(busy_voices().size(), 1, "still plays")
	assert_almost_eq((busy_voices()[0] as Dictionary)["position"], Vector2(150, 0), Vector2(0.5, 0.5), "fallback: voice at the event")


func test_voice_follows_a_moving_listener() -> void:
	godot_listener_at(Vector2.ZERO)
	var sp := _spawner_with_listener(BulletSpawner2D.SOUND_LISTENER_NODE_GROUP)
	sp.sound_listener_node_group = &"listeners"
	sp.position = Vector2(150, 0)
	var listener := make_listener(Vector2(100, 0))
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	sp.sound_effects = [s]
	watch_signals(sp)
	assert_true(sp.shoot_once(), "shot fires")
	await physics(2)
	assert_almost_eq((busy_voices()[0] as Dictionary)["position"], Vector2(50, 0), Vector2(0.5, 0.5), "starts at G + (P - L)")
	listener.position = Vector2(120, 0)
	await physics(2)
	assert_almost_eq((busy_voices()[0] as Dictionary)["position"], Vector2(30, 0), Vector2(0.5, 0.5), "follows L every sweep")


func test_point_listeners_stay_fixed() -> void:
	godot_listener_at(Vector2.ZERO)
	var sp := _spawner_with_listener(BulletSpawner2D.SOUND_LISTENER_GLOBAL_POSITIONS)
	sp.sound_listener_global_positions = PackedVector2Array([Vector2(200, 0)])
	sp.position = Vector2(260, 0)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_SHOT)
	sp.sound_effects = [s]
	watch_signals(sp)
	assert_true(sp.shoot_once(), "shot fires")
	await physics(2)
	assert_almost_eq((busy_voices()[0] as Dictionary)["position"], Vector2(60, 0), Vector2(0.5, 0.5), "point offset at play")
	sp.sound_listener_global_positions = PackedVector2Array([Vector2(300, 0)])
	await physics(2)
	assert_almost_eq((busy_voices()[0] as Dictionary)["position"], Vector2(60, 0), Vector2(0.5, 0.5), "playing voice stays fixed")


func test_orphan_measures_from_spawner_settings() -> void:
	godot_listener_at(Vector2.ZERO)
	var sp := _spawner_with_listener(BulletSpawner2D.SOUND_LISTENER_NODE_GROUP)
	sp.sound_listener_node_group = &"listeners"
	sp.position = Vector2(150, 0)
	make_listener(Vector2(100, 0))
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_CLEAR)
	sp.sound_effects = [s]
	watch_signals(sp)
	assert_true(sp.shoot_once(), "shot fires")
	var v: BulletVolley2D = get_signal_parameters(sp, "volley_fired", 0)[0] as BulletVolley2D
	sp.queue_free()
	await idle(1)
	var p: Vector2 = v.get_bullet_global_transform(0).origin
	assert_true(v.clear_bullet(0), "orphan clears")
	await physics(2)
	assert_eq(busy_voices().size(), 1, "orphan still sounds")
	assert_almost_eq((busy_voices()[0] as Dictionary)["position"], p - Vector2(100, 0), Vector2(0.5, 0.5), "orphan keeps the detector")


func test_flying_volley_sees_new_listeners() -> void:
	godot_listener_at(Vector2.ZERO)
	var sp := _spawner_with_listener(BulletSpawner2D.SOUND_LISTENER_NODE_GROUP)
	sp.sound_listener_node_group = &"listeners"
	sp.position = Vector2(150, 0)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_CLEAR)
	sp.sound_effects = [s]
	watch_signals(sp)
	assert_true(sp.shoot_once(), "shot fires")
	var v: BulletVolley2D = get_signal_parameters(sp, "volley_fired", 0)[0] as BulletVolley2D
	make_listener(Vector2(100, 0))
	assert_eq(sp.refresh_sound_listeners(), 1, "newcomer counts")
	var p: Vector2 = v.get_bullet_global_transform(0).origin
	assert_true(v.clear_bullet(0), "clears")
	await physics(2)
	assert_almost_eq((busy_voices()[0] as Dictionary)["position"], p - Vector2(100, 0), Vector2(0.5, 0.5), "flying volley measures from the newcomer")


func test_factory_volley_group_path() -> void:
	godot_listener_at(Vector2.ZERO)
	make_listener(Vector2(100, 0))
	var v: BulletVolley2D = factory.spawn_volley(H.make_volley_data(1, 0.0, 30.0))
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_CLEAR)
	v.sound_set_effects([s], &"listeners")
	assert_eq(v.sound_get_listener_group(), &"listeners", "group stored")
	await idle(1)
	assert_true(v.clear_bullet(0), "clears")
	await physics(2)
	assert_eq(busy_voices().size(), 1, "group path sounds")
	assert_almost_eq((busy_voices()[0] as Dictionary)["position"], Vector2(-100, 0), Vector2(0.5, 0.5), "voice at G + (P - L)")
