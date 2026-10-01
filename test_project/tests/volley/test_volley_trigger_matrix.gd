extends SceneTree
## Volley trigger-matrix suite: every effect trigger against the path that
## must fire it, across factory spawns, spawner shots, and pooled reuse.
##
## Trigger contract under test: On Spawn fires on every spawn (fresh AND
## pooled reuse); On Hit fires on counted non-killing hits; On Destroy fires
## on collision-kill only; On Bounce fires on ricochet; On Lifetime Over
## fires on expiry (both signal paths); On Clear fires on manual clear only.
## free_active_bullets()/reset() are silent teardown (pinned by
## test_factory_clear_triggers.gd, not here).
##
## Covers: T1 On Spawn via factory spawn, spawner shoot_once, and pooled
## reuse, T2 On Hit on a counted hit, T3 On Destroy on the killing blow
## (and never on timeout), T4 On Lifetime Over on expiry, T5 On Clear on
## clear_bullet only, T6 On Bounce on a wall bounce, T7 spawner-owned hits
## route to spawner signals (never factory), T8 no dangling.
##
## Run: godot --headless --path test_project --script tests/volley/test_volley_trigger_matrix.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0
var _spawner_vols: Array = []
var _factory_body_hits := 0
var _spawner_body_hits := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _on_spawner_volley(volley: Object, _idx: int) -> void:
	if volley is DirectionalBullets2D:
		_spawner_vols.append(volley)

func _on_factory_body(_body: Object, _vol: Object, _idx: int) -> void:
	_factory_body_hits += 1

func _on_spawner_body(_body: Object, _vol: Object, _idx: int) -> void:
	_spawner_body_hits += 1

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

func _layer(trigger: int) -> BulletEffectLayerData2D:
	var l := BulletEffectLayerData2D.new()
	l.trigger = trigger
	l.sprite_frames = _make_frames()
	return l

func _data(layers: Array, max_collisions: int = 0, lifetime: float = 30.0, bounce: bool = false) -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	d.transforms = [Transform2D(0.0, Vector2.ZERO)]
	var s := BulletSpeedData2D.new()
	s.speed = 900.0
	s.max_speed = 3000.0
	d.all_bullet_speed_data = [s]
	d.max_life_time = lifetime
	d.texture_size = Vector2(16, 16)
	d.monitorable = true
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array([4])
	var c := CircleShape2D.new()
	c.radius = 6.0
	d.collision_shape = c
	d.bullet_max_collision_count = max_collisions
	if bounce:
		d.set_bounce_mask_from_array([4])
	d.effect_layers = layers
	return d

