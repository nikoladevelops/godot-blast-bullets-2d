extends Area2D
## The player of every example: the arrow keys move it, a hit flashes it red.

@export var speed := 320.0


func _physics_process(delta: float) -> void:
	position += Input.get_vector("ui_left", "ui_right", "ui_up", "ui_down") * speed * delta
	position = position.clamp(Vector2(24, 24), get_viewport_rect().size - Vector2(24, 24))


func hit() -> void:
	modulate = Color(2.0, 0.4, 0.4)
	create_tween().tween_property(self, "modulate", Color.WHITE, 0.25)
