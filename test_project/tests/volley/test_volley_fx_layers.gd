extends BlastTest
## Volley sprite-effect layers suite: REAL physics (no mocks).
## Covers: custom-frame validation, ON_SPAWN flash fires + expires, trail
## follows + dies on disable + resumes on wake, hit sparks on counted hits,
## destroy explosion on collision-kill only (never on timeout), trigger
## chance gate, opt-in desync across shards, per-bullet trail toggles,
## play_effect_animation switch, pool-reuse reseed, ring caps (explicit +
## auto), 24-frame bake cap, applied z/tint/texture/visibility/light,
## bounce sparks, spawner integration, manual hatch +
## clear, interpolation agreement, ramp tints, volley self_modulate,
## rotation offsets + spin (trail + one-shot), On Clear manual visuals,
## volley fade in/out + tint-over-life ramp, inspector trigger hint,
## layer fades (one-shot envelope + trail volley-age envelope),
## whiten override (exact-color frames, alpha preserved, fallback),
## bullet whiten override (exact tint, toggle, reuse).



func _fx_data(from: Vector2, speed: float, layers: Array, bounce: bool = true) -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	d.sprite_frames = H.make_sprite_frames()
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
	add(wall)
	return wall

func _settle(factory: BulletFactory2D) -> void:
	await idle(1)
	await idle(1)
	factory.free_active_bullets()
	factory.clear_sprite_effects()
	await physics()

func _make_frames(count: int, anims: Array = ["default"], col: Color = Color(1, 1, 1, 1)) -> SpriteFrames:
	var sf := SpriteFrames.new()
	for a in anims:
		if not sf.has_animation(a):
			sf.add_animation(a)
		sf.set_animation_speed(a, 10.0)
		sf.set_animation_loop(a, false)
		for i in count:
			var img := Image.create_empty(8, 8, false, Image.FORMAT_RGBA8)
			img.fill(col)
			sf.add_frame(a, ImageTexture.create_from_image(img))
	return sf

func _make_layer(trigger: int, count: int = 4, anims: Array = ["default"]) -> BulletEffectLayerData2D:
	var l := BulletEffectLayerData2D.new()
	l.trigger = trigger
	l.sprite_frames = _make_frames(count, anims)
	return l

func _fx_live_tint(factory: BulletFactory2D, layer: int) -> Color:
	var st: Dictionary = factory.debug_get_effect_state()
	var bakes: Array = st.get("bakes", [])
	for b in bakes:
		if int(b.get("layer", -1)) != layer:
			continue
		var live: Array = b.get("live_slots", [])
		if live.is_empty():
			continue
		return live[0].get("tint", Color(-1, -1, -1, -1))
	return Color(-1, -1, -1, -1)

func _fx_trail_tint(v: DirectionalBullets2D) -> Color:
	var info: Dictionary = v.debug_get_effect_layers_info()
	var bakes: Array = info.get("trail_bakes", [])
	if bakes.is_empty():
		return Color(-1, -1, -1, -1)
	var live: Array = bakes[0].get("live_trails", [])
	if live.is_empty():
		return Color(-1, -1, -1, -1)
	return live[0].get("tint", Color(-1, -1, -1, -1))

var wall


func _preamble() -> void:
	wall = _make_wall(Vector2(200, 0))
	await physics()


func test_validation_inert_layers_stay_silent_rejects_hold() -> void:
	await _preamble()
	# FX T1 validation: inert layers stay silent, rejects hold
	var dead := BulletEffectLayerData2D.new()
	assert_true(dead.random_start_frame == false, "desync off by default")
	assert_true(dead.max_instances == 0, "auto cap by default")
	assert_true(factory.spawn_layer_effect(dead, Transform2D.IDENTITY) == -1, "null-frames layer inert, no crash")
	dead.trigger = 9
	assert_true(dead.trigger == 0, "bad trigger rejected")
	dead.set_trigger_chance(2.0)
	assert_true(dead.trigger_chance == 1.0, "chance > 1 rejected")
	dead.set_trigger_chance(NAN)
	assert_true(dead.trigger_chance == 1.0, "NaN chance rejected")
	dead.set_max_instances(-1)
	assert_true(dead.max_instances == 0, "negative cap rejected")
	dead.set_max_instances(0)
	assert_true(dead.max_instances == 0, "auto cap (0) accepted")
	dead.set_random_scale_min(5.0)
	assert_true(dead.random_scale_min == 1.0, "scale min above max rejected")
	dead.set_rotation_degrees(NAN)
	assert_true(dead.rotation_degrees == 0.0, "NaN rotation rejected")
	dead.set_spin_degrees_per_sec(NAN)
	assert_true(dead.spin_degrees_per_sec == 0.0, "NaN spin rejected")
	var off := _make_layer(3, 4)
	off.enabled = false
	assert_true(factory.spawn_layer_effect(off, Transform2D.IDENTITY) == -1, "disabled layer never fires")
	assert_true(factory.spawn_layer_effect(null, Transform2D.IDENTITY) == -1, "null layer rejected")
	expect_errors_containing("sprite_frames is null", 1, "null frames fail loud")
	expect_errors_containing("trigger must be", 1, "bad trigger fails loud")
	expect_errors_containing("trigger_chance must be finite", 2, "bad chance fails loud")
	expect_errors_containing("max_instances must be", 1, "negative instances fail loud")
	expect_errors_containing("random_scale_min must be finite", 1, "inverted scale fails loud")
	expect_errors_containing("rotation_degrees must be finite", 1, "NaN rotation fails loud")
	expect_errors_containing("spin_degrees_per_sec must be finite", 1, "NaN spin fails loud")
	expect_errors_containing("layer is null or disabled", 2, "dead layers fail loud")


func test_spawn_flash_fires_once_expires() -> void:
	await _preamble()
	# FX T2 spawn flash fires once + expires
	await _settle(factory)
	var v2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 300.0, [_make_layer(1, 4), _make_layer(2, 4), _make_layer(3, 4)]))
	await physics()
	assert_true(factory.get_active_effect_count() == 1, "only the flash fires at spawn (hit/destroy silent)")
	for i in 30:
		await physics()
	assert_true(factory.get_active_effect_count() == 0, "flash expired on schedule (0.4s)")


func test_trail_follows_dies_on_disable_resumes_on_wake() -> void:
	await _preamble()
	# FX T3 trail follows + dies on disable + resumes on wake
	await _settle(factory)
	var v3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 200.0, [_make_layer(0, 1)]))
	assert_true(v3.has_trail_effects(), "trail baked")
	for i in 5:
		await physics()
	var bt: Vector2 = v3.get_bullet_transform(0).origin
	var tt: Vector2 = v3.debug_get_trail_transform(0, 0).origin
	assert_true(tt.distance_to(bt) < 0.01, "trail sits on the bullet")
	v3.disable_bullet(0)
	assert_true(v3.debug_get_trail_transform(0, 0) == Transform2D(), "disable zeroes the trail")
	v3.enable_bullet(0)
	await physics()
	await physics()
	assert_true(v3.debug_get_trail_transform(0, 0).origin.distance_to(v3.get_bullet_transform(0).origin) < 0.01, "wake resumes the trail")


