extends BlastTest
## The script-facing debug-readout contract (block H): the introspection API
## is walked from ClassDB over every life state (fresh / active / parked /
## pooled / dying), so a new debug_* method is covered automatically, and the
## individual readouts are pinned against the public API they mirror:
## debug_get_volley_info, debug_get_clocks, the interpolation-pass snapshot,
## and the factory's frame stats against its own state and pool counters.

const DT := 0.0625
const INDEX_GETTERS := [
	"debug_get_previous_origin",
	"debug_get_bullet_shape_transform",
	"debug_get_trail_transform",
	"debug_get_bullet_flock_steering",
	"debug_get_bullet_flock_neighbors",
	"debug_get_bullet_flock_info",
]


func _arg(a: Dictionary) -> Variant:
	match String(a.get("name", "")):
		"bullet_index":
			return 0
		"target_instance_id":
			return 1000003
		"probe_count":
			return 2
		"fraction":
			return 0.5
		"layer_index":
			return 0
	match int(a.get("type", TYPE_NIL)):
		TYPE_INT:
			return 0
		TYPE_FLOAT:
			return 0.5
		TYPE_BOOL:
			return true
	return null


## Calls every bound debug_* method on `v` with a typed dummy argument for
## each declared parameter and checks the returned Variant against the
## method's ClassDB return type (void methods are only called: "no crash").
## Returns {problems, covered} so the caller can prove the walk is not empty.
func _walk(v: BulletVolley2D, state: String) -> Dictionary:
	var problems: Array = []
	var covered := 0
	for m in ClassDB.class_get_method_list(&"BulletVolley2D", true):
		var method_name := String(m["name"])
		if not method_name.begins_with("debug_"):
			continue
		covered += 1
		var args: Array = []
		for a in m.get("args", []):
			args.append(_arg(a))
		var value: Variant = v.callv(method_name, args)
		var declared := int((m["return"] as Dictionary).get("type", TYPE_NIL))
		if declared == TYPE_NIL:
			continue
		if value == null:
			problems.append("%s/%s: returned null" % [state, method_name])
		elif typeof(value) != declared:
			problems.append("%s/%s: returned type %d, declared %d" % [state, method_name, typeof(value), declared])
	return {"problems": problems, "covered": covered}


## Case 52: one volley instance per life state, every bound debug_* method
## returns a non-nil value of its declared type and the volley's invariant
## books stay clean. The only state that refuses is "fresh" (a never-spawned
## handle has 0 bullets): the six index-taking getters push their pinned
## out-of-range text; every live state must stay silent.
func test_every_debug_method_returns_its_declared_type_in_every_state() -> void:
	var fresh := BulletVolley2D.new()
	add(fresh)
	assert_eq(fresh.debug_get_life_state(), "fresh", "never spawned")
	var fresh_walk: Dictionary = _walk(fresh, "fresh")
	var problems: Array = fresh_walk["problems"]
	assert_gte(int(fresh_walk["covered"]), 25, "the ClassDB walk covers the whole debug surface")
	for fn in INDEX_GETTERS:
		# Index 0 of a 0-bullet handle: each getter refuses once, wording pinned.
		expect_errors_containing("Invalid bullet index in %s: 0 (amount_bullets: 0)" % fn, 1)
	var active := quick_volley(4, 100.0, 30.0)
	step_factory(2, DT)
	assert_eq(active.debug_get_life_state(), "active", "ticked")
	var parked := quick_volley(2, 0.0, 30.0)
	parked.is_auto_pooling_enabled = false
	for i in 2:
		parked.disable_bullet(i)
	assert_eq(parked.debug_get_life_state(), "parked", "auto pooling off, all bullets out")
	var pooled := quick_volley(2, 0.0, 30.0)
	for i in 2:
		pooled.disable_bullet(i)
	assert_eq(pooled.debug_get_life_state(), "pooled", "auto pooling on, all bullets out")
	var dying := quick_volley(1, 0.0, 30.0)
	dying.queue_free()
	assert_eq(dying.debug_get_life_state(), "dying", "queued")
	for pair in [["active", active], ["parked", parked], ["pooled", pooled], ["dying", dying]]:
		problems.append_array(_walk(pair[1], pair[0])["problems"])
	expect_no_errors("the four live states refuse nothing")
	assert_eq(problems, [], "every bound debug_* method returned its declared type")
	for pair in [["fresh", fresh], ["active", active], ["parked", parked], ["pooled", pooled], ["dying", dying]]:
		assert_eq((pair[1] as BulletVolley2D).debug_check_invariants(), "", "%s: invariant books hold" % pair[0])
	await idle(1) # the queued volley's deletion lands
	assert_true(factory.debug_assert_no_dangling()["ok"], "factory books hold")