func _make_wall(parent: Node, pos: Vector2) -> StaticBody2D:
	var wall := StaticBody2D.new()
	wall.position = pos
	wall.collision_layer = 8
	wall.collision_mask = 2
	var col := CollisionShape2D.new()
	var box := RectangleShape2D.new()
	box.size = Vector2(20, 400)
	col.shape = box
	wall.add_child(col)
	parent.add_child(wall)
	return wall

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	# ---------------------------------------------------------------
	printerr("MATRIX T1 On Spawn fires on factory spawn, spawner shot, reuse")
	var v1: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data([_layer(1)]))
	_check(factory.get_active_effect_count() >= 1, "T1 factory spawn flashes (active=%d)" % factory.get_active_effect_count())
	factory.clear_sprite_effects()
	factory.free_active_bullets()
	var spawner := BulletSpawner2D.new()
	get_root().add_child(spawner)
	spawner.set_bullet_factory(factory)
	spawner.set_spawn_data(_data([_layer(1)]))
	spawner.set_shooting_enabled(false)
	spawner.volley_fired.connect(_on_spawner_volley)
	_spawner_vols.clear()
	_check(spawner.shoot_once(), "T1 spawner shoot_once fires")
	_check(not _spawner_vols.is_empty(), "T1 volley_fired observed")
	_check(factory.get_active_effect_count() >= 1, "T1 spawner shot flashes (active=%d)" % factory.get_active_effect_count())
	factory.clear_sprite_effects()
	# Pooled reuse: disable everything (parks pooled), spawn again, the new
	# life must flash too (reseed fires spawn layers).
	factory.debug_reset_pool_stats()
	for vv in _spawner_vols:
		(vv as DirectionalBullets2D).clear_all_bullets()
	factory.clear_sprite_effects()
	_spawner_vols.clear()
	_check(spawner.shoot_once(), "T1 second spawner shot fires")
	var stats: Dictionary = factory.debug_get_pool_hit_stats()
	_check(int(stats.get("directional_hits", 0)) >= 1, "T1 second shot reused the pool (hits=%s)" % str(stats.get("directional_hits", 0)))
	_check(factory.get_active_effect_count() >= 1, "T1 pooled reuse flashes again (active=%d)" % factory.get_active_effect_count())
	factory.clear_sprite_effects()
	factory.free_active_bullets()
	await process_frame

	# ---------------------------------------------------------------
	printerr("MATRIX T2 On Hit fires on a counted non-killing hit")
	var wall := _make_wall(get_root(), Vector2(200, 0))
	await physics_frame
	var v2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data([_layer(2)], 0))
	factory.clear_sprite_effects()
	var hit_seen := false
	for i in 40:
		await physics_frame
		if v2.get_bullet_collision_count(0) >= 1:
			hit_seen = true
			break
	_check(hit_seen, "T2 bullet registered the hit")
	_check(factory.get_active_effect_count() >= 1, "T2 On Hit sparked (active=%d)" % factory.get_active_effect_count())
	factory.clear_sprite_effects()
	factory.free_active_bullets()
	await process_frame

	# ---------------------------------------------------------------
	printerr("MATRIX T3 On Destroy fires on kill, never on timeout")
	var v3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data([_layer(3)], 1))
	factory.clear_sprite_effects()
	for i in 40:
		await physics_frame
		if not v3.is_bullet_status_enabled(0):
			break
	_check(not v3.is_bullet_status_enabled(0), "T3 killing blow disabled the bullet")
	_check(factory.get_active_effect_count() >= 1, "T3 On Destroy detonated (active=%d)" % factory.get_active_effect_count())
	factory.clear_sprite_effects()
	factory.free_active_bullets()
	await process_frame
	var v3b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data([_layer(3)], 0, 0.3))
	factory.clear_sprite_effects()
	for i in 40:
		await physics_frame
		if not v3b.is_bullet_status_enabled(0):
			break
	_check(not v3b.is_bullet_status_enabled(0), "T3b timeout disabled the bullet")
	_check(factory.get_active_effect_count() == 0, "T3b timeout never detonates")
	factory.clear_sprite_effects()
	factory.free_active_bullets()
	await process_frame

	# ---------------------------------------------------------------
	printerr("MATRIX T4 On Lifetime Over fires on expiry")
	var v4: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data([_layer(5)], 0, 0.3))
	factory.clear_sprite_effects()
	var expired_seen := false
	for i in 60:
		await physics_frame
		if factory.get_active_effect_count() >= 1:
			expired_seen = true
			break
	_check(expired_seen, "T4 On Lifetime Over fizzled on expiry")
	factory.clear_sprite_effects()
	factory.free_active_bullets()
	await process_frame

	# ---------------------------------------------------------------
	printerr("MATRIX T5 On Clear fires on clear_bullet only")
	var v5: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data([_layer(6)], 0, 60.0))
	factory.clear_sprite_effects()
	_check(v5.clear_bullet(0), "T5 clear_bullet returns true")
	_check(factory.get_active_effect_count() >= 1, "T5 On Clear fired (active=%d)" % factory.get_active_effect_count())
	factory.clear_sprite_effects()
	factory.free_active_bullets()
	await process_frame

	# ---------------------------------------------------------------
	printerr("MATRIX T6 On Bounce fires on a wall bounce")
	var v6: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data([_layer(4)], 0, 60.0, true))
	factory.clear_sprite_effects()
	var bounced := false
	for i in 60:
		await physics_frame
		if v6.bullet_get_bounce_count(0) >= 1:
			bounced = true
			break
	_check(bounced, "T6 bullet bounced")
	_check(factory.get_active_effect_count() >= 1, "T6 On Bounce sparked (active=%d)" % factory.get_active_effect_count())
	factory.clear_sprite_effects()
	factory.free_active_bullets()
	wall.queue_free()
	await process_frame

	# ---------------------------------------------------------------
	printerr("MATRIX T7 spawner-owned hits route to spawner signals only")
	var wall7 := _make_wall(get_root(), Vector2(200, 0))
	await physics_frame
	factory.directional_body_entered.connect(_on_factory_body)
	spawner.body_entered.connect(_on_spawner_body)
	_factory_body_hits = 0
	_spawner_body_hits = 0
	_spawner_vols.clear()
	spawner.set_spawn_data(_data([], 0, 60.0))
	_check(spawner.shoot_once(), "T7 spawner shot fires")
	for i in 40:
		await physics_frame
		if _spawner_body_hits >= 1:
			break
	_check(_spawner_body_hits >= 1, "T7 spawner body_entered fired")
	_check(_factory_body_hits == 0, "T7 factory signal stayed silent for a spawner volley (got %d)" % _factory_body_hits)
	factory.directional_body_entered.disconnect(_on_factory_body)
	spawner.body_entered.disconnect(_on_spawner_body)
	wall7.queue_free()
	factory.clear_sprite_effects()
	factory.free_active_bullets()

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	spawner.queue_free()
	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL TRIGGER-MATRIX TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
