# BlastBullets2D benchmark: latest run

- run: `2026-10-02T16-52-38Z` commit `0d968c9` (dirty src/)
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.8-2-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 3 (median)
- baseline: `b366c8a` (2026-10-02T16-11-40Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| collision_storm | 0.679 | 1.310 | 1.363 | 2.696 | 0.629 | 2.559 | 0.152 | 0.342 | 1400 | 30.4 | +0 | frame p50 -84%, tick p50 -88% |

## Scenario extras

- **collision_storm**: records_per_frame=200.000

## Scenarios

- **collision_storm**: 200-bullet volley/frame dying on walls (collision drain + pool churn)