func test_destroy_explosion_on_kill_only_chance_gate() -> void:
	await _preamble()
	# FX T4 destroy explosion on kill only, chance gate
	await _settle(factory)
	var d4 := _fx_data(Vector2.ZERO, 300.0, [_make_layer(3, 8)], false)
	d4.set_bullet_max_collision_count(1)
	var v4: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d4)
	for i in 120:
		await physics()
		if not v4.is_bullet_status_enabled(0):
			break
	assert_true(not v4.is_bullet_status_enabled(0), "wall killed the bullet")
	for i in 3:
		await physics()
	assert_true(factory.get_active_effect_count() >= 1, "kill detonated")
	for i in 60:
		await physics()
	assert_true(factory.get_active_effect_count() == 0, "explosion expired (0.8s)")
	await _settle(factory)
	var shy := _make_layer(3, 4)
	shy.trigger_chance = 0.0
	var d4b := _fx_data(Vector2.ZERO, 300.0, [shy], false)
	d4b.set_bullet_max_collision_count(1)
	var v4b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d4b)
	for i in 120:
		await physics()
		if not v4b.is_bullet_status_enabled(0):
			break
	for i in 3:
		await physics()
	assert_true(factory.get_active_effect_count() == 0, "chance 0 never fires")
	await _settle(factory)
	var d4c := _fx_data(Vector2.ZERO, 0.0, [_make_layer(3, 4)], false)
	d4c.max_life_time = 0.2
	var v4c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d4c)
	for i in 60:
		await physics()
	assert_true(factory.get_active_effect_count() == 0, "timeout never detonates")


func test_hit_sparks_on_counted_non_killing_hits() -> void:
	await _preamble()
	# FX T5 hit sparks on counted non-killing hits
	await _settle(factory)
	var d5 := _fx_data(Vector2.ZERO, 300.0, [_make_layer(2, 6), _make_layer(3, 6)], false)
	d5.set_bullet_max_collision_count(0)
	var v5: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d5)
	for i in 120:
		await physics()
		if v5.get_bullet_collision_count(0) >= 1:
			break
	assert_true(v5.get_bullet_collision_count(0) >= 1, "through-shot counted (max 0)")
	assert_true(factory.get_active_effect_count() == 1, "only the spark fires on a non-killing hit")


func test_opt_in_desync_same_tick_kills_span_shards() -> void:
	await _preamble()
	# FX T6 opt-in desync: same-tick kills span shards
	await _settle(factory)
	seed(12345)
	var dd := DirectionalBulletsData2D.new()
	dd.sprite_frames = H.make_sprite_frames()
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
		await physics()
		if factory.debug_get_effect_state().get("active_total", 0) >= 1:
			break
	await physics()
	var busy := 0
	for bake in factory.debug_get_effect_state().get("bakes", []):
		busy = maxi(busy, int(bake.get("shards_busy", 0)))
	assert_true(busy >= 2, "six same-tick kills desync across shards (busy %d)" % busy)
	await _settle(factory)
	var dd6b := _fx_data(Vector2.ZERO, 300.0, [_make_layer(3, 8)], false)
	dd6b.set_bullet_max_collision_count(1)
	var v6b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(dd6b)
	for i in 120:
		await physics()
		if not v6b.is_bullet_status_enabled(0):
			break
	await physics()
	var busy6b := 0
	for bake in factory.debug_get_effect_state().get("bakes", []):
		busy6b = maxi(busy6b, int(bake.get("shards_busy", 0)))
	assert_true(busy6b <= 1, "default sync parks one kill on one shard")


func test_per_bullet_toggles_animation_switch() -> void:
	await _preamble()
	# FX T7 per-bullet toggles + animation switch
	await _settle(factory)
	var dd7 := _fx_data(Vector2.ZERO, 200.0, [_make_layer(0, 1)])
	dd7.transforms = [Transform2D(0.0, Vector2.ZERO), Transform2D(0.0, Vector2(0, 40))]
	var s7a := BulletSpeedData2D.new()
	s7a.speed = 200.0
	s7a.max_speed = 3000.0
	dd7.all_bullet_speed_data = [s7a, s7a]
	var v7: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(dd7)
	for i in 5:
		await physics()
	v7.bullet_set_trail_enabled(0, 0, false)
	assert_true(v7.debug_get_trail_transform(0, 0) == Transform2D(), "bullet 0 trail off")
	assert_true(v7.debug_get_trail_transform(0, 1).origin.distance_to(v7.get_bullet_transform(1).origin) < 0.01, "bullet 1 trail on")
	v7.all_bullets_set_trail_enabled(0, false)
	assert_true(v7.debug_get_trail_transform(0, 1) == Transform2D(), "range toggle hides all")
	v7.all_bullets_set_trail_enabled(0, true)
	await physics()
	assert_true(v7.debug_get_trail_transform(0, 1).origin.distance_to(v7.get_bullet_transform(1).origin) < 0.01, "range toggle restores")
	var custom := BulletEffectLayerData2D.new()
	custom.trigger = 0
	custom.sprite_frames = _make_frames(1, ["a", "b"])
	var v7b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 200.0, [custom]))
	assert_true(v7b.has_trail_effects(), "custom frames bake as trail")
	assert_true(v7b.play_effect_animation(0, "b"), "animation switch accepted")
	assert_true(not v7b.play_effect_animation(0, "nope"), "unknown animation rejected")
	assert_true(not v7b.play_effect_animation(9, "b"), "unknown layer rejected")
	expect_errors_containing("missing animation", 1, "unknown animation fails loud")


func test_pool_reuse_reseeds_caps_hold_explicit_auto() -> void:
	await _preamble()
	# FX T8 pool reuse reseeds + caps hold (explicit + auto)
	await _settle(factory)
	var d8 := _fx_data(Vector2.ZERO, 200.0, [_make_layer(1, 4), _make_layer(0, 1)])
	d8.max_life_time = 0.3
	var v8: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d8)
	await physics()
	assert_true(factory.get_active_effect_count() >= 1, "first life flashes")
	for i in 60:
		await physics()
		if not v8.is_bullet_status_enabled(0):
			break
	assert_true(not v8.is_bullet_status_enabled(0), "expiry pooled the volley")
	var v8b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 200.0, [_make_layer(1, 4)]))
	await physics()
	assert_true(factory.get_active_effect_count() >= 1, "pooled reuse flashes again")
	assert_true(not v8b.has_trail_effects(), "reseed without trail drops it")
	await _settle(factory)
	var capped := _make_layer(3, 4)
	capped.max_instances = 2
	for i in 5:
		factory.spawn_layer_effect(capped, Transform2D(0.0, Vector2(i * 10, 0)))
	assert_true(factory.get_active_effect_count() <= 2, "explicit ring cap holds at 2")
	factory.clear_sprite_effects()
	var auto_small := _make_layer(1, 2)
	var v8c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 200.0, [auto_small]))
	var auto_slots := -1
	for bake in factory.debug_get_effect_state().get("bakes", []):
		auto_slots = int(bake.get("slots", -1))
	assert_true(auto_slots == 32, "auto cap floors at 32 for a 1-bullet volley (got %d)" % auto_slots)
	await _settle(factory)
	var wide := BulletEffectLayerData2D.new()
	wide.trigger = 0
	wide.sprite_frames = _make_frames(30)
	var v8d: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 200.0, [wide]))
	var baked_frames := -1
	for bake in v8d.debug_get_effect_layers_info().get("trail_bakes", []):
		baked_frames = int(bake.get("frames", -1))
	assert_true(baked_frames == 24, "30-frame anim truncates to the 24-frame bake cap")


