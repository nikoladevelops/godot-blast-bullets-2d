extends Node2D
## A small complete fight, the way a game usually wires it:
## - Your Gun fires only while you hold Space or the left mouse button
##   (examples/common/example_player.gd: shooting_enabled follows the input,
##   the gun's own interval sets the rate; 1-4 switch weapons).
## - ONE handler on the factory takes every hit, yours and the boss's: the
##   damage comes from each bullet's custom data (examples/common/bullet_damage.gd).
## - The boss is a BulletSpawner2D that strafes along a Path2D (Movement group);
##   its hurtbox is a child, so it moves with it.
## - B is a bomb: it clears only the bullets that can hit you (can_hit_mask).

const FX := preload("res://examples/common/example_fx.gd")
const PLAYER_LAYER := 2 # the player's collision layer value: boss bullets mask it

var boss_hp := 60
var player_hp := 10
@onready var factory: BulletFactory2D = $BulletFactory2D
@onready var gun: BulletSpawner2D = $Player/Gun
@onready var boss: BulletSpawner2D = $Boss


func _ready() -> void:
	boss.add_to_group(&"enemy") # the Homing weapon hunts this group
	factory.area_entered.connect(_on_bullet_hit)
	_update_labels.call_deferred() # the HUD joins the tree a frame after the player


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_B:
		bomb()


## Clears every bullet that can hurt the player; your own shots keep flying.
func bomb() -> int:
	return factory.clear_bullets_in_circle($Player.global_position, 4000.0, PLAYER_LAYER)


## Every bullet in the scene reports here: neither spawner has a handler of
## its own, so their hits fall through to the factory.
func _on_bullet_hit(target: Object, volley: BulletVolley2D, bullet_index: int) -> void:
	var damage := _bullet_damage(volley, bullet_index)
	if target == $Boss/Hurtbox and boss_hp > 0:
		boss_hp = maxi(boss_hp - damage, 0)
		FX.hit_spark(factory, volley.get_bullet_transform(bullet_index).origin)
		$Boss/Ship.modulate = Color(2.2, 1.4, 1.4)
		create_tween().tween_property($Boss/Ship, "modulate", Color.WHITE, 0.12)
		if boss_hp == 0:
			var at := boss.global_position # where it dies (read before anything moves it)
			boss.shooting_enabled = false
			boss.movement_stop(false) # stop in place: the default resets it to the path's start
			bomb() # its bullets go with it
			FX.boss_death(factory, at, 70.0)
			boss.hide()
			$Boss/Hurtbox.set_deferred("monitorable", false)
	elif target == $Player and player_hp > 0:
		player_hp = maxi(player_hp - damage, 0)
		$Player.hit()
		if player_hp == 0:
			$Player.can_fire = false
			FX.explosion(factory, $Player.global_position, 1.3)
			$Player.hide()
			$Player.set_deferred("monitorable", false)
	_update_labels()


## The bullet's own custom data, else the volley's shared custom data.
func _bullet_damage(volley: BulletVolley2D, bullet_index: int) -> int:
	var data: Resource = volley.bullet_get_custom_data(bullet_index)
	if data == null:
		data = volley.shared_bullets_custom_data
	return data.amount if data != null else 1


func _update_labels() -> void:
	$Player.hud.set_health("boss", "BOSS", boss_hp, 60, Color(1.0, 0.3, 0.45))
	$Player.hud.set_health("player", "YOU", player_hp, 10, Color(0.3, 0.9, 1.0))
