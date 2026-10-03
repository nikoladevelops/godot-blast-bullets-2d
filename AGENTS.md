# AGENTS.md — BlastBullets2D agentic workflow

Godot 4 GDExtension (C++ via godot-cpp) + GUT headless suites. Stability and
performance are Priority 1; every behavior change must be verified by an
executed test. Read §0 first, then the section you need before touching
`src/` or tests. Every fact below was verified by running it; if you find
one that is wrong, fix this file in the same change.

## 0. Quick start + decision table (read this even if you read nothing else)

```sh
GODOTPP_NONINTERACTIVE=1 python3 tools/compile_debug_build.py   # build (never raw scons, never edit SConstruct)
python3 tools/run_tests.py                                       # ~115 files / ~725 tests, ~10 s, leak-checked
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
| Runner prints only part of a failure | Re-run with `--full` (every failing parameter, unclipped) | Guess from the first line |
| Runner says "src/ is newer than the compiled extension" | Rebuild (§0c step 2); comment-only edits count too | `--allow-stale` |
| Unsure about engine behavior | Probe it: write `test_project/tests/spawner/test_zz_probe.gd` (extends BlastTest, put the values in a failing `assert_eq(..., "", "PROBE")`), run `godot --headless --fixed-fps 60 --path test_project -s addons/gut/gut_cmdln.gd -gconfig= -gtest=res://tests/spawner/test_zz_probe.gd -gexit -gdisable_colors`, read the message, then DELETE the file and its `.uid` | Guess from memory of Godot docs; leave the probe behind |
| A pattern draws fewer bullets or two bullets on one spot | Extend `spawner/test_spawner_pattern_counts.gd` first, then fix the generator (§6b) | Accept "it looks fine" |

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

## 0c. Change recipe (do these steps in order, every time)

1. Read the code you will touch (`rg -n` the method name; files in §12).
2. Write or extend the test FIRST; build and run it; watch it FAIL for the
   right reason:
   ```sh
   GODOTPP_NONINTERACTIVE=1 python3 tools/compile_debug_build.py
   python3 tools/run_tests.py --suite <your_test_file_name> --full
   ```
3. Fix the code. Rebuild. Run the same suite until it passes.
4. Run everything, then the harness self-check:
   ```sh
   python3 tools/run_tests.py
   python3 tools/run_tests.py --self-test
   ```
5. Hot path touched (tick, spawn, pattern generation, preview)? Benchmark
   (§15) and compare with `test_project/benchmarks/log/LATEST.md`.
6. Public API changed? Update `doc_classes/` (§10) and add the new suite to
   `test_project/tests/README.md` (the lint fails otherwise).
7. Commit only if asked, following §0b.

## 0d. Glossary

- **volley**: one spawn call = one `BulletVolley2D` (one MultiMesh + one physics
  area holding N bullets). Its spawn data is one `BulletVolleyData2D`.
- **marker / generator**: the Node2D a pattern is built around
  (`transforms_generator`, else the spawner itself).
- **pattern source**: which generator builds the per-bullet transforms
  (`pattern_source`, 33 ids, serialized).
- **bake**: the spawner's cached raw pattern (§13), re-posed per shot.
- **chain**: a burst (N shots apart) or a telegraph (warning, then shot).
- **leg**: one traversal of a movement Path2D.
- **pool**: parked volleys reused by `VolleyPoolKey2D` (bullet count + collision shape).

## 0e. Never edit

- `SConstruct` (use `tools/*.py` / `setup.py`).
- `test_project/addons/gut/` (vendored GUT).
- Personal paths in `tools/config.json`.
- Scenes the user edits by hand (e.g. `test_project/benchmark_scene/*.tscn`)
  unless asked.
- Generated `doc_classes/*.xml` structure (only description text; §10).

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
python3 tools/run_tests.py --full            # print every failure message unclipped (all failing parameters)
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
rg -n "push_error|emit_signal" src/bullet_volley/  # fail-loud sites (pin exact text in tests)
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

