# BlastBullets2D headless stability suites

Every file is a `SceneTree` script: `godot --headless --path test_project --script <path>`.
Exit code 0 = all checks pass. Reaching the end proves survival of every error
path; the PASS lines prove the documented behavior. Any FAIL = behavior drift
or a real bug — investigate before shipping.

## Layout (mirrors `src/`)

- `volley/test_volley_presence_bits.gd` — per-bullet seed PRESENCE: a valid all-zero speed/rotation entry is intent and survives a shared fallback (no travel, no spin), null/absent entries still fall back, direct per-bullet writes claim presence, fill-once, rejected-NaN degrades to a valid zero, pool-reuse neutrality.
- `volley/test_volley_timer_identity.gd` — per-timer ids: a targeted detach cancels a queued one-shot fire (the volley-wide generation could not), repeat timers stop cleanly, a sibling timer survives, re-attach works, pool-reuse neutrality, 64-timer cap.
- `volley/test_volley_shared_homing_epoch.gd` — shared deque front epoch: a manual push/clear/pop between the auto-pop queue and its flush cancels the stale pop, a back-push on a non-empty deque does not, the latch is released after a cancellation.
- `volley/test_volley_collision_dedup.gd` — object-level collision dedup: a 3-shape target counts once per overlap, the legacy per-shape mode is reachable via `collision_dedup_by_object`, both bullets against one target count independently, the dedup window resets between drains.
- `factory/test_factory_spawn_data_guards.gd` — spawn-data finiteness: `texture_rotation_radians` / `collision_shape_offset` / `texture_size` / `self_modulate` / `block_rotation_radians` reject NaN/Inf and keep the old value, while a valid volley with a non-zero texture rotation stays finite.
- `spawner/test_spawner_pattern_source_lock.gd` — every PatternSource keeps its serialized integer (the enum is fully explicit now, so a renumber would repoint saved scenes), the inspector hint pins all 33, the setter accepts 0..32 and refuses the rest.
- `spawner/test_spawner_burst_mirror.gd` — burst alternate-mirror: the flag alternates every other shot, a mirrored SPIRAL / MULTISPIRAL actually reverses its winding (this was the bug: only the emitter spin flipped), non-spiral patterns are unaffected, DISTRIBUTE with the default `homing_max_targets=1` runs (and warns) while 2+ spreads.
- `spawner/test_spawner_property_visibility.gd` — inspector-visibility coverage: no `helper_*` property is invisible in all 33 pattern modes (the safety net for the untyped `begins_with` chain), aimed-target sharing between AIMED and CORRIDOR, homing knobs gated on `homing_enabled`.
- `factory/test_factory_lifecycle.gd` — init recovery, spawn validation, deferred wrappers, flag reset, wake alias, NaN atomicity, teardown.
- `factory/test_factory_pooling.gd` — duplicate cache, retarget stagger, debugger budget, preview rings, pool hit/miss.
- `volley/test_directional_core.gd` — speed/direction/transform/velocity/rotation get/set + rejects.
- `volley/test_volley_lifetime.gd` — finite/infinite/invalid lifetimes, live infinite toggle + max<=0 guard refusal, expiry pooling, collision-count reads, deferred signal, collision-max interplay.
- `volley/test_volley_collision.gd` — REAL physics vs StaticBody2D + Area2D wall: slim payloads, max counts, epoch guard.
- `volley/test_volley_bounce.gd` — REAL physics ricochet: defaults-off, free/consumed bounces, uncapped strength scaling with max_speed clamp, wall ping-pong with max_count, mask precedence, spawner ownership, radial vs precise normals (incl. capsule branch), smooth visual pursuit with render-continuity proof, teleport-into-wall, attachment nudge tracking, cooldown-vs-consumed semantics, cooldown expiry, retarget preservation, bulk counts, inspector group coherence, runtime toggles, gravity flag refresh, homing/wobble/gravity mixes, rejects, pool-reuse neutrality.
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

## Running

```
python3 tools/run_tests.py                  # every suite, summary table
python3 tools/run_tests.py --suite volley   # substring filter
python3 tools/run_tests.py --changed-only   # suites plausibly affected
python3 tools/run_tests.py --list           # discover without running
```

A suite is only green when it exits 0 **and** prints its own completion marker
(`ALL ... TESTS PASSED` / `GROUP_DONE`). A suite that exits 0 without printing
one is reported as CRASH, not PASS — that is the check which catches a stale
`.so` (an unloaded extension makes `debug_get_*` assertions pass against
defaults). `test_edge_fuzz.gd` is multi-group: the runner sets `CASE` and runs
each group in its own process.

## Strict indexing + migration

- Per-bullet entry `i` drives bullet `i` only. Fallback per bullet:
  per-bullet (valid) > shared (valid) > feature default. Short arrays cover
  their own indices; the rest fall back. Each `tile_*` checkbox (off by
  default) restores wrap-around for its array only.
- Coming from 1-entry-broadcast or short-array tiling: provide one entry per
  bullet, set the `shared_*` value, or check the `tile_*` box.
- **Presence, not zeros.** A per-bullet entry that exists and is all-zero is
  INTENT ("this bullet must not move / spin"), not absence, so the shared
  fallback leaves it alone. Only a slot with no entry, a null entry, or an
  entry the resource setter rejected counts as a gap that shared may fill.
  This is order-independent: writing the explicit zero before *or* after
  `set_shared_bullet_*` produces the same result. (Behavior change — it used
  to depend purely on call order, so the same intent behaved differently
  depending on sequencing.)

## Pooling (`pooling/`)

The no-leak proof for object reuse. One shared factory stressed by N spawners
plus direct `spawn_controllable_*`, asserting owner attribution, per-bucket
isolation, full state reset (homing/orbit/curves/wobble/pattern/custom-data/
timers/rotation/gravity/fall-speed/flags/owner/generation), key-validity
(hit/miss, never silent reuse), attachment lifecycle via the `AttachmentProbe2D`
fixture (`scenes/attachment_probe.gd`, packed in code so suites stay
self-contained), and cross-owner handover (adopt, foreign-wake warn + reseed,
deferred free inside collision handlers).
