class_name PickupTestHelpers
extends RefCounted
## Builders shared by the pickup suites (not a suite: no test_ prefix).


## A coin that reaches a target within ~1 s from a few hundred px.
static func coin(value: Variant = 1) -> BulletPickupData2D:
	var p := BulletPickupData2D.new()
	p.magnet_delay_sec = 0.1
	p.magnet_acceleration = 3000.0
	p.magnet_max_speed = 1500.0
	p.drag = 2.0
	p.collect_radius = 20.0
	p.lifetime_sec = 30.0
	p.value = value
	return p


## SpriteFrames with a looping 4-frame "spin" (12x12 frames, 10 fps) and a
## non-looping 3-frame "pop" (20x20 frames, 10 fps).
static func frames() -> SpriteFrames:
	var sf := SpriteFrames.new()
	sf.remove_animation("default")
	sf.add_animation("spin")
	sf.set_animation_speed("spin", 10.0)
	sf.set_animation_loop("spin", true)
	for i in 4:
		sf.add_frame("spin", _tex(12, Color(1, 0.8 - i * 0.1, 0.1)))
	sf.add_animation("pop")
	sf.set_animation_speed("pop", 10.0)
	sf.set_animation_loop("pop", false)
	for i in 3:
		sf.add_frame("pop", _tex(20, Color(0.2, 1.0 - i * 0.2, 0.2)))
	return sf


static func _tex(size: int, color: Color) -> ImageTexture:
	var image := Image.create(size, size, false, Image.FORMAT_RGBA8)
	image.fill(color)
	return ImageTexture.create_from_image(image)


## A still volley with one bullet per point on `factory`.
static func still(factory: BulletFactory2D, points: Array, speed := 0.0, lifetime := 60.0) -> BulletVolley2D:
	var data := BlastTestHelpers.make_volley_data(points.size(), speed, lifetime)
	var arr: Array = []
	for p in points:
		arr.append(Transform2D(0.0, p))
	data.transforms = arr
	return factory.spawn_volley(data)


static func alive(v: BulletVolley2D) -> Array:
	var out: Array = []
	for i in v.get_amount_bullets():
		if v.is_bullet_status_enabled(i):
			out.append(i)
	return out
