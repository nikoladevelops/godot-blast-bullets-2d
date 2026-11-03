extends Node2D
## Near misses score points. Turret/Spawner has a GrazeZone2D with two rings
## (Graze group, target group "player"): the inner ring is worth more.

var score := 0


func _ready() -> void:
	$BulletFactory2D.bullet_grazed.connect(_on_grazed)
	$Turret/Spawner.area_entered.connect(func(target: Object, _v, _b): if target == $Player: $Player.hit())


func _on_grazed(_target: Node2D, _volley: BulletVolley2D, _bullet: int, _zone: GrazeZone2D, ring_index: int) -> void:
	score += 25 if ring_index == 1 else 5
	$Score.text = "Score: %d" % score
