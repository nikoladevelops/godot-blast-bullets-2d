class_name MixLedger
extends RefCounted
## The feature-mix oracle: records what happened to every bullet life and
## checks that every report matches it, whatever features are mixed.
##
## Sources: the factory's bullet event log (every life start, wake and
## disable with its cause), its sound offer log (every offer BEFORE the
## mixer's limits) and its effect log (every one-shot that fired), plus the
## bullet signals of the factory, the spawners and the volleys.
##
## Rules (problems() lists every break):
## - One death per life: a bullet's interval opens at its life start or wake
##   and closes at exactly one disable; a disable needs an open interval of
##   the same life, a wake a closed one, a new life no open interval.
## - Every sound offer and effect naming a bullet names its current life and
##   a bullet alive in that frame (closed in that frame at the latest).
## - Death triggers match the death: On Destroy = killed by a hit, On
##   Lifetime Over = expired, On Despawn = culled, On Clear = cleared (or
##   converted with clear effects); at most `entry_limit` per life and
##   trigger. On Spawn fires on the frame the life began.
## - Every bullet signal names a live bullet of a live (not pooled) volley
##   inside its handler (the ledger connects FIRST, before any test handler);
##   bullets_cleared / bullets_despawned name bullets taken out that frame.
## Attach the ledger before the first spawn and before the test's handlers.

const S_DESTROY := 2
const S_LIFETIME := 4
const S_CLEAR := 5
const S_FLIGHT := 10
const S_DESPAWN := 11
const S_BOUNCE := 3
const S_SHOT := 0
const S_TELEGRAPH := 9

## Offers per life and trigger allowed (one sound entry per trigger).
var entry_limit := 1
## On Flight / On Bounce at most `entry_limit` per bullet per physics frame.
## Off where extra ticks run inside one physics frame (debug_advance_time).
var per_frame_limits := true
## Problems found so far (first 32 kept, `problem_count` counts all).
var problem_list: Array[String] = []
var problem_count := 0
## Event counts by kind ("life", "wake", "disable:<cause>", "offer:<trigger>",
## "effect:<trigger>", "signal:<name>").
var counts := {}

var _factory: BulletFactory2D
var _intervals := {} # "volley:bullet" -> Array of [open_frame, close_frame (-1 open), life, epoch, cause]
var _watched := {} # volley id -> true (volley-level signals connected)
var _per_life := {} # "volley:bullet:life:open_frame:trigger" -> count (death and spawn triggers)
var _per_frame := {} # "volley:bullet:frame:trigger" -> count (flight, bounce)
var _pending_out := [] # [frame, volley id, indexes, cause, life]: bullets_cleared / despawned reports
var _links: Array = [] # [object, signal, callable]: every connection, cut by detach()


func _link(obj: Object, sig: String, callable: Callable) -> void:
	obj.connect(sig, callable)
	_links.append([obj, sig, callable])


func attach(factory: BulletFactory2D, spawners: Array = []) -> void:
	_factory = factory
	factory.debug_set_bullet_event_log_enabled(true)
	factory.debug_set_sound_offer_log_enabled(true)
	factory.debug_set_effect_log_enabled(true)
	_connect_bullet_signals(factory)
	for name in ["bullets_cleared", "bullets_despawned"]:
		_link(factory, name, func(_p, v, idx) -> void: _note_out(name, v, idx))
	_link(factory, "bullet_split", func(pos, _cause, children, _depth) -> void: _on_split(pos, children))
	_link(factory, "pickup_collected", func(_t, _p, v, i, _val) -> void: _alive("pickup_collected", v, i))
	for sp in spawners:
		watch_spawner(sp)


