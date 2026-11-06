class_name FreeSpawnerOnPredeleteSound2D
extends BulletSoundData2D
## Frees `victim` from NOTIFICATION_PREDELETE: the last reference to this
## entry dropped mid-leg (a live-control release/re-arm) must not leave the
## leg walking a freed spawner (L6: snapshot + re-validate every leg).
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
