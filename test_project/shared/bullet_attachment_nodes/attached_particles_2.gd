extends BulletAttachment2D

@onready var GPUParticles = $GPUParticles2D

func _ready()->void:
	BENCHMARK_GLOBALS.UI.bullets_selected_z_index_changed.connect(func(new_z_index):
		z_index = new_z_index
	)

func on_bullet_spawn() -> void:
	pass

func on_spawn_in_pool() -> void:
	set_process(false)
	set_physics_process(false)
	visible = false

func on_bullet_disable() -> void:
	visible = false
	
	set_process(false)
	set_physics_process(false)

func on_bullet_enable() -> void:
	GPUParticles.restart()
	set_process(true)
	set_physics_process(true)
	
	visible = true
