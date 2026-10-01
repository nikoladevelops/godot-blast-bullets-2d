extends SceneTree
## Factory clear-methods suite: which factory entry fires On Clear visuals
## and which stays silent teardown.
##
## The bug: volley-level clear_bullet()/clear_all_bullets() fired On Clear,
## but the factory had NO clearing entry that fired it. free_active_bullets()
## memdeletes volleys (their one-shot bakes die with them, so firing there
## could never show), and reset() wipes everything. Users clearing a wave
## through the factory therefore never saw the dismissal puff.
##
## The fix: BulletFactory2D.clear_active_bullets() (+ deferred twin) runs each
## active volley's clear_all_bullets(), so On Clear layers fire and emptied
## volleys park in the pool. free_active_bullets()/reset() stay silent
## teardown by design, and this suite pins that contract.
##
## Covers: T1 clear_bullet returns true and fires, T2 clear_active_bullets
## clears every live bullet across volleys / returns the count / parks volleys
## pooled, T3 free_active_bullets stays silent and frees, T4 reset stays
## silent, T5 key-scoped clear touches only its bucket, T6 deferred twins run,
## T7 no dangling.
##
## Run: godot --headless --path test_project --script tests/factory/test_factory_clear_triggers.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _make_frames() -> SpriteFrames:
	var sf := SpriteFrames.new()
	if not sf.has_animation("default"):
		sf.add_animation("default")
	sf.set_animation_speed("default", 10.0)
	sf.set_animation_loop("default", false)
	for i in 4:
		var img := Image.create_empty(8, 8, false, Image.FORMAT_RGBA8)
		img.fill(Color(1, 1, 1, 1))
		sf.add_frame("default", ImageTexture.create_from_image(img))
	return sf

func _clear_layer() -> BulletEffectLayerData2D:
	var l := BulletEffectLayerData2D.new()
	l.trigger = 6 # EFFECT_ON_CLEAR
	l.sprite_frames = _make_frames()
	return l

func _data(bullets: int, layer: BulletEffectLayerData2D) -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	var tr: Array = []
	for i in bullets:
		tr.append(Transform2D(0.0, Vector2(i * 8.0, 0.0)))
	d.transforms = tr
	var speeds: Array = []
	for i in bullets:
		var s := BulletSpeedData2D.new()
		s.speed = 0.0
		s.max_speed = 3000.0
		speeds.append(s)
	d.all_bullet_speed_data = speeds
	d.max_life_time = 60.0
	d.texture_size = Vector2(16, 16)
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array([3])
	var c := CircleShape2D.new()
	c.radius = 6.0
	d.collision_shape = c
	d.bullet_max_collision_count = 0
	d.effect_layers = [layer]
	return d

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	var layer := _clear_layer()

	# ---------------------------------------------------------------
	printerr("CLEAR T1 clear_bullet returns true and fires On Clear")
	var v1: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data(2, layer))
	factory.clear_sprite_effects()
	_check(factory.get_active_effect_count() == 0, "T1 quiet before clear")
	_check(v1.clear_bullet(0), "T1 clear_bullet live slot returns true")
	_check(factory.get_active_effect_count() >= 1, "T1 On Clear fired (active=%d)" % factory.get_active_effect_count())
	_check(not v1.clear_bullet(0), "T1 double clear stays silent")
	factory.clear_sprite_effects()
	factory.free_active_bullets()
	await process_frame

	# ---------------------------------------------------------------
	printerr("CLEAR T2 clear_active_bullets clears every volley with visuals")
	var va: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data(2, layer))
	var vb: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data(3, layer))
	factory.clear_sprite_effects()
	var cleared: int = factory.clear_active_bullets()
	_check(cleared == 5, "T2 returns total live bullets (got %d)" % cleared)
	_check(factory.get_active_effect_count() >= 5, "T2 On Clear fired per bullet (active=%d)" % factory.get_active_effect_count())
	_check(factory.debug_get_active_bullets_amount(0) == 0, "T2 no active volleys left")
	_check(factory.debug_get_bullets_pool_amount(0) >= 2, "T2 emptied volleys parked pooled (pooled=%d)" % factory.debug_get_bullets_pool_amount(0))
	_check(not va.is_bullet_status_enabled(0), "T2 volley A bullet parked disabled")
	_check(not vb.is_bullet_status_enabled(0), "T2 volley B bullet parked disabled")
	factory.clear_sprite_effects()
	factory.free_active_bullets()
	factory.free_bullets_pool(0)
	await process_frame

	# ---------------------------------------------------------------
	printerr("CLEAR T3 free_active_bullets stays silent and frees")
	var v3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data(2, layer))
	factory.clear_sprite_effects()
	factory.free_active_bullets()
	_check(factory.get_active_effect_count() == 0, "T3 free fires no On Clear")
	_check(factory.debug_get_active_bullets_amount(0) == 0, "T3 volleys freed")
	await process_frame

	# ---------------------------------------------------------------
	printerr("CLEAR T4 reset stays silent")
	var v4: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data(2, layer))
	factory.clear_sprite_effects()
	factory.reset()
	_check(factory.get_active_effect_count() == 0, "T4 reset fires no On Clear")
	await process_frame

	# ---------------------------------------------------------------
	printerr("CLEAR T5 key-scoped clear touches only its bucket")
	var v5a: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data(2, layer))
	var v5b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data(3, layer))
	var key_a: MultiMeshPoolKey2D = factory.debug_expected_pool_key(_data(2, layer))
	factory.clear_sprite_effects()
	var cleared_a: int = factory.clear_active_bullets(key_a)
	_check(cleared_a == 2, "T5 scoped clear returns 2 (got %d)" % cleared_a)
	_check(factory.get_active_effect_count() >= 2, "T5 scoped clear fires visuals (active=%d)" % factory.get_active_effect_count())
	_check(v5b.is_bullet_status_enabled(0), "T5 other bucket still live")
	_check(v5b.is_bullet_status_enabled(2), "T5 other bucket third bullet still live")
	factory.clear_sprite_effects()
	factory.free_active_bullets()
	factory.free_bullets_pool(0)
	await process_frame

	# ---------------------------------------------------------------
	printerr("CLEAR T6 deferred twins run after the frame")
	var v6: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data(2, layer))
	factory.clear_sprite_effects()
	factory.clear_active_bullets_deferred()
	await process_frame
	await process_frame
	_check(factory.get_active_effect_count() >= 2, "T6 deferred clear fired (active=%d)" % factory.get_active_effect_count())
	factory.clear_sprite_effects()
	var v6b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data(2, layer))
	factory.free_active_bullets_deferred()
	await process_frame
	await process_frame
	_check(factory.debug_get_active_bullets_amount(0) == 0, "T6 deferred free ran")
	_check(factory.get_active_effect_count() == 0, "T6 deferred free stayed silent")

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL FACTORY-CLEAR TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
