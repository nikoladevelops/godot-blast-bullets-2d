extends BlastTest
## Integration scene (tests/scenes/graze_scene.tscn), authored like a user
## would: an enemy spawner with a graze zone sub-resource (2 rings, After
## Exit) firing at a player Area2D in the "player" group. Locks
## serialization (every graze property and the regraze id survive load)
## and the end-to-end order in real physics frames: the outer ring, the
## inner ring, then the hit that kills the bullet (no exit for it).

const SCENE := preload("res://tests/scenes/graze_scene.tscn")

var root: Node2D
var enemy: BulletSpawner2D
var player: Area2D


func before_each() -> void:
	await super()
	root = SCENE.instantiate()
	add(root)
	enemy = root.get_node("Enemy")
	player = root.get_node("Player")
	await idle(2)


func test_serialized_graze_properties_load() -> void:
	assert_true(enemy.graze_enabled, "graze on")
	assert_false(enemy.graze_show_preview, "preview toggle")
	assert_eq(enemy.graze_preview_line_width, 3.0, "line width")
	assert_eq(enemy.graze_zones.size(), 1, "one zone")
	var z: BulletGrazeZone2D = enemy.graze_zones[0]
	assert_eq(z.target_group, &"player", "group")
	assert_eq([z.ring_count, z.ring_1_radius, z.ring_2_radius], [2, 48.0, 24.0], "rings")
	assert_eq(z.regraze, BulletGrazeZone2D.REGRAZE_AFTER_EXIT, "regraze id 1 = After Exit")
	assert_true(z.count_bullet_size, "default kept")
	assert_eq(enemy.resolve_graze_targets(0), [player], "the player is the target")


func test_graze_then_hit_in_real_frames() -> void:
	var log: Array = []
	enemy.bullet_grazed.connect(func(t: Node2D, _v: BulletVolley2D, _i: int, _z: BulletGrazeZone2D, r: int) -> void: log.append("graze %d %s" % [r, t.name]))
	enemy.bullet_graze_exited.connect(func(_t: Node2D, _v: BulletVolley2D, _i: int, _z: BulletGrazeZone2D, r: int) -> void: log.append("exit %d" % r))
	enemy.area_entered.connect(func(a: Object, _v: BulletVolley2D, _i: int) -> void: log.append("hit %s" % (a as Node).name))
	assert_true(enemy.shoot_once(), "fired")
	for i in 120:
		await physics()
		if log.has("hit Player"):
			break
	await physics(10)
	assert_eq(log, ["graze 0 Player", "graze 1 Player", "hit Player"], "outer ring, inner ring, then the killing hit: no exit for a dead bullet")
