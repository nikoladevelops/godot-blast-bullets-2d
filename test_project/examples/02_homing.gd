extends Node2D
## Bullets that chase the player: the Homing group of Turret/Spawner targets
## the node group "player". Hold the RIGHT mouse button (the left one fires)
## and the spawner hunts the mouse instead: its homing source switches to
## Mouse (read every frame), and retarget_live_volleys() turns the bullets
## already flying at once. Let go and they hunt the player again.
##
## Why switch the source instead of calling home_live_volleys_to(cursor):
## the spawner retargets its live bullets every homing_retarget_interval_sec
## from its OWN source, so a one-off redirect would be undone within 0.5 s.

@onready var spawner: BulletSpawner2D = $Turret/Spawner


func _ready() -> void:
	spawner.area_entered.connect(func(target: Object, _v, _b): if target == $Player: $Player.hit())


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_RIGHT:
		set_chase_mouse(event.pressed)


func set_chase_mouse(on: bool) -> void:
	spawner.homing_target_source = BulletSpawner2D.HOMING_SOURCE_MOUSE if on else BulletSpawner2D.HOMING_SOURCE_NODE_GROUP
	spawner.retarget_live_volleys() # the bullets already flying follow the new source now
