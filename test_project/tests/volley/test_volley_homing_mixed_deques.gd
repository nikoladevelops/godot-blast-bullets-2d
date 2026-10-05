extends BlastTest
## Mixed homing deques: a bullet with its own (per-bullet) targets steers by
## them even while the shared deque holds targets, so its reach belongs to
## its own deque: per-bullet auto-pop pops its own front, and the shared deque
## is never popped on its behalf (not even when the same node sits in both).

var events: Array = []


func _data(n: int, per_bullet_pop: bool, shared_pop: bool) -> BulletVolleyData2D:
	var d := H.make_volley_data(n, 0.0, 30.0)
	var arr: Array = []
	for i in n:
		arr.append(Transform2D())
	d.transforms = arr
	d.homing_distance_before_reached = 400.0 # (5, 0) is reached at once, (900, 0)+ never
	d.bullet_homing_auto_pop_after_target_reached = per_bullet_pop
	d.shared_homing_deque_auto_pop_after_target_reached = shared_pop
	return d


func _on_reached(_volley: BulletVolley2D, idx: int, _target: Object, pos: Vector2) -> void:
	events.append([idx, pos])


func before_each() -> void:
	await super()
	events.clear()


func test_per_bullet_auto_pop_works_while_the_shared_deque_has_targets() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(1, true, true))
	v.bullet_homing_target_reached.connect(_on_reached)
	v.shared_homing_deque_push_back_global_position_target(Vector2(5000, 0))
	v.bullet_homing_push_back_global_position_target(0, Vector2(5, 0))
	v.bullet_homing_push_back_global_position_target(0, Vector2(900, 0))
	await physics(3)
	assert_eq(events.size(), 1, "the own front is reached exactly once")
	assert_eq(v.bullet_homing_check_targets_amount(0), 1, "the reached own front was auto-popped")
	assert_almost_eq(v.bullet_get_current_homing_target(0), Vector2(900, 0), Vector2(0.01, 0.01), "the next own target is the front")
	assert_eq(v.shared_homing_deque_check_homing_targets_amount(), 1, "the shared deque is untouched")


func test_a_node_in_both_deques_never_pops_the_shared_copy_for_a_per_bullet_reach() -> void:
	var target := Node2D.new()
	target.position = Vector2(5, 0)
	add(target)
	var v: BulletVolley2D = factory.spawn_volley(_data(1, false, true))
	v.bullet_homing_target_reached.connect(_on_reached)
	v.shared_homing_deque_push_back_homing_targets_array([target])
	v.bullet_homing_push_back_node2d_target(0, target)
	await physics(3)
	assert_eq(events.size(), 1, "reached once, through the bullet's own deque")
	assert_eq(v.shared_homing_deque_check_homing_targets_amount(), 1, "the shared copy was never steered by this bullet: no shared pop")
	assert_eq(v.bullet_homing_check_targets_amount(0), 1, "per-bullet auto-pop is off: the own copy stays")


func test_shared_reach_still_pops_the_shared_deque_next_to_a_per_bullet_bullet() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(2, true, true))
	v.bullet_homing_target_reached.connect(_on_reached)
	v.shared_homing_deque_push_back_global_position_target(Vector2(5, 0))
	v.shared_homing_deque_push_back_global_position_target(Vector2(5000, 0))
	v.bullet_homing_push_back_global_position_target(0, Vector2(900, 0)) # bullet 0 steers by its own far target
	await physics(3)
	assert_eq(events.size(), 1, "only bullet 1 (shared) reached")
	assert_eq(events[0][0], 1, "the reach belongs to the shared-steered bullet")
	assert_eq(v.shared_homing_deque_check_homing_targets_amount(), 1, "one shared pop for bullet 1's reach")
	assert_eq(v.bullet_homing_check_targets_amount(0), 1, "bullet 0's own far target stays")
