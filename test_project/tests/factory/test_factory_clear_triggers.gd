extends BlastTest
## Factory clear contract: clear_bullet/clear_active_bullets fire On Clear
## visuals and park emptied volleys pooled; free_active_bullets()/reset()
## stay silent teardown; key-scoped clears touch only their bucket; deferred
## twins run on the next idle frame.

var layer: EffectLayerData2D


func before_each() -> void:
	await super()
	layer = EffectLayerData2D.new()
	layer.trigger = EffectLayerData2D.EFFECT_ON_CLEAR
	var sf := SpriteFrames.new()
	sf.set_animation_speed("default", 10.0)
	sf.set_animation_loop("default", false)
	for i in 4:
		var img := Image.create_empty(8, 8, false, Image.FORMAT_RGBA8)
		img.fill(Color.WHITE)
		sf.add_frame("default", ImageTexture.create_from_image(img))
	layer.sprite_frames = sf


func _data(bullets: int) -> BulletVolleyData2D:
	var d := H.make_volley_data(bullets, 0.0, 60.0)
	var transforms: Array = []
	for i in bullets:
		transforms.append(Transform2D(0.0, Vector2(i * 8.0, 0.0)))
	d.transforms = transforms
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 0
	d.effect_layers = [layer]
	return d


func test_clear_bullet_fires_once() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(2))
	factory.clear_sprite_effects()
	assert_eq(factory.get_active_effect_count(), 0, "quiet before clear")
	assert_true(v.clear_bullet(0), "clear_bullet on a live slot returns true")
	assert_eq(factory.get_active_effect_count(), 1, "On Clear fired exactly once")
	assert_false(v.clear_bullet(0), "double clear stays silent")
	assert_eq(factory.get_active_effect_count(), 1, "and spawned no second effect")


func test_clear_active_bullets_clears_every_volley() -> void:
	var va: BulletVolley2D = factory.spawn_volley(_data(2))
	var vb: BulletVolley2D = factory.spawn_volley(_data(3))
	factory.clear_sprite_effects()
	assert_eq(factory.clear_active_bullets(), 5, "returns total live bullets")
	assert_gte(factory.get_active_effect_count(), 5, "On Clear fired per bullet")
	assert_eq(factory.debug_get_active_volleys_amount(), 0, "no active volleys left")
	assert_gte(factory.debug_get_bullets_pool_amount(), 2, "emptied volleys parked pooled")
	assert_false(va.is_bullet_status_enabled(0))
	assert_false(vb.is_bullet_status_enabled(0))


func test_free_active_bullets_is_silent() -> void:
	factory.spawn_volley(_data(2))
	factory.clear_sprite_effects()
	factory.free_active_bullets()
	assert_eq(factory.get_active_effect_count(), 0, "free fires no On Clear")
	assert_eq(factory.debug_get_active_volleys_amount(), 0, "volleys freed")


func test_reset_is_silent() -> void:
	factory.spawn_volley(_data(2))
	factory.clear_sprite_effects()
	factory.reset()
	assert_eq(factory.get_active_effect_count(), 0, "reset fires no On Clear")


func test_key_scoped_clear() -> void:
	factory.spawn_volley(_data(2))
	var vb: BulletVolley2D = factory.spawn_volley(_data(3))
	var key_a: VolleyPoolKey2D = BulletFactory2D.debug_expected_pool_key(_data(2))
	factory.clear_sprite_effects()
	assert_eq(factory.clear_active_bullets(key_a), 2, "scoped clear returns its bucket's bullets")
	assert_gte(factory.get_active_effect_count(), 2, "scoped clear fires visuals")
	assert_true(vb.is_bullet_status_enabled(0), "other bucket still live")
	assert_true(vb.is_bullet_status_enabled(2))


func test_deferred_twins() -> void:
	factory.spawn_volley(_data(2))
	factory.clear_sprite_effects()
	factory.clear_active_bullets_deferred()
	await idle()
	assert_gte(factory.get_active_effect_count(), 2, "deferred clear fired")
	factory.clear_sprite_effects()
	factory.spawn_volley(_data(2))
	factory.free_active_bullets_deferred()
	await idle()
	assert_eq(factory.debug_get_active_volleys_amount(), 0, "deferred free ran")
	assert_eq(factory.get_active_effect_count(), 0, "deferred free stayed silent")


func test_deferred_twin_from_inside_physics() -> void:
	# Regression: call_deferred from a physics callback flushes INSIDE the
	# physics frame, where structural calls are refused. The *_deferred
	# wrappers now queue onto the next idle frame.
	factory.spawn_volley(_data(2))
	await physics()
	assert_true(Engine.is_in_physics_frame(), "test resumes inside physics")
	factory.free_active_bullets_deferred()
	factory.reset_deferred()
	await idle()
	assert_eq(factory.debug_get_active_volleys_amount(), 0, "deferred free ran from physics")


func _shaped(bullets: int, shape: Shape2D) -> BulletVolleyData2D:
	var d := H.make_volley_data(bullets, 0.0, 60.0)
	var transforms: Array = []
	for i in bullets:
		transforms.append(Transform2D(0.0, Vector2(i * 8.0, 0.0)))
	d.transforms = transforms
	d.collision_shape = shape
	d.bullet_max_collision_count = 0
	return d


