extends BlastTest
## Open-addressed (bullet, target) key set behind collision_dedup_by_object,
## exercised through debug bindings on an idle volley.
## D1 a grow must rehash (not wipe) stored pairs. D2 keys are 64-bit (no
## birthday collisions). D3 a cold table shrinks back, a hot one keeps its
## capacity.

var v: DirectionalBullets2D


func before_each() -> void:
	await super()
	var d := H.make_directional_data(1, 100.0, 30.0)
	d.transforms = [Transform2D()]
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 0
	v = factory.spawn_controllable_directional_bullets(d)


func _count_probes(n: int, target_base: int, expected: bool) -> int:
	var hits := 0
	for i in n:
		if v.debug_dedup_probe(i, target_base + i) == expected:
			hits += 1
	return hits


func test_pairs_survive_first_grow() -> void:
	v.debug_dedup_reset()
	for i in 120:
		v.debug_dedup_mark(i, 100000 + i)
	var st: Dictionary = v.debug_dedup_stats()
	assert_eq(int(st.get("used", -1)), 120, "used counts every distinct pair")
	assert_gte(int(st.get("capacity", 0)), 256, "table grew past the 64-slot start")
	assert_eq(_count_probes(120, 100000, true), 120, "all pairs still deduped after grow")
	assert_eq(_count_probes(120, 200000, false), 120, "unseen pairs probe false")


func test_pairs_survive_repeated_grows() -> void:
	v.debug_dedup_reset()
	for i in 2000:
		v.debug_dedup_mark(i, 300000 + i)
	assert_eq(int(v.debug_dedup_stats().get("used", -1)), 2000, "used is 2000")
	assert_eq(_count_probes(2000, 300000, true), 2000, "all 2000 pairs survive repeated grows")
	assert_eq(_count_probes(2000, 400000, false), 2000, "2000 unseen pairs stay false")


func test_64bit_key_has_no_birthday_collision() -> void:
	var found: Dictionary = v.debug_dedup_find_collision(300000)
	assert_false(found.get("collided", true), "no key collision in 300k probes")


func test_cold_shrinks_hot_keeps_capacity() -> void:
	v.debug_dedup_reset()
	for i in 5000:
		v.debug_dedup_mark(i, 500000 + i)
	var big: Dictionary = v.debug_dedup_stats()
	assert_gte(int(big.get("capacity", 0)), 16384, "5000 pairs grow the table")
	assert_eq(int(big.get("used", -1)), 5000)
	v.debug_dedup_reset()
	var hot: Dictionary = v.debug_dedup_stats()
	assert_eq(int(hot.get("used", -1)), 0, "hot reset clears used")
	assert_gte(int(hot.get("capacity", 0)), 16384, "hot reset keeps capacity")
	v.debug_dedup_reset()
	assert_lte(int(v.debug_dedup_stats().get("capacity", 0)), 1024, "cold reset shrinks the table")
	v.debug_dedup_mark(7, 777)
	assert_true(v.debug_dedup_probe(7, 777), "table works after shrink")
	assert_false(v.debug_dedup_probe(7, 778), "unseen pair false after shrink")


func test_live_volley_unaffected() -> void:
	v.debug_dedup_mark(1, 2)
	v.debug_dedup_reset()
	assert_true(v.get_collision_dedup_by_object(), "dedup mode untouched by debug traffic")
	assert_eq(int(v.debug_dedup_stats().get("used", -1)), 0, "table idle after reset")
	assert_eq(int(v.debug_get_volley_info().get("amount_bullets", 0)), 1, "volley intact")
