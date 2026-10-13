class_name MotionProbe
extends RefCounted
## Measures how bullets MOVE over a run of factory ticks (debug_advance_time),
## so tests can pin motion quality instead of single poses:
## - turn rate: how fast each bullet's flight heading turns (deg/s);
## - turn flips: how often a bullet's turn changes side (left, right, left)
##   while it turns noticeably: the "spinning / jittering" a player sees;
## - snaps: turns of `snap_deg` or more in ONE tick (a sprite whipping
##   around); `steer_snaps` counts only those on a tick the bullet did not
##   bounce (a wall reflection is a legitimate snap, steering never is);
## - facing error: the angle between the sprite's forward axis (texture
##   rotation stripped) and the flight heading;
## - edge gaps: center distance minus both bullets' drawn radii (negative =
##   the sprites overlap), the size every flock rule counts;
## - non-finite poses.
## Only enabled bullets count; a bullet that ends mid-run stops counting.

## A turn smaller than this per tick never counts as a flip (float noise and
## a calm flock's tiny corrections are not jitter).
var flip_floor_deg := 0.5
## Ticks at the start of the run that the rates and flips skip (a spawn or a
## blend-in settling). Facing and finiteness are always checked.
var settle_ticks := 0
## A turn this large in one tick counts as a snap.
var snap_deg := 20.0

var _factory: BulletFactory2D
var _volleys: Array = []
var _texture_rotation: Array = []
var _quad_radius: Array = []

var ticks := 0
var bullet_ticks := 0
var max_turn_deg_per_sec := 0.0
var turn_sum_deg := 0.0
var flips := 0
var snaps := 0
var steer_snaps := 0
## The first snaps as [volley index, bullet index, tick, turn degrees].
var snap_log: Array = []
var max_facing_error_deg := 0.0
var nonfinite := 0
var _last_heading := {}
var _last_turn := {}
var _last_bounces := {}


## `texture_rotations` / `quad_sizes`: per volley, its spawn data's
## texture_rotation_radians and texture_size (the probe cannot read the data).
func _init(factory: BulletFactory2D, volleys: Array, texture_rotations: Array = [], quad_sizes: Array = []) -> void:
	_factory = factory
	_volleys = volleys
	for k in volleys.size():
		_texture_rotation.append(float(texture_rotations[k]) if k < texture_rotations.size() else 0.0)
		var quad: Vector2 = quad_sizes[k] if k < quad_sizes.size() else Vector2(16, 16)
		_quad_radius.append(0.25 * (quad.x + quad.y))


## Adds a volley spawned later (a burst shot) to the measurement.
func add_volley(v: BulletVolley2D, texture_rotation := 0.0, quad_size := Vector2(16, 16)) -> void:
	_volleys.append(v)
	_texture_rotation.append(texture_rotation)
	_quad_radius.append(0.25 * (quad_size.x + quad_size.y))


## Runs `n` ticks of `dt` and measures each. `check_facing` adds the facing
## error (only meaningful when something faces the heading).
func run(n: int, dt := 1.0 / 60.0, check_facing := false) -> void:
	for k in n:
		_factory.debug_advance_time(dt)
		sample(dt, check_facing)


