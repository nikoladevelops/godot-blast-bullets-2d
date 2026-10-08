class_name BlastTestHelpers
extends RefCounted
## Shared builders for the BlastBullets2D headless suites.
## Every suite is a SceneTree script; include this via preload and call the
## static makers so spawn data stays identical across suites (same speeds,
## layers, texture_size) and failures mean real regressions, not drift.
## Every builder assigns a real 1x1 SpriteFrames, so a normal spawn emits no
## errors (GUT strict mode fails tests on unexpected errors). texture_size
## keeps volleys visible-sized for the debugger.

## Minimal valid art: one 1x1 white frame in the "default" animation.
static func make_sprite_frames() -> SpriteFrames:
	var img := Image.create_empty(1, 1, false, Image.FORMAT_RGBA8)
	img.fill(Color.WHITE)
	var sf := SpriteFrames.new()
	sf.add_frame("default", ImageTexture.create_from_image(img))
	return sf

static func make_volley_data(n: int = 4, speed: float = 200.0, lifetime: float = 5.0) -> BulletVolleyData2D:
	var data := BulletVolleyData2D.new()
	var arr: Array = []
	for i in n:
		arr.append(Transform2D(0.0, Vector2(24.0 * i, 0.0)))
	data.transforms = arr
	# Strict indexing: one speed entry per bullet (entry i drives bullet i).
	var speeds: Array = []
	for i in n:
		var sp := BulletSpeedData2D.new()
		sp.speed = speed
		sp.max_speed = 3000.0
		sp.acceleration = 0.0
		speeds.append(sp)
	data.all_bullet_speed_data = speeds
	data.max_life_time = lifetime
	data.texture_size = Vector2(16, 16)
	data.sprite_frames = make_sprite_frames()
	data.set_collision_layer_from_array([2])
	data.set_collision_mask_from_array([4])
	return data

static func finite_volley(v: Array) -> bool:
	for t in v:
		if not (t as Transform2D).is_finite():
			return false
	return true

static func make_circle_shape(radius: float = 8.0) -> CircleShape2D:
	var c := CircleShape2D.new()
	c.radius = radius
	return c

## Stationary volley used by many state suites: n bullets 16 px apart, speed
## 0, 60 s life, circle r6, infinite collisions, mask layer 3.
static func make_still_data(n: int = 2) -> BulletVolleyData2D:
	var d := make_volley_data(n, 0.0, 60.0)
	var arr: Array = []
	for i in n:
		arr.append(Transform2D(0.0, Vector2(16.0 * i, 0.0)))
	d.transforms = arr
	d.set_collision_mask_from_array([3])
	d.collision_shape = make_circle_shape(6.0)
	d.bullet_max_collision_count = 0
	return d

static func make_rotation(speed: float, max_speed: float = 100.0, accel: float = 0.0) -> BulletRotationData2D:
	var r := BulletRotationData2D.new()
	r.rotation_speed = speed
	r.max_rotation_speed = max_speed
	r.rotation_acceleration = accel
	return r

static func make_speed(speed: float, max_speed: float = 3000.0, accel: float = 0.0) -> BulletSpeedData2D:
	var s := BulletSpeedData2D.new()
	s.speed = speed
	s.max_speed = max_speed
	s.acceleration = accel
	return s

## SpriteFrames with `frames` 8x8 white frames at `fps`, non-looping.
static func make_effect_frames(frames: int = 1, fps: float = 10.0) -> SpriteFrames:
	var sf := SpriteFrames.new()
	sf.set_animation_speed("default", fps)
	sf.set_animation_loop("default", false)
	for i in frames:
		var img := Image.create_empty(8, 8, false, Image.FORMAT_RGBA8)
		img.fill(Color.WHITE)
		sf.add_frame("default", ImageTexture.create_from_image(img))
	return sf

## Effect layer with `trigger` (BulletEffectLayerData2D.EFFECT_*).
static func make_effect_layer(trigger: int, frames: int = 1) -> BulletEffectLayerData2D:
	var l := BulletEffectLayerData2D.new()
	l.trigger = trigger
	l.sprite_frames = make_effect_frames(frames)
	return l

## Flat Curve at `value` with a wide value range (Curve clamps to [0, 1] by default).
static func make_flat_curve(value: float) -> Curve:
	var c := Curve.new()
	c.min_value = -10000.0
	c.max_value = 10000.0
	c.add_point(Vector2(0, value))
	c.add_point(Vector2(1, value))
	return c

## Graze zone with one ring per radius (bullet size off unless
## asked, so distances in tests are center to center).
static func make_graze_zone(radii: Array = [24.0], count_bullet_size := false) -> BulletGrazeZone2D:
	var z := BulletGrazeZone2D.new()
	z.ring_count = radii.size()
	for i in radii.size():
		z.set_ring_radius(i, radii[i])
	z.count_bullet_size = count_bullet_size
	return z

## Records every graze event `emitter` (a factory or a spawner) emits as
## [kind, target, volley, bullet_index, zone, ring], kind "enter" / "exit".
static func record_graze(emitter: Object) -> Array:
	var log: Array = []
	emitter.connect("bullet_grazed", func(t: Node2D, v: BulletVolley2D, i: int, z: BulletGrazeZone2D, r: int) -> void: log.append(["enter", t, v, i, z, r]))
	emitter.connect("bullet_graze_exited", func(t: Node2D, v: BulletVolley2D, i: int, z: BulletGrazeZone2D, r: int) -> void: log.append(["exit", t, v, i, z, r]))
	return log

## "kind:bullet:ring" per recorded graze event (compact asserts).
static func graze_kinds(log: Array) -> Array:
	var out: Array = []
	for e in log:
		out.append("%s:%d:%d" % [e[0], e[3], e[5]])
	return out

## One transform per point, facing +X.
static func transforms_at(points: Array) -> Array:
	var out: Array = []
	for p in points:
		out.append(Transform2D(0.0, p))
	return out

## Silent 8-bit mono WAV of `seconds`; loops by default so a voice stays busy
## for the whole test. 8-bit WAV is UNSIGNED: silence is 128, not 0.
static func make_sound_stream(seconds := 0.25, loop := true) -> AudioStreamWAV:
	var w := AudioStreamWAV.new()
	w.format = AudioStreamWAV.FORMAT_8_BITS
	w.mix_rate = 8000
	var bytes := PackedByteArray()
	bytes.resize(int(8000 * seconds))
	bytes.fill(128)
	w.data = bytes
	if loop:
		w.loop_mode = AudioStreamWAV.LOOP_FORWARD
		w.loop_end = bytes.size()
	return w

## Sound on `trigger` with a looping silent stream; interval 0 so every
## offer can play (tests that pin the interval set it themselves).
static func make_sound(trigger: int, max_voices := 4) -> BulletSoundData2D:
	var s := BulletSoundData2D.new()
	s.trigger = trigger
	s.streams = [make_sound_stream()]
	s.min_interval_sec = 0.0
	s.max_voices = max_voices
	return s
