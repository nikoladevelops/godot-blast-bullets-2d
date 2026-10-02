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
- `volley/test_volley_dedup_table.gd` — dedup TABLE internals via debug bindings: pairs survive table grows (grow rehashes, never drops), 2000-pair scale with zero phantoms, 64-bit key resists birthday collisions (300k probes), cold tables shrink while hot tables keep capacity.
- `factory/test_factory_spawn_data_guards.gd` — spawn-data finiteness: `texture_rotation_radians` / `collision_shape_offset` / `texture_size` / `self_modulate` / `block_rotation_radians` reject NaN/Inf and keep the old value, while a valid volley with a non-zero texture rotation stays finite.
- `factory/test_factory_clear_triggers.gd` — factory clear contract: `clear_bullet` fires On Clear, `clear_active_bullets()` clears every live bullet with visuals and returns the count (volleys park pooled), `free_active_bullets()`/`reset()` stay silent teardown, key-scoped clear touches only its bucket, deferred twins run.
- `volley/test_volley_trigger_matrix.gd` — every effect trigger against its path: On Spawn on factory spawn / spawner shot / pooled reuse, On Hit on counted hits, On Destroy on kill only (never timeout), On Lifetime Over on expiry, On Clear on manual clear, On Bounce on ricochet, spawner-owned hits route to spawner signals only.
- `volley/test_volley_collision_bodies.gd` — collision body matrix: Static/Character/Rigid/Animatable bodies count and emit `body_entered`, Area2D routes to `area_entered` (never body), spawner-owned volleys route to spawner signals (factory silent), block volleys route to `block_body_entered`.
- `pooling/test_pool_volley_reset.gd` — pooled reuse starts a new life: pool-hit reuse, collision counts zeroed, dedup window fresh (same wall hits again), old custom timers never fire, no attachments leak, per-bullet ballistics reseeded.
- `spawner/test_spawner_muzzle_offset.gd` — `spawn_position_offset` applies to every volley (plain and homing, exactly once); zero is a no-op.
- `volley/test_volley_disable_reentrancy.gd` — nested disable from `on_bullet_disable` is rejected: counter exact, single pool entry, clean reuse.
- `spawner/test_spawner_tracker_cap.gd` — 260 homing shots cap at 256 tracked volleys, newest still retargets, clear re-arms.
- `volley/test_volley_trail_refcount.gd` — trail shard hides exactly when its last bullet leaves (shared-shard ordering, wake re-tracks).
- `spawner/test_spawner_preview_rotation.gd` — preview parity under rotation: dots sit on the rotated track, rotated corners/rings present in global space, fan cone centers on marker rotation + direction.
- `volley/test_volley_rotation_presence_clear.gd` — clearing rotation data clears presence bits: gaps refill from shared, authored zeros still win.
- `spawner/test_spawner_preset_burst.gd` — spin presets advance from idle; failed burst shots retry (never silently consumed), permanent failure aborts with burst_finished.
- `volley/test_volley_singular_transforms.gd` — zero/singular scales rejected at spawn and setters via the central invertibility check.
- `volley/test_volley_curves_baseline.gd` — pins current curve semantics: clear freezes last sample, Additive direction accumulates (characterized, not changed).
- `spawner/test_spawner_flower_parity.gd` — rings never bridge petal arcs/rows (INF separators captured in shape_loop), dots on track, inward/outward layers.
- `factory/test_factory_singular_marker.gd` — inverting generators reject singular markers loudly; non-inverting ones degrade to finite zero-size.
- `spawner/test_spawner_path2d_singular_base.gd` — AT_PATH2D linker falls back to raw points on a singular base.
- `spawner/test_spawner_max_volleys_edge.gd` — cap trip emits finished once; lowering onto the count emits stopped only.
- `volley/test_volley_bounce_scaled_shape.gd` — degenerate shapes bounce finite via radial fallback (never double-counted); slivers separate; sane shapes precise.
- `spawner/test_spawner_spin_matrix.gd` — spin is one shared matrix: spun ring equals rotated unspun ring, mirrored generators and sheared customs survive spin exactly.
- `spawner/test_spawner_node_cache.gd` — validated manual assignment wins over stale paths; out-of-tree nodes stay assigned.
- `spawner/test_spawner_fire_arc_generator.gd` — fire-arc gate reads the muzzle frame, not the spawner node.
- `volley/test_volley_rotation_wake_presence.gd` — same-owner wakes keep presence decisions; new lives reset them.
- `spawner/test_spawner_retarget_stagger.gd` — unrelated knob changes never re-arm the retarget countdown.
- `volley/test_volley_gravity_presence.gd` — shared gravity fills gaps only; seeded/per-bullet values survive.
- `volley/test_volley_orbit_rearm.gd` — linear re-arm updates every parameter; Random never re-rolls.
- `spawner/test_spawner_outline_visibility.gd` — distribution knob hidden for smooth loops, shown for corner shapes.
- `factory/test_factory_spawn_in_handler.gd` — same-key spawns from a killing-blow handler succeed (never the draining volley; pooled instances reused mid-sweep).
- `volley/test_volley_lifetime_signal_reuse.gd` — expiry signals survive same-frame pool reuse (physics-tick and call_deferred spawns); handlers see attachments; one disable per expiry, no enable churn.
- `volley/test_audit_regressions.gd` — audit pins: texture-rotation round-trip with offset, timer period carry, block spin after bullet 0 dies, pool-key enum ids, mirrored transforms_scale, scaled-slope bounce normal, free_volley_deferred type gate, bounded distance-phased wobble.
- `spawner/test_spawner_lifecycle_signals.gd` — transitions report exactly once: pre-tree setters silent, load emits one started, toggles/caps report in-tree only.
- `spawner/test_spawner_terrain_crest.gd` — crest normals tilt with the slope (up-right ascending, up-left descending), all finite.
- `spawner/test_spawner_preview_state.gd` — dead track drops rings (dots survive); seed setters rebuild the gizmo.
- `spawner/test_spawner_path2d_gating.gd` — gated resampling: node motion reflects in frames, curve edits within the staleness bound.
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
