extends BlastBenchmark
## Pool misses fired from a physics callback (where a game's attacks fire):
## every 30 frames 40 brand-new monitorable volleys of 64-73 bullets (a new
## bullet count per wave, so every volley is a pool miss) spawn from a node's
## _physics_process after the factory ticked. A fresh area once joined the
## space with the default layer/mask and its shapes clustered at the origin:
## the next physics step grew with the square of the new bullets (~3 s here).
## Watch frame_ms.max / p99.
func describe() -> String:
	return "40 cold monitorable volleys per wave spawned inside _physics_process"

class Shooter extends Node:
	var target_factory: BulletFactory2D
	var queue: Array = []
	func _physics_process(_delta: float) -> void:
		for d in queue:
			target_factory.spawn_volley(d)
		queue.clear()

var _waves: Array = []
var _shooter: Shooter

func setup() -> void:
	warmup_frames = 0
	measure_frames = 300
	for w in 10:
		var wave: Array = []
		for i in 40:
			var d := ring_data(64 + w, Vector2(200 + 64 * (i % 10), 200 + 64 * (i / 10)), 24.0, 100.0, 0.3)
			d.monitorable = true
			d.collision_layer = 0
			d.collision_mask = 1
			wave.append(d)
		_waves.append(wave)
	_shooter = Shooter.new()
	_shooter.target_factory = factory
	add_child(_shooter)

func step(frame: int) -> void:
	if frame % 30 == 5 and frame / 30 < _waves.size():
		_shooter.queue = _waves[frame / 30]
