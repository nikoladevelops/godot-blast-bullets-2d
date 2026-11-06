extends Node2D
## Bombs that pay out: press B and every bullet that could hit you turns into
## a gem (BulletFactory2D.convert_all_bullets_to_pickups). Gems burst out,
## then a magnet pulls them to your ship; each one collected plays a chime
## and adds its `value` to the counter (the factory's pickup_collected).
## Your own shots are left alone: the conversion takes only bullets that can
## hit the player's layer.

const FX := preload("res://examples/common/example_fx.gd")
const PLAYER_LAYER := 2 # the player's collision layer value: boss bullets mask it

var gems := 0
var gem := BulletPickupData2D.new()
@onready var factory: BulletFactory2D = $BulletFactory2D


func _ready() -> void:
	$Boss.add_to_group(&"enemy")
	gem.texture = preload("res://shared/art/pickups/gem.png")
	gem.texture_size = Vector2(22, 22)
	gem.value = 1
	gem.scatter_speed_min = 80.0 # the burst out before the magnet takes over
	gem.scatter_speed_max = 220.0
	gem.magnet_delay_sec = 0.4
	gem.magnet_acceleration = 2200.0
	gem.magnet_max_speed = 1200.0
	gem.collect_radius = 28.0
	gem.rotation_random_min_degrees = -20.0
	gem.rotation_random_max_degrees = 20.0
	gem.collect_sound = preload("res://shared/effects/gem_collect_sound.tres")
	factory.pickup_collected.connect(_on_gem_collected)
	factory.area_entered.connect(_on_bullet_hit)
	_show_count.call_deferred() # the HUD joins the tree a frame after the player


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo and event.physical_keycode == KEY_B:
		bomb()


## Turns every bullet that can hit the player into a gem flying to the player.
func bomb() -> int:
	var volleys: Array = factory.convert_all_bullets_to_pickups(gem, $Player, PLAYER_LAYER)
	var made := 0
	for v: BulletVolley2D in volleys:
		made += v.get_active_bullet_count()
	if made > 0:
		$Player.hud.announce("+%d GEMS INCOMING" % made, Color(0.4, 1.0, 0.85))
	return made


func _on_gem_collected(_target: Node2D, _at: Vector2, _volley: BulletVolley2D, _bullet: int, value: Variant) -> void:
	gems += int(value)
	_show_count()


func _show_count() -> void:
	$Player.hud.set_counter("Gems: %d" % gems)


func _on_bullet_hit(target: Object, volley: BulletVolley2D, bullet_index: int) -> void:
	if target == $Boss/Hurtbox:
		FX.hit_spark(factory, volley.get_bullet_transform(bullet_index).origin)
		$Boss/Ship.modulate = Color(2.2, 1.4, 1.4)
		create_tween().tween_property($Boss/Ship, "modulate", Color.WHITE, 0.12)
	elif target == $Player:
		$Player.hit()
