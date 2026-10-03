extends BlastTest
## Volley bounce suite: REAL physics (no mocks).
## Covers: defaults-off, free bounce vs wall (precedence over normal path),
## strength scaling (incl. uncapped, no clamp, curve persistence),
## max-count ping-pong between two walls, mask precedence, spawner ownership,
## precise-vs-radial normals, capsule precise branch, smooth visual pursuit,
## render continuity across snap (prev-origin proof), teleport-into-wall,
## attachment nudge tracking, cooldown-vs-consumed semantics, lifetime expiry
## inside cooldown, spawner retarget preservation, bulk bounce counts,
## inspector group coherence, runtime arm/disarm, gravity flag refresh,
## feature mixes (homing/wobble/gravity/orbit/curves), rejects + fuzz,
## pool-reuse neutrality, stale-velocity guard + queue snapshot (T36),
## cross-type matrix: platforms, slopes, boundaries, convex fallback,
## rigid awake/sleeping, static/dashing areas, tilemap opt-in (T37),
## bounce forensics + multiplier reuse + shared walls + tilemap budgets.


var _bounce_body: Array = []
var _bounce_area: Array = []
var _norm_body: Array = []
var _spawner_bounce: Array = []
var _factory_bounce: Array = []
# T36 mid-drain mutation rig: when armed, the first bounce signal flips the
# target velocity, so the second record in the same drain must still use its
# queue-time snapshot. Disarmed by default: zero impact on other phases.
var _t36_mutate := false
var _t36_done := false
var _t36_body: CharacterBody2D = null

func _on_bounce_body(body: Object, volley: DirectionalBullets2D, idx: int) -> void:
	_bounce_body.append([body, volley, idx])
	if _t36_mutate and not _t36_done and body == _t36_body and _t36_body != null:
		_t36_done = true
		_t36_body.velocity = Vector2(400, 0)

func _on_bounce_area(area: Object, volley: DirectionalBullets2D, idx: int) -> void:
	_bounce_area.append([area, volley, idx])

func _on_norm_body(body: Object, volley: DirectionalBullets2D, idx: int) -> void:
	_norm_body.append([body, volley, idx])

func _on_spawner_bounce(_t: Object, _v: DirectionalBullets2D, _i: int) -> void:
	_spawner_bounce.append(1)

func _on_factory_bounce(_t: Object, _v: DirectionalBullets2D, _i: int) -> void:
	_factory_bounce.append(1)

var _bounce_capture: Array = []

func _on_bounce_capture(_body: Object, volley: DirectionalBullets2D, idx: int) -> void:
	_bounce_capture.append([volley.debug_get_previous_origin(idx), volley.get_bullet_global_transform(idx).origin])

func _finite_volley(v: DirectionalBullets2D) -> bool:
	for i in v.get_amount_bullets():
		if not v.get_bullet_transform(i).is_finite():
			return false
		if not v.get_bullet_direction(i).is_finite():
			return false
		if not v.get_bullet_velocity(i).is_finite():
			return false
	return true

# Bullet data aimed at +X (or given angle) from a start pos.
# Bounce walls live on layer 4 (value 8); plain walls on layer 5 (value 16).
func _bounce_data(from: Vector2, angle: float, speed: float, bounce_layers: Array, mask_layers: Array, lifetime: float = 8.0) -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	d.sprite_frames = H.make_sprite_frames()
	d.transforms = [Transform2D(angle, from)]
	var sp := BulletSpeedData2D.new()
	sp.speed = speed
	sp.max_speed = 3000.0
	sp.acceleration = 0.0
	d.all_bullet_speed_data = [sp]
	d.max_life_time = lifetime
	d.texture_size = Vector2(16, 16)
	d.monitorable = true
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array(mask_layers)
	d.set_bounce_mask_from_array(bounce_layers)
	var c := CircleShape2D.new()
	c.radius = 6.0
	d.collision_shape = c
	return d

func _make_wall(pos: Vector2, layer_value: int, size: Vector2 = Vector2(20, 400), rot: float = 0.0) -> StaticBody2D:
	var wall := StaticBody2D.new()
	wall.position = pos
	wall.rotation = rot
	wall.collision_layer = layer_value
	wall.collision_mask = 2
	var col := CollisionShape2D.new()
	var box := RectangleShape2D.new()
	box.size = size
	col.shape = box
	wall.add_child(col)
	add(wall)
	return wall

func _make_tile_layer() -> TileMapLayer:
	var img := Image.create_empty(32, 32, false, Image.FORMAT_RGBA8)
	img.fill(Color(1, 1, 1, 1))
	var ts := TileSet.new()
	ts.tile_shape = TileSet.TILE_SHAPE_SQUARE
	ts.tile_size = Vector2i(32, 32)
	ts.add_physics_layer(0)
	ts.set_physics_layer_collision_layer(0, 8)
	var src := TileSetAtlasSource.new()
	src.texture = ImageTexture.create_from_image(img)
	src.texture_region_size = Vector2i(32, 32)
	src.create_tile(Vector2i(0, 0))
	ts.add_source(src)
	var td: TileData = src.get_tile_data(Vector2i(0, 0), 0)
	td.set_collision_polygons_count(0, 1)
	td.set_collision_polygon_points(0, 0, PackedVector2Array([Vector2(-16, -16), Vector2(16, -16), Vector2(16, 16), Vector2(-16, 16)]))
	var layer := TileMapLayer.new()
	layer.tile_set = ts
	add(layer)
	layer.set_cell(Vector2i(7, 9), ts.get_source_id(0), Vector2i(0, 0))
	return layer

func _clear_signals() -> void:
	_bounce_body.clear()
	_bounce_area.clear()
	_norm_body.clear()
	_spawner_bounce.clear()
	_factory_bounce.clear()
	_bounce_capture.clear()

# Park on idle, wipe every flying volley, clear signal buckets: each phase
# then observes ONLY the volley it spawns (stray infinite-lifetime volleys
# from earlier phases would otherwise satisfy signal waits early).
func _settle(factory: BulletFactory2D) -> void:
	await idle(1)
	await idle(1)
	factory.free_active_bullets()
	_clear_signals()
	await physics()



func _connect_signals() -> void:
	_clear_signals()
	factory.directional_bounce_body_entered.connect(_on_bounce_body)
	factory.directional_bounce_area_entered.connect(_on_bounce_area)
	factory.directional_body_entered.connect(_on_norm_body)


## Shared arena for the T1..T28 tests (split out of one 1,700-line test):
## the signal buckets plus T0's bounce wall (layer value 8) at (200, 0) that
## every section implicitly relied on. Returns the wall for the sections that
## move it.
func _arena() -> StaticBody2D:
	_connect_signals()
	var wall := _make_wall(Vector2(200, 0), 8)
	await physics()
	return wall


func test_t0_defaults_feature_off_normal_path_intact() -> void:
	# BOUNCE T0 defaults: feature off, normal path intact
	_connect_signals()
	var d0 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [], [4])
	assert_true(d0.bounce_mask == 0, "bounce_mask defaults 0")
	assert_true(d0.bounce_strength == 1.0, "bounce_strength defaults 1")
	assert_true(d0.bounce_hit_consumed == false, "bounce_hit_consumed defaults false")
	assert_true(d0.bounce_max_count == 0, "bounce_max_count defaults 0 (unlimited)")
	assert_true(d0.bounce_mode == 0, "bounce_mode defaults radial")
	assert_true(d0.bounce_rotate_texture == true, "bounce_rotate_texture defaults true")
	assert_true(d0.bounce_rotation_smooth == 0.0, "bounce_rotation_smooth defaults 0")
	assert_true(d0.bounce_randomness_deg == 0.0, "bounce_randomness defaults 0")
	var wall := _make_wall(Vector2(200, 0), 8)
	await physics()
	var v0: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d0)
	assert_true(v0 != null, "plain spawn ok")
	assert_true(v0.get_bounce_mask() == 0, "live mirror reseeded to 0")
	for i in 90:
		await physics()
		if not v0.is_bullet_status_enabled(0):
			break
	assert_true(_norm_body.size() >= 1, "normal body signal fires with bounce off")
	assert_true(_bounce_body.is_empty(), "no bounce signal with bounce off")
	assert_true(v0.bullet_get_bounce_count(0) == 0, "bounce count stays 0")
	assert_true(v0.debug_get_bounce_info(0).get("bounce_enabled", true) == false, "debug info reports disabled")
	assert_true(v0.debug_get_bounce_info(99).get("valid", true) == false, "debug info OOB invalid")
	assert_true(v0.bullet_get_bounce_count(99) == 0, "bounce count OOB reads 0")
	expect_error_sequence(["Invalid bullet index in bullet_get_bounce_count"])


func test_t1_free_bounce_wall_reflects_bullet_lives_no_hit_consumed() -> void:
	# BOUNCE T1 free bounce: wall reflects, bullet lives, no hit consumed
	await _arena()
	await _settle(factory)
	var d1 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	var v1: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d1)
	for i in 120:
		await physics()
		if _bounce_body.size() >= 1:
			break
	assert_true(_bounce_body.size() >= 1, "bounce body signal fired")
	if _bounce_body.size() >= 1:
		assert_true(_bounce_body[0][1] == v1 and (_bounce_body[0][2] as int) == 0, "bounce slim payload (volley, index 0)")
	assert_true(v1.is_bullet_status_enabled(0), "bullet alive after free bounce")
	assert_true(v1.bullet_get_bounce_count(0) >= 1, "bounce count tracked")
	assert_true(v1.get_bullet_collision_count(0) == 0, "free bounce consumes no hit")
	assert_true(_norm_body.is_empty(), "normal signal suppressed on free bounce (precedence)")
	var dir1: Vector2 = v1.get_bullet_direction(0)
	assert_true(dir1.x < -0.9 and absf(dir1.y) < 0.3, "heading reflected to -X")
	var spd1: float = v1.get_bullet_velocity(0).length()
	assert_true(absf(spd1 - 300.0) < 30.0, "elastic strength preserves speed")
	assert_true(v1.get_bullet_velocity(0).is_finite() and v1.get_bullet_transform(0).is_finite(), "post-bounce state finite")


func test_t1b_body_area_pair_on_one_target_single_bounce_no_double_flip() -> void:
	# BOUNCE T1b body+area pair on one target: single bounce, no double-flip
	await _arena()
	await _settle(factory)
	var eye := Area2D.new()
	eye.position = Vector2(200, 0)
	eye.collision_layer = 8
	eye.monitoring = true
	eye.monitorable = true
	var ecol := CollisionShape2D.new()
	var ebox := RectangleShape2D.new()
	ebox.size = Vector2(20, 400)
	ecol.shape = ebox
	eye.add_child(ecol)
	add(eye)
	await physics()
	var v1b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4]))
	for i in 120:
		await physics()
		if _bounce_body.size() >= 1:
			break
	assert_true(_bounce_body.size() >= 1, "bounce fired with paired shapes")
	var dir1b: Vector2 = v1b.get_bullet_direction(0)
	assert_true(dir1b.x < -0.5, "no double-flip back to +X (per-tick guard)")
	assert_true(v1b.bullet_get_bounce_count(0) == 1, "exactly one bounce counted for the pair")
	eye.queue_free()
	await idle(1)


func test_t2_consumed_bounce_bounce_normal_fire_bullet_dies_at_max_1() -> void:
	# BOUNCE T2 consumed bounce: bounce + normal fire, bullet dies at max 1
	await _arena()
	await _settle(factory)
	var d2 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d2.bounce_hit_consumed = true
	var v2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d2)
	for i in 120:
		await physics()
		if not v2.is_bullet_status_enabled(0):
			break
	assert_true(_bounce_body.size() >= 1, "bounce signal fired on consumed hit")
	assert_true(_norm_body.size() >= 1, "normal signal also fires when consumed")
	assert_true(not v2.is_bullet_status_enabled(0), "consumed bounce kills at max 1")
	assert_true(v2.get_bullet_collision_count(0) >= 1, "consumed hit counted")


func test_t3_strength_scaling() -> void:
	# BOUNCE T3 strength scaling
	await _arena()
	await _settle(factory)
	var d3 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d3.bounce_strength = 0.5
	var v3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d3)
	for i in 120:
		await physics()
		if _bounce_body.size() >= 1:
			break
	var spd3: float = v3.get_bullet_velocity(0).length()
	assert_true(absf(spd3 - 150.0) < 25.0, "strength 0.5 halves speed")
	await _settle(factory)
	var d3b := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d3b.bounce_strength = 0.0
	var v3b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d3b)
	for i in 120:
		await physics()
		if _bounce_body.size() >= 1:
			break
	assert_true(v3b.get_bullet_velocity(0).length() < 5.0, "strength 0 dead-stops")
	assert_true(v3b.is_bullet_status_enabled(0), "dead-stop bullet stays alive (free)")


func test_t4_ping_pong_between_two_walls_max_count_gates_back_to_normal() -> void:
	# BOUNCE T4 ping-pong between two walls, max_count gates back to normal
	await _arena()
	await _settle(factory)
	var wall_l := _make_wall(Vector2(-200, 0), 8)
	await physics()
	var d4 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d4.bounce_max_count = 2
	d4.set_bullet_max_collision_count(0)
	var v4: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d4)
	for i in 400:
		await physics()
		if v4.bullet_get_bounce_count(0) >= 2 and _norm_body.size() >= 1:
			break
	assert_true(v4.bullet_get_bounce_count(0) == 2, "exactly 2 bounces then budget exhausted")
	assert_true(_norm_body.size() >= 1, "post-budget hit takes normal path (still alive, max 0)")
	assert_true(v4.is_bullet_status_enabled(0), "ping-pong survivor alive (infinite hits)")
	wall_l.queue_free()
	await idle(1)


func test_t5_mask_precedence_bounce_layer_wins_plain_layer_stays_normal() -> void:
	# BOUNCE T5 mask precedence: bounce layer wins, plain layer stays normal
	var wall := await _arena()
	await _settle(factory)
	var plain := _make_wall(Vector2(200, 220), 16, Vector2(20, 120))
	await physics()
	var v5: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2(0, 220), 0.0, 300.0, [4], [4, 5]))
	for i in 120:
		await physics()
		if _norm_body.size() >= 1:
			break
	assert_true(_bounce_body.is_empty(), "plain-layer wall never bounces")
	assert_true(_norm_body.size() >= 1, "plain-layer wall reports normally")
	plain.queue_free()
	await idle(1)


func test_t6_spawner_ownership_spawner_signal_only_factory_silent() -> void:
	# BOUNCE T6 spawner ownership: spawner signal only, factory silent
	await _arena()
	await _settle(factory)
	factory.directional_bounce_body_entered.connect(_on_factory_bounce)
	var spawner := BulletSpawner2D.new()
	spawner.bullet_factory_path = factory.get_path()
	spawner.shooting_enabled = false
	spawner.pattern_source = BulletSpawner2D.PATTERN_FROM_SELF
	spawner.position = Vector2.ZERO
	add(spawner)
	await idle(1)
	spawner.bounce_body_entered.connect(_on_spawner_bounce)
	spawner.spawn_data = _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	assert_true(spawner.shoot_once(), "spawner volley fired")
	for i in 120:
		await physics()
		if _spawner_bounce.size() >= 1:
			break
	assert_true(_spawner_bounce.size() >= 1, "spawner bounce signal fired")
	assert_true(_factory_bounce.is_empty(), "factory stays silent for spawner volleys")
	spawner.queue_free()
	await idle(1)


func test_t7_precise_mode_on_45_degree_wall_reflects_across_face() -> void:
	# BOUNCE T7 precise mode on 45-degree wall reflects across face
	var wall := await _arena()
	await _settle(factory)
	wall.position.x = 2000.0
	await physics()
	var diag := _make_wall(Vector2(200, 0), 8, Vector2(20, 400), PI / 4.0)
	await physics()
	var d7 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d7.bounce_mode = 1
	var v7: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d7)
	for i in 120:
		await physics()
		if _bounce_body.size() >= 1:
			break
	var dir7: Vector2 = v7.get_bullet_direction(0)
	assert_true(absf(dir7.y) > 0.85 and absf(dir7.x) < 0.4, "precise face normal turns +X into vertical")
	diag.queue_free()
	wall.position.x = 200.0
	await idle(1)
	await _settle(factory)
	var v7b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4]))
	for i in 120:
		await physics()
		if _bounce_body.size() >= 1:
			break
	var dir7b: Vector2 = v7b.get_bullet_direction(0)
	assert_true(dir7b.x < -0.9, "radial mode on flat wall still reflects to -X")


