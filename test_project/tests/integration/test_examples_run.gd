extends BlastTest
## Every scene in res://examples runs as shipped: it loads, fires bullets
## within a few seconds, reports no error or warning and leaks nothing. Each
## runs in its own World2D (the harness keeps a factory of its own in the
## main world). 07_player_gun is also played: holding fire shoots, its one
## factory handler damages the boss with the bullets' custom data, and the
## bomb clears only the bullets that can hurt the player.

const EXAMPLES_DIR := "res://examples"

## The example's own viewport: key events reach its _unhandled_input there
## (a SubViewport does not receive the main window's input).
var _viewport: SubViewport


func _example_scenes() -> Array:
	var out := []
	for file in DirAccess.get_files_at(EXAMPLES_DIR):
		if file.ends_with(".tscn"):
			out.append(EXAMPLES_DIR.path_join(file))
	out.sort()
	return out


func _play(path: String) -> Node:
	H.audio_used = true # the examples play hit and explosion sounds: drain headless audio at the end of the file
	var scene: Node = (load(path) as PackedScene).instantiate()
	add_in_own_world(scene)
	_viewport = scene.get_viewport() as SubViewport
	_viewport.size = Vector2i(1152, 648) # the examples' layout (the player clamps to it)
	await idle(2)
	return scene


func test_there_are_examples_to_run() -> void:
	assert_gt(_example_scenes().size(), 6, "the examples folder holds the scenes")


func test_every_example_fires_cleanly(path: String = use_parameters(_example_scenes())) -> void:
	var scene := await _play(path)
	var example_factory: BulletFactory2D = scene.get_node("BulletFactory2D")
	var peak := 0
	for i in 240: # four seconds: every example fires well within that
		await physics(1)
		peak = maxi(peak, example_factory.get_active_bullet_count() + example_factory.get_active_lasers().size())
	assert_gt(peak, 0, "%s puts bullets or lasers on screen" % path)
	scene.queue_free()
	await idle(2)


func test_the_player_gun_example_plays() -> void:
	var scene := await _play("res://examples/07_player_gun.tscn")
	var gun: BulletSpawner2D = scene.get_node("Player/Gun")
	var boss: BulletSpawner2D = scene.get_node("Boss")
	var example_factory: BulletFactory2D = scene.get_node("BulletFactory2D")
	await physics(30)
	assert_eq(gun.get_volleys_fired(), 0, "the gun waits for input")
	var boss_start: Vector2 = boss.global_position
	# Hold fire under the boss's path until it loses health.
	scene.get_node("Player").global_position = Vector2(576, 540)
	Input.action_press("ui_accept")
	for i in 600:
		await physics(1)
		if scene.boss_hp < 60:
			break
	Input.action_release("ui_accept")
	assert_gt(gun.get_volleys_fired(), 0, "holding fire shoots")
	assert_lt(scene.boss_hp, 60, "the factory handler applied the rockets' damage")
	assert_eq((60 - scene.boss_hp) % 2, 0, "each rocket deals its custom data's amount (2)")
	assert_ne(boss.global_position, boss_start, "the boss strafes along its path")
	await physics(10)
	var fired := gun.get_volleys_fired()
	await physics(30)
	assert_eq(gun.get_volleys_fired(), fired, "letting go stops the gun")
	# The bomb clears the boss's bullets (they can hit the player) and nothing else.
	for i in 120:
		await physics(1)
		if example_factory.get_active_bullet_count() > 0:
			break
	assert_gt(example_factory.get_active_bullet_count(), 0, "boss bullets on screen")
	assert_gt(scene.bomb(), 0, "the bomb cleared bullets")
	assert_eq(example_factory.get_nearest_bullet(Vector2.ZERO, 1.0e6, scene.PLAYER_LAYER), {}, "nothing left that can hit the player")
	scene.queue_free()
	await idle(2)


func _key(code: Key, pressed: bool) -> void:
	var e := InputEventKey.new()
	e.physical_keycode = code
	e.keycode = code
	e.pressed = pressed
	Input.parse_input_event(e) # polled keys (movement, fire)
	if _viewport != null:
		_viewport.push_input(e) # event keys (weapons)


