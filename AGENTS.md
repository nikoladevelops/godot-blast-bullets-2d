# AGENTS.md — BlastBullets2D agentic workflow

Godot 4 GDExtension (C++ via godot-cpp) + GUT headless suites. Stability and
performance are Priority 1; every behavior change must be verified by an
executed test. Read §0 first, then the section you need before touching
`src/` or tests. Every fact below was verified by running it; if you find
one that is wrong, fix this file in the same change.

## 0. Quick start + decision table (read this even if you read nothing else)

```sh
GODOTPP_NONINTERACTIVE=1 python3 tools/compile_debug_build.py   # build (never raw scons, never edit SConstruct)
python3 tools/run_tests.py                                       # 97 files / ~600 tests, ~8 s, leak-checked
python3 tools/run_tests.py --self-test                           # the harness itself still catches failures/leaks
python3 tools/run_benchmarks.py --scenario <name>                # perf evidence (§15)
```

| Situation | Do this | Never do this |
|---|---|---|
| A test fails after your change | Read the FULL failure text; reproduce with `--suite <file>`; fix the code | Edit the assertion to match new behavior without asking the user (§16.3) |
| Unexpected `push_error` fails a test | Find the emitter with `rg -n "<text>" src/`; decide if the call is wrong (fix the test input) or the code is wrong (fix code) | `swallow_errors()` (lint rejects it outside the fuzz allowlist) |
| `CRASH` / missing JUnit report | Stale or broken `.so`: rebuild, rerun; check the log for `SCRIPT ERROR` / `class_db.cpp` | Assume it passed |
| `LEAK` verdict | Something survives the test: `add()`/autofree nodes, free RIDs, disconnect | Add `--no-leaks` and call it green |
| New property invisible in inspector | ADD_PROPERTY before its bind, wrong subgroup prefix, or `_validate_property` gate (§9) | Hide the visibility test failure |
| Need to know if your test is real | Mutation-check it (§6): break the code on purpose, watch the test fail, restore | Trust a test you never saw fail |
| Perf change | Benchmark before AND after on the same machine/build (§15) | Claim a speedup from one run or a different build type |
| Unsure about engine behavior | Write a 10-line probe test in `test_project/tests/` and run it | Guess from memory of Godot docs |

## 0b. Commits (user rule — overrides any tool or harness default)

- NEVER add `Co-Authored-By` lines, and never mention Claude, Anthropic,
  or any AI model/assistant anywhere in a commit (title, body, trailer).
- Title: imperative, short and descriptive, at most 50 characters
  (`Fix flower bullet count`, `Add spawner cadence tests`).
- Body: optional, at most 3 short lines saying what changed and why.
  No essays, no test counts, no file lists.
- Commit only when the user asks (or the approved plan says so); push only
  when the user asks. Never force-push without explicit approval.
- Stage files explicitly. Never stage unrelated user changes (e.g. a scene
  the user edited by hand, like `test_project/benchmark_scene/benchmark.tscn`).

```sh
git add <the files you changed>
git commit -m "Fix flower bullet count" -m "FAN splits the amount over petals."
```

## 1. Running tests (only supported path)

```sh
python3 tools/run_tests.py                  # every suite, leak-checked
python3 tools/run_tests.py --suite volley    # substring/glob filter (repeatable)
python3 tools/run_tests.py --changed-only    # files plausibly affected by uncommitted changes
python3 tools/run_tests.py --fail-fast       # stop scheduling after first red file
python3 tools/run_tests.py --list            # discover without running
python3 tools/run_tests.py --self-test       # canaries must report FAIL + LEAK + strict mismatch
python3 tools/run_tests.py --no-leaks        # faster, NOT green (skips leak gate)
python3 tools/run_tests.py --report          # + test_project/test_results/summary.json (slowest first)
python3 tools/run_tests.py --realtime        # real-time pacing (default is simulated time, see below)
python3 tools/lint_tests.py                  # static test lint (the runner runs it first; exit 3 = lint failed)
```

- Time is SIMULATED: the runner passes `--fixed-fps 60`, so every frame
  advances exactly 1/60 s and runs as fast as the CPU allows. Frame-counted
  tests behave identically to real time; the full suite runs in ~8 s.