## Spawners created later (orphan tests, chaos) join here.
func watch_spawner(sp: BulletSpawner2D) -> void:
	_connect_bullet_signals(sp)
	_link(sp, "bullet_split", func(pos, _cause, children, _depth) -> void: _on_split(pos, children))
	_link(sp, "volley_fired", func(v, _n) -> void:
		_count("signal:volley_fired")
		watch_volley(v)
		if not is_instance_valid(v) or v.is_pooled() or v.debug_get_life_state() != "active":
			_problem("volley_fired handed out a dead volley"))
	_link(sp, "volley_telegraphed", func(_aim) -> void: _count("signal:volley_telegraphed"))
	_link(sp, "volley_bullet_homing_target_reached", func(v, i, _t, _p) -> void: _alive("volley_bullet_homing_target_reached", v, i))


## Volley-level signals (homing reach of factory volleys, animation).
func watch_volley(v: BulletVolley2D) -> void:
	if not is_instance_valid(v) or _watched.has(v.get_instance_id()):
		return
	_watched[v.get_instance_id()] = true
	_link(v, "bullet_homing_target_reached", func(vv, i, _t, _p) -> void: _alive("bullet_homing_target_reached", vv, i))
	_link(v, "sprite_animation_finished", func(vv) -> void:
		_count("signal:sprite_animation_finished")
		if not is_instance_valid(vv) or vv.is_pooled() or vv.debug_get_life_state() != "active":
			_problem("sprite_animation_finished from a pooled or inactive volley"))


func _connect_bullet_signals(emitter: Object) -> void:
	for name in ["area_entered", "body_entered", "bounce_area_entered", "bounce_body_entered"]:
		_link(emitter, name, func(_t, v, i) -> void: _alive(name, v, i))
	_link(emitter, "life_time_over", func(v, idx) -> void:
		for i in idx:
			_alive("life_time_over", v, i))
	_link(emitter, "bullet_grazed", func(_t, v, i, _z, _r) -> void: _alive("bullet_grazed", v, i))
	_link(emitter, "bullet_graze_exited", func(_t, v, i, _z, _r) -> void: _alive("bullet_graze_exited", v, i))


func _alive(name: String, v: Object, i: int) -> void:
	_count("signal:" + name)
	if not is_instance_valid(v):
		_problem("%s: a freed volley" % name)
		return
	var volley := v as BulletVolley2D
	watch_volley(volley)
	if volley.is_pooled():
		_problem("%s: a pooled volley (bullet %d)" % [name, i])
	elif i < 0 or i >= volley.get_amount_bullets() or not volley.is_bullet_status_enabled(i):
		_problem("%s: bullet %d of volley %d is not alive in its handler" % [name, i, volley.get_instance_id()])
	elif not volley.get_bullet_transform(i).is_finite():
		_problem("%s: bullet %d has a non-finite pose in its handler" % [name, i])


func _on_split(pos: Vector2, children: Object) -> void:
	_count("signal:bullet_split")
	if not pos.is_finite():
		_problem("bullet_split at a non-finite position")
	if not is_instance_valid(children):
		_problem("bullet_split handed out a freed children volley")
		return
	var c := children as BulletVolley2D
	watch_volley(c)
	if c.is_pooled() or c.debug_get_life_state() != "active":
		_problem("bullet_split handed out a pooled or inactive children volley")


func _note_out(name: String, v: Object, idx: Array) -> void:
	_count("signal:" + name)
	if is_instance_valid(v):
		_pending_out.append([Engine.get_physics_frames(), v.get_instance_id(), idx.duplicate(), "despawn" if name == "bullets_despawned" else "clear", v.get_life_id()])


func _count(key: String, n := 1) -> void:
	counts[key] = int(counts.get(key, 0)) + n


func _problem(text: String) -> void:
	problem_count += 1
	if problem_list.size() < 32:
		problem_list.append("[frame %d] %s" % [Engine.get_physics_frames(), text])


