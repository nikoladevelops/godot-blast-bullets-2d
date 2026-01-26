extends BulletAttachment2D

var current_color:Color = Color.WHITE
var change_color_max_time:float = 0.5
var curr_time:float = 0.0

func _physics_process(delta: float) -> void:
	if curr_time >= change_color_max_time:
		var rand_R:float = randf()
		var rand_G:float = randf()
		var rand_B:float = randf()
		
		current_color = Color(rand_R, rand_G, rand_B, 1)
		modulate = current_color
		
		curr_time = 0.0
	
	curr_time += delta
	
func on_bullet_spawn() -> void:
	pass

func on_spawn_in_pool() -> void:
	set_process(false)
	set_physics_process(false)
	visible = false
	
	curr_time = 0

func on_bullet_disable() -> void:
	visible = false
	set_process(false)
	set_physics_process(false)

func on_bullet_enable() -> void:
	curr_time = 0
	current_color = Color.WHITE
	modulate = current_color
	
	visible = true
	set_process(true)
	set_physics_process(true)
	
