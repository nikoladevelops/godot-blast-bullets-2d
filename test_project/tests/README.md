# BlastBullets2D headless stability suites (GUT)

Every file under `tests/` is a GUT suite extending `BlastTest`
(`tests/common/blast_test.gd`): `python3 tools/run_tests.py` runs each file in
its own headless Godot process. GUT runs in strict mode — any `push_error` /
engine error the test did not declare fails it — and `BlastTest.after_each`
asserts a dangling-free factory plus zero new orphans after every test.

## Layout (mirrors `src/`)

- `volley/test_volley_presence_bits.gd` — per-bullet seed PRESENCE: a valid all-zero speed/rotation entry is intent and survives a shared fallback (no travel, no spin), null/absent entries still fall back, direct per-bullet writes claim presence, fill-once, rejected-NaN degrades to a valid zero, pool-reuse neutrality.
- `volley/test_volley_timer_identity.gd` — per-timer ids: a targeted detach cancels a queued one-shot fire (the volley-wide generation could not), repeat timers stop cleanly, a sibling timer survives, re-attach works, pool-reuse neutrality, 64-timer cap.
- `volley/test_volley_shared_homing_epoch.gd` — shared deque front epoch: a manual push/clear/pop between the auto-pop queue and its flush cancels the stale pop, a back-push on a non-empty deque does not, the latch is released after a cancellation.
- `volley/test_volley_collision_dedup.gd` — object-level collision dedup: a 3-shape target counts once per overlap, the legacy per-shape mode is reachable via `collision_dedup_by_object`, both bullets against one target count independently, the dedup window resets between drains.
- `volley/test_volley_dedup_table.gd` — dedup TABLE internals via debug bindings: pairs survive table grows (grow rehashes, never drops), 2000-pair scale with zero phantoms, 64-bit key resists birthday collisions (300k probes), cold tables shrink while hot tables keep capacity.
- `factory/test_factory_api_surface.gd` — public surface lock after the volley refactor: BulletVolley2D/BulletVolleyData2D/VolleyPoolKey2D/BulletVolleyDebugger2D registered and every removed class absent; one `spawn_volley` entry (returns the volley or null with the pinned error); no BulletType enum or bullet-type params; factory and spawner declare the same volley signals with `volley` payloads; exact stats keys; one `BulletVolleysContainer` + one `BulletVolleysDebugger`; `debugger_color` round-trips before and after ready.
- `factory/test_factory_spawn_data_guards.gd` — spawn-data finiteness: `texture_rotation_radians` / `collision_shape_offset` / `texture_size` / `self_modulate` reject NaN/Inf and keep the old value, while a valid volley with a non-zero texture rotation stays finite.
- `factory/test_factory_clear_triggers.gd` — factory clear contract: `clear_bullet` fires On Clear, `clear_active_bullets()` clears every live bullet with visuals and returns the count (volleys park pooled), `free_active_bullets()`/`reset()` stay silent teardown, key-scoped clear touches only its bucket, deferred twins run.
- `volley/test_volley_trigger_matrix.gd` — every effect trigger against its path: On Spawn on factory spawn / spawner shot / pooled reuse, On Hit on counted hits, On Destroy on kill only (never timeout), On Lifetime Over on expiry, On Clear on manual clear, On Bounce on ricochet, spawner-owned hits route to spawner signals only.
- `volley/test_volley_collision_bodies.gd` — collision body matrix: Static/Character/Rigid/Animatable bodies count and emit `body_entered`, Area2D routes to `area_entered` (never body), spawner-owned volleys route to spawner signals (factory silent).
- `pooling/test_pool_volley_reset.gd` — pooled reuse starts a new life: pool-hit reuse, collision counts zeroed, dedup window fresh (same wall hits again), old custom timers never fire, no attachments leak, per-bullet ballistics reseeded.
- `spawner/test_spawner_muzzle_offset.gd` — `spawn_position_offset` applies to every volley (plain and homing, exactly once); zero is a no-op.
- `volley/test_volley_disable_reentrancy.gd` — nested disable from `on_bullet_disable` is rejected: counter exact, single pool entry, clean reuse.
- `spawner/test_spawner_tracker_cap.gd` — 260 homing shots cap at 256 tracked volleys, newest still retargets, clear re-arms.
- `volley/test_volley_trail_refcount.gd` — trail shard hides exactly when its last bullet leaves (shared-shard ordering, wake re-tracks).
- `spawner/test_spawner_preview_rotation.gd` — preview parity under rotation: dots sit on the rotated track, rotated corners/rings present in global space, fan cone centers on marker rotation + direction.
- `volley/test_volley_rotation_presence_clear.gd` — clearing rotation data clears presence bits: gaps refill from shared, authored zeros still win.
- `spawner/test_spawner_preset_burst.gd` — spin presets advance from idle; failed burst shots retry (never silently consumed), permanent failure aborts with burst_finished.
- `volley/test_volley_singular_transforms.gd` — zero/singular scales rejected at spawn and setters via the central invertibility check.
- `volley/test_volley_data_inspector.gd` — generic inspector walk of BulletVolleyData2D and a live BulletVolley2D: every own property sits under a group, names and group titles are unique, the dedup knob lives in Collision, `calculate_bitmask` skips out-of-range layers loudly.
- `volley/test_volley_pool_reuse_all_features.gd` — a volley that lived with every feature on (homing, orbit, bounce, wobble, gravity, curves, patterns, trails, timers, attachments, custom data, tint, spin), expired into the pool and was reused for plain data matches a cold volley field by field, pose for pose and in flight; the old timer never fires.
- `volley/test_volley_single_bullet.gd` — a one-bullet volley behaves like bullet 0 of any volley: per-bullet curves beat shared curves at assignment, speed/rotation/direction/velocity accessors round-trip slot 0, index 1 is rejected (never aliased to 0), flight speed exact.
- `volley/test_volley_curves_baseline.gd` — pins current curve semantics: clear freezes last sample, Additive direction accumulates (characterized, not changed).
- `spawner/test_spawner_flower_parity.gd` — rings never bridge petal arcs/rows (INF separators captured in shape_loop), dots on track, inward/outward layers.
- `factory/test_factory_singular_marker.gd` — inverting generators reject singular markers loudly; non-inverting ones degrade to finite zero-size.
- `spawner/test_spawner_path2d_singular_base.gd` — AT_PATH2D linker falls back to raw points on a singular base.
- `spawner/test_spawner_max_volleys_edge.gd` — cap trip emits finished once; lowering onto the count emits stopped only.
- `volley/test_volley_bounce_scaled_shape.gd` — degenerate shapes bounce finite via radial fallback (never double-counted); slivers separate; sane shapes precise.
- `spawner/test_spawner_spin_matrix.gd` — spin is one shared matrix: spun ring equals rotated unspun ring, mirrored generators and sheared customs survive spin exactly.
- `spawner/test_spawner_orphan_policy.gd` — orphaned_volleys when a spawner is FREED (never on reparent): Keep Flying (bullets still die, one warning explains the lost hits/expiry), Hand To Factory (factory signals fire), Clear (On Clear per bullet, other owners untouched), Remove (silent); freeing a spawner from a hit handler mid-tick and scene teardown in both orders are clean; a live emitter with no hit handler (or whose receiver was freed) warns once; emit_collision_signals = false silences signals and warnings; spawner clear_active_bullets counts, fires On Clear and pools parked volleys; enum ids, labels and setter locked.
- `spawner/test_spawner_node_cache.gd` — validated manual assignment wins over stale paths; out-of-tree nodes stay assigned.
- `integration/test_engine_facts.gd` — engine facts the design relies on: `call_deferred` from `_physics_process` still runs inside the physics frame, Area2D overlap callbacks of frame N arrive before any `_physics_process` of frame N, connections live on the emitter (freeing the receiver drops them).
- `volley/test_volley_hit_contract.gd` — live hit contract: area/body_entered fire at the start of the volley tick with the bullet alive (status, hit count, custom data, attachment, impact pose all readable, first and LAST bullet alike); the kill happens after the handler (one On Destroy at the impact pose, never an extra On Hit); a heal vetoes it; handler disable/sibling disable/queue_free/free/target free end their records; exact counts for max 1/2/0; a pause parks the remaining records and replays them once; a parked overlap with a freed target is dropped; spawner routing; clear_active_bullets works inside a handler.
- `volley/test_volley_lifetime_contract.gd` — live lifetime contract: life_time_over fires inside the factory tick with every listed bullet alive (attachments, custom data readable), expiry runs after the handler (On Lifetime Over once per bullet the plugin expired), the handler vetoes via set_is_life_time_infinite / set_life_time_left (validated), owns bullets it disabled, only live bullets are listed, signal-off still expires, same-key spawns get another instance, free/queue_free/structural calls in the handler are safe, spawner routing.
- `volley/test_volley_signal_timing.gd` — every callback is live (inside the factory tick, never deferred): homing reached once per bullet per target with the bullet alive, auto-pop pops only the reached front (targets the handler pushed survive), one shared pop per tick, the spawner forwards reaches without per-volley connections, a reused volley drops the previous life's listeners, sprite_animation_finished once (restart re-arms, loops never finish), timers fire live, a sibling detached by an earlier callback never fires, attach inside a hit handler is immediate, a timer freeing its volley is safe.
- `volley/test_volley_freeze_contract.gd` — freeze contract: disable keeps every runtime value (debug snapshot equality, pose, hit count, custom data) and a wake resumes it; frozen bullets never collide and a wake reports a still-present overlap once; reset_state/bullet_reset_state clear only the ledgers; collision_amount -1/0/clamp; pooling off parks (owner, timers, features kept, whole-volley wake), expired parked volleys get a fresh lifetime, manual drains keep the remaining time and clock; suspended attachments are released once on pooling; frozen bullets accept configuration; life id bumps once per life; pooled wakes are refused; auto-pooling toggles parked -> pooled.
- `volley/test_volley_custom_data_contract.gd` — custom data contract: each bullet reads its own Resource (identity) and the shared value in every callback (killing hits for first/middle/last bullet, lifetime, bounce, homing reached, pre_shoot/volley_fired) alongside status/count/velocity/bounce count; tiled vs strict short arrays (never falls back to shared); runtime writes persist across hits and freeze/wake; pool reuse never leaks data and a pooled volley releases its references (weakref proof).
- `patterns/test_patterns_module.gd` — the pattern library is BulletPatterns2D (static-only, abstract): every generator/preview track/layout helper/inspector and pattern enum lives there and the factory exposes none; the spawner Path2D mode and helper_generate_transforms_polyline share one layout (108 knob combinations agree), polyline ids mirror PATH2D_*, hostile polyline input fails loud with exact text, closed even rings never double the seam.
- `volley/test_volley_bounce_signals.gd` — bounce signal contract: bounce handlers see the bullet alive; a consumed bounce counts exactly one hit after the bounce handler, and re-validates first (a handler that disabled or queue_freed the bullet ends the record, no hit signal for a dead bullet); free bounces never reach the hit path.
- `pooling/test_pool_cross_spawner_reuse.gd` — one pooled instance, two owners: spawner B's shot reuses the SAME volley spawner A pooled (identity), A's stored life id goes stale, hits/bounces/lifetime/homing reached reach B only (never A or the factory), A's timers/listeners/custom data/attachments never survive into B's life, A's retarget never touches it, two same-frame shots never share an instance.
- `pooling/test_pool_neutrality_sweep.gd` — pool neutrality: a generic sweep mutates every BulletVolley2D property (own and inherited Node/CanvasItem/Node2D: transform, top_level, filtering, sorting, process mode...), pools the volley and proves the reused instance reads exactly like the cold one; user groups and metadata are dropped on reuse, engine groups kept; runtime-only knobs (collision dedup mode) reset per life.
- `factory/test_factory_handler_rejects.gd` — structural calls from inside a hit handler are refused with a message naming the matching `*_deferred` twin; the deferred twins (factory reset, volley shape change) apply on the next idle frame.
- `spawner/test_spawner_fire_arc_generator.gd` — fire-arc gate reads the muzzle frame, not the spawner node.
- `volley/test_volley_rotation_wake_presence.gd` — same-owner wakes keep presence decisions; new lives reset them.
- `spawner/test_spawner_retarget_stagger.gd` — unrelated knob changes never re-arm the retarget countdown.
- `volley/test_volley_gravity_presence.gd` — shared gravity fills gaps only; seeded/per-bullet values survive.
- `volley/test_volley_orbit_rearm.gd` — linear re-arm updates every parameter; Random never re-rolls.
- `spawner/test_spawner_outline_visibility.gd` — distribution knob hidden for smooth loops, shown for corner shapes.
- `factory/test_factory_spawn_in_handler.gd` — same-key spawns from a killing-blow handler succeed (never the draining volley; pooled instances reused mid-sweep).
- `volley/test_volley_lifetime_signal_reuse.gd` — expiry signals survive same-frame pool reuse (physics-tick and call_deferred spawns); handlers see attachments; one disable per expiry, no enable churn.
- `volley/test_audit_regressions.gd` — audit pins: texture-rotation round-trip with offset, timer period carry, shared spin after bullet 0 is disabled, pool-key enum ids, mirrored transforms_scale, scaled-slope bounce normal, free_volley_deferred type gate, bounded distance-phased wobble.
- `spawner/test_spawner_lifecycle_signals.gd` — transitions report exactly once: pre-tree setters silent, load emits one started, toggles/caps report in-tree only.
- `spawner/test_spawner_terrain_crest.gd` — crest normals tilt with the slope (up-right ascending, up-left descending), all finite.
- `spawner/test_spawner_preview_state.gd` — dead track drops rings (dots survive); seed setters rebuild the gizmo.
- `spawner/test_spawner_path2d_gating.gd` — gated resampling: node motion reflects in frames, curve edits within the staleness bound.
- `spawner/test_spawner_pattern_source_lock.gd` — every PatternSource keeps its serialized integer (the enum is fully explicit now, so a renumber would repoint saved scenes), the inspector hint pins all 33, the setter accepts 0..32 and refuses the rest.
- `spawner/test_spawner_burst_mirror.gd` — burst alternate-mirror: the flag alternates every other shot, a mirrored SPIRAL / MULTISPIRAL actually reverses its winding (this was the bug: only the emitter spin flipped), non-spiral patterns are unaffected, DISTRIBUTE with the default `homing_max_targets=1` runs (and warns) while 2+ spreads.
- `spawner/test_spawner_property_visibility.gd` — inspector-visibility coverage: no `helper_*` property is invisible in all 33 pattern modes (the safety net for the untyped `begins_with` chain), aimed-target sharing between AIMED and CORRIDOR, homing knobs gated on `homing_enabled`; inspector layout lock (Setup group, Transform subgroup, one prefixed subgroup per shape with no stray members, Outline Layers last, related knobs adjacent); spin/burst/telegraph knobs follow their switch + mode, and every gating switch emits `property_list_changed`.
- `factory/test_factory_lifecycle.gd` — init recovery, spawn validation, deferred wrappers, flag reset, wake alias, NaN atomicity, teardown.
- `factory/test_factory_pooling.gd` — duplicate cache, retarget stagger, debugger budget, preview rings, pool hit/miss.
- `volley/test_volley_core.gd` — speed/direction/transform/velocity/rotation get/set + rejects.
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
- `volley/test_volley_crash_proof.gd` — hostile input (NaN/Inf/null/wrong-type/OOB/empty/degenerate), freed targets, disable/teleport storms, pool churn, queue/timer floods, 0/1/64-bullet extremes. Never crashes, never NaN-poisons, no leaks; every bound BulletVolley2D method swept on a bare (never spawned) volley with index probes 0/-1/99 and on a live volley with out-of-range indexes (never crash, never NaN).
- `volley/test_volley_mix_match.gd` — feature crosses: homing snakes, wobble rings, gravity+drag+curves, spin+patterns+homing, sovereign per-bullet volleys, spawner mixed shots, expiry inside mixes.
- `volley/test_volley_rotation_helpers.gd` — per-bullet rotation API (get/set/range/clear) crossed with shared fallback, negative spin + -max clamp, stop flag both ways, rotate_only both ways, adjust steering + skip, live order both ways, remove persistence, shared getters, inverted/OOB ranges, tick spin, pool reuse, hostile input.
- `factory/test_factory_helper_edges.gd` — helper hostile input: amount caps (10000/call), grid-product/gap/points/vertices caps, NaN/Inf rejects, huge-finite clamp-to-box, scale band, skip/layer guards.
- `spawner/test_spawner_pool_stress.gd` — 3 spawners on 1 factory: shared fire, cross-shape reuse isolation, reset/free under live volleys, adopt chains, spawner-freed orphans, retarget storms, pool-hit accounting.
- `volley/test_volley_math_edges.gd` — quantitative tick identities: drag decay band, rotation clamp, wobble boundedness, smoothing clamps.
- `spawner/test_spawner_patterns.gd` — all 33 sources (parameterized), cap, order ops, spin/scales, skip carve, presets.
- `spawner/test_spawner_homing_orbit.gd` — 6 target sources, cache, fire-arc gate, retarget, fuse, stagger.
- `spawner/test_spawner_signals_sequencing.gd` — handler contracts, burst/telegraph/pattern-list, cap, adopt/clear/override.
- `integration/test_interpolation_integration.gd` — interpolation agreement/toggle, pause, churn, two factories.
- `common/blast_test_helpers.gd` — shared `BulletVolleyData2D` builders (identical speeds/layers/sizes so failures mean regressions).
- `factory/test_factory_generator_fuzz.gd` — crash-fuzz for every generator: hostile counts (incl. the 10000 cap on all 29 generators, amount 0 = silent empty), degenerate geometry, NaN inputs, extreme twists/offsets, oversized scales, edge-image extraction, side-spread/skip contracts, exact-geometry semantics. Every rejection is pinned with its exact error right after the call; valid input never errors.
- `factory/test_factory_layer_rings.gd` — outline-layer design proofs: every extra layer re-spawns the shape scaled about the loop center, facings/quotas/corners exact per ring.
- `spawner/test_spawner_refactor.gd` — `bullet_spawner2d.cpp` internals: keep-awake predicate (incl. stop_pattern_list-must-not-sleep-a-pending-burst), freed factory/generator/target/path2d report missing, corridor door + fan-vs-aimed parity, pattern-hint lock, outline setter reject-and-keep.
- `spawner/test_spawner_tree.gd` — live-tree integration: error paths, preview/cap/custom/line collection, homing resolution, live shooting, factory smoke.
- `spawner/test_spawner_preview_coincidence.gd` — preview dots sit on the layer rings for every outline shape, fill deal, twist/cap/scales/curve, sides/offsets/layouts/distributions, corner knobs, dense star.
- `volley/test_volley_fx_layers.gd` — sprite-effect layers with REAL physics: validation, spawn flash, trails (disable/wake), destroy/hit/bounce/lifetime/clear triggers, desync shards, per-bullet toggles, pool-reuse reseed + caps, styling, spawner integration, interpolation agreement, colour ramps, fades, whiten overrides, hostile configs.
- `spawner/test_wave1_regressions.gd` — wave-1 regression pins: fire arc in the generator frame, rotation presence across a same-owner wake, retarget stagger kept across no-op setters, gravity fill-gaps, outline-distribution gating, linear orbit re-arm, bad presets are no-ops.
- `spawner/test_spawner_spin_bench.gd` — spin on/off + cold/warm pool timings (heart, 1500 bullets) with catastrophe-only budgets; the warm shot is proven to be a pool hit. Real tracking lives in `tools/run_benchmarks.py`.
- `spawner/test_spawner_spin_perf.gd` — a 10k spiral collect with advancing spin stays in the millisecond class (catastrophe guard).
- `pooling/test_pool_attachments.gd` — BulletAttachment2D lifecycle through the pool (AttachmentProbe2D counts callbacks): spawn/disable/enable/in-pool sequences, blank slots on reuse, null/wrong-type scenes rejected.
- `pooling/test_pool_cross_owner_handover.gd` — adopt hand-over between spawners, foreign pooled wake warns and reseeds, `free_volley_deferred` from a collision handler never corrupts the sweep.
- `pooling/test_pool_key_validity.gd` — exact (amount + shape) pool hits; mismatches miss and allocate (never silent reuse); capsule bucket isolation; per-bucket free; mismatched enable refuses cleanly.
- `pooling/test_pool_multispawner_shared_factory.gd` — three spawners plus direct spawns on ONE factory: census by owner, per-spawner retarget and live-bullet fuse, per-bucket free isolation.
- `pooling/test_pool_state_reset.gd` — state-reset matrix: a volley seeded with every feature is drained and reused neutral; runtime shape change re-buckets; a new amount never reuses.
- `integration/test_reentrant_frees.gd` — user handlers calling `free()` re-entrantly: attachments freed inside a killing/non-killing `body_entered` and inside `life_time_over` (both used to read freed memory; the lifetime one segfaulted), and the LAST bullet's killing-hit handler sees its own attachment.
- `factory/test_factory_frame_stats.gd` — profiling API: exact spawn/tick/expiry/collision counters in `get_frame_stats()`, measured tick time, `reset_frame_stats()`, BlastBullets2D/* editor monitors owned by one factory and removed on exit.
- `spawner/test_spawner_pattern_bake.gd` — pattern bake cache: cached == uncached for all 33 sources under random generator motion, RIGID ring hits while moving (no re-bake), scaled generator re-bakes once, TRANSLATION class for world-direction rain, unseeded randomness and external-input sources never cached, OFF mode, burst-mirror bakes, a generic sweep proving EVERY pattern inspector property invalidates the bake (mutation-tested), skip/scales after the bake.
- `spawner/test_spawner_preview_pose.gd` — zero-cost preview: 30 spinning frames = 0 rebuilds/0 redraws, layer pose equals the live spin (P3), drawn dots land exactly on spawned bullets (incl. mirrored generator), rigid parent moves never rebuild, target-dependent sources still do, spin never hides target moves (P2), mid-spin rebuild keeps the track under the dots (P1), inspector refresh signals (P8), layer-start-offset gating (P9).
- `volley/test_volley_render_buffers.gd` — the multimesh instance buffer (uploaded with one set_buffer at spawn) matches every bullet: cold spawn, pooled re-spawn (no stale poses), after ticks, spawner native-path shot (bullets at their pattern slots).
- `integration/test_steady_state_memory.gd` — warm pools retain NOTHING per shot/spawn (live Object count flat over 200 spawner shots and 200 short-array script spawns; the exit leak check cannot see retention), and configuration warnings fire once per resource (re-armed by a new array size or another resource).
- `spawner/test_spawner_easing.gd` — the C++ easing used by movement matches Godot's Tween.interpolate_value for all 12 transitions x 4 eases (33 samples each), endpoints, clamping, invalid ids.
- `spawner/test_spawner_movement.gd` — Path2D movement: duration/speed timing, Tween-identical easing, ONCE/LOOP/PING_PONG leg counts + exact signal counts, reverse, ATTACH vs RELATIVE_TO_START, rotate-with-path (+offset, turns around), start ratio/delay/endpoint pause, progress Curve, play/pause/seek/reverse/stop, reject-and-keep for every setter, unusable/freed paths (warn once, never NaN), handlers that free things, shooting while moving (pattern centered, inherited velocity), bake hits + zero preview rebuilds while moving, _process sleeps after the run.
- `integration/test_scene_moving_turret.gd` — `scenes/moving_turret.tscn` (authored like an editor scene): serialized movement properties/enum ids load, the turret patrols, auto-fires and hits a floor, spawner-owned hits stay off the factory signal.
- `volley/test_volley_range_contract.gd` — EVERY all_bullets_* range method (discovered via get_method_list): (0, -1) = whole volley, end -1 = through the last bullet, out-of-range/inverted ranges fail loud exactly once and apply nothing.
- `volley/test_volley_pause_overlaps.gd` — factory pause/resume: overlaps that start while paused hit exactly once on resume, pause-only overlaps that ended are cancelled, pre-pause overlaps are never recounted, parked records never reach a pooled volley's next life.
- `spawner/test_spawner_setter_contract.gd` — generic reject-and-keep sweep over get_property_list(): every float rejects NaN/Inf, every Vector2 rejects NaN, every enum rejects ids outside its hint, every colour rejects NaN - exactly one 'keeping the old value' error and the value unchanged; caps match the generators, corridor width/gap load in any order (gap clamped with one warning), NodePaths reject wrong types, a shorter interval shortens the running wait, out-of-range skip indices and oversized grids warn once.
- `spawner/test_spawner_api_surface.gd` — removed legacy members stay removed (bullets_per_petal, is_orbit_armed, get_pooled_volley_count, spin aliases, debug log, internal burst hooks, ...), cache info carries the template keys, the legacy 'transforms_source' list key is an unknown key, one auto-pop switch drives both queue kinds, the data's adjust_direction flag is no longer overwritten.
- `spawner/test_spawner_cadence.gd` — initial delay + interval timing, fire_n_volleys/pause/resume/volleys_remaining (budget clears itself, resume shoots normally), controls called before the tree survive _ready, deferred and nested shots, a skip reason for every failure, budget check before pattern work, shooting_finished only for auto-fire, seeded reload jitter (bounded, reproducible, seeds independent).
- `spawner/test_spawner_burst_telegraph.gd` — auto bursts never pass max_volleys or a fire_n_volleys budget, disabling auto-fire or burst mode cancels with burst_finished, pause freezes and resume completes the chain, mirror rhythm starts plain for any count, burst_count shrinking mid-chain keeps indexes 1..n, a misconfigured burst reports once, the telegraph payload matches the shot (one warning per chain).
- `spawner/test_spawner_pattern_lists.gd` — entries are temporary overrides in both modes: order + amounts, source/amount/spawn data and preset-written knobs restored after each entry, pattern_list_finished once in both modes, same spawn data keeps the template, bad inputs fail loud with exact wording.
- `spawner/test_spawner_presets.gd` — all 20 presets draw their amount; presets are clean (Bullet Patterns knobs and Spin reset, Transform subgroup and node wiring kept); same preset, same pattern.
- `spawner/test_spawner_tree_reentry.gd` — reparenting keeps pointer-assigned nodes (path filled in), out-of-tree assignments replace stale paths, tracked homing volleys, burst chains and pattern lists resume; spawners enabling homing together stay staggered.
- `spawner/test_spawner_homing_selection.gd` — nearest order, seeded random without repeats, tree order, round robin across shots (peeking does not rotate), distribute deal, max_targets bounds, detection range + filter group, node-name match modes and case, recursive children, mouse resolves no nodes.
- `spawner/test_spawner_homing_propagation.gd` — steering and orbit settings reach every bullet, per-bullet smoothing fan, homing signals with exact counts/payloads, target-reached forwarding, retarget counts + previous-volleys switch, fire arc follows spin and the volley chases the approved target, orbiting without homing warns once and emits no homing signals.
- `spawner/test_spawner_live_volleys.gd` — adopt_live_volley rejects null/pooled/dying volleys, clear_live_volleys_homing empties queues, override_live_volleys_velocity changes flight and rejects NaN.
- `spawner/test_spawner_collision_signals.gd` — area_entered, life_time_over and bounce_area_entered go to the spawner; the factory's same-named signals stay silent.
- `spawner/test_spawner_setup_warnings.gd` — every setup warning string (factory, spawn data, invisible data, aimed/corridor/path2d/custom, movement path, orbit without homing) and none for a wired spawner.
- `spawner/test_spawner_homing_detection.gd` — every homing_target_source: what it finds and never picks (the spawner, its markers, nodes under the factory, dying nodes, non-Node2Ds), plain volleys + homing_targets_resolved([]) when nothing is found, one warning per homing configuration, target-path type check.
- `spawner/test_spawner_homing_queues.gd` — 256-target queue cap without errors, freed targets trimmed mid-flight, retarget skips dead/pooled/foreign/old-factory volleys and disabled bullets, homing signal counts and payloads, handlers freeing the volley or the target.
- `spawner/test_spawner_pattern_counts.gd` — every generated pattern draws EXACTLY helper_bullets_amount bullets (On Outline, Layers, Fill Inside) at distinct spots; flower FAN splits the amount over petals; rose/lissajous/spiral-stride/seam/self-closed-path/cross/fill overflow and gap patterns (Corridor, Ellipse WALL) keep every bullet visible.
- `spawner/test_spawner_config_ux.gd` — muzzle offset space (GLOBAL default vs LOCAL turning with the spawner), the preview draws the offset exactly where bullets spawn (both spaces, rotated + spinning), editor configuration warnings, and the auto-fire error latch (one report per misconfiguration, re-armed by a config change; manual calls always report).
- `test_no_orphan_suites.gd` — repo hygiene: fails if any runnable `test_*.gd` exists outside `tests/` (invisible to the runner, would silently stop running).

## Conventions

- `await idle(n)` resumes on an idle frame: safe for structural factory calls
  (`reset`/`free_*`/`populate_*`). `await physics(n)` resumes *inside* a
  physics frame (bullets moved): structural calls reject there, so call
  `idle()` first. Never use GUT's `wait_*_frames` (n+1 resume skew).
- `before_each` adds a fresh `factory` (use `make_spawner()` / `quick_volley()`
  / `make_preview_spawner()` builders); `after_each` asserts
  `debug_assert_no_dangling()` + zero new orphans.
- Rejections are loud by contract: pin them RIGHT AFTER the hostile call with
  `expect_error_sequence([exact texts in order])` (exact count + order +
  wording), `expect_error(text)`, or `expect_errors_containing(text, N)`
  (EXACTLY N; `at_least=true` needs a `# lint: at-least <reason>` comment).
  `expect_no_errors()` is a mid-test checkpoint. `swallow_errors()` is only
  allowed in `tools/lint_tests.py`'s fuzz allowlist.
- `tools/lint_tests.py` (run automatically by the runner) enforces these
  rules plus: no `extends SceneTree` suites, no GUT `wait_*_frames`, no stray
  `print(` (use `print("BENCH ...")` for benchmark output), and every suite
  listed in this README.

## Running

```
python3 tools/run_tests.py                  # every suite, summary table
python3 tools/run_tests.py --suite volley   # substring filter
python3 tools/run_tests.py --changed-only   # suites plausibly affected
python3 tools/run_tests.py --list           # discover without running
python3 tools/run_tests.py --self-test     # canaries: proves failures + leaks are detected
python3 tools/run_tests.py --report        # also write test_project/test_results/summary.json
python3 tools/run_tests.py --realtime      # real-time pacing instead of simulated time (slow)
```

Time is SIMULATED by default (`godot --fixed-fps 60`): every frame advances
exactly 1/60 s and runs as fast as the CPU allows, so frame-counted tests
behave identically and the whole run takes seconds (bounce: 101 s -> 0.14 s).

A file is green only when its process exits 0 **and** GUT's JUnit report shows
0 failures **and** the `--verbose` exit report shows no leaks (ObjectDB/RID/
resource/StringName) **and** no `SCRIPT ERROR` was printed. A missing report
means the extension or GUT never loaded — reported as CRASH, never PASS (that
is the check which catches a stale `.so`: an unloaded extension makes
`debug_get_*` assertions pass against defaults).

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
