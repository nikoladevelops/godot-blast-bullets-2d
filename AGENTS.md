# AGENTS.md — BlastBullets2D agentic workflow

Godot 4 GDExtension (C++ via godot-cpp) + GUT headless suites. Stability and
performance are Priority 1; every behavior change must be verified by an
executed test. Read the section you need before touching `src/` or tests.

## 1. Running tests (only supported path)

```sh
python3 tools/run_tests.py                  # every suite, leak-checked
python3 tools/run_tests.py --suite volley    # substring/glob filter (repeatable)
python3 tools/run_tests.py --changed-only    # files plausibly affected by uncommitted changes
python3 tools/run_tests.py --fail-fast       # stop scheduling after first red file
python3 tools/run_tests.py --list            # discover without running
python3 tools/run_tests.py --self-test       # canaries must report FAIL + LEAK
python3 tools/run_tests.py --no-leaks        # faster, NOT green (skips leak gate)
python3 tools/run_tests.py --report          # + test_project/test_results/summary.json (slowest first)
python3 tools/run_tests.py --realtime        # real-time pacing (default is simulated time, see below)
python3 tools/lint_tests.py                  # static test lint (the runner runs it first; exit 3 = lint failed)
```

Time is SIMULATED: the runner passes `--fixed-fps 60`, so every frame
advances exactly 1/60 s and runs as fast as the CPU allows. Frame-counted
tests behave identically to real time; the full suite runs in ~6 s.

A file is green only when ALL hold: process exits 0, GUT JUnit shows
0 failures (missing report = CRASH, never PASS — catches a stale `.so`),
`--verbose` exit report has no leaks, no `SCRIPT ERROR`. Never run test
files directly with `--script`; always go through the runner (class-cache
refresh, per-file processes, JUnit + leak verdicts).

## 2. Leak detection

- Runner passes `--verbose` to headless Godot and fails files matching:
  `ObjectDB instances leaked`, `Leaked instance:`, `RID ... leaked`,
  `resources still in use`, `Orphan StringName`.
- `BlastTest.after_each` asserts `debug_assert_no_dangling()` + zero new
  orphans after EVERY test — a leak fails at the test, not at exit.
- Deliberate leakers live in `test_project/tests_meta/` and run ONLY via
  `--self-test` (they must be reported, never green).

## 3. Searching (use `rg`, not `grep`)

`rg` (ripgrep) is installed and an order of magnitude faster here.
Repo-correct patterns:

```sh
rg --files test_project/tests -g 'test_*.gd'    # suite inventory (-g globs the BASENAME)
python3 tools/run_tests.py --list               # same inventory, as the runner sees it
rg -n "push_error|emit_signal" src/bullets/     # fail-loud sites (pin exact text in tests)
rg -n "^func test_" test_project/tests/volley/test_volley_bounce.gd
```

## 4. GUT framework reference (what's available, what's used)

- **Discovery**: `.gutconfig.json` points at `res://tests/`; file must be
  named `test_*.gd` and extend `GutTest` (via `BlastTest`). Each file runs
  in its OWN Godot process (no cross-file state); tests inside a file run
  sequentially in one scene.
- **Lifecycle**: `before_all` / `before_each` / `after_each` / `after_all`.
  `BlastTest.before_each` builds a fresh `factory` (+2 idle frames);
  `after_each` idles, asserts a dangling-free factory, `reset()`s, and
  asserts zero new orphans. Override `before_each` with `await super()`
  first when you need more fixtures.
- **Asserts** (`test.gd`): `assert_eq/ne`, `assert_almost_eq/ne`,
  `assert_gt/gte/lt/lte`, `assert_true/false`, `assert_between`,
  `assert_has`, `assert_has_method`, `assert_null/not_null`,
  `assert_freed/not_freed`, `assert_no_new_orphans`, `assert_property*`,
  `assert_string_contains`, `assert_connected`, `assert_has_signal`,
  `assert_is/typeof`, `assert_eq_deep`. Prefer the TYPED assert matching
  the comparison (failure output shows got/expected).
- **Signals**: `watch_signals(obj)`, `assert_signal_emitted`,
  `assert_signal_not_emitted`, `assert_signal_emit_count(obj, name, n)`,
  `assert_signal_emitted_with_parameters`, `get_signal_emit_count`,
  `get_signal_parameters`. Use counts (not just emitted) for exactly-once
  contracts like `shooting_started/stopped`.