- Builders: `quick_volley()`, `make_spawner()` (shooting + homing OFF before it
  enters the tree), `make_preview_spawner()`, `H` (`blast_test_helpers.gd`:
  `make_volley_data`, `make_still_data`, `make_speed`, `make_rotation`,
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

## 6b. How to add or fix a pattern (BulletSpawner2D)

Files: generator in `src/factory/bullet_factory2d_patterns_<family>.cpp`
(`helper_generate_transforms_<shape>`; families: `shapes`, `curves`,
`polygons`, `edges`), shared layout machinery in
`bullet_factory2d_patterns_layout.cpp` (declared in
`bullet_factory2d_patterns_internal.hpp`), its binding in
`bullet_factory2d_patterns_bindings.cpp`, preview track in
`bullet_factory2d_patterns_preview.cpp` (`helper_sample_outline_<shape>`).
Spawner side: dispatch in `src/bullet_spawner/bullet_spawner2d_patterns.cpp`
(`generate_raw_pattern`), properties in
`bullet_spawner2d_pattern_properties.cpp`, bindings and inspector gating in
`bullet_spawner2d_bindings.cpp`.

Invariants every generator must keep (pinned by
`spawner/test_spawner_pattern_counts.gd`, `test_spawner_pattern_bake.gd`,
`test_spawner_preview_coincidence.gd`):
1. Exactly `helper_bullets_amount` transforms (On Outline, Layers, Fill
   Inside; gaps redistribute, overflow shrinks spacing).
2. No two bullets closer than 0.5 px (no hidden duplicates). Closed curves:
   sweep the curve exactly once (watch retraces: odd roses, shared-factor
   Lissajous, multi-revolution spirographs) and use
   `resample_loop_even_distinct` for self-crossing curves.
3. Pure function of its inputs (the bake cache relies on it; random
   patterns take a seed).
4. The preview track (`helper_sample_outline_*`) draws the same curve the
   bullets sit on.
5. Every bad input fails loud once with exact wording.

Spawner test template (copy, rename, list in `tests/README.md`):

```gdscript
extends BlastTest
## <One paragraph: the contract this file pins.>


func test_<behavior>_<expectation>() -> void:
	var sp := make_spawner(H.make_volley_data(4, 0.0, 30.0), BulletSpawner2D.PATTERN_FROM_HELPER_RING, 4)
	watch_signals(sp)
	sp.helper_ring_radius = 80.0
	var pts: Array = sp.collect_spawn_transforms()
	assert_eq(pts.size(), 4, "exact count")
	sp.helper_ring_radius = NAN
	expect_error_sequence(["helper_ring_radius must be finite"]) # exact text, right after the call
	assert_true(sp.shoot_once(), "fires")
	for i in 30: # early-break wait, never a fixed long wait
		await idle(1)
		if get_signal_emit_count(sp, "volley_fired") > 0:
			break
	assert_signal_emit_count(sp, "volley_fired", 1, "exactly once")
```

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
  Performance. Spawn data (`BulletVolleyData2D`, one inspector category):
  Bullets, Appearance, Movement Speed, Bullet Rotation (shared + per-bullet
  + tile + stop flag together), Wobble, Gravity, Bounce and Ricochet,
  Movement Pattern Paths, Homing, Collision, Attachments, Sprite Effects,
  Rendering and Material. No duplicate group titles and no ungrouped
  property (`volley/test_volley_data_inspector.gd`).
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
  register_types.cpp                      class registration (ClassDB)
  bullet_volley/                          BulletVolley2D: one volley = N bullets, one MultiMesh, one physics area
    bullet_volley2d.hpp                   the class: fields grouped per feature + declarations (+ file map)
    bullet_volley2d_internal.hpp          inline helpers shared by several volley TUs (volley .cpp files only)
    bullet_volley2d.cpp                   lifecycle: spawn, enable_volley (pool reuse), enable/disable/clear bullet, teardown
    bullet_volley2d_tick.cpp              move_bullets: THE per-bullet loop + its inline helpers (hot path)
    bullet_volley2d_render.cpp            physics interpolation pass
    bullet_volley2d_setup.cpp             MultiMesh/buffer setup
    bullet_volley2d_collision.cpp         area + ONE shared shape, intake, dedup, paused-overlap park/replay, drain, counts
    bullet_volley2d_{bounce,homing,orbit,motion,curves,wobble,gravity,lifetime,timers,attachments,effects,
                     animation,teleport,debug,bindings}.cpp   one feature each
    homing_target_deque.hpp, bullet_movement_pattern_data2d.hpp   volley-only data structures
  factory/                                BulletFactory2D
    bullet_factory2d.cpp                  lifecycle, containers/debugger, interpolation, tick/render sweeps, teleport
    bullet_factory2d_spawn.cpp            request validation, spawn_volley(+_span), pool pre-population
    bullet_factory2d_structural.cpp       reset/free_*/clear_*, *_deferred queue, volley bookkeeping (vec/set/pool sync)
    bullet_factory2d_effects.cpp          factory-owned one-shot effect bakes
    bullet_factory2d_stats.cpp            frame stats, monitors, debugger knobs, debug_*
    bullet_factory2d_bindings.cpp         _bind_methods (calls bind_pattern_helpers)
    bullet_factory2d_patterns_*.cpp       pattern generators per family + layout engine + preview tracks + inspectors
    bullet_factory2d_internal.hpp, bullet_factory2d_patterns_internal.hpp, factory_operation_guard2d.hpp
  bullet_spawner/bullet_spawner2d.cpp     wiring, shooting cadence, spin, bursts/telegraph, pattern lists, lifecycle, shoot_once
  bullet_spawner/bullet_spawner2d_pattern_properties.cpp  Bullet Patterns accessors + apply_pattern_preset
  bullet_spawner/bullet_spawner2d_patterns.cpp   raw generation, bake cache, native span collect, verifier
  bullet_spawner/bullet_spawner2d_homing.cpp     homing/orbiting, target resolution, live-volley steering
  bullet_spawner/bullet_spawner2d_preview.cpp    preview snapshot/rebuild, pose, dirty checks, debug_* geometry
  bullet_spawner/bullet_spawner2d_preview_layer.cpp  PatternPreviewLayer2D (draw_multimesh canvas layer)
  bullet_spawner/bullet_spawner2d_movement.cpp   Path2D movement
  bullet_spawner/bullet_spawner2d_bindings.cpp   _bind_methods (groups/subgroups) + _validate_property
  bullet_spawner/bullet_spawner2d_internal.hpp   statics shared by >1 spawner TU (limits, pattern-source table)
  data/        inspector Resources: BulletVolleyData2D, BulletSpeed/Rotation/Curves/Wobble/EffectLayerData2D
  pooling/     VolleyPool (parked volleys per key), VolleyPoolKey2D
  attachments/ BulletAttachment2D + its object pool
  debugger/    BulletVolleyDebugger2D (collision-shape overlay)
  core/        header-only utilities: warn_once2d, cached_string_names2d, easing2d (Tween port),
               dynamic_sparse_set, collision_shape_helper2d, reentrancy_guard2d
```

- Same class across the split files (pure moves). Put new code in the file
  of its concern; keep per-bullet loops inside ONE translation unit (no
  cross-TU call per bullet). Volley helpers marked `_ALWAYS_INLINE_` are
  defined in the .cpp that uses them or in `bullet_volley2d_internal.hpp`:
  calling one from another file links fine but fails at extension LOAD time
  (undefined symbol: the runner reports CRASH) - move the definition to the
  internal header instead. Includes are root-relative (`"data/..."`).
- Threading: everything runs on the main thread (physics callbacks
  included); no locks exist and none are needed. Do not add threads.
- **Spawn flow**: `BulletFactory2D.spawn_volley` → `spawn_volley_internal`
  → pool pop by `VolleyPoolKey2D` (bullet count + shape) or fresh alloc →
  `enable_volley` (validates EVERYTHING before mutating; a refused
  enable changes nothing) → `set_up_bullet_instances` (one `set_buffer`
  upload). The spawner calls the C++ span path
  (`spawn_volley_span`, no Variant boxing).
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
- Spawner contracts (v4.1), each pinned by the named suite:

| Behavior | Suite |
|---|---|
| Every helper draws exactly `helper_bullets_amount` bullets at distinct spots; flower FAN splits the amount over petals | `test_spawner_pattern_counts` |
| Presets are clean (pattern knobs + spin reset; Transform subgroup and node wiring kept) | `test_spawner_presets` |
| Pattern-list entries are temporary overrides (everything restored); `pattern_list_finished` in both modes | `test_spawner_pattern_lists` |
| Auto bursts clamp to `volleys_remaining`, cancel when auto-fire turns off (`burst_finished` still fires); mirror starts plain | `test_spawner_burst_telegraph` |
| Controls called before the tree survive `_ready`; a spent `fire_n_volleys` budget clears itself; `shooting_*` signals are auto-fire only | `test_spawner_cadence` |
| Every failed shot emits `volley_skipped(reason)` (`no_factory`, `no_spawn_data`, `no_transforms`, `over_budget`, `outside_fire_arc`, `factory_refused`, `dropped`) | `test_spawner_cadence` |
| Reparenting keeps assigned nodes, tracked volleys, chains and lists; NodePaths are rewritten on re-entry | `test_spawner_tree_reentry` |
| Setters reject NaN/Inf/out-of-range with "keeping the old value"; no setter depends on another field (load order) | `test_spawner_setter_contract` |
| Fire arc follows spin and the volley chases the targets the arc approved | `test_spawner_homing_propagation` |
| One-time warnings use `WarnOnce2D` codes 101+ (spawner) and stay quiet in the preview | `test_spawner_setter_contract` |
| Homing sources never pick the spawner, its markers, factory nodes, dying nodes or non-Node2Ds; an empty resolution fires a plain volley and warns once per homing configuration | `test_spawner_homing_detection` |
| Homing queues cap at 256 without errors; freed targets are trimmed; retarget skips dead/pooled/foreign/old-factory volleys and disabled bullets | `test_spawner_homing_queues` |
| Public surface after the volley refactor: one `spawn_volley`, no `BulletType`, factory and spawner share signal names/payloads, exact stats keys, one container + one debugger | `test_factory_api_surface` |
| A pooled volley reused for plain data matches a cold volley field by field, pose and flight (every feature reset) | `test_volley_pool_reuse_all_features` |
| A one-bullet volley behaves like bullet 0 of any volley (per-bullet curves beat shared) | `test_volley_single_bullet` |
| Every spawn-data and volley property sits in a group; names and group titles unique | `test_volley_data_inspector` |

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
python3 tools/run_benchmarks.py                     # 15 headless scenarios x5 (median)
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
- Facts (debug build, Ryzen 7 8840HS): 10k bullets in flight
  ≈ 0.27 ms factory tick (`volley_10k_flight`). Cold spawn was O(N²) (8k: 1.5 s) because every
  per-bullet `shape_set_data` re-updated all shapes of the area; one
  shared shape per volley made it O(N) (8k: 11 ms, 1k: 0.66 ms).
  Spawner preview spin/move: 0 rebuilds, p99 ~50 ms → 3–5 ms.
  Inlining `move_bullets` into the factory loop cost 20-25% on
  `trails_fx_2k` (it lives in its own TU now); a `Ref<>` returned by value
  per bullet costs a reference()/unreference() engine call pair (trail and
  effect shards hand out raw `MultiMesh *`, ~9% p50 / ~18% p95 on trails).

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
