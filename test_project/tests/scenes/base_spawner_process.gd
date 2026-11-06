extends BulletSpawner2D
## Fixture: a base spawner script whose _process must survive the plugin's
## engine-callback detection for a child script that inherits it.

var processed := 0


func _process(_delta: float) -> void:
	processed += 1