func test_styling_z_tint_texture_visibility_light_bounce() -> void:
	await _preamble()
	# FX T9 styling: z, tint, texture, visibility, light + bounce
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
	assert_true(got_z == 5 and got_rel, "shard carries layer z (+5, relative)")
	assert_true(got_mod.is_equal_approx(Color(1, 0.25, 0.1)), "shard carries layer tint")
	assert_true(got_tex, "shard carries the baked texture")
	assert_true(got_vis == 4 and got_light == 8, "shard carries visibility + light masks")
	await _settle(factory)
	var bspark := _make_layer(4, 6)
	var db := _fx_data(Vector2.ZERO, 300.0, [bspark, _make_layer(3, 6)])
	db.bounce_strength = 1.0
	var vb: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(db)
	for i in 120:
		await physics()
		if vb.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(vb.bullet_get_bounce_count(0) >= 1, "wall bounced")
	assert_true(factory.get_active_effect_count() == 1, "only the bounce sparks (destroy silent)")


func test_spawner_integration_manual_hatch() -> void:
	await _preamble()
	# FX T10 spawner integration + manual hatch
	await _settle(factory)
	var spawner := BulletSpawner2D.new()
	spawner.bullet_factory_path = factory.get_path()
	spawner.shooting_enabled = false
	spawner.pattern_source = BulletSpawner2D.PATTERN_FROM_SELF
	spawner.position = Vector2.ZERO
	add(spawner)
	await idle(1)
	spawner.spawn_data = _fx_data(Vector2.ZERO, 200.0, [_make_layer(1, 4), _make_layer(0, 1)], false)
	assert_true(spawner.shoot_once(), "spawner volley fired")
	await physics()
	await physics()
	assert_true(factory.get_active_effect_count() >= 1, "spawner volley flashed")
	spawner.queue_free()
	await idle(1)
	var ring := _make_layer(3, 4)
	assert_true(factory.spawn_layer_effect(ring, Transform2D(0.0, Vector2(300, 0))) >= 0, "manual hatch fires")
	assert_true(factory.get_active_effect_count() >= 1, "manual effect live")
	factory.clear_sprite_effects()
	assert_true(factory.get_active_effect_count() == 0, "manual clear stops all")


func test_interpolation_agreement() -> void:
	await _preamble()
	# FX T11 interpolation agreement
	await _settle(factory)
	factory.use_physics_interpolation = true
	var v11: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 0.0, [_make_layer(0, 1)], false))
	v11.set_bullet_transform(0, Transform2D(0.0, Vector2(100, 0)))
	v11.debug_run_interpolation_pass()
	assert_true(v11.debug_get_trail_transform(0, 0).origin.distance_to(Vector2(100, 0)) < 0.01, "interp pass writes trail with zero physics ticks")
	v11.set_bullet_transform(0, Transform2D(0.0, Vector2(200, 0)))
	v11.debug_run_interpolation_pass()
	assert_true(v11.debug_get_trail_transform(0, 0).origin.distance_to(Vector2(200, 0)) < 0.01, "interp pass tracks teleports")
	assert_true(v11.debug_get_trail_transform(0, 0).is_finite(), "interp trail finite")
	factory.use_physics_interpolation = false
	var v11b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 0.0, [_make_layer(0, 1)], false))
	v11b.set_bullet_transform(0, Transform2D(0.0, Vector2(100, 0)))
	v11b.debug_run_interpolation_pass()
	assert_true(v11b.debug_get_trail_transform(0, 0) == Transform2D(), "interp pass gated off when disabled")


func test_ramp_tints_over_life() -> void:
	await _preamble()
	# FX T12 ramp tints over life
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
		await physics()
		if not v12.is_bullet_status_enabled(0):
			break
	var birth_tint := Color(-1, -1, -1, -1)
	for bake in factory.debug_get_effect_state().get("bakes", []):
		for row in bake.get("live_slots", []):
			birth_tint = row.get("tint", birth_tint)
	assert_true(birth_tint.r > 0.9 and birth_tint.g > 0.9 and birth_tint.b > 0.9, "ramp starts near-white (got %s)" % str(birth_tint))
	for i in 30:
		await physics()
	var mid_tint := Color(-1, -1, -1, -1)
	for bake in factory.debug_get_effect_state().get("bakes", []):
		for row in bake.get("live_slots", []):
			mid_tint = row.get("tint", mid_tint)
	assert_true(mid_tint.r > 0.9 and mid_tint.g < 0.75 and mid_tint.b < 0.75, "ramp reddens mid-life (got %s)" % str(mid_tint))
	await _settle(factory)
	var cool := _make_layer(0, 10)
	cool.color_ramp = ramp
	var v12b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 0.0, [cool], false))
	for i in 35:
		await physics()
	var trail_tint := Color(-1, -1, -1, -1)
	for bake in v12b.debug_get_effect_layers_info().get("trail_bakes", []):
		for row in bake.get("live_trails", []):
			trail_tint = row.get("tint", trail_tint)
	assert_true(trail_tint.r > 0.9 and trail_tint.g < 0.9, "trail loop-phase tint applies (got %s)" % str(trail_tint))


func test_volley_self_modulate_applies_reseeds_clean() -> void:
	await _preamble()
	# FX T13 volley self_modulate applies + reseeds clean
	await _settle(factory)
	var d13 := _fx_data(Vector2.ZERO, 200.0, [], false)
	d13.self_modulate = Color(1, 0.2, 0.2)
	d13.max_life_time = 0.3
	var v13: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d13)
	assert_true(v13.debug_get_volley_info().get("self_modulate", Color(0, 0, 0, 0)).is_equal_approx(Color(1, 0.2, 0.2)), "volley tint applied from data")
	for i in 60:
		await physics()
		if not v13.is_bullet_status_enabled(0):
			break
	assert_true(not v13.is_bullet_status_enabled(0), "tinted volley expired into the pool")
	var d13b := _fx_data(Vector2.ZERO, 200.0, [], false)
	var v13b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d13b)
	assert_true(v13b.debug_get_volley_info().get("self_modulate", Color(0, 0, 0, 0)).is_equal_approx(Color(1, 1, 1, 1)), "pool reuse resets tint to white")


