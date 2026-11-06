extends Node2D
## A boss with two phases. Hold Space or the left mouse button to fire (1-4
## switch weapons); every hit on the boss costs it 1 HP and sparks. Below half
## health the boss switches its pattern from a ring to a fast double spiral,
## set from code like any inspector value. At 0 it blows up.

const FX := preload("res://examples/common/example_fx.gd")

var hp := 120
var phase := 1
@onready var attack: BulletSpawner2D = $Boss/Attack
@onready var factory: BulletFactory2D = $BulletFactory2D


func _ready() -> void:
	$Boss.add_to_group(&"enemy") # the Homing weapon hunts this group
	$Player/Gun.area_entered.connect(_on_player_shot_hit)
	attack.area_entered.connect(func(target: Object, _v, _b): if target == $Player: $Player.hit())
	_show_hp.call_deferred() # the HUD joins the tree a frame after the player


func _show_hp() -> void:
	$Player.hud.set_health("boss", "BOSS", hp, 120, Color(1.0, 0.3, 0.45))


func _on_player_shot_hit(target: Object, volley: BulletVolley2D, bullet: int) -> void:
	if target != $Boss or hp <= 0:
		return
	hp -= 1
	_show_hp()
	FX.hit_spark(factory, volley.get_bullet_transform(bullet).origin)
	$Boss/Ship.modulate = Color(2.2, 1.4, 1.4)
	create_tween().tween_property($Boss/Ship, "modulate", Color.WHITE, 0.12)
	if phase == 1 and hp <= 60:
		phase = 2
		attack.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_MULTISPIRAL
		attack.shoot_interval_sec = 0.08
		attack.spin_speed_deg_per_sec = 140.0
		FX.explosion(factory, $Boss.global_position, 1.4) # phase change
	elif hp == 0:
		attack.shooting_enabled = false
		factory.clear_bullets_in_circle($Boss.global_position, 4000.0, 2) # its bullets go with it
		FX.boss_death(factory, $Boss.global_position, 70.0)
		$Boss.hide()
		$Boss.set_deferred("monitorable", false)
