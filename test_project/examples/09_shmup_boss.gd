extends Node2D
## A small, complete shmup fight. Everything is a plain BulletSpawner2D driven
## from code:
## - three phases at 100 / 66 / 33 % health, each with its own bullet colors
##   (bullet_color_mode + bullet_color_gradient), strafe speed and patterns;
## - inside a phase the boss switches patterns every few seconds;
## - phase three is a RAGE: faster patterns, faster strafing, a red pulse;
## - a phase break clears the screen with an explosion (a moment to breathe);
## - health bars for both sides, a short invulnerability blink after a hit;
## - "BOSS DEFEATED" / "YOU DIED" screens; R plays again.
## One handler on the factory takes every hit (damage from custom data).

const FX := preload("res://examples/common/example_fx.gd")
const PLAYER_LAYER := 2
const BOSS_MAX_HP := 240
const PLAYER_MAX_HP := 8
const INVULNERABLE_SEC := 0.9

const PHASES := [
	{"title": "PHASE 1", "below": 1.01, "colors": [Color(0.3, 0.9, 1.0), Color(0.55, 0.55, 1.0)],
		"strafe": 130.0, "switch_sec": 4.0, "tint": Color(1, 1, 1), "patterns": ["ring", "spiral"]},
	{"title": "PHASE 2", "below": 0.66, "colors": [Color(1.0, 0.4, 0.9), Color(1.0, 0.85, 0.3)],
		"strafe": 190.0, "switch_sec": 3.2, "tint": Color(1.2, 1.0, 1.2), "patterns": ["flower", "aimed", "spiral"]},
	{"title": "RAGE!", "below": 0.33, "colors": [Color(1.0, 0.25, 0.15), Color(1.0, 0.75, 0.2)],
		"strafe": 280.0, "switch_sec": 2.2, "tint": Color(1.7, 0.6, 0.55), "patterns": ["multispiral", "aimed", "ring"]},
]

var boss_hp := BOSS_MAX_HP
var player_hp := PLAYER_MAX_HP
var phase := -1
var over := false
var _pattern := 0
var _switch_in := 0.0
var _invulnerable_until := 0.0
var _hits_taken := 0
var _started_msec := 0
@onready var factory: BulletFactory2D = $BulletFactory2D
@onready var boss: BulletSpawner2D = $Boss
@onready var player: Area2D = $Player


func _ready() -> void:
	boss.add_to_group(&"enemy")
	factory.area_entered.connect(_on_bullet_hit)
	_started_msec = Time.get_ticks_msec()
	_start.call_deferred() # the HUD joins the tree a frame after the player


func _start() -> void:
	_show_health()
	enter_phase(0)


func _process(delta: float) -> void:
	if over or phase < 0:
		return
	_switch_in -= delta
	if _switch_in <= 0.0:
		_pattern += 1
		_apply_pattern()
	if phase == PHASES.size() - 1: # rage: the hull pulses red
		var pulse := 0.5 + 0.5 * sin(Time.get_ticks_msec() / 90.0)
		$Boss/Ship.self_modulate = (PHASES[phase].tint as Color).lerp(Color(2.2, 0.4, 0.4), pulse)


func _unhandled_input(event: InputEvent) -> void:
	if over and event is InputEventKey and event.pressed and event.physical_keycode == KEY_R:
		get_tree().reload_current_scene()


func enter_phase(index: int) -> void:
	phase = index
	var p: Dictionary = PHASES[index]
	_pattern = 0
	boss.movement_speed = p.strafe
	$Boss/Ship.self_modulate = p.tint
	var gradient := Gradient.new()
	gradient.colors = PackedColorArray(p.colors)
	boss.bullet_color_gradient = gradient
	boss.bullet_color_mode = BulletSpawner2D.COLOR_MODE_GRADIENT_ACROSS_PATTERN
	if index > 0: # a phase break: an explosion and a clear screen
		FX.explosion(factory, boss.global_position, 1.4)
		factory.clear_bullets_in_circle(boss.global_position, 4000.0, PLAYER_LAYER)
	player.hud.announce(p.title, p.colors[0])
	_apply_pattern()