func test_rotation_offsets_spin() -> void:
	await _preamble()
	# FX T14 rotation offsets + spin
	await _settle(factory)
	var turned := _make_layer(0, 1)
	turned.rotation_degrees = 90.0
	var v14c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 200.0, [turned], false))
	for i in 3:
		await physics()
	assert_true(absf(absf(wrapf(v14c.debug_get_trail_transform(0, 0).get_rotation(), -PI, PI)) - PI / 2.0) < 0.08, "90-degree offset rotates the trail")
	await _settle(factory)
	var spinner := _make_layer(0, 1)
	spinner.spin_degrees_per_sec = 180.0
	var v14d: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 0.0, [spinner], false))
	for i in 30:
		await physics()
	var spin_rot: float = absf(wrapf(v14d.debug_get_trail_transform(0, 0).get_rotation(), -PI, PI))
	assert_true(absf(spin_rot - PI / 2.0) < 0.2, "180dps trail spun a quarter turn in 0.5s (got %.2f)" % spin_rot)
	await _settle(factory)
	var boom_spin := _make_layer(3, 4)
	boom_spin.spin_degrees_per_sec = 360.0
	var d14e := _fx_data(Vector2.ZERO, 300.0, [boom_spin], false)
	d14e.set_bullet_max_collision_count(1)
	var v14e: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d14e)
	for i in 120:
		await physics()
		if not v14e.is_bullet_status_enabled(0):
			break
	for i in 15:
		await physics()
	var spin_angle := 999.0
	for bake in factory.debug_get_effect_state().get("bakes", []):
		for row in bake.get("live_slots", []):
			spin_angle = float(row.get("rotation", 999.0))
	assert_true(spin_angle < 900.0 and absf(wrapf(spin_angle, -PI, PI)) > 0.3, "spinning one-shot rotates over life (got %.2f)" % spin_angle)


func test_lifetime_over_trigger_fires_on_both_expiry_paths() -> void:
	await _preamble()
	# FX T15 lifetime-over trigger fires on both expiry paths
	await _settle(factory)
	var d15 := _fx_data(Vector2.ZERO, 0.0, [_make_layer(5, 4)], false)
	d15.max_life_time = 0.3
	var v15: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d15)
	for i in 60:
		await physics()
		if not v15.is_bullet_status_enabled(0):
			break
	assert_true(not v15.is_bullet_status_enabled(0), "silent expiry pooled the bullet")
	for i in 3:
		await physics()
	assert_true(factory.get_active_effect_count() >= 1, "expiry fizzle fired (silent path)")
	for i in 40:
		await physics()
	assert_true(factory.get_active_effect_count() == 0, "fizzle expired on schedule")
	await _settle(factory)
	var d15b := _fx_data(Vector2.ZERO, 0.0, [_make_layer(5, 4)], false)
	d15b.max_life_time = 0.3
	d15b.is_life_time_over_signal_enabled = true
	var v15b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d15b)
	for i in 60:
		await physics()
		if not v15b.is_bullet_status_enabled(0):
			break
	assert_true(not v15b.is_bullet_status_enabled(0), "signalled expiry pooled the bullet")
	for i in 3:
		await physics()
	assert_true(factory.get_active_effect_count() >= 1, "expiry fizzle fired (signal path)")


func test_trail_visibility_retires_unknown_layers_null_entries() -> void:
	await _preamble()
	# FX T16 trail visibility retires + unknown layers + null entries
	await _settle(factory)
	var dd16 := _fx_data(Vector2.ZERO, 200.0, [_make_layer(0, 1)], false)
	dd16.transforms = [Transform2D(0.0, Vector2.ZERO), Transform2D(0.0, Vector2(0, 40))]
	var s16a := BulletSpeedData2D.new()
	s16a.speed = 200.0
	s16a.max_speed = 3000.0
	dd16.all_bullet_speed_data = [s16a, s16a]
	var v16: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(dd16)
	for i in 5:
		await physics()
	var vis16 := -1
	for bake in v16.debug_get_effect_layers_info().get("trail_bakes", []):
		vis16 = int(bake.get("shards_visible", -1))
	assert_true(vis16 >= 1, "live trails mark shards visible")
	v16.all_bullets_set_trail_enabled(0, false)
	var vis16b := -1
	for bake in v16.debug_get_effect_layers_info().get("trail_bakes", []):
		vis16b = int(bake.get("shards_visible", -1))
	assert_true(vis16b == 0, "fully hidden trail retires its shards (draw calls back to zero)")
	v16.bullet_set_trail_enabled(9, 0, true)
	await physics()
	assert_true(v16.debug_get_trail_transform(0, 0) == Transform2D(), "unknown layer errors without side effects")
	v16.bullet_set_trail_enabled(0, 0, true)
	await physics()
	assert_true(v16.debug_get_trail_transform(0, 0).origin.distance_to(v16.get_bullet_transform(0).origin) < 0.01, "valid toggle still works after the bad one")
	await _settle(factory)
	var nulllayer := _make_layer(0, 1)
	var d16b := _fx_data(Vector2.ZERO, 200.0, [null, nulllayer], false)
	var v16b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d16b)
	for i in 5:
		await physics()
	assert_true(v16b.has_trail_effects(), "null layer entry tolerated, rest bakes")
	assert_true(v16b.debug_get_trail_transform(1, 0).origin.distance_to(v16b.get_bullet_transform(0).origin) < 0.01, "trail follows despite the null sibling")
	expect_errors_containing("no baked trail layer 9", 1, "unknown trail layer fails loud")


func test_lifetime_fizzle_live_rebake_bounce_knob_sparks() -> void:
	await _preamble()
	# FX T17 lifetime fizzle + live rebake + bounce-knob sparks
	await _settle(factory)
	var d17 := H.make_directional_data(1, 0.0, 0.3)
	d17.effect_layers = [_make_layer(5, 4)]
	factory.spawn_controllable_directional_bullets(d17)
	var fired17 := false
	for i in 60:
		await physics()
		if factory.get_active_effect_count() >= 1:
			fired17 = true
			break
	assert_true(fired17, "expiry fizzles")
	for i in 40:
		await physics()
	assert_true(factory.get_active_effect_count() == 0, "fizzle expires")
	await _settle(factory)
	var v17: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 200.0, [], false))
	assert_true(not v17.has_trail_effects(), "no layers, no trails")
	v17.set_effect_layers([_make_layer(0, 1)])
	assert_true(v17.has_trail_effects(), "live set bakes trails with zero spawns")
	for i in 5:
		await physics()
	assert_true(v17.debug_get_trail_transform(0, 0).origin.distance_to(v17.get_bullet_transform(0).origin) < 0.01, "live-set trail follows")
	await _settle(factory)
	var bs17 := _make_layer(4, 6)
	var db17 := _fx_data(Vector2.ZERO, 300.0, [bs17])
	db17.bounce_strength = 1.0
	db17.bounce_push_assist = false
	db17.bounce_charge_amplify = false
	var v17b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(db17)
	for i in 120:
		await physics()
		if v17b.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(factory.get_active_effect_count() >= 1, "bounce sparks with knobs off (orthogonal)")


