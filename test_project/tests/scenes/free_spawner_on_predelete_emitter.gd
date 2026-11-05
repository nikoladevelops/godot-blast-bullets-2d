class_name FreeSpawnerOnPredeleteEmitter2D
extends BulletEmitterData2D
## Frees `victim` from NOTIFICATION_PREDELETE: the last reference to this
## timing resource dropped mid-leg (a live-control re-arm replaces the config
## a script gave the volley with emitter_set) must not leave the leg walking a
## freed spawner (L6: snapshot + re-validate every leg).
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
