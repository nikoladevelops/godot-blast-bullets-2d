extends BlastTest
## Shooting transitions report exactly once, and only inside the tree: the
## scene loader calls setters before _ready (which emits the start itself),
## so pre-tree setters must stay silent.


func _fresh() -> BulletSpawner2D:
	var sp := BulletSpawner2D.new()
	sp.set_spawn_data(H.make_volley_data(1, 0.0, 60.0))
	autofree(sp)
	watch_signals(sp)
	return sp


func test_pre_tree_setters_silent_then_load_emits_once() -> void:
	var sp := _fresh()
	sp.set_shooting_enabled(false)
	sp.set_shooting_enabled(true)
	sp.max_volleys = 0
	sp.max_volleys = -1
	assert_signal_emit_count(sp, "shooting_started", 0, "no started before the tree")
	assert_signal_emit_count(sp, "shooting_stopped", 0, "no stopped before the tree")
	sp.set_bullet_factory(factory)
	add_child(sp)
	await idle(5)
	assert_signal_emit_count(sp, "shooting_started", 1, "exactly one started across load")
	sp.set_shooting_enabled(false)
	assert_signal_emit_count(sp, "shooting_stopped", 1, "one stopped on disable")
	sp.set_shooting_enabled(true)
	assert_signal_emit_count(sp, "shooting_started", 2, "one more started on re-enable")


func test_pre_tree_cap_silent_tree_cap_reports() -> void:
	var sp := _fresh()
	sp.max_volleys = 0
	assert_signal_emit_count(sp, "shooting_stopped", 0, "pre-tree cap silent")
	sp.set_bullet_factory(factory)
	add_child(sp)
	await idle(1)
	var started_before: int = get_signal_emit_count(sp, "shooting_started")
	sp.max_volleys = -1
	assert_eq(get_signal_emit_count(sp, "shooting_started"), started_before + 1, "tree raise reports started")
	sp.max_volleys = 0
	assert_signal_emit_count(sp, "shooting_stopped", 1, "tree cap reports stopped")
