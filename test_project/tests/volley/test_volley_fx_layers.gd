extends SceneTree
## Volley sprite-effect layers suite: REAL physics (no mocks).
## Covers: custom-frame validation, ON_SPAWN flash fires + expires, trail
## follows + dies on disable + resumes on wake, hit sparks on counted hits,
## destroy explosion on collision-kill only (never on timeout), trigger
## chance gate, opt-in desync across shards, per-bullet trail toggles,
## play_effect_animation switch, pool-reuse reseed, ring caps (explicit +
## auto), 24-frame bake cap, applied z/tint/texture/visibility/light,
## bounce sparks, block-volley trails, spawner integration, manual hatch +
## clear, interpolation agreement, ramp tints, volley self_modulate,
## rotation offsets + spin (trail + one-shot).
## Run: godot --headless --path test_project --script tests/volley/test_volley_fx_layers.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _fx_data(from: Vector2, speed: float, layers: Array, bounce: bool = true) -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	d.transforms = [Transform2D(0.0, from)]
	var sp := BulletSpeedData2D.new()
	sp.speed = speed
	sp.max_speed = 3000.0
	sp.acceleration = 0.0
	d.all_bullet_speed_data = [sp]
	d.max_life_time = 8.0
	d.texture_size = Vector2(16, 16)
	d.monitorable = true
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array([4])
	if bounce:
		d.set_bounce_mask_from_array([4])
	d.effect_layers = layers
	return d

func _make_wall(pos: Vector2) -> StaticBody2D:
	var wall := StaticBody2D.new()
	wall.position = pos
	wall.collision_layer = 8
	wall.collision_mask = 2
	var col := CollisionShape2D.new()
	var box := RectangleShape2D.new()
	box.size = Vector2(20, 400)
	col.shape = box
	wall.add_child(col)
	get_root().add_child(wall)
	return wall

func _settle(factory: BulletFactory2D) -> void:
	await process_frame
	await process_frame
	factory.free_active_bullets()
	factory.clear_sprite_effects()
	await physics_frame

func _make_frames(count: int, anims: Array = ["default"]) -> SpriteFrames:
	var sf := SpriteFrames.new()
	for a in anims:
		if not sf.has_animation(a):
			sf.add_animation(a)
		sf.set_animation_speed(a, 10.0)
		sf.set_animation_loop(a, false)
		for i in count:
			var img := Image.create_empty(8, 8, false, Image.FORMAT_RGBA8)
			img.fill(Color(1, 1, 1, 1))
			sf.add_frame(a, ImageTexture.create_from_image(img))
	return sf

