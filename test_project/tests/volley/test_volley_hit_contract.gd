extends BlastTest
## Hit contract (REAL physics). area_entered / body_entered fire LIVE from the
## factory tick, at the start of the volley's tick (before this frame's
## move), with the bullet still alive: every runtime read in the handler
## (status, hit count, custom data, attachment, transform) shows the bullet
## exactly as it hit. Only AFTER the handler does the plugin decide: if the
## bullet is still the same live bullet and its hit count still reaches the
## max, it dies (On Destroy fires once, at the impact pose); otherwise it
## lives (On Hit). Handlers own whatever they change: a heal vetoes the kill,
## a handler disable/clear ends the record, a freed target or a dying volley
## reports nothing more, a pause parks the remaining records.
## Walls sit on layer value 4 at x = 200; bullets fly +X at 300 px/s.

const DESTROY := BulletEffectLayerData2D.EFFECT_ON_DESTROY
const HIT := BulletEffectLayerData2D.EFFECT_ON_HIT

var hits: Array = [] # Dictionaries recorded inside the handler
var on_hit: Callable = Callable() # optional extra handler action(volley, idx, body)
var start_pose: Dictionary = {} # physics frame -> {bullet index: origin} at frame start
var watched: BulletVolley2D = null


func _hit_data(ys: Array, max_hits: int = 1, lifetime: float = 8.0) -> BulletVolleyData2D:
	var d := H.make_volley_data(ys.size(), 300.0, lifetime)
	var arr: Array = []
	for y in ys:
		arr.append(Transform2D(0.0, Vector2(0, y)))
	d.transforms = arr
	d.monitorable = true
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = max_hits
	return d


func _layers() -> Array:
	return [H.make_effect_layer(DESTROY, 4), H.make_effect_layer(HIT, 4)]


func _record(body: Object, volley: BulletVolley2D, idx: int, kind: String) -> void:
	var att = volley.bullet_get_attachment(idx)
	hits.append({
		"kind": kind,
		"volley": volley,
		"index": idx,
		"body": body,
		"alive": volley.is_bullet_status_enabled(idx),
		"count": volley.get_bullet_collision_count(idx),
		"custom": volley.bullet_get_custom_data(idx),
		"shared_custom": volley.get_shared_bullets_custom_data(),
		"attached": att != null,
		"active": bool(volley.debug_get_volley_info().get("is_active", false)),
		"pooled": bool(volley.debug_get_volley_info().get("is_pooled", true)),
		"origin": volley.get_bullet_global_transform(idx).origin,
		"frame": Engine.get_physics_frames(),
	})
	if on_hit.is_valid():
		on_hit.call(volley, idx, body)


func _on_body(body: Object, volley: BulletVolley2D, idx: int) -> void:
	_record(body, volley, idx, "body")


func _on_area(area: Object, volley: BulletVolley2D, idx: int) -> void:
	_record(area, volley, idx, "area")


func before_each() -> void:
	await super()
	hits.clear()
	on_hit = Callable()
	start_pose.clear()
	watched = null
	factory.body_entered.connect(_on_body)
	factory.area_entered.connect(_on_area)
	factory.debug_set_effect_log_enabled(true)
	make_wall(Vector2(200, 0))
	await physics()


func after_each() -> void:
	factory.debug_set_effect_log_enabled(false)
	await super()


## Steps physics frames until `count` hits were recorded, snapshotting the
## watched volley's poses at the start of every frame (the pose the physics
## server tested for this frame's overlap records).
func _wait_hits(count: int, frames := 90) -> void:
	for i in frames:
		await physics()
		if watched != null and is_instance_valid(watched):
			var poses := {}
			for b in watched.get_amount_bullets():
				poses[b] = watched.get_bullet_global_transform(b).origin
			start_pose[Engine.get_physics_frames()] = poses
		if hits.size() >= count:
			break


