extends BlastBenchmark
## The target scene of the plugin: 8 spawners auto-firing at once into ONE
## warm pool (rings, spinning spirals, fans, grids; one homing on a moving
## player, one splitting, two grazing it), 1.5 s lifetimes against walls:
## ~10k bullets alive and volleys pooled and reused every frame (steady
## state: no allocation, objects_growth stays ~0).
var _player: Node2D

func describe() -> String:
	return "8 auto-fire spawners (spin, homing, split, graze) into one warm pool, ~10k live"

func _spawner(src: int, n: int, pos: Vector2, interval: float) -> BulletSpawner2D:
	var sp := BulletSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.position = pos
	add_child(sp)
	sp.set_bullet_factory(factory)
	var d := bench_data(4, 240.0, 1.5)
	d.shared_bullet_speed_data = d.all_bullet_speed_data[0]
	d.all_bullet_speed_data = []
	d.set_collision_mask_from_array([1])
	sp.set_spawn_data(d)
	sp.pattern_source = src
	sp.helper_bullets_amount = n
	sp.shoot_interval_sec = interval
	return sp

func setup() -> void:
	warmup_frames = 180 # two lifetimes: the pool reaches its steady size first
	_player = Node2D.new()
	_player.add_to_group("bench_player")
	add_child(_player)
	make_static_box(Vector2(960, 1060), Vector2(1900, 40))
	make_static_box(Vector2(960, 20), Vector2(1900, 40))
	var spawners: Array = []
	for k in 2:
		spawners.append(_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 96, Vector2(400 + 1100 * k, 300), 0.1))
	for k in 2:
		var sp := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_SPIRAL, 120, Vector2(700 + 500 * k, 500), 0.08)
		sp.spin_enabled = true
		sp.spin_speed_deg_per_sec = 90.0 * (1 - 2 * k)
		spawners.append(sp)
	for k in 2:
		spawners.append(_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_GRID, 64, Vector2(300 + 1300 * k, 800), 0.15))
	var homing := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 48, Vector2(960, 200), 0.25)
	homing.homing_node_group = &"bench_player"
	homing.set_homing_enabled(true)
	spawners.append(homing)
	var splitter := _spawner(BulletSpawner2D.PATTERN_FROM_HELPER_FAN, 40, Vector2(960, 900), 0.2)
	var stage := BulletSplitStage2D.new()
	stage.count = 3
	var chain := BulletSplitData2D.new()
	chain.stages = [stage]
	splitter.split_data = chain
	splitter.split_enabled = true
	spawners.append(splitter)
	var zone := H.make_graze_zone([30.0, 60.0])
	for k in [0, 2]:
		spawners[k].graze_zones = [zone]
		spawners[k].graze_node_group = &"bench_player"
		spawners[k].graze_enabled = true
	factory.bullet_grazed.connect(func(_t, _v, _i, _z, _r) -> void: pass)
	factory.body_entered.connect(func(_b, _v, _i) -> void: pass)
	for sp in spawners:
		sp.body_entered.connect(func(_b, _v, _i) -> void: pass)
		sp.set_shooting_enabled(true)

func step(frame: int) -> void:
	var t := frame / 60.0
	_player.position = Vector2(960, 560) + Vector2(cos(t * 0.8) * 500.0, sin(t * 1.7) * 200.0)
