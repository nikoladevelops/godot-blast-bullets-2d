class_name FreeSpawnerOnPredeleteSpawnData2D
extends BulletVolleyData2D
## Frees `victim` from NOTIFICATION_PREDELETE: the last reference to this spawn
## data resource dropped inside BulletSpawner2D::set_spawn_data must not leave
## the setter reading the freed spawner.
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