func test_hostile_zero_frames_ring_caps_shared_refs_teardown() -> void:
	await _preamble()
	# FX T18 hostile: zero frames, ring caps, shared refs, teardown
	await _settle(factory)
	# T18a: zero-frame animation is inert, manual hatch refuses it.
	var zeroframes := SpriteFrames.new()
	var dead18 := BulletEffectLayerData2D.new()
	dead18.trigger = 1
	dead18.sprite_frames = zeroframes
	var v18: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 300.0, [dead18], false))
	await physics()
	assert_true(factory.get_active_effect_count() == 0, "zero-frame volley layer stays silent")
	assert_true(factory.spawn_layer_effect(dead18, Transform2D.IDENTITY) == -1, "zero-frame manual hatch refused")
	# T18b: max_instances = 1 recycles the oldest slot (3 kills, 1 live).
	await _settle(factory)
	var cap18 := _make_layer(3, 6)
	cap18.max_instances = 1
	var d18b := _fx_data(Vector2.ZERO, 300.0, [cap18], false)
	d18b.set_bullet_max_collision_count(1)
	var v18b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d18b)
	for i in 200:
		await physics()
		if factory.get_active_effect_count() >= 1:
			break
	assert_true(factory.get_active_effect_count() <= 1, "ring cap 1 holds under repeat kills")
	await _settle(factory)
	# T18c: one shared layer Ref across two volleys fires independently.
	var shared18 := _make_layer(1, 4)
	var v18c1: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 300.0, [shared18], false))
	var v18c2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 300.0, [shared18], false))
	await physics()
	await physics()
	assert_true(factory.get_active_effect_count() == 2, "shared Ref fires once per volley")
	await _settle(factory)
	# T18d: queue_free volley with live one-shots drains without crashing.
	var live18 := _make_layer(1, 4)
	var v18d: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 300.0, [live18], false))
	await physics()
	assert_true(factory.get_active_effect_count() >= 1, "one-shot live before queue_free")
	v18d.queue_free()
	await idle(1)
	await idle(1)
	for i in 40:
		await physics()
	assert_true(factory.get_active_effect_count() == 0, "freed volley effects drain on expiry")
	assert_true(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after volley freed with live effects")
	# T18e: factory.reset() with live effects zeroes everything.
	await _settle(factory)
	var live18e: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 300.0, [_make_layer(1, 4)], false))
	await physics()
	assert_true(factory.get_active_effect_count() >= 1, "one-shot live before reset")
	# Structural calls reject inside the physics step: awaiting
	# physics_frame resumes mid-step, so park on idle first (same reason
	# _settle uses process_frame before factory mutations).
	await idle(1)
	factory.reset()
	assert_true(factory.get_active_effect_count() == 0, "reset zeroes live effects")
	# T18f: play_effect_animation rejects one-shot layers and unknown indexes.
	await _settle(factory)
	var v18f: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 200.0, [_make_layer(2, 4)], false))
	await physics()
	assert_true(v18f.play_effect_animation(0, &"default") == false, "play_effect_animation refuses one-shot layer")
	assert_true(v18f.play_effect_animation(9, &"default") == false, "play_effect_animation refuses unknown layer")
	assert_true(not v18f.has_trail_effects(), "one-shot volley still has no trails")
	# T18g: set_effect_layers([]) mid-life retires trails silently.
	await _settle(factory)
	var v18g: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 200.0, [_make_layer(0, 1)], false))
	for i in 5:
		await physics()
	assert_true(v18g.has_trail_effects(), "trail baked before clear")
	v18g.set_effect_layers([])
	assert_true(not v18g.has_trail_effects(), "empty set clears trails")
	assert_true(v18g.debug_get_trail_transform(0, 0) == Transform2D(), "cleared trail reads identity")
	for i in 5:
		await physics()
	assert_true(v18g.get_bullet_transform(0).is_finite(), "volley finite after mid-life clear")
	# T18h: chance gates, huge spin, manual-hatch eviction cap.
	await _settle(factory)
	var never18 := _make_layer(1, 4)
	never18.trigger_chance = 0.0
	var v18h: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 300.0, [never18], false))
	await physics()
	await physics()
	assert_true(factory.get_active_effect_count() == 0, "trigger_chance 0 never fires")
	await _settle(factory)
	var always18 := _make_layer(1, 4)
	always18.trigger_chance = 1.0
	var v18h2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 300.0, [always18], false))
	await physics()
	assert_true(factory.get_active_effect_count() >= 1, "trigger_chance 1 always fires")
	await _settle(factory)
	var spin18 := _make_layer(1, 4)
	# INF is rejected by the setter (stays 0), so exercise the fire-path
	# finite-gate with a huge-but-finite spin instead: it must still fire
	# statically and never crash the ager.
	spin18.spin_degrees_per_sec = 1e30
	var v18h3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 300.0, [spin18], false))
	await physics()
	assert_true(factory.get_active_effect_count() >= 1, "huge spin still fires statically")
	for i in 10:
		await physics()
	assert_true(factory.get_active_effect_count() <= 2, "huge spin never crashes the ager")
	await _settle(factory)
	for i in 9:
		var ml := _make_layer(1, 2, ["default", "alt%d" % i])
		if not ml.sprite_frames.has_animation("alt%d" % i):
			ml.sprite_frames.add_animation("alt%d" % i)
		factory.spawn_layer_effect(ml, Transform2D.IDENTITY)
	assert_true(factory.debug_get_effect_state().get("manual_bakes", 99) <= 8, "manual hatch capped at 8 bakes")
	# T18i: collision kills never fizzle; infinite life never fizzles.
	await _settle(factory)
	var kill18 := _fx_data(Vector2.ZERO, 300.0, [_make_layer(5, 4)], false)
	kill18.set_bullet_max_collision_count(1)
	var v18i: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(kill18)
	for i in 120:
		await physics()
		if not v18i.is_bullet_status_enabled(0):
			break
	assert_true(not v18i.is_bullet_status_enabled(0), "kill volley died on the wall")
	for i in 3:
		await physics()
	assert_true(factory.get_active_effect_count() == 0, "collision kill fires no lifetime fizzle")
	await _settle(factory)
	var inf18 := _fx_data(Vector2.ZERO, 100.0, [_make_layer(5, 4)], false)
	inf18.is_life_time_infinite = true
	var v18i2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(inf18)
	for i in 30:
		await physics()
	assert_true(factory.get_active_effect_count() == 0, "infinite life never fizzles")
	expect_errors_containing("has no animation with frames", 2, "frameless layers fail loud")


