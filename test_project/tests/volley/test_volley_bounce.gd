extends SceneTree
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
## pool-reuse neutrality.
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

var _bounce_capture: Array = []

func _on_bounce_capture(_body: Object, volley: DirectionalBullets2D, idx: int) -> void:
	_bounce_capture.append([volley.debug_get_previous_origin(idx), volley.get_bullet_global_transform(idx).origin])

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
	_bounce_capture.clear()

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
	wall.position.x = 2000.0
	await physics_frame
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
	wall.position.x = 200.0
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
	dr.set_bounce_strength(-1.0)
	_check(dr.bounce_strength == keep_strength, "negative strength rejected")
	dr.set_bounce_strength(5.0)
	_check(dr.bounce_strength == 5.0, "strength uncapped (5.0 accepted)")
	for p in dr.get_property_list():
		if str(p.get("name", "")) == "bounce_strength":
			_check(int(p.get("hint", -1)) == PROPERTY_HINT_NONE, "no editor cap on strength (plain float, user decides)")
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

	printerr("BOUNCE T11 uncapped strength: 5x reflection, 50x never clamps")
	await _settle(factory)
	var d11 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d11.bounce_strength = 5.0
	var v11: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d11)
	for i in 120:
		await physics_frame
		if _bounce_body.size() >= 1:
			break
	var spd11: float = v11.get_bullet_velocity(0).length()
	_check(absf(spd11 - 1500.0) < 120.0, "strength 5 scales speed 5x")
	_check(v11.get_bullet_speed_data(0).max_speed >= 1500.0 - 120.0, "bounce raises cached max_speed to the new speed")
	for i in 60:
		await physics_frame
	var spd11_late: float = v11.get_bullet_velocity(0).length()
	_check(absf(spd11_late - 1500.0) < 150.0, "strength 5 boost persists (no tick erosion)")
	await _settle(factory)
	var d11b := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d11b.bounce_strength = 50.0
	var v11b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d11b)
	for i in 120:
		await physics_frame
		if _bounce_body.size() >= 1:
			break
	var spd11b: float = v11b.get_bullet_velocity(0).length()
	_check(v11b.get_bullet_velocity(0).is_finite(), "strength 50 stays finite")
	_check(absf(spd11b - 15000.0) < 1500.0, "strength 50 never clamps (15000, uncapped)")
	_check(v11b.get_bullet_speed_data(0).max_speed >= 15000.0 - 1500.0, "strength 50 raises max_speed too")

	printerr("BOUNCE T11b boost survives speed-curve overwrite")
	await _settle(factory)
	var d11c := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d11c.bounce_strength = 2.0
	var v11c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d11c)
	for i in 120:
		await physics_frame
		if _bounce_body.size() >= 1:
			break
	_check(absf(v11c.get_bullet_velocity(0).length() - 600.0) < 60.0, "strength 2 doubles speed")
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
		await physics_frame
	_check(absf(v11c.get_bullet_velocity(0).length() - 600.0) < 90.0, "curve rewrite keeps the x2 boost")
	v11c.remove_shared_bullet_curves_data()
	for i in 10:
		await physics_frame
	_check(absf(v11c.get_bullet_velocity(0).length() - 600.0) < 90.0, "boost survives curve removal")

	printerr("BOUNCE T12 cooldown vs consumed: pair counts once, re-hit counts")
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
	get_root().add_child(eye2)
	await physics_frame
	var d12 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d12.bounce_hit_consumed = true
	d12.set_bullet_max_collision_count(10)
	var v12: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d12)
	for i in 120:
		await physics_frame
		if _bounce_body.size() >= 1:
			break
	_check(v12.bullet_get_bounce_count(0) == 1, "pair consumed: exactly one bounce")
	_check(v12.get_bullet_collision_count(0) == 1, "pair consumed: exactly one hit (no double count)")
	eye2.queue_free()
	await process_frame
	await _settle(factory)
	var d12b := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d12b.bounce_hit_consumed = true
	d12b.set_bullet_max_collision_count(10)
	d12b.bounce_cooldown_sec = 5.0
	var v12b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d12b)
	for i in 120:
		await physics_frame
		if _bounce_body.size() >= 1:
			break
	_check(v12b.bullet_get_bounce_count(0) == 1, "first bounce recorded")
	v12b.teleport_bullet(0, Vector2(195, 0))
	for i in 40:
		await physics_frame
	_check(v12b.bullet_get_bounce_count(0) == 1, "cooldown re-hit does not re-bounce")
	_check(v12b.get_bullet_collision_count(0) >= 2, "cooldown re-hit still counts when consumed")

	printerr("BOUNCE T13 snap keeps render position continuous")
	await _settle(factory)
	factory.set_use_physics_interpolation_runtime(true)
	factory.directional_bounce_body_entered.connect(_on_bounce_capture)
	var v13: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4]))
	for i in 5:
		await physics_frame
	for i in 120:
		await physics_frame
		if _bounce_body.size() >= 1:
			break
	# Captured inside the bounce emission (drain time): prev must still hold
	# the tick-start origin while current already carries the escape nudge.
	# A snap that overwrote prev with current would read a zero gap here.
	_check(_bounce_capture.size() >= 1, "bounce capture recorded at emit time")
	if _bounce_capture.size() >= 1:
		var cap_prev: Vector2 = _bounce_capture[0][0]
		var cap_cur: Vector2 = _bounce_capture[0][1]
		var cap_gap: float = cap_cur.x - cap_prev.x
		# Gap = one tick of +X travel (~+5px) plus the escape nudge (~-8px):
		# about -3px. Zero would mean the snap overwrote prev with current.
		_check(cap_gap < -0.5 and cap_gap > -8.0, "prev origin preserved across snap (render lerps)")
		_check(cap_prev.is_finite() and cap_cur.is_finite(), "prev/current finite")
	factory.directional_bounce_body_entered.disconnect(_on_bounce_capture)
	factory.set_use_physics_interpolation_runtime(false)

	printerr("BOUNCE T14 precise mode on capsule wall uses cap normal")
	await _settle(factory)
	wall.position.x = 2000.0
	await physics_frame
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
	get_root().add_child(cap_wall)
	await physics_frame
	var d14 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d14.bounce_mode = 1
	var v14: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d14)
	for i in 120:
		await physics_frame
		if _bounce_body.size() >= 1:
			break
	var dir14: Vector2 = v14.get_bullet_direction(0)
	_check(_bounce_body.size() >= 1, "capsule wall bounces in precise mode")
	_check(dir14.x < -0.9 and absf(dir14.y) < 0.3, "capsule side normal reflects to -X")
	cap_wall.queue_free()
	wall.position.x = 200.0
	await process_frame

	printerr("BOUNCE T15 teleport into wall bounces fresh overlap")
	await _settle(factory)
	var v15: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4]))
	v15.teleport_bullet(0, Vector2(195, 0))
	for i in 60:
		await physics_frame
		if _bounce_body.size() >= 1:
			break
	_check(_bounce_body.size() >= 1, "teleported overlap bounces")
	_check(v15.bullet_get_bounce_count(0) >= 1, "teleport bounce counted")
	_check(v15.get_bullet_direction(0).x < -0.5, "teleport bounce heads out")

	printerr("BOUNCE T16 attachment rides the escape nudge")
	await _settle(factory)
	var Probe := preload("res://tests/scenes/attachment_probe.gd")
	var probe_node := BulletAttachment2D.new()
	probe_node.set_script(Probe)
	var pack := PackedScene.new()
	_check(pack.pack(probe_node) == OK, "probe scene packs")
	probe_node.queue_free()
	var v16: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4]))
	v16.bullet_set_attachment(0, pack, Vector2.ZERO, true)
	for i in 120:
		await physics_frame
		if _bounce_body.size() >= 1:
			break
	for i in 5:
		await physics_frame
	var att16: BulletAttachment2D = v16.bullet_get_attachment(0) as BulletAttachment2D
	_check(att16 != null, "attachment present after bounce")
	if att16 != null:
		var miss16: float = att16.global_position.distance_to(v16.get_bullet_global_transform(0).origin)
		_check(miss16 < 5.0, "attachment tracks nudged bullet")

	printerr("BOUNCE T17 lifetime expiry inside cooldown is clean")
	await _settle(factory)
	var d17 := _bounce_data(Vector2.ZERO, 0.0, 600.0, [4], [4])
	d17.bounce_cooldown_sec = 5.0
	d17.max_life_time = 0.5
	var v17: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d17)
	for i in 120:
		await physics_frame
		if _bounce_body.size() >= 1:
			break
	_check(_bounce_body.size() >= 1, "fast bullet bounces before expiry")
	for i in 90:
		await physics_frame
		if not v17.is_bullet_status_enabled(0):
			break
	_check(not v17.is_bullet_status_enabled(0), "bullet expires inside cooldown window")
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after cooldown expiry")

	printerr("BOUNCE T18 spawner retarget keeps bounce config live")
	await _settle(factory)
	var sp18 := BulletSpawner2D.new()
	sp18.bullet_factory_path = factory.get_path()
	sp18.shooting_enabled = false
	sp18.pattern_source = BulletSpawner2D.PATTERN_FROM_SELF
	sp18.position = Vector2.ZERO
	sp18.homing_enabled = true
	sp18.homing_target_source = BulletSpawner2D.HOMING_SOURCE_GLOBAL_POSITION
	sp18.homing_global_position = Vector2(400, 0)
	get_root().add_child(sp18)
	await process_frame
	sp18.spawn_data = _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	_check(sp18.shoot_once(), "homing spawner volley fired")
	var live18: Array = sp18.get_live_volleys()
	_check(live18.size() >= 1, "volley tracked")
	sp18.retarget_live_volleys()
	var rv18: DirectionalBullets2D = live18[0] as DirectionalBullets2D
	_check(rv18.get_bounce_mask() == 8, "retarget keeps bounce mask")
	_check(rv18.debug_get_bounce_info(0).get("bounce_enabled", false) == true, "retarget keeps bounce armed")
	sp18.bounce_body_entered.connect(_on_spawner_bounce)
	for i in 150:
		await physics_frame
		if _spawner_bounce.size() >= 1:
			break
	_check(_spawner_bounce.size() >= 1, "retargeted volley still bounces")
	sp18.queue_free()
	await process_frame

	printerr("BOUNCE T19 bulk bounce counts")
	await _settle(factory)
	var d19 := DirectionalBulletsData2D.new()
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
	_check(counts19_init.size() == 3 and int(counts19_init[0]) == 0 and int(counts19_init[1]) == 0 and int(counts19_init[2]) == 0, "bulk counts start at zero")
	for i in 150:
		await physics_frame
		if v19.bullet_get_bounce_count(0) >= 1 and v19.bullet_get_bounce_count(1) >= 1 and v19.bullet_get_bounce_count(2) >= 1:
			break
	var counts19: Array = v19.all_bullets_get_bounce_count()
	_check(counts19.size() == 3 and int(counts19[0]) == 1 and int(counts19[1]) == 1 and int(counts19[2]) == 1, "bulk counts track every bullet")

	printerr("BOUNCE T20 inspector groups stay coherent")
	var data20 := DirectionalBulletsData2D.new()
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
	_check(bounce_group_at >= 0, "bounce group header registered")
	_check(first_bounce_at > bounce_group_at, "bounce props follow their group")
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
	_check(data_groups.slice(0, want_data_groups.size()) == want_data_groups, "data group order matches workflow")
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
	_check(duplicate_groups.is_empty(), "no duplicate group titles on the resource")
	# Rotation triplet is split across classes BY DESIGN: the per-bullet
	# arrays live on the base (BlockBullets spins from the shared base spawn
	# path), the shared fallback lives here. Moving them together would break
	# BlockBullets. Pin the split so nobody "fixes" it into a regression.
	_check(str(data_group_of.get("shared_bullet_rotation_data", "")) == "Bullet Rotation", "shared rotation grouped with steering")
	_check(str(data_group_of.get("all_bullet_rotation_data", "")) == "Per-Bullet Rotation", "per-bullet rotation grouped with arrays")
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
	_check(triplet_ok, "related triplets stick together in one group")
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
	_check(collision_group_idx >= 0 and collision_group_idx < layer_idx and layer_idx < mask_idx, "base Collision group precedes layer props")
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
	_check(base_groups.slice(0, 6) == ["Bullets", "Appearance", "Collision", "Attachments", "Per-Bullet Rotation", "Rendering and Material"], "base group order matches workflow")
	var base_group_of := {}
	var base_cur := ""
	for p in base_props:
		var pname := str(p.get("name", ""))
		var pusage: int = int(p.get("usage", 0))
		if (pusage & PROPERTY_USAGE_GROUP) != 0:
			base_cur = pname
		elif pname != "":
			base_group_of[pname] = base_cur
	_check(base_group_of.get("max_life_time", "") == "Appearance", "lifetime lives with Appearance")
	_check(base_group_of.get("z_index", "") == "Appearance", "z-index lives with Appearance")
	_check(base_group_of.get("shared_bullets_custom_data", "") == "Collision", "custom data lives with Collision")
	_check(base_group_of.get("rotate_only_textures", "") == "Appearance", "rotate-only flag lives with Appearance")
	_check(base_group_of.get("is_texture_rotation_permanent", "") == "Appearance", "permanent rotation flag lives with Appearance")
	_check(base_group_of.get("stop_rotation_when_max_reached", "") == "Per-Bullet Rotation", "stop flag lives with rotation")
	# Live instance mirrors the data workflow order.
	var vinst: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2(0, 9000), 0.0, 1.0, [], [4]))
	var inst_groups: Array = []
	for p in vinst.get_property_list():
		var pname := str(p.get("name", ""))
		var pusage: int = int(p.get("usage", 0))
		if (pusage & PROPERTY_USAGE_GROUP) != 0 and not inst_groups.has(pname):
			inst_groups.append(pname)
	_check(inst_groups.slice(0, 7) == ["Movement Speed", "Bullet Rotation", "Wobble", "Gravity", "Bounce and Ricochet", "Movement Pattern Paths", "Homing"], "instance group order mirrors data")
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
	_check(danglers.is_empty(), "transforms grouped under Bullets, not Bounce")
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
	_check(vgroups.get("shared_bullets_custom_data", "") == "Custom Data", "volley custom data grouped")
	_check(vgroups.get("is_multimesh_auto_pooling_enabled", "") == "Pooling", "volley pooling grouped")
	_check(vgroups.get("bullet_max_collision_count", "") == "Collision", "volley collision grouped")
	_check(vgroups.get("is_life_time_infinite", "") == "Lifetime", "volley lifetime grouped")
	_check(vgroups.get("shared_bullet_curves_data", "") == "Curves", "volley curves grouped")
	var spawner20 := BulletSpawner2D.new()
	get_root().add_child(spawner20)
	await process_frame
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
		_check(dups.is_empty(), "no duplicate group titles on " + str(entry[1]))
	var block20 := BlockBulletsData2D.new()
	var block_groups: Array = []
	for p in block20.get_property_list():
		if (int(p.get("usage", 0)) & PROPERTY_USAGE_GROUP) != 0:
			var gname := str(p.get("name", ""))
			if not block_groups.has(gname):
				block_groups.append(gname)
	_check(block_groups.slice(0, 1) == ["Block Bullets"], "block data leads with its own group")
	_check(groups_found.has("Shooting"), "spawner Shooting group present")
	_check(groups_found.has("Homing"), "spawner Homing group present")
	_check(groups_found.has("Orbiting"), "spawner Orbiting group present")
	_check(groups_found.has("Preview"), "spawner Preview group present")
	_check(groups_found.has("Bullet Patterns"), "spawner Bullet Patterns group present")
	_check(not groups_found.has("Transform Generation"), "old Transform Generation name gone")
	_check(not groups_found.has("Burst and Telegraph"), "Burst group merged away")
	_check(spawner_group_order.slice(0, 6) == ["Bullet Patterns", "Shooting", "Spin", "Homing", "Orbiting", "Preview"], "spawner group order: patterns, shooting, spin, homing, orbit, preview")
	spawner20.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_GRID
	await process_frame
	var ring_visible_grid := false
	var fan_visible_grid := false
	for p in spawner20.get_property_list():
		var pname := str(p.get("name", ""))
		var shown := (int(p.get("usage", 0)) & PROPERTY_USAGE_EDITOR) != 0
		if pname == "helper_ring_radius" and shown:
			ring_visible_grid = true
		if pname == "helper_fan_spread" and shown:
			fan_visible_grid = true
	_check(not ring_visible_grid and not fan_visible_grid, "grid mode hides other families")
	spawner20.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_RING
	await process_frame
	var ring_visible_ring := false
	for p in spawner20.get_property_list():
		if str(p.get("name", "")) == "helper_ring_radius" and (int(p.get("usage", 0)) & PROPERTY_USAGE_EDITOR) != 0:
			ring_visible_ring = true
	_check(ring_visible_ring, "ring mode reveals ring knobs")
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
	_check(prop_group.get("pattern_source", "") == "Bullet Patterns", "pattern_source grouped under Bullet Patterns")
	_check(prop_group.get("helper_ring_radius", "") == "Bullet Patterns", "helpers grouped under Bullet Patterns")
	_check(prop_group.get("helper_skip_indices", "") == "Bullet Patterns", "skip indices grouped under Bullet Patterns")
	_check(prop_group.get("pattern_scale", "") == "Bullet Patterns", "pattern_scale grouped under Bullet Patterns")
	_check(prop_group.get("transforms_scale", "") == "Bullet Patterns", "transforms_scale grouped under Bullet Patterns")
	_check(prop_group.get("spawn_position_offset", "") == "Bullet Patterns", "spawn offset grouped under Bullet Patterns")
	_check(prop_group.get("shooting_enabled", "") == "Shooting", "shooting master grouped")
	_check(prop_group.get("reload_jitter_sec", "") == "Shooting", "reload jitter moved to Shooting")
	_check(prop_group.get("reload_jitter_seed", "") == "Shooting", "reload seed moved to Shooting")
	_check(prop_group.get("max_live_bullets", "") == "Shooting", "max_live_bullets moved to Shooting")
	_check(prop_group.get("spin_enabled", "") == "Spin", "spin master grouped")
	_check(prop_group.get("homing_delay_sec", "") == "Homing", "homing gates moved to Homing")
	_check(prop_group.get("homing_duration_sec", "") == "Homing", "homing duration moved to Homing")
	_check(prop_group.get("homing_lose_range_px", "") == "Homing", "homing lose range moved to Homing")
	_check(prop_group.get("homing_fire_arc_deg", "") == "Homing", "fire arc moved to Homing")
	_check(prop_group.get("homing_retarget_phase", "") == "Homing", "retarget phase moved to Homing")
	_check(prop_group.get("burst_enabled", "") == "Shooting", "burst merged into Shooting")
	_check(prop_group.get("burst_count", "") == "Shooting", "burst count merged into Shooting")
	_check(prop_group.get("telegraph_enabled", "") == "Shooting", "telegraph merged into Shooting")
	_check(prop_group.get("telegraph_sec", "") == "Shooting", "telegraph seconds merged into Shooting")
	_check(prop_group.get("telegraph_sec", "") == "Shooting", "telegraph seconds merged into Shooting")
	_check(prop_group.get("orbiting_enabled", "") == "Orbiting", "orbiting master grouped")
	_check(prop_group.get("show_pattern_preview", "") == "Preview", "preview master grouped")
	var stray_homing := []
	for pname in prop_group.keys():
		var gname := str(prop_group[pname])
		if (pname.begins_with("homing_") or pname == "adjust_direction_based_on_rotation") and gname != "Homing":
			stray_homing.append(pname)
		if pname.begins_with("reload_jitter") and gname != "Shooting":
			stray_homing.append(pname)
		if pname == "max_live_bullets" and gname != "Shooting":
			stray_homing.append(pname)
	_check(stray_homing.is_empty(), "no homing/reload/budget prop leaks into Spin or Burst")
	spawner20.queue_free()
	await process_frame

	printerr("BOUNCE T21 runtime toggles and gravity flag refresh")
	await _settle(factory)
	var v21: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2.ZERO, 0.0, 300.0, [], [4]))
	_check(v21.debug_get_bounce_info(0).get("bounce_enabled", true) == false, "starts disarmed")
	v21.set_bounce_mask(8)
	_check(v21.debug_get_bounce_info(0).get("bounce_enabled", false) == true, "runtime arm sizes ledger")
	for i in 120:
		await physics_frame
		if _bounce_body.size() >= 1:
			break
	_check(_bounce_body.size() >= 1, "runtime-armed volley bounces")
	v21.set_bounce_mask(0)
	_check(v21.debug_get_bounce_info(0).get("bounce_enabled", true) == false, "runtime disarm clears armed state")
	v21.set_bounce_mask(8)
	_check(v21.debug_get_bounce_info(0).get("bounce_enabled", false) == true, "re-arm works after disarm")
	await _settle(factory)
	var v21c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4]))
	v21c.set_bounce_mask(0)
	for i in 120:
		await physics_frame
		if not v21c.is_bullet_status_enabled(0):
			break
	_check(_bounce_body.is_empty(), "runtime disarm flies through clean")
	_check(not v21c.is_bullet_status_enabled(0), "disarmed bullet dies normally")
	await _settle(factory)
	var v21g: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2.ZERO, 0.0, 0.0, [], [4]))
	v21g.set_gravity(Vector2(0, 500.0))
	for i in 45:
		await physics_frame
	_check(v21g.bullet_get_fall_speed(0) > 20.0, "runtime gravity engages fall")
	v21g.bullet_set_gravity(0, Vector2(0, 0))
	for i in 30:
		await physics_frame
	_check(v21g.bullet_get_fall_speed(0) < 5.0, "zeroed gravity holds fall speed")

	printerr("BOUNCE T22 same-target debounce: stuck bullet bounces once per window")
	await _settle(factory)
	var d22 := _bounce_data(Vector2(100, 0), 0.0, 0.0, [4], [4])
	_check(d22.bounce_debounce_sec == 0.15, "debounce defaults 0.15")
	var keep_deb: float = d22.bounce_debounce_sec
	d22.set_bounce_debounce_sec(NAN)
	_check(d22.bounce_debounce_sec == keep_deb, "NaN debounce rejected")
	d22.set_bounce_debounce_sec(-1.0)
	_check(d22.bounce_debounce_sec == keep_deb, "negative debounce rejected")
	d22.bounce_cooldown_sec = 0.0
	var v22: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d22)
	_check(absf(v22.get_bounce_debounce_sec() - 0.15) < 0.0001, "live mirror reseeds debounce")
	for i in 30:
		if i % 2 == 0:
			v22.set_bullet_transform(0, Transform2D(0.0, Vector2(200, 0)))
		else:
			v22.set_bullet_transform(0, Transform2D(0.0, Vector2(100, 0)))
		await physics_frame
	var stuck_count: int = v22.bullet_get_bounce_count(0)
	_check(stuck_count <= 5, "same wall re-hits debounced, not machine-gunned (got %d)" % stuck_count)
	_check(stuck_count >= 1, "first contact still bounces")
	_check(float(v22.debug_get_bounce_info(0).get("debounce", -1.0)) >= 0.0, "debug info reports debounce window")
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
		await physics_frame
	_check(v22b.bullet_get_bounce_count(0) >= 12, "debounce 0 bounces every re-entry (got %d)" % v22b.bullet_get_bounce_count(0))
	printerr("BOUNCE T22b debounce is per-target: alternating walls bounce freely")
	await _settle(factory)
	var wall_b := _make_wall(Vector2(-200, 0), 8)
	await physics_frame
	var d22c := _bounce_data(Vector2(100, 0), 0.0, 0.0, [4], [4])
	d22c.bounce_cooldown_sec = 0.0
	var v22c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d22c)
	for i in 20:
		if i % 2 == 0:
			v22c.set_bullet_transform(0, Transform2D(0.0, Vector2(200, 0)))
		else:
			v22c.set_bullet_transform(0, Transform2D(0.0, Vector2(-200, 0)))
		await physics_frame
	_check(v22c.bullet_get_bounce_count(0) >= 16, "new target each hit bounces despite debounce (got %d)" % v22c.bullet_get_bounce_count(0))
	wall_b.queue_free()
	await process_frame

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
