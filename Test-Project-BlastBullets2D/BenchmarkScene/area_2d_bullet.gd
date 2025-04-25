class_name Area2DBullet
extends Area2D

@onready var life_time_timer:Timer = $LifeTimeTimer
@onready var animated_sprite:AnimatedSprite2D = $AnimatedSprite2D

var speed:float = 200
var direction:Vector2 = Vector2.RIGHT
var damage:int = 10
var life_time:float = 5

func _ready()->void:
	life_time_timer.wait_time = life_time
	life_time_timer.start()
	
	animated_sprite.play('default')

func _physics_process(delta: float) -> void:
	global_position += direction * speed * delta

func _on_body_entered(body: Node2D) -> void:
	queue_free()

func _on_area_entered(area: Area2D) -> void:
	if area is AbstractEnemy:
		area.take_damage(damage)
	queue_free()

func _on_life_time_timer_timeout() -> void:
	queue_free()
