# AGENTS.md — BlastBullets2D: how to work on this codebase

Godot 4 GDExtension (C++ via godot-cpp) + headless GUT test suites. Stability
and performance come first; every behavior change is proven by an executed
test, every refactor by an unchanged behavior snapshot. Every fact below was
verified by running it; if one turns out wrong, fix this file in the same
change.

**If you read nothing else, read §0, §1 and §2.** Use the table of contents
for the rest: the sections are written to be looked up, not read once.

| § | Topic | § | Topic |
|---|---|---|---|
| 0 | Golden rules | 10 | Pattern module (BulletPatterns2D, spawner patterns) |
| 1 | Commands | 11 | Volley (BulletVolley2D) architecture |
| 2 | The work loop (do it like this) | 12 | Factory, spawner, signals, pooling |
| 3 | Decision table | 13 | Pattern bake cache |
| 4 | Playbooks | 14 | Contracts and pitfalls catalog |
| 5 | Commits | 15 | Benchmarks and performance lessons |
| 6 | Never edit | 16 | Inspector groups and serialization |
| 7 | Test harness | 17 | Documentation generation |
| 8 | Writing tests | 18 | tools/ catalog |
| 9 | Searching and reading | 19 | Final report template |
|  |  | 20 | Graze (zones, tick stage, routing, preview) |
|  |  | 21 | Sound (mixer, triggers, listeners, RNG) |

## 0. Golden rules

1. **Never guess.** Read the code (`rg -n`), probe the engine, run the test.
   Cite `file:line` for every claim you make.
2. **Failing test first.** A bug fix starts with a test that fails for the
   right reason; a fix you never saw red is not proven.
3. **Never weaken a test** to make it pass. Fix the code, or ask the user
   (with engine-code references) when the contract itself is in question.
4. **A refactor changes nothing observable.** Prove it with
   `tools/api_snapshot.py diff` (IDENTICAL) before you commit.
5. **Hot paths are sacred** (§15): no per-bullet allocation, no per-bullet
   engine boundary crossing, benchmark before AND after.
6. **Build only with `tools/compile_debug_build.py`**; check its exit code
   before you test (a failed build leaves the old library).
7. **Small steps:** one concern per change, build + test + snapshot after
   each, commit each green step (§5).
8. **Error and warning texts are public API.** Keep them byte-identical
   unless the change is the point (then pin the new text in a test).
9. **Ask the user** before changing a contract (public behavior, wording,
   defaults); fix plain bugs without asking but list them in the report.
10. **Finish green:** full suite + `--self-test` + format check + snapshot
    diff + benchmark gate (+ `run_editor_smoke.py` when editor/preview code
    changed), then report with Big-O and a ratings table (§19).

## 1. Commands

```sh
GODOTPP_NONINTERACTIVE=1 python3 tools/compile_debug_build.py   # build (exit 1 = failed; never raw scons, never edit SConstruct)
python3 tools/run_tests.py                                       # 171 files / 1254 tests, ~10 s, leak-checked
python3 tools/run_tests.py --suite <substring> --full            # one area, every failure message unclipped
python3 tools/run_tests.py --self-test                           # proves the harness still catches failures/leaks
python3 tools/run_editor_smoke.py                                # headless EDITOR run of tests/editor_smoke/ (editor-only paths)
python3 tools/api_snapshot.py save <label>                       # behavior snapshot (~10 s, §7.4)
python3 tools/api_snapshot.py diff <label_a> <label_b>           # must print IDENTICAL for a refactor
python3 tools/format_code.py [--check] [files or dirs]           # clang-format (repo .clang-format)
python3 tools/run_benchmarks.py [--scenario <name>] [--gate]     # performance (§15)
GODOTPP_NONINTERACTIVE=1 python3 tools/generate_xml_docs.py      # regenerate doc_classes (§17)
```

## 2. The work loop (this is how the expert works)

Follow these steps in order for EVERY change. Expected output is shown so you
know what "done" looks like.

1. **Understand before touching.** Find the code: `rg -n "name" src/`.
   Read the whole function and its callers (`Read` with offset/limit for big
   files: never assume what a function does from its name). Find the tests
   that pin it: `rg -n "name" test_project/tests`. Note the error texts.
2. **Probe what you are unsure about** (engine behavior, current output):
   write `test_project/tests/spawner/test_zz_probe.gd` (extends BlastTest,
   print values through a failing `assert_eq(str(values), "", "PROBE")`),
   run it with `python3 tools/run_tests.py --suite test_zz_probe --full
   --no-lint` (lint R6 refuses an unlisted suite), read the FAIL line
   (`["<your values>"] expected to equal [""]: PROBE`), then DELETE the file
   and its `.uid`.
3. **Take a snapshot** when you will change C++: build, then
   `python3 tools/api_snapshot.py save before`.
4. **Bug fix: write the test first.** Add it to the suite of that area (or a
   new suite: §8.4), run it, and confirm it FAILS for the reason you
   expect (the assert message, not a script error).
5. **Change the code** in the smallest steps you can. After each step:
   ```sh
   GODOTPP_NONINTERACTIVE=1 python3 tools/compile_debug_build.py   # must print "Compilation finished successfully"
   python3 tools/run_tests.py --suite <area>                        # "ALL n TEST FILES PASSED (... no leaks)"
   python3 tools/api_snapshot.py save after && python3 tools/api_snapshot.py diff before after
   ```
   A pure refactor must print `IDENTICAL`. A bug fix shows ONLY the entries
   you meant to change: read each one; anything else is a regression.
6. **Format** what you touched: `python3 tools/format_code.py src/<dir>`.
7. **Hot path touched?** Benchmark before and after (§15) on the same
   machine and build; differences under ~5% p50 are noise.
8. **Full verification:** `python3 tools/run_tests.py`,
   `python3 tools/run_tests.py --self-test`, `python3 tools/format_code.py --check`.
9. **Public API or docs changed?** Update `doc_classes/` (§17) and list new
   suites in `test_project/tests/README.md` (the lint fails otherwise).
10. **Commit** the green step (§5), then continue with the next step.

Habits that matter:
- Before changing any message text, `rg` the tests for SEVERAL fragments
  of it (prefix and suffix): `expect_errors_containing` pins substrings, so
  searching one part misses the pin. Run the FULL suite before every commit,
  not only the area suite (a reworded curves error once passed its area
  suite and broke `volley/test_volley_api_coverage.gd`).
- Scripted multi-file edits: assert every `old` text occurs exactly as many
  times as you expect BEFORE writing (`str.replace("", x)` inserts x between
  every character: an empty match once exploded `pattern_generate2d.cpp`). Re-read the diff (`git diff --stat`, `git diff`) after.
- Mechanical rewrites (regex over many functions) are fine when the
  compiler and the snapshot check them; fix the few compile errors by hand.
- When a benchmark regresses, diagnose before reverting: the cause is often
  a per-bullet store/allocation or a mutable context the compiler must
  reload (§15.3).
- Keep the user's hand-edited files out of your commits
  (`test_project/benchmark_scene/benchmark.tscn`).

## 3. Decision table

| Situation | Do this | Never do this |
|---|---|---|
| A test fails after your change | Read the FULL failure (`--full`); reproduce with `--suite <file>`; fix the code | Edit the assertion to match new behavior without asking (§0.3) |
| Unexpected `push_error` fails a test | Find the emitter: `rg -n "<text>" src/`; decide whether the call (test input) or the code is wrong | `swallow_errors()` (lint rejects it outside the fuzz allowlist) |
| `CRASH` / missing JUnit report | Stale or broken `.so`: rebuild, rerun; read the log for `SCRIPT ERROR` / `class_db.cpp` | Assume it passed |
| Runner says "src/ changed after the last successful build" | The last build FAILED (or never ran): fix the compile error, rebuild | `--allow-stale` |
| `LEAK` verdict | Something survives the test: `add()`/autofree nodes, free RIDs, disconnect | `--no-leaks` and call it green |
| Snapshot diff shows entries you did not intend | It is a behavior change: find which step caused it (re-snapshot per step) | Commit and hope |
| Snapshot diff is IDENTICAL but a test failed | Tests are stricter in places (signals, timing): the test wins | Ignore the test |
| New property invisible in the inspector | ADD_PROPERTY before its bind, wrong subgroup prefix, or `_validate_property` gate (§16) | Hide the visibility test failure |
| Need to know if your test is real | Mutation-check it (§8.3): break the code, watch it fail, restore | Trust a test you never saw fail |
| Perf change | Benchmark before AND after on the same build (§15) | Claim a speedup from one run |
| Unsure about engine behavior | Probe it (§2.2) | Guess from memory of Godot docs |
| A pattern draws fewer bullets or stacks two | Extend `spawner/test_spawner_pattern_counts.gd`, then fix the generator (§10) | "It looks fine" |
| Adding a pattern knob | Field in `PatternKnobs2D` + one `PATTERN_KNOB` row (§10.3) | Hand-written accessor, declaration and bind |
| Adding a generator argument | One row in `pattern_signatures2d.inc` + the Params struct field (§10.2) | Editing the header, the wrapper and the bind by hand |
| A handler needs reset / free_* / populate_* / a shape change | Call the `*_deferred` twin (§12.3) | `call_deferred` from physics (still inside the physics frame) |
| "Homing bullets sometimes miss" | Check inherited momentum vs speed (`inherit_movement_velocity`), turn rate (`homing_smoothing`) vs hitbox | Assume a pattern bug |
| A warning must be pinned | `expect_warning_sequence([...])` | `expect_error_sequence` (it ignores warnings) |

## 4. Playbooks