- **Parameters**: `func test_x(i: int = use_parameters([...]))` runs once
  per value with full before/after_each isolation (used for the 14-shape
  preview sweep and the 33-source pattern sweep). NOTE: this GUT version
  groups executions under one `<parameterized>` JUnit case — isolation is
  real, per-value reporting is not.
- **Liveness rule (C++)**: never carry a raw `Object*` across user code or
  `call_deferred` (the binder converts Variant args BEFORE your body's id
  check runs). Capture `get_instance_id()` BEFORE the user signal/callback
  and compare `ObjectDB::get_instance(id) == live_slot_pointer` afterwards
  (see `slot_still_holds_attachment_id`). `integration/test_reentrant_frees`
  is the regression net: handlers that `free()` mid-signal.
- **Doubles** (`doubler`/`spy`/`stubber`, `assert_called*`): available but
  UNUSED in this repo. Reach for them when a handler must be observed
  without side effects (e.g. counting spawner callbacks without firing).
- **Strict errors**: any `push_error`/engine error not claimed by
  `expect_error*` fails the test. `get_errors()` + `err.handled` is the
  mechanism; `swallow_errors()` marks all handled (fuzz allowlist only,
  enforced by `tools/lint_tests.py`).

## 5. Test doctrine (`test_project/tests/common/blast_test.gd`)

- Every suite: `extends BlastTest`. Builders: `spawn_dir()`,
  `make_spawner()`, `make_preview_spawner()`, `H` (shared spawn-data
  builders), `make_wall/make_area/make_probe_scene/add` (autofreed).
- Frames: `await idle(n)` (idle frame — structural factory calls
  `reset/free_*/populate_*` allowed) vs `await physics(n)` (INSIDE the
  physics step — structural calls rejected; call `idle()` first). NEVER
  GUT's `wait_*_frames` (resumes after n+1 frames, skews every count).
- Strict mode: any undeclared `push_error`/engine error fails the test.
  Rejections are loud by contract — pin them RIGHT AFTER the hostile call:
  `expect_error_sequence(["exact text", ...])` (exact count + order +
  wording; the preferred form), `expect_error(text)`,
  `expect_errors_containing(text, N)` (EXACTLY N; `at_least=true` needs a
  `# lint: at-least <reason>` comment), `expect_no_errors()` (checkpoint).
  `swallow_errors()` is ONLY for the lint allowlist (crash-proof fuzz).
  End-of-test lumps hide bugs: strict pinning once exposed two bounce tests
  that silently ran with a setter-rejected value (`bounce_cooldown_sec = 5.0`
  is outside [0, 1]).
- `Array(...)`-wrap engine arrays before `assert_eq` against literals.

## 6. Writing good tests (checklist)

- Name: `test_<behavior>_<expectation>` (`test_paused_steady_overlap_drops_records_by_design`).
- One behavior per test; shared builders over local setup; no magic
  numbers without a comment (frame budgets, tolerances, layer values).
- Failing-first: write the test, watch it fail for the RIGHT reason
  (assert vs unexpected-error), then fix, then keep it.
- Determinism: never wall-clock sleeps; frame counts with margins
  (prefer early-break loops `for i in N: await physics(); if cond: break`
  over fixed waits); `seed()` any randomness you assert on.
- Each `push_error` the test triggers gets an `expect_*` with the EXACT
  engine wording (copy from a failing run, then verify it fires where you
  think — e.g. rejection at spawn vs at assignment).
- New API surface: `has_method` bind check + happy path + reject-and-keep
  + OOB + pool-reuse neutrality (spawn → pool → respawn, state must not
  leak across lives).

## 7. Integration tests & feature mixing

- Harness pattern: `make_spawner(data, source, n)` + `make_wall(pos)` /
  `Area2D` on known layers + `watch_signals` on factory/spawner/volley.
  `make_wall` defaults to layer value 4 (matches `H.make_*_data` masks);
  the bounce suite uses its own convention (bounce wall 8, plain 16).
- A spawner you add to the tree auto-fires by default: create test
  spawners with `make_spawner()` or call `set_shooting_enabled(false)`
  BEFORE `add()`, otherwise it fires (and errors) every interval.
- Physics tests need REAL frames: spawn → `await physics(k)` with
  early-break on the observed condition; assert counts, signals, AND
  finiteness (`_finite` helpers).
