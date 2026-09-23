extends SceneTree

# Headless crash-fuzz for every BulletFactory2D generator.
# Usage:
#   for c in counts degenerate nan twist_extreme slots_offsets scales edge_image side_spread_skip; do
#     CASE=$c godot --headless --path test_project --script test_edge_fuzz.gd
#   done
# Exit code = failures in the group. A crash kills only its own group process,
# and the missing GROUP_DONE line attributes it. Reaching GROUP_DONE proves
# survival; the PASS lines prove the documented error/degenerate behavior.

var failures := 0
var marker := Transform2D.IDENTITY

func _check(cond: bool, label: String) -> void:
	if cond:
		print("PASS  ", label)
	else:
		failures += 1
		printerr("FAIL  ", label)

func _finite_volley(v: Array) -> bool:
	for t in v:
		var tr: Transform2D = t
		if not tr.is_finite():
			return false
	return true

# First-class references to the static generators (verified dispatch: callv
# works even though is_valid() reports false on native statics).
var _callables: Array = []

func _funcs() -> Array:
	if _callables.is_empty():
		_callables = [
			BulletFactory2D.helper_generate_transforms_grid,
			BulletFactory2D.helper_generate_transforms_ring,
			BulletFactory2D.helper_generate_transforms_fan,
			BulletFactory2D.helper_generate_transforms_spiral,
			BulletFactory2D.helper_generate_transforms_line,
			BulletFactory2D.helper_generate_transforms_aimed,
			BulletFactory2D.helper_generate_transforms_flower,
			BulletFactory2D.helper_generate_transforms_ellipse,
			BulletFactory2D.helper_generate_transforms_rain,
			BulletFactory2D.helper_generate_transforms_scatter,
			BulletFactory2D.helper_generate_transforms_star_polygon,
			BulletFactory2D.helper_generate_transforms_multispiral,
			BulletFactory2D.helper_generate_transforms_cross,
			BulletFactory2D.helper_generate_transforms_star,
			BulletFactory2D.helper_generate_transforms_heart,
			BulletFactory2D.helper_generate_transforms_wave,
			BulletFactory2D.helper_generate_transforms_waterfall,
			BulletFactory2D.helper_generate_transforms_lattice,
			BulletFactory2D.helper_generate_transforms_rose,
			BulletFactory2D.helper_generate_transforms_counter_spiral,
			BulletFactory2D.helper_generate_transforms_corridor,
			BulletFactory2D.helper_generate_transforms_lissajous,
			BulletFactory2D.helper_generate_transforms_circle,
			BulletFactory2D.helper_generate_transforms_rectangle,
			BulletFactory2D.helper_generate_transforms_polygon,
			BulletFactory2D.helper_generate_transforms_triangle,
			BulletFactory2D.helper_generate_transforms_trapezoid,
			BulletFactory2D.helper_generate_transforms_diamond,
			BulletFactory2D.helper_generate_transforms_edge_from_points,
		]
	return _callables

func _gen_args(id: int, amount: int) -> Array:
	var m := marker
	match id:
		0: return [amount, m, 3, 4, 150.0, 150.0, true, false, 0.0, 0]
		1: return [amount, m, 150.0, 0.0, TAU, true, false, true, 1.0, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 0, 1]
		2: return [amount, m, 0.5, 0.0, 0.0, true, 0.0, 0]
		3: return [amount, m, 50.0, 15.0, 0.6, true, 0, 0.0]
		4: return [amount, m, Vector2(1, 0), 32.0, true, 1, false]
		5: return [amount, m, Vector2(300, 400), 0.3, 0.0, true]
		6: return [amount, m, 6, 5, 150.0, 0.5, 1.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 0, 0.0, 45.0, 80.0, 6.0, 1.0]
		7: return [amount, m, 150.0, 100.0, 0.0, 0.0, TAU, 0, 0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]
		8: return [amount, m, 600.0, Vector2(0, 1), 48.0, 0.0, 0]
		9: return [amount, m, 120.0, 0.4, 0, 0.0, Vector2(1, 0), TAU, 0]
		10: return [amount, m, 5, 150.0, 2.0, 0.0, true, 0.0]
		11: return [amount, m, 3, 50.0, 15.0, 0.6, true, 0, 0.0, 1]
		12: return [amount, m, 4, 150.0, 32.0, 0.0, true, 0.0]
		13: return [amount, m, 5, 150.0, 65.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]
		14: return [amount, m, 150.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]
		15: return [amount, m, 600.0, 48.0, 2.0, Vector2(1, 0), true, 0.0]
		16: return [amount, m, 4, 64.0, 4, 64.0, 0.5, Vector2(0, 1), 0.0, 0.0, 0]
		17: return [amount, m, 4, 4, 64.0, 64.0, true, true, 0.0]
		18: return [amount, m, 6, 150.0, 1.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]
		19: return [amount, m, 2, 50.0, 15.0, 0.6, true, 0, 0.0, 1, true]
		20: return [amount, m, Vector2(0, 1), 400.0, 32.0, 96.0, true, 0.0]
		21: return [amount, m, 200.0, 120.0, 3.0, 2.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]
		22: return [amount, m, 150.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]
		23: return [amount, m, Vector2(300, 200), true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]
		24: return [amount, m, 6, 150.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]
		25: return [amount, m, 0, 200.0, 200.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]
		26: return [amount, m, 150.0, 280.0, 140.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]
		27: return [amount, m, 200.0, 300.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]
		28: return [amount, m, PackedVector2Array([Vector2(-50, 0), Vector2(50, 0), Vector2(50, 50)]), false, false, false, 0.0, 0.0, 0, 0.0, 2.0, 0, 0.0]
	return []