### 4.1 Fix a bug
1. Reproduce: smallest script or test that shows it (§2.2 probe).
2. Find the cause in code; cite `file:line`. Look for siblings: the same
   mistake is often copied (e.g. velocity composed without the fall speed
   appeared in 8 setters).
3. Red test in the area's suite (assert the documented behavior, exact
   texts). Run: it fails for the right reason.
4. Minimal fix; rebuild; the test passes; the full suite passes.
5. Snapshot diff: only the intended entries changed.
6. Commit: `Fix <thing>` + one line of why.

### 4.2 Pure refactor (behavior must not change)
1. `save before` on the current build.
2. Change one thing (e.g. extract a helper); keep every expression, cast and
   operation order: float results must stay bit-identical (`a/2.0f` and
   `a*0.5f` are equal; `0.0` vs `-0.0` after `x*0` is NOT; `Vector2(r(), r())`
   has unspecified evaluation order; `Transform2D(rot, pos)` + `set_scale`
   differs from the 4-argument constructor).
3. Build, `save after`, `diff before after` → `IDENTICAL`; run the area's
   suites; benchmark if the hot path moved.
4. The snapshot only covers what it exercises: if your refactor touches a
   path it does not reach (check by breaking it on purpose), add a snapshot
   section first and re-baseline at the OLD code: `git checkout <old> -- src/`,
   build, `save base`, `git checkout HEAD -- src/`, build.

### 4.3 Add or change a volley feature / property
Read §11 first. Then the checklist in §8.5. Per-bullet state lives in
parallel arrays sized per volley; if your array must reset on a new life,
add it where the others of its feature reset (bounce arrays: ONE list in
`visit_bounce_ledger`; rotation arrays: `visit_rotation_trio`). Per-bullet
work in the tick goes into the matching stage of `move_bullets` (§11.2).

### 4.4 Patterns: add a knob, a generator argument or a shape
See §10. A new knob = one table row; a new generator argument = one
signature row + a Params field (append at the END: positional GDScript calls
must keep working); a new shape = registry row + Params struct + signature
entry + `generate_<shape>2d` + dispatch + track case.

### 4.5 Performance change
`run_benchmarks.py --scenario <x>` before, change, rebuild, after; repeat a
surprising result. A/B against an old commit: §15.2. Never update the
baseline unless the change is accepted (say so in the commit).

### 4.6 Documentation
Bound API change → §17. Description text only → edit `doc_classes/*.xml`
directly (BBCode, escape `<` `>`), rebuild, regenerate: the diff must be
empty apart from escaping.

### 4.7 Formatting
`python3 tools/format_code.py <files or dirs>` after editing C++.
`--check` must pass before you finish. X-macro tables (`*.inc`) are not
formatted (rows are aligned by hand). Formatting-only commits go into
`.git-blame-ignore-revs` (`git config blame.ignoreRevsFile .git-blame-ignore-revs`).

## 5. Commits (user rule — overrides any tool or harness default)

- NEVER add `Co-Authored-By` lines, and never mention Claude, Anthropic, or
  any AI model/assistant anywhere in a commit (title, body, trailer).
- Title: imperative, short and descriptive, at most 50 characters
  (`Fix flower bullet count`, `Add spawner cadence tests`).
- Body: optional, at most 3 short lines saying what changed and why. No
  essays, no test counts, no file lists.
- Commit only when the user asks (or the approved plan says so); one commit
  per green step. Push only when asked; never force-push without approval.
- Stage files explicitly; never stage unrelated user changes (e.g. the
  hand-edited `test_project/benchmark_scene/benchmark.tscn`). New test files
  come with their `.uid` (run a headless `--import` if it is missing).

```sh
git add <the files you changed>
git commit -m "Fix flower bullet count" -m "FAN splits the amount over petals."
```

## 6. Never edit

- `SConstruct` (use `tools/*.py` / `setup.py`).
- `test_project/addons/gut/` (vendored GUT).
- Personal paths in `tools/config.json`.
- Scenes the user edits by hand (`test_project/benchmark_scene/*.tscn`).
- The structure of generated `doc_classes/*.xml` (description text only; §17).

## 7. Test harness

### 7.1 Running tests (only supported path)

```sh
python3 tools/run_tests.py                  # every suite, leak-checked
python3 tools/run_tests.py --suite volley    # substring/glob filter (repeatable)
python3 tools/run_tests.py --changed-only    # files plausibly affected by uncommitted changes
python3 tools/run_tests.py --fail-fast       # stop scheduling after the first red file
python3 tools/run_tests.py --list            # discover without running
python3 tools/run_tests.py --self-test       # canaries must report FAIL + LEAK + strict mismatch
python3 tools/run_tests.py --no-leaks        # faster, NOT green (skips the leak gate)
python3 tools/run_tests.py --report          # + test_project/test_results/summary.json (slowest first)
python3 tools/run_tests.py --full            # every failure message unclipped (all failing parameters)
python3 tools/run_tests.py --realtime        # real-time pacing (default: simulated time)
python3 tools/lint_tests.py                  # static test lint (run first by the runner; exit 3 = failed)
```

- Time is SIMULATED (`--fixed-fps 60`): every frame advances exactly 1/60 s
  as fast as the CPU allows. Frame-counted tests behave like real time.
- A file is green only when ALL hold: exit 0, GUT JUnit with 0 failures
  (missing report = CRASH, never PASS), no leaks in the `--verbose` exit
  report, no `SCRIPT ERROR`, no `godot-cpp/src/core/class_db.cpp` error.
- Stale-build guard: every SUCCESSFUL build writes
  `test_project/addons/blastbullets2d/bin/.build_stamp`; tests and snapshots
  refuse to run while any `src/` file is newer than it. A failed build keeps
  the refusal (before the stamp, objects compiled before the error made a
  failed build look fresh and tests ran against the previous library).
- Never run test files with `--script`; the runner does the class-cache
  refresh, per-file processes, JUnit and leak verdicts.
- Lint rules (`tools/lint_tests.py`): R1 no `extends SceneTree`; R2 every
  suite `extends BlastTest`; R3 no GUT `wait_*_frames`; R4 `swallow_errors()`
  only in `volley/test_volley_crash_proof.gd`; R5 `at_least=true` expects
  need a `# lint: at-least <reason>` comment; R6 every suite listed in
  `test_project/tests/README.md`; R7 no stray `print(` (only `print("BENCH`).

### 7.2 Leak detection

- The runner fails files matching `ObjectDB instances leaked`,
  `Leaked instance:`, `RID ... leaked`, `resources still in use`,
  `Orphan StringName`.
- `BlastTest.after_each` asserts `debug_assert_no_dangling()` and zero new
  orphans after EVERY test: a leak fails at the test, not at exit.
- Deliberate leakers/failers live in `test_project/tests_meta/` and run only
  via `--self-test` (they must be reported, never green).
- `integration/test_steady_state_memory.gd` pins the live Object count over
  repeated warm shots (retention leaks the exit report never sees).

### 7.3 Harness facts

- From an idle point `await idle(k)` runs exactly k factory ticks;
  `await physics(n)` resumes INSIDE frame n before the factory ticked
  (n-1 ticks). `factory.debug_advance_time(delta)` runs one tick with any
  delta (zero included) from an idle point; with `--fixed-fps`,
  `Engine.time_scale` does not change the delta.
- The runner prints only the first failing assert per test and GUT clips
  array diffs: join problems into one string to see them all.
- A `Packed*Array` read from an Array is a copy (write it back).
- `push_warning` lands in `get_errors()` (`err.is_push_warning()`); pin it
  with `expect_warning_sequence`.
- `obj.set("misspelled", v)` silently does nothing: assert `k in obj` and
  read the value back when a test sets properties by name.
- NEVER `free()` nodes you `watch_signals()` on: freeing several watched
  nodes hangs GUT at the end of the test (plain Node2Ds too; the runner's
  per-file timeout then reports a CRASH). Let autofree take them.
- The mouse is drivable headless: `mouse_to(pos)` (BlastTest) feeds motion
  events so `factory.get_global_mouse_position()` reads `pos` (the window's
  stretch transform is measured, never assumed; a Camera2D is honored).

### 7.4 The behavior snapshot (`tools/api_snapshot.py`)

Runs `test_project/snapshot/api_snapshot.tscn` headless and stores what the
plugin does as short hashes plus the exact error/warning texts each call
produced (JSON in `test_project/test_results/snapshots/`, gitignored).
Sections:
- `surface`: ClassDB surface of every plugin class (methods, argument names
  and types, default values with their Variant type, properties with
  hints/usage in order, signals, enum constants).
- `patterns`: every static BulletPatterns2D method over defaults, amounts
  (-1, 0, 1, 2, 7, 24, 10001), markers, one-argument perturbations and seeded
  combinations.
- `generate`: `BulletPatterns2D.generate` for every shape and every knob.
- `layouts`: 48 multi-ring + 8 fill-inside seeded outline combinations per
  outline shape.
- `spawner`: every pattern source x every inspector-visible knob (spawn
  transforms + preview dots/track/rings) and every preset.
- `setters`: hostile setter probes (NaN, INF, negatives, huge) for spawner,
  volley and data resources.
- `traces`: deterministic volley flights (`debug_advance_time`) for every
  motion feature.
- `volley_api`: every bound BulletVolley2D method x 3 argument sets on a
  plain and a featured volley: return value, errors, full per-bullet state
  before and after a tick.

Unseeded randomness is detected (each call runs twice) and recorded by size
only. `save` refuses a stale build. `diff a b` exits 1 on any difference and
prints the first entries per section (`--limit N`).

## 8. Writing tests

### 8.1 Doctrine (`test_project/tests/common/blast_test.gd`)