- A file is green only when ALL hold: process exits 0, GUT JUnit shows
  0 failures (missing report = CRASH, never PASS — catches a stale `.so`),
  `--verbose` exit report has no leaks, no `SCRIPT ERROR`, and no
  `godot-cpp/src/core/class_db.cpp` error (a dropped property/method).
- Never run test files directly with `--script`; the runner does the
  class-cache refresh, per-file processes, JUnit + leak verdicts.
- Lint rules (`tools/lint_tests.py`): R1 no `extends SceneTree`; R2 every
  suite `extends BlastTest`; R3 no GUT `wait_*_frames`; R4 `swallow_errors()`
  only in `volley/test_volley_crash_proof.gd`; R5 `at_least=true` expects
  need a `# lint: at-least <reason>` comment; R6 every suite listed in
  `test_project/tests/README.md`; R7 no stray `print(` (only `print("BENCH`).

## 2. Leak detection

- Runner passes `--verbose` to headless Godot and fails files matching:
  `ObjectDB instances leaked`, `Leaked instance:`, `RID ... leaked`,
  `resources still in use`, `Orphan StringName`.
- `BlastTest.after_each` asserts `debug_assert_no_dangling()` + zero new
  orphans after EVERY test — a leak fails at the test, not at exit.
- Deliberate leakers / failers live in `test_project/tests_meta/` and run
  ONLY via `--self-test` (they must be reported, never green).
- `integration/test_steady_state_memory.gd` pins the live Object count
  across repeated warm shots (retention leaks the exit report never sees).

## 3. Searching (use `rg`, not `grep`)

```sh
rg --files test_project/tests -g 'test_*.gd'    # suite inventory (-g globs the BASENAME)
python3 tools/run_tests.py --list               # same inventory, as the runner sees it
rg -n "push_error|emit_signal" src/bullets/     # fail-loud sites (pin exact text in tests)
rg -n "^func test_" test_project/tests/volley/test_volley_bounce.gd
rg -n 'BulletSpawner2D::shoot_once' src/        # where a method lives (files are split, §12)
```

## 4. GUT framework reference (what's available, what's used)

- **Discovery**: `.gutconfig.json` points at `res://tests/`; file must be
  named `test_*.gd` and extend `BlastTest`. Each file runs in its OWN Godot
  process (no cross-file state); tests inside a file run sequentially.
- **Lifecycle**: `before_all` / `before_each` / `after_each` / `after_all`.
  `BlastTest.before_all` turns the spawner cache verifier ON (§13);
  `before_each` builds a fresh `factory` (+2 idle frames); `after_each`
  idles, asserts a dangling-free factory, `reset()`s, and asserts zero new
  orphans. Override `before_each` with `await super()` first.
- **Asserts**: `assert_eq/ne`, `assert_almost_eq/ne`, `assert_gt/gte/lt/lte`,
  `assert_true/false`, `assert_between`, `assert_has`, `assert_has_method`,
  `assert_null/not_null`, `assert_freed/not_freed`, `assert_no_new_orphans`,
  `assert_property*`, `assert_string_contains`, `assert_connected`,
  `assert_has_signal`, `assert_is/typeof`, `assert_eq_deep`. Prefer the
  TYPED assert matching the comparison (failure output shows got/expected).
- **Signals**: `watch_signals(obj)`, `assert_signal_emitted`,
  `assert_signal_not_emitted`, `assert_signal_emit_count(obj, name, n)`,
  `assert_signal_emitted_with_parameters`, `get_signal_emit_count`,
  `get_signal_parameters`. Use COUNTS for exactly-once contracts
  (`shooting_started/stopped`, movement signals, `property_list_changed`).
- **Parameters**: `func test_x(i: int = use_parameters([...]))` runs once
  per value with full before/after_each isolation (33-source sweeps, the
  12x4 easing parity). This GUT version groups executions under one
  `<parameterized>` JUnit case — isolation is real, per-value reporting is not.
- **Doubles** (`double`/`spy`/`stub`, `assert_called*`): available, UNUSED
  so far. Reach for them when a handler must be observed without side effects.
- **Strict errors**: any `push_error`/engine error not claimed by an
  `expect_*` fails the test (`get_errors()` + `err.handled`).

## 5. Test doctrine (`test_project/tests/common/blast_test.gd`)

