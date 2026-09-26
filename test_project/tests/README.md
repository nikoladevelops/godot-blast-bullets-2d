# BlastBullets2D headless stability suites

Every file is a `SceneTree` script: `godot --headless --path test_project --script <path>`.
Exit code 0 = all checks pass. Reaching the end proves survival of every error
path; the PASS lines prove the documented behavior. Any FAIL = behavior drift
or a real bug — investigate before shipping.

## Layout (mirrors `src/`)

- `factory/test_factory_lifecycle.gd` — init recovery, spawn validation, deferred wrappers, flag reset, wake alias, NaN atomicity, teardown.
- `factory/test_factory_pooling.gd` — duplicate cache, retarget stagger, debugger budget, preview rings, pool hit/miss.
- `volley/test_directional_core.gd` — speed/direction/transform/velocity/rotation get/set + rejects.
- `volley/test_volley_lifetime.gd` — finite/infinite/invalid lifetimes, live infinite toggle + max<=0 guard refusal, expiry pooling, collision-count reads, deferred signal, collision-max interplay.
- `volley/test_volley_collision.gd` — REAL physics vs StaticBody2D + Area2D wall: slim payloads, max counts, epoch guard.
- `volley/test_volley_homing.gd` — deques, target types, steering convergence, freed targets, delay/duration/lose, reached signal.
- `volley/test_volley_orbiting.gd` — arming without targets, setters (clamp vs reject), linear shells, rigid follow, disable.
- `volley/test_volley_curves_wobble.gd` — shared/per-bullet curves, curves tile wrap + strict twin, rotation_speed_curve spin, per-bullet gravity_strength scaling, wobble angular/cosine/phase-fan/distance-phased/damping/windows, negative-speed-under-gravity, ownership guards, Path2D/Curve2D patterns, shared speed/rotation.
- `volley/test_volley_runtime_mutation.gd` — custom data separation, layers, shape runtime, 64-timer cap, enable/disable, animation guards.
- `volley/test_volley_gravity_feature.gd` — opt-in gravity: default-off, shared/per-bullet seeding, delay/duration windows, strength curves, rejects, reuse neutrality, homing mix, spawner seeding.
- `volley/test_volley_precedence_sizes.gd` — strict rule: entry i drives bullet i only; per-bullet (valid) > shared (valid) > default; short arrays cover own indices, tile_* boxes opt into wrap; invalid-entry gap fallback; custom-data separation; negative speed/curve reverse flight.
- `volley/test_volley_precedence_gaps.gd` — documented limits + live order: shared-only rotation fans to all slots and spins; short-tail/partial-zero/all-zero/NaN-shared semantics; fill-once set order both ways; tiled invalid entries; shared curves outrank ballistics; per-channel split + curves-clear symmetry; pattern flag snapshot; no-gap writes; homing drain handover; smoothing latch/clear/pool-neutrality; shared-park vs per-clear finish.
- `volley/test_volley_debug_api.gd` — new introspection: orbit center/angle ranges + debug_get_orbiting_info (per-wins center proof), homing amount range, debug_get_curves_info/pattern_info shapes, full wobble seed, curves-clear binds, attachment-index doc proof; OOB/inverted everywhere.
- `spawner/test_spawner_precedence_mixes.gd` — 3+ spawners, mixed flavors, one factory: per-wins/shared-tail/tiled-negative-custom, rotation/wobble isolation, relative homing convergence, gravity isolation + pool-reuse neutrality, spawn-data pattern tile + strict twin, spawner smoothing fan (start+step*i), wobble random-gen guards, speed range fan.
- `volley/test_volley_mixed_edges.gd` — mixed stacks + edges: wobble texture-follow, live wobble API, wobble+gravity+drag+homing+curves compose, coincident aim / zero-radius orbit / zero-heading holds, pool-reuse neutrality, pattern finish, monitorable round-trip.
- `volley/test_volley_api_coverage.gd` — every bound getter/setter/range/debug path incl. OOB, inverted ranges, velocity-under-curve block, transforms/texture/curves/wobble ranges, homing target getter + type, custom null clears, orbit lock/center/angle, curves/patterns live API, debug snapshot.
- `volley/test_volley_crash_proof.gd` — hostile input (NaN/Inf/null/wrong-type/OOB/empty/degenerate), freed targets, disable/teleport storms, pool churn, queue/timer floods, 0/1/64-bullet extremes. Never crashes, never NaN-poisons, no leaks.
- `volley/test_volley_mix_match.gd` — feature crosses: homing snakes, wobble rings, gravity+drag+curves, spin+patterns+homing, sovereign per-bullet volleys, spawner mixed shots, expiry inside mixes.
- `volley/test_volley_rotation_helpers.gd` — per-bullet rotation API (get/set/range/clear) crossed with shared fallback, negative spin + -max clamp, stop flag both ways, rotate_only both ways, adjust steering + skip, live order both ways, remove persistence, shared getters, inverted/OOB ranges, tick spin, pool reuse, hostile input.
- `factory/test_factory_helper_edges.gd` — helper hostile input: amount caps (10000/call), grid-product/gap/points/vertices caps, NaN/Inf rejects, huge-finite clamp-to-box, scale band, skip/layer guards.
- `spawner/test_spawner_pool_stress.gd` — 3 spawners on 1 factory: shared fire, cross-shape reuse isolation, reset/free under live volleys, adopt chains, spawner-freed orphans, retarget storms, pool-hit accounting.
- `volley/test_volley_math_edges.gd` — quantitative tick identities: drag decay band, rotation clamp, wobble boundedness, smoothing clamps.
- `volley/test_block_volleys.gd` — rigid volleys: spawn, teleport, pooling, expiry, block-data construction (no curves/pattern surface), null-pattern clear, live accounting.
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

## Strict indexing + migration

- Per-bullet entry `i` drives bullet `i` only. Fallback per bullet:
  per-bullet (valid) > shared (valid) > feature default. Short arrays cover
  their own indices; the rest fall back. Each `tile_*` checkbox (off by
  default) restores wrap-around for its array only.
- Coming from 1-entry-broadcast or short-array tiling: provide one entry per
  bullet, set the `shared_*` value, or check the `tile_*` box.

## Pooling (`pooling/`)

The no-leak proof for object reuse. One shared factory stressed by N spawners
plus direct `spawn_controllable_*`, asserting owner attribution, per-bucket
isolation, full state reset (homing/orbit/curves/wobble/pattern/custom-data/
timers/rotation/gravity/fall-speed/flags/owner/generation), key-validity
(hit/miss, never silent reuse), attachment lifecycle via the `AttachmentProbe2D`
fixture (`scenes/attachment_probe.gd`, packed in code so suites stay
self-contained), and cross-owner handover (adopt, foreign-wake warn + reseed,
deferred free inside collision handlers).
