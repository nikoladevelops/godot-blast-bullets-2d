extends BlastTest
## The sprite animation (a volley-wide SpriteFrames animation, not looping,
## ending in sprite_animation_finished) mixed with pooling, parking, time
## scale, pauses, splitting, pickups and handlers that act on the finish:
## every life plays from frame 0 and finishes exactly once (never for a life
## that ended first), its clock is the volley's (scaled, frozen while
## paused or parked), and whatever a finish handler does leaves the books
## balanced.

const DT := 1.0 / 60.0
const FPS := 20.0 # 4 frames: finished after 0.2 s of volley time


func _data(lifetime := 2.0, n := 2) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, 100.0, lifetime)
	d.sprite_frames = H.make_effect_frames(4, FPS)
	return d


func _watch(v: BulletVolley2D, log: Array) -> void:
	v.sprite_animation_finished.connect(func(vv) -> void: log.append([vv.get_life_id(), Engine.get_physics_frames()]))


func _frame(v: BulletVolley2D) -> int:
	return int(v.debug_get_clocks()["anim_frame_index"])


## The frame index after each of `ticks` ticks.
func _frames(v: BulletVolley2D, ticks: int) -> Array:
	var out: Array = [_frame(v)]
	for k in ticks:
		step_factory(1)
		out.append(_frame(v))
	return out


func test_every_life_plays_from_frame_zero_and_finishes_once() -> void:
	var finished: Array = []
	var v: BulletVolley2D = factory.spawn_volley(_data(0.5))
	_watch(v, finished)
	var cold := _frames(v, 16)
	assert_eq(cold[0], 0, "first life starts at frame 0")
	step_factory(24) # 0.66 s: finished at 0.2, expired at 0.5
	assert_eq(finished.size(), 1, "finished once in the first life")
	assert_true(v.is_pooled(), "pooled after its lifetime")
	var again: BulletVolley2D = factory.spawn_volley(_data(0.5))
	assert_eq(again, v, "the same volley from the pool")
	assert_false(bool(again.debug_get_clocks()["anim_finished"]), "not finished yet")
	_watch(again, finished) # its connections were dropped with the old life
	assert_eq(_frames(again, 16), cold, "the reused life plays the same frames on the same ticks")
	assert_eq(finished.size(), 2, "finished once in the second life too")
	assert_eq(int(finished[1][0]), again.get_life_id(), "reported by the new life")


func test_a_life_that_ends_first_never_finishes() -> void:
	var finished: Array = []
	var v: BulletVolley2D = factory.spawn_volley(_data(0.1))
	_watch(v, finished)
	step_factory(30)
	assert_true(v.is_pooled(), "expired before the animation's end")
	assert_eq(finished.size(), 0, "no finish for a life that ended first")


func test_the_animation_runs_on_the_volley_clock(scale: float = use_parameters([0.5, 2.0])) -> void:
	factory.time_scale = scale
	var finished: Array = []
	var v: BulletVolley2D = factory.spawn_volley(_data())
	_watch(v, finished)
	# 0.2 s of volley time = 0.2 / scale s of real time (+ one tick of slack).
	var ticks := int(ceil(0.2 / scale / DT))
	step_factory(ticks - 2)
	assert_eq(finished.size(), 0, "scale %s: not yet" % scale)
	step_factory(3)
	assert_eq(finished.size(), 1, "scale %s: finished on the scaled clock" % scale)


func test_a_pause_and_a_frozen_clock_hold_the_frame() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data())
	step_factory(3) # frame 1
	var at := _frame(v)
	factory.time_scale = 0.0
	step_factory(30)
	assert_eq(_frame(v), at, "time scale 0 holds the frame")
	factory.time_scale = 1.0
	factory.set_is_factory_processing_bullets(false)
	await physics(30)
	assert_eq(_frame(v), at, "a paused factory holds the frame")
	factory.set_is_factory_processing_bullets(true)
	step_factory(3)
	assert_gt(_frame(v), at, "and it plays on afterwards")


func test_a_parked_volley_holds_its_frame_and_a_woken_one_plays_to_its_end() -> void:
	var finished: Array = []
	var v: BulletVolley2D = factory.spawn_volley(_data(0.15))
	_watch(v, finished)
	v.set_is_auto_pooling_enabled(false)
	step_factory(3)
	var at := _frame(v)
	step_factory(20) # expired at 0.15 s, before the end: parked
	assert_true(v.is_parked(), "parked")
	assert_eq(finished.size(), 0, "no finish for an expired life")
	var parked_frame := _frame(v)
	step_factory(10)
	assert_eq(_frame(v), parked_frame, "a parked volley holds its frame")
	assert_gte(parked_frame, at, "it had played on until it expired")
	v.enable_bullet(0) # an expired parked volley: a fresh lifetime
	step_factory(20)
	assert_eq(finished.size(), 1, "the woken life plays to its end once")


var _action := ""
var _extra: Array = []


func _on_finished(vv: BulletVolley2D) -> void:
	match _action:
		"queue_free":
			vv.queue_free()
		"clear":
			vv.clear_all_bullets()
		"free_like_release":
			BulletFactory2D.debug_free_like_release(vv)
		"spawn":
			_extra.append(factory.spawn_volley(_data(0.3)))
		"disable":
			vv.disable_bullet(0)


func test_finish_handlers_may_free_pool_clear_or_spawn(action: String = use_parameters(["queue_free", "clear", "free_like_release", "spawn", "disable"])) -> void:
	_action = action
	_extra.clear()
	var v: BulletVolley2D = factory.spawn_volley(_data())
	v.sprite_animation_finished.connect(_on_finished)
	var extra := _extra
	for f in 20:
		await physics(1)
	await idle(2)
	assert_true(factory.debug_assert_no_dangling()["ok"], "%s: balanced books" % action)
	if action == "spawn":
		assert_eq(extra.size(), 1, "the handler spawned exactly once")
	expect_no_errors(action)


func test_split_children_and_pickups_start_their_own_animation() -> void:
	var d := _data(1.0, 1)
	var stage := BulletSplitStage2D.new()
	stage.count = 2
	stage.split_after_sec = 0.25 # after the parent's animation finished
	var chain := BulletSplitData2D.new()
	chain.stages = [stage]
	var v: BulletVolley2D = factory.spawn_volley(d)
	v.split_set_data(chain, _data(1.0, 2))
	var children: Array = []
	factory.bullet_split.connect(func(_p, _c, ch, _d) -> void:
		children.append([ch, _frame(ch), bool(ch.debug_get_clocks()["anim_finished"])]))
	for f in 20:
		await physics(1)
	assert_eq(children.size(), 1, "split")
	assert_eq(children[0][1], 0, "the children start at frame 0")
	assert_false(children[0][2], "and are not finished")
	var made: Array = []
	factory.bullets_converted.connect(func(_n, list) -> void: made.append_array(list))
	var coin := BulletPickupData2D.new()
	coin.sprite_frames = H.make_effect_frames(3, 10.0)
	factory.convert_all_bullets_to_pickups(coin, Vector2(900, 900))
	assert_gt(made.size(), 0, "converted")
	for p in made:
		assert_eq(_frame(p), 0, "a pickup starts its own look at frame 0")