## Reads and clears the factory logs, checking every entry. Call once per
## frame (and before problems()).
func pull() -> void:
	if not is_instance_valid(_factory):
		return
	if _factory.debug_get_bullet_event_log_dropped() > 0:
		_problem("the bullet event log overflowed (pull every frame)")
	var events: Array = _factory.debug_get_bullet_event_log()
	_factory.debug_clear_bullet_event_log()
	var offers: Array = _factory.debug_get_sound_offer_log()
	_factory.debug_clear_sound_offer_log()
	var effects: Array = _factory.debug_get_effect_log()
	_factory.debug_clear_effect_log()
	for e in events:
		_apply_event(e)
	for o in offers:
		_count("offer:%d" % int(o["trigger"]))
		if int(o["volley"]) != 0 and int(o["bullet"]) >= 0:
			_check_report("sound trigger %d" % int(o["trigger"]), int(o["trigger"]), true, int(o["volley"]), int(o["life"]), int(o["bullet"]), int(o["frame"]))
	for f in effects:
		_count("effect:%d" % int(f["trigger"]))
		_check_report("effect trigger %d" % int(f["trigger"]), int(f["trigger"]), false, int(f["volley"]), int(f["life"]), int(f["bullet"]), int(f["frame"]))
	for out in _pending_out:
		# The payload names the life that just lost these bullets (the
		# volley may be pooled by then, or even reused later that frame).
		for i in out[2]:
			var left = null
			for iv in _intervals.get(_key(int(out[1]), int(i)), []):
				if int(iv[2]) == int(out[4]) and int(iv[1]) >= 0 and int(iv[1]) <= int(out[0]) and String(iv[4]) == out[3]:
					left = iv
			if left == null:
				_problem("bullets_%s names bullet %d of volley %d life %d, which did not leave that way (%s)" % ["despawned" if out[3] == "despawn" else "cleared", int(i), int(out[1]), int(out[4]), str(_intervals.get(_key(int(out[1]), int(i)), []))])
	_pending_out.clear()


func _key(volley: int, bullet: int) -> String:
	return "%d:%d" % [volley, bullet]


func _last(volley: int, bullet: int) -> Variant:
	var list: Array = _intervals.get(_key(volley, bullet), [])
	return null if list.is_empty() else list[-1]


func _apply_event(e: Dictionary) -> void:
	var kind: String = e["kind"]
	var volley := int(e["volley"])
	var life := int(e["life"])
	var frame := int(e["frame"])
	if kind == "life":
		_count("life")
		for b in int(e["bullet"]):
			var iv = _last(volley, b)
			if iv != null and int(iv[1]) < 0:
				_problem("volley %d began life %d while bullet %d of life %d was still alive" % [volley, life, b, int(iv[2])])
				iv[1] = frame
				iv[4] = "relife"
			_open(volley, b, frame, life, -1)
		return
	var bullet := int(e["bullet"])
	var epoch := int(e["epoch"])
	var iv = _last(volley, bullet)
	if kind == "wake":
		_count("wake")
		if iv != null and int(iv[1]) < 0:
			_problem("volley %d bullet %d woke while alive" % [volley, bullet])
		elif iv != null and int(iv[2]) != life:
			_problem("volley %d bullet %d woke into life %d, it died in life %d" % [volley, bullet, life, int(iv[2])])
		_open(volley, bullet, frame, life, epoch)
		return
	var cause: String = e["cause"]
	_count("disable:" + cause)
	if iv == null or int(iv[1]) >= 0:
		_problem("volley %d bullet %d (life %d) left twice or was never alive (cause %s, last %s)" % [volley, bullet, life, cause, str(iv)])
		return
	if int(iv[2]) != life:
		_problem("volley %d bullet %d left in life %d, alive in life %d" % [volley, bullet, life, int(iv[2])])
	if int(iv[3]) >= 0 and int(iv[3]) != epoch:
		_problem("volley %d bullet %d left with epoch %d, woke with %d" % [volley, bullet, epoch, int(iv[3])])
	iv[1] = frame
	iv[4] = cause


func _open(volley: int, bullet: int, frame: int, life: int, epoch: int) -> void:
	var k := _key(volley, bullet)
	if not _intervals.has(k):
		_intervals[k] = []
	(_intervals[k] as Array).append([frame, -1, life, epoch, ""])