- Feature crosses: combine 2–4 systems (homing+wobble+gravity+curves),
  assert each system's signature survives the mix + full finiteness +
  pool-reuse neutrality. Pairwise over the feature list beats exhaustive.
- Preview/gizmo tests: `make_preview_spawner`, `await idle(6)` for
  rebuilds, `debug_check_layer_coincidence(tol)` + `debug_get_layer_rings`.
- Spawner-owned vs factory-owned routing: connect BOTH signal sets, assert
  the silent side stayed silent.

## 8. New-feature checklist (do all of these)

1. Bind check (`has_method`) + happy-path test.
2. Setter reject-and-keep: NaN/Inf/OOB/negative/inverted, old value kept,
   exact error text pinned.
3. Inspector: correct `ADD_GROUP`, visible in the right pattern modes
   (`test_spawner_property_visibility` pattern), hint strings byte-exact,
   serialized-int enums locked (see `test_spawner_pattern_source_lock`).
4. Pool-reuse neutrality + no-dangling (automatic via `after_each`, but
   add explicit state assertions).
5. Signals: emission counts incl. negative cases (stays silent).
6. `doc_classes/<Class>.xml` updated (see §10), `tests/README.md` entry.
7. Audit-table entry if it fixes a reported bug (see
   `test_audit_regressions.gd` style: one pinned check per fix).

## 9. Inspector groups & serialization locks

- Groups come from `ADD_GROUP("Name", "")` in `_bind_methods`
  (spawner: Bullet Patterns/Shooting/Spin/Homing/Orbiting/Preview;
  spawn-data: Bullets/Appearance/Collision/Attachments/Sprite
  Effects/Per-Bullet Rotation). Tests assert no duplicate group titles.
- `helper_*` visibility is PER-MODE (gated polygon); the visibility suite
  fails on any property invisible in all 33 modes.
- Enum ids are serialized into `.tscn`: renumbering silently repoints
  saved scenes. `pattern_source` ids AND pool-key shape ids
  (`Circle:3,Rectangle:4,Capsule:5`) are locked by tests — extend the lock
  for any new serialized enum.

## 10. Documentation generation (loss + reorder hazards — verified)

- `python3 tools/generate_xml_docs.py` (or `setup.py` → Plugin
  Configuration) runs `godot --doctool <repo> --gdextension-docs` with CWD
  at the test project. The tool REGENERATES xml from the LOADED binary:
- Requirement 1 — close the editor first (open editor locks the binary,
  SCons can't update it, doctool misses custom classes and your edits
  describe ghosts).
- Requirement 2 — rebuild FIRST (`compile_debug_build.py`) so `bin/`
  holds the new binary, otherwise new bindings are absent from output.
- The output OVERWRITES `doc_classes/*.xml`: prose in generated sections
  is lost on regen, and member order follows the tool, not your file —
  keep custom text in description fields only, re-verify the diff after
  every regen, and never hand-reorder expecting it to stick.
- BBCode: `[Class]`, `[method C.m]`, `[member C.p]`, `[param x]`,
  `[constant C]`, `[code]`, `[codeblock]`, `[b]`, `[i]`.

## 11. `tools/` + `setup.py` catalog

- `setup.py`: interactive menu (Godot paths/versions, project folder,
  rename, icons, docs, debug/release builds, profiles, LTO, export zip,
  tutorials). First stop for a new machine.
- Build: `compile_debug_build.py` / `compile_release_build.py` /
  `clean_build.py` / `select_build_profile.py` / `edit_build_profile.py`
  / `change_lto_mode.py` / `toggle_editor_target.py` /
  `toggle_debug_symbols.py` / `toggle_reloadable.py` (hot reload).
- Config: `select_godot_path.py`, `select_godot_project.py`,
  `change_godot_target_version.py`, `update_godot_cpp.py`,
  `config_manager.py` + `config.json` (machine state — don't commit
  personal paths), `paths.py` (canonical locations).
- Plugin: `renaming.py`, `update_icons.py`, `export_plugin.py`
  (asset-store zip), `generate_xml_docs.py`, `gdextension_file_helper.py`,
  `apple_helpers.py`, `git_helpers.py`, `scons_helpers.py` +
  `scons_build_helpers.py`, `tutorials.py`.

## 12. Architecture primer (read before debugging)

