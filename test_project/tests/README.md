# BlastBullets2D headless stability suites

Every file is a `SceneTree` script: `godot --headless --path test_project --script <path>`.
Exit code 0 = all checks pass. Reaching the end proves survival of every error
path; the PASS lines prove the documented behavior. Any FAIL = behavior drift
or a real bug — investigate before shipping.

## Layout (mirrors `src/`)

- `factory/test_factory_lifecycle.gd` — init recovery, spawn validation, deferred wrappers, flag reset, wake alias, NaN atomicity, teardown.
- `factory/test_factory_pooling.gd` — duplicate cache, retarget stagger, debugger budget, preview rings, pool hit/miss.
- `volley/test_directional_core.gd` — speed/direction/transform/velocity/rotation get/set + rejects.
- `volley/test_volley_lifetime.gd` — finite/infinite/invalid lifetimes, expiry pooling, deferred signal, collision-max interplay.
- `volley/test_volley_collision.gd` — REAL physics vs StaticBody2D + Area2D wall: slim payloads, max counts, epoch guard.
- `volley/test_volley_homing.gd` — deques, target types, steering convergence, freed targets, delay/duration/lose, reached signal.
- `volley/test_volley_orbiting.gd` — arming without targets, setters (clamp vs reject), linear shells, rigid follow, disable.
- `volley/test_volley_curves_wobble.gd` — shared/per-bullet curves, ownership guards, Path2D/Curve2D patterns, gravity acceleration, shared speed/rotation, wobble.
- `volley/test_volley_runtime_mutation.gd` — custom data separation, layers, shape runtime, 64-timer cap, enable/disable, animation guards.
- `volley/test_volley_gravity_feature.gd` — opt-in gravity: default-off, shared/per-bullet seeding, delay/duration windows, strength curves, rejects, reuse neutrality, homing mix, spawner seeding.
- `volley/test_volley_math_edges.gd` — quantitative tick identities: drag decay band, rotation clamp, wobble boundedness, smoothing clamps.
- `volley/test_block_volleys.gd` — rigid volleys: spawn, teleport, pooling, expiry.
- `spawner/test_spawner_patterns.gd` — all 30 sources, cap, order ops, spin/scales, skip carve, presets.
- `spawner/test_spawner_homing_orbit.gd` — 6 target sources, cache, fire-arc gate, retarget, fuse, stagger.
- `spawner/test_spawner_signals_sequencing.gd` — handler contracts, burst/telegraph/pattern-list, cap, adopt/clear/override.
- `integration/test_interpolation_integration.gd` — interpolation agreement/toggle, pause, churn, two factories.
- `common/blast_test_helpers.gd` — shared `DirectionalBulletsData2D`/`BlockBulletsData2D` builders (identical speeds/layers/sizes so failures mean regressions).

## Conventions

- Park on idle (`await process_frame` ×2) before structural ops; `await
  physics_frame` resumes *inside* physics and structural calls reject there.
- Timer attach/detach counts only read fresh on idle frames (they defer in physics).
- `factory.debug_assert_no_dangling()` after every destructive section.
- Legacy root-level suites (`test_edge_fuzz.gd`, …) still run unchanged.

## Pooling (`pooling/`)

The no-leak proof for object reuse. One shared factory stressed by N spawners
plus direct `spawn_controllable_*`, asserting owner attribution, per-bucket
isolation, full state reset (homing/orbit/curves/wobble/pattern/custom-data/
timers/rotation/gravity/fall-speed/flags/owner/generation), key-validity
(hit/miss, never silent reuse), attachment lifecycle via the `AttachmentProbe2D`
fixture (`scenes/attachment_probe.gd`, packed in code so suites stay
self-contained), and cross-owner handover (adopt, foreign-wake warn + reseed,
deferred free inside collision handlers).
