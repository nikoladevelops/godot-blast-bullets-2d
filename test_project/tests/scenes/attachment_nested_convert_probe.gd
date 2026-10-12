class_name AttachmentNestedConvertProbe2D
extends BulletAttachment2D
## Starts another conversion while the first one is spawning its pickups (once
## per attachment): the nested call must be refused, not convert the same
## bullets twice.

static var factory: BulletFactory2D
static var pickup: BulletPickupData2D
static var results: Array = []
static var attempts := 0


func _try() -> void:
	if factory != null and pickup != null and attempts < 8:
		attempts += 1
		results.append(factory.convert_all_bullets_to_pickups(pickup, Vector2(0, 900)))


func on_bullet_spawn() -> void:
	_try()


func on_bullet_enable() -> void:
	_try()
