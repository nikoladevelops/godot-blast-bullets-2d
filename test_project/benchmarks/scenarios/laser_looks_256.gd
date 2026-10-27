extends BlastBenchmark
## Laser looks: 8 sweeping LaserVolley2Ds of 32 beams each (256 beams, 300 px)
## with an animated body plus a muzzle and a tip animation on every beam (512
## point frames per frame) and 24 pierce targets each showing a pierce
## animation. Per frame: one triangle array per laser and one textured rect
## per point frame. The looks are set through the saved property names so the
## same scenario measures older builds.
var _hits := 0

func describe() -> String:
	return "256 sweeping beams (8 lasers) with body, muzzle, tip and pierce animations"

func _frames(count: int) -> SpriteFrames:
	var sf := SpriteFrames.new()
	sf.set_animation_speed(&"default", 12.0)
	for i in count:
		var img := Image.create(16, 16, false, Image.FORMAT_RGBA8)
		img.fill(Color(1, 1 - 0.2 * i, 0.5, 1))
		sf.add_frame(&"default", ImageTexture.create_from_image(img))
	return sf

func setup() -> void:
	for i in 24:
		make_static_box(Vector2(200 + 70 * i, 300 + 400 * (i % 2)), Vector2(24, 24), 4)
	var d := H.make_laser(0.0, 0.0, 0.0, 0.0, 300.0) # active until ended
	d.sweep_speed_degrees = 40.0
	d.sweep_arc_degrees = 90.0
	d.body_frames = _frames(3)
	var sf := _frames(4)
	d.set("muzzle_frames", sf)
	d.set("tip_frames", sf)
	d.set("pierce_frames", sf)
	factory.laser_hit.connect(_on_hit)
	for k in 8:
		var center := Vector2(300 + 440 * (k % 4), 300 + 480 * (k / 4))
		var beams: Array[Transform2D] = []
		for b in 32:
			beams.append(Transform2D(TAU * b / 32.0, center))
		factory.spawn_laser(d, beams)

func _on_hit(_t: Node, _l: LaserVolley2D, _b: int, _p: Vector2) -> void:
	_hits += 1

func step(frame: int) -> void:
	extra["hits"] = _hits
