extends BlastTest
## Zero-cost preview pose: spin and rigid generator motion re-pose the layer
## NODES (O(1)) instead of rebuilding geometry or re-issuing draw commands.
## Regression pins for P1 (track spun twice), P2 (spin hid other changes),
## P3 (pose one frame behind), mirrored-generator pose, P8/P9 inspector.
##
## Preview state: a dead track (zero path width) drops the layer rings while
## the dots survive; seed setters rebuild the gizmo (scatter positions move).
##
## helper_outline_distribution only steers corner-anchored apportionment, so
## the inspector hides it for smooth loops and shows it for corner shapes.

const HOLDER := "~BlastBulletsPatternPreview"
const SMOOTH := [
	BulletSpawner2D.PATTERN_FROM_HELPER_CIRCLE,
	BulletSpawner2D.PATTERN_FROM_HELPER_ELLIPSE,
	BulletSpawner2D.PATTERN_FROM_HELPER_HEART,
	BulletSpawner2D.PATTERN_FROM_HELPER_FLOWER,
	BulletSpawner2D.PATTERN_FROM_HELPER_ROSE,
	BulletSpawner2D.PATTERN_FROM_HELPER_LISSAJOUS,
]


const CORNER := [
	BulletSpawner2D.PATTERN_FROM_HELPER_RECTANGLE,
	BulletSpawner2D.PATTERN_FROM_HELPER_SQUARE,
	BulletSpawner2D.PATTERN_FROM_HELPER_POLYGON,
	BulletSpawner2D.PATTERN_FROM_HELPER_TRIANGLE,
	BulletSpawner2D.PATTERN_FROM_HELPER_TRAPEZOID,
	BulletSpawner2D.PATTERN_FROM_HELPER_DIAMOND,
	BulletSpawner2D.PATTERN_FROM_HELPER_STAR,
]


func _layers(n: Node) -> Array:
	var holder: Node = n.get_node_or_null(HOLDER)
	if holder == null:
		return []
	return [holder.get_node_or_null("Dots"), holder.get_node_or_null("Arrows")]


func _seg_dist(p: Vector2, a: Vector2, b: Vector2) -> float:
	var ab := b - a
	var denom := ab.length_squared()
	if denom <= 0.00000001:
		return p.distance_to(a)
	var t: float = clampf((p - a).dot(ab) / denom, 0.0, 1.0)
	return p.distance_to(a + ab * t)


func _max_dev(dots: PackedVector2Array, track: PackedVector2Array) -> float:
	var worst := 0.0
	for d in dots:
		var best := 1e30
		for k in range(track.size() - 1):
			if track[k].is_finite() and track[k + 1].is_finite():
				best = minf(best, _seg_dist(d, track[k], track[k + 1]))
		if best < 1e29:
			worst = maxf(worst, best)
	return worst


func _spinning_preview(src: int, n: int) -> BulletSpawner2D:
	var sp := make_preview_spawner(src, n)
	sp.position = Vector2(400, 300)
	sp.spin_enabled = true
	sp.spin_speed_deg_per_sec = 90.0
	return sp


func test_spin_never_rebuilds_or_redraws() -> void:
	var sp := _spinning_preview(BulletSpawner2D.PATTERN_FROM_HELPER_HEART, 1500)
	await idle(4)
	var s0: Dictionary = sp.debug_get_preview_stats()
	assert_eq(int(s0["dots_draws"]), 1, "preview drew once")
	await idle(30)
	var s1: Dictionary = sp.debug_get_preview_stats()
	assert_eq(int(s1["rebuilds"]), int(s0["rebuilds"]), "30 spinning frames: zero rebuilds")
	assert_eq(int(s1["dots_draws"]), int(s0["dots_draws"]), "30 spinning frames: zero dot redraws")
	assert_eq(int(s1["arrows_draws"]), int(s0["arrows_draws"]), "30 spinning frames: zero arrow redraws")
	var dots: Node2D = _layers(sp)[0]
	assert_almost_eq(angle_difference(dots.rotation, deg_to_rad(sp.get_spin_angle_deg())), 0.0, 0.0001, "layer pose IS the current spin (P3: no one-frame lag)")


