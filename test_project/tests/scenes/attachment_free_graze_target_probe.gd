class_name AttachmentFreeGrazeTargetProbe2D
extends BulletAttachment2D
## Frees `victims` from on_bullet_disable: user code inside the sweep that must
## invalidate the graze target lists (AGENTS §20.2), so a later volley never
## tests a target this callback freed. Static, like the other hook probes.

static var victims: Array = []
static var calls := 0


func on_bullet_disable() -> void:
	calls += 1
	for v in victims:
		if is_instance_valid(v):
			v.free()
	victims.clear()
