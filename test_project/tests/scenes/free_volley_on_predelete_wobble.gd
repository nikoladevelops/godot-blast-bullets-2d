class_name FreeVolleyOnPredeleteWobble2D
extends BulletWobbleData2D
## Frees `victim` from NOTIFICATION_PREDELETE: the last reference to this
## wobble resource dropped inside
## BulletVolley2D::set_shared_bullet_wobble_data must not leave the setter
## reading the freed volley.
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
