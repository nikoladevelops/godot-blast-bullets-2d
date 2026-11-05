extends BlastTest
## Editing the spawner's spawn_data from script AFTER a shot reaches the next
## shot: the spawner caches a duplicate of the resource, and every in-place
## edit must invalidate it.

var fired: Array = []


func _on_fired(volley: BulletVolley2D, _count: int) -> void:
	fired.append(volley)


func before_each() -> void:
	await super()
	fired.clear()


func _shot_life_time(sp: BulletSpawner2D) -> float:
	assert_true(sp.shoot_once(), "shot fires")
	var v: BulletVolley2D = fired.back()
	return v.get_life_time_left()


func test_max_life_time_edit_reaches_the_next_shot() -> void:
	var data := H.make_volley_data(2, 0.0, 30.0)
	var sp := make_spawner(data, BulletSpawner2D.PATTERN_FROM_HELPER_RING, 2)
	sp.volley_fired.connect(_on_fired)
	await idle(1)
	assert_almost_eq(_shot_life_time(sp), 30.0, 0.5, "first shot uses the authored life time")
	sp.spawn_data.max_life_time = 7.0
	assert_almost_eq(_shot_life_time(sp), 7.0, 0.5, "the edited life time reaches the next shot")


func test_accepted_setters_emit_changed_and_rejected_ones_do_not() -> void:
	var data := H.make_volley_data(2, 0.0, 30.0)
	watch_signals(data)
	data.max_life_time = 5.0 # float
	data.is_life_time_infinite = true # bool
	data.z_index = 3 # int
	data.shared_bullet_attachment_offset = Vector2(1, 2) # Vector2
	data.material = CanvasItemMaterial.new() # Resource
	assert_signal_emit_count(data, "changed", 5, "one changed per accepted edit")
	data.shared_bullet_attachment_offset = Vector2(NAN, 0)
	expect_error_sequence(["BulletVolleyData2D: shared_bullet_attachment_offset must be finite, keeping the old value."])
	assert_signal_emit_count(data, "changed", 5, "a rejected edit emits nothing")


## A spawn data resource's LAST reference can drop inside `set_spawn_data`:
## its NOTIFICATION_PREDELETE is user code that may free the spawner, so the
## setter must stop before reading `this` again (ASan: heap-use-after-free).
func test_a_spawn_data_predelede_freeing_the_spawner_mid_setter_is_survived() -> void:
	var sp := make_spawner()
	var probe := FreeSpawnerOnPredeleteSpawnData2D.new()
	sp.spawn_data = probe
	probe = null
	FreeSpawnerOnPredeleteSpawnData2D.fired = 0
	FreeSpawnerOnPredeleteSpawnData2D.victim = sp
	check_factory_after = false
	sp.spawn_data = null
	assert_gt(FreeSpawnerOnPredeleteSpawnData2D.fired, 0, "the last Ref dropped inside the setter")
	assert_false(is_instance_valid(sp), "the PREDELETE handler freed the spawner")
	await idle(1)
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "nothing dangles")
	assert_not_null(quick_volley(2), "the factory still spawns")