func test_stacked_trails_offset_live_one_shot_swap_pause_freeze() -> void:
	await _preamble()
	# FX T19 stacked trails, offset, live one-shot swap, pause freeze
	await _settle(factory)
	var t19a := _make_layer(0, 1)
	var t19b := _make_layer(0, 1)
	t19b.offset = Vector2(24, 0)
	var v19: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 200.0, [t19a, t19b], false))
	for i in 5:
		await physics()
	assert_true(v19.debug_get_trail_transform(0, 0).origin.distance_to(v19.get_bullet_transform(0).origin) < 0.01, "first trail sits on the bullet")
	assert_true(absf(v19.debug_get_trail_transform(1, 0).origin.distance_to(v19.get_bullet_transform(0).origin) - 24.0) < 0.5, "offset trail rides 24px ahead")
	await _settle(factory)
	var swap_hit := _make_layer(2, 4)
	var swap_boom := _make_layer(3, 4)
	var v19b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 200.0, [swap_hit], false))
	v19b.set_effect_layers([swap_boom])
	assert_true(not v19b.has_trail_effects(), "live swap keeps trail state accurate")
	await _settle(factory)
	var stream := _make_layer(0, 1)
	var v19c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 200.0, [stream], false))
	for i in 3:
		await physics()
	var pre_pause: Vector2 = v19c.debug_get_trail_transform(0, 0).origin
	factory.set_is_factory_processing_bullets(false)
	for i in 10:
		await physics()
	assert_true(v19c.debug_get_trail_transform(0, 0).origin.distance_to(pre_pause) < 0.01, "paused factory freezes trails")
	assert_true(factory.get_active_effect_count() == 0, "no stray one-shots while paused")
	factory.set_is_factory_processing_bullets(true)
	for i in 3:
		await physics()
	assert_true(v19c.debug_get_trail_transform(0, 0).origin.distance_to(v19c.get_bullet_transform(0).origin) < 0.01, "unpaused trails resume")


func test_hostile_configs_never_crash() -> void:
	await _preamble()
	# FX T20 hostile configs never crash
	await _settle(factory)
	var zeroanim := BulletEffectLayerData2D.new()
	zeroanim.trigger = 1
	var empty_sf := SpriteFrames.new()
	zeroanim.sprite_frames = empty_sf
	assert_true(factory.spawn_layer_effect(zeroanim, Transform2D.IDENTITY) == -1, "zero-frame animation inert, no crash")
	var wronganim := BulletEffectLayerData2D.new()
	wronganim.trigger = 1
	wronganim.sprite_frames = _make_frames(2)
	wronganim.animation = &"nope"
	assert_true(factory.spawn_layer_effect(wronganim, Transform2D.IDENTITY) == -1, "wrong animation name inert, no crash")
	var dup_a := _make_layer(0, 1)
	var v20: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 200.0, [dup_a, dup_a], false))
	for i in 5:
		await physics()
	assert_true(v20.debug_get_trail_transform(0, 0).origin.distance_to(v20.get_bullet_transform(0).origin) < 0.01, "duplicate layer refs both follow")
	assert_true(factory.get_active_effect_count() == 0, "no stray one-shots from trail-only volley")
	expect_errors_containing("has no animation with frames", 1, "frameless layer fails loud")
	expect_errors_containing("missing animation", 1, "wrong animation fails loud")


func test_interp_teleports_scale_paused_manual_hatch() -> void:
	await _preamble()
	# FX T21 interp teleports, scale, paused manual hatch
	await _settle(factory)
	factory.set_use_physics_interpolation_runtime(true)
	var big21 := _make_layer(0, 1)
	big21.scale = Vector2(2, 2)
	var v21: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_fx_data(Vector2.ZERO, 0.0, [big21], false))
	v21.set_bullet_transform(0, Transform2D(0.0, Vector2(100, 0)))
	v21.debug_run_interpolation_pass()
	assert_true(v21.debug_get_trail_transform(0, 0).origin.distance_to(Vector2(100, 0)) < 0.01, "interp trail glued after teleport")
	assert_true(v21.debug_get_trail_transform(0, 0).get_scale().distance_to(Vector2(2, 2)) < 0.01, "trail carries layer scale")
	factory.set_use_physics_interpolation_runtime(false)
	await _settle(factory)
	var held := _make_layer(3, 4)
	factory.set_is_factory_processing_bullets(false)
	var slot21: int = factory.spawn_layer_effect(held, Transform2D(0.0, Vector2(50, 0)))
	assert_true(slot21 >= 0, "manual hatch fires while paused")
	for i in 10:
		await physics()
	assert_true(factory.get_active_effect_count() >= 1, "paused one-shot held, not expired")
	factory.set_is_factory_processing_bullets(true)
	for i in 40:
		await physics()
	assert_true(factory.get_active_effect_count() == 0, "resumed one-shot expires on schedule")


func test_manual_hatch_rebakes_on_layer_edits() -> void:
	await _preamble()
	# FX T22 manual hatch rebakes on layer edits
	await _settle(factory)
	var vedit := _make_layer(3, 2)
	assert_true(factory.spawn_layer_effect(vedit, Transform2D(0.0, Vector2(60, 0))) >= 0, "manual hatch fires")
	assert_true(int(factory.debug_get_effect_state().get("manual_frames", -1)) == 2, "manual bake holds 2 frames")
	vedit.sprite_frames = _make_frames(5)
	assert_true(factory.spawn_layer_effect(vedit, Transform2D(0.0, Vector2(70, 0))) >= 0, "manual hatch fires after edit")
	assert_true(int(factory.debug_get_effect_state().get("manual_frames", -1)) == 5, "manual rebake picks up 5 frames")
	for i in 60:
		await physics()
	assert_true(factory.get_active_effect_count() == 0, "edited manual effects expire")


func test_on_clear_fires_on_manual_clear_only() -> void:
	await _preamble()
	# FX T23 On Clear fires on manual clear only
	await _settle(factory)
	var clear_ok := BulletEffectLayerData2D.new()
	clear_ok.trigger = 6
	assert_true(clear_ok.trigger == 6, "trigger 6 (On Clear) accepted")
	var d23 := _fx_data(Vector2.ZERO, 0.0, [_make_layer(6, 4)], false)
	d23.transforms = [Transform2D(0.0, Vector2.ZERO), Transform2D(0.0, Vector2(32, 0))]
	var v23: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d23)
	assert_true(v23.clear_bullet(0), "clear_bullet live slot returns true")
	for i in 3:
		await physics()
	assert_true(factory.get_active_effect_count() >= 1, "manual clear fires On Clear")
	var count_after: int = factory.get_active_effect_count()
	assert_true(not v23.clear_bullet(0), "double clear stays silent")
	assert_true(factory.get_active_effect_count() == count_after, "no second visual for dead slot")
	assert_true(not v23.clear_bullet(99), "clear invalid index refused")
	assert_true(v23.clear_all_bullets() == 1, "clear_all drains the last live bullet")
	for i in 60:
		await physics()
	assert_true(factory.get_active_effect_count() == 0, "clear visuals expire")
	await _settle(factory)
	var d23k := _fx_data(Vector2.ZERO, 300.0, [_make_layer(6, 4)], false)
	d23k.set_bullet_max_collision_count(1)
	var v23k: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d23k)
	for i in 120:
		await physics()
		if not v23k.is_bullet_status_enabled(0):
			break
	assert_true(not v23k.is_bullet_status_enabled(0), "wall killed the clear-layer bullet")
	for i in 3:
		await physics()
	assert_true(factory.get_active_effect_count() == 0, "kill does not fire On Clear (Destroy owns it)")
	await _settle(factory)
	var d23e := _fx_data(Vector2.ZERO, 0.0, [_make_layer(6, 4)], false)
	d23e.max_life_time = 0.2
	var v23e: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d23e)
	for i in 60:
		await physics()
	assert_true(factory.get_active_effect_count() == 0, "timeout does not fire On Clear (Lifetime Over owns it)")
	assert_true(not v23e.is_bullet_status_enabled(0), "expiry volley drained")
	expect_errors_containing("Invalid bullet index in clear_bullet", 1, "stale clear fails loud")