const GEN_COUNT := 29

func _gen_call(id: int, args: Array) -> Array:
	var c: Callable = _funcs()[id]
	var v: Variant = c.callv(args)
	if v is Array:
		return v
	return []

func _gen(id: int, amount: int) -> Array:
	return _gen_call(id, _gen_args(id, amount))

func _g_counts() -> void:
	for id in GEN_COUNT:
		for amount in [-1, 0, 1]:
			var v: Array = _gen(id, amount)
			if amount < 0 or amount == 0:
				_check(v.size() == 0, "gen %d amount %d rejected" % [id, amount])
			else:
				_check(v.size() <= 1 and _finite_volley(v), "gen %d amount 1 sane (n=%d)" % [id, v.size()])

func _g_degenerate() -> void:
	var m := marker
	# Zero sizes: degenerate loops stack, never crash, always finite.
	for v in [
		_gen_call(1, [8, m, 0.0, 0.0, TAU, true, false, true, 1.0, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 0, 1]),
		_gen_call(22, [8, m, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]),
		_gen_call(23, [8, m, Vector2(0, 0), true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]),
		_gen_call(24, [8, m, 6, 0.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]),
		_gen_call(13, [8, m, 5, 0.0, 0.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]),
		_gen_call(7, [8, m, 0.0, 0.0, 0.0, 0.0, TAU, 0, 0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]),
		_gen_call(8, [8, m, 0.0, Vector2(0, 1), 0.0, 0.0, 0]),
		_gen_call(15, [8, m, 0.0, 0.0, 2.0, Vector2(1, 0), true, 0.0]),
		_gen_call(2, [8, m, 0.0, 0.0, 0.0, true, 0.0, 0]),
		_gen_call(4, [8, m, Vector2(1, 0), 0.0, true, 1, false]),
		_gen_call(25, [8, m, 0, 0.0, 0.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]),
		_gen_call(26, [8, m, 0.0, 0.0, 0.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]),
		_gen_call(27, [8, m, 0.0, 0.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]),
		_gen_call(14, [8, m, 0.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]),
		_gen_call(10, [8, m, 5, 0.0, 2.0, 0.0, true, 0.0]),
		_gen_call(18, [8, m, 6, 0.0, 1.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]),
		_gen_call(3, [8, m, 0.0, 15.0, 0.6, true, 0, 0.0]),
		_gen_call(11, [8, m, 3, 0.0, 15.0, 0.6, true, 0, 0.0, 1]),
		_gen_call(19, [8, m, 2, 0.0, 15.0, 0.6, true, 0, 0.0, 1, true]),
		_gen_call(21, [8, m, 0.0, 0.0, 3.0, 2.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]),
		_gen_call(6, [8, m, 6, 5, 0.0, 0.5, 1.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 0, 0.0, 45.0, 80.0, 6.0, 1.0]),
		_gen_call(9, [8, m, 0.0, 0.4, 0, 0.0, Vector2(1, 0), TAU, 0]),
		_gen_call(16, [8, m, 4, 0.0, 4, 0.0, 0.5, Vector2(0, 1), 0.0, 0.0, 0]),
		_gen_call(17, [8, m, 4, 4, 0.0, 0.0, true, true, 0.0]),
		_gen_call(0, [8, m, 3, 4, 0.0, 0.0, true, false, 0.0, 0]),
		_gen_call(12, [8, m, 4, 0.0, 32.0, 0.0, true, 0.0]),
	]:
		_check(_finite_volley(v), "degenerate volley finite (n=%d)" % v.size())
	# Documented rejections: invalid counts/directions eat the whole request.
	# (Heart size 0 is rejected, not stacked: size must be > 0.)
	_check(_gen_call(14, [8, m, 0.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]).is_empty(), "heart size 0 rejected")
	_check(_gen_call(20, [8, m, Vector2(0, 1), 0.0, 32.0, 0.0, true, 0.0]).is_empty(), "corridor gap==width rejected")
	_check(_gen_call(11, [8, m, 0, 50.0, 15.0, 0.6, true, 0, 0.0, 1]).is_empty(), "multispiral arms=0 rejected")
	_check(_gen_call(19, [8, m, 1, 50.0, 15.0, 0.6, true, 0, 0.0, 1, true]).is_empty(), "counter arms=1 rejected")
	_check(_gen_call(10, [8, m, 2, 150.0, 2.0, 0.0, true, 0.0]).is_empty(), "star_polygon vertices=2 rejected")
	_check(_gen_call(6, [8, m, 0, 5, 150.0, 0.5, 1.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 0, 0.0, 45.0, 80.0, 6.0, 1.0]).is_empty(), "flower petals=0 rejected")
	_check(_gen_call(18, [8, m, 1, 150.0, 1.0, 0.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0]).is_empty(), "rose petals=1 rejected")
	_check(_gen_call(16, [8, m, 0, 64.0, 4, 64.0, 0.5, Vector2(0, 1), 0.0, 0.0, 0]).is_empty(), "waterfall columns=0 rejected")
	_check(_gen_call(17, [8, m, 4, 0, 64.0, 64.0, true, true, 0.0]).is_empty(), "lattice rows=0 rejected")
	_check(_gen_call(0, [8, m, 0, 4, 150.0, 150.0, true, false, 0.0, 0]).is_empty(), "grid rows=0 rejected")
	_check(_gen_call(12, [8, m, 0, 150.0, 32.0, 0.0, true, 0.0]).is_empty(), "cross arms=0 rejected")
	_check(_gen_call(4, [8, m, Vector2(0, 0), 32.0, true, 1, false]).is_empty(), "line zero direction rejected")
	_check(_gen_call(5, [8, m, Vector2(0, 0), 0.3, 0.0, true]).is_empty(), "aimed coincident target rejected")
	_check(_gen_call(8, [8, m, 600.0, Vector2(0, 0), 48.0, 0.0, 0]).is_empty(), "rain zero direction rejected")
	_check(_gen_call(20, [8, m, Vector2(0, 0), 400.0, 32.0, 96.0, true, 0.0]).is_empty(), "corridor zero aim rejected")
	_check(_gen_call(16, [8, m, 4, 64.0, 4, 64.0, 0.5, Vector2(0, 0), 0.0, 0.0, 0]).is_empty(), "waterfall zero rain rejected")
	# Dead-knob degradation (documented): zero sector direction falls back to +X.
	var sz: Array = _gen_call(9, [8, m, 120.0, 0.4, 0, 0.0, Vector2(0, 0), TAU, 0])
	_check(sz.size() == 8 and _finite_volley(sz), "scatter zero sector degrades")