func test_pose_matches_the_volley() -> void:
	var sp := _spinning_preview(BulletSpawner2D.PATTERN_FROM_HELPER_STAR, 10)
	await idle(20)
	var layer: Node2D = _layers(sp)[0]
	var shots: Array = sp.collect_spawn_transforms()
	var dots: PackedVector2Array = sp.debug_get_preview_dot_points()
	assert_eq(dots.size(), shots.size())
	for i in dots.size():
		var drawn: Vector2 = layer.get_global_transform() * dots[i]
		assert_almost_eq(drawn, (shots[i] as Transform2D).origin, Vector2(0.05, 0.05), "dot %d drawn where bullet %d spawns" % [i, i])


func test_mirrored_generator_pose_follows_global_spin() -> void:
	var sp := _spinning_preview(BulletSpawner2D.PATTERN_FROM_HELPER_STAR, 10)
	sp.scale = Vector2(-1, 1)
	await idle(20)
	var layer: Node2D = _layers(sp)[0]
	var shots: Array = sp.collect_spawn_transforms()
	var dots: PackedVector2Array = sp.debug_get_preview_dot_points()
	for i in dots.size():
		assert_almost_eq(layer.get_global_transform() * dots[i], (shots[i] as Transform2D).origin, Vector2(0.05, 0.05), "mirrored: dot %d on its bullet" % i)


func test_rigid_parent_move_never_rebuilds() -> void:
	var ship: Node2D = add(Node2D.new())
	var sp := BulletSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.set_homing_enabled(false)
	sp.set_spawn_data(H.make_volley_data(1, 0.0, 60.0))
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_RING
	sp.helper_bullets_amount = 500
	sp.show_pattern_preview = true
	sp.show_preview_during_runtime = true
	ship.add_child(sp)
	sp.set_bullet_factory(factory)
	await idle(4)
	var r0: int = sp.debug_get_preview_stats()["rebuilds"]
	for i in 20:
		ship.position += Vector2(7, 3)
		ship.rotation += 0.05
		await idle(1)
	assert_eq(int(sp.debug_get_preview_stats()["rebuilds"]), r0, "rigidly moving parent: no rebuild (canvas items ride along)")
	var layer: Node2D = _layers(sp)[0]
	var shots: Array = sp.collect_spawn_transforms()
	var dots: PackedVector2Array = sp.debug_get_preview_dot_points()
	assert_almost_eq(layer.get_global_transform() * dots[7], (shots[7] as Transform2D).origin, Vector2(0.05, 0.05), "still exactly where the shot spawns")


func test_world_anchored_source_still_rebuilds_on_move() -> void:
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_AIMED, 5)
	var tgt: Node2D = add(Node2D.new())
	tgt.position = Vector2(800, 0)
	sp.set_helper_aimed_target(tgt)
	await idle(4)
	var r0: int = sp.debug_get_preview_stats()["rebuilds"]
	sp.position += Vector2(0, 100)
	await idle(2)
	assert_gt(int(sp.debug_get_preview_stats()["rebuilds"]), r0, "aimed (target-dependent) pattern rebuilds when the spawner moves")


func test_spin_does_not_hide_target_moves() -> void:
	# P2: the spin branch used to return early from the dirty check.
	var sp := _spinning_preview(BulletSpawner2D.PATTERN_FROM_HELPER_AIMED, 5)
	var tgt: Node2D = add(Node2D.new())
	tgt.position = Vector2(800, 300)
	sp.set_helper_aimed_target(tgt)
	await idle(4)
	var r0: int = sp.debug_get_preview_stats()["rebuilds"]
	tgt.position = Vector2(400, 900)
	await idle(2)
	assert_gt(int(sp.debug_get_preview_stats()["rebuilds"]), r0, "target move detected while spinning")