- Builders: `quick_volley()`, `make_spawner()` (shooting + homing OFF
  before it enters the tree), `make_preview_spawner()`, graze:
  `make_graze_target(pos, group)`, `graze_volley(transforms, speed)`,
  `step_factory(n, delta)`, `H.make_graze_zone(radii, group)`,
  `H.record_graze(emitter)` + `H.graze_kinds(log)`, `mouse_to(pos)`, `H`
  (`blast_test_helpers.gd`: `make_volley_data`, `make_still_data`,
  `make_speed`, `make_rotation`, `make_effect_layer`, `make_flat_curve`,
  ...), `make_wall/make_area/make_probe_scene/add` (autofreed).
- Frames: `await idle(n)` (idle frame: structural factory calls allowed) vs
  `await physics(n)` (INSIDE the physics step: structural calls rejected;
  call `idle()` first). NEVER GUT's `wait_*_frames` (resumes after n+1
  frames, skews every count).
- Pin errors RIGHT AFTER the hostile call: `expect_error_sequence(["exact
  text", ...])` (exact count + order + wording; preferred),
  `expect_error(text)`, `expect_errors_containing(text, N)` (EXACTLY N),
  `expect_no_errors()` (mid-test checkpoint), `expect_warning_sequence`
  (same contract for warnings). End-of-test lumps hide bugs.
- `Array(...)`-wrap engine arrays before `assert_eq` against literals.
- Headless rendering: the dummy RenderingServer stores multimesh buffers but
  `get_instance_transform_2d()` returns identity — decode `multimesh.buffer`
  (`volley/test_volley_render_buffers.gd`).

### 8.2 Checklist for a good test

- Name: `test_<behavior>_<expectation>`.
- One behavior per test; shared builders over local setup; no magic numbers
  without a comment (frame budgets, tolerances, layer values).
- Determinism: no wall-clock sleeps; frame counts with margins (early-break
  loops `for i in N: await physics(); if cond: break`); `seed()` any
  randomness you assert on.
- Each `push_error`/`push_warning` gets an expect with the EXACT text.
- New API surface: `has_method` bind check + happy path + reject-and-keep +
  OOB + pool-reuse neutrality (spawn → pool → respawn, no state leaks).
- Prefer GENERIC tests that walk `get_property_list()` / `get_method_list()`
  / `ClassDB.class_get_method_list()` so future additions are covered
  automatically (range contract sweep, bake invalidation sweep, subgroup
  lock, visibility sweep, every-generator wording sweep).
- Invariants beat examples: "the next tick moves the bullet by exactly
  (reported velocity + g dt) dt" catches every composition bug at once.

### 8.3 Mutation check (prove a test is real)

Copy the source file to a scratch location, break the fix on purpose (old
wording back, `if (false && ...)`), rebuild, run the suite: it must FAIL on
exactly the tests you expect. Restore the copy, rebuild, run again (green).

### 8.4 New suite template (copy, rename, list in `tests/README.md`)

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

### 8.5 New-feature checklist (do all of these)

1. Bind check (`has_method`) + happy-path test.
2. Setter reject-and-keep: NaN/Inf/OOB/negative/inverted, old value kept,
   exact error text pinned.
3. Inspector: right group/subgroup (§16), per-mode visibility, gating
   setters call `notify_property_list_changed()`, hint strings byte-exact,
   serialized enums locked.
4. Pattern geometry? The setter calls `on_pattern_changed()` (§13).
5. Pool-reuse neutrality + no-dangling (automatic via `after_each`, but add
   explicit state assertions).
6. Signals: emission counts incl. negative cases; liveness check after
   every emit (§12.4).
7. `doc_classes/<Class>.xml` (§17), `tests/README.md` entry (lint R6).
8. Hot path touched? Benchmark before/after (§15).

### 8.6 GUT reference

- Discovery: `.gutconfig.json` points at `res://tests/`; files named
  `test_*.gd`, extending `BlastTest`. Each file runs in its OWN Godot process.
- Lifecycle: `before_all` / `before_each` / `after_each` / `after_all`.
  `BlastTest.before_all` turns the spawner cache verifier ON (§13);
  `before_each` builds a fresh `factory`; override with `await super()` first.
- Asserts: `assert_eq/ne`, `assert_almost_eq/ne`, `assert_gt/gte/lt/lte`,
  `assert_true/false`, `assert_between`, `assert_has`, `assert_has_method`,
  `assert_null/not_null`, `assert_freed/not_freed`, `assert_no_new_orphans`,
  `assert_string_contains`, `assert_connected`, `assert_has_signal`,
  `assert_is/typeof`, `assert_eq_deep`. Prefer the typed assert matching the
  comparison (failure output shows got/expected).
- Signals: `watch_signals(obj)`, `assert_signal_emit_count(obj, name, n)`,
  `assert_signal_emitted_with_parameters`, `get_signal_emit_count`,
  `get_signal_parameters`. Use COUNTS for exactly-once contracts.
- Parameters: `func test_x(i: int = use_parameters([...]))` runs once per
  value with full before/after_each isolation; this GUT version reports them
  under one `<parameterized>` case.
- Doubles (`double`/`spy`/`stub`): available, unused so far.
- Strict errors: any `push_error`/engine error not claimed by an `expect_*`
  fails the test.

### 8.7 Integration tests, scenes and feature mixing

- Harness pattern: `make_spawner(data, source, n)` + `make_wall(pos)` /
  `make_area` on known layers + `watch_signals` on factory/spawner/volley.
  `make_wall` defaults to layer value 4 (matches `H.make_*_data` masks); the
  bounce suite uses its own convention (bounce wall 8, plain 16).
- A spawner added to the tree auto-fires: use `make_spawner()` or
  `set_shooting_enabled(false)` BEFORE `add()`.
- Scene tests: author a `.tscn` under `test_project/tests/scenes/` and
  `load().instantiate()` it (`integration/test_scene_moving_turret.gd`); this
  also locks serialization (property names + enum ids survive load).
- Physics tests need REAL frames: spawn → `await physics(k)` with
  early-break; assert counts, signals AND finiteness.
- Re-entrancy: `integration/test_reentrant_frees.gd` frees things inside
  every signal handler. Godot REFUSES `free()` on an object that is
  emitting; handlers must `queue_free()` the emitter.
- Feature crosses: combine 2–4 systems; assert each system's signature, full
  finiteness and pool-reuse neutrality. Pairwise beats exhaustive.
- Preview tests: `make_preview_spawner`, `await idle(6)` for rebuilds,
  `debug_check_layer_coincidence(tol)`, `debug_get_preview_stats()`
  (spin/move keep `rebuilds`/`*_draws` flat).
- Spawner-owned vs factory-owned routing: connect BOTH signal sets and
  assert the silent side stayed silent.

## 9. Searching and reading

```sh
rg --files test_project/tests -g 'test_*.gd'      # suite inventory (-g globs the BASENAME)
rg -n "push_error|emit_signal" src/bullet_volley/   # fail-loud sites (pin exact text in tests)
rg -n "^func test_" test_project/tests/volley/test_volley_bounce.gd
rg -n 'BulletSpawner2D::shoot_once' src/           # where a method lives (files are split, §11/§12)
rg -n "PATTERN_ARGS_ring" src/patterns/            # a generator's signature row
```

- Big files: read the region you need (`Read` with offset/limit); never edit
  a file you have not read.
- Includes are root-relative (`"data/..."`). New `.cpp` files under `src/`
  are picked up automatically (recursive glob).

## 10. Pattern module (`src/patterns/`, class `BulletPatterns2D`)

### 10.1 File map

| File | Holds |
|---|---|
| `patterns_shapes.cpp` | direct-placement generators: grid, fan, spiral, line, aimed, rain, scatter, star polygon, multi/counter spiral (one `arm_spirals2d` core + `spiral_facing2d`), cross, wave, waterfall, lattice, corridor |
| `patterns_curves.cpp` | loop generators: ring, flower, ellipse, star, heart, rose, lissajous, circle (`dense_curve_loop2d` = dense sweep → even resample → fill outline) |
| `patterns_polygons.cpp` | rectangle, polygon, triangle, trapezoid, diamond; corner builders, `build_symmetric_polygon_loop`, `layout_polygon_corners2d`, `stack_at_marker2d` |
| `patterns_edges.cpp`, `patterns_polyline.cpp` | edge from points / image edges; polyline (and the spawner Path2D layout) |
| `pattern_layout2d.cpp` | the outline layout engine `layout_outline_slots` (On Outline / Layers / Fill Inside), resamplers, generator head checks (`pattern_check_amount`, `pattern_check_marker`, `pattern_check_corner_layout`, `danmaku_validate_head`), layer helpers |
| `pattern_preview_tracks2d.cpp` | `helper_sample_outline_*`: the curve each pattern's bullets sit on |
| `pattern_signatures2d.inc` | ONE row per argument of every `helper_generate_transforms_*` (§10.2) |
| `bullet_patterns2d_bindings.cpp` | the generated wrappers + every bind (one line each) |
| `bullet_patterns2d.hpp`, `pattern_params2d.hpp` | class + enums; the `<Shape>Params2D` structs (defaults = GDScript defaults) |
| `pattern_knobs2d.hpp`, `pattern_knob_table2d.inc`, `pattern_knob_checks2d.hpp` | spawner knobs, the knob table (§10.3), its value checks + shared array validators |
| `pattern_dispatch2d.cpp`, `pattern_track_dispatch2d.cpp` | per source: knobs → Params → `generate_<shape>2d`; knobs → preview track |
| `pattern_gating2d.cpp`, `pattern_presets2d.cpp`, `pattern_registry2d.cpp` | inspector gating, presets, ONE registry table (ids, names, prefixes, capabilities; `get_shapes()`) |
| `pattern_generate2d.cpp` | `BulletPatterns2D.generate(shape, amount, marker, params)` (writes knobs by name through the same table and checks) |
| `pattern_bake_cache2d.*`, `pattern_pose2d.*` | the spawner bake cache (§13) |
| `pattern_debug2d.cpp` | outline debug inspectors used by tests |
| `patterns_internal.hpp`, `pattern_curves2d.hpp` | shared declarations and formulas used by a generator AND its track |

