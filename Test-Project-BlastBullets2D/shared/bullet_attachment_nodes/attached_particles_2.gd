extends BulletAttachment2D

@onready var GPUParticles = $GPUParticles2D

func on_bullet_spawn() -> void:
	attachment_id = 2

func on_bullet_spawn_as_disabled() -> void:
	attachment_id = 2
	
	set_process(false)
	set_physics_process(false)
	visible = false

func on_bullet_disable() -> void:
	visible = false
	
	set_process(false)
	set_physics_process(false)

func on_bullet_activate() -> void:
	GPUParticles.restart()
	set_process(true)
	set_physics_process(true)
	
	visible = true
	
func on_bullet_save() -> Resource:
	return Resource.new()

func on_bullet_load(custom_data_to_load: Resource) -> void:
	pass
