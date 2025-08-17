class_name Player
extends CharacterBody2D

# Player movement speed
@export var movement_speed = 520

# The position at which a single bullet gets spawned
@onready var bullet_marker:Marker2D = $MarkerContainer/BulletMarker

# A timer used to define when the player is allowed to shoot again.
@onready var fire_timer = $FireTimer

# Contains logic related to bullet data set up  # Note: it's cool to use composition in your project and separate different logic when scripts become huge
@onready var player_data_node:PlayerData = $PlayerDataNode

# Controls sprite animations
@onready var animated_sprite:AnimatedSprite2D = $AnimatedSprite2D

# The main camera
@onready var camera:Camera2D = $Camera2D

# Whether player is trying to shoot
var is_trying_to_shoot=false

# Whether the player can shoot in the current frame
var allowed_to_shoot=true

# The value which helps adjust the zoom value of the camera
var zoom_factor:float = 1.5

# The bigest possible camera zoom value
var max_zoom_value:float = 10

# The smallest possible camera zoom value
var min_zoom_value:float = 0.005

func _ready() -> void:
	player_data_node.set_up(bullet_marker)

# A simple function that determines the direction vector
func get_direction()->Vector2:
	var direction:Vector2=Vector2(0,0)
	
	if Input.is_action_pressed('move_up'):
		direction.y-=1
	elif Input.is_action_pressed("move_down"):
		direction.y+=1
	
	if Input.is_action_pressed('move_left'):
		direction.x-=1
	elif Input.is_action_pressed('move_right'):
		direction.x+=1
	return direction.normalized()

func _unhandled_input(event)->void:
	if event.is_action_pressed('player_shoot'):
		is_trying_to_shoot = true
	if event.is_action_released("player_shoot"):
		is_trying_to_shoot = false

	if event.is_action_pressed("zoom_in"):
		# The more you increase the zoom, the camera becomes smaller
		camera.zoom.x = clampf(camera.zoom.x * zoom_factor, min_zoom_value, max_zoom_value)
		camera.zoom.y = clampf(camera.zoom.y * zoom_factor, min_zoom_value, max_zoom_value)
	elif event.is_action_pressed("zoom_out"):
		# The more you decrease the zoom, the camera becomes larger
		camera.zoom.x = clampf(camera.zoom.x / zoom_factor, min_zoom_value, max_zoom_value)
		camera.zoom.y = clampf(camera.zoom.y / zoom_factor, min_zoom_value, max_zoom_value)
		
func _process(_delta)->void:
	if velocity != Vector2.ZERO:
		animated_sprite.play("moving")
	else:
		animated_sprite.play("idle")

	# Shoot logic
	if allowed_to_shoot && is_trying_to_shoot: # If the user has pressed the left mouse button and is allowed to shoot, then go ahead and spawn bullets
		player_data_node.spawn_bullets(rotation) # all the logic inside player_data_node could be put here, but I decided on data separation so it's more clear
		allowed_to_shoot=false
		fire_timer.start()

func _physics_process(delta: float) -> void:
	velocity = get_direction() * movement_speed
	move_and_slide()
	look_at(get_global_mouse_position())

# When the player needs to be damaged this function should be called
func take_damage(dmg_amount:int)->void:
	BENCHMARK_GLOBALS.PLAYER_HEALTH_BAR.take_damage(dmg_amount)

# Executed when the fire timer times out. Used as something like a cooldown so that bullets don't get spammed every single frame when the function gets called
func _on_fire_timer_timeout()->void:
	allowed_to_shoot=true # allow the player to shoot

# Changes the cooldown time of the fire timer
func change_fire_timer_cooldown(new_wait_time:float)->void:
	fire_timer.stop()
	allowed_to_shoot=true
	
	fire_timer.wait_time = new_wait_time
	fire_timer.start()
	