- Builders: `spawn_dir()`, `make_spawner()` (shooting + homing OFF before it
  enters the tree), `make_preview_spawner()`, `H` (`blast_test_helpers.gd`:
  `make_directional_data`, `make_still_data`, `make_speed`, `make_rotation`,
  `make_effect_layer`, `make_flat_curve`, ...), `make_wall/make_area/
  make_probe_scene/add` (autofreed).
- Frames: `await idle(n)` (idle frame — structural factory calls
  `reset/free_*/populate_*` allowed) vs `await physics(n)` (INSIDE the
  physics step — structural calls rejected; call `idle()` first). NEVER
  GUT's `wait_*_frames` (resumes after n+1 frames, skews every count).
- Pin errors RIGHT AFTER the hostile call:
  `expect_error_sequence(["exact text", ...])` (exact count + order +
  wording; preferred), `expect_error(text)`, `expect_errors_containing(text, N)`
  (EXACTLY N), `expect_no_errors()` (mid-test checkpoint). End-of-test lumps
  hide bugs: strict pinning once exposed two bounce tests that silently ran
  with a setter-rejected value (`bounce_cooldown_sec = 5.0` is outside [0, 1]).
- `Array(...)`-wrap engine arrays before `assert_eq` against literals.
- Headless rendering: the dummy RenderingServer stores multimesh buffers
  but `get_instance_transform_2d()` returns identity — decode
  `multimesh.buffer` instead (`volley/test_volley_render_buffers.gd`).

## 6. Writing good tests (checklist)

- Name: `test_<behavior>_<expectation>` (`test_paused_steady_overlap_registers_once_on_resume`).
- One behavior per test; shared builders over local setup; no magic
  numbers without a comment (frame budgets, tolerances, layer values).
- Failing-first: write the test, watch it fail for the RIGHT reason
  (assert vs unexpected-error), then fix, then keep it.
- **Mutation check** (how to prove a test is real when the fix already
  exists): copy the source file to the scratchpad, disable the fix (e.g.
  `if (false && ...)`, comment out a `notify_property_list_changed()`),
  rebuild, run the suite — it must FAIL on exactly the tests you expect —
  then restore the copy and rebuild. Used for the cache invalidation sweep,
  render buffers and inspector gating.
- Determinism: never wall-clock sleeps; frame counts with margins (prefer
  early-break loops `for i in N: await physics(); if cond: break`);
  `seed()` any randomness you assert on.
- Each `push_error` gets an `expect_*` with the EXACT engine wording (copy
  from a failing run, verify it fires where you think).
- New API surface: `has_method` bind check + happy path + reject-and-keep
  + OOB + pool-reuse neutrality (spawn → pool → respawn, no state leaks).
- Prefer GENERIC tests that walk `get_property_list()` / `get_method_list()`
  so future additions are covered automatically (range contract sweep,
  bake invalidation sweep, subgroup-prefix lock, visibility sweep).

## 7. Integration tests, scenes & feature mixing

- Harness pattern: `make_spawner(data, source, n)` + `make_wall(pos)` /
  `make_area` on known layers + `watch_signals` on factory/spawner/volley.
  `make_wall` defaults to layer value 4 (matches `H.make_*_data` masks);
  the bounce suite uses its own convention (bounce wall 8, plain 16).
- A spawner added to the tree auto-fires by default: use `make_spawner()`
  or `set_shooting_enabled(false)` BEFORE `add()`.
- Scene tests: author a `.tscn` under `test_project/tests/scenes/` like an
  editor scene, `load().instantiate()` it in a suite
  (`integration/test_scene_moving_turret.gd`). This also locks
  serialization: property names + enum ids must survive load.
- Physics tests need REAL frames: spawn → `await physics(k)` with
  early-break; assert counts, signals AND finiteness.
- Re-entrancy: `integration/test_reentrant_frees.gd` frees things inside
  every signal handler. Godot REFUSES `free()` on an object that is
  currently emitting; handlers must `queue_free()` the emitter.
- Feature crosses: combine 2–4 systems (homing+wobble+gravity+curves,
  movement+spin+homing); assert each system's signature survives + full
  finiteness + pool-reuse neutrality. Pairwise beats exhaustive.