func test_the_player_moves_with_wasd_switches_weapons_and_fires() -> void:
	var scene := await _play("res://examples/01_ring_turret.tscn")
	var player: Area2D = scene.get_node("Player")
	var gun: BulletSpawner2D = player.get_node("Gun") # built by the player script
	var start := player.position
	_key(KEY_D, true)
	await physics(20)
	_key(KEY_D, false)
	assert_gt(player.position.x, start.x + 50.0, "D moves right")
	_key(KEY_W, true)
	await physics(10)
	_key(KEY_W, false)
	assert_lt(player.position.y, start.y - 20.0, "W moves up")
	for slot in 4:
		_key([KEY_1, KEY_2, KEY_3, KEY_4][slot], true)
		await idle(1)
		_key([KEY_1, KEY_2, KEY_3, KEY_4][slot], false)
		assert_eq(player.weapon, slot, "key %d selects weapon %d" % [slot + 1, slot + 1])
	assert_eq(gun.pattern_source, BulletSpawner2D.PATTERN_FROM_HELPER_RING, "4 = Nova, a ring")
	_key(KEY_2, true)
	await idle(1)
	_key(KEY_2, false)
	assert_eq(gun.helper_bullets_amount, 7, "2 = Spread, seven bullets")
	assert_eq(gun.get_volleys_fired(), 0, "nothing fires without the button")
	_key(KEY_SPACE, true)
	await physics(30)
	_key(KEY_SPACE, false)
	assert_gt(gun.get_volleys_fired(), 0, "Space fires")
	scene.queue_free()
	await idle(2)


## The barrel art points along +x (measured from turret_gun.png): its global
## +x axis must point at the player, and so must the shots the spawner fires.
func test_the_turret_faces_the_player_and_sparks_when_hit() -> void:
	var scene := await _play("res://examples/02_homing.tscn")
	var turret: Node2D = scene.get_node("Turret")
	var spawner: BulletSpawner2D = turret.get_node("Spawner")
	var player: Area2D = scene.get_node("Player")
	var example_factory: BulletFactory2D = scene.get_node("BulletFactory2D")
	for spot in [Vector2(200, 500), Vector2(950, 500), Vector2(576, 600), Vector2(1000, 160)]:
		player.global_position = spot
		await physics(90) # the turn rate needs under a second
		var to_player: Vector2 = (player.global_position - turret.global_position).normalized()
		var barrel_forward: Vector2 = (turret.get_node("EnemyTurret") as Node2D).global_transform.x.normalized()
		assert_gt(barrel_forward.dot(to_player), 0.995, "the barrel points at the player at %s" % spot)
		var shots := Vector2.ZERO
		for t: Transform2D in spawner.collect_spawn_transforms():
			shots += t.x.normalized()
		assert_gt(shots.normalized().dot(to_player), 0.995, "the fan is centred on the player at %s" % spot)
	# Fire straight up into the turret: the hurtbox sparks.
	player.global_position = Vector2(turret.global_position.x, 500)
	_key(KEY_1, true)
	await idle(1)
	_key(KEY_1, false)
	_key(KEY_SPACE, true)
	var sparked := false
	for i in 120:
		await physics(1)
		if example_factory.get_active_effect_count() > 0:
			sparked = true
			break
	_key(KEY_SPACE, false)
	assert_true(sparked, "a shot on the turret sparks")
	scene.queue_free()
	await idle(2)


