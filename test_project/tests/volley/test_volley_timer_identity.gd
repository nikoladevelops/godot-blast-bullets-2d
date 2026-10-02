extends BlastTest
## Custom-timer identity: a queued timer fire must not survive the detach
## that logically cancelled it (each timer carries an id; a targeted detach
## cancels that callable's pending fires, detach_all bumps the generation).

## Each probe is a distinct callable target so a detach can hit one timer.
class Probe extends RefCounted:
	var fires := 0
	func tick() -> void:
		fires += 1


func _still() -> DirectionalBullets2D:
	return spawn_dir(1, 0.0, 30.0)


func _until(p: Probe, n: int, frames := 20) -> void:
	for i in frames:
		await physics()
		if p.fires >= n:
			return


func test_one_shot_fires_once() -> void:
	var v := _still()
	var p := Probe.new()
	v.multimesh_attach_time_based_function(0.05, p.tick)
	assert_eq(v.debug_get_timer_count(), 1, "timer attached")
	await physics(15)
	assert_gte(p.fires, 1, "one-shot fired")
	assert_eq(v.debug_get_timer_count(), 0, "one-shot removed itself after firing")


func test_detach_all_cancels() -> void:
	var v := _still()
	var p := Probe.new()
	v.multimesh_attach_time_based_function(0.05, p.tick)
	await physics(3)
	v.multimesh_detach_all_time_based_functions()
	await idle()
	assert_eq(v.debug_get_timer_count(), 0, "detach-all cleared the timer")
	var after := p.fires
	await physics(10)
	assert_eq(p.fires, after, "no fire after detach-all")


func test_targeted_detach_cancels_one_shot() -> void:
	var v := _still()
	var p := Probe.new()
	v.multimesh_attach_time_based_function(0.05, p.tick)
	await _until(p, 1)
	var at_detach := p.fires
	v.multimesh_detach_time_based_function(p.tick)
	await idle()
	await physics(12)
	assert_eq(v.debug_get_timer_count(), 0, "targeted detach removed the timer")
	assert_eq(p.fires, at_detach, "no fire after targeted detach")


func test_targeted_detach_stops_repeat() -> void:
	var v := _still()
	var p := Probe.new()
	v.multimesh_attach_time_based_function(0.05, p.tick, true)
	await _until(p, 2)
	assert_gte(p.fires, 2, "repeating timer fired repeatedly")
	v.multimesh_detach_time_based_function(p.tick)
	var r := p.fires
	await idle()
	await physics(20)
	assert_eq(p.fires, r, "repeat stops after targeted detach")
	assert_eq(v.debug_get_timer_count(), 0, "timer count back to 0")


func test_detach_one_leaves_sibling() -> void:
	var v := _still()
	var keep := Probe.new()
	var drop := Probe.new()
	v.multimesh_attach_time_based_function(0.05, keep.tick, true)
	v.multimesh_attach_time_based_function(0.05, drop.tick, true)
	await idle()
	assert_eq(v.debug_get_timer_count(), 2, "two timers attached")
	v.multimesh_detach_time_based_function(drop.tick)
	await idle()
	assert_eq(v.debug_get_timer_count(), 1, "one timer left after targeted detach")
	await _until(keep, 3)
	assert_gte(keep.fires, 3, "surviving timer keeps firing")


func test_reattach_after_detach() -> void:
	var v := _still()
	var p := Probe.new()
	v.multimesh_attach_time_based_function(0.05, p.tick, true)
	await _until(p, 1)
	v.multimesh_detach_time_based_function(p.tick)
	await idle()
	var base := p.fires
	v.multimesh_attach_time_based_function(0.05, p.tick, true)
	await _until(p, base + 1)
	assert_gt(p.fires, base, "re-attached timer fires again")
	v.multimesh_detach_all_time_based_functions()


func test_pool_reuse_neutrality() -> void:
	var v := _still()
	var p := Probe.new()
	v.multimesh_attach_time_based_function(0.05, p.tick, true)
	await _until(p, 1)
	v.clear_all_bullets()
	await idle()
	var vb := _still()
	assert_eq(vb.debug_get_timer_count(), 0, "new life has no inherited timers")
	var pb := Probe.new()
	vb.multimesh_attach_time_based_function(0.05, pb.tick, true)
	await _until(pb, 1)
	assert_gte(pb.fires, 1, "new life's own timer fires")
	var old := p.fires
	await physics(10)
	assert_eq(p.fires, old, "previous owner's timer is silent")
	vb.multimesh_detach_all_time_based_functions()


func test_cap_enforced_at_64() -> void:
	var v := _still()
	for i in 71:
		v.multimesh_attach_time_based_function(10.0, func() -> void: pass)
	await idle()
	assert_eq(v.debug_get_timer_count(), 64, "cap is 64")
	assert_eq(expect_errors_containing("timer limit (64 per multimesh) reached"), 7, "every over-cap attach fails loud")
	v.multimesh_detach_all_time_based_functions()
	await idle()
	assert_eq(v.debug_get_timer_count(), 0, "detach-all empties the list")