- Preview tests: `make_preview_spawner`, `await idle(6)` for rebuilds,
  `debug_check_layer_coincidence(tol)`, `debug_get_preview_stats()`
  (spin/move must keep `rebuilds`/`*_draws` flat).
- Spawner-owned vs factory-owned routing: connect BOTH signal sets, assert
  the silent side stayed silent.

## 8. New-feature checklist (do all of these)

1. Bind check (`has_method`) + happy-path test.
2. Setter reject-and-keep: NaN/Inf/OOB/negative/inverted, old value kept,
   exact error text pinned.
3. Inspector: right group/subgroup (§9), per-mode visibility, gating
   setters call `notify_property_list_changed()`, hint strings byte-exact,
   serialized enums locked.
4. If it affects pattern geometry: the setter calls `on_pattern_changed()`
   (§13) — the bake sweep fails otherwise.
5. Pool-reuse neutrality + no-dangling (automatic via `after_each`, but add
   explicit state assertions).
6. Signals: emission counts incl. negative cases (stays silent); liveness
   check after every emit (§12).
7. `doc_classes/<Class>.xml` (§10), `tests/README.md` entry (lint R6).
8. Hot path touched? Benchmark before/after (§15).

## 9. Inspector groups & serialization locks

- Spawner groups, in order (locked by `test_volley_bounce.gd`): Setup,
  Bullet Patterns, Shooting, Spin, Homing, Orbiting, Preview, Movement,
  Performance. Spawn-data: Bullets/Appearance/Collision/Attachments/Sprite
  Effects/Per-Bullet Rotation. No duplicate group titles (tested).
- Bullet Patterns: `pattern_source` + `helper_bullets_amount`, then a
  `Transform` subgroup (scales, muzzle offset + space, skip indices), one
  `ADD_SUBGROUP("Ring", "helper_ring_")` per shape (prefix stripped in the
  inspector), `Outline Layers` last. The inspector EJECTS a property whose
  name lacks the subgroup prefix; `test_every_prefixed_subgroup_member_carries_the_prefix`
  catches it. Mind overlapping prefixes (`helper_star_polygon_` vs
  `helper_star_`, `helper_counter_spiral_` vs `helper_spiral_`).
- ADD_PROPERTY BEFORE its bind_method is SILENTLY dropped by ClassDB (the
  runner flags the `class_db.cpp` error). Moving a property = moving its
  whole bind+ADD_PROPERTY paragraph.
- A setter whose field `_validate_property` reads MUST call
  `notify_property_list_changed()` (pinned per switch).
- Gating: helper_* per pattern mode; homing/orbiting/movement/preview knobs
  hide while their master switch is off; spin speed only in Continuous,
  amplitude/frequency only in Oscillate; burst_*/telegraph_sec only when on.
- Enum ids are serialized into `.tscn`: renumbering silently repoints saved
  scenes. `pattern_source` ids and pool-key shape ids
  (`Circle:3,Rectangle:4,Capsule:5`) are locked — extend the lock for any
  new serialized enum. Saved property ORDER follows the bind order, so an
  inspector reorg reorders `.tscn` lines (harmless, expected in diffs).

## 10. Documentation generation (verified behavior)

- Order: close the editor → rebuild (`compile_debug_build.py`) →
  `GODOTPP_NONINTERACTIVE=1 python3 tools/generate_xml_docs.py` → review
  `git diff doc_classes/` → fill every empty `<description>` → rebuild
  (docs compile into the binary) → regenerate once more: the diff must be
  empty apart from escaping (`>` becomes `&gt;`).
- The doctool KEEPS existing descriptions of members that still exist,
  ADDS new members with empty text, DROPS removed ones and re-sorts
  alphabetically (never hand-reorder). Write literal `<`/`>` as
  `&lt;`/`&gt;` or the XML breaks.
- BBCode: `[Class]`, `[method C.m]`, `[member C.p]`, `[signal C.s]`,
  `[param x]`, `[constant C]`, `[enum C.E]`, `[code]`, `[codeblock]`, `[b]`, `[i]`.

## 11. `tools/` + `setup.py` catalog

- `setup.py`: interactive menu (Godot paths/versions, project folder,
  rename, icons, docs, debug/release builds, profiles, LTO, export zip,
  tutorials). First stop for a new machine.