func test_t8_smooth_visual_pursuit_lags_then_converges() -> void:
	# BOUNCE T8 smooth visual pursuit lags then converges
	await _arena()
	await _settle(factory)
	var d8 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d8.bounce_rotation_smooth = 3.0
	var v8: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d8)
	for i in 120:
		await physics()
		if _bounce_body.size() >= 1:
			break
	await physics()
	await physics()
	var visual_ang: float = v8.get_bullet_transform(0).get_rotation()
	var logic_ang: float = v8.get_bullet_direction(0).angle()
	var lag: float = absf(wrapf(visual_ang - logic_ang, -PI, PI))
	assert_true(lag > 0.1, "smooth visual lags ballistics right after bounce")
	for i in 150:
		await physics()
	var visual_ang2: float = v8.get_bullet_transform(0).get_rotation()
	var logic_ang2: float = v8.get_bullet_direction(0).angle()
	var lag2: float = absf(wrapf(visual_ang2 - logic_ang2, -PI, PI))
	assert_true(lag2 < 0.15, "smooth visual converges onto heading")
	assert_true(v8.debug_get_bounce_info(0).get("visual_pending", true) == false, "pursuit flag clears on arrival")


func test_t9_mixes_homing_wobble_gravity_curves_stay_finite() -> void:
	# BOUNCE T9 mixes: homing + wobble + gravity + curves stay finite
	await _arena()
	await _settle(factory)
	var hunter := Node2D.new()
	# Ahead of the wall: homing drives the bullet INTO the wall (a target
	# behind would U-turn it away and it would never reach the wall).
	hunter.position = Vector2(400, 0)
	add(hunter)
	var d9 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d9.gravity = Vector2(0, 200.0)
	var wob := BulletWobbleData2D.new()
	wob.enabled = true
	wob.amplitude = 40.0
	wob.frequency_hz = 3.0
	d9.shared_bullet_wobble_data = wob
	var v9: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d9)
	v9.shared_homing_deque_push_back_node2d_target(hunter)
	v9.set_homing_take_control_of_texture_rotation(true)
	v9.set_homing_smoothing(4.0)
	for i in 150:
		await physics()
	assert_true(_bounce_body.size() >= 1, "homing+wobble+gravity volley still bounces")
	assert_true(v9.get_bullet_direction(0).is_finite() and v9.get_bullet_velocity(0).is_finite(), "mixed post-bounce state finite")
	assert_true(v9.get_bullet_transform(0).is_finite(), "mixed transform finite")
	hunter.queue_free()
	await _settle(factory)
	var d9b := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	var v9b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d9b)
	v9b.set_bounce_rotate_texture(false)
	for i in 120:
		await physics()
		if _bounce_body.size() >= 1:
			break
	assert_true(_bounce_body.size() >= 1, "rotate_texture=false still bounces ballistically")


func test_t10_rejects_fuzz_pool_reuse_neutrality() -> void:
	# BOUNCE T10 rejects + fuzz + pool-reuse neutrality
	await _arena()
	var dr := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	var keep_strength: float = dr.bounce_strength
	dr.set_bounce_strength(NAN)
	expect_error_sequence(["DirectionalBulletsData2D: bounce_strength must be finite and >= 0"])
	assert_true(dr.bounce_strength == keep_strength, "NaN strength rejected")
	dr.set_bounce_strength(-1.0)
	expect_error_sequence(["DirectionalBulletsData2D: bounce_strength must be finite and >= 0"])
	assert_true(dr.bounce_strength == keep_strength, "negative strength rejected")
	dr.set_bounce_strength(5.0)
	assert_true(dr.bounce_strength == 5.0, "strength uncapped (5.0 accepted)")
	for p in dr.get_property_list():
		if str(p.get("name", "")) == "bounce_strength":
			assert_true(int(p.get("hint", -1)) == PROPERTY_HINT_NONE, "no editor cap on strength (plain float, user decides)")
	var keep_mask: int = dr.bounce_mask
	dr.set_bounce_mask(-1)
	expect_error_sequence(["DirectionalBulletsData2D: bounce_mask must be >= 0"])
	assert_true(dr.bounce_mask == keep_mask, "negative mask rejected")
	dr.set_bounce_mode(7)
	expect_error_sequence(["DirectionalBulletsData2D: bounce_mode must be"])
	assert_true(dr.bounce_mode == 0, "bad mode rejected")
	dr.set_bounce_randomness_deg(999.0)
	expect_error_sequence(["DirectionalBulletsData2D: bounce_randomness_deg must be finite"])
	assert_true(dr.bounce_randomness_deg == 0.0, "randomness > 180 rejected")
	dr.set_bounce_cooldown_sec(9.0)
	expect_error_sequence(["DirectionalBulletsData2D: bounce_cooldown_sec must be finite in [0, 1]"])
	assert_true(dr.bounce_cooldown_sec == 0.05, "cooldown > 1 rejected")
	dr.set_bounce_max_count(-3)
	expect_error_sequence(["DirectionalBulletsData2D: bounce_max_count must be"])
	assert_true(dr.bounce_max_count == 0, "negative max count rejected")
	await _settle(factory)
	var vr: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(dr)
	for i in 120:
		await physics()
		if _bounce_body.size() >= 1:
			break
	assert_true(_bounce_body.size() >= 1, "rejected-value volley still bounces sanely")
	assert_true(vr.get_bullet_velocity(0).is_finite(), "post-reject state finite")
	# Pool reuse: free the bounce volley on idle, spawn a plain one (pops the
	# same bucket), bounce config must reseed to off.
	await idle(1)
	await idle(1)
	factory.free_active_bullets()
	await idle(1)
	await idle(1)
	var plain2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2.ZERO, 0.0, 300.0, [], [4]))
	assert_true(plain2.get_bounce_mask() == 0, "pooled reuse reseeds bounce off")
	assert_true(plain2.bullet_get_bounce_count(0) == 0, "pooled reuse zeroes bounce ledger")
	assert_true(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after bounce churn")


func test_t11_uncapped_strength_5x_reflection_50x_never_clamps() -> void:
	# BOUNCE T11 uncapped strength: 5x reflection, 50x never clamps
	await _arena()
	await _settle(factory)
	var d11 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d11.bounce_strength = 5.0
	var v11: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d11)
	for i in 120:
		await physics()
		if _bounce_body.size() >= 1:
			break
	var spd11: float = v11.get_bullet_velocity(0).length()
	assert_true(absf(spd11 - 1500.0) < 120.0, "strength 5 scales speed 5x")
	assert_true(v11.get_bullet_speed_data(0).max_speed >= 1500.0 - 120.0, "bounce raises cached max_speed to the new speed")
	for i in 60:
		await physics()
	var spd11_late: float = v11.get_bullet_velocity(0).length()
	assert_true(absf(spd11_late - 1500.0) < 150.0, "strength 5 boost persists (no tick erosion)")
	await _settle(factory)
	var d11b := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d11b.bounce_strength = 50.0
	var v11b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d11b)
	for i in 120:
		await physics()
		if _bounce_body.size() >= 1:
			break
	var spd11b: float = v11b.get_bullet_velocity(0).length()
	assert_true(v11b.get_bullet_velocity(0).is_finite(), "strength 50 stays finite")
	assert_true(absf(spd11b - 15000.0) < 1500.0, "strength 50 never clamps (15000, uncapped)")
	assert_true(v11b.get_bullet_speed_data(0).max_speed >= 15000.0 - 1500.0, "strength 50 raises max_speed too")


func test_t11b_boost_survives_speed_curve_overwrite() -> void:
	# BOUNCE T11b boost survives speed-curve overwrite
	await _arena()
	await _settle(factory)
	var d11c := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d11c.bounce_strength = 2.0
	var v11c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d11c)
	for i in 120:
		await physics()
		if _bounce_body.size() >= 1:
			break
	assert_true(absf(v11c.get_bullet_velocity(0).length() - 600.0) < 60.0, "strength 2 doubles speed")
	var flat := BulletCurvesData2D.new()
	var flat_curve := Curve.new()
	flat_curve.min_value = 0.0
	flat_curve.max_value = 2000.0
	flat_curve.add_point(Vector2(0, 300))
	flat_curve.add_point(Vector2(1, 300))
	flat.movement_speed_curve = flat_curve
	flat.movement_use_unit_curve = false
	v11c.set_shared_bullet_curves_data(flat)
	for i in 30:
		await physics()
	assert_true(absf(v11c.get_bullet_velocity(0).length() - 600.0) < 90.0, "curve rewrite keeps the x2 boost")
	v11c.remove_shared_bullet_curves_data()
	for i in 10:
		await physics()
	assert_true(absf(v11c.get_bullet_velocity(0).length() - 600.0) < 90.0, "boost survives curve removal")


func test_t12_cooldown_vs_consumed_pair_counts_once_re_hit_counts() -> void:
	# BOUNCE T12 cooldown vs consumed: pair counts once, re-hit counts
	await _arena()
	await _settle(factory)
	var eye2 := Area2D.new()
	eye2.position = Vector2(200, 0)
	eye2.collision_layer = 8
	eye2.monitoring = true
	eye2.monitorable = true
	var ecol2 := CollisionShape2D.new()
	var ebox2 := RectangleShape2D.new()
	ebox2.size = Vector2(20, 400)
	ecol2.shape = ebox2
	eye2.add_child(ecol2)
	add(eye2)
	await physics()
	var d12 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d12.bounce_hit_consumed = true
	d12.set_bullet_max_collision_count(10)
	var v12: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d12)
	for i in 120:
		await physics()
		if _bounce_body.size() >= 1:
			break
	assert_true(v12.bullet_get_bounce_count(0) == 1, "pair consumed: exactly one bounce")
	assert_true(v12.get_bullet_collision_count(0) == 1, "pair consumed: exactly one hit (no double count)")
	eye2.queue_free()
	await idle(1)
	await _settle(factory)
	var d12b := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d12b.bounce_hit_consumed = true
	d12b.set_bullet_max_collision_count(10)
	# Legal maximum (range [0, 1]): this used to say 5.0, which the setter
	# rejected, so the test silently ran with the 0.05 default cooldown.
	d12b.bounce_cooldown_sec = 1.0
	var v12b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d12b)
	for i in 120:
		await physics()
		if _bounce_body.size() >= 1:
			break
	assert_true(v12b.bullet_get_bounce_count(0) == 1, "first bounce recorded")
	v12b.teleport_bullet(0, Vector2(195, 0))
	for i in 40:
		await physics()
	assert_true(v12b.bullet_get_bounce_count(0) == 1, "cooldown re-hit does not re-bounce")
	assert_true(v12b.get_bullet_collision_count(0) >= 2, "cooldown re-hit still counts when consumed")


func test_t13_snap_keeps_render_position_continuous() -> void:
	# BOUNCE T13 snap keeps render position continuous
	await _arena()
	await _settle(factory)
	factory.set_use_physics_interpolation_runtime(true)
	factory.directional_bounce_body_entered.connect(_on_bounce_capture)
	var v13: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4]))
	for i in 5:
		await physics()
	for i in 120:
		await physics()
		if _bounce_body.size() >= 1:
			break
	# Captured inside the bounce emission (drain time): prev must still hold
	# the tick-start origin while current already carries the escape nudge.
	# A snap that overwrote prev with current would read a zero gap here.
	assert_true(_bounce_capture.size() >= 1, "bounce capture recorded at emit time")
	if _bounce_capture.size() >= 1:
		var cap_prev: Vector2 = _bounce_capture[0][0]
		var cap_cur: Vector2 = _bounce_capture[0][1]
		var cap_gap: float = cap_cur.x - cap_prev.x
		# Gap = one tick of +X travel (~+5px) plus the escape nudge (~-8px):
		# about -3px. Zero would mean the snap overwrote prev with current.
		assert_true(cap_gap < -0.5 and cap_gap > -8.0, "prev origin preserved across snap (render lerps)")
		assert_true(cap_prev.is_finite() and cap_cur.is_finite(), "prev/current finite")
	factory.set_use_physics_interpolation_runtime(false)


func test_t14_precise_mode_on_capsule_wall_uses_cap_normal() -> void:
	# BOUNCE T14 precise mode on capsule wall uses cap normal
	var wall := await _arena()
	await _settle(factory)
	wall.position.x = 2000.0
	await physics()
	var cap_wall := StaticBody2D.new()
	cap_wall.position = Vector2(200, 0)
	cap_wall.collision_layer = 8
	cap_wall.collision_mask = 2
	var capcol := CollisionShape2D.new()
	var capshape := CapsuleShape2D.new()
	capshape.radius = 20.0
	capshape.height = 200.0
	capcol.shape = capshape
	cap_wall.add_child(capcol)
	add(cap_wall)
	await physics()
	var d14 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d14.bounce_mode = 1
	var v14: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d14)
	for i in 120:
		await physics()
		if _bounce_body.size() >= 1:
			break
	var dir14: Vector2 = v14.get_bullet_direction(0)
	assert_true(_bounce_body.size() >= 1, "capsule wall bounces in precise mode")
	assert_true(dir14.x < -0.9 and absf(dir14.y) < 0.3, "capsule side normal reflects to -X")
	cap_wall.queue_free()
	wall.position.x = 200.0
	await idle(1)


func test_t15_teleport_into_wall_bounces_fresh_overlap() -> void:
	# BOUNCE T15 teleport into wall bounces fresh overlap
	await _arena()
	await _settle(factory)
	var v15: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4]))
	v15.teleport_bullet(0, Vector2(195, 0))
	for i in 60:
		await physics()
		if _bounce_body.size() >= 1:
			break
	assert_true(_bounce_body.size() >= 1, "teleported overlap bounces")
	assert_true(v15.bullet_get_bounce_count(0) >= 1, "teleport bounce counted")
	assert_true(v15.get_bullet_direction(0).x < -0.5, "teleport bounce heads out")


func test_t16_attachment_rides_the_escape_nudge() -> void:
	# BOUNCE T16 attachment rides the escape nudge
	await _arena()
	await _settle(factory)
	var Probe := preload("res://tests/scenes/attachment_probe.gd")
	var probe_node := BulletAttachment2D.new()
	probe_node.set_script(Probe)
	var pack := PackedScene.new()
	assert_true(pack.pack(probe_node) == OK, "probe scene packs")
	probe_node.queue_free()
	var v16: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4]))
	v16.bullet_set_attachment(0, pack, Vector2.ZERO, true)
	for i in 120:
		await physics()
		if _bounce_body.size() >= 1:
			break
	for i in 5:
		await physics()
	var att16: BulletAttachment2D = v16.bullet_get_attachment(0) as BulletAttachment2D
	assert_true(att16 != null, "attachment present after bounce")
	if att16 != null:
		var miss16: float = att16.global_position.distance_to(v16.get_bullet_global_transform(0).origin)
		assert_true(miss16 < 5.0, "attachment tracks nudged bullet")


func test_t17_lifetime_expiry_inside_cooldown_is_clean() -> void:
	# BOUNCE T17 lifetime expiry inside cooldown is clean
	await _arena()
	await _settle(factory)
	var d17 := _bounce_data(Vector2.ZERO, 0.0, 600.0, [4], [4])
	# Legal maximum (range [0, 1]): this used to say 5.0, which the setter
	# rejected, so the test silently ran with the 0.05 default cooldown.
	d17.bounce_cooldown_sec = 1.0
	d17.max_life_time = 0.5
	var v17: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d17)
	for i in 120:
		await physics()
		if _bounce_body.size() >= 1:
			break
	assert_true(_bounce_body.size() >= 1, "fast bullet bounces before expiry")
	for i in 90:
		await physics()
		if not v17.is_bullet_status_enabled(0):
			break
	assert_true(not v17.is_bullet_status_enabled(0), "bullet expires inside cooldown window")
	assert_true(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after cooldown expiry")


func test_t18_spawner_retarget_keeps_bounce_config_live() -> void:
	# BOUNCE T18 spawner retarget keeps bounce config live
	await _arena()
	await _settle(factory)
	var sp18 := BulletSpawner2D.new()
	sp18.bullet_factory_path = factory.get_path()
	sp18.shooting_enabled = false
	sp18.pattern_source = BulletSpawner2D.PATTERN_FROM_SELF
	sp18.position = Vector2.ZERO
	sp18.homing_enabled = true
	sp18.homing_target_source = BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION
	sp18.homing_global_position = Vector2(400, 0)
	add(sp18)
	await idle(1)
	sp18.spawn_data = _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	assert_true(sp18.shoot_once(), "homing spawner volley fired")
	var live18: Array = sp18.get_live_volleys()
	assert_true(live18.size() >= 1, "volley tracked")
	sp18.retarget_live_volleys()
	var rv18: DirectionalBullets2D = live18[0] as DirectionalBullets2D
	assert_true(rv18.get_bounce_mask() == 8, "retarget keeps bounce mask")
	assert_true(rv18.debug_get_bounce_info(0).get("bounce_enabled", false) == true, "retarget keeps bounce armed")
	sp18.bounce_body_entered.connect(_on_spawner_bounce)
	for i in 150:
		await physics()
		if _spawner_bounce.size() >= 1:
			break
	assert_true(_spawner_bounce.size() >= 1, "retargeted volley still bounces")
	sp18.queue_free()
	await idle(1)


func test_t19_bulk_bounce_counts() -> void:
	# BOUNCE T19 bulk bounce counts
	await _arena()
	await _settle(factory)
	var d19 := DirectionalBulletsData2D.new()
	d19.sprite_frames = H.make_sprite_frames()
	d19.transforms = [Transform2D(0.0, Vector2.ZERO), Transform2D(0.0, Vector2(0, 24)), Transform2D(0.0, Vector2(0, 48))]
	var speeds19: Array = []
	for i in 3:
		var sp19 := BulletSpeedData2D.new()
		sp19.speed = 300.0
		sp19.max_speed = 3000.0
		speeds19.append(sp19)
	d19.all_bullet_speed_data = speeds19
	d19.max_life_time = 8.0
	d19.texture_size = Vector2(16, 16)
	d19.monitorable = true
	d19.set_collision_layer_from_array([2])
	d19.set_collision_mask_from_array([4])
	d19.set_bounce_mask_from_array([4])
	var shape19 := CircleShape2D.new()
	shape19.radius = 6.0
	d19.collision_shape = shape19
	var v19: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d19)
	var counts19_init: Array = v19.all_bullets_get_bounce_count()
	assert_true(counts19_init.size() == 3 and int(counts19_init[0]) == 0 and int(counts19_init[1]) == 0 and int(counts19_init[2]) == 0, "bulk counts start at zero")
	for i in 150:
		await physics()
		if v19.bullet_get_bounce_count(0) >= 1 and v19.bullet_get_bounce_count(1) >= 1 and v19.bullet_get_bounce_count(2) >= 1:
			break
	var counts19: Array = v19.all_bullets_get_bounce_count()
	assert_true(counts19.size() == 3 and int(counts19[0]) == 1 and int(counts19[1]) == 1 and int(counts19[2]) == 1, "bulk counts track every bullet")