## free_disabled_bullets() drops what is already out of play: the POOLED
## volley is destroyed (the pool shrinks), a PARKED volley and the live
## volleys are untouched, and nothing is announced as cleared.
func test_free_disabled_bullets_drops_pooled_but_not_parked_or_active() -> void:
	watch_signals(factory)
	var circle := H.make_circle_shape(6.0)
	var active: BulletVolley2D = factory.spawn_volley(_shaped(2, circle))
	var parked: BulletVolley2D = factory.spawn_volley(_shaped(2, circle))
	parked.set_is_auto_pooling_enabled(false)
	parked.disable_bullet(0)
	parked.disable_bullet(1)
	var pooled: BulletVolley2D = factory.spawn_volley(_shaped(2, circle))
	pooled.disable_bullet(0)
	pooled.disable_bullet(1)
	await idle(1)
	assert_true(pooled.is_pooled(), "one volley is pooled")
	assert_eq(parked.debug_get_life_state(), "parked", "one volley is parked")
	assert_eq(factory.debug_get_bullets_pool_amount(), 1, "the pool holds the pooled one")
	var total_before: int = int(factory.debug_get_factory_state()["volleys_total"])
	assert_eq(total_before, 3, "three volleys in the factory")
	factory.free_disabled_bullets()
	await idle(1)
	assert_eq(factory.debug_get_bullets_pool_amount(), 0, "the pooled volley is gone")
	assert_eq(int(factory.debug_get_factory_state()["volleys_total"]), total_before - 1, "and only it was dropped")
	assert_true(is_instance_valid(parked), "the parked volley survives")
	assert_eq(parked.debug_get_life_state(), "parked", "still parked")
	assert_true(is_instance_valid(active) and active.is_volley_active(), "the active volley is untouched")
	assert_eq(active.get_active_bullet_count(), 2, "with both bullets live")
	assert_signal_emit_count(factory, "bullets_cleared", 0, "no clear is announced")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "books ok")


## free_active_bullets(key) is scoped by the (amount, shape) pool key: only the
## volley of that bucket dies, a volley of another shape and a PARKED volley
## of the same shape stay exactly as they were.
func test_free_active_bullets_by_key_spares_the_other_buckets_and_parked() -> void:
	var circle := H.make_circle_shape(6.0)
	var rect := RectangleShape2D.new()
	rect.size = Vector2(14, 10)
	var a: BulletVolley2D = factory.spawn_volley(_shaped(2, circle))
	var b: BulletVolley2D = factory.spawn_volley(_shaped(2, rect))
	var c: BulletVolley2D = factory.spawn_volley(_shaped(2, circle))
	c.set_is_auto_pooling_enabled(false)
	c.disable_bullet(0)
	c.disable_bullet(1)
	assert_eq(c.debug_get_life_state(), "parked", "C is parked")
	var survivors: Array = [b, c]
	factory.free_active_bullets(VolleyPoolKey2D.make(2, PhysicsServer2D.SHAPE_CIRCLE))
	await idle(1)
	assert_false(is_instance_valid(a), "the circle-bucket active volley is destroyed")
	for s in survivors:
		assert_true(is_instance_valid(s), "the other spawns survive")
	assert_true(b.is_volley_active(), "B (rectangle key) keeps flying")
	assert_eq(b.get_active_bullet_count(), 2, "with both bullets")
	assert_eq(c.debug_get_life_state(), "parked", "C is still parked, not destroyed")
	assert_eq(factory.debug_get_active_volleys_amount(), 1, "only B is active")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "books ok")


## reset() is the hard teardown: it destroys the live volleys WITHOUT pooling
## them ({ } pool before and after), reports reset_finished exactly once, and
## the first spawn of that key afterwards is a MISS (nothing was kept).
func test_reset_leaves_an_empty_pool_and_the_next_spawn_misses() -> void:
	watch_signals(factory)
	var circle := H.make_circle_shape(6.0)
	factory.spawn_volley(_shaped(2, circle))
	factory.spawn_volley(_shaped(2, circle))
	assert_eq(factory.debug_get_bullets_pool_info(), {}, "nothing is pooled before the reset")
	factory.debug_reset_pool_stats()
	factory.reset()
	await idle(1)
	assert_signal_emit_count(factory, "reset_finished", 1, "reset_finished fires exactly once")
	assert_eq(factory.debug_get_active_volleys_amount(), 0, "no active volleys left")
	assert_eq(factory.debug_get_bullets_pool_info(), {}, "the reset pooled nothing")
	assert_eq(factory.debug_get_bullets_pool_amount(), 0, "the pool is empty")
	factory.spawn_volley(_shaped(2, circle))
	assert_eq(factory.debug_get_pool_hit_stats().get("misses", 0), 1, "the first spawn after the reset is a miss")
	assert_eq(factory.debug_get_pool_hit_stats().get("hits", 0), 0, "and not a hit")
