class_name AttachmentStatusProbe2D
extends BulletAttachment2D
## Records what the volley reports about bullet 0 from inside the spawn and
## enable callbacks (static: the packed scene instantiates a fresh node).

static var volley: BulletVolley2D
static var spawn_seen: Array = []
static var enable_seen: Array = []


func on_bullet_spawn() -> void:
	if volley != null:
		spawn_seen.append(volley.is_bullet_status_enabled(0))


func on_bullet_enable() -> void:
	if volley != null:
		enable_seen.append(volley.is_bullet_status_enabled(0))