func test_t20_inspector_groups_stay_coherent() -> void:
	# BOUNCE T20 inspector groups stay coherent
	await _arena()
	var data20 := DirectionalBulletsData2D.new()
	data20.sprite_frames = H.make_sprite_frames()
	var props20: Array = data20.get_property_list()
	var bounce_group_at := -1
	var first_bounce_at := -1
	var collision_at := -1
	for i in props20.size():
		var pname := str(props20[i].get("name", ""))
		var usage: int = int(props20[i].get("usage", 0))
		if (usage & PROPERTY_USAGE_GROUP) != 0 and pname == "Bounce and Ricochet":
			bounce_group_at = i
		if first_bounce_at < 0 and pname == "bounce_mask":
			first_bounce_at = i
		if pname == "collision_mask":
			collision_at = i
	assert_true(bounce_group_at >= 0, "bounce group header registered")
	assert_true(first_bounce_at > bounce_group_at, "bounce props follow their group")
	# Data class: exact group order follows the setup workflow, and every
	# related shared/per-bullet/tile triplet sticks together with nothing
	# foreign interleaved between its members.
	var data_groups: Array = []
	var data_group_of := {}
	var data_current := ""
	for p in props20:
		var pname := str(p.get("name", ""))
		var pusage: int = int(p.get("usage", 0))
		if (pusage & PROPERTY_USAGE_GROUP) != 0:
			data_current = pname
			if not data_groups.has(pname):
				data_groups.append(pname)
		elif pname != "":
			data_group_of[pname] = data_current
	var want_data_groups := ["Movement Speed", "Bullet Rotation", "Wobble", "Gravity", "Bounce and Ricochet", "Movement Pattern Paths", "Homing"]
	assert_true(data_groups.slice(0, want_data_groups.size()) == want_data_groups, "data group order matches workflow")
	# Group titles must be unique across the FULL list (derived + base):
	# two sections with one name is exactly the confusion being removed.
	var seen_groups := {}
	var duplicate_groups: Array = []
	for p in props20:
		if (int(p.get("usage", 0)) & PROPERTY_USAGE_GROUP) != 0:
			var gname := str(p.get("name", ""))
			if seen_groups.has(gname):
				duplicate_groups.append(gname)
			seen_groups[gname] = true
	assert_true(duplicate_groups.is_empty(), "no duplicate group titles on the resource")
	# Rotation triplet is split across classes BY DESIGN: the per-bullet
	# arrays live on the base (BlockBullets spins from the shared base spawn
	# path), the shared fallback lives here. Moving them together would break
	# BlockBullets. Pin the split so nobody "fixes" it into a regression.
	assert_true(str(data_group_of.get("shared_bullet_rotation_data", "")) == "Bullet Rotation", "shared rotation grouped with steering")
	assert_true(str(data_group_of.get("all_bullet_rotation_data", "")) == "Per-Bullet Rotation", "per-bullet rotation grouped with arrays")
	# Every bounce knob lives in the Bounce group (all 12, both classes):
	# a stray bounce prop in Movement Speed would confuse setup order.
	var bounce_props := ["bounce_mask", "bounce_strength", "bounce_push_assist", "bounce_charge_amplify", "bounce_hit_consumed", "bounce_max_count", "bounce_mode", "bounce_rotate_texture", "bounce_rotation_smooth", "bounce_randomness_deg", "bounce_cooldown_sec", "bounce_debounce_sec"]
	var bounce_homed := true
	for bname in bounce_props:
		if str(data_group_of.get(bname, "")) != "Bounce and Ricochet":
			bounce_homed = false
	assert_true(bounce_homed, "all 12 bounce props grouped with Bounce")
	var triplet_families := [
		["shared_bullet_speed_data", "all_bullet_speed_data", "tile_all_bullet_speed_data"],
		["shared_bullet_curves_data", "all_bullet_curves_data", "tile_all_bullet_curves_data"],
		["shared_bullet_wobble_data", "all_bullet_wobble_data", "tile_all_bullet_wobble_data"],
		["gravity", "all_bullet_gravity", "tile_all_bullet_gravity"],
		["shared_movement_pattern_path", "all_bullet_movement_pattern_paths", "tile_all_bullet_movement_pattern_paths"],
		["shared_movement_pattern_face_movement_direction", "all_bullet_movement_pattern_face_movement_directions", "tile_all_bullet_movement_pattern_face_movement_directions"],
		["shared_movement_pattern_repeat", "all_bullet_movement_pattern_repeats", "tile_all_bullet_movement_pattern_repeats"],
	]
	var triplet_ok := true
	for fam in triplet_families:
		var idxs: Array = []
		for pname in props20:
			var sname := str(pname.get("name", ""))
			if fam.has(sname):
				idxs.append(props20.find(pname))
		idxs.sort()
		for k in range(1, idxs.size()):
			if int(idxs[k]) != int(idxs[k - 1]) + 1:
				triplet_ok = false
		var fgroup := ""
		for sname in fam:
			var gname := str(data_group_of.get(sname, "?"))
			if fgroup == "":
				fgroup = gname
			elif gname != fgroup:
				triplet_ok = false
	assert_true(triplet_ok, "related triplets stick together in one group")
	# Base data groups: Collision header precedes the collision props.
	var base20 := MultiMeshBulletsData2D.new()
	var base_props: Array = base20.get_property_list()
	var collision_group_idx := -1
	var layer_idx := -1
	var mask_idx := -1
	for i in base_props.size():
		var pname := str(base_props[i].get("name", ""))
		var usage: int = int(base_props[i].get("usage", 0))
		if (usage & PROPERTY_USAGE_GROUP) != 0 and pname == "Collision" and collision_group_idx < 0:
			collision_group_idx = i
		if pname == "collision_layer" and layer_idx < 0:
			layer_idx = i
		if pname == "collision_mask" and mask_idx < 0:
			mask_idx = i
	assert_true(collision_group_idx >= 0 and collision_group_idx < layer_idx and layer_idx < mask_idx, "base Collision group precedes layer props")
	# Base class: exact group order follows the setup workflow, material last.
	var base_groups: Array = []
	var base_current := ""
	for p in base_props:
		var pname := str(p.get("name", ""))
		var pusage: int = int(p.get("usage", 0))
		if (pusage & PROPERTY_USAGE_GROUP) != 0:
			base_current = pname
			if not base_groups.has(pname):
				base_groups.append(pname)
	assert_true(base_groups.slice(0, 7) == ["Bullets", "Appearance", "Collision", "Attachments", "Sprite Effects", "Per-Bullet Rotation", "Rendering and Material"], "base group order matches workflow")
	var base_group_of := {}
	var base_cur := ""
	for p in base_props:
		var pname := str(p.get("name", ""))
		var pusage: int = int(p.get("usage", 0))
		if (pusage & PROPERTY_USAGE_GROUP) != 0:
			base_cur = pname
		elif pname != "":
			base_group_of[pname] = base_cur
	assert_true(base_group_of.get("max_life_time", "") == "Appearance", "lifetime lives with Appearance")
	assert_true(base_group_of.get("z_index", "") == "Appearance", "z-index lives with Appearance")
	assert_true(base_group_of.get("shared_bullets_custom_data", "") == "Collision", "custom data lives with Collision")
	assert_true(base_group_of.get("rotate_only_textures", "") == "Appearance", "rotate-only flag lives with Appearance")
	assert_true(base_group_of.get("is_texture_rotation_permanent", "") == "Appearance", "permanent rotation flag lives with Appearance")
	assert_true(base_group_of.get("stop_rotation_when_max_reached", "") == "Per-Bullet Rotation", "stop flag lives with rotation")
	# Live instance mirrors the data workflow order.
	var vinst: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2(0, 9000), 0.0, 1.0, [], [4]))
	var inst_groups: Array = []
	for p in vinst.get_property_list():
		var pname := str(p.get("name", ""))
		var pusage: int = int(p.get("usage", 0))
		if (pusage & PROPERTY_USAGE_GROUP) != 0 and not inst_groups.has(pname):
			inst_groups.append(pname)
	assert_true(inst_groups.slice(0, 7) == ["Movement Speed", "Bullet Rotation", "Wobble", "Gravity", "Bounce and Ricochet", "Movement Pattern Paths", "Homing"], "instance group order mirrors data")
	# No dangling: base props must never fall into the last derived group.
	# Tracks the current header across the FULL list (derived + base).
	var full_current := ""
	var danglers: Array = []
	for p in props20:
		var pname := str(p.get("name", ""))
		var pusage: int = int(p.get("usage", 0))
		if (pusage & PROPERTY_USAGE_GROUP) != 0:
			full_current = pname
		elif pname == "transforms" and full_current != "Bullets":
			danglers.append(pname)
	assert_true(danglers.is_empty(), "transforms grouped under Bullets, not Bounce")
	# Base instance props on a live volley group under their own headers.
	var vgroups := {}
	var vcurrent := ""
	for p in vinst.get_property_list():
		var pname := str(p.get("name", ""))
		var pusage: int = int(p.get("usage", 0))
		if (pusage & PROPERTY_USAGE_GROUP) != 0:
			vcurrent = pname
		elif pname != "":
			vgroups[pname] = vcurrent
	assert_true(vgroups.get("shared_bullets_custom_data", "") == "Custom Data", "volley custom data grouped")
	assert_true(vgroups.get("is_multimesh_auto_pooling_enabled", "") == "Pooling", "volley pooling grouped")
	assert_true(vgroups.get("bullet_max_collision_count", "") == "Collision", "volley collision grouped")
	assert_true(vgroups.get("is_life_time_infinite", "") == "Lifetime", "volley lifetime grouped")
	assert_true(vgroups.get("shared_bullet_curves_data", "") == "Curves", "volley curves grouped")
	# Live mirrors keep the same Bounce home as the data (all 12).
	var vbounced := true
	for bname in ["bounce_mask", "bounce_strength", "bounce_push_assist", "bounce_charge_amplify", "bounce_hit_consumed", "bounce_max_count", "bounce_mode", "bounce_rotate_texture", "bounce_rotation_smooth", "bounce_randomness_deg", "bounce_cooldown_sec", "bounce_debounce_sec"]:
		if str(vgroups.get(bname, "")) != "Bounce and Ricochet":
			vbounced = false
	assert_true(vbounced, "all 12 live bounce props grouped with Bounce")
	var spawner20 := BulletSpawner2D.new()
	spawner20.set_shooting_enabled(false) # inspector-only fixture: never auto-fire
	add(spawner20)
	await idle(1)
	var groups_found := {}
	var spawner_group_order: Array = []
	for p in spawner20.get_property_list():
		if (int(p.get("usage", 0)) & PROPERTY_USAGE_GROUP) != 0:
			var gname := str(p.get("name", ""))
			groups_found[gname] = true
			if not spawner_group_order.has(gname):
				spawner_group_order.append(gname)
	# Unique titles on every inspector surface (volley, spawner, block data).
	for entry in [[vinst, "volley"], [spawner20, "spawner"]]:
		var seen := {}
		var dups: Array = []
		for p in (entry[0] as Object).get_property_list():
			if (int(p.get("usage", 0)) & PROPERTY_USAGE_GROUP) != 0:
				var gname := str(p.get("name", ""))
				if seen.has(gname):
					dups.append(gname)
				seen[gname] = true
		assert_true(dups.is_empty(), "no duplicate group titles on " + str(entry[1]))
	var block20 := BlockBulletsData2D.new()
	block20.sprite_frames = H.make_sprite_frames()
	var block_groups: Array = []
	for p in block20.get_property_list():
		if (int(p.get("usage", 0)) & PROPERTY_USAGE_GROUP) != 0:
			var gname := str(p.get("name", ""))
			if not block_groups.has(gname):
				block_groups.append(gname)
	assert_true(block_groups.slice(0, 1) == ["Block Bullets"], "block data leads with its own group")
	assert_true(groups_found.has("Shooting"), "spawner Shooting group present")
	assert_true(groups_found.has("Homing"), "spawner Homing group present")
	assert_true(groups_found.has("Orbiting"), "spawner Orbiting group present")
	assert_true(groups_found.has("Preview"), "spawner Preview group present")
	assert_true(groups_found.has("Bullet Patterns"), "spawner Bullet Patterns group present")
	assert_true(not groups_found.has("Transform Generation"), "old Transform Generation name gone")
	assert_true(not groups_found.has("Burst and Telegraph"), "Burst group merged away")
	assert_eq(spawner_group_order.slice(0, 9), ["Setup", "Bullet Patterns", "Shooting", "Spin", "Homing", "Orbiting", "Preview", "Movement", "Performance"], "spawner group order")
	spawner20.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_GRID
	await idle(1)
	var ring_visible_grid := false
	var fan_visible_grid := false
	for p in spawner20.get_property_list():
		var pname := str(p.get("name", ""))
		var shown := (int(p.get("usage", 0)) & PROPERTY_USAGE_EDITOR) != 0
		if pname == "helper_ring_radius" and shown:
			ring_visible_grid = true
		if pname == "helper_fan_spread" and shown:
			fan_visible_grid = true
	assert_true(not ring_visible_grid and not fan_visible_grid, "grid mode hides other families")
	spawner20.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_RING
	await idle(1)
	var ring_visible_ring := false
	for p in spawner20.get_property_list():
		if str(p.get("name", "")) == "helper_ring_radius" and (int(p.get("usage", 0)) & PROPERTY_USAGE_EDITOR) != 0:
			ring_visible_ring = true
	assert_true(ring_visible_ring, "ring mode reveals ring knobs")
	# Group placement: every property must sit under its family header.
	# Tracks the current ADD_GROUP header while walking the list in order.
	var prop_group := {}
	var current_group := ""
	for p in spawner20.get_property_list():
		var pname := str(p.get("name", ""))
		var pusage: int = int(p.get("usage", 0))
		if (pusage & PROPERTY_USAGE_GROUP) != 0:
			current_group = pname
		elif pname != "":
			prop_group[pname] = current_group
	assert_true(prop_group.get("pattern_source", "") == "Bullet Patterns", "pattern_source grouped under Bullet Patterns")
	assert_true(prop_group.get("helper_ring_radius", "") == "Bullet Patterns", "helpers grouped under Bullet Patterns")
	assert_true(prop_group.get("helper_skip_indices", "") == "Bullet Patterns", "skip indices grouped under Bullet Patterns")
	assert_true(prop_group.get("pattern_scale", "") == "Bullet Patterns", "pattern_scale grouped under Bullet Patterns")
	assert_true(prop_group.get("transforms_scale", "") == "Bullet Patterns", "transforms_scale grouped under Bullet Patterns")
	assert_true(prop_group.get("spawn_position_offset", "") == "Bullet Patterns", "spawn offset grouped under Bullet Patterns")
	assert_true(prop_group.get("shooting_enabled", "") == "Shooting", "shooting master grouped")
	assert_true(prop_group.get("reload_jitter_sec", "") == "Shooting", "reload jitter moved to Shooting")
	assert_true(prop_group.get("reload_jitter_seed", "") == "Shooting", "reload seed moved to Shooting")
	assert_true(prop_group.get("max_live_bullets", "") == "Shooting", "max_live_bullets moved to Shooting")
	assert_true(prop_group.get("spin_enabled", "") == "Spin", "spin master grouped")
	assert_true(prop_group.get("homing_delay_sec", "") == "Homing", "homing gates moved to Homing")
	assert_true(prop_group.get("homing_duration_sec", "") == "Homing", "homing duration moved to Homing")
	assert_true(prop_group.get("homing_lose_range_px", "") == "Homing", "homing lose range moved to Homing")
	assert_true(prop_group.get("homing_fire_arc_deg", "") == "Homing", "fire arc moved to Homing")
	assert_true(prop_group.get("homing_retarget_phase", "") == "Homing", "retarget phase moved to Homing")
	assert_true(prop_group.get("burst_enabled", "") == "Shooting", "burst merged into Shooting")
	assert_true(prop_group.get("burst_count", "") == "Shooting", "burst count merged into Shooting")
	assert_true(prop_group.get("telegraph_enabled", "") == "Shooting", "telegraph merged into Shooting")
	assert_true(prop_group.get("telegraph_sec", "") == "Shooting", "telegraph seconds merged into Shooting")
	assert_true(prop_group.get("orbiting_enabled", "") == "Orbiting", "orbiting master grouped")
	assert_true(prop_group.get("show_pattern_preview", "") == "Preview", "preview master grouped")
	var stray_homing := []
	for pname in prop_group.keys():
		var gname := str(prop_group[pname])
		if pname.begins_with("homing_") and gname != "Homing":
			stray_homing.append(pname)
		if pname.begins_with("reload_jitter") and gname != "Shooting":
			stray_homing.append(pname)
		if pname == "max_live_bullets" and gname != "Shooting":
			stray_homing.append(pname)
	assert_true(stray_homing.is_empty(), "no homing/reload/budget prop leaks into Spin or Burst")
	spawner20.queue_free()
	await idle(1)


