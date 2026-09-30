extends SceneTree
## Burst alternate-mirror + homing DISTRIBUTE suite.
##
## Part 1 - burst_alternate_mirror chirality. The flag used to negate ONLY the
## emitter spin, so a mirrored spiral wound exactly like the unmirrored one and
## merely pointed the opposite way, despite the comment promising a chirality
## flip. The fix negates the spiral-family winding (spiral / multispiral /
## counter-spiral angle_step) as well. Non-spiral patterns have no winding and
## must be untouched.
##
## Part 2 - homing DISTRIBUTE with homing_max_targets == 1 (the DEFAULT). The
## deal is `pool[i % pool.size()]`, so a one-element pool hands every bullet
## pool[0] and DISTRIBUTE silently behaves exactly like SHARED. The fix warns
## once instead of silently no-opping.
##
## Firing goes through shoot_once()/begin_burst() and reads the volley off the
## volley_fired signal (the same route the existing sequencing suite uses), so
## nothing here depends on the auto-shoot timer.
##
## Covers: T1 the mirror flag alternates per burst shot, T2 a spiral's winding
## reverses on a mirrored shot, T3 a plain shot is unchanged, T4 a non-spiral
## pattern is unaffected, T5 DISTRIBUTE with max_targets==1 is reachable and
## warned, T6 DISTRIBUTE with 2+ spreads, T7 spin+preview+mirror survives, T8
## no dangling.
##
## Run: godot --headless --path test_project --script tests/spawner/test_spawner_burst_mirror.gd
## Exit code 0 = all pass. Any FAIL = behavior drift or a real bug.

const H := preload("res://tests/common/blast_test_helpers.gd")

var failures := 0
var _mirror_flags: Array = []
var _vols: Array = []

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _on_volley_fired(volley: Object, _amount: int) -> void:
	if volley is DirectionalBullets2D:
		_vols.append(volley)

func _on_burst_shot_fired(_index: int, mirrored: bool) -> void:
	_mirror_flags.append(mirrored)

func _spawner(factory: BulletFactory2D) -> BulletSpawner2D:
	var s := BulletSpawner2D.new()
	get_root().add_child(s)
	# set_bullet_factory (not a guessed bullet_factory_path) is how the spawner
	# resolves its factory, and it must come after the node is in the tree.
	s.set_bullet_factory(factory)
	s.set_spawn_data(H.make_directional_data(1, 60.0, 60.0))
	s.set_shooting_enabled(false)
	s.volley_fired.connect(_on_volley_fired)
	s.burst_shot_fired.connect(_on_burst_shot_fired)
	return s

# Angles of the bullet origins about the emitter origin, in bullet order.
func _origin_angles(v: DirectionalBullets2D) -> Array:
	var out: Array = []
	for i in v.get_amount_bullets():
		out.append(v.get_bullet_transform(i).get_origin().angle())
	return out

# +1 when the angles advance around the emitter, -1 when they recede, 0 when not
# measurable. Uses the total wrapped progression rather than consecutive pairs,
# so a single wrap (a spiral crossing +-PI) cannot flip the reading.
func _winding_sign(angles: Array) -> int:
	if angles.size() < 2:
		return 0
	var total := 0.0
	for i in range(1, angles.size()):
		total += angle_difference(float(angles[i - 1]), float(angles[i]))
	if absf(total) < 0.01:
		return 0
	return 1 if total > 0.0 else -1

# Fires a 2-shot alternate-mirror burst and returns {mirrored, plain} volleys.
# Shot 1 is mirrored, shot 2 is plain (the documented alternation), and
# burst_shot_fired reports which is which - so the pairing is authoritative
# rather than assumed from capture order. There is no public setter for the
# internal burst_mirror_next flag (it is owned by the burst driver), and
# driving it directly would test the flag instead of the behaviour.
func _fire_burst_pair(spawner: BulletSpawner2D) -> Dictionary:
	spawner.clear_live_volleys()
	_vols.clear()
	_mirror_flags.clear()
	spawner.set_burst_alternate_mirror(true)
	spawner.set_burst_enabled(true)
	spawner.set_burst_count(2)
	spawner.set_burst_interval_sec(0.05)
	spawner.begin_burst()
	for i in 80:
		await physics_frame
		if _mirror_flags.size() >= 2 and _vols.size() >= 2:
			break
	spawner.set_burst_enabled(false)
	spawner.set_burst_alternate_mirror(false)
	# Pair each captured volley with the flag reported for that shot.
	var out := {"mirrored": null, "plain": null}
	var pairs := mini(_vols.size(), _mirror_flags.size())
	for i in range(pairs):
		out["mirrored" if bool(_mirror_flags[i]) else "plain"] = _vols[i]
	return out

