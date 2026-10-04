extends BlastTest
## Lifetime contract. life_time_over(volley, indexes) fires LIVE from the
## factory tick (inside the physics frame) at the moment the volley's
## lifetime runs out, with every listed bullet still alive: status,
## attachment, custom data and pose are all readable. Only AFTER the handler
## does the plugin expire the bullets that are still the same live bullets
## (On Lifetime Over once each). The handler can veto the expiry for the
## whole volley (set_is_life_time_infinite(true) or set_life_time_left(t)),
## and owns any bullet it disabled or woke itself.

const LIFETIME_FX := BulletEffectLayerData2D.EFFECT_ON_LIFETIME_OVER

var events: Array = [] # one Dictionary per life_time_over emit
var on_expire: Callable = Callable()


func _data(n: int = 3, lifetime: float = 0.2) -> BulletVolleyData2D:
	var d := H.make_still_data(n)
	d.max_life_time = lifetime
	d.is_life_time_over_signal_enabled = true
	d.effect_layers = [H.make_effect_layer(LIFETIME_FX, 4)]
	return d


func _on_life_time_over(volley: BulletVolley2D, indexes: Array) -> void:
	var alive: Array = []
	var attached: Array = []
	var customs: Array = []
	for i in indexes:
		alive.append(volley.is_bullet_status_enabled(i))
		attached.append(volley.bullet_get_attachment(i) != null)
		customs.append(volley.bullet_get_custom_data(i))
	events.append({
		"volley": volley,
		"indexes": indexes.duplicate(),
		"alive": alive,
		"attached": attached,
		"customs": customs,
		"in_physics": Engine.is_in_physics_frame(),
		"pooled": bool(volley.debug_get_volley_info().get("is_pooled", true)),
	})
	if on_expire.is_valid():
		on_expire.call(volley, indexes)


func before_each() -> void:
	await super()
	events.clear()
	on_expire = Callable()
	factory.life_time_over.connect(_on_life_time_over)
	factory.debug_set_effect_log_enabled(true)


func after_each() -> void:
	factory.debug_set_effect_log_enabled(false)
	await super()


func _wait_events(count: int, frames := 120) -> void:
	for i in frames:
		await physics()
		if events.size() >= count:
			break


func _fx_count() -> int:
	var n := 0
	for e in factory.debug_get_effect_log():
		if int(e["trigger"]) == LIFETIME_FX:
			n += 1
	return n


func test_handler_sees_every_expiring_bullet_alive() -> void:
	var d := _data(3)
	var customs := [Resource.new(), Resource.new(), Resource.new()]
	d.all_bullets_custom_data = customs
	d.shared_bullet_attachment = make_probe_scene()
	var v: BulletVolley2D = factory.spawn_volley(d)
	await _wait_events(1)
	assert_eq(events.size(), 1, "one life_time_over")
	var e: Dictionary = events[0]
	assert_same(e["volley"], v, "payload volley")
	assert_eq(e["indexes"], [0, 1, 2], "every live bullet listed")
	assert_eq(e["alive"], [true, true, true], "listed bullets are alive inside the handler")
	assert_eq(e["attached"], [true, true, true], "attachments still attached")
	for i in 3:
		assert_same(e["customs"][i], customs[i], "custom data %d readable" % i)
	assert_true(e["in_physics"], "emitted live from the factory tick")
	assert_false(e["pooled"], "volley not pooled before the handler ran")
	await physics()
	for i in 3:
		assert_false(v.is_bullet_status_enabled(i), "bullet %d expired after the handler" % i)
	assert_eq(_fx_count(), 3, "On Lifetime Over once per bullet")
	assert_eq(factory.debug_get_active_attachments_amount(), 0, "attachments released after the handler")


func test_infinite_from_the_handler_vetoes_the_expiry() -> void:
	on_expire = func(volley: BulletVolley2D, _idx): volley.set_is_life_time_infinite(true)
	var v: BulletVolley2D = factory.spawn_volley(_data(2))
	await _wait_events(1)
	await physics(30)
	assert_eq(events.size(), 1, "one emit")
	assert_true(v.is_bullet_status_enabled(0) and v.is_bullet_status_enabled(1), "vetoed: every bullet lives on")
	assert_eq(_fx_count(), 0, "no expiry effect for a vetoed expiry")


func test_life_time_left_from_the_handler_extends_the_volley() -> void:
	on_expire = func(volley: BulletVolley2D, _idx):
		if events.size() == 1:
			volley.set_life_time_left(0.25)
	var v: BulletVolley2D = factory.spawn_volley(_data(2, 0.2))
	await _wait_events(1)
	await physics()
	assert_true(v.is_bullet_status_enabled(0), "extended")
	assert_almost_eq(v.get_life_time_left(), 0.25 - 1.0 / 60.0, 0.02, "life_time_left reads the extension")
	await _wait_events(2)
	assert_eq(events.size(), 2, "expires again once the extension runs out")
	await physics()
	assert_false(v.is_bullet_status_enabled(0), "second expiry not vetoed")