# id -> [arg index, kind]: kind 0 = float NAN, 1 = Vector2(NAN,NAN), 2 = single-NAN point array.
const NAN_IDXS := {
	0: [4, 0], 1: [2, 0], 2: [2, 0], 3: [2, 0], 4: [3, 0], 5: [2, 1],
	6: [4, 0], 7: [2, 0], 8: [2, 0], 9: [2, 0], 10: [3, 0], 11: [3, 0],
	12: [3, 0], 13: [3, 0], 14: [2, 0], 15: [2, 0], 16: [3, 0], 17: [4, 0],
	18: [3, 0], 19: [3, 0], 20: [3, 0], 21: [2, 0], 22: [2, 0], 23: [2, 1],
	24: [3, 0], 25: [3, 0], 26: [2, 0], 27: [2, 0], 28: [2, 2],
}

func _nan_arg(kind: int) -> Variant:
	if kind == 1:
		return Vector2(NAN, NAN)
	if kind == 2:
		return PackedVector2Array([Vector2(NAN, 0)])
	return NAN

func _g_nan() -> void:
	for id in GEN_COUNT:
		var spec: Array = NAN_IDXS[id]
		var args: Array = _gen_args(id, 8)
		args[spec[0]] = _nan_arg(spec[1])
		var v: Array = _gen_call(id, args)
		_check(v.is_empty(), "gen %d NAN rejected" % id)
	# NAN marker origin is rejected everywhere too.
	var bad_m := Transform2D(0.0, Vector2(NAN, 0))
	var vm: Array = BulletFactory2D.helper_generate_transforms_ring(8, bad_m, 150.0, 0.0, TAU, true, false, true, 1.0, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 0, 1)
	_check(vm.is_empty(), "NAN marker rejected")
	# NAN direction is rejected by the strict generators (line/rain).
	var dl: Array = BulletFactory2D.helper_generate_transforms_line(8, marker, Vector2(NAN, 0), 32.0, true, 1, false)
	_check(dl.is_empty(), "line NAN direction rejected")