## Measures the current poses against the previous sample.
func sample(dt: float, check_facing := false) -> void:
	ticks += 1
	for vi in _volleys.size():
		var v: BulletVolley2D = _volleys[vi]
		if not is_instance_valid(v):
			continue
		var n := v.get_amount_bullets()
		for i in n:
			var key := Vector2i(vi, i)
			if not v.is_bullet_status_enabled(i):
				_last_heading.erase(key)
				_last_turn.erase(key)
				_last_bounces.erase(key)
				continue
			var t: Transform2D = v.get_bullet_transform(i)
			var dir: Vector2 = v.get_bullet_direction(i)
			if not t.is_finite() or not dir.is_finite():
				nonfinite += 1
				continue
			if dir.length_squared() < 1e-8:
				continue
			var heading := dir.angle()
			var bounced := v.bullet_get_bounce_count(i) != int(_last_bounces.get(key, v.bullet_get_bounce_count(i)))
			_last_bounces[key] = v.bullet_get_bounce_count(i)
			if check_facing:
				var facing := t.x.angle() - float(_texture_rotation[vi])
				max_facing_error_deg = maxf(max_facing_error_deg, absf(rad_to_deg(angle_difference(heading, facing))))
			if _last_heading.has(key) and ticks > settle_ticks:
				var turn := rad_to_deg(angle_difference(_last_heading[key], heading))
				bullet_ticks += 1
				turn_sum_deg += absf(turn)
				max_turn_deg_per_sec = maxf(max_turn_deg_per_sec, absf(turn) / dt)
				if absf(turn) >= snap_deg:
					snaps += 1
					if not bounced:
						steer_snaps += 1
					if snap_log.size() < 16:
						snap_log.append([vi, i, ticks, turn])
				if absf(turn) >= flip_floor_deg:
					if _last_turn.has(key) and signf(_last_turn[key]) != signf(turn):
						flips += 1
					_last_turn[key] = turn
			_last_heading[key] = heading


## Mean turn rate over every measured bullet tick (deg/s).
func mean_turn_deg_per_sec(dt := 1.0 / 60.0) -> float:
	return turn_sum_deg / maxf(1.0, float(bullet_ticks)) / dt


## Turn flips per bullet per second of measured flight.
func flips_per_bullet_sec(dt := 1.0 / 60.0) -> float:
	return float(flips) / maxf(1.0, float(bullet_ticks) * dt)


## Snaps per bullet per second of measured flight.
func snaps_per_bullet_sec(dt := 1.0 / 60.0) -> float:
	return float(snaps) / maxf(1.0, float(bullet_ticks) * dt)


## The drawn radius of bullet i of volley vi (the flock's count_bullet_size
## radius: half the quad's mean side times the bullet's mean scale).
func radius(vi: int, i: int) -> float:
	var t: Transform2D = (_volleys[vi] as BulletVolley2D).get_bullet_transform(i)
	return float(_quad_radius[vi]) * 0.5 * (t.x.length() + t.y.length())


## Edge gaps of the current poses: {min_gap, overlap_fraction (bullets whose
## nearest flockmate overlaps), mean_nearest_gap, count}.
func gaps() -> Dictionary:
	var pts: Array = []
	var rad: Array = []
	for vi in _volleys.size():
		var v: BulletVolley2D = _volleys[vi]
		if not is_instance_valid(v):
			continue
		for i in v.get_amount_bullets():
			if v.is_bullet_status_enabled(i):
				pts.append(v.get_bullet_transform(i).origin)
				rad.append(radius(vi, i))
	var min_gap := INF
	var overlapping := 0
	var nearest_total := 0.0
	for a in pts.size():
		var best := INF
		for b in pts.size():
			if a != b:
				best = minf(best, (pts[a] as Vector2).distance_to(pts[b]) - rad[a] - rad[b])
		min_gap = minf(min_gap, best)
		if best < 0.0:
			overlapping += 1
		if best < INF:
			nearest_total += best
	return {
		"min_gap": min_gap,
		"overlap_fraction": float(overlapping) / maxf(1.0, pts.size()),
		"mean_nearest_gap": nearest_total / maxf(1.0, pts.size()),
		"count": pts.size(),
	}


## One line for failure messages and probes.
func summary(dt := 1.0 / 60.0) -> String:
	var g := gaps()
	return "max_turn=%.0f deg/s mean_turn=%.1f deg/s flips=%.2f /bullet/s snaps=%.3f /bullet/s (%d not bounces) facing_err=%.2f deg min_gap=%.1f overlap=%.2f mean_gap=%.1f nonfinite=%d" % [
		max_turn_deg_per_sec, mean_turn_deg_per_sec(dt), flips_per_bullet_sec(dt), snaps_per_bullet_sec(dt), steer_snaps, max_facing_error_deg,
		g["min_gap"], g["overlap_fraction"], g["mean_nearest_gap"], nonfinite]