func test_volley_fades_in_out_ramp_infinite_reuse() -> void:
	await _preamble()
	# FX T24 volley fades: in, out, ramp, infinite, reuse
	await _settle(factory)
	var d24 := _fx_data(Vector2.ZERO, 0.0, [], false)
	d24.fade_in_sec = 0.5
	d24.max_life_time = 8.0
	var v24: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d24)
	for i in 2:
		await physics()
	assert_true(v24.self_modulate.a < 1.0, "fade-in starts transparent")
	assert_true(v24.self_modulate.a > 0.0, "fade-in already ramping")
	for i in 60:
		await physics()
	assert_true(absf(v24.self_modulate.a - 1.0) < 0.05, "fade-in reaches full tint")
	await _settle(factory)
	var d24o := _fx_data(Vector2.ZERO, 0.0, [], false)
	d24o.fade_out_sec = 0.5
	d24o.max_life_time = 1.0
	var v24o: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d24o)
	for i in 45:
		await physics()
	assert_true(v24o.self_modulate.a < 0.9, "fade-out dims before expiry")
	assert_true(v24o.is_bullet_status_enabled(0), "volley alive mid fade-out")
	for i in 60:
		await physics()
	assert_true(not v24o.is_bullet_status_enabled(0), "fade-out volley still expires")
	await _settle(factory)
	var d24i := _fx_data(Vector2.ZERO, 0.0, [], false)
	d24i.is_life_time_infinite = true
	d24i.fade_in_sec = 0.3
	d24i.fade_out_sec = 5.0
	var v24i: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d24i)
	for i in 120:
		await physics()
	assert_true(absf(v24i.self_modulate.a - 1.0) < 0.05, "infinite volley ignores fade-out, holds full tint")
	await _settle(factory)
	var vol_ramp := Gradient.new()
	vol_ramp.set_color(0, Color(1, 1, 1, 1))
	vol_ramp.set_color(1, Color(1, 1, 1, 0))
	var d24r := _fx_data(Vector2.ZERO, 0.0, [], false)
	d24r.max_life_time = 2.0
	d24r.modulate_ramp = vol_ramp
	var v24r: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d24r)
	for i in 60:
		await physics()
	assert_true(absf(v24r.self_modulate.a - 0.5) < 0.15, "ramp samples lifetime fraction")
	await _settle(factory)
	var d24n := _fx_data(Vector2.ZERO, 0.0, [], false)
	d24n.fade_in_sec = NAN
	assert_true(d24n.fade_in_sec == 0.0, "NaN fade-in rejected")
	d24n.fade_out_sec = -1.0
	assert_true(d24n.fade_out_sec == 0.0, "negative fade-out rejected")
	d24n.fade_in_sec = 0.5
	d24n.max_life_time = 8.0
	var v24a: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d24n)
	for i in 70:
		await physics()
	var v24b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d24n)
	for i in 2:
		await physics()
	assert_true(v24b.self_modulate.a < 1.0, "second life restarts transparent (no tint leak)")
	expect_errors_containing("fade_in_sec must be finite", 1, "NaN fade-in fails loud")
	expect_errors_containing("fade_out_sec must be finite", 1, "NaN fade-out fails loud")


func test_on_clear_visible_in_the_inspector_dropdown() -> void:
	await _preamble()
	# FX T25 On Clear visible in the inspector dropdown
	await _settle(factory)
	var hint_found := false
	var order: Dictionary = {}
	for p in BulletEffectLayerData2D.new().get_property_list():
		var pname: String = p.get("name", "")
		if pname == "trigger":
			hint_found = String(p.get("hint_string", "")).contains("On Clear")
		if pname in ["self_modulate", "color_ramp", "fade_in_sec", "fade_out_sec", "override_frame_color"]:
			order[pname] = order.size()
	assert_true(hint_found, "trigger hint lists On Clear")
	assert_true(order.get("color_ramp", 99) == order.get("self_modulate", -1) + 1, "color_ramp sits with self_modulate")
	assert_true(order.get("fade_in_sec", 99) == order.get("color_ramp", -1) + 1, "fade_in sits with colors")
	assert_true(order.get("fade_out_sec", 99) == order.get("fade_in_sec", -1) + 1, "fade_out sits with colors")
	assert_true(order.get("override_frame_color", 99) == order.get("fade_out_sec", -1) + 1, "override sits with colors")
	var dorder: Dictionary = {}
	for p in DirectionalBulletsData2D.new().get_property_list():
		var pname2: String = p.get("name", "")
		if pname2 in ["self_modulate", "override_frame_color"]:
			dorder[pname2] = dorder.size()
	assert_true(dorder.get("override_frame_color", 99) == dorder.get("self_modulate", -1) + 1, "bullet override sits with self_modulate")


