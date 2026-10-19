extends BlastTest
## The plugin's helper nodes (the factory's volley, attachment and effect
## containers, the collision debugger and its multimesh nodes, the sound
## voices, effect and trail shards) are ordinary children a script can see
## and free(). The contract: a freed helper is never touched again (no freed
## memory: tools/run_sanitizer_tests.py runs this suite under ASan), the
## factory recreates what it needs, and spawning, ticking, hits, effects,
## sounds, pooling, the debugger and teardown keep working.

const FACTORY_CHILDREN := ["BulletVolleysContainer", "BulletAttachmentsContainer", "BulletVolleysDebugger"]

var probe_scene: PackedScene


func before_each() -> void:
	await super()
	if probe_scene == null:
		var probe := BulletAttachment2D.new()
		probe_scene = PackedScene.new()
		assert_eq(probe_scene.pack(probe), OK, "attachment scene packs")
		probe.free()


## A moving volley with an attachment, an On Hit flash, a trail and a hit
## sound that flies into a wall at x = 60.
func _rich_data() -> BulletVolleyData2D:
	var d := H.make_volley_data(3, 600.0, 30.0)
	d.transforms = [Transform2D(), Transform2D(0.0, Vector2(0, 30)), Transform2D(0.0, Vector2(0, 60))]
	d.set_collision_mask_from_array([3])
	d.collision_shape = H.make_circle_shape(6.0)
	d.bullet_max_collision_count = 1
	d.shared_bullet_attachment = probe_scene
	d.effect_layers = [H.make_effect_layer(BulletEffectLayerData2D.EFFECT_TRAIL_FOLLOW, 3), H.make_effect_layer(BulletEffectLayerData2D.EFFECT_ON_HIT, 3)]
	return d


## Everything that touches the helper nodes, after the given node was freed.
## Returns the problems found.
func _exercise(where: String) -> Array:
	var problems: Array = []
	make_wall(Vector2(60, 0))
	factory.body_entered.connect(func(_b, _v, _i) -> void: pass)
	factory.is_debugger_enabled = true
	factory.debugger_color = Color(1, 0, 0, 0.5)
	if factory.debugger_color != Color(1, 0, 0, 0.5):
		problems.append("%s: debugger_color did not round-trip" % where)
	factory.debugger_max_providers = 4
	factory.debugger_draw_inactive = true
	var v: BulletVolley2D = factory.spawn_volley(_rich_data())
	if v == null:
		problems.append("%s: spawn refused" % where)
		return problems
	if not v.is_inside_tree():
		problems.append("%s: the volley is not in the tree" % where)
	assert_true(factory.play_sound(H.make_sound(BulletSoundData2D.SOUND_ON_HIT), Vector2.ZERO), "play_sound accepted")
	for k in 20:
		await physics(1)
	await idle(1) # structural calls run at an idle point
	factory.populate_attachments_pool(probe_scene, 2)
	factory.debug_get_total_attachments_amount()
	factory.clear_active_bullets()
	await idle(1)
	if factory.spawn_volley(_rich_data()) == null:
		problems.append("%s: pooled respawn refused" % where)
	factory.stop_sounds()
	factory.is_debugger_enabled = false
	factory.is_debugger_enabled = true
	await physics(2)
	await idle(1)
	var books: Dictionary = factory.debug_assert_no_dangling()
	if not books["ok"]:
		problems.append("%s: %s" % [where, books["error"]])
	return problems


func test_freeing_a_factory_helper_child_never_breaks_the_factory(child_name: String = use_parameters(FACTORY_CHILDREN)) -> void:
	factory.spawn_volley(_rich_data())
	await physics(2)
	var child: Node = factory.get_node_or_null(child_name)
	assert_not_null(child, "%s exists" % child_name)
	child.free()
	var problems := await _exercise(child_name)
	assert_eq("\n".join(problems), "", "the factory survives losing %s" % child_name)
	assert_not_null(factory.get_node_or_null(child_name), "%s was recreated" % child_name)


func test_freeing_effect_containers_and_shards_never_breaks_effects() -> void:
	var v: BulletVolley2D = factory.spawn_volley(_rich_data())
	make_wall(Vector2(60, 0))
	factory.body_entered.connect(func(_b, _v, _i) -> void: pass)
	for k in 20: # the On Hit flash creates the effects container
		await physics(1)
		if factory.get_node_or_null("SpriteEffectsContainer") != null:
			break
	var effects: Node = factory.get_node_or_null("SpriteEffectsContainer")
	assert_not_null(effects, "the hit flash made the effects container")
	if effects != null:
		effects.free()
	# The trail shards are children of the volley: free them mid-flight.
	var fresh: BulletVolley2D = factory.spawn_volley(_rich_data())
	await physics(1)
	for c in fresh.get_children(true):
		if c is MultiMeshInstance2D:
			c.free()
	var problems := await _exercise("effects")
	assert_eq("\n".join(problems), "", "effects survive their nodes being freed")
	assert_true(is_instance_valid(v), "first volley alive (pooled or flying)")


func test_freeing_sound_voices_never_breaks_the_mixer() -> void:
	godot_listener_at(Vector2.ZERO)
	var s := H.make_sound(BulletSoundData2D.SOUND_ON_HIT)
	assert_true(factory.play_sound(s, Vector2.ZERO), "offer accepted")
	await physics(2)
	var voices: Node = factory.get_node_or_null("SoundVoices")
	assert_not_null(voices, "the first play made the voice container")
	if voices != null and voices.get_child_count(true) > 0:
		voices.get_child(0, true).free() # a live voice
	factory.stop_sounds()
	factory.set_is_factory_processing_bullets(false)
	factory.set_is_factory_processing_bullets(true)
	for k in 3:
		assert_true(factory.play_sound(s, Vector2(k, 0)), "offer %d accepted" % k)
		await physics(1)
	voices = factory.get_node_or_null("SoundVoices")
	if voices != null:
		voices.free()
	factory.stop_sounds()
	assert_true(factory.play_sound(s, Vector2.ZERO), "offer after the container died")
	await physics(2)
	factory.debug_get_sound_voices()
	var problems := await _exercise("sound")
	assert_eq("\n".join(problems), "", "sound survives its voices being freed")


func test_freeing_debugger_multimesh_nodes_never_breaks_the_debugger() -> void:
	factory.is_debugger_enabled = true
	factory.spawn_volley(_rich_data())
	await physics(2)
	var debugger: Node = factory.get_node("BulletVolleysDebugger")
	assert_gt(debugger.get_child_count(true), 0, "the debugger draws the volley")
	debugger.get_child(0, true).free()
	factory.spawn_volley(_rich_data())
	await physics(2)
	if debugger.get_child_count(true) > 0:
		var mmi := debugger.get_child(0, true) as MultiMeshInstance2D
		if mmi != null:
			mmi.multimesh = null
	await physics(2)
	var problems := await _exercise("debugger nodes")
	assert_eq("\n".join(problems), "", "the debugger survives its nodes being freed")
