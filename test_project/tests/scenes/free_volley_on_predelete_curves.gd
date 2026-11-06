class_name FreeVolleyOnPredeleteCurves2D
extends BulletCurvesData2D
## Frees `victim` from NOTIFICATION_PREDELETE: the last reference to this
## curves resource dropped inside
## BulletVolley2D::populate_shared_curves_related_data (and the per-bullet
## twin) must not leave the setter reading the freed volley.
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