func test_t21_runtime_toggles_and_gravity_flag_refresh() -> void:
	# BOUNCE T21 runtime toggles and gravity flag refresh
	await _arena()
	await _settle(factory)
	var v21: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2.ZERO, 0.0, 300.0, [], [4]))
	assert_true(v21.debug_get_bounce_info(0).get("bounce_enabled", true) == false, "starts disarmed")
	v21.set_bounce_mask(8)
	assert_true(v21.debug_get_bounce_info(0).get("bounce_enabled", false) == true, "runtime arm sizes ledger")
	for i in 120:
		await physics()
		if _bounce_body.size() >= 1:
			break
	assert_true(_bounce_body.size() >= 1, "runtime-armed volley bounces")
	v21.set_bounce_mask(0)
	assert_true(v21.debug_get_bounce_info(0).get("bounce_enabled", true) == false, "runtime disarm clears armed state")
	v21.set_bounce_mask(8)
	assert_true(v21.debug_get_bounce_info(0).get("bounce_enabled", false) == true, "re-arm works after disarm")
	await _settle(factory)
	var v21c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4]))
	v21c.set_bounce_mask(0)
	for i in 120:
		await physics()
		if not v21c.is_bullet_status_enabled(0):
			break
	assert_true(_bounce_body.is_empty(), "runtime disarm flies through clean")
	assert_true(not v21c.is_bullet_status_enabled(0), "disarmed bullet dies normally")
	await _settle(factory)
	var v21g: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2.ZERO, 0.0, 0.0, [], [4]))
	v21g.set_gravity(Vector2(0, 500.0))
	for i in 45:
		await physics()
	assert_true(v21g.bullet_get_fall_speed(0) > 20.0, "runtime gravity engages fall")
	v21g.bullet_set_gravity(0, Vector2(0, 0))
	for i in 30:
		await physics()
	assert_true(v21g.bullet_get_fall_speed(0) < 5.0, "zeroed gravity holds fall speed")


func test_t22_same_target_debounce_stuck_bullet_bounces_once_per_window() -> void:
	# BOUNCE T22 same-target debounce: stuck bullet bounces once per window
	var wall := await _arena()
	await _settle(factory)
	var d22 := _bounce_data(Vector2(100, 0), 0.0, 0.0, [4], [4])
	assert_true(d22.bounce_debounce_sec == 0.15, "debounce defaults 0.15")
	var keep_deb: float = d22.bounce_debounce_sec
	d22.set_bounce_debounce_sec(NAN)
	expect_error_sequence(["DirectionalBulletsData2D: bounce_debounce_sec must be finite and >= 0"])
	assert_true(d22.bounce_debounce_sec == keep_deb, "NaN debounce rejected")
	d22.set_bounce_debounce_sec(-1.0)
	expect_error_sequence(["DirectionalBulletsData2D: bounce_debounce_sec must be finite and >= 0"])
	assert_true(d22.bounce_debounce_sec == keep_deb, "negative debounce rejected")
	d22.bounce_cooldown_sec = 0.0
	var v22: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d22)
	assert_true(absf(v22.get_bounce_debounce_sec() - 0.15) < 0.0001, "live mirror reseeds debounce")
	for i in 30:
		if i % 2 == 0:
			v22.set_bullet_transform(0, Transform2D(0.0, Vector2(200, 0)))
		else:
			v22.set_bullet_transform(0, Transform2D(0.0, Vector2(100, 0)))
		await physics()
	var stuck_count: int = v22.bullet_get_bounce_count(0)
	assert_true(stuck_count <= 5, "same wall re-hits debounced, not machine-gunned (got %d)" % stuck_count)
	assert_true(stuck_count >= 1, "first contact still bounces")
	assert_true(float(v22.debug_get_bounce_info(0).get("debounce", -1.0)) >= 0.0, "debug info reports debounce window")
	await _settle(factory)
	var d22b := _bounce_data(Vector2(100, 0), 0.0, 0.0, [4], [4])
	d22b.bounce_cooldown_sec = 0.0
	d22b.bounce_debounce_sec = 0.0
	var v22b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d22b)
	for i in 30:
		if i % 2 == 0:
			v22b.set_bullet_transform(0, Transform2D(0.0, Vector2(200, 0)))
		else:
			v22b.set_bullet_transform(0, Transform2D(0.0, Vector2(100, 0)))
		await physics()
	assert_true(v22b.bullet_get_bounce_count(0) >= 12, "debounce 0 bounces every re-entry (got %d)" % v22b.bullet_get_bounce_count(0))


func test_t22b_debounce_is_per_target_alternating_walls_bounce_freely() -> void:
	# BOUNCE T22b debounce is per-target: alternating walls bounce freely
	await _arena()
	await _settle(factory)
	var wall_b := _make_wall(Vector2(-200, 0), 8)
	await physics()
	var d22c := _bounce_data(Vector2(100, 0), 0.0, 0.0, [4], [4])
	d22c.bounce_cooldown_sec = 0.0
	var v22c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d22c)
	for i in 20:
		if i % 2 == 0:
			v22c.set_bullet_transform(0, Transform2D(0.0, Vector2(200, 0)))
		else:
			v22c.set_bullet_transform(0, Transform2D(0.0, Vector2(-200, 0)))
		await physics()
	assert_true(v22c.bullet_get_bounce_count(0) >= 16, "new target each hit bounces despite debounce (got %d)" % v22c.bullet_get_bounce_count(0))
	wall_b.queue_free()
	await idle(1)


func test_t23_moving_targets_push_surges_head_on_amplifies_separating_swal() -> void:
	# BOUNCE T23 moving targets: push surges, head-on amplifies, separating swallows
	var wall := await _arena()
	await _settle(factory)
	var pusher := RigidBody2D.new()
	pusher.position = Vector2(-150, 0)
	pusher.collision_layer = 8
	pusher.collision_mask = 0
	pusher.gravity_scale = 0.0
	pusher.linear_damp = 0.0
	pusher.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	pusher.can_sleep = false
	pusher.linear_velocity = Vector2(500, 0)
	var pcol := CollisionShape2D.new()
	var pbox := RectangleShape2D.new()
	pbox.size = Vector2(20, 400)
	pcol.shape = pbox
	pusher.add_child(pcol)
	add(pusher)
	await physics()
	var d23 := _bounce_data(Vector2.ZERO, 0.0, 100.0, [4], [4])
	var v23: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d23)
	for i in 150:
		await physics()
		if v23.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v23.bullet_get_bounce_count(0) >= 1, "pusher wall bounced the bullet")
	var push_dir: Vector2 = v23.get_bullet_direction(0)
	var push_spd: float = v23.get_bullet_velocity(0).length()
	assert_true(push_dir.x > 0.5, "pushed bullet keeps flying forward, never reverses (dir %s)" % str(push_dir))
	assert_true(absf(push_spd - 900.0) < 80.0, "pusher surge reflects relative velocity (~900, got %.0f)" % push_spd)
	pusher.queue_free()
	await idle(1)
	await _settle(factory)
	var charger := RigidBody2D.new()
	charger.position = Vector2(400, 0)
	charger.collision_layer = 8
	charger.collision_mask = 0
	charger.gravity_scale = 0.0
	charger.linear_damp = 0.0
	charger.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	charger.can_sleep = false
	charger.linear_velocity = Vector2(-400, 0)
	var ccol := CollisionShape2D.new()
	var cbox := RectangleShape2D.new()
	cbox.size = Vector2(20, 400)
	ccol.shape = cbox
	charger.add_child(ccol)
	add(charger)
	await physics()
	var d23b := _bounce_data(Vector2.ZERO, 0.0, 100.0, [4], [4])
	var v23b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d23b)
	for i in 150:
		await physics()
		if v23b.bullet_get_bounce_count(0) >= 1:
			break
	var head_spd: float = v23b.get_bullet_velocity(0).length()
	assert_true(v23b.get_bullet_direction(0).x < -0.5, "head-on wall reflects backwards")
	assert_true(absf(head_spd - 900.0) < 80.0, "head-on impact amplifies (~900, got %.0f)" % head_spd)
	charger.queue_free()
	await idle(1)


func test_t23b_separating_repeats_never_re_bounce() -> void:
	# BOUNCE T23b separating repeats never re-bounce
	await _arena()
	await _settle(factory)
	var d23c := _bounce_data(Vector2(100, 0), 0.0, 300.0, [4], [4])
	d23c.bounce_cooldown_sec = 0.0
	d23c.bounce_debounce_sec = 0.05
	var v23c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d23c)
	# First contact bounces (fresh overlaps keep the historical bounce).
	# Each 8-frame cycle below re-enters the SAME wall while separating
	# from it (detection lands mid-block, long after the 0.05 debounce);
	# the repeat guard must silence every cycle after the first.
	for i in 40:
		if i % 8 < 4:
			if i % 8 == 0:
				v23c.set_bullet_transform(0, Transform2D(0.0, Vector2(205, 0)))
				v23c.set_bullet_direction(0, Vector2(1, 0))
		else:
			if i % 8 == 4:
				v23c.set_bullet_transform(0, Transform2D(0.0, Vector2(100, 0)))
		await physics()
	var repeat_count: int = v23c.bullet_get_bounce_count(0)
	assert_true(repeat_count >= 1, "first separating contact still bounces")
	assert_true(repeat_count <= 2, "separating repeats never re-bounce (got %d)" % repeat_count)
	await _settle(factory)
	var d23d := _bounce_data(Vector2(100, 0), 0.0, 300.0, [4], [4])
	d23d.bounce_hit_consumed = true
	d23d.set_bullet_max_collision_count(0)
	d23d.bounce_cooldown_sec = 0.0
	d23d.bounce_debounce_sec = 0.05
	var v23d: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d23d)
	for i in 40:
		if i % 8 < 4:
			if i % 8 == 0:
				v23d.set_bullet_transform(0, Transform2D(0.0, Vector2(205, 0)))
				v23d.set_bullet_direction(0, Vector2(1, 0))
		else:
			if i % 8 == 4:
				v23d.set_bullet_transform(0, Transform2D(0.0, Vector2(100, 0)))
		await physics()
	assert_true(v23d.bullet_get_bounce_count(0) <= 2, "consumed repeats never re-bounce (got %d)" % v23d.bullet_get_bounce_count(0))
	assert_true(v23d.get_bullet_collision_count(0) >= 5, "consumed repeats still count through the normal path")
	assert_true(v23d.is_bullet_status_enabled(0), "max 0 never kills")


func test_t24_push_charge_knobs_each_side_opts_out_separately() -> void:
	# BOUNCE T24 push/charge knobs: each side opts out separately
	await _arena()
	await _settle(factory)
	var d24 := _bounce_data(Vector2.ZERO, 0.0, 100.0, [4], [4])
	assert_true(d24.bounce_push_assist == true, "push assist defaults true")
	assert_true(d24.bounce_charge_amplify == true, "charge amplify defaults true")
	d24.bounce_push_assist = false
	var arcade := RigidBody2D.new()
	arcade.position = Vector2(-150, 0)
	arcade.collision_layer = 8
	arcade.collision_mask = 0
	arcade.gravity_scale = 0.0
	arcade.linear_damp = 0.0
	arcade.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	arcade.can_sleep = false
	arcade.linear_velocity = Vector2(500, 0)
	var acol := CollisionShape2D.new()
	var abox := RectangleShape2D.new()
	abox.size = Vector2(20, 400)
	acol.shape = abox
	arcade.add_child(acol)
	add(arcade)
	await physics()
	var v24: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d24)
	assert_true(v24.get_bounce_push_assist() == false, "live mirror reseeds push off")
	assert_true(v24.get_bounce_charge_amplify() == true, "live mirror reseeds charge on")
	for i in 150:
		await physics()
		if v24.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v24.bullet_get_bounce_count(0) >= 1, "unassisted bounce still fires")
	assert_true(v24.get_bullet_direction(0).x < -0.5, "push assist off reverses (no surge)")
	var arcade_spd: float = v24.get_bullet_velocity(0).length()
	assert_true(absf(arcade_spd - 100.0) < 30.0, "push assist off keeps plain speed (~100, got %.0f)" % arcade_spd)
	arcade.queue_free()
	await idle(1)
	await _settle(factory)
	var d24b := _bounce_data(Vector2.ZERO, 0.0, 100.0, [4], [4])
	d24b.bounce_charge_amplify = false
	var charger2 := RigidBody2D.new()
	charger2.position = Vector2(400, 0)
	charger2.collision_layer = 8
	charger2.collision_mask = 0
	charger2.gravity_scale = 0.0
	charger2.linear_damp = 0.0
	charger2.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	charger2.can_sleep = false
	charger2.linear_velocity = Vector2(-400, 0)
	var c2col := CollisionShape2D.new()
	var c2box := RectangleShape2D.new()
	c2box.size = Vector2(20, 400)
	c2col.shape = c2box
	charger2.add_child(c2col)
	add(charger2)
	await physics()
	var v24b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d24b)
	assert_true(v24b.get_bounce_charge_amplify() == false, "live mirror reseeds charge off")
	for i in 150:
		await physics()
		if v24b.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v24b.get_bullet_direction(0).x < -0.5, "charge amplify off still reflects")
	var plain_spd: float = v24b.get_bullet_velocity(0).length()
	assert_true(absf(plain_spd - 100.0) < 30.0, "charge amplify off keeps plain speed (~100, got %.0f)" % plain_spd)
	charger2.queue_free()
	await idle(1)
	await _settle(factory)
	var d24c := _bounce_data(Vector2.ZERO, 0.0, 100.0, [4], [4])
	d24c.bounce_push_assist = false
	d24c.bounce_charge_amplify = false
	var arcade2 := RigidBody2D.new()
	arcade2.position = Vector2(-150, 0)
	arcade2.collision_layer = 8
	arcade2.collision_mask = 0
	arcade2.gravity_scale = 0.0
	arcade2.linear_damp = 0.0
	arcade2.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	arcade2.can_sleep = false
	arcade2.linear_velocity = Vector2(500, 0)
	var a2col := CollisionShape2D.new()
	var a2box := RectangleShape2D.new()
	a2box.size = Vector2(20, 400)
	a2col.shape = a2box
	arcade2.add_child(a2col)
	add(arcade2)
	await physics()
	var v24c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d24c)
	for i in 150:
		await physics()
		if v24c.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v24c.get_bullet_direction(0).x < -0.5, "both off reverses like legacy")
	assert_true(v24c.get_bullet_velocity(0).length() < 200.0, "both off keeps plain speed")
	arcade2.queue_free()
	await idle(1)


