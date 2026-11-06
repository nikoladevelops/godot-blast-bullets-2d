extends Node2D
## A charging beam that sweeps the arena. The LaserData2D of Emitter/Laser
## sets the phases (charge, active, fade) and the sweep; hits repeat every
## hit_interval_sec while the player stays in the beam.


func _ready() -> void:
	$Emitter/Laser.laser_hit.connect(_on_laser_hit)


func _on_laser_hit(target: Object, _laser: LaserVolley2D, _beam: int, _at: Vector2) -> void:
	if target == $Player:
		$Player.hit()