## Case 53: debug_get_volley_info mirrors the public readouts it summarizes,
## and its pool_* fields are exactly the volley's pool bucket.
func test_volley_info_matches_the_public_readouts_and_the_pool_bucket() -> void:
	var d := H.make_volley_data(4, 120.0, 30.0)
	var v: BulletVolley2D = factory.spawn_volley(d)
	var info: Dictionary = v.debug_get_volley_info()
	assert_eq(int(info["active_bullets"]), v.get_active_bullet_count(), "active_bullets == get_active_bullet_count()")
	assert_eq(int(info["generation"]), v.get_life_id(), "generation == get_life_id()")
	assert_eq(bool(info["is_pooled"]), v.is_pooled(), "is_pooled == is_pooled()")
	assert_eq(int(info["amount_bullets"]), v.get_amount_bullets(), "amount_bullets == get_amount_bullets()")
	assert_eq(bool(info["is_active"]), v.is_volley_active(), "is_active == is_volley_active()")
	assert_eq(int(info["owner_spawner_id"]), 0, "a factory volley has no owner spawner")
	var bucket: VolleyPoolKey2D = factory.debug_get_pool_bucket(v)
	assert_not_null(bucket, "every volley has a pool bucket")
	assert_eq(int(info["pool_amount"]), bucket.get_amount_bullets(), "pool_amount == the bucket's amount")
	assert_eq(int(info["pool_shape"]), bucket.get_shape_type(), "pool_shape == the bucket's shape")
	assert_eq(bool(info["pool_colors"]), bucket.get_use_bullet_colors(), "pool_colors == the bucket's colors")
	var expected: VolleyPoolKey2D = factory.debug_expected_pool_key(d)
	assert_eq([bucket.get_amount_bullets(), int(bucket.get_shape_type()), bucket.get_use_bullet_colors()],
			[expected.get_amount_bullets(), int(expected.get_shape_type()), expected.get_use_bullet_colors()], "the bucket is the spawn data's key")
	var life := v.get_life_id()
	v.disable_bullet(0)
	info = v.debug_get_volley_info()
	assert_eq(int(info["active_bullets"]), 3, "one disabled: still in step with the public count")
	assert_eq(int(info["active_bullets"]), v.get_active_bullet_count())
	for i in [1, 2, 3]:
		v.disable_bullet(i)
	assert_true(v.is_pooled(), "the last one out pools it")
	info = v.debug_get_volley_info()
	assert_eq([bool(info["is_pooled"]), int(info["active_bullets"]), int(info["generation"])], [true, 0, life], "a pooled handle reports itself, 0 live bullets and the same generation")
	assert_eq(int(info["generation"]), v.get_life_id(), "generation still == get_life_id()")
	expect_no_errors()


## Case 54: the one clocks snapshot mirrors get_life_time_left(), advances by
## exactly four ticks of DT, lists every attached timer with its fields, and
## a pooled handle has dropped the timers (its other clocks are stale reads
## of the finished life, kept consistent with the public getter).
func test_clocks_mirror_the_public_clocks_and_advance_exactly() -> void:
	var v := quick_volley(3, 0.0, 30.0)
	var calls := []
	v.attach_time_based_function(0.5, func() -> void: calls.append(1), true, false)
	var clocks: Dictionary = v.debug_get_clocks()
	assert_eq(clocks["life_time_left"], v.get_life_time_left(), "life_time_left == get_life_time_left()")
	assert_eq([clocks["max_life_time"], clocks["is_life_time_infinite"], clocks["curves_elapsed_time"]], [30.0, false, 0.0], "the data's window, finite, at 0")
	assert_eq(v.debug_get_timer_count(), 1, "the timer is counted")
	var timers: Array = clocks["timers"]
	assert_eq(timers.size(), 1, "and listed")
	assert_eq([int(timers[0]["id"]) > 0, timers[0]["period"], timers[0]["time_left"], timers[0]["repeating"]], [true, 0.5, 0.5, true], "id, period, remaining time and repeating flag")
	step_factory(4, DT)
	clocks = v.debug_get_clocks()
	assert_eq(clocks["life_time_left"], v.get_life_time_left(), "still in step with the public getter")
	assert_eq(clocks["life_time_left"], 30.0 - 4.0 * DT, "four ticks advanced it by exactly 4*DT")
	assert_eq(clocks["curves_elapsed_time"], 4.0 * DT, "the volley clock too")
	assert_eq((clocks["timers"] as Array)[0]["time_left"], 0.5 - 4.0 * DT, "the timer clock too")
	assert_eq(calls, [], "0.25 s in: the 0.5 s timer has not run")
	for i in 3:
		v.disable_bullet(i)
	assert_true(v.is_pooled(), "pooled")
	clocks = v.debug_get_clocks()
	assert_eq([v.debug_get_timer_count(), clocks["timers"]], [0, []], "pooling drops the life's timers")
	assert_eq(clocks["life_time_left"], v.get_life_time_left(), "a stale handle still reads the same life clock consistently")
	assert_eq((clocks["bounce_cooldowns"] as PackedFloat32Array).size(), 0, "and no bounce cooldowns")
	expect_no_errors()


