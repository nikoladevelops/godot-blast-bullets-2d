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
# T36 mid-drain mutation rig: when armed, the first bounce signal flips the
# target velocity, so the second record in the same drain must still use its
# queue-time snapshot. Disarmed by default: zero impact on other phases.
var _t36_mutate := false
var _t36_done := false
var _t36_body: CharacterBody2D = null

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

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
	# Every bounce knob lives in the Bounce group (all 12, both classes):
	# a stray bounce prop in Movement Speed would confuse setup order.
	var bounce_props := ["bounce_mask", "bounce_strength", "bounce_push_assist", "bounce_charge_amplify", "bounce_hit_consumed", "bounce_max_count", "bounce_mode", "bounce_rotate_texture", "bounce_rotation_smooth", "bounce_randomness_deg", "bounce_cooldown_sec", "bounce_debounce_sec"]
	var bounce_homed := true
	for bname in bounce_props:
		if str(data_group_of.get(bname, "")) != "Bounce and Ricochet":
			bounce_homed = false
	_check(bounce_homed, "all 12 bounce props grouped with Bounce")
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
	_check(base_groups.slice(0, 7) == ["Bullets", "Appearance", "Collision", "Attachments", "Sprite Effects", "Per-Bullet Rotation", "Rendering and Material"], "base group order matches workflow")
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
	# Live mirrors keep the same Bounce home as the data (all 12).
	var vbounced := true
	for bname in ["bounce_mask", "bounce_strength", "bounce_push_assist", "bounce_charge_amplify", "bounce_hit_consumed", "bounce_max_count", "bounce_mode", "bounce_rotate_texture", "bounce_rotation_smooth", "bounce_randomness_deg", "bounce_cooldown_sec", "bounce_debounce_sec"]:
		if str(vgroups.get(bname, "")) != "Bounce and Ricochet":
			vbounced = false
	_check(vbounced, "all 12 live bounce props grouped with Bounce")
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

	printerr("BOUNCE T23 moving targets: push surges, head-on amplifies, separating swallows")
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
	get_root().add_child(pusher)
	await physics_frame
	var d23 := _bounce_data(Vector2.ZERO, 0.0, 100.0, [4], [4])
	var v23: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d23)
	for i in 150:
		await physics_frame
		if v23.bullet_get_bounce_count(0) >= 1:
			break
	_check(v23.bullet_get_bounce_count(0) >= 1, "pusher wall bounced the bullet")
	var push_dir: Vector2 = v23.get_bullet_direction(0)
	var push_spd: float = v23.get_bullet_velocity(0).length()
	_check(push_dir.x > 0.5, "pushed bullet keeps flying forward, never reverses (dir %s)" % str(push_dir))
	_check(absf(push_spd - 900.0) < 80.0, "pusher surge reflects relative velocity (~900, got %.0f)" % push_spd)
	pusher.queue_free()
	await process_frame
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
	get_root().add_child(charger)
	await physics_frame
	var d23b := _bounce_data(Vector2.ZERO, 0.0, 100.0, [4], [4])
	var v23b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d23b)
	for i in 150:
		await physics_frame
		if v23b.bullet_get_bounce_count(0) >= 1:
			break
	var head_spd: float = v23b.get_bullet_velocity(0).length()
	_check(v23b.get_bullet_direction(0).x < -0.5, "head-on wall reflects backwards")
	_check(absf(head_spd - 900.0) < 80.0, "head-on impact amplifies (~900, got %.0f)" % head_spd)
	charger.queue_free()
	await process_frame
	printerr("BOUNCE T23b separating repeats never re-bounce")
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
		await physics_frame
	var repeat_count: int = v23c.bullet_get_bounce_count(0)
	_check(repeat_count >= 1, "first separating contact still bounces")
	_check(repeat_count <= 2, "separating repeats never re-bounce (got %d)" % repeat_count)
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
		await physics_frame
	_check(v23d.bullet_get_bounce_count(0) <= 2, "consumed repeats never re-bounce (got %d)" % v23d.bullet_get_bounce_count(0))
	_check(v23d.get_bullet_collision_count(0) >= 5, "consumed repeats still count through the normal path")
	_check(v23d.is_bullet_status_enabled(0), "max 0 never kills")

	printerr("BOUNCE T24 push/charge knobs: each side opts out separately")
	await _settle(factory)
	var d24 := _bounce_data(Vector2.ZERO, 0.0, 100.0, [4], [4])
	_check(d24.bounce_push_assist == true, "push assist defaults true")
	_check(d24.bounce_charge_amplify == true, "charge amplify defaults true")
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
	get_root().add_child(arcade)
	await physics_frame
	var v24: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d24)
	_check(v24.get_bounce_push_assist() == false, "live mirror reseeds push off")
	_check(v24.get_bounce_charge_amplify() == true, "live mirror reseeds charge on")
	for i in 150:
		await physics_frame
		if v24.bullet_get_bounce_count(0) >= 1:
			break
	_check(v24.bullet_get_bounce_count(0) >= 1, "unassisted bounce still fires")
	_check(v24.get_bullet_direction(0).x < -0.5, "push assist off reverses (no surge)")
	var arcade_spd: float = v24.get_bullet_velocity(0).length()
	_check(absf(arcade_spd - 100.0) < 30.0, "push assist off keeps plain speed (~100, got %.0f)" % arcade_spd)
	arcade.queue_free()
	await process_frame
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
	get_root().add_child(charger2)
	await physics_frame
	var v24b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d24b)
	_check(v24b.get_bounce_charge_amplify() == false, "live mirror reseeds charge off")
	for i in 150:
		await physics_frame
		if v24b.bullet_get_bounce_count(0) >= 1:
			break
	_check(v24b.get_bullet_direction(0).x < -0.5, "charge amplify off still reflects")
	var plain_spd: float = v24b.get_bullet_velocity(0).length()
	_check(absf(plain_spd - 100.0) < 30.0, "charge amplify off keeps plain speed (~100, got %.0f)" % plain_spd)
	charger2.queue_free()
	await process_frame
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
	get_root().add_child(arcade2)
	await physics_frame
	var v24c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d24c)
	for i in 150:
		await physics_frame
		if v24c.bullet_get_bounce_count(0) >= 1:
			break
	_check(v24c.get_bullet_direction(0).x < -0.5, "both off reverses like legacy")
	_check(v24c.get_bullet_velocity(0).length() < 200.0, "both off keeps plain speed")
	arcade2.queue_free()
	await process_frame

	printerr("BOUNCE T25 velocity sources: CharacterBody reads, Area2D stays static")
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
	get_root().add_child(charlie)
	await physics_frame
	var d25 := _bounce_data(Vector2.ZERO, 0.0, 100.0, [4], [4])
	var v25: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d25)
	v25.set_bullet_transform(0, Transform2D(0.0, Vector2(200, 300)))
	for i in 30:
		await physics_frame
		if v25.bullet_get_bounce_count(0) >= 1:
			break
	_check(v25.bullet_get_bounce_count(0) >= 1, "CharacterBody velocity read without motion")
	_check(v25.get_bullet_direction(0).x > 0.5, "CharacterBody push surges forward")
	var char_spd: float = v25.get_bullet_velocity(0).length()
	_check(absf(char_spd - 900.0) < 80.0, "CharacterBody surge magnitude (~900, got %.0f)" % char_spd)
	charlie.queue_free()
	await process_frame
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
	get_root().add_child(eye25)
	await physics_frame
	var d25b := _bounce_data(Vector2.ZERO, 0.0, 100.0, [4], [4])
	var v25b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d25b)
	v25b.set_bullet_transform(0, Transform2D(0.0, Vector2(200, 300)))
	for i in 30:
		await physics_frame
		if v25b.bullet_get_bounce_count(0) >= 1:
			break
	_check(v25b.bullet_get_bounce_count(0) >= 1, "areas bounce too")
	_check(v25b.get_bullet_direction(0).x < -0.5, "static area reverses")
	eye25.queue_free()
	await process_frame
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
	get_root().add_child(runner)
	await physics_frame
	var d25c := _bounce_data(Vector2(100, 300), 0.0, 100.0, [4], [4])
	var v25c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d25c)
	for i in 150:
		runner.position.x += 500.0 / 60.0
		await physics_frame
		if v25c.bullet_get_bounce_count(0) >= 1:
			break
	_check(v25c.bullet_get_bounce_count(0) >= 1, "moved area still bounces")
	_check(v25c.get_bullet_direction(0).x < -0.5, "moved area counts as static (documented limit, no surge)")
	_check(v25c.get_bullet_velocity(0).length() < 250.0, "moved area adds no boost")
	runner.queue_free()
	await process_frame
	printerr("BOUNCE T25b strength 0 vs pusher sticks, precise moves too")
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
	get_root().add_child(pusher0)
	await physics_frame
	var d25d := _bounce_data(Vector2.ZERO, 0.0, 100.0, [4], [4])
	d25d.bounce_strength = 0.0
	var v25d: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d25d)
	for i in 150:
		await physics_frame
		if v25d.bullet_get_bounce_count(0) >= 1:
			break
	_check(v25d.get_bullet_direction(0).x > 0.5, "dead-stop vs pusher rides the wall")
	var stick_spd: float = v25d.get_bullet_velocity(0).length()
	_check(absf(stick_spd - 500.0) < 40.0, "dead-stop adopts wall speed (~500, got %.0f)" % stick_spd)
	pusher0.queue_free()
	await process_frame
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
	get_root().add_child(pusher1)
	await physics_frame
	var d25e := _bounce_data(Vector2.ZERO, 0.0, 100.0, [4], [4])
	d25e.bounce_mode = 1
	var v25e: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d25e)
	for i in 150:
		await physics_frame
		if v25e.bullet_get_bounce_count(0) >= 1:
			break
	_check(v25e.get_bullet_direction(0).x > 0.5, "precise mode surges too")
	var precise_spd: float = v25e.get_bullet_velocity(0).length()
	_check(absf(precise_spd - 900.0) < 80.0, "precise surge magnitude (~900, got %.0f)" % precise_spd)
	pusher1.queue_free()
	await process_frame
	printerr("BOUNCE T25c knob independence + pool ghost-boost")
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
	get_root().add_child(charger3)
	await physics_frame
	var v25f: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d25f)
	for i in 150:
		await physics_frame
		if v25f.bullet_get_bounce_count(0) >= 1:
			break
	var cross_spd: float = v25f.get_bullet_velocity(0).length()
	_check(absf(cross_spd - 900.0) < 80.0, "charge works while push is off (~900, got %.0f)" % cross_spd)
	charger3.queue_free()
	await process_frame
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
	get_root().add_child(pusher2)
	await physics_frame
	var v25g: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d25g)
	for i in 150:
		await physics_frame
		if v25g.bullet_get_bounce_count(0) >= 1:
			break
	var surge_spd: float = v25g.get_bullet_velocity(0).length()
	_check(v25g.get_bullet_direction(0).x > 0.5, "push works while charge is off")
	_check(absf(surge_spd - 900.0) < 80.0, "push surge intact (~900, got %.0f)" % surge_spd)
	pusher2.queue_free()
	await process_frame
	await _settle(factory)
	var d25h := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d25h.bounce_strength = 2.0
	d25h.max_life_time = 2.0
	var v25h: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d25h)
	for i in 120:
		await physics_frame
		if v25h.bullet_get_bounce_count(0) >= 1:
			break
	_check(absf(v25h.get_bullet_velocity(0).length() - 600.0) < 80.0, "strength 2 doubles before pooling")
	for i in 200:
		await physics_frame
		if not v25h.is_bullet_status_enabled(0):
			break
	_check(not v25h.is_bullet_status_enabled(0), "expiry pooled the volley")
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
		await physics_frame
	var wake_spd: float = v25h.get_bullet_velocity(0).length()
	_check(absf(wake_spd - 300.0) < 80.0, "wake has no ghost boost from the dead life (got %.0f)" % wake_spd)

	printerr("BOUNCE T26 reset semantics, multi-bullet, gravity arc, area routing")
	await _settle(factory)
	var d26 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	var v26: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d26)
	for i in 120:
		await physics_frame
		if v26.bullet_get_bounce_count(0) >= 1:
			break
	_check(v26.bullet_get_bounce_count(0) >= 1, "baseline bounce counted")
	v26.disable_bullet(0)
	_check(v26.bullet_get_bounce_count(0) == 0, "disable zeroes the bounce count")
	v26.enable_bullet(0)
	_check(v26.bullet_get_bounce_count(0) == 0, "wake restarts the ledger")
	_check(v26.is_bullet_status_enabled(0), "wake revives the bullet")
	await _settle(factory)
	var dd26 := DirectionalBulletsData2D.new()
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
		await physics_frame
		if v26b.bullet_get_bounce_count(0) >= 1 and v26b.bullet_get_bounce_count(1) >= 1:
			break
	_check(v26b.bullet_get_bounce_count(0) == 1 and v26b.bullet_get_bounce_count(1) == 1, "both bullets bounce independently")
	_check(v26b.get_bullet_direction(0).x < -0.5 and v26b.get_bullet_direction(1).x < -0.5, "both bullets reflect")
	await _settle(factory)
	var d26c := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d26c.gravity = Vector2(0, 200.0)
	var v26c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d26c)
	for i in 120:
		await physics_frame
		if v26c.bullet_get_bounce_count(0) >= 1:
			break
	_check(v26c.bullet_get_bounce_count(0) >= 1, "gravity volley bounces")
	_check(v26c.get_bullet_direction(0).x < 1.0 - 0.05, "arcing impact deflects off head-on")
	_check(v26c.get_bullet_velocity(0).y > 20.0, "fall continues downward after the bounce")
	_check(v26c.get_bullet_velocity(0).is_finite(), "post-bounce gravity state finite")
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
	get_root().add_child(eye26)
	await physics_frame
	var d26d := _bounce_data(Vector2.ZERO, 0.0, 100.0, [4], [4])
	var v26d: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d26d)
	v26d.set_bullet_transform(0, Transform2D(0.0, Vector2(200, 300)))
	for i in 60:
		await physics_frame
		if v26d.bullet_get_bounce_count(0) >= 1:
			break
	_check(v26d.bullet_get_bounce_count(0) >= 1, "pure area overlap bounces")
	_check(_bounce_area.size() >= 1, "area bounce routes to the area signal")
	_check(_bounce_body.is_empty(), "area bounce never touches the body signal")
	eye26.queue_free()
	await process_frame
	await _settle(factory)
	var d26e := _bounce_data(Vector2.ZERO, 0.0, 200.0, [4], [4])
	d26e.bounce_push_assist = false
	d26e.bounce_charge_amplify = false
	var v26e: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d26e)
	_check(v26e.get_bounce_push_assist() == false and v26e.get_bounce_charge_amplify() == false, "knobs-off mirrors reseeded")
	await process_frame
	factory.free_active_bullets()
	await process_frame
	await process_frame
	var v26f: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2.ZERO, 0.0, 200.0, [4], [4]))
	_check(v26f.get_bounce_push_assist() == true and v26f.get_bounce_charge_amplify() == true, "respawn reseeds knobs to defaults")

	printerr("BOUNCE T27 curves beat surge, guard ignores knobs, degenerate config")
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
	get_root().add_child(push27)
	await physics_frame
	var d27 := _bounce_data(Vector2(0, 300), 0.0, 100.0, [4], [4])
	var v27: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d27)
	v27.set_shared_bullet_curves_data(flat27)
	for i in 150:
		await physics_frame
		if v27.bullet_get_bounce_count(0) >= 1:
			break
	_check(v27.get_bullet_velocity(0).length() > 600.0, "curved bullet still surges at the drain")
	for i in 3:
		await physics_frame
	_check(absf(v27.get_bullet_velocity(0).length() - 300.0) < 80.0, "curves reclaim the surge next tick")
	push27.queue_free()
	await process_frame
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
		await physics_frame
	_check(v27b.bullet_get_bounce_count(0) <= 2, "repeat guard holds with both knobs off (got %d)" % v27b.bullet_get_bounce_count(0))
	await _settle(factory)
	var wall27b := _make_wall(Vector2(-200, 0), 8)
	await physics_frame
	var d27c := _bounce_data(Vector2(100, 0), 0.0, 0.0, [4], [4])
	d27c.bounce_cooldown_sec = 0.0
	d27c.bounce_debounce_sec = 0.0
	var v27c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d27c)
	for i in 20:
		if i % 2 == 0:
			v27c.set_bullet_transform(0, Transform2D(0.0, Vector2(200, 0)))
		else:
			v27c.set_bullet_transform(0, Transform2D(0.0, Vector2(-200, 0)))
		await physics_frame
	_check(v27c.bullet_get_bounce_count(0) >= 16, "zero guards bounce every detected entry (got %d)" % v27c.bullet_get_bounce_count(0))
	wall27b.queue_free()
	await process_frame

	printerr("BOUNCE T28 hostile: freed walls, NaN motion, overflow, starvation")
	await _settle(factory)
	# T28a: freed wall mid-overlap freezes counts, volley stays finite.
	var d28 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	var v28: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d28)
	for i in 120:
		await physics_frame
		if v28.bullet_get_bounce_count(0) >= 1:
			break
	_check(v28.bullet_get_bounce_count(0) >= 1, "hostile baseline bounced")
	var frozen28: int = v28.bullet_get_bounce_count(0)
	wall.queue_free()
	await process_frame
	await process_frame
	for i in 30:
		await physics_frame
	_check(v28.bullet_get_bounce_count(0) == frozen28, "freed wall freezes bounce counts")
	_check(_finite_volley(v28), "volley finite after wall freed mid-flight")
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling after wall freed mid-flight")
	wall = _make_wall(Vector2(200, 0), 8)
	await physics_frame
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
	get_root().add_child(nan28)
	await physics_frame
	var d28b := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	var v28b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d28b)
	for i in 120:
		await physics_frame
		if v28b.bullet_get_bounce_count(0) >= 1:
			break
	_check(v28b.bullet_get_bounce_count(0) >= 1, "NaN-velocity wall still bounces (static fallback)")
	_check(v28b.get_bullet_velocity(0).is_finite(), "velocity finite after NaN-velocity bounce")
	nan28.queue_free()
	await process_frame
	# T28c: absurd-finite strength refuses the bounce instead of poisoning.
	await _settle(factory)
	var d28c := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d28c.bounce_strength = 1e30
	var v28c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d28c)
	for i in 10:
		await physics_frame
	_check(_finite_volley(v28c), "1e30 strength never poisons the volley")
	_check(v28c.bullet_get_bounce_count(0) <= 1, "overflow bounce refused, not counted")
	# T28d: empty transforms with bounce armed refuse or spawn empty.
	await _settle(factory)
	var d28d := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d28d.transforms = []
	var v28d: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d28d)
	_check(v28d == null or v28d.get_amount_bullets() == 0, "empty transforms refused-or-empty with bounce armed")
	# T28e: subnormal strength is a finite dead-stop, heading preserved.
	await _settle(factory)
	var d28e := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d28e.bounce_strength = 1e-30
	var v28e: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d28e)
	for i in 120:
		await physics_frame
		if v28e.bullet_get_bounce_count(0) >= 1:
			break
	_check(v28e.bullet_get_bounce_count(0) >= 1, "subnormal strength still bounces")
	_check(v28e.get_bullet_velocity(0).is_finite(), "subnormal bounce stays finite")
	_check(v28e.get_bullet_velocity(0).length() < 0.01, "subnormal strength dead-stops")
	# T28f: spawner shoot_once rejects null data and bad factory paths.
	# NOTE: set_bullet_factory(null) then shoot_once() would exercise the
	# no-factory path, but assigning the spawner under a live factory via
	# set_bullet_factory hits path-resolution edge cases headless; the
	# null-data path below plus the spawner suites' factory-missing
	# coverage (test_spawner_tree T: bare spawner, failed shot uncounted)
	# pin the fail-early contract without flaking the runner.
	await _settle(factory)
	var spawner28 := BulletSpawner2D.new()
	get_root().add_child(spawner28)
	await process_frame
	spawner28.set_bullet_factory(factory)
	spawner28.set_spawn_data(null)
	var fired28: int = spawner28.get_volleys_fired()
	_check(spawner28.shoot_once() == false, "shoot_once with null data returns false")
	_check(spawner28.get_volleys_fired() == fired28, "failed shot not counted")
	spawner28.queue_free()
	await process_frame
	# No-factory path: a spawner with data but no factory refuses cleanly.
	var lonely28 := BulletSpawner2D.new()
	get_root().add_child(lonely28)
	await process_frame
	lonely28.set_spawn_data(_bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4]))
	var lonely_fired: int = lonely28.get_volleys_fired()
	_check(lonely28.shoot_once() == false, "shoot_once with no factory returns false")
	_check(lonely28.get_volleys_fired() == lonely_fired, "factory-less shot not counted")
	lonely28.queue_free()
	await process_frame
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
	get_root().add_child(push28)
	await physics_frame
	var d28g := _bounce_data(Vector2(0, 300), 0.0, 100.0, [4], [4])
	d28g.transforms = [Transform2D(0.0, Vector2(0, 300)), Transform2D(0.0, Vector2(0, 340))]
	var sg28 := BulletSpeedData2D.new()
	sg28.speed = 100.0
	sg28.max_speed = 3000.0
	sg28.acceleration = 0.0
	d28g.all_bullet_speed_data = [sg28, sg28]
	var v28g: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d28g)
	for i in 150:
		await physics_frame
		if v28g.bullet_get_bounce_count(0) >= 1 and v28g.bullet_get_bounce_count(1) >= 1:
			break
	_check(v28g.bullet_get_bounce_count(0) >= 1 and v28g.bullet_get_bounce_count(1) >= 1, "both bullets bounce off the shared pusher")
	_check(_finite_volley(v28g), "both bullets finite after shared-pusher bounce")
	push28.queue_free()
	await process_frame
	# T28h: StayLocked orbit wins over the pusher (zero bounces). The whole
	# rig lives on the y=300 lane: the suite wall at (200, 0) would
	# otherwise intercept the flight, and a RigidBody pusher would crash
	# into it physically. Spawn sits past the wall with a clean run to the
	# ring; the pusher is teleported onto the locked bullet for guaranteed
	# overlap (its velocity still feeds the bounce math, which orbit skips).
	await _settle(factory)
	var orbit28 := Node2D.new()
	orbit28.position = Vector2(400, 300)
	get_root().add_child(orbit28)
	await process_frame
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
	get_root().add_child(lock28)
	await physics_frame
	var d28h := _bounce_data(Vector2(250, 300), 0.0, 300.0, [4], [4])
	d28h.set_bullet_max_collision_count(0)
	var v28h: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d28h)
	# OrbitRight=2, FaceTarget=0, FollowTarget=0, StayLocked=1.
	v28h.bullet_homing_push_back_homing_target(0, orbit28)
	v28h.bullet_enable_orbiting(0, 60.0, 2, 0, 0, 0.0, 1, true)
	for i in 150:
		await physics_frame
		if v28h.bullet_is_orbiting_locked(0):
			break
	_check(v28h.bullet_is_orbiting_locked(0), "orbiter locked before the pusher arrives")
	_clear_signals()
	for k in 3:
		lock28.position = v28h.get_bullet_transform(0).origin
		for i in 10:
			await physics_frame
	_check(v28h.bullet_get_bounce_count(0) == 0, "StayLocked orbit takes zero bounces")
	_check(v28h.get_bullet_collision_count(0) >= 1, "locked hits still count normally")
	_check(v28h.bullet_is_orbiting_locked(0), "StayLocked ring survives the pusher")
	orbit28.queue_free()
	lock28.queue_free()
	await process_frame

	printerr("BOUNCE T29 precise-mode degenerate shapes fall back to radial")
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
	get_root().add_child(zero29)
	await physics_frame
	var d29 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d29.bounce_mode = 1
	var v29: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d29)
	for i in 120:
		await physics_frame
		if v29.bullet_get_bounce_count(0) >= 1:
			break
	_check(v29.bullet_get_bounce_count(0) >= 1, "zero-size rect falls back to radial and bounces")
	_check(_finite_volley(v29), "volley finite after degenerate-shape bounce")
	zero29.queue_free()
	await process_frame
	await _settle(factory)
	var null29 := StaticBody2D.new()
	null29.position = Vector2(200, 0)
	null29.collision_layer = 8
	null29.collision_mask = 2
	var null29col := CollisionShape2D.new()
	null29.add_child(null29col)
	get_root().add_child(null29)
	await physics_frame
	var v29b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d29)
	for i in 120:
		await physics_frame
		if v29b.bullet_get_bounce_count(0) >= 1:
			break
	_check(v29b.bullet_get_bounce_count(0) >= 1, "null-shape wall falls back to radial and bounces")
	_check(_finite_volley(v29b), "volley finite after null-shape bounce")
	null29.queue_free()
	await process_frame

	printerr("BOUNCE T30 consumed bounces spark once and still count")
	await _settle(factory)
	var d30 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d30.bounce_hit_consumed = true
	d30.bounce_max_count = 2
	d30.set_bullet_max_collision_count(2)
	var v30: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d30)
	for i in 120:
		await physics_frame
		if v30.bullet_get_bounce_count(0) >= 1:
			break
	_check(v30.bullet_get_bounce_count(0) == 1, "consumed bounce counted once")
	_check(v30.get_bullet_collision_count(0) == 1, "consumed bounce also consumes one hit")
	_check(_finite_volley(v30), "volley finite after consumed bounce")

	printerr("BOUNCE T31 setter rejects keep old values")
	var d31 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	d31.set_bounce_strength(NAN)
	_check(absf(d31.bounce_strength - 1.0) < 0.0001, "NaN strength rejected")
	d31.set_bounce_strength(-1.0)
	_check(absf(d31.bounce_strength - 1.0) < 0.0001, "negative strength rejected")
	d31.set_bounce_mask(-1)
	_check(d31.bounce_mask == 8, "negative mask rejected")
	d31.set_bounce_max_count(-1)
	_check(d31.bounce_max_count == 0, "negative max count rejected")
	d31.set_bounce_cooldown_sec(5.0)
	_check(absf(d31.bounce_cooldown_sec - 0.05) < 0.0001, "over-range cooldown rejected")
	d31.set_bounce_randomness_deg(999.0)
	_check(absf(d31.bounce_randomness_deg) < 0.0001, "over-range randomness rejected")
	var v31: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4]))
	v31.set_bounce_strength(NAN)
	_check(absf(v31.get_bounce_strength() - 1.0) < 0.0001, "live NaN strength rejected")
	v31.set_bounce_mask(-1)
	_check(v31.get_bounce_mask() == 8, "live negative mask rejected")

	printerr("BOUNCE T32 mover switching, gravity push, unlocked orbit, spawner routing")
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
	get_root().add_child(push32)
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
	get_root().add_child(charge32)
	await physics_frame
	var d32 := _bounce_data(Vector2(0, 300), 0.0, 100.0, [4], [4])
	var v32: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d32)
	for i in 200:
		await physics_frame
		if v32.bullet_get_bounce_count(0) >= 2:
			break
	_check(v32.bullet_get_bounce_count(0) >= 2, "push then charge both bounce")
	_check(v32.get_bullet_direction(0).x < -0.5, "charger wins the second exchange")
	_check(v32.get_bullet_velocity(0).is_finite(), "mover switching stays finite")
	push32.queue_free()
	charge32.queue_free()
	await process_frame
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
	get_root().add_child(push32b)
	await physics_frame
	var v32b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d32b)
	for i in 200:
		await physics_frame
		if v32b.bullet_get_bounce_count(0) >= 1:
			break
	_check(v32b.get_bullet_direction(0).x > 0.0, "gravity push keeps forward motion")
	_check(v32b.get_bullet_velocity(0).length() > 400.0, "gravity push still surges")
	_check(v32b.get_bullet_velocity(0).y > 0.0, "fall continues after a moving bounce")
	push32b.queue_free()
	await process_frame
	await _settle(factory)
	var moon32 := Node2D.new()
	moon32.position = Vector2(600, 300)
	get_root().add_child(moon32)
	var ringwall := _make_wall(Vector2(600, 300), 8)
	await physics_frame
	var d32c := _bounce_data(Vector2(500, 300), 0.0, 200.0, [4], [4])
	d32c.set_bullet_max_collision_count(0)
	var v32c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d32c)
	v32c.shared_homing_deque_push_back_node2d_target(moon32)
	v32c.bullet_enable_orbiting(0, 64.0, 2, 0)
	for i in 150:
		await physics_frame
		if v32c.bullet_get_bounce_count(0) >= 1:
			break
	_check(v32c.bullet_get_bounce_count(0) >= 1, "unlocked orbit never blocks wall contact")
	_check(v32c.get_bullet_transform(0).is_finite(), "unlocked orbit flight finite")
	moon32.queue_free()
	ringwall.queue_free()
	await process_frame
	await _settle(factory)
	var spawner32 := BulletSpawner2D.new()
	spawner32.bullet_factory_path = factory.get_path()
	spawner32.shooting_enabled = false
	spawner32.pattern_source = BulletSpawner2D.PATTERN_FROM_SELF
	spawner32.position = Vector2(0, 300)
	get_root().add_child(spawner32)
	await process_frame
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
	get_root().add_child(push32d)
	await physics_frame
	spawner32.bounce_body_entered.connect(_on_spawner_bounce)
	spawner32.spawn_data = _bounce_data(Vector2(0, 300), 0.0, 100.0, [4], [4])
	_check(spawner32.shoot_once(), "spawner push volley fired")
	for i in 200:
		await physics_frame
		if _spawner_bounce.size() >= 1:
			break
	_check(_spawner_bounce.size() >= 1, "spawner routes the push bounce")
	_check(_factory_bounce.is_empty(), "factory silent on spawner push")
	spawner32.queue_free()
	push32d.queue_free()
	await process_frame

	printerr("BOUNCE T33 unspawned instances never crash")
	await _settle(factory)
	var bare := DirectionalBullets2D.new()
	get_root().add_child(bare)
	await process_frame
	var empty_data := DirectionalBulletsData2D.new()
	empty_data.transforms = []
	_check(bare.enable_multimesh(empty_data, Vector2.ZERO, 0) == false, "enable on never-spawned volley refuses cleanly")
	_check(bare.get_amount_bullets() == 0, "fresh volley holds zero bullets")
	_check(bare.bullet_get_bounce_count(0) == 0, "bounce count OOB reads 0")
	_check(bare.get_bullet_direction(0) == Vector2(), "direction OOB reads zero")
	_check(bare.get_bullet_velocity(0) == Vector2(), "velocity OOB reads zero")
	_check(bare.get_bullet_transform(0) == Transform2D(), "transform OOB reads identity")
	_check(not bare.has_trail_effects(), "no trails without a bake")
	_check(bare.debug_get_effect_layers_info().get("trail_bake_count", -1) == 0, "effect debug empty")
	_check(bare.debug_get_bounce_info(0).get("valid", true) == false, "bounce debug OOB invalid")
	bare.disable_bullet(0)
	bare.enable_bullet(0)
	bare.set_bullet_transform(0, Transform2D.IDENTITY)
	bare.set_bullet_direction(0, Vector2(1, 0))
	bare.set_bullet_speed_data(0, BulletSpeedData2D.new())
	bare.bullet_set_gravity(0, Vector2(0, 1))
	bare.bullet_set_trail_enabled(0, 0, true)
	bare.play_effect_animation(0, "x")
	_check(bare.get_bullet_transform(0) == Transform2D(), "hostile storm leaves identity")
	_check(bare.debug_get_volley_info().get("amount_bullets", -1) == 0, "volley info zeroed")
	bare.queue_free()
	await process_frame
	var bare_block := BlockBullets2D.new()
	get_root().add_child(bare_block)
	await process_frame
	var empty_block := BlockBulletsData2D.new()
	empty_block.transforms = []
	_check(bare_block.enable_multimesh(empty_block, Vector2.ZERO, 0) == false, "block enable on fresh instance refuses cleanly")
	bare_block.queue_free()
	await process_frame

	printerr("BOUNCE T34 teleport matrix + interpolation mixing")
	await _settle(factory)
	factory.set_use_physics_interpolation_runtime(false)
	var d34 := _bounce_data(Vector2.ZERO, 0.0, 300.0, [4], [4])
	var v34: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d34)
	v34.set_bullet_transform(0, Transform2D(0.0, Vector2(100, 0)))
	factory.set_is_factory_processing_bullets(false)
	for i in 10:
		await physics_frame
	_check(v34.bullet_get_bounce_count(0) == 0, "paused teleport never bounces")
	_check(v34.get_bullet_transform(0).origin.distance_to(Vector2(100, 0)) < 0.01, "paused teleport holds still")
	factory.set_is_factory_processing_bullets(true)
	for i in 120:
		await physics_frame
		if v34.bullet_get_bounce_count(0) >= 1:
			break
	_check(v34.bullet_get_bounce_count(0) >= 1, "resume bounces the teleported overlap")
	await _settle(factory)
	factory.set_use_physics_interpolation_runtime(true)
	var hunter34 := Node2D.new()
	hunter34.position = Vector2(400, 0)
	get_root().add_child(hunter34)
	var d34b := _bounce_data(Vector2.ZERO, 0.0, 300.0, [], [4])
	var v34b: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d34b)
	v34b.shared_homing_deque_push_back_node2d_target(hunter34)
	v34b.set_homing_distance_before_reached(64.0)
	var reached34: Array = []
	var on_reached := func(_v: DirectionalBullets2D, _i: int, _t: Object, _p: Vector2) -> void:
		reached34.append(1)
	v34b.bullet_homing_target_reached.connect(on_reached)
	for i in 3:
		await physics_frame
	v34b.set_bullet_transform(0, Transform2D(0.0, Vector2(400, 0)))
	for i in 30:
		await physics_frame
		if reached34.size() >= 1:
			break
	_check(reached34.size() >= 1, "teleport onto homing target fires reached")
	_check(v34b.get_bullet_transform(0).is_finite(), "post-teleport homing flight finite")
	v34b.bullet_homing_target_reached.disconnect(on_reached)
	hunter34.queue_free()
	await process_frame
	await _settle(factory)
	var d34c := _bounce_data(Vector2.ZERO, 0.0, 0.0, [4], [4])
	var v34c: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d34c)
	for i in 5:
		await physics_frame
		v34c.set_bullet_transform(0, Transform2D(0.3 * i, Vector2(20 * i, 0)))
		v34c.debug_run_interpolation_pass()
		_check(v34c.get_bullet_transform(0).is_finite(), "teleport storm stays finite with interp")
	factory.set_use_physics_interpolation_runtime(false)

	printerr("BOUNCE T35 orbit endurance, block census, telegraph-zero, helper hostility")
	await _settle(factory)
	var moon35 := Node2D.new()
	moon35.position = Vector2(600, 300)
	get_root().add_child(moon35)
	var d35 := _bounce_data(Vector2(500, 300), 0.0, 200.0, [], [4], 30.0)
	var v35: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d35)
	v35.shared_homing_deque_push_back_node2d_target(moon35)
	v35.bullet_enable_orbiting(0, 64.0, 2, 0)
	for i in 720:
		await physics_frame
	_check(v35.get_bullet_transform(0).is_finite() and v35.get_bullet_velocity(0).is_finite(), "720-frame orbit never NaNs")
	_check(absf(v35.bullet_get_orbiting_radius(0) - 64.0) < 2.0, "ring radius stable over 720 frames")
	_check(v35.is_bullet_status_enabled(0), "orbiter alive after endurance run")
	moon35.queue_free()
	await process_frame
	await _settle(factory)
	factory.spawn_block_bullets(H.make_block_data(2, 150.0, 10.0))
	await physics_frame
	_check(factory.debug_get_live_volley_ids(0).size() >= 1, "factory census includes block volleys")
	await _settle(factory)
	var tsp := BulletSpawner2D.new()
	get_root().add_child(tsp)
	tsp.set_bullet_factory(factory)
	tsp.set_spawn_data(H.make_directional_data(2, 200.0))
	tsp.set_shooting_enabled(false)
	tsp.set_telegraph_enabled(true)
	tsp.set_telegraph_sec(0.0)
	var fired35: int = tsp.get_volleys_fired()
	_check(tsp.shoot_once(), "telegraph-zero shot accepted")
	for i in 30:
		await physics_frame
		if tsp.get_volleys_fired() > fired35:
			break
	_check(tsp.get_volleys_fired() > fired35, "telegraph-zero fires without hanging")
	tsp.queue_free()
	await process_frame
	await _settle(factory)
	_check(BulletFactory2D.helper_generate_transforms_grid(-1, Transform2D.IDENTITY, 2).is_empty(), "negative grid amount refused")
	_check(BulletFactory2D.helper_generate_transforms_grid(0, Transform2D.IDENTITY, 2).is_empty(), "zero grid amount yields empty")
	_check(BulletFactory2D.helper_generate_transforms_grid(8, Transform2D.IDENTITY, 0).is_empty(), "zero grid rows refused")
	_check(BulletFactory2D.helper_generate_transforms_rain(8, Transform2D.IDENTITY, 600.0, Vector2.ZERO, 48.0, 0.0, 1).is_empty(), "zero rain direction refused")
	_check(BulletFactory2D.helper_generate_transforms_rain(4, Transform2D.IDENTITY).size() == 4, "rain defaults still generate")

	printerr("BOUNCE T36 stale target velocity never steers the bounce")
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
	get_root().add_child(back36)
	await physics_frame
	var d36a := _bounce_data(Vector2(100, 300), 0.0, 200.0, [4], [4])
	var v36a: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(d36a)
	for i in 150:
		await physics_frame
		if v36a.bullet_get_bounce_count(0) >= 1:
			break
	_check(v36a.bullet_get_bounce_count(0) >= 1, "back-off overlap bounces")
	var vel36a: Vector2 = v36a.get_bullet_velocity(0)
	_check(vel36a.is_finite(), "back-off post-bounce finite")
	_check(vel36a.x < 0.0, "back-off sends the bullet back -X, never through (+X)")
	back36.queue_free()
	await process_frame

	printerr("BOUNCE T36b knob matrix: every mix separates, stays finite")
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
			get_root().add_child(mover)
			await physics_frame
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
				await physics_frame
				if vv.bullet_get_bounce_count(0) >= 1:
					break
			var tag: String = str(s[0]) + " vs " + str(mix_names[vi])
			_check(vv.bullet_get_bounce_count(0) >= 1, tag + " bounces")
			var vel: Vector2 = vv.get_bullet_velocity(0)
			_check(vel.is_finite() and vv.get_bullet_transform(0).is_finite(), tag + " stays finite")
			_check(vel.x <= 1.0, tag + " never moves forward (+X)")
			if cfg.get("bconsumed", false):
				_check(vv.get_bullet_collision_count(0) == 1, tag + " consumes exactly one hit")
			mover.queue_free()
			await process_frame

	printerr("BOUNCE T36c queue-time snapshot wins over mid-drain mutation")
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
	get_root().add_child(mut36)
	await physics_frame
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
		await physics_frame
		if v36c.bullet_get_bounce_count(0) >= 1 and v36c.bullet_get_bounce_count(1) >= 1:
			break
	_t36_mutate = false
	_check(_t36_done, "mutation handler ran mid-drain")
	_check(v36c.bullet_get_bounce_count(0) >= 1 and v36c.bullet_get_bounce_count(1) >= 1, "both records bounce")
	for bi in [0, 1]:
		var bv: Vector2 = v36c.get_bullet_velocity(bi)
		_check(bv.is_finite(), "mutated-drain bullet " + str(bi) + " finite")
		_check(bv.x < 0.0, "mutated-drain bullet " + str(bi) + " heads -X")
		_check(bv.length() > 500.0, "mutated-drain bullet " + str(bi) + " keeps queue-time charge energy")
	mut36.queue_free()
	_t36_body = null
	await process_frame

	factory.directional_bounce_body_entered.disconnect(_on_bounce_body)
	factory.directional_bounce_area_entered.disconnect(_on_bounce_area)
	factory.directional_body_entered.disconnect(_on_norm_body)
	await process_frame
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
