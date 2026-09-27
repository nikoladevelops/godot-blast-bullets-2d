extends SceneTree
## Volley bounce suite: REAL physics (no mocks).
## Covers: defaults-off, free bounce vs wall (precedence over normal path),
## consumed bounce, strength scaling, max-count ping-pong between two walls,
## mask precedence, spawner ownership, precise-vs-radial normals, smooth
## visual pursuit, feature mixes (homing/wobble/gravity/orbit/curves),
## rejects + fuzz, pool-reuse neutrality.
## Run: godot --headless --path test_project --script tests/volley/test_volley_bounce.gd
## Exit code 0 = all pass.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0
var _bounce_body: Array = []
var _bounce_area: Array = []
var _norm_body: Array = []
var _spawner_bounce: Array = []
var _factory_bounce: Array = []

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _on_bounce_body(body: Object, volley: DirectionalBullets2D, idx: int) -> void:
	_bounce_body.append([body, volley, idx])

func _on_bounce_area(area: Object, volley: DirectionalBullets2D, idx: int) -> void:
	_bounce_area.append([area, volley, idx])

func _on_norm_body(body: Object, volley: DirectionalBullets2D, idx: int) -> void:
	_norm_body.append([body, volley, idx])

func _on_spawner_bounce(_t: Object, _v: DirectionalBullets2D, _i: int) -> void:
	_spawner_bounce.append(1)

func _on_factory_bounce(_t: Object, _v: DirectionalBullets2D, _i: int) -> void:
	_factory_bounce.append(1)

# Bullet data aimed at +X (or given angle) from a start pos.
# Bounce walls live on layer 4 (value 8); plain walls on layer 5 (value 16).
func _bounce_data(from: Vector2, angle: float, speed: float, bounce_layers: Array, mask_layers: Array, lifetime: float = 8.0) -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
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
	get_root().add_child(wall)
	return wall

func _clear_signals() -> void:
	_bounce_body.clear()
	_bounce_area.clear()
	_norm_body.clear()
	_spawner_bounce.clear()
	_factory_bounce.clear()