- **Spawn flow**: `BulletFactory2D.spawn_controllable_*` (GDScript entry)
  → pool pop by `MultiMeshPoolKey2D` (amount + shape + type) or fresh
  alloc → `enable_multimesh(data, ...)` →
  `set_up_bullet_instances` + `generate_multimesh` (`enable_multimesh`
  validates everything BEFORE mutating, so a refused enable changes
  nothing). Spawner `shoot_once()`
  funnels through the same guarded path (homing/orbit/signals consistent).
- **Pooling**: one bucket per key; pop prefers newest non-ticked volley
  (`is_being_ticked` skips volleys mid-sweep); same-key spawns from
  handlers reuse mid-sweep, foreign volleys never silently reused.
  Structural ops (`reset/free_*/populate_*`) are idle-frame only: called
  inside a physics frame they are REJECTED loudly (`reject_when_iterating`).
  Their `*_deferred` twins queue through `queue_structural_call` (flushed
  on the next process_frame, in order) and are safe from anywhere.
- **Lifetimes**: `reduce_lifetime` ticks down → `disable_bullet` per slot
  → last-out funnels to pool. With `life_time_over` signals armed, the
  volley is HELD out of the pool (`lifetime_flush_pending`) until the
  deferred signal + attachment releases flush (kills the same-frame-reuse
  generation-bump signal loss).
- **Collision pipeline**: physics server area callbacks
  (`area/body_entered_func`, ADDED only) → dedup window (object-level,
  O(1) hash; shape-level opt-out) → `all_collided_bullets` records with
  queue-time epochs/velocity/pose → per-tick drain → counting → signals →
  bounce (radial default; precise shape-analytic with radial/head-on
  fallbacks). A paused factory DROPS overlap records (anti-hitch).
- **Spawner loop**: `_process` runs iff `needs_process()` (`shooting ||
  spin || retarget || preview || burst/telegraph/pattern pending`; grep
  `needs_process` in bullet_spawner2d.cpp); `shooting_*`
  SIGNALS track auto-shooting transitions only (subsystem wakes are
  signal-silent — pinned).
- **Per-bullet model**: entry `i` drives bullet `i`; per-bullet (valid) >
  shared (valid) > default; explicit all-zero = intent (presence, not
  absence); `tile_*` opts into wrap per array. Curves: clear FREEZES last
  sample (pinned characterization).
- **Preview**: editor + runtime gizmo rebuilt from the same resolvers as
  volleys; `debug_*` bindings expose rings/dots for coincidence tests.

## 13. Edge-case catalog (test these shapes of input everywhere)

NaN/Inf scalars and vectors; null array entries; empty arrays; short vs
oversized arrays; OOB indices (-1/99); inverted ranges; zero
amounts/sizes/speeds; singular transforms (zero scale, singular markers);
freed factory/generator/target/path mid-flight; pool reuse across lives;
pause/resume with overlaps in flight; 10k cap boundaries (10000 ok, 10001
rejected); maxed queues/timers (64); same-frame expiry+respawn; deferred
calls from collision handlers; teleport-into-wall; coincident aim/target;
zero-radius orbit; negative speeds under curves.
- First-spawn hitch: a cold 1500-bullet shot costs ~50ms (full area/RID
  alloc), pooled re-shots ~1ms. Pre-warm startup-critical patterns with
  `populate_bullets_pool(BulletFactory2D.debug_expected_pool_key(data),
  data, n)`; `free_active_bullets()` DESTROYS (cold again), only
  clear/expiry park (`test_spawner_spin_bench` proves the warm shot is a
  pool hit).

## 14. Performance rules (tick code is sacred)

- No per-bullet extension-boundary crossings in the tick (hoist sin/cos,
  inverses, strings; see `NodeInverseScope`, `CachedStringNames2D`).
- O(1)/amortized per bullet: dedup hash not scans, fast-path identical
  bases in interpolation, 4→2 curve samples via lap algebra.
- Allocations: reuse scratch buffers, `reserve()` vectors, swap-remove
  pools; never allocate per bullet per tick.
- Any new per-tick work needs a benchmark note + a test that still passes
  frame-budget-adjacent (bulk-count tests are the canary).

## 15. Rules (non-negotiable)

1. Failing-first: reproduce with a test that fails, then fix, then keep it.
2. Evidence before synthesis: read the code, cite `file:line`; never
   assert behavior you haven't executed.
3. Never weaken a test to fit the code — fix the code, or bring the
   contract question to the user with engine-code references.
4. Full suite green (`run_tests.py`, leaks on) + `--self-test` before
   finishing. End reports with Big-O + a ratings table.