func _apply_pattern() -> void:
	var p: Dictionary = PHASES[phase]
	_switch_in = p.switch_sec
	boss.burst_enabled = false
	boss.spin_enabled = true
	var rage := phase == PHASES.size() - 1
	match p.patterns[_pattern % p.patterns.size()]:
		"ring":
			boss.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_RING
			boss.helper_bullets_amount = 26 if rage else 18
			boss.helper_ring_radius = 40.0
			boss.shoot_interval_sec = 0.35 if rage else 0.55
			boss.spin_speed_deg_per_sec = 70.0
		"spiral":
			boss.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_SPIRAL
			boss.helper_bullets_amount = 6
			boss.shoot_interval_sec = 0.12
			boss.spin_speed_deg_per_sec = 150.0
		"flower":
			boss.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_FLOWER
			boss.helper_bullets_amount = 30
			boss.helper_flower_petals = 5
			boss.shoot_interval_sec = 0.7
			boss.spin_speed_deg_per_sec = 40.0
		"aimed": # fans at the player, in bursts of three
			boss.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_AIMED
			boss.helper_aimed_target = boss.get_path_to(player)
			boss.helper_bullets_amount = 7 if rage else 5
			boss.shoot_interval_sec = 0.6 if rage else 0.8
			boss.spin_enabled = false
			boss.burst_enabled = true
			boss.burst_count = 3
			boss.burst_interval_sec = 0.09
		"multispiral":
			boss.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_MULTISPIRAL
			boss.shoot_interval_sec = 0.07
			boss.spin_speed_deg_per_sec = 190.0


## Every bullet in the fight reports here (no spawner has a handler of its own).
func _on_bullet_hit(target: Object, volley: BulletVolley2D, bullet_index: int) -> void:
	if over:
		return
	var at := volley.get_bullet_transform(bullet_index).origin
	if target == $Boss/Hurtbox:
		boss_hp = maxi(boss_hp - _damage(volley, bullet_index), 0)
		FX.hit_spark(factory, at)
		_show_health()
		if boss_hp == 0:
			_boss_defeated()
		elif phase + 1 < PHASES.size() and float(boss_hp) / BOSS_MAX_HP < PHASES[phase + 1].below:
			enter_phase(phase + 1)
	elif target == player and Time.get_ticks_msec() / 1000.0 >= _invulnerable_until:
		player_hp = maxi(player_hp - _damage(volley, bullet_index), 0)
		_hits_taken += 1
		player.hit()
		_show_health()
		if player_hp == 0:
			_player_died()
			return
		_invulnerable_until = Time.get_ticks_msec() / 1000.0 + INVULNERABLE_SEC
		var blink := create_tween().set_loops(4)
		blink.tween_property(player.get_node("Ship"), "modulate:a", 0.25, INVULNERABLE_SEC / 8.0)
		blink.tween_property(player.get_node("Ship"), "modulate:a", 1.0, INVULNERABLE_SEC / 8.0)


func _boss_defeated() -> void:
	over = true
	var at := boss.global_position # read before anything moves it
	boss.shooting_enabled = false
	boss.movement_stop(false) # stay where it died
	factory.clear_bullets_in_circle(at, 4000.0, PLAYER_LAYER)
	FX.boss_death(factory, at, 80.0)
	boss.hide()
	$Boss/Hurtbox.set_deferred("monitorable", false)
	player.can_fire = false
	var seconds := (Time.get_ticks_msec() - _started_msec) / 1000.0
	player.hud.show_end_screen("BOSS DEFEATED", "Time %.1f s    Hits taken %d" % [seconds, _hits_taken], Color(0.4, 1.0, 0.7))


func _player_died() -> void:
	over = true
	FX.explosion(factory, player.global_position, 1.3)
	player.can_fire = false
	player.hide()
	player.set_deferred("monitorable", false)
	boss.shooting_enabled = false
	player.hud.show_end_screen("YOU DIED", "The boss had %d%% health left" % roundi(100.0 * boss_hp / BOSS_MAX_HP), Color(1.0, 0.35, 0.35))


func _show_health() -> void:
	player.hud.set_health("boss", "BOSS", boss_hp, BOSS_MAX_HP, Color(1.0, 0.3, 0.45))
	player.hud.set_health("player", "YOU", player_hp, PLAYER_MAX_HP, Color(0.3, 0.9, 1.0))


func _damage(volley: BulletVolley2D, bullet_index: int) -> int:
	var data: Resource = volley.bullet_get_custom_data(bullet_index)
	if data == null:
		data = volley.shared_bullets_custom_data
	return data.amount if data != null else 1
