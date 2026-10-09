# BlastBullets2D Quickstart

Goal: a boss that fires a spiral at your player, with hits, graze and a bullet budget, in about ten minutes. Everything here is also documented in the editor (F1 → search `BulletSpawner2D`).

## The one idea: use `BulletSpawner2D`

You can call `BulletFactory2D.spawn_volley()` yourself, but for a bullet hell **start with `BulletSpawner2D`**. It already does the things you would otherwise rebuild: 31 ready-made patterns (ring, fan, spiral, flower, rose, rain, walls with gaps...), a live preview in the editor, fire rate, bursts, telegraphs, spin, homing, graze, sound, moving along a `Path2D`, and the bake cache that makes repeated shots cheap. Reach for the factory directly only for one-off effects and scripted shots.

| Node / resource | Job |
|---|---|
| `BulletFactory2D` | One per game/level. Owns the pools, physics and rendering of every bullet. |
| `BulletSpawner2D` | One per gun/boss/turret. Decides **when and where** bullets spawn. |
| `BulletVolleyData2D` | **What** a bullet is: art, speed, lifetime, collision, hit counts. |

## 1. Set up the scene

1. Add a `BulletFactory2D` to your level. Keep one reference to it (autoload, or a `static var`), see the README.
2. Add a `BulletSpawner2D` under your boss. Set **Bullet Factory Path** to the factory.
3. Create a `BulletVolleyData2D` in **Spawn Data** and fill in:
   - `sprite_frames` (your bullet art) and `texture_size`
   - `collision_shape` (circle, rectangle or capsule), `collision_layer`, `collision_mask`
   - `shared_bullet_speed_data` → a `BulletSpeedData2D` (`speed`, `max_speed`, optional `acceleration`)
   - `max_life_time`
   Leave `transforms` alone: the spawner's pattern provides the positions.
4. In **Bullet Patterns** pick a `pattern_source` (for example Spiral) and set `helper_bullets_amount`. The preview shows exactly where bullets will start.
5. In **Shooting** turn on `shooting_enabled` and set `shoot_interval_sec`. Press play.

> Per-bullet speed arrays need exactly one entry per bullet. Because a spawner's count can change with the pattern, use the **shared** fields (`shared_bullet_speed_data`, `shared_bullets_custom_data`) unless you really want bullet 3 to differ from bullet 4.

## 2. Things a bullet hell almost always needs

| I want to... | Do this |
|---|---|
| Try patterns fast | `apply_pattern_preset(BulletPatterns2D.PATTERN_PRESET_SPIRAL_3ARM)`, then tune the `helper_*` group |
| Make the boss spin its fire | `spin_enabled`, `spin_mode`, `spin_speed_deg_per_sec` |
| Fire in waves | `burst_enabled`, `burst_count`, `burst_interval_sec` (optional `burst_alternate_mirror`) |
| Warn the player first | `telegraph_enabled` and `telegraph_sec`; react to `volley_telegraphed` |
| Chain attacks | `spawn_pattern_list(...)`; the `pattern_list_finished` signal tells you when to pick the next one |
| Aim at the player | `pattern_source` = Aimed, or enable `homing_enabled` with `homing_target_source` = Node Group and `homing_node_group` = your player group |
| Hurt the player | Connect the spawner's `area_entered` / `body_entered` signals (see below) |
| Reward near misses | `graze_enabled`, `graze_node_group`, and one or more `BulletGrazeZone2D` in `graze_zones` |
| Cap the screen | `max_live_bullets` on the spawner (0 = unlimited): shots are skipped with `volley_skipped("over_budget")` |
| Move the boss | `movement_enabled` with a `Path2D` in `movement_path`; shots inherit its speed (`inherit_movement_velocity`) |
| Wipe the screen (bomb, player death, phase change) | `factory.clear_active_bullets()` (or the `_deferred` twin from a handler) |
| Stop and resume | `pause_shooting()`, `resume_shooting()`, `reset_shooting()`; `shoot_once()` for scripted shots |
| Boss dies mid-attack | `orphaned_volleys` decides if its bullets keep flying or vanish |

### Hits and graze, in code

```gdscript
extends Node

@onready var boss_gun: BulletSpawner2D = $Boss/BulletSpawner2D

func _ready() -> void:
	boss_gun.area_entered.connect(_on_bullet_hit_area)
	boss_gun.bullet_grazed.connect(_on_graze)

# The bullet is still alive in the handler: read its data, then let it die.
func _on_bullet_hit_area(target: Object, volley: BulletVolley2D, bullet_index: int) -> void:
	var data := volley.bullet_get_custom_data(bullet_index) # your own Resource, e.g. damage
	if target.has_method("take_hit"):
		target.take_hit(data.damage if data else 1)

func _on_graze(_target: Node2D, _volley: BulletVolley2D, _index: int, _zone: BulletGrazeZone2D, ring_index: int) -> void:
	score += 10 * (ring_index + 1) # ring_index 0 = ring_1_radius, 1 = ring_2_radius, ...
```

Spawner volleys signal on the **spawner**, factory-spawned volleys on the **factory**, never both. Graze is the exception: it reaches the spawner first and always the factory too.

### Spawning a one-off volley from code

```gdscript
var data := preload("res://bullets/sparkle.tres").duplicate() as BulletVolleyData2D # never edit the shared resource
data.transforms = BulletPatterns2D.helper_generate_transforms_ring(24, Transform2D(0.0, boss.global_position))
var volley := factory.spawn_volley(data)
```

## 3. Performance habits

- **Keep bullet counts steady per volley.** Pooling is by exact `(bullet count, shape)`; a repeat of the same size is a cheap reuse, a new size is a fresh allocation. Constant-size patterns are ideal. Pre-warm the sizes you use with `factory.populate_bullets_pool(key, data, n)` at load time (`factory.debug_expected_pool_key(data)` gives you the key).
- **Fewer, bigger volleys beat many tiny ones**: one volley is one MultiMesh draw and one physics area. All bullets in a volley share one texture, so put different looks in different spawners/data.
- Use areas for enemies and the player; static bodies need `monitorable` on and cost more.
- Turn on `factory.use_physics_interpolation` (and the matching project setting) for smooth bullets on 120 Hz+ screens.
- Watch the numbers with `factory.get_frame_stats()` or `register_performance_monitors`, and see the shapes with the factory's debugger (`is_debugger_enabled`).
- The per-call bullet limit defaults to 20,000 and mainly catches typos. Raise it with the project setting `blastbullets2d/patterns/max_bullets_per_pattern` or `BulletPatterns2D.set_max_bullets_per_pattern(n)`, but know the cost: one volley is one physics area with a shape per bullet, and tens of thousands of tightly packed shapes make Godot's broadphase hitch for seconds and can abort the game. For bigger barrages fire several volleys of a few thousand bullets (several spawners, or `burst_count`).

## 4. Gotchas that bite first-timers

- **Structural calls** (`reset`, `free_*`, `populate_*`) are rejected inside physics frames and signal handlers: use the `*_deferred` versions there.
- Use `queue_free()` on volleys, never `free()`. A pooled volley can be handed out again later, so do not keep a stale reference to an expired one.
- A spawner that is not ready, has no factory or no spawn data skips its shot and says why in `volley_skipped`. Check the editor warnings on the node (`get_setup_warnings()`).
- Not supported: Y-sort, polygon collision shapes, bullet-to-bullet collision, save/load of live bullets.

## Where next

[README.md](README.md) for the full feature list and installation, the in-editor class docs for every property, and `test_project/` for a runnable benchmark scene.