func test_t25_velocity_sources_characterbody_reads_area2d_stays_static() -> void:
	# BOUNCE T25 velocity sources: CharacterBody reads, Area2D stays static
	await _arena()
	await _settle(factory)
	var charlie := CharacterBody2D.new()
	charlie.position = Vector2(200, 300)
	charlie.collision_layer = 8
	charlie.collision_mask = 0
	charlie.velocity = Vector2(500, 0)
	var chcol := CollisionShape2D.new()
	var chbox := RectangleShape2D.new()
	chbox.size = Vector2(20, 400)
	chcol.shape = chbox
	charlie.add_child(chcol)
	add(charlie)
	await physics()
	var d25 := _bounce_data(Vector2.ZERO, 0.0, 100.0, [4], [4])
	var v25: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d25)
	v25.set_bullet_transform(0, Transform2D(0.0, Vector2(200, 300)))
	for i in 30:
		await physics()
		if v25.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v25.bullet_get_bounce_count(0) >= 1, "CharacterBody velocity read without motion")
	assert_true(v25.get_bullet_direction(0).x > 0.5, "CharacterBody push surges forward")
	var char_spd: float = v25.get_bullet_velocity(0).length()
	assert_true(absf(char_spd - 900.0) < 80.0, "CharacterBody surge magnitude (~900, got %.0f)" % char_spd)
	charlie.queue_free()
	await idle(1)
	await _settle(factory)
	var eye25 := Area2D.new()
	eye25.position = Vector2(200, 300)
	eye25.collision_layer = 8
	eye25.monitoring = true
	eye25.monitorable = true
	var ecol25 := CollisionShape2D.new()
	var ebox25 := RectangleShape2D.new()
	ebox25.size = Vector2(20, 400)
	ecol25.shape = ebox25
	eye25.add_child(ecol25)
	add(eye25)
	await physics()
	var d25b := _bounce_data(Vector2.ZERO, 0.0, 100.0, [4], [4])
	var v25b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d25b)
	v25b.set_bullet_transform(0, Transform2D(0.0, Vector2(200, 300)))
	for i in 30:
		await physics()
		if v25b.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v25b.bullet_get_bounce_count(0) >= 1, "areas bounce too")
	assert_true(v25b.get_bullet_direction(0).x < -0.5, "static area reverses")
	eye25.queue_free()
	await idle(1)
	await _settle(factory)
	var runner := Area2D.new()
	runner.position = Vector2(-150, 300)
	runner.collision_layer = 8
	runner.monitoring = true
	runner.monitorable = true
	var rcol := CollisionShape2D.new()
	var rbox := RectangleShape2D.new()
	rbox.size = Vector2(20, 400)
	rcol.shape = rbox
	runner.add_child(rcol)
	add(runner)
	await physics()
	var d25c := _bounce_data(Vector2(100, 300), 0.0, 100.0, [4], [4])
	var v25c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d25c)
	for i in 150:
		runner.position.x += 500.0 / 60.0
		await physics()
		if v25c.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v25c.bullet_get_bounce_count(0) >= 1, "moved area still bounces")
	assert_true(v25c.get_bullet_direction(0).x > 0.5, "moved area surges forward (position estimate feeds the push)")
	assert_true(v25c.get_bullet_velocity(0).length() > 500.0, "moved area adds push boost")
	runner.queue_free()
	await idle(1)


func test_t25b_strength_0_vs_pusher_sticks_precise_moves_too() -> void:
	# BOUNCE T25b strength 0 vs pusher sticks, precise moves too
	var wall := await _arena()
	await _settle(factory)
	var pusher0 := RigidBody2D.new()
	pusher0.position = Vector2(-150, 0)
	pusher0.collision_layer = 8
	pusher0.collision_mask = 0
	pusher0.gravity_scale = 0.0
	pusher0.linear_damp = 0.0
	pusher0.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	pusher0.can_sleep = false
	pusher0.linear_velocity = Vector2(500, 0)
	var p0col := CollisionShape2D.new()
	var p0box := RectangleShape2D.new()
	p0box.size = Vector2(20, 400)
	p0col.shape = p0box
	pusher0.add_child(p0col)
	add(pusher0)
	await physics()
	var d25d := _bounce_data(Vector2.ZERO, 0.0, 100.0, [4], [4])
	d25d.bounce_strength = 0.0
	var v25d: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d25d)
	for i in 150:
		await physics()
		if v25d.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v25d.get_bullet_direction(0).x > 0.5, "dead-stop vs pusher rides the wall")
	var stick_spd: float = v25d.get_bullet_velocity(0).length()
	assert_true(absf(stick_spd - 500.0) < 40.0, "dead-stop adopts wall speed (~500, got %.0f)" % stick_spd)
	pusher0.queue_free()
	await idle(1)
	await _settle(factory)
	var pusher1 := RigidBody2D.new()
	pusher1.position = Vector2(-150, 0)
	pusher1.collision_layer = 8
	pusher1.collision_mask = 0
	pusher1.gravity_scale = 0.0
	pusher1.linear_damp = 0.0
	pusher1.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	pusher1.can_sleep = false
	pusher1.linear_velocity = Vector2(500, 0)
	var p1col := CollisionShape2D.new()
	var p1box := RectangleShape2D.new()
	p1box.size = Vector2(20, 400)
	p1col.shape = p1box
	pusher1.add_child(p1col)
	add(pusher1)
	await physics()
	var d25e := _bounce_data(Vector2.ZERO, 0.0, 100.0, [4], [4])
	d25e.bounce_mode = 1
	var v25e: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d25e)
	for i in 150:
		await physics()
		if v25e.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v25e.get_bullet_direction(0).x > 0.5, "precise mode surges too")
	var precise_spd: float = v25e.get_bullet_velocity(0).length()
	assert_true(absf(precise_spd - 900.0) < 80.0, "precise surge magnitude (~900, got %.0f)" % precise_spd)
	pusher1.queue_free()
	await idle(1)


func test_t25c_knob_independence_pool_ghost_boost() -> void:
	# BOUNCE T25c knob independence + pool ghost-boost
	await _arena()
	await _settle(factory)
	var d25f := _bounce_data(Vector2.ZERO, 0.0, 100.0, [4], [4])
	d25f.bounce_push_assist = false
	var charger3 := RigidBody2D.new()
	charger3.position = Vector2(400, 0)
	charger3.collision_layer = 8
	charger3.collision_mask = 0
	charger3.gravity_scale = 0.0
	charger3.linear_damp = 0.0
	charger3.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	charger3.can_sleep = false
	charger3.linear_velocity = Vector2(-400, 0)
	var c3col := CollisionShape2D.new()
	var c3box := RectangleShape2D.new()
	c3box.size = Vector2(20, 400)
	c3col.shape = c3box
	charger3.add_child(c3col)
	add(charger3)
	await physics()
	var v25f: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d25f)
	for i in 150:
		await physics()
		if v25f.bullet_get_bounce_count(0) >= 1:
			break
	var cross_spd: float = v25f.get_bullet_velocity(0).length()
	assert_true(absf(cross_spd - 900.0) < 80.0, "charge works while push is off (~900, got %.0f)" % cross_spd)
	charger3.queue_free()
	await idle(1)
	await _settle(factory)
	var d25g := _bounce_data(Vector2.ZERO, 0.0, 100.0, [4], [4])
	d25g.bounce_charge_amplify = false
	var pusher2 := RigidBody2D.new()
	pusher2.position = Vector2(-150, 0)
	pusher2.collision_layer = 8
	pusher2.collision_mask = 0
	pusher2.gravity_scale = 0.0
	pusher2.linear_damp = 0.0
	pusher2.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	pusher2.can_sleep = false
	pusher2.linear_velocity = Vector2(500, 0)
	var p2col := CollisionShape2D.new()
	var p2box := RectangleShape2D.new()
	p2box.size = Vector2(20, 400)
	p2col.shape = p2box
	pusher2.add_child(p2col)
	add(pusher2)
	await physics()
	var v25g: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d25g)
	for i in 150:
		await physics()
		if v25g.bullet_get_bounce_count(0) >= 1:
			break
	var surge_spd: float = v25g.get_bullet_velocity(0).length()
	assert_true(v25g.get_bullet_direction(0).x > 0.5, "push works while charge is off")
	assert_true(absf(surge_spd - 900.0) < 80.0, "push surge intact (~900, got %.0f)" % surge_spd)
	pusher2.queue_free()
	await idle(1)
	await _settle(factory)
	var d25h := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d25h.bounce_strength = 2.0
	d25h.max_life_time = 2.0
	var v25h: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d25h)
	for i in 120:
		await physics()
		if v25h.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(absf(v25h.get_bullet_velocity(0).length() - 600.0) < 80.0, "strength 2 doubles before pooling")
	for i in 200:
		await physics()
		if not v25h.is_bullet_status_enabled(0):
			break
	assert_true(not v25h.is_bullet_status_enabled(0), "expiry pooled the volley")
	v25h.enable_bullet(0)
	var flat25 := BulletCurvesData2D.new()
	var flat25_curve := Curve.new()
	flat25_curve.min_value = 0.0
	flat25_curve.max_value = 2000.0
	flat25_curve.add_point(Vector2(0, 300))
	flat25_curve.add_point(Vector2(1, 300))
	flat25.movement_speed_curve = flat25_curve
	flat25.movement_use_unit_curve = false
	v25h.set_shared_bullet_curves_data(flat25)
	for i in 10:
		await physics()
	var wake_spd: float = v25h.get_bullet_velocity(0).length()
	assert_true(absf(wake_spd - 300.0) < 80.0, "wake has no ghost boost from the dead life (got %.0f)" % wake_spd)


func test_t26_reset_semantics_multi_bullet_gravity_arc_area_routing() -> void:
	# BOUNCE T26 reset semantics, multi-bullet, gravity arc, area routing
	await _arena()
	await _settle(factory)
	var d26 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	var v26: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d26)
	for i in 120:
		await physics()
		if v26.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v26.bullet_get_bounce_count(0) >= 1, "baseline bounce counted")
	v26.disable_bullet(0)
	assert_true(v26.bullet_get_bounce_count(0) == 0, "disable zeroes the bounce count")
	v26.enable_bullet(0)
	assert_true(v26.bullet_get_bounce_count(0) == 0, "wake restarts the ledger")
	assert_true(v26.is_bullet_status_enabled(0), "wake revives the bullet")
	await _settle(factory)
	var dd26 := DirectionalBulletsData2D.new()
	dd26.sprite_frames = H.make_sprite_frames()
	dd26.transforms = [Transform2D(0.0, Vector2.ZERO), Transform2D(0.0, Vector2.ZERO)]
	var s26: Array = []
	for i in 2:
		var sp26 := BulletSpeedData2D.new()
		sp26.speed = 300.0
		sp26.max_speed = 3000.0
		s26.append(sp26)
	dd26.all_bullet_speed_data = s26
	dd26.max_life_time = 8.0
	dd26.texture_size = Vector2(16, 16)
	dd26.monitorable = true
	dd26.set_collision_layer_from_array([2])
	dd26.set_collision_mask_from_array([4])
	dd26.set_bounce_mask_from_array([4])
	var cc := CircleShape2D.new()
	cc.radius = 6.0
	dd26.collision_shape = cc
	var v26b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(dd26)
	for i in 150:
		await physics()
		if v26b.bullet_get_bounce_count(0) >= 1 and v26b.bullet_get_bounce_count(1) >= 1:
			break
	assert_true(v26b.bullet_get_bounce_count(0) == 1 and v26b.bullet_get_bounce_count(1) == 1, "both bullets bounce independently")
	assert_true(v26b.get_bullet_direction(0).x < -0.5 and v26b.get_bullet_direction(1).x < -0.5, "both bullets reflect")
	await _settle(factory)
	var d26c := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d26c.gravity = Vector2(0, 200.0)
	var v26c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d26c)
	for i in 120:
		await physics()
		if v26c.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v26c.bullet_get_bounce_count(0) >= 1, "gravity volley bounces")
	assert_true(v26c.get_bullet_direction(0).x < 1.0 - 0.05, "arcing impact deflects off head-on")
	assert_true(v26c.get_bullet_velocity(0).y > 20.0, "fall continues downward after the bounce")
	assert_true(v26c.get_bullet_velocity(0).is_finite(), "post-bounce gravity state finite")
	await _settle(factory)
	var eye26 := Area2D.new()
	eye26.position = Vector2(200, 300)
	eye26.collision_layer = 8
	eye26.monitoring = true
	eye26.monitorable = true
	var e26col := CollisionShape2D.new()
	var e26box := RectangleShape2D.new()
	e26box.size = Vector2(20, 400)
	e26col.shape = e26box
	eye26.add_child(e26col)
	add(eye26)
	await physics()
	var d26d := _bounce_data(Vector2.ZERO, 0.0, 100.0, [4], [4])
	var v26d: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d26d)
	v26d.set_bullet_transform(0, Transform2D(0.0, Vector2(200, 300)))
	for i in 60:
		await physics()
		if v26d.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v26d.bullet_get_bounce_count(0) >= 1, "pure area overlap bounces")
	assert_true(_bounce_area.size() >= 1, "area bounce routes to the area signal")
	assert_true(_bounce_body.is_empty(), "area bounce never touches the body signal")
	eye26.queue_free()
	await idle(1)
	await _settle(factory)
	var d26e := _bounce_data(Vector2.ZERO, 0.0, 200.0, [4], [4])
	d26e.bounce_push_assist = false
	d26e.bounce_charge_amplify = false
	var v26e: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d26e)
	assert_true(v26e.get_bounce_push_assist() == false and v26e.get_bounce_charge_amplify() == false, "knobs-off mirrors reseeded")
	await idle(1)
	factory.free_active_bullets()
	await idle(1)
	await idle(1)
	var v26f: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2.ZERO, 0.0, 200.0, [4], [4]))
	assert_true(v26f.get_bounce_push_assist() == true and v26f.get_bounce_charge_amplify() == true, "respawn reseeds knobs to defaults")


func test_t27_curves_beat_surge_guard_ignores_knobs_degenerate_config() -> void:
	# BOUNCE T27 curves beat surge, guard ignores knobs, degenerate config
	await _arena()
	await _settle(factory)
	var flat27 := BulletCurvesData2D.new()
	var flat27_curve := Curve.new()
	flat27_curve.min_value = 0.0
	flat27_curve.max_value = 2000.0
	flat27_curve.add_point(Vector2(0, 300))
	flat27_curve.add_point(Vector2(1, 300))
	flat27.movement_speed_curve = flat27_curve
	flat27.movement_use_unit_curve = false
	var push27 := RigidBody2D.new()
	push27.position = Vector2(-150, 300)
	push27.collision_layer = 8
	push27.collision_mask = 0
	push27.gravity_scale = 0.0
	push27.linear_damp = 0.0
	push27.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	push27.can_sleep = false
	push27.linear_velocity = Vector2(500, 0)
	var p27col := CollisionShape2D.new()
	var p27box := RectangleShape2D.new()
	p27box.size = Vector2(20, 400)
	p27col.shape = p27box
	push27.add_child(p27col)
	add(push27)
	await physics()
	var d27 := _bounce_data(Vector2(0, 300), 0.0, 100.0, [4], [4])
	var v27: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d27)
	v27.set_shared_bullet_curves_data(flat27)
	for i in 150:
		await physics()
		if v27.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v27.get_bullet_velocity(0).length() > 600.0, "curved bullet still surges at the drain")
	for i in 3:
		await physics()
	assert_true(absf(v27.get_bullet_velocity(0).length() - 300.0) < 80.0, "curves reclaim the surge next tick")
	push27.queue_free()
	await idle(1)
	await _settle(factory)
	var d27b := _bounce_data(Vector2(100, 0), 0.0, 300.0, [4], [4])
	d27b.bounce_cooldown_sec = 0.0
	d27b.bounce_debounce_sec = 0.05
	d27b.bounce_push_assist = false
	d27b.bounce_charge_amplify = false
	var v27b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d27b)
	for i in 40:
		if i % 8 < 4:
			if i % 8 == 0:
				v27b.set_bullet_transform(0, Transform2D(0.0, Vector2(205, 0)))
				v27b.set_bullet_direction(0, Vector2(1, 0))
		else:
			if i % 8 == 4:
				v27b.set_bullet_transform(0, Transform2D(0.0, Vector2(100, 0)))
		await physics()
	assert_true(v27b.bullet_get_bounce_count(0) <= 2, "repeat guard holds with both knobs off (got %d)" % v27b.bullet_get_bounce_count(0))
	await _settle(factory)
	var wall27b := _make_wall(Vector2(-200, 0), 8)
	await physics()
	var d27c := _bounce_data(Vector2(100, 0), 0.0, 0.0, [4], [4])
	d27c.bounce_cooldown_sec = 0.0
	d27c.bounce_debounce_sec = 0.0
	var v27c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d27c)
	for i in 20:
		if i % 2 == 0:
			v27c.set_bullet_transform(0, Transform2D(0.0, Vector2(200, 0)))
		else:
			v27c.set_bullet_transform(0, Transform2D(0.0, Vector2(-200, 0)))
		await physics()
	assert_true(v27c.bullet_get_bounce_count(0) >= 16, "zero guards bounce every detected entry (got %d)" % v27c.bullet_get_bounce_count(0))
	wall27b.queue_free()
	await idle(1)


