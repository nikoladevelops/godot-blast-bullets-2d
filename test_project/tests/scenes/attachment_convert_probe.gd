class_name AttachmentConvertProbe2D
extends BulletAttachment2D
## on_bullet_disable calls back into the factory (a conversion): the busy
## latch of the conversion in flight must refuse it loudly, not recurse.

static var factory: BulletFactory2D
static var pickup: BulletPickupData2D
static var refused_calls := 0
static var results: Array = []


func on_bullet_disable() -> void:
	if factory != null and pickup != null:
		refused_calls += 1
		results.append(factory.convert_all_bullets_to_pickups(pickup, Vector2.ZERO))
