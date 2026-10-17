# BlastBullets2D benchmark: latest run

- run: `2026-10-17T19-32-30Z` commit `f90321d` (dirty src/)
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.10-1-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `cedbd11` (2026-10-06T05-29-10Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| collision_storm | 0.742 | 1.033 | 2.261 | 2.600 | 0.223 | 2.509 | 0.155 | 0.269 | 1400 | 31.8 | +0 | frame p50 +9%, tick p50 +8% |
| hit_handlers_2k | 1.336 | 1.805 | 2.764 | 3.229 | 0.407 | 2.985 | 0.368 | 0.594 | 1870 | 37.1 | +318 | **REGRESSION**: factory_tick_ms.p50 0.297->0.368 ms (+24%) |

## Scenario extras

- **collision_storm**: records_per_frame=200.000
- **hit_handlers_2k**: handler_calls=38157, healed=4769

## Scenarios

- **collision_storm**: 200-bullet volley/frame dying on walls (collision drain + pool churn)
- **hit_handlers_2k**: 100-bullet volley/frame into walls, a GDScript hit handler per bullet (live emit + kill decision)
