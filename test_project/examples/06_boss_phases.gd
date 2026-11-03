extends Node2D
## A boss with two phases. The player's Gun fires on its own; every hit on the
## boss costs it 1 HP. Below half health the boss switches its pattern from a
## ring to a fast double spiral, set from code like any inspector value.

var hp := 120
var phase := 1
@onready var attack: BulletSpawner2D = $Boss/Attack


func _ready() -> void:
	$Player/Gun.area_entered.connect(_on_player_shot_hit)
	attack.area_entered.connect(func(target: Object, _v, _b): if target == $Player: $Player.hit())


func _on_player_shot_hit(target: Object, _volley: BulletVolley2D, _bullet: int) -> void:
	if target != $Boss or hp <= 0:
		return
	hp -= 1
	$HP.text = "Boss HP: %d" % hp
	if phase == 1 and hp <= 60:
		phase = 2
		attack.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_MULTISPIRAL
		attack.shoot_interval_sec = 0.08
		attack.spin_speed_deg_per_sec = 140.0
	elif hp == 0:
		attack.shooting_enabled = false
		$Boss.hide()