- Build: `compile_debug_build.py` / `compile_release_build.py` /
  `clean_build.py` / `select_build_profile.py` / `edit_build_profile.py` /
  `change_lto_mode.py` / `toggle_editor_target.py` /
  `toggle_debug_symbols.py` / `toggle_reloadable.py` (hot reload). New
  `.cpp` files under `src/` are picked up automatically (recursive glob).
- Config: `select_godot_path.py`, `select_godot_project.py`,
  `change_godot_target_version.py`, `update_godot_cpp.py`,
  `config_manager.py` + `config.json` (machine state — don't commit
  personal paths), `paths.py` (canonical locations).
- Plugin: `renaming.py`, `update_icons.py`, `export_plugin.py` (asset-store
  zip), `generate_xml_docs.py`, `gdextension_file_helper.py`,
  `apple_helpers.py`, `git_helpers.py`, `scons_helpers.py` +
  `scons_build_helpers.py`, `tutorials.py`.
- Tests/perf: `run_tests.py`, `lint_tests.py`, `run_benchmarks.py`.

## 12. Architecture map (read before debugging)

```
src/
  factory/bullet_factory2d.cpp            spawn entry points, pools, structural calls, effects, stats/monitors, bindings
  factory/bullet_factory2d_patterns.cpp   helper_generate_transforms_* generators, outline layout/samplers, debug verifiers
  bullets/multimesh_bullets2d.{hpp,cpp}   shared volley base: buffers, physics area + ONE shared shape, lifetimes,
                                          attachments, collision intake/dedup/drain, paused-overlap park/replay, ranges
  bullets/directional_bullets2d.cpp       directional tick: movement, homing/orbit, curves, bounce
  bullets/block_bullets2d.*               block (shared-velocity) volleys
  bullet_spawner/bullet_spawner2d.cpp     wiring, shooting cadence, spin, bursts/telegraph, pattern lists, lifecycle, shoot_once
  bullet_spawner/bullet_spawner2d_pattern_properties.cpp  Bullet Patterns accessors + apply_pattern_preset
  bullet_spawner/bullet_spawner2d_patterns.cpp   raw generation, bake cache, native span collect, verifier
  bullet_spawner/bullet_spawner2d_homing.cpp     homing/orbiting, target resolution, live-volley steering
  bullet_spawner/bullet_spawner2d_preview.cpp    preview snapshot/rebuild, pose, dirty checks, debug_* geometry
  bullet_spawner/bullet_spawner2d_preview_layer.cpp  PatternPreviewLayer2D (draw_multimesh canvas layer)
  bullet_spawner/bullet_spawner2d_movement.cpp   Path2D movement
  bullet_spawner/bullet_spawner2d_bindings.cpp   _bind_methods (groups/subgroups) + _validate_property
  bullet_spawner/bullet_spawner2d_internal.hpp   statics shared by >1 spawner TU (limits, pattern-source table)
  shared/  easing2d.hpp (Tween port), warn_once2d.hpp, cached_string_names2d.hpp, pools, data resources
```

- Same class across the split files (pure moves). Put new code in the file
  of its concern; keep per-bullet loops inside ONE translation unit (no
  cross-TU call per bullet).
- Threading: everything runs on the main thread (physics callbacks
  included); no locks exist and none are needed. Do not add threads.
- **Spawn flow**: `BulletFactory2D.spawn_controllable_*` → pool pop by
  `MultiMeshPoolKey2D` (amount + shape + type) or fresh alloc →
  `enable_multimesh` (validates EVERYTHING before mutating; a refused
  enable changes nothing) → `set_up_bullet_instances` (one `set_buffer`
  upload). The spawner calls the C++ span path
  (`spawn_controllable_directional_bullets_span`, no Variant boxing).
- **Pooling**: one bucket per key; pop prefers newest non-ticked volley;
  same-key spawns from handlers reuse mid-sweep. Structural ops
  (`reset/free_*/populate_*`) are idle-frame only: inside a physics frame
  they are REJECTED loudly. Their `*_deferred` twins queue through
  `queue_structural_call` and are safe from anywhere.
  `free_active_bullets()` DESTROYS (next spawn is cold); clear/expiry park.