The module never touches the scene tree: the spawner resolves children /
aimed target / Path2D curve into `PatternInputs2D`
(`bullet_spawner2d_patterns.cpp`, `generate_raw_pattern`).

### 10.2 Generators and the signature table

Every generator is a native core `generate_<shape>2d(amount, marker, const
<Shape>Params2D &p)` returning `PatternSlots2D` (std::vector, no Variant per
bullet). Inside, inputs read as `p.<field>`; the function names itself once
and rejects input in a fixed order (`patterns_shapes.cpp`, grid):
```cpp
const char *caller = "helper_generate_transforms_grid";
PATTERN_REQUIRE(pattern_check_amount(caller, transforms_amount));       // "<caller>: transforms_amount must be between 0 and 10000."
PATTERN_REJECT_IF(!Math::is_finite(p.jitter) || p.jitter < 0.0, "jitter must be a finite number >= 0."); // pushes "<caller>: <tail>", returns {}
PATTERN_REQUIRE(pattern_check_marker(caller, marker_transform, false)); // finite marker (true = also invertible)
```
Shortcuts: `danmaku_validate_head(caller, amount, marker)` = amount + finite
invertible marker; `pattern_check_corner_layout(caller, p.corner,
p.outline.layer_layout)` = the six polygon corner checks in pinned order.
Check ORDER is part of the contract (only the first failing check reports).

