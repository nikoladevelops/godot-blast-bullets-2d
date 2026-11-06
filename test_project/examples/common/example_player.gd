extends Area2D
## The player of every example.
## Move: WASD or the arrow keys. Fire: hold Space or the left mouse button.
## Weapons: 1 Rapid, 2 Spread, 3 Homing (hunts the "enemy" group), 4 Nova.
## A hit flashes the ship. The gun is the scene's Player/Gun spawner (its
## spawn data is the base of every weapon: art, damage, collision), or one
## built here when the scene has none.

@export var speed := 320.0
## Off while the player is dead (examples/07 turns it off).
var can_fire := true

const WEAPON_NAMES := ["Rapid", "Spread", "Homing", "Nova"]
var weapon := 0
var gun: BulletSpawner2D
var _base_data: BulletVolleyData2D
## The weapon bar and health bars (examples/common/example_hud.gd).
var hud: CanvasLayer


func _ready() -> void:
	gun = get_node_or_null("Gun") as BulletSpawner2D
	if gun == null:
		gun = _build_gun()
	gun.shooting_enabled = false # fires only while the button is held
	_base_data = gun.spawn_data
	hud = preload("res://examples/common/example_hud.gd").new()
	hud.build(WEAPON_NAMES)
	get_parent().add_child.call_deferred(hud)
	select_weapon(0, false)


func _physics_process(delta: float) -> void:
	var move := Input.get_vector("ui_left", "ui_right", "ui_up", "ui_down")
	move += Vector2(
			float(Input.is_physical_key_pressed(KEY_D)) - float(Input.is_physical_key_pressed(KEY_A)),
			float(Input.is_physical_key_pressed(KEY_S)) - float(Input.is_physical_key_pressed(KEY_W)))
	position += move.limit_length(1.0) * speed * delta
	position = position.clamp(Vector2(24, 24), get_viewport_rect().size - Vector2(24, 24))
	gun.shooting_enabled = can_fire and (Input.is_physical_key_pressed(KEY_SPACE) or Input.is_action_pressed("ui_accept") or Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT))


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		var slot: int = [KEY_1, KEY_2, KEY_3, KEY_4].find(event.physical_keycode)
		if slot >= 0:
			select_weapon(slot)


## Rebuilds the gun as one of the four weapons. Each copies the base spawn
## data (so damage and collision stay), then changes speed and pattern.
func select_weapon(slot: int, announce := true) -> void:
	weapon = clampi(slot, 0, WEAPON_NAMES.size() - 1)
	var data: BulletVolleyData2D = _base_data.duplicate()
	var bullet_speed := BulletSpeedData2D.new()
	gun.homing_enabled = false
	gun.helper_fan_direction_angle = -PI / 2 # up the screen
	match weapon:
		0: # Rapid: two tight streams
			bullet_speed.speed = 760.0
			gun.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_FAN
			gun.helper_bullets_amount = 2
			gun.helper_fan_spread = 0.08
			gun.shoot_interval_sec = 0.09
		1: # Spread: a wide fan
			bullet_speed.speed = 600.0
			gun.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_FAN
			gun.helper_bullets_amount = 7
			gun.helper_fan_spread = 1.0
			gun.shoot_interval_sec = 0.22
		2: # Homing: slow missiles that hunt the nearest enemy
			bullet_speed.speed = 420.0
			gun.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_FAN
			gun.helper_bullets_amount = 3
			gun.helper_fan_spread = 0.6
			gun.shoot_interval_sec = 0.3
			gun.homing_enabled = true
			gun.homing_target_source = BulletSpawner2D.HOMING_SOURCE_NODE_GROUP
			gun.homing_node_group = &"enemy"
			gun.homing_smoothing = 6.0
			data.homing_take_control_of_texture_rotation = true
		3: # Nova: a ring all around the ship
			bullet_speed.speed = 480.0
			gun.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_RING
			gun.helper_bullets_amount = 16
			gun.helper_ring_radius = 20.0
			gun.shoot_interval_sec = 0.45
	bullet_speed.max_speed = bullet_speed.speed
	data.shared_bullet_speed_data = bullet_speed
	data.all_bullet_speed_data = []
	gun.spawn_data = data
	hud.select(weapon, announce)


func hit() -> void:
	var factory := get_parent().get_node_or_null("BulletFactory2D") as BulletFactory2D
	if factory != null:
		preload("res://examples/common/example_fx.gd").player_hurt(factory, global_position)
	modulate = Color(2.0, 0.4, 0.4)
	create_tween().tween_property(self, "modulate", Color.WHITE, 0.25)


## A gun for scenes that do not have one: rockets that hit the enemy layer (4).
func _build_gun() -> BulletSpawner2D:
	var frames := SpriteFrames.new()
	for i in range(1, 11):
		frames.add_frame(&"default", load("res://shared/art/player_bullets/rocket_%02d.png" % i))
	frames.set_animation_speed(&"default", 20.0)
	var data := BulletVolleyData2D.new()
	data.sprite_frames = frames
	data.texture_size = Vector2(36, 36)
	data.max_life_time = 1.4
	var shape := CircleShape2D.new()
	shape.radius = 6.0
	data.collision_shape = shape
	data.collision_layer = 0
	data.collision_mask = 4
	var built := BulletSpawner2D.new()
	built.name = "Gun"
	built.shooting_enabled = false
	built.position = Vector2(0, -18)
	built.spawn_data = data
	add_child(built)
	return built