func test_rebuild_mid_spin_keeps_track_under_dots() -> void:
	# P1: a rebuild during spin used to spin the track at build time AND at
	# draw time. Both snapshots are now unspun holder-local data.
	var sp := _spinning_preview(BulletSpawner2D.PATTERN_FROM_HELPER_STAR, 10)
	sp.helper_star_points = 5
	await idle(23) # ~34 deg into the spin: not a multiple of the 72 deg symmetry
	sp.preview_dot_radius = sp.preview_dot_radius + 1.0 # forces a rebuild now
	await idle(1)
	assert_lt(_max_dev(sp.debug_get_preview_dot_points(), sp.debug_get_preview_track_points()), 1.0, "dots sit on the star outline after a mid-spin rebuild")


func test_inspector_refresh_signals() -> void:
	# P8: setters whose values gate other properties' visibility must emit
	# property_list_changed so the inspector refreshes.
	var sp := make_spawner()
	watch_signals(sp)
	sp.helper_flower_type = 1
	assert_signal_emit_count(sp, "property_list_changed", 1, "flower type refreshes the inspector")
	sp.homing_target_selection = 1
	assert_signal_emit_count(sp, "property_list_changed", 2, "target selection refreshes the inspector")
	sp.show_pattern_preview = true
	assert_signal_emit_count(sp, "property_list_changed", 3, "preview toggle refreshes the inspector")
	sp.show_preview_during_runtime = true
	assert_signal_emit_count(sp, "property_list_changed", 4, "runtime preview toggle refreshes the inspector")


func test_layer_start_offset_hidden_outside_outline_sources() -> void:
	# P9: missing `relevant &&` guard leaked the knob into every mode.
	var sp := make_spawner(null, BulletSpawner2D.PATTERN_FROM_HELPER_GRID)
	sp.helper_outline_placement = 1
	sp.helper_outline_layer_fill = 1
	assert_false(is_editor_visible(sp, &"helper_outline_layer_start_offset"), "hidden for GRID")
	sp.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_CIRCLE
	assert_true(is_editor_visible(sp, &"helper_outline_layer_start_offset"), "shown for an outline source in LAYERS + sequential")


func test_dead_track_drops_rings_keeps_dots() -> void:
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 24)
	sp.helper_ring_radius = 120.0
	sp.helper_outline_placement = 1 # LAYERS
	sp.helper_outline_layer_count = 3
	await idle(8)
	assert_eq(sp.debug_get_layer_rings().size(), 2, "two rings while live")
	assert_false(sp.debug_get_preview_dot_points().is_empty(), "dots present while live")
	sp.preview_path_width = 0.0
	await idle(8)
	assert_true(sp.debug_get_layer_rings().is_empty(), "rings dropped when the track dies")
	assert_false(sp.debug_get_preview_dot_points().is_empty(), "dots survive")


func test_scatter_seed_rebuilds_dots() -> void:
	var sp := make_preview_spawner(BulletSpawner2D.PATTERN_FROM_HELPER_SCATTER, 12)
	sp.helper_scatter_burst_radius = 130.0
	sp.helper_scatter_seed = 11
	await idle(8)
	var before := sp.debug_get_preview_dot_points()
	assert_eq(before.size(), 12)
	sp.helper_scatter_seed = 77
	await idle(8)
	var after := sp.debug_get_preview_dot_points()
	assert_eq(after.size(), 12)
	var moved := false
	for i in mini(before.size(), after.size()):
		if before[i].distance_to(after[i]) > 0.5:
			moved = true
	assert_true(moved, "seeded layout refreshed the dots")


func _visible_for(src: int) -> bool:
	var sp := make_spawner()
	sp.helper_outline_placement = 0 # ON_OUTLINE
	sp.pattern_source = src
	return is_editor_visible(sp, &"helper_outline_distribution")