func _g_twist_extreme() -> void:
	var m := marker
	for twist in [-2147483648, -1000000, 1000000, 2147483647]:
		var ring: Array = BulletFactory2D.helper_generate_transforms_ring(
			24, m, 150.0, 0.0, TAU, true, false, true, 1.0, 0.0,
			1, 0, false, 0, 32.0, false, 0.0,
			3, 0.2, 0, 0, 0, 0, PackedFloat32Array(), twist, 0, 0, 1)
		_check(ring.size() == 24 and _finite_volley(ring), "extreme twist ring survives (twist=%d)" % twist)
		var worst := 0.0
		for i in ring.size():
			var p: Vector2 = (ring[i] as Transform2D).origin
			var layer: int = BulletFactory2D.helper_bullet_layer_index(i, 24, 3, 0, 0)
			worst = maxf(worst, absf(p.distance_to(m.origin) - 150.0 * (1.0 + 0.2 * layer)))
		_check(worst < 1.0, "extreme twist ring on rings (twist=%d, worst %.3f)" % [twist, worst])
	# 64 layers at the extreme: int64 deal math must hold.
	var big: Array = BulletFactory2D.helper_generate_transforms_circle(
		64, m, 50.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0,
		64, 0.2, 0, 0, 0, 0, PackedFloat32Array(), -2147483648, 0, 1)
	_check(big.size() == 64 and _finite_volley(big), "64 layers INT_MIN twist survives")

func _g_slots_offsets() -> void:
	var m := marker
	for off in [-1, -5, -1000000, 1000000, 2147483647]:
		var ring: Array = BulletFactory2D.helper_generate_transforms_ring(
			12, m, 150.0, 0.0, TAU, true, false, true, 1.0, 0.0,
			0, 0, false, off, 32.0, false, 0.0,
			1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 0, 1)
		_check(ring.size() == 12 and _finite_volley(ring), "outline_slot_offset wraps (off=%d)" % off)
	# layer_start_offset rejects negatives, wraps huge positives.
	var bad_start: Array = BulletFactory2D.helper_generate_transforms_ring(
		12, m, 150.0, 0.0, TAU, true, false, true, 1.0, 0.0,
		1, 0, false, 0, 32.0, false, 0.0,
		3, 0.2, 0, 1, -1, 0, PackedFloat32Array(), 0, 0, 0, 1)
	_check(bad_start.is_empty(), "layer_start_offset negative rejected")
	var huge_start: Array = BulletFactory2D.helper_generate_transforms_ring(
		12, m, 150.0, 0.0, TAU, true, false, true, 1.0, 0.0,
		1, 0, false, 0, 32.0, false, 0.0,
		3, 0.2, 0, 1, 1000000, 0, PackedFloat32Array(), 0, 0, 0, 1)
	_check(huge_start.size() == 12 and _finite_volley(huge_start), "layer_start_offset huge wraps")
	# Offset rotation is exact, not just non-crashing: bullet 0 rides slot k.
	var plain: Array = BulletFactory2D.helper_generate_transforms_ring(
		12, m, 150.0, 0.0, TAU, true, false, true, 1.0, 0.0,
		0, 0, false, 0, 32.0, false, 0.0,
		1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 0, 1)
	var rot3: Array = BulletFactory2D.helper_generate_transforms_ring(
		12, m, 150.0, 0.0, TAU, true, false, true, 1.0, 0.0,
		0, 0, false, 3, 32.0, false, 0.0,
		1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 0, 1)
	_check((rot3[0] as Transform2D).origin.distance_to((plain[3] as Transform2D).origin) < 0.01, "slot_offset +3 rotates order")
	var rotneg: Array = BulletFactory2D.helper_generate_transforms_ring(
		12, m, 150.0, 0.0, TAU, true, false, true, 1.0, 0.0,
		0, 0, false, -1, 32.0, false, 0.0,
		1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 0, 1)
	_check((rotneg[0] as Transform2D).origin.distance_to((plain[11] as Transform2D).origin) < 0.01, "slot_offset -1 wraps")

