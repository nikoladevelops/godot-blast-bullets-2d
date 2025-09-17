extends Sprite2D

@export var speed:int = 200
@export var range_x:int = 30
@export var direction:int = 1
@export var switch_direction_when_reaching_range_x:bool = true

var original_global_position:Vector2

var max_x_range:float
var min_x_range:float

func _ready()->void:
	original_global_position = global_position
	max_x_range = original_global_position.x + range_x
	min_x_range = original_global_position.x - range_x

func _physics_process(delta: float) -> void:
	global_position.x += direction * speed * delta
	
	# When reaching max range or min range, turn around and start moving in opposite direction
	if switch_direction_when_reaching_range_x && (global_position.x >= max_x_range || global_position.x <= min_x_range):
		direction = direction * -1
	