func _effects(trigger: int) -> Array:
	var out: Array = []
	for e in factory.debug_get_effect_log():
		if int(e["trigger"]) == trigger:
			out.append(e)
	return out


func test_killing_hit_handler_sees_a_live_bullet_for_the_first_and_last_bullet() -> void:
	var d := _hit_data([0.0, 40.0])
	var c0 := Resource.new()
	var c1 := Resource.new()
	var shared := Resource.new()
	d.all_bullets_custom_data = [c0, c1]
	d.shared_bullets_custom_data = shared
	d.shared_bullet_attachment = make_probe_scene()
	var v: BulletVolley2D = factory.spawn_volley(d)
	await _wait_hits(2)
	assert_eq(hits.size(), 2, "one killing hit per bullet")
	for h in hits:
		var i: int = h["index"]
		assert_true(h["alive"], "bullet %d alive inside its killing-hit handler" % i)
		assert_eq(h["count"], 1, "bullet %d hit count already counted" % i)
		assert_same(h["custom"], c0 if i == 0 else c1, "bullet %d carries its own custom data" % i)
		assert_same(h["shared_custom"], shared, "shared custom data readable")
		assert_true(h["attached"], "bullet %d still holds its attachment" % i)
		assert_true(h["active"], "volley active inside the handler")
		assert_false(h["pooled"], "volley not pooled inside the handler (even for the last bullet)")
	await physics()
	assert_false(v.is_bullet_status_enabled(0), "bullet 0 died after its handler")
	assert_false(v.is_bullet_status_enabled(1), "bullet 1 died after its handler")
	assert_true(bool(v.debug_get_volley_info().get("is_pooled", false)), "drained volley pooled after the last handler")
	assert_null(v.bullet_get_attachment(0), "attachment 0 released after the handler")
	assert_null(v.bullet_get_attachment(1), "attachment 1 released after the handler")


func test_killing_hit_detonates_once_at_the_impact_pose() -> void:
	var d := _hit_data([0.0])
	d.effect_layers = _layers()
	watched = factory.spawn_volley(d)
	factory.debug_clear_effect_log() # drop nothing (no spawn layer), keep it explicit
	await _wait_hits(1)
	assert_eq(hits.size(), 1, "one hit")
	var h: Dictionary = hits[0]
	var frame: int = h["frame"]
	assert_true(start_pose.has(frame), "frame-start pose captured")
	assert_eq(h["origin"], start_pose[frame][0], "handler sees the pose the physics server tested (before this frame's move)")
	await physics()
	var destroys := _effects(DESTROY)
	assert_eq(destroys.size(), 1, "On Destroy fired exactly once")
	assert_eq(destroys[0]["position"], h["origin"], "On Destroy at the impact pose")
	assert_eq(_effects(HIT).size(), 0, "a killing blow never sparks On Hit too")


func test_handler_heal_vetoes_the_kill() -> void:
	var d := _hit_data([0.0])
	d.effect_layers = _layers()
	on_hit = func(volley: BulletVolley2D, idx: int, _body): volley.set_bullet_collision_count(idx, 0)
	var v: BulletVolley2D = factory.spawn_volley(d)
	await _wait_hits(1)
	await physics()
	assert_true(v.is_bullet_status_enabled(0), "healed bullet survives its killing hit")
	assert_eq(v.get_bullet_collision_count(0), 0, "heal kept")
	assert_eq(_effects(DESTROY).size(), 0, "no detonation for a vetoed kill")
	assert_eq(_effects(HIT).size(), 1, "the survived hit sparks On Hit once")