func test_hidden_for_smooth_loops() -> void:
	for src in SMOOTH:
		assert_false(_visible_for(src), "distribution hidden for smooth source %d" % src)


func test_shown_for_corner_shapes() -> void:
	for src in CORNER:
		assert_true(_visible_for(src), "distribution shown for corner source %d" % src)


## Two spawners on ONE external generator each own their preview: turning
## one preview off (which deletes its holder) never deletes the other's,
## the other keeps spinning on live nodes, and each draws its own dots.
func test_spawners_sharing_a_generator_own_separate_previews() -> void:
	var gen: Node2D = add(Node2D.new())
	gen.position = Vector2(300, 200)
	var a := _spinning_preview(BulletSpawner2D.PATTERN_FROM_HELPER_STAR, 10)
	var b := _spinning_preview(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 6)
	a.set_transforms_generator(gen)
	b.set_transforms_generator(gen)
	await idle(4)
	assert_eq(a.debug_get_preview_dot_points().size(), 10, "A draws its 10 dots")
	assert_eq(b.debug_get_preview_dot_points().size(), 6, "B draws its own 6 dots")
	a.show_pattern_preview = false
	for k in 10:
		await idle(1)
	assert_eq(b.debug_get_preview_dot_points().size(), 6, "B's preview survives A turning its own off")
	var layer: Node2D = null
	for c in gen.get_children(true):
		if c.has_meta("_blast_preview_owner") and int(c.get_meta("_blast_preview_owner")) == b.get_instance_id():
			layer = c.get_node_or_null("Dots")
	assert_not_null(layer, "B's holder still sits under the generator")
	if layer != null:
		assert_almost_eq(angle_difference(layer.rotation, deg_to_rad(b.get_spin_angle_deg())), 0.0, 0.0001, "B keeps posing its live layer")


## A spawner without a runtime preview that enters the tree on a generator
## another spawner already previews never deletes that live holder (its
## stray cleanup only takes holders that are its own or nobody's).
func test_a_late_spawner_never_deletes_another_spawners_holder() -> void:
	var gen: Node2D = add(Node2D.new())
	var b := _spinning_preview(BulletSpawner2D.PATTERN_FROM_HELPER_RING, 6)
	b.set_transforms_generator(gen)
	await idle(4)
	var holder: Node = gen.get_node_or_null(HOLDER)
	assert_not_null(holder, "B's holder takes the plain name")
	var holder_id := holder.get_instance_id()
	var a := BulletSpawner2D.new()
	a.set_shooting_enabled(false)
	a.set_spawn_data(H.make_volley_data(1))
	a.set_transforms_generator_path(gen.get_path())
	add(a) # A's _ready runs its stray cleanup on the shared generator
	assert_true(is_instance_id_valid(holder_id), "B's live holder survives A's _ready")
	await idle(2)
	assert_eq(b.debug_get_preview_dot_points().size(), 6, "B still draws")


## A script that clears the external generator's children (the Children
## marker source invites it) frees the runtime preview holder with them: the
## spinning spawner and a muzzle-offset edit never touch the freed nodes,
## and the preview rebuilds.
func test_clearing_the_generator_children_never_touches_a_freed_preview() -> void:
	var gen: Node2D = add(Node2D.new())
	var sp := _spinning_preview(BulletSpawner2D.PATTERN_FROM_HELPER_STAR, 10)
	sp.set_transforms_generator(gen)
	await idle(4)
	assert_eq(sp.debug_get_preview_dot_points().size(), 10, "preview drawn under the generator")
	for c in gen.get_children(true):
		c.free()
	sp.spawn_position_offset = Vector2(5, 0)
	assert_eq(sp.debug_get_preview_dot_points().size(), 10, "the edit rebuilt the lost preview")
	for c in gen.get_children(true):
		c.free()
	await idle(3)
	assert_eq(sp.debug_get_preview_dot_points().size(), 10, "spinning rebuilt the lost preview")
	var stats: Dictionary = sp.debug_get_preview_stats()
	assert_gt(int(stats["dots_draws"]), 0, "stats read live layers")