func _make_layer(trigger: int, count: int = 4, anims: Array = ["default"]) -> BulletEffectLayerData2D:
	var l := BulletEffectLayerData2D.new()
	l.trigger = trigger
	l.sprite_frames = _make_frames(count, anims)
	return l

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	var wall := _make_wall(Vector2(200, 0))
	await physics_frame

	printerr("FX T1 validation: inert layers stay silent, rejects hold")
	var dead := BulletEffectLayerData2D.new()
	_check(dead.random_start_frame == false, "desync off by default")
	_check(dead.max_instances == 0, "auto cap by default")
	_check(factory.spawn_layer_effect(dead, Transform2D.IDENTITY) == -1, "null-frames layer inert, no crash")
	dead.trigger = 9
	_check(dead.trigger == 0, "bad trigger rejected")
	dead.set_trigger_chance(2.0)
	_check(dead.trigger_chance == 1.0, "chance > 1 rejected")
	dead.set_trigger_chance(NAN)
	_check(dead.trigger_chance == 1.0, "NaN chance rejected")
	dead.set_max_instances(-1)
	_check(dead.max_instances == 0, "negative cap rejected")
	dead.set_max_instances(0)
	_check(dead.max_instances == 0, "auto cap (0) accepted")
	dead.set_random_scale_min(5.0)
	_check(dead.random_scale_min == 1.0, "scale min above max rejected")
	dead.set_rotation_degrees(NAN)
	_check(dead.rotation_degrees == 0.0, "NaN rotation rejected")
	dead.set_spin_degrees_per_sec(NAN)
	_check(dead.spin_degrees_per_sec == 0.0, "NaN spin rejected")
	var off := _make_layer(3, 4)
	off.enabled = false
	_check(factory.spawn_layer_effect(off, Transform2D.IDENTITY) == -1, "disabled layer never fires")
	_check(factory.spawn_layer_effect(null, Transform2D.IDENTITY) == -1, "null layer rejected")

	printerr("FX T2 spawn flash fires once + expires")
	await _settle(factory)
	var v2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 300.0, [_make_layer(1, 4)]))
	await physics_frame
	_check(factory.get_active_effect_count() >= 1, "flash alive right after spawn")
	for i in 30:
		await physics_frame
	_check(factory.get_active_effect_count() == 0, "flash expired on schedule (0.4s)")

	printerr("FX T3 trail follows + dies on disable + resumes on wake")
	await _settle(factory)
	var v3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 200.0, [_make_layer(0, 1)]))
	_check(v3.has_trail_effects(), "trail baked")
	for i in 5:
		await physics_frame
	var bt: Vector2 = v3.get_bullet_transform(0).origin
	var tt: Vector2 = v3.debug_get_trail_transform(0, 0).origin
	_check(tt.distance_to(bt) < 0.01, "trail sits on the bullet")
	v3.disable_bullet(0)
	_check(v3.debug_get_trail_transform(0, 0) == Transform2D(), "disable zeroes the trail")
	v3.enable_bullet(0)
	await physics_frame
	await physics_frame
	_check(v3.debug_get_trail_transform(0, 0).origin.distance_to(v3.get_bullet_transform(0).origin) < 0.01, "wake resumes the trail")

	printerr("FX T4 destroy explosion on kill only, chance gate")
	await _settle(factory)
	var d4 := _fx_data(Vector2.ZERO, 300.0, [_make_layer(3, 8)], false)
	d4.set_bullet_max_collision_count(1)
	var v4: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d4)
	for i in 120:
		await physics_frame
		if not v4.is_bullet_status_enabled(0):
			break
	_check(not v4.is_bullet_status_enabled(0), "wall killed the bullet")
	for i in 3:
		await physics_frame
	_check(factory.get_active_effect_count() >= 1, "kill detonated")
	for i in 60:
		await physics_frame
	_check(factory.get_active_effect_count() == 0, "explosion expired (0.8s)")
	await _settle(factory)
	var shy := _make_layer(3, 4)
	shy.trigger_chance = 0.0
	var d4b := _fx_data(Vector2.ZERO, 300.0, [shy], false)
	d4b.set_bullet_max_collision_count(1)
	var v4b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d4b)
	for i in 120:
		await physics_frame
		if not v4b.is_bullet_status_enabled(0):
			break
	for i in 3:
		await physics_frame
	_check(factory.get_active_effect_count() == 0, "chance 0 never fires")
	await _settle(factory)
	var d4c := _fx_data(Vector2.ZERO, 0.0, [_make_layer(3, 4)], false)
	d4c.max_life_time = 0.2
	var v4c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d4c)
	for i in 60:
		await physics_frame
	_check(factory.get_active_effect_count() == 0, "timeout never detonates")

	printerr("FX T5 hit sparks on counted non-killing hits")
	await _settle(factory)
	var d5 := _fx_data(Vector2.ZERO, 300.0, [_make_layer(2, 6)], false)
	d5.set_bullet_max_collision_count(0)
	var v5: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d5)
	for i in 120:
		await physics_frame
		if v5.get_bullet_collision_count(0) >= 1:
			break
	_check(v5.get_bullet_collision_count(0) >= 1, "through-shot counted (max 0)")
	_check(factory.get_active_effect_count() >= 1, "counted hit sparked")

	printerr("FX T6 opt-in desync: same-tick kills span shards")
	await _settle(factory)
	seed(12345)
	var dd := DirectionalBulletsData2D.new()
	var arr: Array = []
	for i in 6:
		arr.append(Transform2D(0.0, Vector2.ZERO))
	dd.transforms = arr
	var speeds: Array = []
	for i in 6:
		var sp6 := BulletSpeedData2D.new()
		sp6.speed = 300.0
		sp6.max_speed = 3000.0
		speeds.append(sp6)
	dd.all_bullet_speed_data = speeds
	dd.max_life_time = 8.0
	dd.texture_size = Vector2(16, 16)
	dd.monitorable = true
	dd.set_collision_layer_from_array([2])
	dd.set_collision_mask_from_array([4])
	var big := _make_layer(3, 8)
	big.random_start_frame = true
	var c := CircleShape2D.new()
	c.radius = 6.0
	dd.collision_shape = c
	dd.effect_layers = [big]
	dd.set_bullet_max_collision_count(1)
	var v6: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(dd)
	for i in 120:
		await physics_frame
		if factory.debug_get_effect_state().get("active_total", 0) >= 1:
			break
	await physics_frame
	var busy := 0
	for bake in factory.debug_get_effect_state().get("bakes", []):
		busy = maxi(busy, int(bake.get("shards_busy", 0)))
	_check(busy >= 2, "six same-tick kills desync across shards (busy %d)" % busy)
	await _settle(factory)
	var dd6b := _fx_data(Vector2.ZERO, 300.0, [_make_layer(3, 8)], false)
	dd6b.set_bullet_max_collision_count(1)
	var v6b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(dd6b)
	for i in 120:
		await physics_frame
		if not v6b.is_bullet_status_enabled(0):
			break
	await physics_frame
	var busy6b := 0
	for bake in factory.debug_get_effect_state().get("bakes", []):
		busy6b = maxi(busy6b, int(bake.get("shards_busy", 0)))
	_check(busy6b <= 1, "default sync parks one kill on one shard")

	printerr("FX T7 per-bullet toggles + animation switch")
	await _settle(factory)
	var dd7 := _fx_data(Vector2.ZERO, 200.0, [_make_layer(0, 1)])
	dd7.transforms = [Transform2D(0.0, Vector2.ZERO), Transform2D(0.0, Vector2(0, 40))]
	var s7a := BulletSpeedData2D.new()
	s7a.speed = 200.0
	s7a.max_speed = 3000.0
	dd7.all_bullet_speed_data = [s7a, s7a]
	var v7: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(dd7)
	for i in 5:
		await physics_frame
	v7.bullet_set_trail_enabled(0, 0, false)
	_check(v7.debug_get_trail_transform(0, 0) == Transform2D(), "bullet 0 trail off")
	_check(v7.debug_get_trail_transform(0, 1).origin.distance_to(v7.get_bullet_transform(1).origin) < 0.01, "bullet 1 trail on")
	v7.all_bullets_set_trail_enabled(0, false)
	_check(v7.debug_get_trail_transform(0, 1) == Transform2D(), "range toggle hides all")
	v7.all_bullets_set_trail_enabled(0, true)
	await physics_frame
	_check(v7.debug_get_trail_transform(0, 1).origin.distance_to(v7.get_bullet_transform(1).origin) < 0.01, "range toggle restores")
	var custom := BulletEffectLayerData2D.new()
	custom.trigger = 0
	custom.sprite_frames = _make_frames(1, ["a", "b"])
	var v7b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 200.0, [custom]))
	_check(v7b.has_trail_effects(), "custom frames bake as trail")
	_check(v7b.play_effect_animation(0, "b"), "animation switch accepted")
	_check(not v7b.play_effect_animation(0, "nope"), "unknown animation rejected")
	_check(not v7b.play_effect_animation(9, "b"), "unknown layer rejected")

	printerr("FX T8 pool reuse reseeds + caps hold (explicit + auto)")
	await _settle(factory)
	var v8: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 200.0, [_make_layer(1, 4), _make_layer(0, 1)]))
	await physics_frame
	_check(factory.get_active_effect_count() >= 1, "first life flashes")
	factory.free_active_bullets()
	await process_frame
	await process_frame
	var v8b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 200.0, [_make_layer(1, 4)]))
	await physics_frame
	_check(factory.get_active_effect_count() >= 1, "pooled reuse flashes again")
	_check(not v8b.has_trail_effects(), "reseed without trail drops it")
	await _settle(factory)
	var capped := _make_layer(3, 4)
	capped.max_instances = 2
	for i in 5:
		factory.spawn_layer_effect(capped, Transform2D(0.0, Vector2(i * 10, 0)))
	_check(factory.get_active_effect_count() <= 2, "explicit ring cap holds at 2")
	factory.clear_sprite_effects()
	var auto_small := _make_layer(1, 2)
	var v8c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 200.0, [auto_small]))
	var auto_slots := -1
	for bake in factory.debug_get_effect_state().get("bakes", []):
		auto_slots = int(bake.get("slots", -1))
	_check(auto_slots == 32, "auto cap floors at 32 for a 1-bullet volley (got %d)" % auto_slots)
	await _settle(factory)
	var wide := BulletEffectLayerData2D.new()
	wide.trigger = 0
	wide.sprite_frames = _make_frames(30)
	var v8d: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 200.0, [wide]))
	var baked_frames := -1
	for bake in v8d.debug_get_effect_layers_info().get("trail_bakes", []):
		baked_frames = int(bake.get("frames", -1))
	_check(baked_frames == 24, "30-frame anim truncates to the 24-frame bake cap")

	printerr("FX T9 styling: z, tint, texture, visibility, light + bounce + block")
	await _settle(factory)
	var high := _make_layer(0, 1)
	high.z_index = 5
	high.self_modulate = Color(1, 0.25, 0.1)
	high.visibility_layer = 4
	high.light_mask = 8
	var v9: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 200.0, [high]))
	var got_z := -99
	var got_rel := false
	var got_mod := Color(0, 0, 0, 0)
	var got_tex := false
	var got_vis := -1
	var got_light := -1
	for bake in v9.debug_get_effect_layers_info().get("trail_bakes", []):
		got_z = int(bake.get("z_index", -99))
		got_rel = bool(bake.get("z_as_relative", false))
		got_mod = bake.get("modulate", Color(0, 0, 0, 0))
		got_tex = bool(bake.get("shard_texture_valid", false))
		got_vis = int(bake.get("visibility_layer", -1))
		got_light = int(bake.get("light_mask", -1))
	_check(got_z == 5 and got_rel, "shard carries layer z (+5, relative)")
	_check(got_mod.is_equal_approx(Color(1, 0.25, 0.1)), "shard carries layer tint")
	_check(got_tex, "shard carries the baked texture")
	_check(got_vis == 4 and got_light == 8, "shard carries visibility + light masks")
	await _settle(factory)
	var bspark := _make_layer(4, 6)
	var db := _fx_data(Vector2.ZERO, 300.0, [bspark])
	db.bounce_strength = 1.0
	var vb: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(db)
	for i in 120:
		await physics_frame
		if vb.bullet_get_bounce_count(0) >= 1:
			break
	_check(vb.bullet_get_bounce_count(0) >= 1, "wall bounced")
	_check(factory.get_active_effect_count() >= 1, "bounce sparked")
	await _settle(factory)
	var bd := H.make_block_data(2, 150.0, 10.0)
	bd.effect_layers = [_make_layer(0, 1)]
	factory.spawn_block_bullets(bd)
	await physics_frame
	await physics_frame
	await physics_frame
	var found := false
	var follows := false
	for kid in factory.get_node("BlockBulletsContainer").get_children():
		if kid is BlockBullets2D and kid.has_trail_effects():
			found = true
			var t0: Vector2 = kid.debug_get_trail_transform(0, 0).origin
			var b0: Vector2 = kid.get_bullet_transform(0).origin
			follows = t0.distance_to(b0) < 0.01
	_check(found, "block volley bakes trails")
	_check(follows, "block trail follows")

	printerr("FX T10 spawner integration + manual hatch")
	await _settle(factory)
	var spawner := BulletSpawner2D.new()
	spawner.bullet_factory_path = factory.get_path()
	spawner.shooting_enabled = false
	spawner.pattern_source = BulletSpawner2D.PATTERN_FROM_SELF
	spawner.position = Vector2.ZERO
	get_root().add_child(spawner)
	await process_frame
	spawner.spawn_data = _fx_data(Vector2.ZERO, 200.0, [_make_layer(1, 4), _make_layer(0, 1)], false)
	_check(spawner.shoot_once(), "spawner volley fired")
	await physics_frame
	await physics_frame
	_check(factory.get_active_effect_count() >= 1, "spawner volley flashed")
	spawner.queue_free()
	await process_frame
	var ring := _make_layer(3, 4)
	_check(factory.spawn_layer_effect(ring, Transform2D(0.0, Vector2(300, 0))) >= 0, "manual hatch fires")
	_check(factory.get_active_effect_count() >= 1, "manual effect live")
	factory.clear_sprite_effects()
	_check(factory.get_active_effect_count() == 0, "manual clear stops all")

	printerr("FX T11 interpolation agreement")
	await _settle(factory)
	factory.use_physics_interpolation = true
	var v11: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 0.0, [_make_layer(0, 1)], false))
	v11.set_bullet_transform(0, Transform2D(0.0, Vector2(100, 0)))
	v11.debug_run_interpolation_pass()
	_check(v11.debug_get_trail_transform(0, 0).origin.distance_to(Vector2(100, 0)) < 0.01, "interp pass writes trail with zero physics ticks")
	v11.set_bullet_transform(0, Transform2D(0.0, Vector2(200, 0)))
	v11.debug_run_interpolation_pass()
	_check(v11.debug_get_trail_transform(0, 0).origin.distance_to(Vector2(200, 0)) < 0.01, "interp pass tracks teleports")
	_check(v11.debug_get_trail_transform(0, 0).is_finite(), "interp trail finite")
	factory.use_physics_interpolation = false
	var v11b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 0.0, [_make_layer(0, 1)], false))
	v11b.set_bullet_transform(0, Transform2D(0.0, Vector2(100, 0)))
	v11b.debug_run_interpolation_pass()
	_check(v11b.debug_get_trail_transform(0, 0) == Transform2D(), "interp pass gated off when disabled")

	printerr("FX T12 ramp tints over life")
	await _settle(factory)
	var ramp := Gradient.new()
	ramp.set_color(0, Color(1, 1, 1, 1))
	ramp.set_color(1, Color(1, 0, 0, 1))
	var hot := _make_layer(3, 10)
	hot.color_ramp = ramp
	var d12 := _fx_data(Vector2.ZERO, 300.0, [hot], false)
	d12.set_bullet_max_collision_count(1)
	var v12: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d12)
	for i in 120:
		await physics_frame
		if not v12.is_bullet_status_enabled(0):
			break
	var birth_tint := Color(-1, -1, -1, -1)
	for bake in factory.debug_get_effect_state().get("bakes", []):
		for row in bake.get("live_slots", []):
			birth_tint = row.get("tint", birth_tint)
	_check(birth_tint.r > 0.9 and birth_tint.g > 0.9 and birth_tint.b > 0.9, "ramp starts near-white (got %s)" % str(birth_tint))
	for i in 30:
		await physics_frame
	var mid_tint := Color(-1, -1, -1, -1)
	for bake in factory.debug_get_effect_state().get("bakes", []):
		for row in bake.get("live_slots", []):
			mid_tint = row.get("tint", mid_tint)
	_check(mid_tint.r > 0.9 and mid_tint.g < 0.75 and mid_tint.b < 0.75, "ramp reddens mid-life (got %s)" % str(mid_tint))
	await _settle(factory)
	var cool := _make_layer(0, 10)
	cool.color_ramp = ramp
	var v12b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 0.0, [cool], false))
	for i in 35:
		await physics_frame
	var trail_tint := Color(-1, -1, -1, -1)
	for bake in v12b.debug_get_effect_layers_info().get("trail_bakes", []):
		for row in bake.get("live_trails", []):
			trail_tint = row.get("tint", trail_tint)
	_check(trail_tint.r > 0.9 and trail_tint.g < 0.9, "trail loop-phase tint applies (got %s)" % str(trail_tint))

	printerr("FX T13 volley self_modulate applies + reseeds clean")
	await _settle(factory)
	var d13 := _fx_data(Vector2.ZERO, 200.0, [], false)
	d13.self_modulate = Color(1, 0.2, 0.2)
	var v13: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d13)
	_check(v13.debug_get_volley_info().get("self_modulate", Color(0, 0, 0, 0)).is_equal_approx(Color(1, 0.2, 0.2)), "volley tint applied from data")
	factory.free_active_bullets()
	await process_frame
	await process_frame
	var d13b := _fx_data(Vector2.ZERO, 200.0, [], false)
	var v13b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d13b)
	_check(v13b.debug_get_volley_info().get("self_modulate", Color(0, 0, 0, 0)).is_equal_approx(Color(1, 1, 1, 1)), "pool reuse resets tint to white")

	printerr("FX T14 rotation offsets + spin")
	await _settle(factory)
	var turned := _make_layer(0, 1)
	turned.rotation_degrees = 90.0
	var v14c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 200.0, [turned], false))
	for i in 3:
		await physics_frame
	_check(absf(absf(wrapf(v14c.debug_get_trail_transform(0, 0).get_rotation(), -PI, PI)) - PI / 2.0) < 0.08, "90-degree offset rotates the trail")
	await _settle(factory)
	var spinner := _make_layer(0, 1)
	spinner.spin_degrees_per_sec = 180.0
	var v14d: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 0.0, [spinner], false))
	for i in 30:
		await physics_frame
	var spin_rot: float = absf(wrapf(v14d.debug_get_trail_transform(0, 0).get_rotation(), -PI, PI))
	_check(absf(spin_rot - PI / 2.0) < 0.2, "180dps trail spun a quarter turn in 0.5s (got %.2f)" % spin_rot)
	await _settle(factory)
	var boom_spin := _make_layer(3, 4)
	boom_spin.spin_degrees_per_sec = 360.0
	var d14e := _fx_data(Vector2.ZERO, 300.0, [boom_spin], false)
	d14e.set_bullet_max_collision_count(1)
	var v14e: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d14e)
	for i in 120:
		await physics_frame
		if not v14e.is_bullet_status_enabled(0):
			break
	for i in 15:
		await physics_frame
	var spin_angle := 999.0
	for bake in factory.debug_get_effect_state().get("bakes", []):
		for row in bake.get("live_slots", []):
			spin_angle = float(row.get("rotation", 999.0))
	_check(spin_angle < 900.0 and absf(wrapf(spin_angle, -PI, PI)) > 0.3, "spinning one-shot rotates over life (got %.2f)" % spin_angle)

	factory.reset()
	wall.queue_free()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL FX TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
