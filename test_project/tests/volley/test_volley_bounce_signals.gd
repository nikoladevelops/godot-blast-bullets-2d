extends BlastTest
## Bounce signal contract with REAL physics. A bounce fires bounce_body_entered
## / bounce_area_entered with the bullet alive. When the bounce also consumes
## the hit (bounce_hit_consumed), the normal hit path runs AFTER the bounce
## handler, and it must re-validate first: a handler that disabled, cleared or
## pooled the bullet ends the record, so no body_entered/area_entered ever
## fires for a dead bullet. Free bounces never reach the hit path at all.
## Bounce walls live on layer 4 (value 8), the bullet flies +X at 300 px/s.

var bounce_hits: Array = [] # [volley, index, alive_at_emit]
var body_hits: Array = [] # [volley, index, alive_at_emit]
var disable_in_bounce := false
var free_volley_in_bounce := false


func _bounce_data(consumed: bool, max_hits: int = 3) -> BulletVolleyData2D:
	# Two bullets: bullet 0 hits the wall, bullet 1 flies past it (y = 300),
	# so disabling bullet 0 never drains the volley (a drained volley resets
	# its bounce config, which would hide a missing re-validation).
	var d := H.make_volley_data(2, 300.0, 8.0)
	d.transforms = [Transform2D(0.0, Vector2(0, 0)), Transform2D(0.0, Vector2(0, 300))]
	d.monitorable = true
	d.set_collision_mask_from_array([4])
	d.set_bounce_mask_from_array([4])
	d.bounce_hit_consumed = consumed
	d.bullet_max_collision_count = max_hits
	d.collision_shape = H.make_circle_shape(6.0)
	return d


func _make_bounce_wall(pos: Vector2) -> StaticBody2D:
	return make_wall(pos, Vector2(20, 400), 8, 2)


func _on_bounce(_body: Object, volley: BulletVolley2D, idx: int) -> void:
	bounce_hits.append([volley, idx, volley.is_bullet_status_enabled(idx)])
	if disable_in_bounce:
		volley.disable_bullet(idx)
	if free_volley_in_bounce:
		volley.queue_free()


func _on_body(_body: Object, volley: BulletVolley2D, idx: int) -> void:
	body_hits.append([volley, idx, volley.is_bullet_status_enabled(idx)])


func before_each() -> void:
	await super()
	bounce_hits.clear()
	body_hits.clear()
	disable_in_bounce = false
	free_volley_in_bounce = false
	factory.bounce_body_entered.connect(_on_bounce)
	factory.body_entered.connect(_on_body)
	_make_bounce_wall(Vector2(200, 0))
	await physics()


func _wait_for_bounce(frames := 90) -> void:
	for i in frames:
		await physics()
		if not bounce_hits.is_empty():
			break


func test_consumed_bounce_then_handler_disable_never_reports_a_hit() -> void:
	disable_in_bounce = true
	var v: BulletVolley2D = factory.spawn_volley(_bounce_data(true))
	await _wait_for_bounce()
	await physics(3)
	assert_eq(bounce_hits.size(), 1, "exactly one bounce")
	assert_true(bounce_hits[0][2], "bounce handler sees the bullet alive")
	assert_eq(body_hits.size(), 0, "the handler disabled the bullet: no hit signal for a dead bullet")
	assert_false(v.is_bullet_status_enabled(0), "bullet stays disabled")


func test_consumed_bounce_reports_bounce_then_hit_once_each() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_bounce_data(true))
	await _wait_for_bounce()
	await physics(3)
	assert_eq(bounce_hits.size(), 1, "one bounce")
	assert_eq(body_hits.size(), 1, "the consumed bounce counts exactly one hit")
	assert_true(body_hits[0][2], "hit handler sees the bullet alive")
	assert_eq(v.get_bullet_collision_count(0), 1, "one hit consumed")


func test_free_bounce_never_reaches_the_hit_path() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_bounce_data(false))
	await _wait_for_bounce()
	await physics(3)
	assert_eq(bounce_hits.size(), 1, "one bounce")
	assert_eq(body_hits.size(), 0, "free bounce: no hit signal")
	assert_eq(v.get_bullet_collision_count(0), 0, "free bounce consumes nothing")
	assert_true(v.is_bullet_status_enabled(0), "bullet keeps flying")


func test_consumed_bounce_then_handler_queue_free_stops_the_record() -> void:
	free_volley_in_bounce = true
	var v: BulletVolley2D = factory.spawn_volley(_bounce_data(true))
	await _wait_for_bounce()
	assert_eq(bounce_hits.size(), 1, "one bounce")
	assert_eq(body_hits.size(), 0, "a volley queued for deletion reports nothing more")
	await idle(2)
	assert_false(is_instance_valid(v), "volley freed at frame end")
