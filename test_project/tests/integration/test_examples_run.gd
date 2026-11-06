extends BlastTest
## Every scene in res://examples runs as shipped: it loads, fires bullets
## within a few seconds, reports no error or warning and leaks nothing. Each
## runs in its own World2D (the harness keeps a factory of its own in the
## main world). 07_player_gun is also played: holding fire shoots, its one
## factory handler damages the boss with the bullets' custom data, and the
## bomb clears only the bullets that can hurt the player.

const EXAMPLES_DIR := "res://examples"


func _example_scenes() -> Array:
	var out := []
	for file in DirAccess.get_files_at(EXAMPLES_DIR):
		if file.ends_with(".tscn"):
			out.append(EXAMPLES_DIR.path_join(file))
	out.sort()
	return out


func _play(path: String) -> Node:
	var scene: Node = (load(path) as PackedScene).instantiate()
	add_in_own_world(scene)
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
