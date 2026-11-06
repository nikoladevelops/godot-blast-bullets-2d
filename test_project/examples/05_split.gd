extends Node2D
## Big orbs burst into rings of small ones: the Splitting group of
## Turret/Spawner holds a BulletSplitData2D with one stage (count 8, a ring,
## after 0.9 s). The label counts the bursts.

var bursts := 0


func _ready() -> void:
	$Turret/Spawner.bullet_split.connect(_on_split)
	$Turret/Spawner.area_entered.connect(func(target: Object, _v, _b): if target == $Player: $Player.hit())


func _on_split(_at: Vector2, _cause: int, _children: BulletVolley2D, _depth: int) -> void:
	bursts += 1
	$Bursts.text = "Bursts: %d" % bursts