func test_set_life_time_left_rejects_bad_values() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(1, 5.0))
	v.set_life_time_left(NAN)
	v.set_life_time_left(-1.0)
	v.set_life_time_left(0.0)
	expect_error_sequence([
		"set_life_time_left: seconds must be finite and > 0, keeping the old value.",
		"set_life_time_left: seconds must be finite and > 0, keeping the old value.",
		"set_life_time_left: seconds must be finite and > 0, keeping the old value.",
	])
	assert_almost_eq(v.get_life_time_left(), 5.0, 0.001, "old value kept")
	var inf: BulletVolley2D = factory.spawn_volley(H.make_still_data(1))
	inf.set_is_life_time_infinite(true)
	inf.set_life_time_left(1.0)
	expect_error_sequence(["set_life_time_left: this volley has an infinite lifetime (set_is_life_time_infinite(false) first)."])


func test_handler_owns_the_bullets_it_disabled() -> void:
	on_expire = func(volley: BulletVolley2D, _idx): volley.disable_bullet(1)
	var v: BulletVolley2D = factory.spawn_volley(_data(3))
	await _wait_events(1)
	await physics()
	assert_false(v.is_bullet_status_enabled(0), "expired")
	assert_false(v.is_bullet_status_enabled(1), "disabled by the handler")
	assert_false(v.is_bullet_status_enabled(2), "expired")
	assert_eq(_fx_count(), 2, "only the bullets the plugin expired fire On Lifetime Over")


func test_bullets_disabled_before_expiry_are_not_listed() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_data(3))
	v.disable_bullet(1)
	await _wait_events(1)
	assert_eq(events[0]["indexes"], [0, 2], "only live bullets are listed")


func test_signal_off_is_silent_but_still_expires() -> void:
	var d := _data(2)
	d.is_life_time_over_signal_enabled = false
	var v: BulletVolley2D = factory.spawn_volley(d)
	for i in 60:
		await physics()
		if not v.is_bullet_status_enabled(0):
			break
	assert_eq(events.size(), 0, "no life_time_over when the signal is off")
	assert_false(v.is_bullet_status_enabled(0) or v.is_bullet_status_enabled(1), "expired")
	assert_eq(_fx_count(), 2, "On Lifetime Over still fires once per bullet")


func test_same_key_spawn_inside_the_handler_gets_another_instance() -> void:
	var spawned: Array = []
	on_expire = func(_volley, _idx): spawned.append(factory.spawn_volley(_data(3, 5.0)))
	var v: BulletVolley2D = factory.spawn_volley(_data(3))
	await _wait_events(1)
	await physics(2)
	assert_eq(events.size(), 1, "exactly one expiry signal")
	assert_eq(spawned.size(), 1, "spawned from the handler")
	assert_not_same(spawned[0], v, "the expiring volley is never handed out mid-tick")
	assert_true(spawned[0].is_bullet_status_enabled(0), "the new volley lives")


func test_handler_free_is_safe() -> void:
	on_expire = func(volley: BulletVolley2D, _idx): volley.free()
	factory.spawn_volley(_data(2))
	await _wait_events(1)
	await physics(3)
	assert_eq(events.size(), 1, "one emit")
	assert_true(factory.debug_assert_no_dangling().get("ok", false), "factory consistent after a free in the handler")


func test_handler_queue_free_skips_the_expiry_work() -> void:
	on_expire = func(volley: BulletVolley2D, _idx): volley.queue_free()
	var v: BulletVolley2D = factory.spawn_volley(_data(2))
	await _wait_events(1)
	assert_eq(_fx_count(), 0, "a volley queued for deletion fires no expiry effects")
	await idle(2)
	assert_false(is_instance_valid(v), "freed at frame end")


func test_structural_call_from_the_handler_is_refused_with_advice() -> void:
	on_expire = func(_volley, _idx): factory.free_disabled_bullets()
	factory.spawn_volley(_data(1))
	await _wait_events(1)
	expect_error_sequence(["Use free_disabled_bullets_deferred() instead"])


func test_spawner_volley_reports_live_to_the_spawner() -> void:
	var got: Array = []
	var sp := make_spawner(_data(1), BulletSpawner2D.PATTERN_FROM_SELF, 1)
	sp.life_time_over.connect(func(volley: BulletVolley2D, idx: Array): got.append([idx.duplicate(), volley.is_bullet_status_enabled(int(idx[0]))]))
	assert_true(sp.shoot_once(), "fired")
	for i in 90:
		await physics()
		if not got.is_empty():
			break
	assert_eq(got, [[[0], true]], "spawner handler sees the expiring bullet alive")
	assert_eq(events.size(), 0, "factory stays silent for spawner volleys")
