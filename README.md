<p align="center">
  <img src="showcase/logo.png" alt="BlastBullets2D" width="640"/>
</p>

<p align="center">
  <b>Thousands of bullets, lasers and bullet-hell patterns for Godot 4, written in C++, driven from GDScript.</b><br>
  Free, open source (MIT), no C++ knowledge needed.
</p>

<p align="center">
  <a href="https://github.com/nikoladevelops/godot-blast-bullets-2d/actions/workflows/tests.yml"><img src="https://github.com/nikoladevelops/godot-blast-bullets-2d/actions/workflows/tests.yml/badge.svg" alt="Tests"></a>
  <a href="https://github.com/nikoladevelops/godot-blast-bullets-2d/releases"><img src="https://img.shields.io/github/v/release/nikoladevelops/godot-blast-bullets-2d" alt="Latest release"></a>
  <img src="https://img.shields.io/badge/Godot-4.5%2B-478cbf" alt="Godot 4.5+">
  <img src="https://img.shields.io/badge/license-MIT-green" alt="MIT license">
</p>

<p align="center">
  <img src="showcase/clips/hero.webp" alt="The Hive Mother's spiral waves in the Arcane Barrage demo" width="720"/>
</p>

**Contents:** [Why](#why-blastbullets2d) · [See it in action](#see-it-in-action) · [Examples](#learn-from-the-examples) · [Install](#install) · [Your first boss in 5 minutes](#your-first-boss-in-5-minutes-no-code) · [Handle hits](#handle-hits) · [Recipes](#recipes) · [Spawning from code](#spawning-from-code) · [Performance](#performance) · [Limits](#limits) · [Rules worth knowing](#rules-worth-knowing) · [Reference](#reference) · [Building and testing](#building-from-source-and-running-the-tests)

## Why BlastBullets2D

- **Fast.** One volley is one MultiMesh draw and one physics area, not one node per bullet. 10,000 flying bullets cost about **0.3 ms** per physics tick, and 8 auto-firing bosses with homing, splitting and graze keeping ~10,000 bullets alive about **0.5 ms** (Ryzen 7 8840HS, the shipped release build). Volleys, lasers and attachments are pooled automatically.
- **Patterns without math.** `BulletSpawner2D` has **33 pattern sources** (ring, fan, spiral, flower, rose, star, heart, rain, walls with gaps, Lissajous, any `Path2D`, your own markers...) with a **live preview in the editor**, 20 one-click presets, spin, bursts, telegraphs, fire budgets and boss phase sequences.
- **Every bullet-hell system, already integrated:** homing and orbiting, bouncing, curves, wobble, gravity, `Path2D` movement patterns, graze (near misses), boids flocking, bullets that split, bullets that fire bullets, lasers (beams, bolts, curvy snakes), bullets that turn into coins, a sound mixer, slow motion, per-bullet colors and lifetimes, animated `SpriteFrames` art, particles that follow bullets.
- **Safe to use.** Every signal fires while the bullet is still alive (read it, heal it, split it), handlers may free anything, and wrong settings fail loudly with a message that says what to fix. Every feature is covered by an automated test suite (about 2,500 tests) that also runs on the release build, under memory and undefined-behavior sanitizers, and under seeded fuzzing.
- **Documented in the editor.** Every class, property, method and signal has in-editor help (<kbd>F1</kbd> or Ctrl+click), and the inspector warns about missing setup.
- **Every platform:** Windows, macOS, Linux, Android, iOS and Web, prebuilt.

**Videos:** [Trailer](https://www.youtube.com/watch?v=OJK7201pzsE) · [Features showcase](https://www.youtube.com/watch?v=SeyD1YVIZss) · [Tutorial part 1](https://www.youtube.com/watch?v=L10sHF4BnLs) · [Tutorial part 2](https://www.youtube.com/watch?v=R2olIyF7aPQ)

## See it in action

Every clip and screenshot below is **Arcane Barrage**, a complete bullet-hell game built only with the plugin's nodes and resources: a wizard against four bosses, six spells, graze scoring, bombs that turn bullets into gems and a 16,000-bullet stress test.

| | |
|---|---|
| <img src="showcase/clips/lasers.webp" alt="Charged laser beams sweeping the arena" width="360"/><br>**Lasers:** charged beams that sweep, lock-on and curvy snakes | <img src="showcase/clips/swarm.webp" alt="A homing, flocking swarm" width="360"/><br>**Homing and flocking:** a swarm that hunts but keeps its distance |
| <img src="showcase/clips/galaxies.webp" alt="Counter-rotating galaxies and an orbit cage" width="360"/><br>**Spin, emitters and orbiting:** bullets that fire bullets, cages that circle you | <img src="showcase/clips/stress.webp" alt="Sixteen thousand bullets" width="360"/><br>**16,000 bullets** with the boss fight running, still at a steady 60 FPS |

| | | |
|---|---|---|
| <img src="showcase/screenshots/hive_spirals.jpg" alt="Spiral waves around the Hive Mother" width="250"/> | <img src="showcase/screenshots/laser_sweep.jpg" alt="Laser beams sweeping the arena" width="250"/> | <img src="showcase/screenshots/rose_storm.jpg" alt="A rose-shaped bullet storm" width="250"/> |
| <img src="showcase/screenshots/serpents.jpg" alt="Curvy laser serpents hunting the wizard" width="250"/> | <img src="showcase/screenshots/twin_galaxies.jpg" alt="Two counter-rotating bullet galaxies" width="250"/> | <img src="showcase/screenshots/stress_16k.jpg" alt="The 16,000-bullet stress test" width="250"/> |

**Get the whole game:** the complete Arcane Barrage project (every boss, attack and spell as short, commented code, plus its art, music and sound) is a thank-you for supporters on [Ko-fi](https://ko-fi.com/realnikich) and [Patreon](https://patreon.com/realnikich). Open it to see how a full bullet hell is put together with the plugin.

## Learn from the examples

[`test_project/examples/`](test_project/examples) holds six small scenes, each a few nodes and one short script. Open `test_project/` in Godot, open a scene and press <kbd>F6</kbd>:

| Scene | Shows |
|---|---|
| `01_ring_turret` | a spinning ring pattern set up entirely in the inspector, and reacting to hits |
| `02_homing` | bullets that chase the player, retargeted to the mouse while you hold the button |
| `03_graze_score` | points for near misses with a two-ring graze zone |
| `04_laser` | a charging beam that sweeps the arena |
| `05_split` | slow orbs that burst into rings of small bullets |
| `06_boss_phases` | a boss that changes its attack at half health, and your ship's gun hitting it |

The rest of `test_project/` is the test lab: a scene with a sidebar to try every feature live (bullet counts, collision, pools, physics rates), and the automated test suites. Press <kbd>F2</kbd> in the test lab or in any example to open the **live inspector**: every property of the factory, the spawners, their spawn and laser data and any nested resource, edited while the game runs (a refused value keeps the old one and says so), plus a button for every argument-free method.

## Install

Requires **Godot 4.5 or newer** (tested on 4.7).

1. Download `blastbullets2d.zip` from the [latest release](https://github.com/nikoladevelops/godot-blast-bullets-2d/releases) (also on the [Godot Asset Library](https://godotengine.org/asset-library/asset/2632) and [itch.io](https://realnikich.itch.io/blastbullets2d)).
2. Extract it and copy the `blastbullets2d` folder anywhere into your project (for example `res://addons/blastbullets2d`).
3. Restart the editor. `BulletFactory2D`, `BulletSpawner2D` and `LaserSpawner2D` now appear in the **Add Node** dialog.

There is no plugin to enable: it is a GDExtension and loads by itself.

## Your first boss in 5 minutes (no code)

You need three things:

| What | Job | How many |
|---|---|---|
| `BulletFactory2D` (node) | Owns every bullet: pooling, physics, drawing, signals | One per level |
| `BulletSpawner2D` (node) | Decides **where** and **when** bullets spawn (pattern, fire rate, spin...) | One per gun, turret or boss |
| `BulletVolleyData2D` (resource) | Decides **what** a bullet is (art, speed, hitbox, lifetime) | One per bullet type |

1. **Add a `BulletFactory2D`** to your level scene.
2. **Add a `BulletSpawner2D`** (as a child of your boss, or anywhere). It finds the factory by itself.
3. **Give it bullets:** in the inspector, **Setup → Spawn Data → New BulletVolleyData2D**, then open it and set:
   - **Appearance → Sprite Frames:** a `SpriteFrames` with your bullet art. Draw bullets **facing right**.
   - **Movement Speed → Shared Bullet Speed Data → New BulletSpeedData2D**, and set its **Speed** (try `200`). *Speed defaults to 0: without this the bullets stand still.*
   - **Collision → Collision Shape** (a `CircleShape2D` is the cheapest), plus **Collision Layer** and **Collision Mask** (what the bullets are, and what they hit).
4. **Pick a pattern:** **Patterns → Pattern Source → Spiral** (or any other) and set **Helper Bullets Amount**. The editor draws a dot and an arrow for every bullet that will spawn.
5. **Press play.** The spawner fires once per second (**Shooting → Shoot Interval Sec**).

> **Faster start:** select the spawner and run `apply_pattern_preset(BulletPatterns2D.PATTERN_PRESET_SPIRAL_3ARM)` from a script, or browse the presets in code completion. They set the pattern knobs for you; tune from there.

Nothing appears? Look at the warning icon on the spawner node: it says what is missing. At runtime, `print($BulletSpawner2D.get_setup_warnings())` returns the same list, and the spawner's `volley_skipped(reason)` signal tells you why a shot did not fire.

## Handle hits

Bullets report what they hit through signals. Connect them on the **spawner** that fired the bullets (or once on the **factory**: a hit the spawner does not handle goes to the factory). The bullet is **still alive** inside the handler, so you can read anything about it:

```gdscript
extends Node2D

@onready var gun: BulletSpawner2D = $Boss/BulletSpawner2D

func _ready() -> void:
	gun.area_entered.connect(_on_bullet_hit)

func _on_bullet_hit(target: Object, volley: BulletVolley2D, bullet_index: int) -> void:
	if target.has_method("take_damage"):
		target.take_damage(10)
	var hit_position := volley.get_bullet_transform(bullet_index).origin
	print("hit at ", hit_position)
```

- `area_entered` fires for `Area2D` targets (recommended: give players and enemies a hurtbox `Area2D`). `body_entered` fires for physics bodies, and needs **Collision → Monitorable** on in the bullet data.
- A bullet dies after **Collision → Bullet Max Collision Count** hits (default 1). `0` makes it pierce forever.
- **Different damage per bullet type?** Put your own `Resource` in **Shared Bullets Custom Data** (or one per bullet in **All Bullets Custom Data**) and read it in the handler with `volley.bullet_get_custom_data(bullet_index)`.

## Recipes

Every snippet assumes `spawner` is a `BulletSpawner2D`, `factory` your `BulletFactory2D` and `player` a `Node2D` of yours. Each property is also in the inspector under the group named in the heading.

<details open>
<summary><b>Aim, spin, bursts and warnings</b> (Patterns, Spin, Shooting)</summary>

```gdscript
spawner.pattern_source = BulletSpawner2D.PATTERN_FROM_HELPER_AIMED
spawner.helper_aimed_target = spawner.get_path_to(player)   # aims a cone at the player, with lead
spawner.spin_enabled = true                                # turn the whole pattern while firing
spawner.spin_speed_deg_per_sec = 90.0
spawner.burst_enabled = true                               # 3 quick shots, then a pause
spawner.burst_count = 3
spawner.burst_interval_sec = 0.1
spawner.telegraph_enabled = true                           # volley_telegraphed fires 0.5 s before each shot
spawner.telegraph_sec = 0.5
spawner.max_live_bullets = 400                             # skip shots while 400 of its bullets fly
```
</details>

<details>
<summary><b>Homing bullets</b> (Homing, Orbiting)</summary>

```gdscript
player.add_to_group(&"player")
spawner.homing_enabled = true
spawner.homing_target_source = BulletSpawner2D.HOMING_SOURCE_NODE_GROUP
spawner.homing_node_group = &"player"
spawner.homing_smoothing = 4.0         # 0 snaps, higher turns more gently
spawner.homing_duration_sec = 2.0      # stop steering after 2 s (0 = forever)
spawner.home_live_volleys_to(player)   # re-aim bullets already flying
```
Bullets can also circle their target: turn on `orbiting_enabled`. `print("\n".join(spawner.debug_explain_features()))` explains in plain sentences what homing, orbiting and flocking will do with the current settings.
</details>

<details>
<summary><b>Graze (near misses)</b> (Graze)</summary>

Create a `GrazeZone2D` resource (one to four ring radii around the player), then:

```gdscript
spawner.graze_enabled = true
spawner.graze_zones = [preload("res://graze_zone.tres")]
spawner.graze_node_group = &"player"          # who can be grazed
factory.bullet_grazed.connect(func(target, volley, bullet_index, zone, ring_index):
	score += 10 * (ring_index + 1))
```
Graze always reaches the factory too, so one handler counts every graze in the game, even of enemies that already died. The editor draws the rings around your player.
</details>

<details>
<summary><b>Lasers</b> (LaserSpawner2D)</summary>

Add a `LaserSpawner2D`: it has every pattern, timing, spin and movement setting of `BulletSpawner2D` and fires one laser per pattern slot. Pick a **Laser Data** preset in the inspector, or in code:

```gdscript
var lasers: LaserSpawner2D = $Boss/LaserSpawner2D
var laser := LaserData2D.create_preset(LaserData2D.PRESET_CHARGED_SWEEP)
laser.collision_mask = 2                # what it hurts
laser.wall_mask = 1                     # what stops it
laser.hit_interval_sec = 0.1            # laser_hit repeats while a target stays in the beam
lasers.laser_data = laser
lasers.laser_hit.connect(func(target, laser_volley, beam_index, hit_position):
	if target.has_method("take_damage"):
		target.take_damage(2))
```
Presets: instant beam, charged sweep, wall beam, reflecting beam, bolt, homing curvy laser, lock-on.
</details>

<details>
<summary><b>Coins and slow motion</b> (World controls, pickups)</summary>

```gdscript
var coin := BulletPickupData2D.new()
coin.sprite_frames = preload("res://coin_frames.tres")
coin.value = 1
factory.pickup_collected.connect(func(target, at, volley, bullet_index, value):
	score += value)
factory.convert_all_bullets_to_pickups(coin, player)   # a bomb: every bullet becomes a coin flying to the player

factory.time_scale = 0.3                               # slow motion for every bullet, spawner and effect
spawner.time_scale = 0.5                               # this spawner even slower (0.3 x 0.5)
factory.clear_active_bullets()                         # wipe the screen (player death, phase change)
```
</details>

<details>
<summary><b>Bombs, clears and dodging</b> (World controls)</summary>

```gdscript
# can_hit_mask picks bullets (and lasers) by what they can hit: pass the player's
# collision layer to take only what can hurt the player and spare the player's shots.
const PLAYER_LAYER := 1
factory.clear_bullets_in_circle(player.global_position, 160.0, PLAYER_LAYER)   # a hurt-flash clear
factory.convert_all_bullets_to_pickups(coin, player, PLAYER_LAYER)             # a bomb: bullets become coins
factory.end_lasers_in_rect(get_viewport_rect(), true, PLAYER_LAYER)            # and enemy beams fade out
# Cheap enough every frame: where the dangerous bullets are, and the closest one.
for p in factory.get_bullet_positions_in_circle(player.global_position, 120.0, PLAYER_LAYER):
    pass   # push your dodge AI away from p
var nearest := factory.get_nearest_bullet(player.global_position, 300.0, PLAYER_LAYER)
if not nearest.is_empty():
    print("closest danger at ", nearest["position"])
```
</details>

<details>
<summary><b>Bullets that fire bullets, split or flock</b> (Emitter Bullets, Splitting, Flocking)</summary>

```gdscript
# Every bullet of `spawner` fires the whole pattern of `sparks` while it flies.
sparks.shooting_enabled = false                        # sparks only fires from the bullets
spawner.emitter = BulletEmitterData2D.new()            # interval, delay, shot limit, spin per shot
spawner.emitter_spawner_path = spawner.get_path_to(sparks)   # a LaserSpawner2D works too
spawner.emitter_enabled = true

spawner.split_enabled = true                           # bullets burst into children when they die
spawner.split_data = BulletSplitData2D.create_preset(BulletSplitData2D.PRESET_BURST_RING)

spawner.flock_enabled = true                           # boids: bullets swarm and keep their distance
```
</details>

<details>
<summary><b>Boss phases</b> (pattern lists)</summary>

```gdscript
spawner.pattern_list_finished.connect(_next_phase)
spawner.spawn_pattern_list([
	{"preset": BulletPatterns2D.PATTERN_PRESET_RADIAL_DENSE},
	{"preset": BulletPatterns2D.PATTERN_PRESET_SPIRAL_3ARM, "helper_bullets_amount": 30},
	{"pattern_source": BulletSpawner2D.PATTERN_FROM_HELPER_FAN, "helper_bullets_amount": 12},
], false, 0.5)                                         # one entry every 0.5 s
spawner.shoot_once_with({"helper_bullets_amount": 64})  # one special shot; settings restored after
```
</details>

## Spawning from code

For one-off effects you can skip the spawner. Build the transforms (where and which way each bullet starts) and spawn:

```gdscript
var data: BulletVolleyData2D = preload("res://bullets/spark.tres")
data = data.duplicate()     # never edit a shared resource: every user of it would change
data.transforms = BulletPatterns2D.helper_generate_transforms_ring(24, Transform2D(0.0, global_position))
var volley := factory.spawn_volley(data)
volley.all_bullets_set_speed(300.0, 0, -1)   # steer it afterwards (0, -1 = every bullet): speed, homing, colors, teleport...
```
Every pattern of the spawner is a static `BulletPatterns2D.helper_generate_transforms_*` function, and `BulletVolley2D` has runtime control for every feature (see its in-editor docs). Factory-spawned bullets report their hits on the factory's signals.

## Performance

- **Fewer, bigger volleys beat many tiny ones.** One volley is one draw call and one physics area; a pattern of 200 bullets is one volley.
- **Keep volley sizes steady.** The pool reuses volleys with the exact same bullet count and shape. Pre-warm a size at load time: `factory.populate_bullets_pool(factory.debug_expected_pool_key(data), data, 5)`.
- **Use `Area2D` hurtboxes** for players and enemies. Physics bodies need `monitorable` on and cost more.
- **Smooth motion on 120 Hz+ screens:** turn on *Project Settings → Physics → Common → Physics Interpolation*; the factory follows it (`physics_interpolation`).
- **Measure:** the factory registers live monitors (*Debugger → Monitors → BlastBullets2D*) and `factory.get_frame_stats()` returns the same numbers. Tick `is_debugger_enabled` on the factory to see every hitbox.
- **The 20,000 limit:** one volley holds at most 20,000 bullets by default (project setting `blastbullets2d/patterns/max_bullets_per_pattern`). It exists because one volley is one physics area, and Godot's broadphase stalls on tens of thousands of tightly packed shapes. For bigger barrages fire several smaller volleys.

## Limits

Know these before you build a game around the plugin:

- **No Y-sorting.** A volley draws as one canvas item, so bullets cannot sort between your Y-sorted characters. Use `z_index` (Rendering and Material) to put bullets above or below them. Top-down, side-view and shmup games are the sweet spot.
- **Collision shapes:** `CircleShape2D`, `RectangleShape2D` and `CapsuleShape2D` (one shape per volley; each bullet's scale applies). Other shapes fall back to a circle with an error.
- **Bullets behave like `Area2D`s:** they detect and bounce, but they are not rigid bodies (no impulses), and bullets do not collide with each other.
- **Very fast bullets** move in steps of one physics tick. A bullet that travels further than its own size in one tick can step over a thin target: make thin targets thicker, or the hitbox larger.
- **No save/load** of bullets in flight.

## Rules worth knowing

- **Inside signal handlers**, structural factory calls (`reset()`, `free_*()`, `populate_*()`) are refused: use their `*_deferred` versions (for example `factory.reset_deferred()`). Editing bullets, spawning and clearing work anywhere.
- **Free volleys with `queue_free()`**, never `free()`; better, let the pool handle them. A volley returned to the pool is a stale handle: do not keep using a reference after its bullets are gone.
- **Duplicate shared resources before editing them from code** (`data.duplicate()`), or every spawner using them changes.
- **Your scripts may use `_ready()`, `_process()` and `_physics_process()`** on the factory and spawners: the plugin does its work through notifications, so both run.
- **Collision signals arrive during the physics step** and the bullet is alive in the handler. Whether it dies is decided afterwards: lower its hit count in the handler (`volley.set_bullet_collision_count(bullet_index, 0)`) and it survives.
- **When an enemy dies,** its spawner's `orphaned_volleys` setting decides what its bullets in flight do (keep flying and hand their hits to the factory, by default).

## Reference

- **In-editor docs:** every class (<kbd>F1</kbd> → `BulletSpawner2D`, `BulletFactory2D`, `BulletVolley2D`, `LaserSpawner2D`...).
- **[QUICKSTART.md](QUICKSTART.md):** one page of common bullet-hell recipes.
- **Examples:** [`test_project/examples/`](test_project/examples), six small scenes (see [Learn from the examples](#learn-from-the-examples)).
- **Arcane Barrage:** the full game project, for [Ko-fi](https://ko-fi.com/realnikich) and [Patreon](https://patreon.com/realnikich) supporters.

<details>
<summary><b>The data model: per-bullet, shared and default values</b></summary>

Most data properties come in two flavors, and every feature resolves them with one rule:

> **per-bullet (valid) > shared (valid) > default.** Entry `i` of a per-bullet array drives bullet `i` only.

- **Per-bullet** arrays (`all_bullet_speed_data`, `all_bullet_rotation_data`, `all_bullet_curves_data`, `all_bullets_custom_data`, ...): one entry per bullet. A short array covers only its own indices; an invalid entry (null, wrong type, not finite) falls back for that bullet only.
- **Shared** values (`shared_bullet_speed_data`, `shared_bullet_rotation_data`, `shared_bullets_custom_data`, ...): one value for every bullet the per-bullet array does not cover. **With a spawner, prefer the shared fields**: a pattern's bullet count can change.
- **Tiling** (`tile_*` checkboxes, off by default): a short per-bullet array repeats (`bullet i` reads entry `i % size`), for example two speed entries for fast/slow/fast/slow.
- **Negative speed** flies backwards; NaN and infinite values are always refused with an error.
- **Inspect it live:** `volley.debug_get_curves_info(i)`, `debug_get_pattern_info(i)`, `debug_get_orbiting_info(i)` and `debug_get_wobble_info(i)` say which value a bullet uses and where it came from.
</details>

<details>
<summary><b>Migrating from version 3.x</b></summary>

The bullet classes were unified into one volley class. Rename these in your scripts and saved scenes (`.tscn` / `.tres` files store class and signal names as text):

| Old | New |
|---|---|
| `DirectionalBullets2D`, `MultiMeshBullets2D` | `BulletVolley2D` |
| `DirectionalBulletsData2D`, `MultiMeshBulletsData2D` | `BulletVolleyData2D` (scene files: `[sub_resource type="BulletVolleyData2D" ...]`) |
| `BlockBullets2D`, `BlockBulletsData2D`, `spawn_block_bullets()` | removed: use `BulletVolley2D` with one shared speed (`shared_bullet_speed_data`) |
| `MultiMeshPoolKey2D` | `VolleyPoolKey2D` |
| `MultiMeshBulletsDebugger2D` | `BulletVolleyDebugger2D` |
| `spawn_directional_bullets()`, `spawn_controllable_directional_bullets()` | `spawn_volley()` (always returns the volley) |
| factory signals `directional_area_entered` / `directional_body_entered` / `directional_life_time_over` / `directional_bounce_area_entered` / `directional_bounce_body_entered` (and `block_*`) | `area_entered` / `body_entered` / `life_time_over` / `bounce_area_entered` / `bounce_body_entered` (same names as on `BulletSpawner2D`) |
| signal argument `directional_bullets_instance` | `volley` |
| `BulletFactory2D.BulletType`, the `bullet_type` argument of `free_bullets_pool()` / `free_bullets_pool_deferred()` / `debug_get_total_bullets_amount()` / `debug_get_active_volleys_amount()` / `debug_get_bullets_pool_amount()` / `debug_get_bullets_pool_info()` | removed (one volley type) |
| `directional_bullets_debugger_color`, `block_bullets_debugger_color` | `debugger_color` |
| `reactivate_multimesh_instance()` | `reactivate_volley()` |
| volley `enable_multimesh()` | `enable_volley()` |
| volley `multimesh_attach_time_based_function()` / `multimesh_detach_time_based_function()` / `multimesh_detach_all_time_based_functions()` | `attach_time_based_function()` / `detach_time_based_function()` / `detach_all_time_based_functions()` |
| volley `is_multimesh_auto_pooling_enabled` | `is_auto_pooling_enabled` |
| debug dictionary keys `directional_total` / `directional_pooled` / `directional_hits` / `directional_misses` | `volleys_total` / `volleys_pooled` / `hits` / `misses` |
| factory child nodes `DirectionalBulletsContainer` / `DirectionalBulletsDebugger` | `BulletVolleysContainer` / `BulletVolleysDebugger` |
| `bullet_homing_target_reached` argument `multimesh_instance` | `volley` |
| a spawner needed `bullet_factory_path` | optional: an empty path uses the factory in the spawner's scene |
</details>

## Building from source and running the tests

You only need this to change the C++ code.

- **Easiest:** fork the repository and run the **Build GDExtension Cross Platform Plugin** workflow in the GitHub Actions tab: it builds every platform and packages the zip ([godot-plus-plus template](https://github.com/nikoladevelops/godot-plus-plus)).
- **Locally:** `python3 setup.py` opens a menu for the Godot path, builds, icons and docs. For scripts and CI: `GODOTPP_NONINTERACTIVE=1 python3 tools/compile_debug_build.py`.
- **Tests:** `python3 tools/run_tests.py` runs every suite headless (about 30 s); `python3 tools/run_release_tests.py` runs them against the release build. Every push runs both, plus format, portability and editor checks, in the [Tests workflow](.github/workflows/tests.yml). [AGENTS.md](AGENTS.md) describes how the code base is organized and how changes are proven.

## Support

BlastBullets2D is free forever. If it helps your game:

- Leave a **star** on the repository.
- Support development on [Ko-fi](https://ko-fi.com/realnikich) or [Patreon](https://patreon.com/realnikich).
- Tutorials on [YouTube](https://www.youtube.com/@realnikich) · [X (Twitter)](https://x.com/realNikich) · [Bluesky](https://bsky.app/profile/realnikich.bsky.social)
