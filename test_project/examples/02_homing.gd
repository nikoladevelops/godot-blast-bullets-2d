extends Node2D
## Bullets that chase the player: the Homing group of Turret/Spawner targets
## the node group "player". Hold the left mouse button and every bullet already
## flying turns toward the cursor instead; let go and they hunt the player again.

@onready var spawner: BulletSpawner2D = $Turret/Spawner


func _ready() -> void:
	spawner.area_entered.connect(func(target: Object, _v, _b): if target == $Player: $Player.hit())


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
		spawner.home_live_volleys_to(get_global_mouse_position() if event.pressed else $Player)