# Park on idle, wipe every flying volley, clear signal buckets: each phase
# then observes ONLY the volley it spawns (stray infinite-lifetime volleys
# from earlier phases would otherwise satisfy signal waits early).
func _settle(factory: BulletFactory2D) -> void:
	await process_frame
	await process_frame
	factory.free_active_bullets()
	_clear_signals()
	await physics_frame

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	factory.directional_bounce_body_entered.connect(_on_bounce_body)
	factory.directional_bounce_area_entered.connect(_on_bounce_area)
	factory.directional_body_entered.connect(_on_norm_body)

	printerr("BOUNCE T0 defaults: feature off, normal path intact")
	var d0 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [], [4])
	_check(d0.bounce_mask == 0, "bounce_mask defaults 0")
	_check(d0.bounce_strength == 1.0, "bounce_strength defaults 1")
	_check(d0.bounce_hit_consumed == false, "bounce_hit_consumed defaults false")
	_check(d0.bounce_max_count == 0, "bounce_max_count defaults 0 (unlimited)")
	_check(d0.bounce_mode == 0, "bounce_mode defaults radial")
	_check(d0.bounce_rotate_texture == true, "bounce_rotate_texture defaults true")
	_check(d0.bounce_rotation_smooth == 0.0, "bounce_rotation_smooth defaults 0")
	_check(d0.bounce_randomness_deg == 0.0, "bounce_randomness defaults 0")
	var wall := _make_wall(Vector2(200, 0), 8)
	await physics_frame
	var v0: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d0)
	_check(v0 != null, "plain spawn ok")
	_check(v0.get_bounce_mask() == 0, "live mirror reseeded to 0")
	for i in 90:
		await physics_frame
		if not v0.is_bullet_status_enabled(0):
			break
	_check(_norm_body.size() >= 1, "normal body signal fires with bounce off")
	_check(_bounce_body.is_empty(), "no bounce signal with bounce off")
	_check(v0.bullet_get_bounce_count(0) == 0, "bounce count stays 0")
	_check(v0.debug_get_bounce_info(0).get("bounce_enabled", true) == false, "debug info reports disabled")
	_check(v0.debug_get_bounce_info(99).get("valid", true) == false, "debug info OOB invalid")
	_check(v0.bullet_get_bounce_count(99) == 0, "bounce count OOB reads 0")

	printerr("BOUNCE T1 free bounce: wall reflects, bullet lives, no hit consumed")
	await _settle(factory)
	var d1 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	var v1: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d1)
	for i in 120:
		await physics_frame
		if _bounce_body.size() >= 1:
			break
	_check(_bounce_body.size() >= 1, "bounce body signal fired")
	if _bounce_body.size() >= 1:
		_check(_bounce_body[0][1] == v1 and (_bounce_body[0][2] as int) == 0, "bounce slim payload (volley, index 0)")
	_check(v1.is_bullet_status_enabled(0), "bullet alive after free bounce")
	_check(v1.bullet_get_bounce_count(0) >= 1, "bounce count tracked")
	_check(v1.get_bullet_collision_count(0) == 0, "free bounce consumes no hit")
	_check(_norm_body.is_empty(), "normal signal suppressed on free bounce (precedence)")
	var dir1: Vector2 = v1.get_bullet_direction(0)
	_check(dir1.x < -0.9 and absf(dir1.y) < 0.3, "heading reflected to -X")
	var spd1: float = v1.get_bullet_velocity(0).length()
	_check(absf(spd1 - 300.0) < 30.0, "elastic strength preserves speed")
	_check(v1.get_bullet_velocity(0).is_finite() and v1.get_bullet_transform(0).is_finite(), "post-bounce state finite")

	printerr("BOUNCE T1b body+area pair on one target: single bounce, no double-flip")
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
	get_root().add_child(eye)
	await physics_frame
	var v1b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4]))
	for i in 120:
		await physics_frame
		if _bounce_body.size() >= 1:
			break
	_check(_bounce_body.size() >= 1, "bounce fired with paired shapes")
	var dir1b: Vector2 = v1b.get_bullet_direction(0)
	_check(dir1b.x < -0.5, "no double-flip back to +X (per-tick guard)")
	_check(v1b.bullet_get_bounce_count(0) == 1, "exactly one bounce counted for the pair")
	eye.queue_free()
	await process_frame

	printerr("BOUNCE T2 consumed bounce: bounce + normal fire, bullet dies at max 1")
	await _settle(factory)
	var d2 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d2.bounce_hit_consumed = true
	var v2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d2)
	for i in 120:
		await physics_frame
		if not v2.is_bullet_status_enabled(0):
			break
	_check(_bounce_body.size() >= 1, "bounce signal fired on consumed hit")
	_check(_norm_body.size() >= 1, "normal signal also fires when consumed")
	_check(not v2.is_bullet_status_enabled(0), "consumed bounce kills at max 1")
	_check(v2.get_bullet_collision_count(0) >= 1, "consumed hit counted")

	printerr("BOUNCE T3 strength scaling")
	await _settle(factory)
	var d3 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d3.bounce_strength = 0.5
	var v3: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d3)
	for i in 120:
		await physics_frame
		if _bounce_body.size() >= 1:
			break
	var spd3: float = v3.get_bullet_velocity(0).length()
	_check(absf(spd3 - 150.0) < 25.0, "strength 0.5 halves speed")
	await _settle(factory)
	var d3b := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d3b.bounce_strength = 0.0
	var v3b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d3b)
	for i in 120:
		await physics_frame
		if _bounce_body.size() >= 1:
			break
	_check(v3b.get_bullet_velocity(0).length() < 5.0, "strength 0 dead-stops")
	_check(v3b.is_bullet_status_enabled(0), "dead-stop bullet stays alive (free)")

	printerr("BOUNCE T4 ping-pong between two walls, max_count gates back to normal")
	await _settle(factory)
	var wall_l := _make_wall(Vector2(-200, 0), 8)
	await physics_frame
	var d4 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d4.bounce_max_count = 2
	d4.set_bullet_max_collision_count(0)
	var v4: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d4)
	for i in 400:
		await physics_frame
		if v4.bullet_get_bounce_count(0) >= 2 and _norm_body.size() >= 1:
			break
	_check(v4.bullet_get_bounce_count(0) == 2, "exactly 2 bounces then budget exhausted")
	_check(_norm_body.size() >= 1, "post-budget hit takes normal path (still alive, max 0)")
	_check(v4.is_bullet_status_enabled(0), "ping-pong survivor alive (infinite hits)")
	wall_l.queue_free()
	await process_frame

	printerr("BOUNCE T5 mask precedence: bounce layer wins, plain layer stays normal")
	await _settle(factory)
	var plain := _make_wall(Vector2(200, 220), 16, Vector2(20, 120))
	await physics_frame
	var v5: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2(0, 220), 0.0, 300.0, [4], [4, 5]))
	for i in 120:
		await physics_frame
		if _norm_body.size() >= 1:
			break
	_check(_bounce_body.is_empty(), "plain-layer wall never bounces")
	_check(_norm_body.size() >= 1, "plain-layer wall reports normally")
	plain.queue_free()
	await process_frame

	printerr("BOUNCE T6 spawner ownership: spawner signal only, factory silent")
	await _settle(factory)
	factory.directional_bounce_body_entered.connect(_on_factory_bounce)
	var spawner := BulletSpawner2D.new()
	spawner.bullet_factory_path = factory.get_path()
	spawner.shooting_enabled = false
	spawner.pattern_source = BulletSpawner2D.PATTERN_FROM_SELF
	spawner.position = Vector2.ZERO
	get_root().add_child(spawner)
	await process_frame
	spawner.bounce_body_entered.connect(_on_spawner_bounce)
	spawner.spawn_data = _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	_check(spawner.shoot_once(), "spawner volley fired")
	for i in 120:
		await physics_frame
		if _spawner_bounce.size() >= 1:
			break
	_check(_spawner_bounce.size() >= 1, "spawner bounce signal fired")
	_check(_factory_bounce.is_empty(), "factory stays silent for spawner volleys")
	factory.directional_bounce_body_entered.disconnect(_on_factory_bounce)
	spawner.queue_free()
	await process_frame

	printerr("BOUNCE T7 precise mode on 45-degree wall reflects across face")
	await _settle(factory)
	var diag := _make_wall(Vector2(200, 0), 8, Vector2(20, 400), PI / 4.0)
	await physics_frame
	var d7 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d7.bounce_mode = 1
	var v7: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d7)
	for i in 120:
		await physics_frame
		if _bounce_body.size() >= 1:
			break
	var dir7: Vector2 = v7.get_bullet_direction(0)
	_check(absf(dir7.y) > 0.85 and absf(dir7.x) < 0.4, "precise face normal turns +X into vertical")
	diag.queue_free()
	await process_frame
	await _settle(factory)
	var v7b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4]))
	for i in 120:
		await physics_frame
		if _bounce_body.size() >= 1:
			break
	var dir7b: Vector2 = v7b.get_bullet_direction(0)
	_check(dir7b.x < -0.9, "radial mode on flat wall still reflects to -X")

	printerr("BOUNCE T8 smooth visual pursuit lags then converges")
	await _settle(factory)
	var d8 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d8.bounce_rotation_smooth = 3.0
	var v8: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d8)
	for i in 120:
		await physics_frame
		if _bounce_body.size() >= 1:
			break
	await physics_frame
	await physics_frame
	var visual_ang: float = v8.get_bullet_transform(0).get_rotation()
	var logic_ang: float = v8.get_bullet_direction(0).angle()
	var lag: float = absf(wrapf(visual_ang - logic_ang, -PI, PI))
	_check(lag > 0.1, "smooth visual lags ballistics right after bounce")
	for i in 150:
		await physics_frame
	var visual_ang2: float = v8.get_bullet_transform(0).get_rotation()
	var logic_ang2: float = v8.get_bullet_direction(0).angle()
	var lag2: float = absf(wrapf(visual_ang2 - logic_ang2, -PI, PI))
	_check(lag2 < 0.15, "smooth visual converges onto heading")
	_check(v8.debug_get_bounce_info(0).get("visual_pending", true) == false, "pursuit flag clears on arrival")

	printerr("BOUNCE T9 mixes: homing + wobble + gravity + curves stay finite")
	await _settle(factory)
	var hunter := Node2D.new()
	# Ahead of the wall: homing drives the bullet INTO the wall (a target
	# behind would U-turn it away and it would never reach the wall).
	hunter.position = Vector2(400, 0)
	get_root().add_child(hunter)
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
		await physics_frame
	_check(_bounce_body.size() >= 1, "homing+wobble+gravity volley still bounces")
	_check(v9.get_bullet_direction(0).is_finite() and v9.get_bullet_velocity(0).is_finite(), "mixed post-bounce state finite")
	_check(v9.get_bullet_transform(0).is_finite(), "mixed transform finite")
	hunter.queue_free()
	await _settle(factory)
	var d9b := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	var v9b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d9b)
	v9b.set_bounce_rotate_texture(false)
	for i in 120:
		await physics_frame
		if _bounce_body.size() >= 1:
			break
	_check(_bounce_body.size() >= 1, "rotate_texture=false still bounces ballistically")

	printerr("BOUNCE T10 rejects + fuzz + pool-reuse neutrality")
	var dr := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	var keep_strength: float = dr.bounce_strength
	dr.set_bounce_strength(NAN)
	_check(dr.bounce_strength == keep_strength, "NaN strength rejected")
	dr.set_bounce_strength(5.0)
	_check(dr.bounce_strength == keep_strength, "strength > 2 rejected")
	var keep_mask: int = dr.bounce_mask
	dr.set_bounce_mask(-1)
	_check(dr.bounce_mask == keep_mask, "negative mask rejected")
	dr.set_bounce_mode(7)
	_check(dr.bounce_mode == 0, "bad mode rejected")
	dr.set_bounce_randomness_deg(999.0)
	_check(dr.bounce_randomness_deg == 0.0, "randomness > 180 rejected")
	dr.set_bounce_cooldown_sec(9.0)
	_check(dr.bounce_cooldown_sec == 0.05, "cooldown > 1 rejected")
	dr.set_bounce_max_count(-3)
	_check(dr.bounce_max_count == 0, "negative max count rejected")
	await _settle(factory)
	var vr: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(dr)
	for i in 120:
		await physics_frame
		if _bounce_body.size() >= 1:
			break
	_check(_bounce_body.size() >= 1, "rejected-value volley still bounces sanely")
	_check(vr.get_bullet_velocity(0).is_finite(), "post-reject state finite")
	# Pool reuse: free the bounce volley on idle, spawn a plain one (pops the
	# same bucket), bounce config must reseed to off.
	await process_frame
	await process_frame
	factory.free_active_bullets()
	await process_frame
	await process_frame
	var plain2: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2.ZERO, 0.0, 300.0, [], [4]))
	_check(plain2.get_bounce_mask() == 0, "pooled reuse reseeds bounce off")
	_check(plain2.bullet_get_bounce_count(0) == 0, "pooled reuse zeroes bounce ledger")
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after bounce churn")

	factory.directional_bounce_body_entered.disconnect(_on_bounce_body)
	factory.directional_bounce_area_entered.disconnect(_on_bounce_area)
	factory.directional_body_entered.disconnect(_on_norm_body)
	factory.reset()
	wall.queue_free()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL BOUNCE TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
