extends SceneTree
## Trail shard-refcount suite: per-shard occupancy without the O(N) scan.
##
## Contract under test: hide_trail_instances() hides a shard exactly when its
## last bullet leaves (frame switch or disable). Previously this rescanned
## every slot per disabled bullet (O(N^2) on full-volley expiry); now a
## per-shard live-bullet count decides in O(1) with identical semantics.
## A single-frame trail puts every bullet on shard 0, so two bullets share
## one shard and the ordering assertions below are deterministic.
##
## Covers: T1 both bullets tracked on shard 0 and shard visible, T2 disabling
## one bullet keeps the shard visible for the survivor, T3 disabling the last
## hides the shard, T4 wake re-tracks and re-shows, T5 no dangling.
##
## Run: godot --headless --path test_project --script tests/volley/test_volley_trail_refcount.gd
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
	var img := Image.create_empty(8, 8, false, Image.FORMAT_RGBA8)
	img.fill(Color(1, 1, 1, 1))
	sf.add_frame("default", ImageTexture.create_from_image(img))
	return sf

func _trail_layer() -> BulletEffectLayerData2D:
	var l := BulletEffectLayerData2D.new()
	l.trigger = 0 # EFFECT_TRAIL_FOLLOW
	l.sprite_frames = _make_frames()
	return l

func _data() -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	d.transforms = [Transform2D(0.0, Vector2.ZERO), Transform2D(0.0, Vector2(16, 0))]
	var speeds: Array = []
	for i in 2:
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
	d.effect_layers = [_trail_layer()]
	return d

func _bake_info(v: DirectionalBullets2D) -> Dictionary:
	var info: Dictionary = v.debug_get_effect_layers_info()
	var bakes: Array = info.get("trail_bakes", [])
	if bakes.is_empty():
		return {}
	return bakes[0]

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	# ---------------------------------------------------------------
	printerr("TRAILREF T1 both bullets share shard 0, shard visible")
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())
	for i in 4:
		await physics_frame
	_check(v.has_trail_effects(), "T1 trail baked")
	var b1: Dictionary = _bake_info(v)
	_check(int(b1.get("bullets_tracked", -1)) == 2, "T1 both bullets tracked (got %s)" % str(b1.get("bullets_tracked", -1)))
	_check(int(b1.get("shards_visible", -1)) >= 1, "T1 shard visible (got %s)" % str(b1.get("shards_visible", -1)))

	# ---------------------------------------------------------------
	printerr("TRAILREF T2 disabling one bullet keeps the shared shard")
	v.disable_bullet(0)
	await physics_frame
	var b2: Dictionary = _bake_info(v)
	_check(int(b2.get("bullets_tracked", -1)) == 1, "T2 one bullet tracked (got %s)" % str(b2.get("bullets_tracked", -1)))
	_check(int(b2.get("shards_visible", -1)) >= 1, "T2 shard stays visible for the survivor (got %s)" % str(b2.get("shards_visible", -1)))

	# ---------------------------------------------------------------
	printerr("TRAILREF T3 disabling the last bullet hides the shard")
	v.disable_bullet(1)
	await physics_frame
	var b3: Dictionary = _bake_info(v)
	_check(int(b3.get("bullets_tracked", -1)) == 0, "T3 no bullets tracked (got %s)" % str(b3.get("bullets_tracked", -1)))
	_check(int(b3.get("shards_visible", 1)) == 0, "T3 shard hidden (got %s)" % str(b3.get("shards_visible", 1)))
	_check(factory.debug_get_bullets_pool_amount(0) >= 1, "T3 volley pooled")

	# ---------------------------------------------------------------
	printerr("TRAILREF T4 wake re-tracks and re-shows")
	v.wake_bullet(0)
	v.wake_bullet(1)
	for i in 4:
		await physics_frame
	var b4: Dictionary = _bake_info(v)
	_check(int(b4.get("bullets_tracked", -1)) == 2, "T4 both bullets tracked again (got %s)" % str(b4.get("bullets_tracked", -1)))
	_check(int(b4.get("shards_visible", -1)) >= 1, "T4 shard visible again (got %s)" % str(b4.get("shards_visible", -1)))

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL TRAIL-REFCOUNT TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