- **Lifetimes**: `reduce_lifetime` → `disable_bullet` per slot → last-out
  funnels to pool. With `life_time_over` armed, the volley is HELD out of
  the pool (`lifetime_flush_pending`) until the deferred signal +
  attachment releases flush. Deferred attachment releases carry
  (index, instance id, epoch) triples and are queued only for slots that
  hold an attachment.
- **Collision pipeline**: area callbacks (ADDED) → object-level dedup
  window (O(1) hash; `collision_dedup_by_object = false` for shape-level)
  → records with queue-time epochs/velocity/pose → per-tick drain →
  counting → signals → bounce. Paused factory: ADDED events are PARKED per
  volley (cap 4096, deduped, cancelled by REMOVED) and replayed once on
  resume; overlaps counted before the pause are never recounted.
- **Liveness doctrine (C++)**: never carry a raw `Object*` across user
  code or `call_deferred` (godot-cpp converts Variant args BEFORE your
  body's id check runs). Capture `get_instance_id()` BEFORE the emit and
  compare `ObjectDB::get_instance(id) == expected_ptr` afterwards
  (`slot_still_holds_attachment_id`, `revalidate_configured_volley`,
  movement's `alive()` lambda). Stop touching `this` when it fails.
- **Spawner loop**: `_process` runs iff `needs_process()` (shooting, spin,
  retarget, preview, movement, burst/telegraph/pattern list pending).
  Runtime order: movement → spin → preview → shooting/bursts, so shots and
  the gizmo use this frame's pose. `shooting_*` signals track auto-shooting
  transitions only. Auto-fire errors are latched (one report per
  misconfiguration until a config setter re-arms it; manual calls always
  report); `get_setup_warnings()` mirrors the editor warning icon.
- **Per-bullet model**: entry `i` drives bullet `i`; per-bullet (valid) >
  shared (valid) > default; explicit all-zero = intent. Curves: clear
  FREEZES the last sample (pinned).
- **Preview**: geometry snapshotted UNSPUN in holder space; spin/move are
  the layer NODE transform (`set_preview_pose`: Hb^-1 R Hb, + muzzle
  offset), so spinning or rigidly moving costs 0 rebuilds / 0 redraws.
  Dots/rings/arrow heads are one `draw_multimesh` each. Full fidelity at
  any count (no LOD).
- **Movement**: the spawner node travels a Path2D at runtime only;
  `advance_movement` (bounded 64-leg catch-up) → `apply_movement_pose`
  (`Curve2D::sample_baked_with_rotation`). Easing = `Easing2D::ease`
  (parity-tested vs `Tween.interpolate_value`) or a progress Curve.

## 13. Pattern bake cache (spawner)

- `resolve_raw_pattern` generates raw transforms (pre spin/scale/skip) once
  per `pattern_version` and re-poses them per shot (one 2x3 multiply per
  bullet). The motion class is MEASURED with probe markers
  (`classify_pattern_motion`): RIGID (follows any rigid generator move),
  TRANSLATION (same basis only, e.g. world-direction rain), NONE.
  CHILDREN/AIMED/CORRIDOR/CUSTOM/PATH2D read outside state → always
  regenerate; unseeded random re-rolls → fails the probe → regenerate.
- Every geometry setter calls `on_pattern_changed()` (version bump +
  preview rebuild). NEVER call bare `rebuild_preview()` from a setter that
  changes geometry. Presets write members raw, then call it once.
- Proof: tests run with `debug_set_pattern_cache_verify(true)` (every
  cached result regenerated and compared; mismatch = error = red test), and
  `test_spawner_pattern_bake` perturbs EVERY pattern property for
  invalidation (mutation-tested). `pattern_cache_mode = Off` exists for
  debugging; moving/spinning spawners never need it.

## 14. Contracts & pitfalls catalog (pinned by tests — do not "fix" back)

- Ranges: `all_bullets_*(start, end)` — (0, -1) = whole volley, end -1 =
  through the last bullet; out-of-range or inverted ranges push
  `Invalid index range in <fn> ...` once and apply NOTHING
  (`volley/test_volley_range_contract.gd`, discovered via `get_method_list`).
- `spawn_pattern_list` entries: unknown keys fail loud with a did-you-mean;
  valid keys still apply.
- Pause/resume replays overlaps that began during the pause, exactly once
  (`volley/test_volley_pause_overlaps.gd`).
- Same-owner full-drain wake keeps linear ballistics; foreign wakes are
  neutralized (see `enable_bullet` docs).
- `spawn_position_offset_space`: Global (default, historical) vs Local
  (turns with the spawner); the preview draws it where bullets spawn.
- Edge cases to test everywhere: NaN/Inf scalars and vectors; null array
  entries; empty arrays; short vs oversized arrays; OOB indices (-1/99);
  inverted ranges; zero amounts/sizes/speeds; singular transforms; freed
  factory/generator/target/path mid-flight; pool reuse across lives;
  pause/resume with overlaps in flight; 10k cap (10000 ok, 10001
  rejected); maxed queues/timers (64); same-frame expiry+respawn; deferred
  calls from collision handlers; teleport-into-wall; coincident
  aim/target; zero-radius orbit; negative speeds under curves.
- Godot facts that bit us: GDExtension virtuals (`_get_configuration_warnings`)
  are not script-callable (expose a public twin); Godot imports `.csv` files
  inside the project as translations (keep logs under a `.gdignore` folder);
  a warning printed per spawn retains objects (use `WarnOnce2D`);
  `--quit-after` guards headless scripts against hangs.

## 15. Benchmarks & profiling (measure before AND after any perf change)

```sh
python3 tools/run_benchmarks.py                     # 16 headless scenarios x5 (median)
python3 tools/run_benchmarks.py --scenario spawner_ # substring filter (repeatable)
python3 tools/run_benchmarks.py --gate              # exit 1 on regression vs log/baseline.json
python3 tools/run_benchmarks.py --update-baseline   # ONLY for an accepted change; say so in the commit
```

- Read `test_project/benchmarks/log/LATEST.md` (this run vs baseline).
  Regression = p50 > +10% AND > +0.05 ms, or p95 > +20% AND > +0.3 ms
  (p99/max are reported, not gated: they are bimodal on a desktop).
  `log/history.csv` = append-only trend, `log/results/*.json` = raw runs.
  Compare only same machine + same build type.
- A/B a change: run the scenarios on your build, `git stash push -u -- src/`,
  rebuild, run again, `git stash pop`, rebuild. Differences under ~5% p50
  are noise on this machine.
- Scenarios: `test_project/benchmarks/scenarios/*.gd` (extend
  `BlastBenchmark`: `setup()`, `step(frame)`, `extra`). Columns: frame =
  step (scenario's own plugin calls) + engine (physics + factory tick +
  render); tick = factory physics tick only.
- In-engine: `BulletFactory2D.get_frame_stats()`, `get_active_bullet_count()`,
  and the `BlastBullets2D/*` custom monitors (editor Debugger → Monitors,
  `register_performance_monitors`). Editor Profiler for script cost,
  Visual Profiler for GPU.
- Facts (debug build, Ryzen 7 8840HS): 10k directional bullets in flight
  ≈ 0.26 ms factory tick. Cold spawn was O(N²) (8k: 1.5 s) because every
  per-bullet `shape_set_data` re-updated all shapes of the area; one
  shared shape per volley made it O(N) (8k: 11 ms, 1k: 0.66 ms).
  Spawner preview spin/move: 0 rebuilds, p99 ~50 ms → 3–5 ms.

## 16. Rules (non-negotiable)

1. Failing-first: reproduce with a test that fails (or mutation-check an
   existing fix), then fix, then keep it.
2. Evidence before synthesis: read the code, cite `file:line`; never
   assert behavior you haven't executed. Re-read code before documenting it.
3. Never weaken a test to fit the code — fix the code, or bring the
   contract question to the user with engine-code references.
4. Tick code is sacred: no per-bullet extension-boundary crossings (hoist
   sin/cos, inverses, StringNames via `CachedStringNames2D`), O(1)
   amortized per bullet, no per-bullet allocation per tick (reuse scratch,
   `reserve()`), batched buffer uploads. New per-tick work needs a
   benchmark delta.
5. Don't modify SConstruct; build only through `tools/*.py` / `setup.py`.
   Test only inside `test_project/` (no temp projects).
6. Full suite green (leaks on) + `--self-test` before finishing. End
   reports with Big-O + a ratings table.