func test_t28_hostile_freed_walls_nan_motion_overflow_starvation() -> void:
	# BOUNCE T28 hostile: freed walls, NaN motion, overflow, starvation
	var wall := await _arena()
	await _settle(factory)
	# T28a: freed wall mid-overlap freezes counts, volley stays finite.
	var d28 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	var v28: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d28)
	for i in 120:
		await physics()
		if v28.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v28.bullet_get_bounce_count(0) >= 1, "hostile baseline bounced")
	var frozen28: int = v28.bullet_get_bounce_count(0)
	wall.queue_free()
	await idle(1)
	await idle(1)
	for i in 30:
		await physics()
	assert_true(v28.bullet_get_bounce_count(0) == frozen28, "freed wall freezes bounce counts")
	assert_true(_finite_volley(v28), "volley finite after wall freed mid-flight")
	assert_true(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after wall freed mid-flight")
	wall = _make_wall(Vector2(200, 0), 8)
	await physics()
	# T28b: NaN wall velocity falls back to static, never crashes.
	await _settle(factory)
	var nan28 := RigidBody2D.new()
	nan28.position = Vector2(200, 0)
	nan28.collision_layer = 8
	nan28.collision_mask = 0
	nan28.gravity_scale = 0.0
	nan28.linear_damp = 0.0
	nan28.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	nan28.can_sleep = false
	nan28.linear_velocity = Vector2(NAN, 0.0)
	var nan28col := CollisionShape2D.new()
	var nan28box := RectangleShape2D.new()
	nan28box.size = Vector2(20, 400)
	nan28col.shape = nan28box
	nan28.add_child(nan28col)
	add(nan28)
	await physics()
	var d28b := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	var v28b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d28b)
	for i in 120:
		await physics()
		if v28b.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v28b.bullet_get_bounce_count(0) >= 1, "NaN-velocity wall still bounces (static fallback)")
	assert_true(v28b.get_bullet_velocity(0).is_finite(), "velocity finite after NaN-velocity bounce")
	nan28.queue_free()
	await idle(1)
	# T28c: absurd-finite strength refuses the bounce instead of poisoning.
	await _settle(factory)
	var d28c := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d28c.bounce_strength = 1e30
	var v28c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d28c)
	for i in 10:
		await physics()
	assert_true(_finite_volley(v28c), "1e30 strength never poisons the volley")
	assert_true(v28c.bullet_get_bounce_count(0) <= 1, "overflow bounce refused, not counted")
	# T28d: empty transforms with bounce armed refuse or spawn empty.
	await _settle(factory)
	var d28d := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d28d.transforms = []
	var v28d: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d28d)
	expect_error_sequence(["No spawn_data or no transforms were provided"])
	assert_null(v28d, "empty transforms refused with bounce armed")
	# T28e: subnormal strength is a finite dead-stop, heading preserved.
	await _settle(factory)
	var d28e := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d28e.bounce_strength = 1e-30
	var v28e: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d28e)
	for i in 120:
		await physics()
		if v28e.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v28e.bullet_get_bounce_count(0) >= 1, "subnormal strength still bounces")
	assert_true(v28e.get_bullet_velocity(0).is_finite(), "subnormal bounce stays finite")
	assert_true(v28e.get_bullet_velocity(0).length() < 0.01, "subnormal strength dead-stops")
	# T28f: spawner shoot_once rejects null data and bad factory paths.
	# NOTE: set_bullet_factory(null) then shoot_once() would exercise the
	# no-factory path, but assigning the spawner under a live factory via
	# set_bullet_factory hits path-resolution edge cases headless; the
	# null-data path below plus the spawner suites' factory-missing
	# coverage (test_spawner_tree T: bare spawner, failed shot uncounted)
	# pin the fail-early contract without flaking the runner.
	await _settle(factory)
	var spawner28 := BulletSpawner2D.new()
	spawner28.set_shooting_enabled(false) # manual shots only (no auto-fire spam)
	add(spawner28)
	await idle(1)
	spawner28.set_bullet_factory(factory)
	spawner28.set_spawn_data(null)
	var fired28: int = spawner28.get_volleys_fired()
	assert_true(spawner28.shoot_once() == false, "shoot_once with null data returns false")
	expect_error_sequence(["BulletSpawner2D::shoot_once: no spawn_data assigned"])
	assert_true(spawner28.get_volleys_fired() == fired28, "failed shot not counted")
	spawner28.queue_free()
	await idle(1)
	# No-factory path: a spawner with data but no factory refuses cleanly.
	var lonely28 := BulletSpawner2D.new()
	lonely28.set_shooting_enabled(false) # manual shots only (no auto-fire spam)
	add(lonely28)
	await idle(1)
	lonely28.set_spawn_data(_bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4]))
	var lonely_fired: int = lonely28.get_volleys_fired()
	assert_true(lonely28.shoot_once() == false, "shoot_once with no factory returns false")
	expect_error_sequence(["BulletSpawner2D::shoot_once: no BulletFactory2D assigned"])
	assert_true(lonely28.get_volleys_fired() == lonely_fired, "factory-less shot not counted")
	lonely28.queue_free()
	await idle(1)
	# T28g: two bullets vs one pusher stay independent in motion.
	await _settle(factory)
	var push28 := RigidBody2D.new()
	push28.position = Vector2(-150, 300)
	push28.collision_layer = 8
	push28.collision_mask = 0
	push28.gravity_scale = 0.0
	push28.linear_damp = 0.0
	push28.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	push28.can_sleep = false
	push28.linear_velocity = Vector2(500, 0)
	var push28col := CollisionShape2D.new()
	var push28box := RectangleShape2D.new()
	push28box.size = Vector2(20, 400)
	push28col.shape = push28box
	push28.add_child(push28col)
	add(push28)
	await physics()
	var d28g := _bounce_data(Vector2(0, 300), 0.0, 100.0, [4], [4])
	d28g.transforms = [Transform2D(0.0, Vector2(0, 300)), Transform2D(0.0, Vector2(0, 340))]
	var sg28 := BulletSpeedData2D.new()
	sg28.speed = 100.0
	sg28.max_speed = 3000.0
	sg28.acceleration = 0.0
	d28g.all_bullet_speed_data = [sg28, sg28]
	var v28g: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d28g)
	for i in 150:
		await physics()
		if v28g.bullet_get_bounce_count(0) >= 1 and v28g.bullet_get_bounce_count(1) >= 1:
			break
	assert_true(v28g.bullet_get_bounce_count(0) >= 1 and v28g.bullet_get_bounce_count(1) >= 1, "both bullets bounce off the shared pusher")
	assert_true(_finite_volley(v28g), "both bullets finite after shared-pusher bounce")
	push28.queue_free()
	await idle(1)
	# T28h: StayLocked orbit wins over the pusher (zero bounces). The whole
	# rig lives on the y=300 lane: the suite wall at (200, 0) would
	# otherwise intercept the flight, and a RigidBody pusher would crash
	# into it physically. Spawn sits past the wall with a clean run to the
	# ring; the pusher is teleported onto the locked bullet for guaranteed
	# overlap (its velocity still feeds the bounce math, which orbit skips).
	await _settle(factory)
	var orbit28 := Node2D.new()
	orbit28.position = Vector2(400, 300)
	add(orbit28)
	await idle(1)
	var lock28 := RigidBody2D.new()
	lock28.position = Vector2(100, 300)
	lock28.collision_layer = 8
	lock28.collision_mask = 0
	lock28.gravity_scale = 0.0
	lock28.linear_damp = 0.0
	lock28.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	lock28.can_sleep = false
	lock28.linear_velocity = Vector2(500, 0)
	var lock28col := CollisionShape2D.new()
	var lock28box := RectangleShape2D.new()
	lock28box.size = Vector2(20, 400)
	lock28col.shape = lock28box
	lock28.add_child(lock28col)
	add(lock28)
	await physics()
	var d28h := _bounce_data(Vector2(250, 300), 0.0, 300.0, [4], [4])
	d28h.set_bullet_max_collision_count(0)
	var v28h: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d28h)
	# OrbitRight=2, FaceTarget=0, FollowTarget=0, StayLocked=1.
	v28h.bullet_homing_push_back_homing_target(0, orbit28)
	v28h.bullet_enable_orbiting(0, 60.0, 2, 0, 0, 0.0, 1, true)
	for i in 150:
		await physics()
		if v28h.bullet_is_orbiting_locked(0):
			break
	assert_true(v28h.bullet_is_orbiting_locked(0), "orbiter locked before the pusher arrives")
	_clear_signals()
	for k in 3:
		lock28.position = v28h.get_bullet_transform(0).origin
		for i in 10:
			await physics()
	assert_true(v28h.bullet_get_bounce_count(0) == 0, "StayLocked orbit takes zero bounces")
	assert_true(v28h.get_bullet_collision_count(0) >= 1, "locked hits still count normally")
	assert_true(v28h.bullet_is_orbiting_locked(0), "StayLocked ring survives the pusher")
	orbit28.queue_free()
	lock28.queue_free()
	await idle(1)

func test_precise_mode_degenerate_shapes_fall_back_to_radial() -> void:
	_connect_signals()
	# BOUNCE T29 precise-mode degenerate shapes fall back to radial
	await _settle(factory)
	var zero29 := StaticBody2D.new()
	zero29.position = Vector2(200, 0)
	zero29.collision_layer = 8
	zero29.collision_mask = 2
	var zero29col := CollisionShape2D.new()
	var zero29box := RectangleShape2D.new()
	zero29box.size = Vector2(0, 0)
	zero29col.shape = zero29box
	zero29.add_child(zero29col)
	add(zero29)
	await physics()
	var d29 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d29.bounce_mode = 1
	var v29: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d29)
	for i in 120:
		await physics()
		if v29.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v29.bullet_get_bounce_count(0) >= 1, "zero-size rect falls back to radial and bounces")
	assert_true(_finite_volley(v29), "volley finite after degenerate-shape bounce")
	zero29.queue_free()
	await idle(1)
	await _settle(factory)
	var null29 := StaticBody2D.new()
	null29.position = Vector2(200, 0)
	null29.collision_layer = 8
	null29.collision_mask = 2
	var null29col := CollisionShape2D.new()
	null29.add_child(null29col)
	add(null29)
	await physics()
	var v29b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d29)
	for i in 120:
		await physics()
		if v29b.bullet_get_bounce_count(0) >= 1:
			break
	# A CollisionShape2D with no shape registers no contact: the wall is
	# intangible, so precise mode has nothing to fall back on. The volley
	# must sail through unharmed (pre-GUT all groups shared one process and
	# this section bounced off T0's never-freed wall, masking it).
	assert_true(v29b.bullet_get_bounce_count(0) == 0, "null-shape wall is intangible, no bounce")
	assert_true(_finite_volley(v29b), "volley finite past null-shape wall")
	null29.queue_free()
	await idle(1)


func test_consumed_bounces_spark_once_and_still_count() -> void:
	_connect_signals()
	# BOUNCE T30 consumed bounces spark once and still count
	await _settle(factory)
	var wall30 := _make_wall(Vector2(200, 0), 8)
	var d30 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d30.bounce_hit_consumed = true
	d30.bounce_max_count = 2
	d30.set_bullet_max_collision_count(2)
	var v30: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d30)
	for i in 120:
		await physics()
		if v30.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v30.bullet_get_bounce_count(0) == 1, "consumed bounce counted once")
	assert_true(v30.get_bullet_collision_count(0) == 1, "consumed bounce also consumes one hit")
	assert_true(_finite_volley(v30), "volley finite after consumed bounce")


func test_setter_rejects_keep_old_values() -> void:
	_connect_signals()
	# BOUNCE T31 setter rejects keep old values
	var d31 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d31.set_bounce_strength(NAN)
	assert_true(absf(d31.bounce_strength - 1.0) < 0.0001, "NaN strength rejected")
	d31.set_bounce_strength(-1.0)
	assert_true(absf(d31.bounce_strength - 1.0) < 0.0001, "negative strength rejected")
	d31.set_bounce_mask(-1)
	assert_true(d31.bounce_mask == 8, "negative mask rejected")
	d31.set_bounce_max_count(-1)
	assert_true(d31.bounce_max_count == 0, "negative max count rejected")
	d31.set_bounce_cooldown_sec(5.0)
	assert_true(absf(d31.bounce_cooldown_sec - 0.05) < 0.0001, "over-range cooldown rejected")
	d31.set_bounce_randomness_deg(999.0)
	assert_true(absf(d31.bounce_randomness_deg) < 0.0001, "over-range randomness rejected")
	var v31: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4]))
	v31.set_bounce_strength(NAN)
	assert_true(absf(v31.get_bounce_strength() - 1.0) < 0.0001, "live NaN strength rejected")
	v31.set_bounce_mask(-1)
	assert_true(v31.get_bounce_mask() == 8, "live negative mask rejected")
	expect_errors_containing("bounce_strength must be finite", 2, "NaN strength fails loud")
	expect_errors_containing("bounce_mask must be", 1, "negative mask fails loud")
	expect_errors_containing("bounce_max_count must be", 1, "negative max count fails loud")
	expect_errors_containing("bounce_cooldown_sec must be finite", 1, "bad cooldown fails loud")
	expect_errors_containing("bounce_randomness_deg must be finite", 1, "bad randomness fails loud")
	expect_errors_containing("set_bounce_strength: value must be finite", 1, "live NaN strength fails loud")
	expect_errors_containing("set_bounce_mask: value must be", 1, "live negative mask fails loud")


func test_mover_switching_gravity_push_unlocked_orbit_spawner_routing() -> void:
	_connect_signals()
	# BOUNCE T32 mover switching, gravity push, unlocked orbit, spawner routing
	await _settle(factory)
	var push32 := RigidBody2D.new()
	push32.position = Vector2(-150, 300)
	push32.collision_layer = 8
	push32.collision_mask = 0
	push32.gravity_scale = 0.0
	push32.linear_damp = 0.0
	push32.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	push32.can_sleep = false
	push32.linear_velocity = Vector2(500, 0)
	var p32col := CollisionShape2D.new()
	var p32box := RectangleShape2D.new()
	p32box.size = Vector2(20, 400)
	p32col.shape = p32box
	push32.add_child(p32col)
	add(push32)
	var charge32 := RigidBody2D.new()
	charge32.position = Vector2(600, 300)
	charge32.collision_layer = 8
	charge32.collision_mask = 0
	charge32.gravity_scale = 0.0
	charge32.linear_damp = 0.0
	charge32.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	charge32.can_sleep = false
	charge32.linear_velocity = Vector2(-400, 0)
	var c32col := CollisionShape2D.new()
	var c32box := RectangleShape2D.new()
	c32box.size = Vector2(20, 400)
	c32col.shape = c32box
	charge32.add_child(c32col)
	add(charge32)
	await physics()
	var d32 := _bounce_data(Vector2(0, 300), 0.0, 100.0, [4], [4])
	var v32: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d32)
	for i in 200:
		await physics()
		if v32.bullet_get_bounce_count(0) >= 2:
			break
	assert_true(v32.bullet_get_bounce_count(0) >= 2, "push then charge both bounce")
	assert_true(v32.get_bullet_direction(0).x < -0.5, "charger wins the second exchange")
	assert_true(v32.get_bullet_velocity(0).is_finite(), "mover switching stays finite")
	push32.queue_free()
	charge32.queue_free()
	await idle(1)
	await _settle(factory)
	var d32b := _bounce_data(Vector2(0, 300), 0.0, 200.0, [4], [4])
	d32b.gravity = Vector2(0, 50.0)
	var push32b := RigidBody2D.new()
	push32b.position = Vector2(-150, 300)
	push32b.collision_layer = 8
	push32b.collision_mask = 0
	push32b.gravity_scale = 0.0
	push32b.linear_damp = 0.0
	push32b.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	push32b.can_sleep = false
	push32b.linear_velocity = Vector2(500, 0)
	var p32bcol := CollisionShape2D.new()
	var p32bbox := RectangleShape2D.new()
	p32bbox.size = Vector2(20, 400)
	p32bcol.shape = p32bbox
	push32b.add_child(p32bcol)
	add(push32b)
	await physics()
	var v32b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d32b)
	for i in 200:
		await physics()
		if v32b.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v32b.get_bullet_direction(0).x > 0.0, "gravity push keeps forward motion")
	assert_true(v32b.get_bullet_velocity(0).length() > 400.0, "gravity push still surges")
	assert_true(v32b.get_bullet_velocity(0).y > 0.0, "fall continues after a moving bounce")
	push32b.queue_free()
	await idle(1)
	await _settle(factory)
	var moon32 := Node2D.new()
	moon32.position = Vector2(600, 300)
	add(moon32)
	var ringwall := _make_wall(Vector2(600, 300), 8)
	await physics()
	var d32c := _bounce_data(Vector2(500, 300), 0.0, 200.0, [4], [4])
	d32c.set_bullet_max_collision_count(0)
	var v32c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d32c)
	v32c.shared_homing_deque_push_back_node2d_target(moon32)
	v32c.bullet_enable_orbiting(0, 64.0, 2, 0)
	for i in 150:
		await physics()
		if v32c.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v32c.bullet_get_bounce_count(0) >= 1, "unlocked orbit never blocks wall contact")
	assert_true(v32c.get_bullet_transform(0).is_finite(), "unlocked orbit flight finite")
	moon32.queue_free()
	ringwall.queue_free()
	await idle(1)
	await _settle(factory)
	var spawner32 := BulletSpawner2D.new()
	spawner32.bullet_factory_path = factory.get_path()
	spawner32.shooting_enabled = false
	spawner32.pattern_source = BulletSpawner2D.PATTERN_FROM_SELF
	spawner32.position = Vector2(0, 300)
	add(spawner32)
	await idle(1)
	var push32d := RigidBody2D.new()
	push32d.position = Vector2(-150, 300)
	push32d.collision_layer = 8
	push32d.collision_mask = 0
	push32d.gravity_scale = 0.0
	push32d.linear_damp = 0.0
	push32d.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	push32d.can_sleep = false
	push32d.linear_velocity = Vector2(500, 0)
	var p32dcol := CollisionShape2D.new()
	var p32dbox := RectangleShape2D.new()
	p32dbox.size = Vector2(20, 400)
	p32dcol.shape = p32dbox
	push32d.add_child(p32dcol)
	add(push32d)
	await physics()
	spawner32.bounce_body_entered.connect(_on_spawner_bounce)
	spawner32.spawn_data = _bounce_data(Vector2(0, 300), 0.0, 100.0, [4], [4])
	assert_true(spawner32.shoot_once(), "spawner push volley fired")
	for i in 200:
		await physics()
		if _spawner_bounce.size() >= 1:
			break
	assert_true(_spawner_bounce.size() >= 1, "spawner routes the push bounce")
	assert_true(_factory_bounce.is_empty(), "factory silent on spawner push")
	spawner32.queue_free()
	push32d.queue_free()
	await idle(1)