func test_the_strafing_boss_explodes_where_it_dies() -> void:
	var scene := await _play("res://examples/07_player_gun.tscn")
	var example_factory: BulletFactory2D = scene.get_node("BulletFactory2D")
	var boss: BulletSpawner2D = scene.get_node("Boss")
	await physics(120) # let it strafe away from the path's start
	var start: Vector2 = boss.get_parent().get_node("StrafePath").curve.get_point_position(0)
	example_factory.debug_set_effect_log_enabled(true)
	scene.boss_hp = 1
	var died_at := Vector2.INF
	scene.get_node("Player").global_position = Vector2(boss.global_position.x, 540)
	_key(KEY_SPACE, true)
	for i in 300:
		scene.get_node("Player").global_position.x = boss.global_position.x # follow it
		await physics(1)
		if scene.boss_hp == 0:
			died_at = boss.global_position
			break
	_key(KEY_SPACE, false)
	assert_eq(scene.boss_hp, 0, "the boss went down")
	assert_gt(died_at.distance_to(start), 50.0, "it died away from the path's start")
	# The chain plays over ~1 s: collect every live explosion's pose meanwhile.
	var seen := {}
	for i in 80:
		await physics(1)
		for bake: Dictionary in example_factory.debug_get_effect_state()["manual"]:
			for slot: Dictionary in bake["live_slots"]:
				var pose: Transform2D = slot["fixed"]
				seen["%s:%s" % [bake["layer"], slot["slot"]] + str(pose.origin)] = pose
	var near := 0
	var biggest := 0.0
	for pose: Transform2D in seen.values():
		assert_lt(pose.origin.distance_to(died_at), 160.0, "every blast goes off where the boss died, not at %s" % pose.origin)
		near += 1
		biggest = maxf(biggest, pose.get_scale().x)
	assert_gt(near, 6, "a chain of blasts")
	assert_gt(biggest, 1.5, "ending in a big one (the 2.2x layer copy)")
	scene.queue_free()
	await idle(2)


func test_the_hud_shows_the_weapons_and_health() -> void:
	var scene := await _play("res://examples/07_player_gun.tscn")
	await idle(2)
	var hud: CanvasLayer = scene.get_node("Player").hud
	assert_true(hud.is_inside_tree(), "the HUD is in the scene")
	assert_eq(hud._slots.size(), 4, "one slot per weapon")
	for slot in 4:
		_key([KEY_1, KEY_2, KEY_3, KEY_4][slot], true)
		await idle(1)
		_key([KEY_1, KEY_2, KEY_3, KEY_4][slot], false)
		for i in 4:
			assert_eq((hud._slots[i].get_child(0).get_child(0)).active, i == slot, "slot %d lit only when selected (picked %d)" % [i, slot])
		assert_eq(hud._banner.text, (["RAPID", "SPREAD", "HOMING", "NOVA"] as Array)[slot], "the banner names the pick")
	await physics(20)
	assert_almost_eq(hud.health_value("boss"), 60.0, 0.01, "the boss bar starts full")
	assert_almost_eq(hud.health_value("player"), 10.0, 0.01, "the player bar too")
	scene.queue_free()
	await idle(2)


func test_the_boss_blows_up() -> void:
	var scene := await _play("res://examples/06_boss_phases.tscn")
	var example_factory: BulletFactory2D = scene.get_node("BulletFactory2D")
	scene.hp = 2
	scene.get_node("Player").global_position = Vector2(scene.get_node("Boss").global_position.x, 540)
	_key(KEY_SPACE, true)
	for i in 300:
		await physics(1)
		if scene.hp == 0:
			break
	_key(KEY_SPACE, false)
	assert_eq(scene.hp, 0, "the boss went down")
	assert_false(scene.get_node("Boss").visible, "and is gone")
	await physics(10)
	assert_gt(example_factory.get_active_effect_count(), 0, "explosions play")
	assert_false(scene.attack.shooting_enabled, "it stopped shooting")
	await physics(90) # let the chain of blasts finish
	scene.queue_free()
	await idle(2)


## The homing example's right-click redirect survives the spawner's own
## retarget passes (every 0.5 s): the source itself switches to Mouse.
func test_the_homing_redirect_holds() -> void:
	var scene := await _play("res://examples/02_homing.tscn")
	var spawner: BulletSpawner2D = scene.get_node("Turret/Spawner")
	await physics(120) # bullets in flight
	scene.set_chase_mouse(true)
	await physics(90) # longer than homing_retarget_interval_sec (0.5 s)
	assert_eq(spawner.homing_target_source, BulletSpawner2D.HOMING_SOURCE_MOUSE, "still hunting the mouse after several retarget passes")
	scene.set_chase_mouse(false)
	await physics(10)
	assert_eq(spawner.homing_target_source, BulletSpawner2D.HOMING_SOURCE_NODE_GROUP, "back to the player")
	expect_no_errors()
	scene.queue_free()
	await idle(2)