func _g_scales() -> void:
	var m := marker
	var big := PackedFloat32Array()
	big.resize(65)
	big.fill(1.0)
	_check(BulletFactory2D.helper_generate_transforms_circle(12, m, 150.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, 0.2, 0, 0, 0, 0, big, 0, 0, 1).is_empty(), "65 custom scales rejected")
	_check(BulletFactory2D.helper_generate_transforms_circle(12, m, 150.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, 0.2, 0, 0, 0, 0, PackedFloat32Array([0.04]), 0, 0, 1).is_empty(), "custom scale 0.04 rejected")
	_check(BulletFactory2D.helper_generate_transforms_circle(12, m, 150.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, 0.2, 0, 0, 0, 0, PackedFloat32Array([64.1]), 0, 0, 1).is_empty(), "custom scale 64.1 rejected")
	_check(BulletFactory2D.helper_generate_transforms_circle(12, m, 150.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, 0.2, 0, 0, 0, 0, PackedFloat32Array([NAN]), 0, 0, 1).is_empty(), "custom scale NAN rejected")
	var ok_scales: Array = BulletFactory2D.helper_generate_transforms_circle(12, m, 150.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, 0.2, 0, 0, 0, 0, PackedFloat32Array([1.0, 1.5, 1.6]), 0, 0, 1)
	_check(ok_scales.size() == 12 and _finite_volley(ok_scales), "valid custom scales accepted")
	_check(BulletFactory2D.helper_generate_transforms_circle(12, m, 150.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, 0.0, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 1).is_empty(), "layer_scale 0 rejected")
	_check(BulletFactory2D.helper_generate_transforms_circle(12, m, 150.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 0, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 1).is_empty(), "layer_count 0 rejected")
	_check(BulletFactory2D.helper_generate_transforms_circle(12, m, 150.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 65, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 1).is_empty(), "layer_count 65 rejected")
	_check(BulletFactory2D.helper_generate_transforms_circle(12, m, 150.0, true, 0.0, 1, 0, false, 0, 32.0, false, 0.0, 3, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, -1, 1).is_empty(), "max_dots -1 rejected")
	_check(BulletFactory2D.helper_generate_transforms_circle(12, m, 150.0, true, 0.0, 1, 3, false, 0, 32.0, false, 0.0, 3, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 1).is_empty(), "outline_facing 3 rejected")
	_check(BulletFactory2D.helper_generate_transforms_circle(12, m, 150.0, true, 0.0, 3, 0, false, 0, 32.0, false, 0.0, 3, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 1).is_empty(), "placement 3 rejected")

func _g_edge_image() -> void:
	var m := marker
	_check((BulletFactory2D.helper_extract_edge_from_image(null, 0.5, 4, true) as Dictionary).get("points", PackedVector2Array()).is_empty(), "null image rejected")
	var transparent := Image.create_empty(8, 8, false, Image.FORMAT_RGBA8)
	transparent.fill(Color(1, 1, 1, 0))
	_check((BulletFactory2D.helper_extract_edge_from_image(transparent, 0.5, 2, true) as Dictionary).get("points", PackedVector2Array()).is_empty(), "transparent image finds nothing")
	var solid := Image.create_empty(8, 8, false, Image.FORMAT_RGBA8)
	solid.fill(Color(1, 1, 1, 1))
	var res: Dictionary = BulletFactory2D.helper_extract_edge_from_image(solid, 0.5, 2, true)
	var pts: PackedVector2Array = res.get("points", PackedVector2Array())
	var nrms: PackedVector2Array = res.get("normals", PackedVector2Array())
	_check(not pts.is_empty() and pts.size() == nrms.size(), "solid image extracts edges (%d)" % pts.size())
	_check((BulletFactory2D.helper_extract_edge_from_image(solid, -0.5, 2, true) as Dictionary).get("points", PackedVector2Array()).is_empty(), "threshold -0.5 rejected")
	_check((BulletFactory2D.helper_extract_edge_from_image(solid, 1.5, 2, true) as Dictionary).get("points", PackedVector2Array()).is_empty(), "threshold 1.5 rejected")
	_check((BulletFactory2D.helper_extract_edge_from_image(solid, 0.5, 0, true) as Dictionary).get("points", PackedVector2Array()).is_empty(), "step 0 rejected")
	# Extracted points feed the sampler both open and closed, with spread.
	for closed in [false, true]:
		var v: Array = BulletFactory2D.helper_generate_transforms_edge_from_points(12, m, pts, closed, false, true, 2.0, 0.0, 7, 3.0, 2.0, 2, 1.0)
		_check(v.size() == 12 and _finite_volley(v), "extracted edge samples (closed=%s)" % str(closed))
	_check(BulletFactory2D.helper_generate_transforms_edge_from_points(8, m, PackedVector2Array(), false, false, false, 0.0, 0.0, 0, 0.0, 2.0, 0, 0.0).is_empty(), "empty edge rejected")
	var solo: Array = BulletFactory2D.helper_generate_transforms_edge_from_points(4, m, PackedVector2Array([Vector2(10, 20)]), false, true, false, 0.0, 0.0, 0, 0.0, 2.0, 0, 0.0)
	_check(solo.size() == 4 and _finite_volley(solo), "single-point edge stacks")
	var coinc: Array = BulletFactory2D.helper_generate_transforms_edge_from_points(4, m, PackedVector2Array([Vector2(5, 5), Vector2(5, 5), Vector2(5, 5)]), true, false, false, 0.0, 0.0, 0, 0.0, 2.0, 0, 0.0)
	_check(coinc.size() == 4 and _finite_volley(coinc), "coincident edge stacks")