func test_unspawned_instances_never_crash() -> void:
	_connect_signals()
	# BOUNCE T33 unspawned instances never crash
	await _settle(factory)
	var bare := DirectionalBullets2D.new()
	add(bare)
	await idle(1)
	var empty_data := DirectionalBulletsData2D.new()
	empty_data.sprite_frames = H.make_sprite_frames()
	empty_data.transforms = []
	assert_true(bare.enable_multimesh(empty_data, Vector2.ZERO, 0) == false, "enable on never-spawned volley refuses cleanly")
	assert_true(bare.get_amount_bullets() == 0, "fresh volley holds zero bullets")
	assert_true(bare.bullet_get_bounce_count(0) == 0, "bounce count OOB reads 0")
	assert_true(bare.get_bullet_direction(0) == Vector2(), "direction OOB reads zero")
	assert_true(bare.get_bullet_velocity(0) == Vector2(), "velocity OOB reads zero")
	assert_true(bare.get_bullet_transform(0) == Transform2D(), "transform OOB reads identity")
	assert_true(not bare.has_trail_effects(), "no trails without a bake")
	assert_true(bare.debug_get_effect_layers_info().get("trail_bake_count", -1) == 0, "effect debug empty")
	assert_true(bare.debug_get_bounce_info(0).get("valid", true) == false, "bounce debug OOB invalid")
	bare.disable_bullet(0)
	bare.enable_bullet(0)
	bare.set_bullet_transform(0, Transform2D.IDENTITY)
	bare.set_bullet_direction(0, Vector2(1, 0))
	bare.set_bullet_speed_data(0, BulletSpeedData2D.new())
	bare.bullet_set_gravity(0, Vector2(0, 1))
	bare.bullet_set_trail_enabled(0, 0, true)
	bare.play_effect_animation(0, "x")
	assert_true(bare.get_bullet_transform(0) == Transform2D(), "hostile storm leaves identity")
	assert_true(bare.debug_get_volley_info().get("amount_bullets", -1) == 0, "volley info zeroed")
	bare.queue_free()
	await idle(1)
	var bare_block := BlockBullets2D.new()
	add(bare_block)
	await idle(1)
	var empty_block := BlockBulletsData2D.new()
	empty_block.sprite_frames = H.make_sprite_frames()
	empty_block.transforms = []
	assert_true(bare_block.enable_multimesh(empty_block, Vector2.ZERO, 0) == false, "block enable on fresh instance refuses cleanly")
	expect_errors_containing("never spawned through BulletFactory2D", 2, "unspawned enable fails loud")
	expect_errors_containing("Invalid bullet index in", 12, "zero-bullet OOB storm fails loud")
	bare_block.queue_free()
	await idle(1)


func test_teleport_matrix_interpolation_mixing() -> void:
	_connect_signals()
	# BOUNCE T34 teleport matrix + interpolation mixing
	await _settle(factory)
	factory.set_use_physics_interpolation_runtime(false)
	var wall34 := _make_wall(Vector2(200, 0), 8)
	var d34 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	var v34: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d34)
	v34.set_bullet_transform(0, Transform2D(0.0, Vector2(100, 0)))
	factory.set_is_factory_processing_bullets(false)
	for i in 10:
		await physics()
	assert_true(v34.bullet_get_bounce_count(0) == 0, "paused teleport never bounces")
	assert_true(v34.get_bullet_transform(0).origin.distance_to(Vector2(100, 0)) < 0.01, "paused teleport holds still")
	factory.set_is_factory_processing_bullets(true)
	for i in 120:
		await physics()
		if v34.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v34.bullet_get_bounce_count(0) >= 1, "resume bounces the teleported overlap")
	await _settle(factory)
	factory.set_use_physics_interpolation_runtime(true)
	var hunter34 := Node2D.new()
	hunter34.position = Vector2(400, 0)
	add(hunter34)
	var d34b := _bounce_data(Vector2.ZERO, 0.0, 300.0, [], [4])
	var v34b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d34b)
	v34b.shared_homing_deque_push_back_node2d_target(hunter34)
	v34b.set_homing_distance_before_reached(64.0)
	var reached34: Array = []
	var on_reached := func(_v: DirectionalBullets2D, _i: int, _t: Object, _p: Vector2) -> void:
		reached34.append(1)
	v34b.bullet_homing_target_reached.connect(on_reached)
	for i in 3:
		await physics()
	v34b.set_bullet_transform(0, Transform2D(0.0, Vector2(400, 0)))
	for i in 30:
		await physics()
		if reached34.size() >= 1:
			break
	assert_true(reached34.size() >= 1, "teleport onto homing target fires reached")
	assert_true(v34b.get_bullet_transform(0).is_finite(), "post-teleport homing flight finite")
	v34b.bullet_homing_target_reached.disconnect(on_reached)
	hunter34.queue_free()
	await idle(1)
	await _settle(factory)
	var d34c := _bounce_data(Vector2.ZERO, 0.0, 0.0, [4], [4])
	var v34c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d34c)
	for i in 5:
		await physics()
		v34c.set_bullet_transform(0, Transform2D(0.3 * i, Vector2(20 * i, 0)))
		v34c.debug_run_interpolation_pass()
		assert_true(v34c.get_bullet_transform(0).is_finite(), "teleport storm stays finite with interp")
	factory.set_use_physics_interpolation_runtime(false)


func test_paused_steady_overlap_registers_once_on_resume() -> void:
	_connect_signals()
	# BOUNCE T34b (contract fix): an overlap that STARTS while the factory is
	# paused used to be dropped forever (a steady overlap never produces a
	# new ADDED event). Volleys now park it and the factory replays it on
	# resume: exactly one bounce.
	await _settle(factory)
	factory.set_use_physics_interpolation_runtime(false)
	_make_wall(Vector2(100, 0), 8)
	var dp := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	var vp: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(dp)
	factory.set_is_factory_processing_bullets(false)
	vp.set_bullet_transform(0, Transform2D(0.0, Vector2(100, 0)))
	for i in 10:
		await physics()
	assert_eq(vp.bullet_get_bounce_count(0), 0, "nothing drains while paused")
	factory.set_is_factory_processing_bullets(true)
	for i in 60:
		await physics()
		if vp.bullet_get_bounce_count(0) >= 1:
			break
	assert_eq(vp.bullet_get_bounce_count(0), 1, "the paused overlap bounces exactly once after resume")
	assert_true(_finite_volley(vp), "volley finite after the replay")
	await _settle(factory)
	factory.set_use_physics_interpolation_runtime(true)


func test_orbit_endurance_block_census_telegraph_zero_helper_hostility() -> void:
	_connect_signals()
	# BOUNCE T35 orbit endurance, block census, telegraph-zero, helper hostility
	await _settle(factory)
	var moon35 := Node2D.new()
	moon35.position = Vector2(600, 300)
	add(moon35)
	var d35 := _bounce_data(Vector2(500, 300), 0.0, 200.0, [], [4], 30.0)
	var v35: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d35)
	v35.shared_homing_deque_push_back_node2d_target(moon35)
	v35.bullet_enable_orbiting(0, 64.0, 2, 0)
	for i in 720:
		await physics()
	assert_true(v35.get_bullet_transform(0).is_finite() and v35.get_bullet_velocity(0).is_finite(), "720-frame orbit never NaNs")
	assert_true(absf(v35.bullet_get_orbiting_radius(0) - 64.0) < 2.0, "ring radius stable over 720 frames")
	assert_true(v35.is_bullet_status_enabled(0), "orbiter alive after endurance run")
	moon35.queue_free()
	await idle(1)
	await _settle(factory)
	factory.spawn_block_bullets(H.make_block_data(2, 150.0, 10.0))
	await physics()
	assert_true(factory.debug_get_live_volley_ids(0).size() >= 1, "factory census includes block volleys")
	await _settle(factory)
	var tsp := BulletSpawner2D.new()
	add(tsp)
	tsp.set_bullet_factory(factory)
	tsp.set_spawn_data(H.make_directional_data(2, 200.0))
	tsp.set_shooting_enabled(false)
	tsp.set_telegraph_enabled(true)
	tsp.set_telegraph_sec(0.0)
	var fired35: int = tsp.get_volleys_fired()
	assert_true(tsp.shoot_once(), "telegraph-zero shot accepted")
	for i in 30:
		await physics()
		if tsp.get_volleys_fired() > fired35:
			break
	assert_true(tsp.get_volleys_fired() > fired35, "telegraph-zero fires without hanging")
	tsp.queue_free()
	await idle(1)
	await _settle(factory)
	assert_true(BulletFactory2D.helper_generate_transforms_grid(-1, Transform2D.IDENTITY, 2).is_empty(), "negative grid amount refused")
	assert_true(BulletFactory2D.helper_generate_transforms_grid(0, Transform2D.IDENTITY, 2).is_empty(), "zero grid amount yields empty")
	assert_true(BulletFactory2D.helper_generate_transforms_grid(8, Transform2D.IDENTITY, 0).is_empty(), "zero grid rows refused")
	assert_true(BulletFactory2D.helper_generate_transforms_rain(8, Transform2D.IDENTITY, 600.0, Vector2.ZERO, 48.0, 0.0, 1).is_empty(), "zero rain direction refused")
	assert_true(BulletFactory2D.helper_generate_transforms_rain(4, Transform2D.IDENTITY).size() == 4, "rain defaults still generate")
	expect_errors_containing("transforms_amount must be between 0 and 10000", 1, "negative grid amount fails loud")
	expect_errors_containing("rows_per_column must be > 0", 1, "zero grid rows fail loud")
	expect_errors_containing("rain_direction must be finite and non-zero", 1, "zero rain direction fails loud")


func test_stale_target_velocity_never_steers_the_bounce() -> void:
	_connect_signals()
	# BOUNCE T36 stale target velocity never steers the bounce
	await _settle(factory)
	var back36 := CharacterBody2D.new()
	back36.position = Vector2(250, 300)
	back36.collision_layer = 8
	back36.collision_mask = 0
	back36.velocity = Vector2(400, 0)
	var b36col := CollisionShape2D.new()
	var b36circ := CircleShape2D.new()
	b36circ.radius = 12.0
	b36col.shape = b36circ
	back36.add_child(b36col)
	add(back36)
	await physics()
	var d36a := _bounce_data(Vector2(100, 300), 0.0, 200.0, [4], [4])
	var v36a: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d36a)
	for i in 150:
		await physics()
		if v36a.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v36a.bullet_get_bounce_count(0) >= 1, "back-off overlap bounces")
	var vel36a: Vector2 = v36a.get_bullet_velocity(0)
	assert_true(vel36a.is_finite(), "back-off post-bounce finite")
	assert_true(vel36a.x < 0.0, "back-off sends the bullet back -X, never through (+X)")
	back36.queue_free()
	await idle(1)


func test_knob_matrix_every_mix_separates_stays_finite() -> void:
	_connect_signals()
	# BOUNCE T36b knob matrix: every mix separates, stays finite
	await _settle(factory)
	var mix_vels: Array = [Vector2(-400, 0), Vector2(0, 0), Vector2(100, 0), Vector2(400, 0)]
	var mix_names: Array = ["charge", "static", "slow back-off", "fast back-off"]
	var mix_setups: Array = [
		["default", {}],
		["push off", {"bpush": false}],
		["charge off", {"bcharge": false}],
		["strength 2", {"bstr": 2.0}],
		["precise", {"bmode": 1}],
		["consumed", {"bconsumed": true}],
		["scatter 10", {"brand": 10.0}],
	]
	for s in mix_setups:
		for vi in mix_vels.size():
			await _settle(factory)
			var mover := CharacterBody2D.new()
			mover.position = Vector2(250, 300)
			mover.collision_layer = 8
			mover.collision_mask = 0
			mover.velocity = mix_vels[vi]
			var mcol := CollisionShape2D.new()
			var mcirc := CircleShape2D.new()
			mcirc.radius = 12.0
			mcol.shape = mcirc
			mover.add_child(mcol)
			add(mover)
			await physics()
			var dd := _bounce_data(Vector2(100, 300), 0.0, 200.0, [4], [4])
			var cfg: Dictionary = s[1]
			if cfg.get("bpush", true) == false:
				dd.bounce_push_assist = false
			if cfg.get("bcharge", true) == false:
				dd.bounce_charge_amplify = false
			if cfg.has("bstr"):
				dd.bounce_strength = cfg["bstr"]
			if cfg.has("bmode"):
				dd.bounce_mode = cfg["bmode"]
			if cfg.get("bconsumed", false):
				dd.bounce_hit_consumed = true
				dd.set_bullet_max_collision_count(2)
			if cfg.has("brand"):
				dd.bounce_randomness_deg = cfg["brand"]
			var vv: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(dd)
			for i in 150:
				await physics()
				if vv.bullet_get_bounce_count(0) >= 1:
					break
			var tag: String = str(s[0]) + " vs " + str(mix_names[vi])
			assert_true(vv.bullet_get_bounce_count(0) >= 1, tag + " bounces")
			var vel: Vector2 = vv.get_bullet_velocity(0)
			assert_true(vel.is_finite() and vv.get_bullet_transform(0).is_finite(), tag + " stays finite")
			assert_true(vel.x <= 1.0, tag + " never moves forward (+X)")
			if cfg.get("bconsumed", false):
				assert_true(vv.get_bullet_collision_count(0) == 1, tag + " consumes exactly one hit")
			mover.queue_free()
			await idle(1)