## The interval of (volley, bullet) alive at `frame` in `life` (closed in
## that frame at the latest), or null.
func _alive_at(volley: int, bullet: int, life: int, frame: int) -> Variant:
	var list: Array = _intervals.get(_key(volley, bullet), [])
	for k in range(list.size() - 1, -1, -1):
		var iv: Array = list[k]
		if int(iv[2]) == life and int(iv[0]) <= frame and (int(iv[1]) < 0 or int(iv[1]) >= frame):
			return iv
	return null


# A manual split leaves like a clear (documented: all_bullets_split).
const _DEATH_SOUND := {2: ["hit"], 4: ["lifetime"], 5: ["clear", "pickup", "split_manual"], 11: ["despawn"]}
const _DEATH_EFFECT := {3: ["hit"], 5: ["lifetime"], 6: ["clear", "pickup", "split_manual"], 7: ["despawn"]}
const _EFFECT_ON_SPAWN := 1


func _check_report(label: String, trigger: int, sound: bool, volley: int, life: int, bullet: int, frame: int) -> void:
	var iv = _alive_at(volley, bullet, life, frame)
	if iv == null:
		_problem("%s for bullet %d of volley %d life %d, which was not alive then (intervals %s)" % [label, bullet, volley, life, str(_intervals.get(_key(volley, bullet), []))])
		return
	var deaths: Dictionary = _DEATH_SOUND if sound else _DEATH_EFFECT
	if deaths.has(trigger):
		if int(iv[1]) != frame or not (deaths[trigger] as Array).has(iv[4]):
			_problem("%s for bullet %d of volley %d, which left by '%s' at frame %d" % [label, bullet, volley, iv[4], int(iv[1])])
		var k := "%s:%d:%d:%d:%s" % [_key(volley, bullet), life, int(iv[0]), trigger, "s" if sound else "e"]
		_per_life[k] = int(_per_life.get(k, 0)) + 1
		if int(_per_life[k]) > entry_limit:
			_problem("%s fired %d times for one death of bullet %d of volley %d" % [label, int(_per_life[k]), bullet, volley])
	elif not sound and trigger == _EFFECT_ON_SPAWN:
		if int(iv[0]) != frame or int(iv[3]) >= 0:
			_problem("%s for bullet %d of volley %d outside its life start" % [label, bullet, volley])
	elif sound and per_frame_limits and (trigger == S_FLIGHT or trigger == S_BOUNCE):
		var k := "%s:%d:%d:%d" % [_key(volley, bullet), life, frame, trigger]
		_per_frame[k] = int(_per_frame.get(k, 0)) + 1
		if int(_per_frame[k]) > entry_limit:
			_problem("%s offered %d times in one tick for bullet %d of volley %d" % [label, int(_per_frame[k]), bullet, volley])


## Every problem so far (pulls first). Empty = the run was consistent.
func problems() -> Array[String]:
	pull()
	var out: Array[String] = problem_list.duplicate()
	if problem_count > problem_list.size():
		out.append("... %d more" % (problem_count - problem_list.size()))
	return out


## How every closed bullet life ended: {cause: count}, plus "alive".
func terminal_summary() -> Dictionary:
	pull()
	var out := {}
	for k in _intervals:
		for iv in _intervals[k]:
			var cause: String = "alive" if int(iv[1]) < 0 else iv[4]
			out[cause] = int(out.get(cause, 0)) + 1
	return out


## Bullets alive now by the ledger (intervals still open on live volleys).
func alive_count() -> int:
	pull()
	var n := 0
	for k in _intervals:
		var iv: Array = (_intervals[k] as Array)[-1]
		if int(iv[1]) < 0 and is_instance_valid(instance_from_id(int(String(k).get_slice(":", 0)))):
			n += 1
	return n


func detach() -> void:
	for l in _links:
		if is_instance_valid(l[0]) and (l[0] as Object).is_connected(l[1], l[2]):
			(l[0] as Object).disconnect(l[1], l[2])
	_links.clear()
	if is_instance_valid(_factory):
		_factory.debug_set_bullet_event_log_enabled(false)
		_factory.debug_set_sound_offer_log_enabled(false)
		_factory.debug_set_effect_log_enabled(false)