## reset_spin_angle() re-poses the preview at once, like set_spin_angle_deg
## (a script reading the gizmo right after the call sees the new pose).
func test_reset_spin_angle_reposes_the_preview_at_once() -> void:
	var sp := make_preview_spawner()
	await idle(6)
	sp.set_spin_angle_deg(90.0)
	assert_almost_eq((_layers(sp)[0] as Node2D).rotation, PI / 2.0, 0.0001, "posed at 90 degrees")
	sp.reset_spin_angle()
	assert_almost_eq((_layers(sp)[0] as Node2D).rotation, 0.0, 0.0001, "back at 0 at once")


## show_pattern_preview switched on at runtime (runtime preview allowed)
## wakes the loop like show_preview_during_runtime does: marker moves keep
## refreshing the gizmo.
func test_show_pattern_preview_at_runtime_wakes_the_loop() -> void:
	var sp := make_spawner(H.make_volley_data(1, 0.0, 60.0))
	sp.show_pattern_preview = false
	sp.show_preview_during_runtime = true
	await idle(2)
	assert_false(sp.is_processing(), "nothing to do: asleep")
	sp.show_pattern_preview = true
	assert_true(sp.is_processing(), "the preview wakes the loop")


## show_preview_during_runtime switched off at runtime takes the preview down
## (the rebuild used to bail out before removing it: it stayed drawn).
func test_runtime_preview_off_takes_the_preview_down() -> void:
	var sp := make_preview_spawner()
	await idle(6)
	assert_eq(_layers(sp).size(), 2, "drawn")
	sp.show_preview_during_runtime = false
	assert_eq(_layers(sp), [], "gone at once")
	assert_eq(sp.debug_get_preview_dot_points().size(), 0, "no dots left to read")


## A spawner that leaves the tree takes its preview off an external
## generator (the holder under the generator stayed drawn); back in the tree
## it rebuilds it.
func test_leaving_the_tree_takes_the_preview_off_an_external_generator(how: String = use_parameters(["remove", "free"])) -> void:
	var gen: Node2D = add(Node2D.new())
	gen.position = Vector2(200, 0)
	var sp := make_preview_spawner()
	sp.set_transforms_generator(gen)
	await idle(6)
	assert_eq(_layers(gen).size(), 2, "drawn under the generator")
	if how == "remove":
		var parent := sp.get_parent()
		parent.remove_child(sp)
		await idle(1)
		assert_eq(_layers(gen), [], "taken down with the spawner")
		parent.add_child(sp)
		await idle(6)
		assert_eq(_layers(gen).size(), 2, "back in the tree: drawn again")
	else:
		sp.free()
		await idle(1)
		assert_eq(_layers(gen), [], "taken down with the spawner")


## A laser spawner's preview beams are as long in the world as its beams,
## under a scaled generator too (they used to be scaled with the holder).
func test_laser_preview_beams_keep_their_world_length_under_a_scaled_generator() -> void:
	var gen: Node2D = add(Node2D.new())
	gen.scale = Vector2(2, 2)
	var sp := LaserSpawner2D.new()
	sp.set_shooting_enabled(false)
	sp.laser_data = H.make_laser(0.0, 0.0, 0.0, 0.0, 200.0)
	sp.pattern_source = LaserSpawner2D.PATTERN_FROM_HELPER_RING
	sp.helper_bullets_amount = 4
	sp.show_pattern_preview = true
	sp.show_preview_during_runtime = true
	add(sp)
	sp.set_transforms_generator(gen)
	await idle(6)
	var arrows: Dictionary = sp.debug_get_preview_arrows()
	assert_almost_eq(float(arrows["beam_length"]) * 2.0, 200.0, 0.001, "holder-local length x the generator scale = the beam's 200 px")
	assert_almost_eq(float(arrows["beam_width"]) * 2.0, sp.laser_data.width, 0.001, "and its world width")
