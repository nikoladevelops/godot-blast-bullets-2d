extends SceneTree
## Dedup-table suite: the open-addressed (bullet, target) key set behind
## collision_dedup_by_object, exercised directly through debug bindings on an
## idle volley (no physics flakiness: the table logic is what is under test,
## the live overlap path is covered by test_volley_collision_dedup.gd).
##
## The bugs:
## D1 grow wipes the table. mark_collision_queued() grows the table at >= 50%
## load, but the grow path assigned a fresh zeroed vector WITHOUT rehashing
## the keys already stored. Every grow therefore forgot every pair queued so
## far in the window: with 33+ distinct (bullet, target) pairs in one drain
## window (a normal bullet-hell frame), the first 32 pairs stopped being
## deduped and multi-shape targets double-counted again.
## D2 32-bit keys. collision_dedup_key() folded (bullet, target) into 32 bits
## of entropy, so at 10k entries a birthday collision (~1%) silently merged
## two unrelated pairs and dropped a real hit. The key must be 64-bit.
## D3 unbounded retention. A volley that once saw a 10k-overlap spike kept a
## 32k-slot table forever (clear only zero-filled). A cold table must shrink
## back; a hot table must keep its capacity so sustained spikes do not
## regrow+rehash every frame.
##
## Covers: T1 120 distinct pairs survive the first grow, T2 2000 pairs survive
## several grows with zero false positives on 2000 unseen pairs, T3 no key
## collision in 300k probes, T4 cold table shrinks / hot table keeps capacity
## and stays functional, T5 live volley unaffected + no dangling.
##
## Run: godot --headless --path test_project --script tests/volley/test_volley_dedup_table.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

var failures := 0

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _data() -> DirectionalBulletsData2D:
	var d := DirectionalBulletsData2D.new()
	d.transforms = [Transform2D(0.0, Vector2.ZERO)]
	var s := BulletSpeedData2D.new()
	s.speed = 100.0
	s.max_speed = 3000.0
	d.all_bullet_speed_data = [s]
	d.max_life_time = 30.0
	d.texture_size = Vector2(16, 16)
	d.set_collision_layer_from_array([2])
	d.set_collision_mask_from_array([3])
	var c := CircleShape2D.new()
	c.radius = 6.0
	d.collision_shape = c
	d.bullet_max_collision_count = 0
	return d

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame
	var v: DirectionalBullets2D = factory.spawn_controllable_directional_bullets(_data())

	# ---------------------------------------------------------------
	printerr("DEDUP-TABLE T1 pairs survive the first grow (D1)")
	# 120 distinct pairs force the 64 -> 256 grow (at 32 used). A grow that
	# drops its contents forgets pairs 0..31: they probe false afterwards.
	v.debug_dedup_reset()
	for i in 120:
		v.debug_dedup_mark(i, 100000 + i)
	var stats_t1: Dictionary = v.debug_dedup_stats()
	_check(int(stats_t1.get("used", -1)) == 120, "T1 used counts every distinct pair (got %s)" % str(stats_t1.get("used", -1)))
	_check(int(stats_t1.get("capacity", 0)) >= 256, "T1 table grew past the 64-slot start (cap=%s)" % str(stats_t1.get("capacity", 0)))
	var lost_t1 := 0
	for i in 120:
		if not v.debug_dedup_probe(i, 100000 + i):
			lost_t1 += 1
	_check(lost_t1 == 0, "T1 all 120 pairs still deduped after grow (lost %d)" % lost_t1)
	var phantom_t1 := 0
	for i in 120:
		if v.debug_dedup_probe(i, 200000 + i):
			phantom_t1 += 1
	_check(phantom_t1 == 0, "T1 120 unseen pairs probe false (phantoms %d)" % phantom_t1)

	# ---------------------------------------------------------------
	printerr("DEDUP-TABLE T2 pairs survive repeated grows at scale (D1)")
	v.debug_dedup_reset()
	for i in 2000:
		v.debug_dedup_mark(i, 300000 + i)
	var stats_t2: Dictionary = v.debug_dedup_stats()
	_check(int(stats_t2.get("used", -1)) == 2000, "T2 used is 2000 (got %s)" % str(stats_t2.get("used", -1)))
	var lost_t2 := 0
	for i in 2000:
		if not v.debug_dedup_probe(i, 300000 + i):
			lost_t2 += 1
	_check(lost_t2 == 0, "T2 all 2000 pairs survive repeated grows (lost %d)" % lost_t2)
	var phantom_t2 := 0
	for i in 2000:
		if v.debug_dedup_probe(i, 400000 + i):
			phantom_t2 += 1
	_check(phantom_t2 == 0, "T2 2000 unseen pairs stay false (phantoms %d)" % phantom_t2)

	# ---------------------------------------------------------------
	printerr("DEDUP-TABLE T3 64-bit key resists birthday collisions (D2)")
	# 300k deterministic distinct pairs: a 32-bit key collides with near
	# certainty (~10 expected), a 64-bit key effectively never (~2e-9).
	var found: Dictionary = v.debug_dedup_find_collision(300000)
	_check(found.get("collided", true) == false, "T3 no key collision in 300k probes (probes=%s)" % str(found.get("probes", -1)))

	# ---------------------------------------------------------------
	printerr("DEDUP-TABLE T4 cold table shrinks, hot table keeps capacity (D3)")
	v.debug_dedup_reset()
	for i in 5000:
		v.debug_dedup_mark(i, 500000 + i)
	var big: Dictionary = v.debug_dedup_stats()
	_check(int(big.get("capacity", 0)) >= 16384, "T4 5000 pairs grow the table big (cap=%s)" % str(big.get("capacity", 0)))
	_check(int(big.get("used", -1)) == 5000, "T4 used is 5000 (got %s)" % str(big.get("used", -1)))
	# First reset drains a HOT window (5000 used): capacity must be kept so a
	# sustained spike does not regrow+rehash every frame.
	v.debug_dedup_reset()
	var hot: Dictionary = v.debug_dedup_stats()
	_check(int(hot.get("used", -1)) == 0, "T4 hot reset clears used (got %s)" % str(hot.get("used", -1)))
	_check(int(hot.get("capacity", 0)) >= 16384, "T4 hot reset keeps capacity (cap=%s)" % str(hot.get("capacity", 0)))
	# Second reset drains a COLD window (0 used): capacity must drop back so a
	# one-off spike does not pin 256KB on the volley forever.
	v.debug_dedup_reset()
	var cold: Dictionary = v.debug_dedup_stats()
	_check(int(cold.get("capacity", 0)) <= 1024, "T4 cold reset shrinks the table (cap=%s)" % str(cold.get("capacity", 0)))
	# Still functional after the shrink: mark/probe round-trip.
	v.debug_dedup_mark(7, 777)
	_check(v.debug_dedup_probe(7, 777) == true, "T4 table works after shrink")
	_check(v.debug_dedup_probe(7, 778) == false, "T4 unseen pair false after shrink")

	# ---------------------------------------------------------------
	printerr("DEDUP-TABLE T5 live volley unaffected by table poking")
	v.debug_dedup_reset()
	_check(v.get_collision_dedup_by_object() == true, "T5 dedup mode untouched by debug traffic")
	_check(int(v.debug_dedup_stats().get("used", -1)) == 0, "T5 table idle after reset")
	_check(v.debug_get_volley_info().get("amount_bullets", 0) == 1, "T5 volley intact")
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL DEDUP-TABLE TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