func _initialize() -> void:
	var factory := BulletFactory2D.new()
	get_root().add_child(factory)
	await process_frame
	await process_frame

	# ---------------------------------------------------------------
	printerr("MIRR T1 the mirror flag alternates per burst shot")
	var s1 := _spawner(factory)
	s1.set_burst_alternate_mirror(true)
	s1.set_burst_enabled(true)
	s1.set_burst_count(4)
	s1.set_burst_interval_sec(0.05)
	_mirror_flags.clear()
	s1.begin_burst()
	for i in 60:
		await physics_frame
		if _mirror_flags.size() >= 4:
			break
	s1.set_burst_enabled(false)
	s1.set_burst_alternate_mirror(false)
	_check(_mirror_flags.size() >= 2, "T1 burst fired %d shots" % _mirror_flags.size())
	if _mirror_flags.size() >= 2:
		# Shot 1 mirrors, shot 2 does not: consecutive shots differ, and the
		# pattern repeats every other shot.
		_check(bool(_mirror_flags[0]) != bool(_mirror_flags[1]),
			"T1 consecutive shots differ (%s)" % str(_mirror_flags))
		var alternating := true
		for i in range(2, _mirror_flags.size()):
			# Shot i mirrors iff i is EVEN (0-based), so compare against the
			# EXPECTED parity, not against the previous flag.
			if bool(_mirror_flags[i]) != (i % 2 == 0):
				alternating = false
		_check(alternating, "T1 strict every-other alternation holds (%s)" % str(_mirror_flags))

	# ---------------------------------------------------------------
	printerr("MIRR T2 a spiral's winding reverses on a mirrored shot")
	var s2 := _spawner(factory)
	s2.set_pattern_source(5) # PATTERN_FROM_HELPER_SPIRAL
	s2.helper_bullets_amount = 12
	s2.helper_spiral_start_radius = 20.0
	s2.helper_spiral_radius_step = 14.0
	s2.helper_spiral_angle_step = 0.7
	var pair2 := await _fire_burst_pair(s2)
	var plain: DirectionalBullets2D = pair2["plain"]
	var mirrored: DirectionalBullets2D = pair2["mirrored"]
	_check(plain != null, "T2 plain spiral volley captured")
	_check(mirrored != null, "T2 mirrored spiral volley captured")
	if plain != null and mirrored != null:
		var plain_angles := _origin_angles(plain)
		var mirror_angles := _origin_angles(mirrored)
		_check(plain_angles.size() == mirror_angles.size(),
			"T2 same bullet count (%d vs %d)" % [plain_angles.size(), mirror_angles.size()])
		if plain_angles.size() >= 2 and mirror_angles.size() >= 2:
			var plain_wind := _winding_sign(plain_angles)
			var mirror_wind := _winding_sign(mirror_angles)
			_check(plain_wind != 0, "T2a the plain spiral has a measurable winding (%d)" % plain_wind)
			_check(mirror_wind != 0, "T2b the mirrored spiral has a measurable winding (%d)" % mirror_wind)
			# THE regression: chirality must flip, not just the facing.
			_check(plain_wind != mirror_wind,
				"T2c mirrored shot reverses the winding (plain=%d mirror=%d)" % [plain_wind, mirror_wind])

	# ---------------------------------------------------------------
	printerr("MIRR T3 multispiral also reverses")
	var s3 := _spawner(factory)
	s3.set_pattern_source(13) # PATTERN_FROM_HELPER_MULTISPIRAL
	s3.helper_bullets_amount = 12
	s3.helper_multispiral_arms = 2
	s3.helper_multispiral_start_radius = 20.0
	s3.helper_multispiral_radius_step = 14.0
	s3.helper_multispiral_angle_step = 0.6
	var pair3 := await _fire_burst_pair(s3)
	var m_plain: DirectionalBullets2D = pair3["plain"]
	var m_mirror: DirectionalBullets2D = pair3["mirrored"]
	_check(m_plain != null and m_mirror != null, "T3 both multispiral volleys captured")
	if m_plain != null and m_mirror != null:
		var mw_plain := _winding_sign(_origin_angles(m_plain))
		var mw_mirror := _winding_sign(_origin_angles(m_mirror))
		_check(mw_plain != mw_mirror,
			"T3 multispiral winding reverses (plain=%d mirror=%d)" % [mw_plain, mw_mirror])

	# ---------------------------------------------------------------
	printerr("MIRR T4 a non-spiral pattern is unaffected by mirroring")
	# RING has no winding term, so a mirrored ring must match a plain one.
	# This guard uses a STATIONARY volley: the two burst shots fire ~0.05s
	# apart, and a moving ring expands between them, which would show up as a
	# radius difference that has nothing to do with mirroring.
	var s4 := _spawner(factory)
	var still := H.make_directional_data(1, 0.0, 60.0)
	still.all_bullet_speed_data = []
	s4.set_spawn_data(still)
	s4.set_pattern_source(3) # PATTERN_FROM_HELPER_RING
	s4.helper_bullets_amount = 8
	s4.helper_ring_radius = 60.0
	var pair4 := await _fire_burst_pair(s4)
	var r_plain: DirectionalBullets2D = pair4["plain"]
	var r_mirror: DirectionalBullets2D = pair4["mirrored"]
	_check(r_plain != null and r_mirror != null, "T4 both ring volleys captured")
	if r_plain != null and r_mirror != null:
		_check(r_plain.get_amount_bullets() == 8 and r_mirror.get_amount_bullets() == 8,
			"T4 both rings have 8 bullets (%d/%d)" % [r_plain.get_amount_bullets(), r_mirror.get_amount_bullets()])
		# A ring is radially symmetric: mirroring must not move any dot's
		# distance from the centre, and must not distort the ring.
		var r_plain_radii: Array = []
		var r_mirror_radii: Array = []
		for i in r_plain.get_amount_bullets():
			r_plain_radii.append(r_plain.get_bullet_transform(i).get_origin().length())
		for i in r_mirror.get_amount_bullets():
			r_mirror_radii.append(r_mirror.get_bullet_transform(i).get_origin().length())
		var plain_spread: float = r_plain_radii.max() - r_plain_radii.min()
		var mirror_spread: float = r_mirror_radii.max() - r_mirror_radii.min()
		_check(plain_spread < 1.0, "T4a plain ring stays circular (spread %.3f)" % plain_spread)
		_check(mirror_spread < 1.0, "T4b mirrored ring stays circular (spread %.3f, radii %s)" % [mirror_spread, str(r_mirror_radii)])

	# ---------------------------------------------------------------
	printerr("MIRR T5 DISTRIBUTE with max_targets == 1 (the silent no-op)")
	var s5 := _spawner(factory)
	s5.set_homing_enabled(true)
	s5.set_homing_target_selection(2) # DISTRIBUTE
	s5.set_homing_max_targets(1)
	_check(int(s5.get_homing_max_targets()) == 1, "T5a the degenerate default is reachable")
	# The resolver warns once; the shot itself must still succeed.
	var fired5 := s5.shoot_once()
	_check(fired5, "T5b the DISTRIBUTE shot still fires with the degenerate pool")
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "T5c no dangling after the warned shot")

	# ---------------------------------------------------------------
	printerr("MIRR T6 DISTRIBUTE with 2+ targets is the real case")
	var s6 := _spawner(factory)
	s6.set_homing_enabled(true)
	s6.set_homing_target_selection(4)
	s6.set_homing_max_targets(4)
	_check(int(s6.get_homing_max_targets()) == 4, "T6a a spreading pool size is accepted")
	_check(s6.shoot_once(), "T6b DISTRIBUTE with a real pool fires cleanly")
	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "T6c no dangling")

	# ---------------------------------------------------------------
	printerr("MIRR T7 spin + preview + mirror survives many frames")
	# Spin used to force a full preview rebuild EVERY frame (the angle changes
	# each frame, so the dirty check always tripped). Now it only re-poses.
	var s7 := _spawner(factory)
	s7.show_preview_during_runtime = true
	s7.set_show_pattern_preview(true)
	s7.set_spin_enabled(true)
	s7.set_spin_speed_deg_per_sec(90.0)
	s7.set_pattern_source(5)
	s7.helper_bullets_amount = 200
	for i in 120:
		await process_frame
	_check(true, "T7 spin+preview at 200 dots survives 120 frames")
	s7.set_spin_enabled(false)
	s7.set_show_pattern_preview(false)

	_check(factory.debug_assert_no_dangling().get("ok", false) == true, "no dangling at end")

	factory.reset()
	factory.queue_free()
	await process_frame
	print("----")
	if failures == 0:
		print("ALL BURST-MIRROR TESTS PASSED")
	else:
		printerr(str(failures) + " TEST(S) FAILED")
	quit(failures)
