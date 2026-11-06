class_name FreeSpawnerOnPredeleteSplit2D
extends BulletSplitData2D
## Frees `victim` from NOTIFICATION_PREDELETE: the last reference to this split
## chain dropped mid-leg (a live-control re-arm replaces the config a script
## gave the volley with split_set_data) must not leave the arm reading the
## freed spawner through the reference it was handed (L6).
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