func test_handler_disable_owns_the_bullet() -> void:
	var d := _hit_data([0.0, 300.0])
	d.effect_layers = _layers()
	on_hit = func(volley: BulletVolley2D, idx: int, _body): volley.disable_bullet(idx)
	var v: BulletVolley2D = factory.spawn_volley(d)
	await _wait_hits(1)
	await physics(2)
	assert_eq(hits.size(), 1, "one hit")
	assert_false(v.is_bullet_status_enabled(0), "handler disabled it")
	assert_true(v.is_bullet_status_enabled(1), "sibling untouched")
	assert_eq(_effects(DESTROY).size(), 0, "a silent handler disable fires no destroy effect")
	assert_eq(_effects(HIT).size(), 0, "nor a hit spark")
	expect_no_errors()


func test_handler_disabling_a_sibling_drops_its_queued_record() -> void:
	on_hit = func(volley: BulletVolley2D, idx: int, _body):
		volley.disable_bullet(1 - idx)
	var v: BulletVolley2D = factory.spawn_volley(_hit_data([0.0, 40.0]))
	await _wait_hits(1)
	await physics(3)
	assert_eq(hits.size(), 1, "the sibling's same-frame record is dropped (it was disabled before its turn)")
	assert_false(v.is_bullet_status_enabled(0), "both bullets gone")
	assert_false(v.is_bullet_status_enabled(1), "both bullets gone")


func test_handler_queue_free_stops_the_drain() -> void:
	on_hit = func(volley: BulletVolley2D, _idx: int, _body): volley.queue_free()
	var v: BulletVolley2D = factory.spawn_volley(_hit_data([0.0, 40.0]))
	await _wait_hits(1)
	assert_eq(hits.size(), 1, "a volley queued for deletion reports nothing more")
	await idle(2)
	assert_false(is_instance_valid(v), "freed at frame end")


func test_handler_free_volley_is_safe() -> void:
	on_hit = func(volley: BulletVolley2D, _idx: int, _body): volley.free()
	factory.spawn_volley(_hit_data([0.0, 40.0]))
	await _wait_hits(1)
	await physics(3)
	assert_eq(hits.size(), 1, "the drain stops at the freed volley")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "factory consistent")


func test_handler_freeing_the_target_drops_its_remaining_records() -> void:
	# Target body in front of the wall: both bullets overlap it on the same
	# frame. The first handler free()s it; the second record must be
	# dropped (no null-target hit, no ghost hit counted).
	var target := CharacterBody2D.new()
	target.name = "Target"
	target.position = Vector2(100, 0)
	target.collision_layer = 4
	var col := CollisionShape2D.new()
	var box := RectangleShape2D.new()
	box.size = Vector2(10, 400)
	col.shape = box
	target.add_child(col)
	add_child(target) # freed by the handler, never autofreed
	await physics()
	on_hit = func(_volley, _idx, body: Object):
		if is_instance_valid(target) and body == target:
			target.free()
	var v: BulletVolley2D = factory.spawn_volley(_hit_data([0.0, 40.0], 2))
	await _wait_hits(1)
	assert_eq(hits.size(), 1, "the second same-frame record against the freed target is dropped")
	var survivor: int = 1 - int(hits[0]["index"])
	assert_eq(v.get_bullet_collision_count(survivor), 0, "no ghost hit counted for the other bullet")
	assert_true(v.is_bullet_status_enabled(survivor), "other bullet keeps flying")
	expect_no_errors()


func test_max_one_reports_exactly_one_hit() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_hit_data([0.0], 1))
	await _wait_hits(1)
	await physics(10)
	assert_eq(hits.size(), 1, "max 1: exactly one hit")
	assert_eq(v.get_bullet_collision_count(0), 1, "count exactly 1")
	assert_false(v.is_bullet_status_enabled(0), "dead")


func test_max_two_counts_body_then_area_then_dies() -> void:
	make_area(Vector2(200, 0))
	await physics()
	var v: BulletVolley2D = factory.spawn_volley(_hit_data([0.0], 2))
	await _wait_hits(2)
	await physics(10)
	assert_eq(hits.size(), 2, "body + area overlap: exactly two hits")
	var kinds := []
	for h in hits:
		kinds.append(h["kind"])
	kinds.sort()
	assert_eq(kinds, ["area", "body"], "one body record and one area record")
	assert_true(hits[0]["alive"] and hits[1]["alive"], "both handlers saw it alive")
	assert_false(v.is_bullet_status_enabled(0), "second hit killed it")


