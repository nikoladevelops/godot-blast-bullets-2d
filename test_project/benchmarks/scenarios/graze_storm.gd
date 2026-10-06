extends BlastBenchmark
## Graze event storm: a 2,000-bullet ring spawned right on the player every
## 0.25 s, so every bullet enters both rings of the zone on its first tick
## and exits a moment later (8,000 events per ring, one connected factory
## handler). Measures event collection + live dispatch, the graze cost that
## scales with grazes instead of bullets.
var _data: BulletVolleyData2D
var _zone: BulletGrazeZone2D
var _events := 0

func describe() -> String:
	return "graze storm: 2k-bullet rings spawned on the player every 0.25 s"

func setup() -> void:
	var player := Node2D.new()
	player.position = Vector2(960, 540)
	add_child(player)
	player.add_to_group(&"bench_storm_player")
	_zone = BulletGrazeZone2D.new()
	_zone.target_group = &"bench_storm_player"
	_zone.ring_count = 2
	_zone.ring_1_radius = 64.0
	_zone.ring_2_radius = 32.0
	_data = ring_data(2000, Vector2(960, 540), 10.0, 240.0, 1.0)
	factory.bullet_grazed.connect(_on_graze)
	factory.bullet_graze_exited.connect(_on_graze)

func _on_graze(_t: Node2D, _v: BulletVolley2D, _i: int, _z: BulletGrazeZone2D, _r: int) -> void:
	_events += 1

func step(frame: int) -> void:
	if frame % 15 == 0:
		var v: BulletVolley2D = factory.spawn_volley(_data)
		v.graze_set_zones([_zone])
	extra["events"] = _events
