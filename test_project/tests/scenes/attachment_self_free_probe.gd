class_name AttachmentSelfFreeProbe2D
extends BulletAttachment2D
## on_time_scale_changed(1.0) frees the VOLLEY the attachment belongs to (it is
## told 1.0 when its bullet is released). Godot refuses to free an object that
## is inside its own call, so the volley is the reachable victim: the release
## path must survive it.

static var armed := false
static var volley: BulletVolley2D


func on_time_scale_changed(scale: float) -> void:
	if armed and scale == 1.0 and volley != null and is_instance_valid(volley):
		var v := volley
		volley = null
		v.free()