func test_max_zero_reports_once_per_overlap_and_never_dies() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_hit_data([0.0], 0))
	await _wait_hits(1)
	await physics(30)
	assert_eq(hits.size(), 1, "one overlap, one report (the steady overlap never re-reports)")
	assert_true(v.is_bullet_status_enabled(0), "infinite bullet lives on")


func test_pause_from_handler_parks_the_rest_and_replays_once() -> void:
	on_hit = func(_volley, _idx, _body):
		if hits.size() == 1:
			factory.set_is_factory_processing_bullets(false)
	var v: BulletVolley2D = factory.spawn_volley(_hit_data([0.0, 40.0], 0))
	await _wait_hits(1)
	await physics(5)
	assert_eq(hits.size(), 1, "paused: the second same-frame record waits")
	var paused_pose: Vector2 = v.get_bullet_global_transform(1).origin
	await physics(3)
	assert_eq(v.get_bullet_global_transform(1).origin, paused_pose, "paused volley does not move")
	factory.set_is_factory_processing_bullets(true)
	await physics(3)
	assert_eq(hits.size(), 2, "the parked record replays exactly once on resume")


func test_parked_overlap_with_a_freed_target_is_skipped() -> void:
	var target := CharacterBody2D.new()
	target.position = Vector2(60, 0)
	target.collision_layer = 4
	var col := CollisionShape2D.new()
	col.shape = H.make_circle_shape(20.0)
	target.add_child(col)
	add_child(target)
	var d := _hit_data([0.0], 0)
	var v: BulletVolley2D = factory.spawn_volley(d)
	factory.set_is_factory_processing_bullets(false)
	v.teleport_bullet(0, Vector2(60, 0)) # overlap starts while paused (parked)
	await physics(3)
	target.free()
	factory.set_is_factory_processing_bullets(true)
	await physics(3)
	assert_eq(hits.size(), 0, "a parked overlap whose target died is dropped, never reported with a null target")
	assert_true(v.is_bullet_status_enabled(0), "no ghost hit was counted")
	assert_eq(v.get_bullet_collision_count(0), 0, "count untouched")


func test_spawner_owned_hits_route_live_to_the_spawner() -> void:
	var sp_hits: Array = []
	var sp := make_spawner(_hit_data([0.0], 1), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.body_entered.connect(func(_b, volley: BulletVolley2D, idx: int): sp_hits.append(volley.is_bullet_status_enabled(idx)))
	factory.body_entered.disconnect(_on_body)
	watch_signals(factory)
	assert_true(sp.shoot_once(), "fired")
	for i in 90:
		await physics()
		if not sp_hits.is_empty():
			break
	assert_eq(sp_hits, [true], "spawner handler saw the bullet alive")
	assert_signal_not_emitted(factory, "body_entered", "factory stays silent for spawner volleys")


func test_clear_active_bullets_inside_a_handler_clears_now() -> void:
	var d := _hit_data([0.0, 300.0], 0)
	d.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_ON_CLEAR, 4)]
	var cleared := []
	on_hit = func(_volley, _idx, _body): cleared.append(factory.clear_active_bullets())
	var v: BulletVolley2D = factory.spawn_volley(d)
	await _wait_hits(1)
	assert_eq(cleared, [2], "both live bullets cleared from inside the handler")
	assert_false(v.is_bullet_status_enabled(0), "cleared")
	assert_false(v.is_bullet_status_enabled(1), "cleared")
	assert_eq(_effects(BulletEffectLayerData2D.EFFECT_ON_CLEAR).size(), 2, "On Clear once per bullet")
	expect_no_errors()