func _snapshot(v: BulletVolley2D) -> String:
	var rows: Array = []
	for i in v.get_amount_bullets():
		rows.append([v.get_bullet_transform(i), v.get_bullet_velocity(i), v.debug_get_previous_origin(i)])
	rows.append(Array(v.get_active_bullet_indexes()))
	rows.append(v.debug_get_clocks())
	return str(rows)


## Case 55: the interpolation pass (0, 0.5, 1) rewrites only what is DRAWN:
## every script readout - origins, velocities, previous origins, active
## indexes, clocks - is identical before and after each pass, while the
## MultiMesh buffer really is blended between the two poses (the pass ran).
func test_interpolation_passes_leave_every_readout_snapshot_identical() -> void:
	factory.physics_interpolation = BulletFactory2D.INTERPOLATION_ON
	var v := quick_volley(3, 600.0, 30.0)
	step_factory(2, DT)
	var before := _snapshot(v)
	assert_gt(v.debug_get_previous_origin(0).distance_to(v.get_bullet_transform(0).origin), 1.0, "it moved this tick")
	var prev: Vector2 = v.debug_get_previous_origin(0)
	var current: Vector2 = v.get_bullet_transform(0).origin
	for f in [0.0, 0.5, 1.0]:
		v.debug_run_interpolation_pass(f)
		assert_eq(_snapshot(v), before, "pass %s changed no script readout" % f)
		var buf: PackedFloat32Array = v.multimesh.buffer
		assert_almost_eq(Vector2(buf[3], buf[7]), prev.lerp(current, f), Vector2(0.01, 0.01), "pass %s did draw between the poses" % f)
	expect_no_errors()


## Case 56: the frame stats' volley buckets cover exactly the factory's own
## state, hits + misses count every spawn, and debug_reset_frame_stats()
## zeroes the cumulative counters without touching live state.
func test_frame_stats_agree_with_factory_state_across_life_states() -> void:
	factory.reset_frame_stats()
	factory.debug_reset_pool_stats()
	var active := quick_volley(4, 100.0, 30.0)
	var parked := quick_volley(2, 0.0, 30.0)
	parked.is_auto_pooling_enabled = false
	for i in 2:
		parked.disable_bullet(i)
	var pooled := quick_volley(3, 0.0, 30.0)
	for i in 3:
		pooled.disable_bullet(i)
	await idle(1)
	var s: Dictionary = factory.get_frame_stats()
	var state: Dictionary = factory.debug_get_factory_state()
	var buckets := [int(s["active_volleys"]), int(s["parked_volleys"]), int(s["pooled_volleys"])]
	assert_eq(buckets, [1, 1, 1], "one volley in each bucket")
	assert_eq(buckets[0] + buckets[1] + buckets[2], int(state["volleys_total"]), "the buckets cover every tracked volley")
	assert_eq(int(state["volleys_total"]), factory.debug_get_total_bullets_amount(), "the factory's total readout agrees")
	assert_eq(int(s["active_bullets"]), active.get_amount_bullets(), "only the active volley holds live bullets")
	assert_eq(int(s["spawned_bullets_total"]), 9, "every spawned bullet counted once")
	factory.debug_reset_pool_stats()
	for i in 3:
		var life := quick_volley(2, 0.0, 0.05)
		for k in 30:
			await physics(1)
			if life.is_pooled():
				break
	s = factory.get_frame_stats()
	assert_eq([int(s["pool_misses"]), int(s["pool_hits"])], [1, 2], "first spawn a miss, the reused two hits")
	assert_eq(int(s["pool_hits"]) + int(s["pool_misses"]), 3, "every spawn resolved as a hit or a miss")
	factory.reset_frame_stats()
	s = factory.get_frame_stats()
	for key in ["physics_ticks", "peak_physics_tick_usec", "collision_records_total", "expired_bullets_total", "spawned_bullets_total"]:
		assert_eq(int(s[key]), 0, "%s is a cumulative counter: reset" % key)
	assert_eq(int(s["active_bullets"]), active.get_active_bullet_count(), "live state survives the reset")
	assert_true(factory.debug_assert_no_dangling()["ok"], "factory books hold")
	expect_no_errors()