func _g_side_spread_skip() -> void:
	var m := marker
	var base: Array = BulletFactory2D.helper_generate_transforms_circle(6, m, 150.0, true, 0.0, 0, 0, false, 0, 32.0, false, 0.0, 1, 0.2, 0, 0, 0, 0, PackedFloat32Array(), 0, 0, 1)
	_check(base.size() == 6, "spread fixture emits 6")
	_check(BulletFactory2D.helper_apply_side_spread(base, -1, 5.0, 2.0, 0).is_empty(), "side_mode -1 rejected")
	_check(BulletFactory2D.helper_apply_side_spread(base, 4, 5.0, 2.0, 0).is_empty(), "side_mode 4 rejected")
	_check(BulletFactory2D.helper_apply_side_spread(base, 1, -1.0, 2.0, 0).is_empty(), "spread -1 rejected")
	_check(BulletFactory2D.helper_apply_side_spread(base, 1, 5.0, 0.0, 0).is_empty(), "exponent 0 rejected")
	for mode in [0, 1, 2, 3]:
		for spread in [0.0, 5.0]:
			var v: Array = BulletFactory2D.helper_apply_side_spread(base, mode, spread, 2.0, 7)
			_check(v.size() == 6 and _finite_volley(v), "side_spread mode=%d spread=%.1f sane" % [mode, spread])
	# Non-finite input slots are skipped, never crash.
	var bad: Array = base.duplicate()
	bad[0] = Transform2D(0.0, Vector2(NAN, 0))
	var vbad: Array = BulletFactory2D.helper_apply_side_spread(bad, 1, 5.0, 2.0, 0)
	_check(vbad.size() == 6, "side_spread skips NAN slot")
	# Skip indices: exact removals, OOB ignored with a warning, never crash.
	_check(BulletFactory2D.helper_apply_skip_indices(base, PackedInt32Array()).size() == 6, "skip empty keeps all")
	_check(BulletFactory2D.helper_apply_skip_indices(base, PackedInt32Array([0])).size() == 5, "skip [0] drops one")
	_check(BulletFactory2D.helper_apply_skip_indices(base, PackedInt32Array([-1, 99, 2147483647])).size() == 6, "skip OOB ignored")
	_check(BulletFactory2D.helper_apply_skip_indices(base, PackedInt32Array([0, 1, 2, 3, 4, 5])).is_empty(), "skip all drops all")
	_check(BulletFactory2D.helper_apply_skip_indices(base, PackedInt32Array([2, 2, 2])).size() == 5, "skip dupes drop once")

func _approx(a: float, b: float, eps: float = 0.001) -> bool:
	return absf(a - b) <= eps