`pattern_signatures2d.inc` describes the GDScript wrapper of each generator:
```
#define PATTERN_ARGS_heart(REQ, OPT) \
	OPT(real_t, size, 150.0, size) \
	OPT(real_t, base_rotation, 0.0, base_rotation) \
	OPT(bool, face_outward, true, face_outward) \
	OPT(real_t, facing_offset_degrees, 0.0, facing_offset_degrees) \
	PATTERN_OUTLINE_HEAD(REQ, OPT) \
	OPT(int, layer_layout, 1, outline.layer_layout)
PATTERN_GENERATOR(heart, HeartParams2D)
```
`REQ(type, name, field)` / `OPT(type, name, default_literal, field)`; field
is the Params path (`outline.`/`corner.` for the shared layout structs).
The table expands into the header declarations (with C++ defaults), the
wrapper bodies and the binds (D_METHOD names + DEFVALs). Rules: exact C++
types (enum types feed the doc's `enum=`, real_t vs double is visible);
defaults are LITERALS (never `Params{}.x`: real_t is float, so 0.3 would
become 0.30000001 in the bound default); new arguments go at the END of an
entry (positional calls); the Params struct default must match the row.

### 10.3 Spawner knobs (`pattern_knob_table2d.inc`)

ONE row per spawner "Bullet Patterns" property in inspector (and `.tscn`)
order, expanded into the spawner getters/setters (validation via
`pattern_knob_checks2d.hpp`, message "BulletSpawner2D: <knob> <text>,
keeping the old value."), their declarations and binds, and the by-name
writer of `generate()`. New knob = field in `PatternKnobs2D` + one
`PATTERN_KNOB` row. `PATTERN_PROPERTY` rows (node paths, arrays, enum-typed
Path2D knobs, spawner settings) keep hand-written accessors in
`bullet_spawner2d_pattern_properties.cpp`. The dispatch copies knobs into
Params; the shared layouts come from `outline_layout()` / `corner_layout()`.

New shape = registry row (next free id) + knobs + table rows + Params
struct + signature entry + `generate_<shape>2d` + a dispatch and a track
case; gating follows its prefix.

### 10.4 Invariants every generator keeps

Pinned by `spawner/test_spawner_pattern_counts.gd`,
`test_spawner_pattern_bake.gd`, `test_spawner_preview_coincidence.gd`,
`test_spawner_preview_track_coincidence.gd`, `patterns/test_patterns_gating.gd`,
`factory/test_factory_helper_edges.gd`:
1. Exactly `helper_bullets_amount` transforms (On Outline, Layers, Fill
   Inside; gaps redistribute, overflow shrinks spacing).
2. No two bullets closer than 0.5 px. Closed curves sweep exactly once
   (watch retraces: odd roses, shared-factor Lissajous, multi-revolution
   spirographs); self-crossing curves use `resample_loop_even_distinct`.
3. Pure function of its inputs (the bake cache relies on it; random
   patterns take a seed; seed 0 = the global RNG).
4. The preview track draws the curve the bullets sit on, for EVERY source
   (dots within 1.5 px x pattern_scale). Formulas both sides need live in
   `patterns_internal.hpp` / `pattern_curves2d.hpp`.
5. Every bad input fails loud once with exact wording; amount errors read
   "<fn>: transforms_amount must be between 0 and 10000." for every shape
   generator (polyline keeps its pinned "must be in 0..10000.").

Not merged on purpose: the six arc-length resamplers (precision and
threshold differences change bits).

## 11. Volley (`src/bullet_volley/`, class `BulletVolley2D`)

One volley = one spawn call = N bullets in one MultiMesh + one physics area.
Per-bullet state is struct-of-arrays (`all_cached_*`, `all_bounce_*`, ...).

### 11.1 File map

```
bullet_volley2d.hpp            the class: fields grouped per feature + declarations
bullet_volley2d_internal.hpp   inline helpers shared by several volley TUs
bullet_volley2d.cpp            lifecycle: spawn, enable_volley (pool reuse), begin/release life, enable/disable/clear bullet
bullet_volley2d_tick.cpp       tick() + move_bullets (THE per-bullet loop) and its stages (hot path)
bullet_volley2d_render.cpp     physics interpolation pass
bullet_volley2d_setup.cpp      MultiMesh/buffer setup (one set_buffer per spawn)
bullet_volley2d_collision.cpp  area + ONE shared shape, intake, dedup, paused-overlap park/replay, drain
bullet_volley2d_{bounce,homing,orbit,motion,curves,wobble,gravity,lifetime,timers,attachments,
                 effects,animation,teleport,debug,bindings}.cpp   one feature each
bullet_volley2d_graze.cpp      graze arming, per-tick zone snapshot (prepare_graze_tick), live dispatch (§20)
homing_target_deque.hpp        per-bullet/shared target queue (RingDeque2D: allocates nothing while empty)
```

### 11.2 The tick (`bullet_volley2d_tick.cpp`)

`tick()`: drain collisions (impact pose) → `prepare_graze_tick` (armed
volleys) → `move_bullets` → homing reached events → graze events →
animation finished → lifetime. `move_bullets` computes a
`const MoveTick2D t` once (hoisted flags, shared curve samples, pattern
length, lock snapshot, `core_limit` = the size every always-sized array
covers) and reuses ONE `BulletStep2D b` scratch for all bullets
(`b.begin(i, ...)` resets only what each bullet must start from). Per
bullet, in order:
`step_homing` (own deque wins, shared is the fallback) →
`step_direction_curves` → `step_wobble` → `step_rotation` (+ adjust
direction, bounce visual) → velocity refresh → `step_pattern_and_forces`
(pattern, wind, gravity) → `step_orbit` → `step_place` (transform, shape,
attachment, trail, reached test) → `step_graze` (only while a zone has a
live target; §20) → `step_speed` (next tick's speed).
All stages are `_ALWAYS_INLINE_` in this TU: keep per-bullet code in ONE
translation unit; a `_ALWAYS_INLINE_` helper called from another file links
but fails at extension LOAD time (CRASH) — move its definition to
`bullet_volley2d_internal.hpp`.

### 11.3 Shared helpers (use them; do not re-implement)

| Helper | Use |
|---|---|
| `for_range` / `collect_range<Arr>` | every `all_bullets_*` method: one range validation, then per bullet |
| `validate_bullet_index(i, fn)` | per-bullet entry check (loud); guarantees `0 <= i < amount_bullets` |
| `reject_pooled_handle(fn)` | a pooled volley is a stale handle: refuse writes (frozen bullets accept them) |
| `refresh_cached_velocity(i)` | the documented composition: direction x speed + inherited + fall speed |
| `present_bullet_transform(i, delta)` | push a changed cached transform to shape, MultiMesh, attachment, interpolation |
| `teleport_bullet_to(i, origin, delta)` | the shared teleport body |
| `visit_bounce_ledger(f)` / `visit_rotation_trio(f)` | the ONE list of those per-bullet arrays |
| `homing_drop_own_targets(i)` / `homing_resync_count(i)` | per-bullet homing counter bookkeeping |
| `bullet_homing_push` / `shared_homing_push` | counted pushes (a rejected push is never counted) |
| `orbit_keep_lock_across_replace_for_bullet` | single routing point for a front change |
| `direction_curve_owns_direction(i)` | direction setters refuse (one warning) while a direction curve steers |
| `sample_volley_curve(curve, use_unit, fallback)` | a curve at the volley clock |
| `apply_direction_axis2d` | one direction-curve channel (x or y) |
| `write_multimesh_transform2d` | the 8-float MultiMesh layout |
| `area_ready_or_error(fn)` | area accessors on a never-spawned volley |

### 11.4 Life states and pooling

ACTIVE → last bullet out → POOLED (auto pooling on: `release_life` drops
attachments, homing, orbit, timers, records, the volley's own connections,
owner, user Resources, groups, metadata; the handle is stale and
`enable_bullet` refuses it) or PARKED (auto pooling off: frozen, still owned,
wakeable). `begin_life` is the single new-life path (one generation bump =
`get_life_id`). `disable_bullet` FREEZES (state kept; `reset_state` /
`bullet_reset_state` clear ledgers); active-only timers hold while parked.

### 11.5 Per-bullet data model

Entry `i` drives bullet `i`; per-bullet (valid) > shared (valid) > default;
explicit all-zero = intent; tile checkboxes wrap short arrays. Curves:
clear FREEZES the last sample (pinned).

## 12. Factory, spawner, signals, pooling

### 12.1 Map

```
src/
  register_types.cpp                      class registration
  factory/bullet_factory2d.cpp            lifecycle, containers/debugger, interpolation, tick/render sweeps
  factory/bullet_factory2d_spawn.cpp      request validation, spawn_volley(+_span), pool pre-population
  factory/bullet_factory2d_structural.cpp reset/free_*/clear_*, *_deferred queue, bookkeeping
  factory/bullet_factory2d_effects.cpp    factory-owned one-shot effects
  factory/bullet_factory2d_stats.cpp      frame stats, monitors, debugger knobs, debug_*
  factory/bullet_factory2d_graze.cpp      graze debug readouts (the factory's detector lives in it)
  factory/graze_detector2d.cpp            GrazeDetector2D: graze target lists (stable slots, scans on the update interval, x-sorted slab order)
  factory/bullet_factory2d_bindings.cpp   _bind_methods
  bullet_spawner/bullet_spawner2d.cpp     wiring, cadence, spin, bursts/telegraph, pattern lists, shoot_once
  bullet_spawner/bullet_spawner2d_pattern_properties.cpp  hand-written pattern accessors + presets
  bullet_spawner/bullet_spawner2d_patterns.cpp   scene inputs -> generate_raw, bake cache use
  bullet_spawner/bullet_spawner2d_homing.cpp     homing/orbiting, target resolution, live steering
  bullet_spawner/bullet_spawner2d_preview.cpp    preview snapshot/rebuild, pose, debug_* geometry
  bullet_spawner/bullet_spawner2d_preview_layer.cpp  PatternPreviewLayer2D (draw_multimesh)
  bullet_spawner/bullet_spawner2d_movement.cpp   Path2D movement
  bullet_spawner/bullet_spawner2d_graze.cpp      Graze group: target sources + detector, arming at shot, refresh/resolve_graze_targets, ring preview (GrazePreviewLayer2D)
  bullet_spawner/bullet_spawner2d_bindings.cpp   _bind_methods (groups/subgroups) + _validate_property
  data/        inspector Resources: BulletVolleyData2D, BulletSpeed/Rotation/Curves/Wobble/EffectLayerData2D, BulletGrazeZone2D
  pooling/     VolleyPool (parked volleys per key), VolleyPoolKey2D
  attachments/ BulletAttachment2D + its object pool
  debugger/    BulletVolleyDebugger2D (collision-shape overlay)
  core/        header-only: warn_once2d, cached_string_names2d, easing2d (Tween port), dynamic_sparse_set,
               collision_shape_helper2d, reentrancy_guard2d, transform_math2d, graze_targets2d (graze target lists' helpers, point target ids),
               node_scan2d (THE target filter of homing and graze, their node-name / children scans, PREVIEW_META_KEY)
```

### 12.2 Flows

- Spawn: `BulletFactory2D.spawn_volley` → `spawn_volley_internal` → pool pop
  by `VolleyPoolKey2D` (bullet count + shape) or fresh alloc →
  `enable_volley` (validates EVERYTHING before mutating; a refused enable
  changes nothing) → `set_up_bullet_instances` (one `set_buffer`). The
  spawner uses the span path (`spawn_volley_span`, no Variant boxing).
- Factory sweep: the snapshot is (instance id, pointer) pairs, never slots
  (a handler `free()` swap-removes `all_volleys`); `volley_free_epoch` skips
  the ObjectDB lookup unless something was freed; `sweep_tick_stamp` /
  `sweep_timer_stamp` give one tick and one timer pass per volley per step.
- Spawner loop: `_process` runs iff `needs_process()`. Order: movement →
  spin → preview → shooting/bursts. `shooting_*` signals track auto-fire
  transitions only; auto-fire errors are latched (one report per
  misconfiguration until a config setter re-arms it).
- Preview: geometry snapshotted UNSPUN in holder space; spin/move are the
  layer NODE transform, so spinning/moving costs 0 rebuilds / 0 redraws.
- Movement: the spawner travels a Path2D at runtime (`advance_movement`,
  bounded 64-leg catch-up; `Easing2D::ease` parity-tested vs Tween).

### 12.3 Structural calls and threading

- Everything runs on the main thread (physics callbacks included); no locks.
  Do not add threads.
- Structural ops (`reset/free_*/populate_*`, shape changes) are idle-frame
  only: inside a physics frame they are REJECTED loudly. Their `*_deferred`
  twins queue through `queue_structural_call` and are safe from anywhere.
  `free_active_bullets()` DESTROYS; clear/expiry return drained volleys to
  the pool (or park them with auto pooling off).

### 12.4 Signals (live, in-tick) and liveness

- Every bullet signal fires synchronously inside `BulletVolley2D::tick`:
  `area_entered` / `body_entered` / `bounce_*` (drain, impact pose),
  `bullet_homing_target_reached` (after the move),
  `sprite_animation_finished`, `life_time_over` (lifetime pass). The bullet
  is ALIVE in the handler (custom data, transforms, velocity, counts,
  attachment readable).
- The plugin decides afterwards from the post-handler state: a hit kills
  only if the count is still at max (a heal vetoes it); expiry kills only
  bullets whose life was not extended; a homing auto-pop pops only if the
  front is still the reached target; a handler that disabled the bullet
  gets no extra effects; freed targets are skipped; a pause stops the sweep.
- Handlers may spawn and edit bullets freely; structural calls need the
  `*_deferred` twins. Remaining `call_deferred` uses: `shoot_once_deferred`,
  the structural queue itself, the debugger restore, preview rebuilds (a
  call deferred from physics still runs inside the physics frame: pinned in
  `integration/test_engine_facts.gd`).
- Routing: a spawner volley emits on its spawner, a factory volley on the
  factory, never both. The ONE exception is graze (§20): `bullet_grazed` /
  `bullet_graze_exited` bubble to the owner spawner (while alive) and then
  ALWAYS to the factory (graze changes nothing and must outlive enemies). A freed spawner takes its connections along
  (`orphaned_volleys` decides what its bullets do).
- C++ liveness: never carry a raw `Object*` across user code or
  `call_deferred` (godot-cpp converts Variant args before your id check
  runs). Capture `get_instance_id()` before the emit and compare
  `ObjectDB::get_instance(id) == expected_ptr` after (`tick_may_continue`,
  `revalidate_configured_volley`, the movement `alive()` lambda). Stop
  touching `this` when it fails.
- Pinned by `volley/test_volley_hit_contract`, `test_volley_lifetime_contract`,
  `test_volley_signal_timing`, `test_volley_bounce_signals`,
  `test_volley_homing_mixed_deques`, `spawner/test_spawner_orphan_policy`.

### 12.5 Collision pipeline

Area callbacks (ADDED) → object-level dedup window (O(1) hash;
`collision_dedup_by_object = false` for shape-level) → records with
queue-time epochs/velocity/pose → per-tick drain → counting → signals →
bounce. Paused factory: ADDED events are PARKED per volley (cap 4096,
deduped, cancelled by REMOVED) and replayed once on resume.

## 13. Pattern bake cache (spawner)

- `resolve_raw_pattern` generates raw transforms (pre spin/scale/skip) once
  per `pattern_version` and re-poses them per shot (one 2x3 multiply per
  bullet). The motion class is MEASURED with probe markers: RIGID,
  TRANSLATION (same basis only, e.g. world-direction rain), NONE. Sources
  with `reads_external_state` (CHILDREN/AIMED/CORRIDOR/CUSTOM/PATH2D) and
  unseeded random always regenerate.
- Every geometry setter calls `on_pattern_changed()` (version bump +
  preview rebuild). Presets write members raw, then call it once.
- Proof: tests run with `debug_set_pattern_cache_verify(true)` (every cached
  result regenerated and compared; mismatch = error = red test);
  `test_spawner_pattern_bake` perturbs EVERY pattern property, and every
  shape knob under its OWN shape. `pattern_cache_mode = Off` exists for
  debugging.

## 14. Contracts and pitfalls catalog (pinned by tests — do not "fix" back)

- Ranges: `all_bullets_*(start, end)`: (0, -1) = whole volley, end -1 =
  through the last bullet; out-of-range/inverted ranges push `Invalid index
  range in <fn> ...` once and apply NOTHING (`volley/test_volley_range_contract.gd`).
- `spawn_pattern_list` entries: unknown keys fail loud with a did-you-mean;
  valid keys still apply.
- Pause/resume replays overlaps that began during the pause, exactly once.
- Same-owner full-drain wake keeps linear ballistics; foreign wakes are
  neutralized.
- `spawn_position_offset_space`: Global (default) vs Local; the preview
  draws it where bullets spawn.

| Behavior | Suite |
|---|---|
| Every helper draws exactly `helper_bullets_amount` bullets at distinct spots; flower FAN splits the amount over petals | `test_spawner_pattern_counts` |
| Every shape generator reports amount and NaN-marker errors in one wording | `test_factory_helper_edges` |
| Presets are clean (pattern knobs + spin reset; Transform subgroup and node wiring kept) | `test_spawner_presets` |
| Pattern-list entries are temporary overrides; `pattern_list_finished` in both modes | `test_spawner_pattern_lists` |
| Auto bursts clamp to `volleys_remaining`, cancel when auto-fire turns off | `test_spawner_burst_telegraph` |
| Controls before the tree survive `_ready`; `shooting_*` signals are auto-fire only | `test_spawner_cadence` |
| Every failed shot emits `volley_skipped(reason)` | `test_spawner_cadence` |
| Reparenting keeps assigned nodes, tracked volleys, chains and lists | `test_spawner_tree_reentry` |
| Setters reject NaN/Inf/out-of-range with "keeping the old value"; no setter depends on another field | `test_spawner_setter_contract` |
| Fire arc follows spin; the volley chases the targets the arc approved | `test_spawner_homing_propagation` |
| ONE target filter for every node source of homing and graze: Node2D, in the tree, alive, finite, in the bullets' world, never a factory or its nodes, never a preview layer; scans never find the spawner's subtree, Node Group / Node Path may name its children, homing Node Path may name the spawner itself, graze never | `test_spawner_target_filter` |
| Every graze / homing source honors one contract (finds its node, newcomers, interval, filter, edits in flight, orphans, preview, tree order, settings kept, scene round trip; homing: selection, range, retarget, live mouse) | `test_spawner_graze_sources`, `test_spawner_homing_sources` |
| Mouse and Global Positions graze: points with a null signal target, visits follow the point's index, read every tick (no filter, no interval), no cap | `test_spawner_graze_point_sources` |
| Every bound method names every argument (no `_unnamed_argN`) | `integration/test_accessor_contract` |
| Homing queues have no cap (300 targets queue, shared and per bullet); freed targets trimmed; retarget skips dead/pooled/foreign volleys | `test_spawner_homing_queues` |
| No hidden caps: every live homing volley stays tracked and retargets; `homing_max_targets` has no upper bound; no default homing/graze group or name (empty settings are named in warnings) | `test_spawner_tracker_cap`, `test_spawner_homing_selection`, `test_spawner_homing_detection` |
| A bullet steering by its own targets owns its reach even while the shared deque has targets | `test_volley_homing_mixed_deques` |
| Every orbiting bullet circles its own front target; a zero-delta tick moves nothing | `test_volley_orbit_own_center` |
| `get_bullet_velocity` = direction x speed + inherited + fall speed, in the tick AND right after every setter; `bullet_set_velocity` sets the exact total | `test_volley_velocity_composition` |
| Direction setters under a direction curve refuse with one direction-worded warning | `test_volley_core` |
| Public surface: one `spawn_volley`, no `BulletType`, shared signal names/payloads, exact stats keys | `test_factory_api_surface` |
| A pooled volley reused for plain data matches a cold volley field by field | `test_volley_pool_reuse_all_features` |
| A one-bullet volley behaves like bullet 0 of any volley | `test_volley_single_bullet` |
| Every spawn-data and volley property sits in a group; names and titles unique | `test_volley_data_inspector` |
| Hits/bounces/lifetime/homing fire live; the kill is decided after the handler | `test_volley_hit_contract`, `test_volley_lifetime_contract`, `test_volley_signal_timing`, `test_volley_bounce_signals` |
| disable_bullet freezes; opt-in reset; parked vs pooled; pooled wake refused | `test_volley_freeze_contract` |
| Every clock restarts per life, holds while parked/paused, finite under hitches | `test_volley_clock_audit` |
| Custom data per bullet and shared, readable in every callback, never leaks through the pool | `test_volley_custom_data_contract` |
| orphaned_volleys policies; unhandled hits warn once | `test_spawner_orphan_policy` |
| Every setter round-trips or rejects loudly and keeps the old value | `integration/test_accessor_contract` |
| Homing aims through inherited spawner momentum | `test_volley_homing_drift` |
| reset_finished fires after the reset; handlers can respawn | `test_factory_reset_finished` |
| Graze: one graze per ring per bullet life (Once), outer ring first, swept test, inclusive effective radius (bullet size counted) | `test_volley_graze_core` |
| Graze visits: one exit per visit that grazed, naming its target; killed/frozen bullets never exit; a vanished target ends or moves its visits silently | `test_volley_graze_exit` |
| Graze handlers are live; events of disabled/woken bullets, freed targets, replaced zones are dropped; a pause finishes the batch | `test_volley_graze_handlers` |
| Spawner volleys are armed before any shot signal; graze bubbles spawner -> factory; orphans keep grazing on the factory | `test_spawner_graze`, `test_spawner_graze_orphans` |
| The ring preview draws exactly the runtime targets; never saved, never a marker or homing candidate | `test_spawner_graze_preview`, `run_editor_smoke.py` |
| No graze target cap (300+ targets, a member joining after the shot counts next tick); a target keeps its slot while others come and go | `test_volley_graze_targets` |
| Graze target sources (node group, path, name, children: the spawner decides who grazes, zones only how), filter, update interval, refresh; edits reach flying bullets; orphans keep the settings; homing's refresh is `retarget_live_volleys()` | `test_spawner_graze_detection` |
| Past 8 targets the slab finds exactly what testing every target finds (same events, same ties) | `test_volley_graze_slab` |
| A fading sound voice is never restarted or stolen again; a pool of fading tails never starves new plays | `sound/test_sound_mixer_unit` |
| A followed bullet keeps one hum voice per sound entry | `sound/test_sound_flight` |
| Cosmetic sound and effect rolls never consume Godot's global RNG (seeded gameplay is unchanged) | `sound/test_sound_rng_isolation` |

- Edge cases to test everywhere: NaN/Inf scalars and vectors; null array
  entries; empty arrays; short vs oversized arrays; OOB indices (-1/99);
  inverted ranges; zero amounts/sizes/speeds; singular transforms; freed
  factory/generator/target/path mid-flight; pool reuse across lives;
  pause/resume with overlaps in flight; 10k cap (10000 ok, 10001 rejected);
  maxed timers (64); huge target/queue counts; same-frame expiry+respawn; deferred calls from
  collision handlers; zero-delta ticks; mixed shared + per-bullet features.
- Godot facts that bit us: GDExtension virtuals
  (`_get_configuration_warnings`) are not script-callable (expose a public
  twin); NEVER open the real test_project in an editor with a scene argument
  (headless or not): the editor saves `open_scenes`/`current_scene` in
  `.godot/editor/editor_layout.cfg` and the developer's next launch reopens
  that scene, so a @tool script in it runs in their editor (an editor-smoke
  probe once quit the developer's editor 3 s after every launch); Godot imports `.csv` files inside the project as translations (keep
  logs under a `.gdignore` folder); a warning printed per spawn retains
  objects (use `WarnOnce2D`); `--quit-after` guards headless scripts;
  NEVER `std::make_shared` in src/ (use `std::shared_ptr<T>(new T(...))`):
  its type tag `_Sp_make_shared_tag::_S_ti()::__tag` is a GNU unique
  symbol, glibc then marks the library NODELETE, `dlclose` keeps it, its
  statics outlive Godot's StringName table and EVERY test file reports ~86
  `Orphan StringName` leaks (diagnose with `LD_DEBUG=all ... | grep NODELETE`).

## 15. Benchmarks and performance lessons

### 15.1 Running

```sh
python3 tools/run_benchmarks.py                     # 21 headless scenarios x5 (median)
python3 tools/run_benchmarks.py --scenario spawner_ # substring filter (repeatable)
python3 tools/run_benchmarks.py --gate              # exit 1 on regression vs log/baseline.json
python3 tools/run_benchmarks.py --update-baseline   # ONLY for an accepted change; say so in the commit
```

- Read `test_project/benchmarks/log/LATEST.md`. Regression = p50 > +10% AND
  > +0.05 ms, or p95 > +20% AND > +0.3 ms (p99/max reported, not gated).
  `log/history.csv` = trend, `log/results/*.json` = raw runs. Compare only
  same machine + same build type. Differences under ~5% p50 are noise.
- Columns: frame = step (the scenario's plugin calls) + engine (physics +
  factory tick + render); tick = factory physics tick only.
- In-engine: `BulletFactory2D.get_frame_stats()`, the `BlastBullets2D/*`
  monitors (`register_performance_monitors`), the editor profilers.

### 15.2 A/B

On your build run the scenarios, `git stash push -u -- src/`, rebuild, run
again, `git stash pop`, rebuild. Against an OLD commit:
`git worktree add --detach <dir> <commit>`, copy `godot-cpp/` (with `bin/`)
into it and point that worktree's `tools/config.json` `godotProjectFolder`
at ITS `test_project` (left as is, the old build installs over the main
project's `.so`). For quick numeric comparisons load two
`log/results/*.json` files and compare `scenarios[name].factory_tick_ms.p50`.

### 15.3 Lessons (measured on Ryzen 7 8840HS, debug build)

- 10k bullets in flight ≈ 0.26 ms factory tick (`volley_10k_flight`).
- Tick code rules: no per-bullet engine-boundary crossings (hoist sin/cos,
  inverses, StringNames via `CachedStringNames2D`), O(1) amortized per
  bullet, no per-bullet allocation (reuse scratch, `reserve()`), batched
  buffer uploads.
- Per-bullet stores are expensive at 10k: a per-bullet scratch struct that
  zero-initialized three Vector2 made `trails_fx_2k` +13% and
  `volley_10k_flight` +7%. Create scratch once per tick, reset only what each
  bullet needs; make the per-tick context a `const` object (a mutable one
  is reloaded after every external call). The staged loop then measured
  level or faster than the monolithic one (tick p50: `trails_fx_2k` −1%,
  `volley_10k_flight` −6%).
- Write-only per-bullet arrays cost a store per bullet per tick: delete
  them (the shape-origin cache was one).
- `std::deque` allocates on construction: two mallocs per bullet per cold
  spawn even with homing unused; `RingDeque2D` allocates nothing while empty.
- Duplicate seeding: a blank pre-seed that ran the full per-bullet speed
  setup before the real one cost ~5% of a cold spawn.
- Cold spawn was O(N²) (8k: 1.5 s) when every per-bullet `shape_set_data`
  re-updated all shapes; one shared shape per volley made it O(N) (8k: ~10 ms).
- Inlining `move_bullets` into the factory loop cost 20–25% on
  `trails_fx_2k`: it stays a separate call in its own TU.
- A `Ref<>` returned by value per bullet costs a reference()/unreference()
  pair (trail and effect shards hand out raw `MultiMesh *`).
- Pattern layout: the pattern refactor (one `marker.affine_inverse()` per
  call instead of per point, shared layout lambdas) cut
  `spawner_warm_shot_5k` p50 ~23% and `spawner_aimed_regen_2k` ~13%.
- Spawner preview spin/move: 0 rebuilds, p99 ~50 ms → 3–5 ms.
- Live signals: `lifetime_signal_10k` adds ~0.25 ms per 10k-bullet expiry
  wave over `mass_expiry_10k` with an empty handler.
- godot-cpp `Vector2::dot()`, `length_squared()` and `is_finite()` are NOT
  inline (a PLT call each): the graze stage tripled its cost through three
  `.dot()` calls per bullet. Hot paths use component math (`a.x * b.x + ...`).
  Check with `objdump -dr -C src/bullet_volley/bullet_volley2d_tick.os`.
- Graze armed: `graze_10k_flight` ≈ +10% tick over `volley_10k_flight`
  (fast path inline in the loop, slow path `_NO_INLINE_`); a separate
  post-move pass measured slower. Graze off costs nothing (hoisted flag).
- Many graze targets: testing every target is ~1.6 ns per bullet-target pair
  (`graze_64_targets`, 10k bullets x 64 targets: 1.32 ms tick). Past 8 live
  targets the list keeps a test view sorted by x (once per sweep, shared by
  every volley) and each bullet binary-searches the slab within reach of
  its motion: ~0.36 ms. Reading few targets through a pointer cost
  `graze_10k_flight` +3% (one more load and branch per bullet): up to 8
  unsorted targets are copied inline into `GrazeTickTargets2D`. Volleys
  read larger lists IN PLACE (no per-volley copy, no per-volley liveness
  pass: the user-code epoch re-validates the shared list instead), and the
  swept distance is measured once per bullet for every zone.

## 16. Inspector groups and serialization locks

- Spawner groups, in order (locked by `test_volley_bounce.gd`): Setup,
  Bullet Patterns, Shooting, Spin, Homing, Orbiting, Graze, Sound, Preview,
  Movement, Performance. Spawn data (`BulletVolleyData2D`): Bullets, Appearance,
  Movement Speed, Bullet Rotation, Wobble, Gravity, Bounce and Ricochet,
  Movement Pattern Paths, Homing, Collision, Attachments, Sprite Effects,
  Rendering and Material. No duplicate titles, no ungrouped property
  (`volley/test_volley_data_inspector.gd`).
- Bullet Patterns: `pattern_source` + `helper_bullets_amount`, then a
  `Transform` subgroup, one `ADD_SUBGROUP("Ring", "helper_ring_")` per shape
  (prefix stripped in the inspector), `Outline Layers` last. The inspector
  EJECTS a property whose name lacks the subgroup prefix
  (`test_every_prefixed_subgroup_member_carries_the_prefix`). Mind
  overlapping prefixes (`helper_star_polygon_` vs `helper_star_`).
- ADD_PROPERTY BEFORE its bind_method is SILENTLY dropped by ClassDB (the
  runner flags the `class_db.cpp` error). Moving a property = moving its
  bind + ADD_PROPERTY paragraph (pattern knobs: move the table row).
- A setter whose field `_validate_property` reads MUST call
  `notify_property_list_changed()`.
- Gating: helper_* per pattern mode; homing/orbiting/movement/preview knobs
  hide while their switch is off; spin speed only in Continuous;
  amplitude/frequency only in Oscillate; burst_*/telegraph_sec only when on.
- Enum ids are serialized: renumbering silently repoints saved scenes.
  `pattern_source` ids and pool-key shape ids (`Circle:3,Rectangle:4,Capsule:5`)
  are locked. Saved property ORDER follows the bind order (an inspector
  reorg reorders `.tscn` lines: harmless, expected in diffs).

## 17. Documentation generation

- Order: close the editor → build → `GODOTPP_NONINTERACTIVE=1 python3
  tools/generate_xml_docs.py` → review `git diff doc_classes/` → fill every
  empty `<description>` → build (docs compile into the binary) → regenerate:
  the diff must be empty apart from escaping (`>` becomes `&gt;`).
- The doctool KEEPS descriptions of existing members, ADDS new members with
  empty text, DROPS removed ones and re-sorts alphabetically. Write `<`/`>`
  as `&lt;`/`&gt;`.
- A regeneration with zero diff proves the bound API matches the docs (use
  it after big refactors).
- BBCode: `[Class]`, `[method C.m]`, `[member C.p]`, `[signal C.s]`,
  `[param x]`, `[constant C]`, `[enum C.E]`, `[code]`, `[codeblock]`, `[b]`, `[i]`.

## 18. tools/ catalog

- `setup.py`: interactive menu (Godot paths/versions, project folder,
  rename, icons, docs, builds, profiles, LTO, export zip, tutorials).
- Build: `compile_debug_build.py` / `compile_release_build.py` (both write
  the build stamp on success) / `clean_build.py` / `select_build_profile.py`
  / `edit_build_profile.py` / `change_lto_mode.py` / `toggle_editor_target.py`
  / `toggle_debug_symbols.py` / `toggle_reloadable.py`.
- Verification: `run_tests.py`, `lint_tests.py`, `api_snapshot.py` (§7.4),
  `format_code.py`, `run_benchmarks.py`, `run_editor_smoke.py` (headless
  editor + @tool probes in `test_project/tests/editor_smoke/`, run on a
  throwaway COPY of the project; probes act only with `--blast-editor-smoke`).
- Config: `select_godot_path.py`, `select_godot_project.py`,
  `change_godot_target_version.py`, `update_godot_cpp.py`,
  `config_manager.py` + `config.json` (machine state), `paths.py`.
- Plugin: `renaming.py`, `update_icons.py`, `export_plugin.py`,
  `generate_xml_docs.py`, `gdextension_file_helper.py`, `apple_helpers.py`,
  `git_helpers.py`, `scons_helpers.py` + `scons_build_helpers.py`,
  `tutorials.py`.

## 19. Final report template

End every task with:
1. What changed (per area), with the commits.
2. Bugs found and fixed, each with the test that pins it.
3. Verification: suite (`ALL n TEST FILES PASSED`), `--self-test`, format
   check, snapshot diff (IDENTICAL or the listed intended deltas), benchmark
   gate + the notable deltas.
4. Big-O of the touched paths (per tick, per spawn, per call).
5. A ratings table (stability, performance, code clarity, test coverage,
   docs) with one line of justification each.
6. Open questions for the user (contract changes you did NOT make).

## 20. Graze (`BulletGrazeZone2D`, volley stage, spawner group)

A bullet grazes ring k of a zone when its motion during one tick (segment
start -> end, swept: fast bullets never skip a ring) comes within ring k's
effective radius (`ring_<k+1>_radius` + bullet bounding radius when
`count_bullet_size`) of a zone target. Detection is math in the tick, never
physics (no layer/mask setup, deterministic under `debug_advance_time`).

### 20.1 Pieces

WHO grazes is decided in ONE place, never in the zone: the spawner's Graze
group (`graze_target_source` + knobs) for spawner volleys, the group passed
to `graze_set_zones(zones, target_group)` for factory volleys. A zone only
says HOW (rings, regraze, bullet size, preview color). Every zone of a volley
rings the same targets.

| Piece | Where | Notes |
|---|---|---|
| Zone config | `data/bullet_graze_zone2d.*` | 1..4 rings (fixed `ring_N_radius`: inspector-safe), `regraze` (Once 0 / After Exit 1, serialized), `count_bullet_size`, `preview_color`; every accepted change emits `changed`. No targeting or runtime preview property (the spawner's `graze_preview_during_runtime` is the only runtime ring preview) |
| Target filter | `core/node_scan2d.hpp`, `core/graze_targets2d.hpp` | `target_node_usable2d` (THE filter, homing too): `target_node_present2d` (Node2D in tree, not queued, finite, same World2D: the per-tick check between scans) + never a preview layer + never inside a factory (`node_in_bullet_factory2d`: one `is_ancestor_of` per `BulletFactory2D::factories_in_tree`, no ancestor walk); `collect_graze_targets2d`: group members (+ filter group), tree order, no cap; shared by every source AND the previews. Point targets (Mouse, Global Positions): id = `GRAZE_POINT_TARGET_BIT` (bit 63, never set for a Node) + index; never looked up in ObjectDB |
| Target lists | `factory/graze_detector2d.*` | `GrazeDetector2D`: the factory owns one (a list per group, every tick) for factory volleys; each spawner owns one (`std::shared_ptr`, shared with every volley it armed) and hands out the factory's list while it is Node Group + no filter + interval 0. `GrazeTargetList2D`: growable, stable slots (`slot_of` hash, free-slot reuse, per-slot change serials), membership rescanned per `update_interval` (factory `graze_clock`), positions once per sweep, re-validated within a sweep when the factory's user-code epoch moved, x-sorted test view past 8 targets |
| Arming + dispatch | `bullet_volley/bullet_volley2d_graze.cpp` | `graze_set_zones(zones, group)` (factory volleys; empty group refused), `graze_arm_from_spawner` (C++), `graze_target_list()`, `prepare_graze_tick` (reads the list in place; copies only <= 8 targets inline or a view without its owner spawner), `dispatch_graze_events` |
| Per-bullet stage | `bullet_volley2d_tick.cpp` | `step_graze`: the swept distance to the targets once per bullet (inline loop, or a binary-searched x slab), then each active zone's outer radius; `graze_visit` (`_NO_INLINE_` slow path) per zone |
| Spawner | `bullet_spawner/bullet_spawner2d_graze.cpp` | Graze group: `graze_target_source` (Node Group / Node Path / Node Name / Node Children / Mouse / Global Positions, serialized 0..5) + `graze_node_group`, `graze_global_positions` and the other knobs (homing's name matching via `core/node_scan2d.hpp`), `graze_update_interval`, `refresh_graze_targets`, `resolve_graze_targets()`, arming in `shoot_once` before the homing signals, ring preview (`GrazePreviewLayer2D`: top-level, internal, owner-less, tagged) |

### 20.2 Contract (pinned by the `*graze*` suites)

- Signals `bullet_grazed(target, volley, bullet_index, zone, ring_index)` and
  `bullet_graze_exited(target, volley, bullet_index, zone, deepest_ring_index)`,
  same payload on spawner and factory, live after the move (tick order §11.2).
  Bubbling: owner spawner first (while alive), then the factory ALWAYS.
- Rings touched in one tick fire largest radius first (equal radii by index).
  Once: each ring once per bullet life (per zone, shared by its targets).
  After Exit: leaving the zone re-arms its rings.
- A visit = inside the outermost ring of any zone target. It belongs to one
  target (anchor slot = target of its latest new graze). Exit fires once per
  visit that grazed, naming that target. Killed/cleared/expired/frozen
  bullets never exit; a wake starts a fresh visit; a vanished anchor moves
  the visit to another target the bullet is still inside, else ends it
  silently; a zone losing every target ends its visits silently. A target
  keeps its slot while others come and go; a new list (source switch, new
  group) re-anchors every open visit the same way.
- Targets: Node Group (default; `graze_node_group`, EMPTY by default: the
  developer names it), Node Path, Node Name, Node Children, all filtered by
  `graze_filter_group` and THE target filter, NO cap, tree order (ties),
  never the spawner, never a factory or its nodes; the scans never find
  anything under the spawner, Node Group / Node Path may name its
  children. Membership rescans every `graze_update_interval`
  (0 = every tick: a joining target counts next tick); between scans
  positions stay live and a freed/queued target drops at once.
- Point sources: Mouse (the factory's `get_global_mouse_position()`, at
  runtime only: the editor draws nothing) and Global Positions (every
  entry, finite by the setter). Read every sweep (no filter group, no
  interval), signals carry a null `target`, a visit follows its point's
  index, `resolve_graze_targets()` returns the points as Vector2.
  `refresh_graze_targets()` rescans now. Spawner settings are shared with
  its volleys (edits reach bullets in flight; orphans keep them and the
  nodes the paths last resolved); `release_life` drops them.
- Inside a sweep, a target freed or queued by ANY live handler of an
  earlier volley is never tested by a later one (its Once ring is never
  spent): `BulletFactory2D::note_user_code()` bumps the user-code epoch
  right before every live emit / timer callback in the tick and the lists
  re-validate when it moved. A NEW emit or callback site in the tick MUST
  call `note_user_code()` (`test_volley_graze_handlers` pins two sites).
- Dispatch drops events whose bullet epoch moved, whose zones generation
  changed, or whose node target is freed/queued (points never die); stops when the volley is freed
  or queued; a pause lets the batch finish; an early tick return keeps the
  queue for the next tick; `release_life` clears it.
- The owner spawner never grazes its own bullets. Volleys hold zone Refs and
  read them every tick (live edits reach bullets in flight); `release_life`
  unrefs them (weakref-tested).

### 20.3 State and limits

`graze_state`: one `uint16_t` per bullet per zone slot (`uint16_t`, not
`uint8_t`: char stores alias everything and would force reloads in the
loop). Bits 0-3 rings grazed, 4 inside, 5-6 deepest ring of the visit,
7 visit fired; `graze_anchor` (`uint32_t`, same indexing) is the visit's
target slot. Targets: no cap. Structural limits: 4 zones per volley, 4
rings per zone (`BulletVolley2D::MAX_GRAZE_ZONES`,
`BulletGrazeZone2D::MAX_RINGS`). Per-bullet cost grows with the LIVE
targets only; past `GrazeTargetList2D::slab_min_targets` (8) a bullet tests
only the x-slab around its motion (`debug_set_graze_slab_min_targets`
forces either path; `test_volley_graze_slab` proves them equal).

### 20.4 Adding a zone option

A zone option is about HOW a graze is measured (who grazes belongs to the
spawner's Graze group: §20.5). Field + validated setter (emit_changed,
"BulletGrazeZone2D: <prop> ..., keeping the old value.") + bind in
`bullet_graze_zone2d.cpp`; read it in
`prepare_graze_tick` into `GrazeTickZone2D` (never read the Resource per
bullet); cover it in `test_volley_graze_core` (mutation-checked); the data
setter sweep picks the setter up automatically; regenerate docs (§17).

### 20.5 Adding a graze target source

Append the id to `GrazeDetector2D::Source` AND `BulletSpawner2D::GrazeTargetSource`
(serialized: never renumber) + the hint string; knobs = member + validated
setter that ends in `apply_graze_detector_config()` (+ `notify_property_list_changed()`
when it gates), a `Config` field filled in `graze_detector_config()`, gating
in `_validate_property`, an empty-setting line in `_get_configuration_warnings`;
the scan is one `case` in `GrazeDetector2D::scan` (nodes go through
`graze_add_target2d` = THE target filter, never the spawner; a point source
emits `GRAZE_POINT_TARGET_BIT | index` ids and belongs in
`GrazeDetector2D::uses_points()`); add the source to the matrices:
`test_spawner_target_filter` (node sources), `test_spawner_graze_sources`
(the per-source contract) or `test_spawner_graze_point_sources`, then docs
(§17).

## 21. Sound (`BulletSoundData2D`, mixer, spawner Sound group)

### 21.1 Pieces

| Piece | Where | Notes |
|---|---|---|
| Entry resource | `data/bullet_sound_data2d.*` | One list entry per sound: triggers (11 SOUND_ON_* ids, serialized), streams + stream mode, mix (volume, pitch, randoms, bus), spatial, limits, fades, ducking, follow_bullet |
| Volley arming | `bullet_volley/bullet_volley2d_sound.cpp` | `sound_set_effects` (factory volleys), `sound_arm_from_spawner` (spawner volleys); trigger sites call `sound_fire`, On Flight calls `sound_fire_flight` from the tick |
| Factory hatch | `factory/bullet_factory2d_sound.cpp` | `sound_offer` (volleys and spawner), `play_sound` (manual), debug readouts |
| Mixer | `factory/sound_mixer2d.*` | Channels per resource (shared by every spawner using it), bounded per-sweep candidates (nearest wins), voice pool (one AudioStreamPlayer2D per voice, polyphony 1), `flush` at the end of the factory sweep |
| Spawner | `bullet_spawner/bullet_spawner2d_sound.cpp` | Sound group: `sound_effects`, `sound_volume_db`, listener source (reuses `GrazeDetector2D`, THE target filter), On Shot / On Telegraph |
| Cosmetic RNG | `core/cosmetic_rng2d.hpp` | PCG32 for mix and effect-layer rolls only; gameplay keeps Godot's global RNG |

### 21.2 Contract

- Zero cost until armed: a trigger site tests one mask bit; an unarmed volley makes no offers.
- Offers never play inside the sweep: `flush` plays the winners after every volley, effect and timer of the sweep (at most one tick of latency).
- A channel's `min_interval_sec` and `max_voices` are shared by every spawner and volley using the resource. Nearest wins each sweep.
- Fading voices hold their pool slot but never count toward `max_voices` and are never restarted. A full pool steals the oldest fading voice first, then starts the oldest live voice's fade (that one play waits one sweep).
- `follow_bullet`: one live voice per bullet and entry; the voice rides the bullet and fades when the bullet ends.
- Cosmetic rolls (volume, pitch, pan, chance, stream picks, effect starts) draw from `CosmeticRng2D`. Tests reseed it with `BulletFactory2D.debug_seed_cosmetic_rng`.

### 21.3 Cost and open work

- Per tick, On Flight offers once per live bullet. A followed bullet also scans the voices of its entry (O(voices)) per offer. Measured with `sound_flight_10k` (10k bullets, followed hum): tick p50 2.03 ms before the per-sweep offer cache, 1.26 ms after (plain 10k flight is about 0.28 ms). `sound_hit_storm` (On Hit on every wall hit) is unchanged at about 0.15 ms tick.
- Ducking ranks the busy voices once per sweep (one sort, O(V log V)); equal priorities never dip each other. Duck timing: a duck applies on the sweep after its voices start.
- Offers still do per-bullet work (channel lookup, candidate copy, follow dedup over the entry's voices). The per-sweep cache removed the engine calls; the rest is the next target if the flight cost matters.

### 21.4 Tests

`test_project/tests/sound/` (13 suites plus `test_sound_rng_isolation`). Mixer rules live in `test_sound_mixer_unit`, flight and hum rules in `test_sound_flight`, spatial and camera in `test_sound_spatial_limits` and `test_sound_camera`.

