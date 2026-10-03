extends BlastTest
## Factory clear contract: clear_bullet/clear_active_bullets fire On Clear
## visuals and park emptied volleys pooled; free_active_bullets()/reset()
## stay silent teardown; key-scoped clears touch only their bucket; deferred
## twins run on the next idle frame.

var layer: BulletEffectLayerData2D


func before_each() -> void:
	await super()
	layer = BulletEffectLayerData2D.new()
	layer.trigger = BulletEffectLayerData2D.EFFECT_ON_CLEAR
	var sf := SpriteFrames.new()
	sf.set_animation_speed("default", 10.0)
	sf.set_animation_loop("default", false)
	for i in 4:
		var img := Image.create_empty(8, 8, false, Image.FORMAT_RGBA8)
		img.fill(Color.WHITE)
		sf.add_frame("default", ImageTexture.create_from_image(img))
	layer.sprite_frames = sf


func _data(bullets: int) -> DirectionalBulletsData2D:
	var d := H.make_directional_data(bullets, 0.0, 60.0)
	var tr: Array = []
	for i in bullets:
		tr.append(Transform2D(0.0, Vector2(i * 8.0, 0.0)))
	d.transforms = tr
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 0
	d.effect_layers = [layer]
	return d


func test_clear_bullet_fires_once() -> void:
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data(2))
	factory.clear_sprite_effects()
	assert_eq(factory.get_active_effect_count(), 0, "quiet before clear")
	assert_true(v.clear_bullet(0), "clear_bullet on a live slot returns true")
	assert_gte(factory.get_active_effect_count(), 1, "On Clear fired")
	assert_false(v.clear_bullet(0), "double clear stays silent")


func test_clear_active_bullets_clears_every_volley() -> void:
	var va: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data(2))
	var vb: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data(3))
	factory.clear_sprite_effects()
	assert_eq(factory.clear_active_bullets(), 5, "returns total live bullets")
	assert_gte(factory.get_active_effect_count(), 5, "On Clear fired per bullet")
	assert_eq(factory.debug_get_active_bullets_amount(), 0, "no active volleys left")
	assert_gte(factory.debug_get_bullets_pool_amount(), 2, "emptied volleys parked pooled")
	assert_false(va.is_bullet_status_enabled(0))
	assert_false(vb.is_bullet_status_enabled(0))


func test_free_active_bullets_is_silent() -> void:
	factory.spawn_controllable_directional_bullets(_data(2))
	factory.clear_sprite_effects()
	factory.free_active_bullets()
	assert_eq(factory.get_active_effect_count(), 0, "free fires no On Clear")
	assert_eq(factory.debug_get_active_bullets_amount(), 0, "volleys freed")


func test_reset_is_silent() -> void:
	factory.spawn_controllable_directional_bullets(_data(2))
	factory.clear_sprite_effects()
	factory.reset()
	assert_eq(factory.get_active_effect_count(), 0, "reset fires no On Clear")


func test_key_scoped_clear() -> void:
	factory.spawn_controllable_directional_bullets(_data(2))
	var vb: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data(3))
	var key_a: MultiMeshPoolKey2D = factory.debug_expected_pool_key(_data(2))
	factory.clear_sprite_effects()
	assert_eq(factory.clear_active_bullets(key_a), 2, "scoped clear returns its bucket's bullets")
	assert_gte(factory.get_active_effect_count(), 2, "scoped clear fires visuals")
	assert_true(vb.is_bullet_status_enabled(0), "other bucket still live")
	assert_true(vb.is_bullet_status_enabled(2))


func test_deferred_twins() -> void:
	factory.spawn_controllable_directional_bullets(_data(2))
	factory.clear_sprite_effects()
	factory.clear_active_bullets_deferred()
	await idle()
	assert_gte(factory.get_active_effect_count(), 2, "deferred clear fired")
	factory.clear_sprite_effects()
	factory.spawn_controllable_directional_bullets(_data(2))
	factory.free_active_bullets_deferred()
	await idle()
	assert_eq(factory.debug_get_active_bullets_amount(), 0, "deferred free ran")
	assert_eq(factory.get_active_effect_count(), 0, "deferred free stayed silent")


func test_deferred_twin_from_inside_physics() -> void:
	# Regression: call_deferred from a physics callback flushes INSIDE the
	# physics frame, where structural calls are refused. The *_deferred
	# wrappers now queue onto the next idle frame.
	factory.spawn_controllable_directional_bullets(_data(2))
	await physics()
	assert_true(Engine.is_in_physics_frame(), "test resumes inside physics")
	factory.free_active_bullets_deferred()
	factory.reset_deferred()
	await idle()
	assert_eq(factory.debug_get_active_bullets_amount(), 0, "deferred free ran from physics")