func _g_semantics() -> void:
	var m := marker
	# Fan cone geometry: centered straddles, one-sided opens forward, lone takes center.
	var fan_c: Array = BulletFactory2D.helper_generate_transforms_fan(3, m, 0.6, 1.0, 0.0, true, 0.0, 0)
	_check(_approx((fan_c[0] as Transform2D).get_rotation(), 0.7) and _approx((fan_c[1] as Transform2D).get_rotation(), 1.0) and _approx((fan_c[2] as Transform2D).get_rotation(), 1.3), "fan centered straddles")
	var fan_o: Array = BulletFactory2D.helper_generate_transforms_fan(3, m, 0.6, 1.0, 0.0, false, 0.0, 0)
	_check(_approx((fan_o[0] as Transform2D).get_rotation(), 1.0) and _approx((fan_o[2] as Transform2D).get_rotation(), 1.6), "fan one-sided opens forward")
	var fan_1: Array = BulletFactory2D.helper_generate_transforms_fan(1, m, 0.6, 1.0, 0.0, true, 0.0, 0)
	_check(fan_1.size() == 1 and _approx((fan_1[0] as Transform2D).get_rotation(), 1.0), "lone fan takes center")
	# Line anchors: center-symmetric, start at marker, end behind it.
	for anchor in [0, 1, 2]:
		var ln: Array = BulletFactory2D.helper_generate_transforms_line(4, m, Vector2(1, 0), 10.0, true, anchor, false)
		var want: Array = [-15.0, -5.0, 5.0, 15.0]
		if anchor == 0:
			want = [0.0, 10.0, 20.0, 30.0]
		elif anchor == 2:
			want = [-30.0, -20.0, -10.0, 0.0]
		var ok_l := ln.size() == 4
		for i in 4:
			if not _approx((ln[i] as Transform2D).origin.x, want[i], 0.01):
				ok_l = false
		_check(ok_l, "line anchor %d exact" % anchor)
	# Aimed cone centers on the target; muzzles stack at the marker.
	var aim: Array = BulletFactory2D.helper_generate_transforms_aimed(3, m, Vector2(300, 400), 0.3, 0.0, true)
	var d: float = Vector2(300, 400).angle()
	_check(aim.size() == 3 and _approx((aim[0] as Transform2D).get_rotation(), d - 0.15) and _approx((aim[1] as Transform2D).get_rotation(), d) and _approx((aim[2] as Transform2D).get_rotation(), d + 0.15), "aimed cone centers on target")
	var muzzles := true
	for t in aim:
		if ((t as Transform2D).origin.distance_to(m.origin) > 0.01):
			muzzles = false
	_check(muzzles, "aimed muzzles stack at marker")
	# Spiral radii grow linearly, angles step evenly (marker rotation off).
	var spi: Array = BulletFactory2D.helper_generate_transforms_spiral(4, m, 50.0, 15.0, 0.6, false, 0, 0.0)
	var ok_s := spi.size() == 4
	for i in 4:
		var p: Vector2 = (spi[i] as Transform2D).origin
		if not _approx(p.length(), 50.0 + 15.0 * i, 0.01) or not _approx(p.angle(), 0.6 * i, 0.01):
			ok_s = false
	_check(ok_s, "spiral radii/angles exact")
	# Multispiral deals arms round-robin; counter-spiral mirrors alternates.
	var msp: Array = BulletFactory2D.helper_generate_transforms_multispiral(4, m, 2, 50.0, 15.0, 0.6, false, 0, 0.0, 1)
	var rr: Array = []
	for t in msp:
		rr.append((t as Transform2D).origin.length())
	rr.sort()
	var ok_m := rr.size() == 4
	var want_m := [50.0, 50.0, 65.0, 65.0]
	for i in 4:
		if not _approx(rr[i], want_m[i], 0.01):
			ok_m = false
	_check(ok_m, "multispiral deals arms")
	var csp: Array = BulletFactory2D.helper_generate_transforms_counter_spiral(4, m, 2, 50.0, 15.0, 0.6, false, 0, 0.0, 1, true)
	var want_a := [0.0, PI, 0.6, PI - 0.6]
	var ok_c := csp.size() == 4
	for i in 4:
		# Wrapped compare: arm 1 lands on -PI, identical to +PI.
		if not _approx(absf(wrapf((((csp[i] as Transform2D).origin - m.origin).angle() - want_a[i]), -PI, PI)), 0.0, 0.01):
			ok_c = false
	_check(ok_c, "counter-spiral mirrors alternate arms")
	# Corridor carves the dodge door and faces down-aim.
	var cor: Array = BulletFactory2D.helper_generate_transforms_corridor(5, m, Vector2(0, 1), 400.0, 32.0, 96.0, true, 0.0)
	_check(cor.size() == 4, "corridor carves gap")
	var ok_w := cor.size() == 4
	for t in cor:
		var p: Vector2 = (t as Transform2D).origin
		if absf(p.x) < 48.0 - 0.01 or not _approx((t as Transform2D).get_rotation(), PI * 0.5, 0.01):
			ok_w = false
	_check(ok_w, "corridor walls flank gap, face aim")
	# Wave sweep: fixed span, sinusoidal crossfall. frac runs [-0.5, 0.5],
	# so one full period (waves=1) peaks exactly at the quarter slots.
	var wav: Array = BulletFactory2D.helper_generate_transforms_wave(5, m, 600.0, 48.0, 1.0, Vector2(1, 0), true, 0.0)
	var ok_w2 := wav.size() == 5
	var want_x := [-300.0, -150.0, 0.0, 150.0, 300.0]
	var want_ay := [0.0, 48.0, 0.0, 48.0, 0.0]
	for i in 5:
		var p: Vector2 = (wav[i] as Transform2D).origin
		if not _approx(p.x, want_x[i], 0.01) or not _approx(absf(p.y), want_ay[i], 0.01):
			ok_w2 = false
	_check(ok_w2, "wave sweep exact")
	# Grid centers full layouts and ragged tails on their own rows.
	var gr: Array = BulletFactory2D.helper_generate_transforms_grid(6, m, 3, 4, 100.0, 100.0, true, false, 0.0, 0)
	var want_g := [Vector2(-50, -100), Vector2(-50, 0), Vector2(-50, 100), Vector2(50, -100), Vector2(50, 0), Vector2(50, 100)]
	var ok_g := gr.size() == 6
	for i in 6:
		if ((gr[i] as Transform2D).origin.distance_to(want_g[i]) > 0.01):
			ok_g = false
	_check(ok_g, "grid centers full layout")
	var gr5: Array = BulletFactory2D.helper_generate_transforms_grid(5, m, 3, 4, 100.0, 100.0, true, false, 0.0, 0)
	var want_g5 := [Vector2(-50, -100), Vector2(-50, 0), Vector2(-50, 100), Vector2(50, -50), Vector2(50, 50)]
	var ok_g5 := gr5.size() == 5
	for i in 5:
		if ((gr5[i] as Transform2D).origin.distance_to(want_g5[i]) > 0.01):
			ok_g5 = false
	_check(ok_g5, "grid ragged tail centers itself")
	# Rain sheets rows; waterfall steps downrange; lattice honeycomb exact.
	var rn: Array = BulletFactory2D.helper_generate_transforms_rain(24, m, 600.0, Vector2(0, 1), 48.0, 0.0, 0)
	var zeros := 0
	var stepped := 0
	for t in rn:
		var y: float = (t as Transform2D).origin.y
		if _approx(y, 0.0, 0.01):
			zeros += 1
		elif _approx(y, -48.0, 0.01):
			stepped += 1
	_check(rn.size() == 24 and zeros == 13 and stepped == 11, "rain sheets rows")
	var wf: Array = BulletFactory2D.helper_generate_transforms_waterfall(6, m, 4, 64.0, 4, 64.0, 0.5, Vector2(0, 1), 0.0, 0.0, 0)
	var ys: Array = []
	for t in wf:
		ys.append((t as Transform2D).origin.y)
	ys.sort()
	var ok_y := ys.size() == 6
	var want_y := [0.0, 0.0, 0.0, 0.0, 64.0, 64.0]
	for i in 6:
		if not _approx(ys[i], want_y[i], 0.01):
			ok_y = false
	_check(ok_y, "waterfall rows step downrange")
	var la: Array = BulletFactory2D.helper_generate_transforms_lattice(5, m, 4, 4, 64.0, 64.0, true, true, 0.0)
	var want_l := [Vector2(-96, -96), Vector2(-32, -96), Vector2(32, -96), Vector2(96, -96), Vector2(-64, -32)]
	var ok_la := la.size() == 5
	for i in 5:
		if ((la[i] as Transform2D).origin.distance_to(want_l[i]) > 0.01):
			ok_la = false
	_check(ok_la, "lattice honeycomb exact")
	# Seeded scatter reproduces identically; cross rays split evenly.
	var s1: Array = BulletFactory2D.helper_generate_transforms_scatter(8, m, 120.0, 0.4, 7, 0.0, Vector2(1, 0), TAU, 0)
	var s2: Array = BulletFactory2D.helper_generate_transforms_scatter(8, m, 120.0, 0.4, 7, 0.0, Vector2(1, 0), TAU, 0)
	var same := s1.size() == 8 and s2.size() == 8
	for i in 8:
		if ((s1[i] as Transform2D).origin.distance_to((s2[i] as Transform2D).origin) > 0.0001):
			same = false
	_check(same, "scatter seeded determinism")
	var cr: Array = BulletFactory2D.helper_generate_transforms_cross(4, m, 4, 150.0, 32.0, 0.0, true, 0.0)
	var want_c := [Vector2(32, 0), Vector2(0, 32), Vector2(-32, 0), Vector2(0, -32)]
	var ok_cr := cr.size() == 4
	for i in 4:
		if ((cr[i] as Transform2D).origin.distance_to(want_c[i]) > 0.01):
			ok_cr = false
	_check(ok_cr, "cross rays exact")
	var sp5: Array = BulletFactory2D.helper_generate_transforms_star_polygon(5, m, 5, 150.0, 0.0, 0.0, true, 0.0)
	var ok_sp := sp5.size() == 5
	for t in sp5:
		if not _approx(((t as Transform2D).origin - m.origin).length(), 150.0, 0.01):
			ok_sp = false
	_check(ok_sp, "star_polygon bias 0 is an even ring")

func _init() -> void:
	var group := OS.get_environment("CASE")
	printerr("FUZZ group=", group)
	match group:
		"counts":
			_g_counts()
		"degenerate":
			_g_degenerate()
		"nan":
			_g_nan()
		"twist_extreme":
			_g_twist_extreme()
		"slots_offsets":
			_g_slots_offsets()
		"scales":
			_g_scales()
		"edge_image":
			_g_edge_image()
		"side_spread_skip":
			_g_side_spread_skip()
		"semantics":
			_g_semantics()
		_:
			printerr("unknown CASE (want counts|degenerate|nan|twist_extreme|slots_offsets|scales|edge_image|side_spread_skip|semantics)")
			quit(2)
			return
	if failures == 0:
		print("GROUP_DONE ", group, " ALL PASS")
	else:
		printerr("GROUP_DONE ", group, " ", failures, " FAILURES")
	quit(failures)
