extends BulletAttachment2D

@onready var CPUParticles = $CPUParticles2D

func on_bullet_spawn() -> void:
	pass
	
func on_bullet_spawn_as_disabled() -> void:
	set_process(false)
	set_physics_process(false)
	visible = false

func on_bullet_disable() -> void:
	visible = false
	set_process(false)
	set_physics_process(false)

func on_bullet_enable() -> void:
	CPUParticles.one_shot = false
	
	CPUParticles.restart()
	
	set_process(true)
	set_physics_process(true)
	visible = true
	
func on_bullet_save() -> Resource:
	return null

func on_bullet_load(_custom_data_to_load: Resource) -> void:
	pass
	