func test_queue_time_snapshot_wins_over_mid_drain_mutation() -> void:
	_connect_signals()
	# BOUNCE T36c queue-time snapshot wins over mid-drain mutation
	await _settle(factory)
	var mut36 := CharacterBody2D.new()
	mut36.position = Vector2(250, 308)
	mut36.collision_layer = 8
	mut36.collision_mask = 0
	mut36.velocity = Vector2(-400, 0)
	var mu36col := CollisionShape2D.new()
	var mu36circ := CircleShape2D.new()
	mu36circ.radius = 16.0
	mu36col.shape = mu36circ
	mut36.add_child(mu36col)
	add(mut36)
	await physics()
	var d36c := _bounce_data(Vector2(100, 300), 0.0, 200.0, [4], [4])
	d36c.transforms = [Transform2D(0.0, Vector2(100, 300)), Transform2D(0.0, Vector2(100, 316))]
	var sp36c := BulletSpeedData2D.new()
	sp36c.speed = 200.0
	sp36c.max_speed = 3000.0
	sp36c.acceleration = 0.0
	d36c.all_bullet_speed_data = [d36c.all_bullet_speed_data[0], sp36c]
	var v36c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d36c)
	_t36_body = mut36
	_t36_done = false
	_t36_mutate = true
	for i in 150:
		await physics()
		if v36c.bullet_get_bounce_count(0) >= 1 and v36c.bullet_get_bounce_count(1) >= 1:
			break
	_t36_mutate = false
	assert_true(_t36_done, "mutation handler ran mid-drain")
	assert_true(v36c.bullet_get_bounce_count(0) >= 1 and v36c.bullet_get_bounce_count(1) >= 1, "both records bounce")
	for bi in [0, 1]:
		var bv: Vector2 = v36c.get_bullet_velocity(bi)
		assert_true(bv.is_finite(), "mutated-drain bullet " + str(bi) + " finite")
		assert_true(bv.x < 0.0, "mutated-drain bullet " + str(bi) + " heads -X")
		assert_true(bv.length() > 500.0, "mutated-drain bullet " + str(bi) + " keeps queue-time charge energy")
	mut36.queue_free()
	_t36_body = null
	await idle(1)


func test_cross_type_matrix_platforms_slopes_edges_areas_tilemaps() -> void:
	_connect_signals()
	# BOUNCE T37 cross-type matrix: platforms, slopes, edges, areas, tilemaps
	await _settle(factory)
	var crush37 := AnimatableBody2D.new()
	crush37.position = Vector2(250, 300)
	crush37.collision_layer = 8
	crush37.collision_mask = 0
	crush37.sync_to_physics = false
	crush37.constant_linear_velocity = Vector2(-400, 0)
	var cr37col := CollisionShape2D.new()
	var cr37circ := CircleShape2D.new()
	cr37circ.radius = 12.0
	cr37col.shape = cr37circ
	crush37.add_child(cr37col)
	add(crush37)
	await physics()
	var d37a := _bounce_data(Vector2(100, 300), 0.0, 200.0, [4], [4])
	var v37a: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d37a)
	for i in 150:
		await physics()
		if v37a.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v37a.bullet_get_bounce_count(0) >= 1, "crusher platform bounces")
	var vel37a: Vector2 = v37a.get_bullet_velocity(0)
	assert_true(vel37a.is_finite(), "crusher post-bounce finite")
	assert_true(vel37a.x < -500.0, "crusher amplifies through constant velocity")
	crush37.queue_free()
	await idle(1)

	await _settle(factory)
	var slope37 := StaticBody2D.new()
	slope37.position = Vector2(250, 300)
	slope37.collision_layer = 8
	slope37.collision_mask = 0
	var s37col := CollisionShape2D.new()
	var s37seg := SegmentShape2D.new()
	s37seg.a = Vector2(-100, 100)
	s37seg.b = Vector2(100, -100)
	s37col.shape = s37seg
	slope37.add_child(s37col)
	add(slope37)
	await physics()
	var d37b := _bounce_data(Vector2(100, 300), 0.0, 200.0, [4], [4])
	d37b.bounce_mode = 1
	var v37b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d37b)
	for i in 150:
		await physics()
		if v37b.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v37b.bullet_get_bounce_count(0) >= 1, "slope bounces in precise mode")
	var vel37b: Vector2 = v37b.get_bullet_velocity(0)
	assert_true(vel37b.is_finite(), "slope post-bounce finite")
	assert_true(vel37b.y < -100.0 and absf(vel37b.x) < 60.0, "slope reflects up off the 45-degree face")
	slope37.queue_free()
	await idle(1)

	await _settle(factory)
	var edge37 := StaticBody2D.new()
	edge37.position = Vector2(250, 300)
	edge37.rotation = PI / 4.0
	edge37.collision_layer = 8
	edge37.collision_mask = 0
	var e37col := CollisionShape2D.new()
	var e37bound := WorldBoundaryShape2D.new()
	e37bound.normal = Vector2(-1, 0)
	e37bound.distance = 0.0
	e37col.shape = e37bound
	edge37.add_child(e37col)
	add(edge37)
	await physics()
	var d37c := _bounce_data(Vector2(100, 300), 0.0, 200.0, [4], [4])
	d37c.bounce_mode = 1
	var v37c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d37c)
	for i in 150:
		await physics()
		if v37c.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v37c.bullet_get_bounce_count(0) >= 1, "boundary plane bounces in precise mode")
	var vel37c: Vector2 = v37c.get_bullet_velocity(0)
	assert_true(vel37c.is_finite(), "boundary post-bounce finite")
	assert_true(vel37c.y < -100.0, "boundary uses the stored normal, not radial")
	edge37.queue_free()
	await idle(1)

	await _settle(factory)
	var poly37 := StaticBody2D.new()
	poly37.position = Vector2(250, 300)
	poly37.collision_layer = 8
	poly37.collision_mask = 0
	var p37col := CollisionShape2D.new()
	var p37poly := ConvexPolygonShape2D.new()
	p37poly.points = PackedVector2Array([Vector2(-20, -20), Vector2(20, -20), Vector2(20, 20), Vector2(-20, 20)])
	p37col.shape = p37poly
	poly37.add_child(p37col)
	add(poly37)
	await physics()
	var d37d := _bounce_data(Vector2(100, 300), 0.0, 200.0, [4], [4])
	d37d.bounce_mode = 1
	var v37d: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d37d)
	for i in 150:
		await physics()
		if v37d.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v37d.bullet_get_bounce_count(0) >= 1, "convex falls back and bounces")
	var vel37d: Vector2 = v37d.get_bullet_velocity(0)
	assert_true(vel37d.is_finite(), "convex post-bounce finite")
	assert_true(vel37d.x < 0.0, "convex radial fallback separates")
	poly37.queue_free()
	await idle(1)

	await _settle(factory)
	var awake37 := RigidBody2D.new()
	awake37.position = Vector2(250, 300)
	awake37.collision_layer = 8
	awake37.collision_mask = 0
	awake37.gravity_scale = 0.0
	awake37.linear_damp = 0.0
	awake37.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	awake37.can_sleep = false
	awake37.linear_velocity = Vector2(-400, 0)
	var a37col := CollisionShape2D.new()
	var a37circ := CircleShape2D.new()
	a37circ.radius = 12.0
	a37col.shape = a37circ
	awake37.add_child(a37col)
	add(awake37)
	await physics()
	var d37e := _bounce_data(Vector2(100, 300), 0.0, 200.0, [4], [4])
	var v37e: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d37e)
	for i in 150:
		await physics()
		if v37e.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v37e.bullet_get_bounce_count(0) >= 1, "awake rigid charger bounces")
	var vel37e: Vector2 = v37e.get_bullet_velocity(0)
	assert_true(vel37e.is_finite(), "awake rigid post-bounce finite")
	assert_true(vel37e.x < -500.0, "awake rigid amplifies")
	awake37.queue_free()
	await idle(1)

	await _settle(factory)
	var sleep37 := RigidBody2D.new()
	sleep37.position = Vector2(250, 300)
	sleep37.collision_layer = 8
	sleep37.collision_mask = 0
	sleep37.gravity_scale = 0.0
	sleep37.linear_damp = 0.0
	sleep37.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	sleep37.linear_velocity = Vector2(-400, 0)
	var sl37col := CollisionShape2D.new()
	var sl37circ := CircleShape2D.new()
	sl37circ.radius = 12.0
	sl37col.shape = sl37circ
	sleep37.add_child(sl37col)
	add(sleep37)
	sleep37.sleeping = true
	await physics()
	var d37f := _bounce_data(Vector2(100, 300), 0.0, 200.0, [4], [4])
	var v37f: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d37f)
	for i in 150:
		await physics()
		if v37f.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v37f.bullet_get_bounce_count(0) >= 1, "sleeping rigid bounces")
	var vel37f: Vector2 = v37f.get_bullet_velocity(0)
	assert_true(vel37f.is_finite(), "sleeping rigid post-bounce finite")
	assert_true(vel37f.x < 0.0, "sleeping rigid separates either way")
	sleep37.queue_free()
	await idle(1)

	await _settle(factory)
	var still37 := Area2D.new()
	still37.position = Vector2(250, 300)
	still37.collision_layer = 8
	still37.collision_mask = 0
	var st37col := CollisionShape2D.new()
	var st37circ := CircleShape2D.new()
	st37circ.radius = 12.0
	st37col.shape = st37circ
	still37.add_child(st37col)
	add(still37)
	await physics()
	var d37g := _bounce_data(Vector2(100, 300), 0.0, 200.0, [4], [4])
	var v37g: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d37g)
	for i in 150:
		await physics()
		if v37g.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v37g.bullet_get_bounce_count(0) >= 1, "static area bounces")
	var vel37g: Vector2 = v37g.get_bullet_velocity(0)
	assert_true(vel37g.is_finite(), "static area post-bounce finite")
	assert_true(absf(vel37g.x + 200.0) < 50.0, "static area reflects exactly")
	still37.queue_free()
	await idle(1)

	await _settle(factory)
	var dash37 := Area2D.new()
	dash37.position = Vector2(250, 300)
	dash37.collision_layer = 8
	dash37.collision_mask = 0
	var da37col := CollisionShape2D.new()
	var da37circ := CircleShape2D.new()
	da37circ.radius = 12.0
	da37col.shape = da37circ
	dash37.add_child(da37col)
	add(dash37)
	await physics()
	var d37h := _bounce_data(Vector2(100, 300), 0.0, 200.0, [4], [4])
	var v37h: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d37h)
	for i in 60:
		dash37.position.x -= 5.0
		await physics()
		if v37h.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v37h.bullet_get_bounce_count(0) >= 1, "dashing area bounces")
	var vel37h: Vector2 = v37h.get_bullet_velocity(0)
	assert_true(vel37h.is_finite(), "dashing area post-bounce finite")
	assert_true(vel37h.x < -400.0, "dash estimate amplifies beyond static")
	dash37.queue_free()
	await idle(1)

	await _settle(factory)
	var tiles37 := _make_tile_layer()
	await physics()
	var d37i := _bounce_data(Vector2(100, 300), 0.0, 200.0, [4], [4])
	var v37i: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d37i)
	for i in 150:
		await physics()
		if v37i.bullet_get_bounce_count(0) >= 1 or not v37i.is_bullet_status_enabled(0):
			break
	assert_true(v37i.bullet_get_bounce_count(0) == 0, "tilemap stays lethal by default (no layer to match)")
	assert_true(not v37i.is_bullet_status_enabled(0), "tilemap kill takes the normal path")
	tiles37.queue_free()
	await idle(1)

	await _settle(factory)
	var tiles37b := _make_tile_layer()
	await physics()
	var d37j := _bounce_data(Vector2(100, 300), 0.0, 200.0, [4], [4])
	d37j.bounce_tilemap_layers = true
	assert_true(d37j.bounce_tilemap_layers == true, "tilemap opt-in defaults off, sets on")
	var v37j: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d37j)
	assert_true(v37j.get_bounce_tilemap_layers() == true, "live mirror reseeds the opt-in")
	for i in 150:
		await physics()
		if v37j.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v37j.bullet_get_bounce_count(0) >= 1, "tilemap bounces when opted in")
	var vel37j: Vector2 = v37j.get_bullet_velocity(0)
	assert_true(vel37j.is_finite(), "tilemap post-bounce finite")
	assert_true(vel37j.x < 0.0, "tilemap head-on reflection separates")
	tiles37b.queue_free()
	await idle(1)


func test_forensics_reuse_energy_shared_walls_tilemap_budgets() -> void:
	_connect_signals()
	# BOUNCE T38 forensics, reuse energy, shared walls, tilemap budgets
	await _settle(factory)
	var wall38 := _make_wall(Vector2(200, 300), 8)
	await physics()
	var d38a := _bounce_data(Vector2(100, 300), 0.0, 200.0, [4], [4])
	var v38a: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d38a)
	for i in 150:
		await physics()
		if v38a.bullet_get_bounce_count(0) >= 1:
			break
	var info38: Dictionary = v38a.debug_get_bounce_info(0)
	assert_true(info38.get("valid", false), "forensics valid after bounce")
	assert_true((info38.get("last_normal", Vector2.ZERO) as Vector2).x < -0.9, "forensics normal faces the wall")
	assert_true((info38.get("last_target_velocity", Vector2(9, 9)) as Vector2).length() < 1.0, "forensics static target reads zero")
	assert_true(info38.get("bounce_tilemap_layers", true) == false, "forensics tilemap flag defaults off")
	wall38.queue_free()
	await idle(1)

	await _settle(factory)
	var rig38 := CharacterBody2D.new()
	rig38.position = Vector2(250, 300)
	rig38.collision_layer = 8
	rig38.collision_mask = 0
	rig38.velocity = Vector2(-400, 0)
	var rg38col := CollisionShape2D.new()
	var rg38circ := CircleShape2D.new()
	rg38circ.radius = 12.0
	rg38col.shape = rg38circ
	rig38.add_child(rg38col)
	add(rig38)
	await physics()
	var d38b := _bounce_data(Vector2(100, 300), 0.0, 200.0, [4], [4])
	var v38b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d38b)
	for i in 150:
		await physics()
		if v38b.bullet_get_bounce_count(0) >= 1:
			break
	var info38b: Dictionary = v38b.debug_get_bounce_info(0)
	assert_true((info38b.get("last_target_velocity", Vector2.ZERO) as Vector2).x < -300.0, "forensics charger velocity captured")
	rig38.queue_free()
	await idle(1)

	await _settle(factory)
	var wall38b := _make_wall(Vector2(200, 300), 8)
	await physics()
	var d38c := _bounce_data(Vector2(100, 300), 0.0, 200.0, [4], [4])
	d38c.bounce_strength = 2.0
	var v38c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d38c)
	for i in 150:
		await physics()
		if v38c.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v38c.get_bullet_velocity(0).length() > 300.0, "strength 2 boosts")
	await _settle(factory)
	var wall38c := _make_wall(Vector2(200, 300), 8)
	await physics()
	var d38d := _bounce_data(Vector2(100, 300), 0.0, 200.0, [4], [4])
	d38d.bounce_strength = 1.0
	var v38d: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d38d)
	assert_true((v38d.debug_get_bounce_info(0).get("last_normal", Vector2(9, 9)) as Vector2) == Vector2(0, 0), "reuse zeroes forensic ledger")
	for i in 150:
		await physics()
		if v38d.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v38d.bullet_get_bounce_count(0) >= 1, "reused volley bounces fresh")
	assert_true(v38d.get_bullet_velocity(0).length() < 300.0, "reuse drops the old multiplier")
	wall38b.queue_free()
	wall38c.queue_free()
	await idle(1)

	await _settle(factory)
	var shared38 := _make_wall(Vector2(200, 300), 8)
	await physics()
	var vans: Array = []
	for k in 3:
		var dk := _bounce_data(Vector2(100, 300), 0.0, 200.0, [4], [4])
		vans.append(factory.spawn_controllable_directional_bullets(dk))
	for i in 150:
		await physics()
		var done := true
		for vv in vans:
			if (vv as DirectionalBullets2D).bullet_get_bounce_count(0) < 1:
				done = false
		if done:
			break
	for k in 3:
		var vk: DirectionalBullets2D = vans[k]
		assert_true(vk.bullet_get_bounce_count(0) >= 1, "shared wall bounces volley " + str(k))
		assert_true(vk.get_bullet_velocity(0).x < 0.0, "shared wall separates volley " + str(k))
	shared38.queue_free()
	await idle(1)

	await _settle(factory)
	var tiles38 := _make_tile_layer()
	await physics()
	var d38e := _bounce_data(Vector2(100, 300), 0.0, 200.0, [4], [4])
	d38e.bounce_tilemap_layers = true
	d38e.bounce_hit_consumed = true
	d38e.set_bullet_max_collision_count(2)
	var v38e: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d38e)
	for i in 150:
		await physics()
		if v38e.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v38e.bullet_get_bounce_count(0) >= 1, "tilemap consumed bounces")
	assert_true(v38e.get_bullet_collision_count(0) == 1, "tilemap consumed counts the hit")
	assert_true((v38e.debug_get_bounce_info(0).get("last_normal", Vector2.ZERO) as Vector2).x < -0.9, "tilemap forensics head-on")
	tiles38.queue_free()
	await idle(1)

	await _settle(factory)
	var tiles38b := _make_tile_layer()
	await physics()
	var d38f := _bounce_data(Vector2(100, 300), 0.0, 200.0, [4], [4])
	d38f.bounce_tilemap_layers = true
	d38f.bounce_max_count = 1
	var v38f: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d38f)
	for i in 150:
		await physics()
		if v38f.bullet_get_bounce_count(0) >= 1:
			break
	assert_true(v38f.bullet_get_bounce_count(0) == 1, "tilemap budget counts exactly once")
	tiles38b.queue_free()
	await idle(1)

	await idle(1)
