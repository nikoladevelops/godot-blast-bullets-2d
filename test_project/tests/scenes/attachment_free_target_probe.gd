class_name AttachmentFreeTargetProbe2D
extends BulletAttachment2D
## Frees the pickup target (and any extra nodes) from inside a conversion,
## after the target was resolved. Pooled attachments get on_bullet_enable, new
## ones on_bullet_spawn: both do it.

static var victims: Array = []
static var calls := 0


func _free_victims() -> void:
	calls += 1
	for v in victims:
		if is_instance_valid(v):
			v.free()
	victims.clear()


func on_bullet_spawn() -> void:
	_free_victims()


func on_bullet_enable() -> void:
	_free_victims()
