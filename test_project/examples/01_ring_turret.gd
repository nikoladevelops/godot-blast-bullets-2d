extends Node2D
## A turret fires spinning rings. Everything about the shot is set in the
## inspector of Turret/Spawner (Patterns: Ring, Spin: on); the script only
## reacts when a bullet hits the player.


func _ready() -> void:
	$Turret/Spawner.area_entered.connect(_on_bullet_hit)


func _on_bullet_hit(target: Object, _volley: BulletVolley2D, _bullet: int) -> void:
	if target == $Player:
		$Player.hit()
