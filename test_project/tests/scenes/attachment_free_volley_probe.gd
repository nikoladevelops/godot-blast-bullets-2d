class_name AttachmentFreeVolleyProbe2D
extends BulletAttachment2D
## on_bullet_disable frees the owning volley (immediate free(), re-entrantly).

static var volley: BulletVolley2D
static var freed := 0


func on_bullet_disable() -> void:
	if volley != null and is_instance_valid(volley):
		var v := volley
		volley = null
		v.free()
		freed += 1