func test_layer_fades_envelope_on_one_shots_trails_ignore() -> void:
	await _preamble()
	# FX T26 layer fades: envelope on one-shots, trails ignore
	await _settle(factory)
	var d26a := _fx_data(Vector2.ZERO, 0.0, [], false)
	var lin := _make_layer(1, 4)
	lin.fade_in_sec = 0.2
	d26a.effect_layers = [lin]
	var v26a: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d26a)
	for i in 2:
		await physics()
	var tint_in: Color = _fx_live_tint(factory, 0)
	assert_true(tint_in.a < 1.0 and tint_in.a > 0.0, "fade-in slot starts transparent")
	for i in 30:
		await physics()
	assert_true(factory.get_active_effect_count() == 0, "fade-in slot still expires on schedule")
	await _settle(factory)
	var d26b := _fx_data(Vector2.ZERO, 0.0, [], false)
	var lout := _make_layer(1, 4)
	lout.fade_out_sec = 0.2
	d26b.effect_layers = [lout]
	var v26b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d26b)
	for i in 2:
		await physics()
	assert_true(_fx_live_tint(factory, 0).a > 0.95, "fade-out slot starts solid")
	for i in 13:
		await physics()
	assert_true(_fx_live_tint(factory, 0).a < 0.95, "fade-out dims before the end")
	for i in 30:
		await physics()
	assert_true(factory.get_active_effect_count() == 0, "fade-out slot still expires")
	await _settle(factory)
	var d26c := _fx_data(Vector2.ZERO, 0.0, [], false)
	var llong := _make_layer(1, 4)
	llong.fade_out_sec = 5.0
	d26c.effect_layers = [llong]
	var v26c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d26c)
	for i in 15:
		await physics()
	assert_true(_fx_live_tint(factory, 0).a < 0.5, "oversized fade-out dims proportionally, never sticks at full")
	for i in 30:
		await physics()
	assert_true(factory.get_active_effect_count() == 0, "oversized fade-out still expires")
	await _settle(factory)
	var d26t := _fx_data(Vector2.ZERO, 0.0, [], false)
	d26t.max_life_time = 8.0
	var ltrail := _make_layer(0, 1)
	ltrail.fade_in_sec = 0.5
	d26t.effect_layers = [ltrail]
	var v26t: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d26t)
	for i in 2:
		await physics()
	assert_true(_fx_trail_tint(v26t).a < 1.0, "trail fade-in starts transparent")
	for i in 40:
		await physics()
	assert_true(absf(_fx_trail_tint(v26t).a - 1.0) < 0.05, "trail fade-in reaches solid")
	assert_true(v26t.is_bullet_status_enabled(0), "faded trail volley alive")
	await _settle(factory)
	var d26to := _fx_data(Vector2.ZERO, 0.0, [], false)
	d26to.max_life_time = 1.0
	var ltrout := _make_layer(0, 1)
	ltrout.fade_out_sec = 0.5
	d26to.effect_layers = [ltrout]
	var v26to: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d26to)
	for i in 45:
		await physics()
	assert_true(_fx_trail_tint(v26to).a < 0.9, "trail fade-out dims with the volley")
	assert_true(v26to.is_bullet_status_enabled(0), "trail volley alive mid fade-out")
	await _settle(factory)
	var d26n := _make_layer(1, 4)
	d26n.fade_in_sec = NAN
	assert_true(d26n.fade_in_sec == 0.0, "NaN layer fade-in rejected")
	d26n.fade_out_sec = -2.0
	assert_true(d26n.fade_out_sec == 0.0, "negative layer fade-out rejected")
	expect_errors_containing("fade_in_sec must be finite", 1, "NaN layer fade-in fails loud")
	expect_errors_containing("fade_out_sec must be finite", 1, "negative layer fade-out fails loud")


func test_whiten_override_pixels_rebake_fallback() -> void:
	await _preamble()
	# FX T27 whiten override: pixels, rebake, fallback
	var red := Image.create_empty(4, 4, false, Image.FORMAT_RGBA8)
	red.fill(Color(1, 0, 0, 1))
	red.set_pixel(1, 1, Color(1, 0, 0, 0.5))
	var white: Image = BulletEffectLayerData2D.whiten_image_copy(red)
	assert_true(white != null and white.get_pixel(0, 0) == Color(1, 1, 1, 1), "red bakes to white")
	assert_true(white != null and absf(white.get_pixel(1, 1).a - 0.5) < 0.01 and white.get_pixel(1, 1).r > 0.9, "alpha preserved under white")
	assert_true(BulletEffectLayerData2D.whiten_image_copy(Image.new()) == null, "empty image refused")
	assert_true(BulletEffectLayerData2D.whiten_image_copy(Image.create_empty(600, 8, false, Image.FORMAT_RGBA8)) == null, "oversized image refused")
	await _settle(factory)
	var d27 := _fx_data(Vector2.ZERO, 0.0, [], false)
	var lwhite := _make_layer(1, 4)
	d27.effect_layers = [lwhite]
	var v27: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d27)
	for i in 3:
		await physics()
	assert_true(factory.get_active_effect_count() >= 1, "plain layer fires before override")
	lwhite.override_frame_color = true
	var v27b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d27)
	for i in 3:
		await physics()
	assert_true(factory.get_active_effect_count() >= 1, "whitened layer fires after rebake")
	for i in 60:
		await physics()
	assert_true(factory.get_active_effect_count() == 0, "whitened slots expire")
	await _settle(factory)
	var sf_bad := SpriteFrames.new()
	if not sf_bad.has_animation("default"):
		sf_bad.add_animation("default")
	sf_bad.set_animation_speed("default", 10.0)
	sf_bad.set_animation_loop("default", false)
	sf_bad.add_frame("default", AtlasTexture.new())
	var lfb := BulletEffectLayerData2D.new()
	lfb.trigger = 1
	lfb.sprite_frames = sf_bad
	lfb.override_frame_color = true
	var d27b := _fx_data(Vector2.ZERO, 0.0, [lfb], false)
	var v27c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d27b)
	for i in 3:
		await physics()
	assert_true(factory.get_active_effect_count() >= 1, "unreadable frame falls back, still fires")
	for i in 30:
		await physics()
	expect_errors_containing("source image is null or empty", 2, "empty whiten image fails loud")
	expect_errors_containing("exceeds 512px", 1, "oversize whiten image fails loud")


func test_bullet_whiten_override_exact_tint_toggle_reuse() -> void:
	await _preamble()
	# FX T28 bullet whiten override: exact tint, toggle, reuse
	await _settle(factory)
	var d28 := _fx_data(Vector2.ZERO, 0.0, [], false)
	d28.sprite_frames = _make_frames(2, ["default"], Color(1, 0, 0, 1))
	d28.self_modulate = Color(0, 0, 1, 1)
	d28.override_frame_color = true
	var v28: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d28)
	assert_true(v28.get_texture().get_image().get_pixel(0, 0).r > 0.9, "red bullet art bakes white")
	assert_true(v28.get_texture().get_image().get_pixel(0, 0).g > 0.9, "white keeps all channels")
	assert_true(v28.is_bullet_status_enabled(0), "whitened volley alive")
	await _settle(factory)
	var d28b := _fx_data(Vector2.ZERO, 0.0, [], false)
	d28b.sprite_frames = _make_frames(2, ["default"], Color(1, 0, 0, 1))
	var v28b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d28b)
	assert_true(v28b.get_texture().get_image().get_pixel(0, 0).r > 0.9, "plain art starts red")
	v28b.set_override_frame_color(true)
	assert_true(v28b.get_texture().get_image().get_pixel(0, 0).r > 0.9 and v28b.get_texture().get_image().get_pixel(0, 0).g > 0.9, "live toggle whitens")
	v28b.set_override_frame_color(false)
	assert_true(v28b.get_texture().get_image().get_pixel(0, 0).g < 0.1, "live toggle restores art")
	await _settle(factory)
	var v28c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d28)
	assert_true(v28c.get_texture().get_image().get_pixel(0, 0).g > 0.9, "pooled reuse honors override")
	var v28d: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d28b)
	assert_true(v28d.get_texture().get_image().get_pixel(0, 0).g < 0.1, "reuse without override restores art")

	await idle(1)
