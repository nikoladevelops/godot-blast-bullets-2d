# BlastBullets2D benchmark: latest run

- run: `2026-10-04T05-38-46Z` commit `9076c9e` (dirty src/)
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.8-2-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `5ed265d` (2026-10-02T16-53-28Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| collision_storm | 0.694 | 1.292 | 2.236 | 2.517 | 0.325 | 2.380 | 0.144 | 0.316 | 1400 | 30.4 | +0 | frame p50 -6%, tick p50 -20% |
| mass_expiry_10k | 0.594 | 0.779 | 4.746 | 6.540 | 2.963 | 2.561 | 0.274 | 0.787 | 7375 | 38.7 | +0 | frame p50 +0%, tick p50 +2% |
| pool_churn | 0.793 | 0.850 | 0.925 | 2.557 | 0.123 | 2.448 | 0.276 | 0.318 | 9000 | 33.8 | +0 | frame p50 -12%, tick p50 -12% |

## Scenario extras

- **collision_storm**: records_per_frame=200.000

## Scenarios

- **collision_storm**: 200-bullet volley/frame dying on walls (collision drain + pool churn)
- **mass_expiry_10k**: 10k bullets expiring on the same frame (3 waves)
- **pool_churn**: one 300-bullet volley per frame, 0.5 s life (warm-pool spawn + expiry)
