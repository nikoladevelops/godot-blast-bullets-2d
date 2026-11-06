class_name FreeVolleyOnPredeleteFrames2D
extends SpriteFrames
## Frees `victim` from NOTIFICATION_PREDELETE: the last reference to these
## sprite frames dropped inside BulletVolley2D::rebuild_sprite_animation (the
## play_sprite_animation path) must not leave the setter reading the freed
## volley.
## Static, like the other hook probes: the callback is the fixture's own.

static var victim: Node = null
static var fired := 0


func _notification(what: int) -> void:
	if what == NOTIFICATION_PREDELETE:
		fired += 1
		var target := victim
		victim = null
		if is_instance_valid(target):
			target.free()
