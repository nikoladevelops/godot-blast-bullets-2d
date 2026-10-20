# BlastBullets2D benchmark: latest run

- run: `2026-10-20T06-09-58Z` commit `b50c08b` (dirty src/)
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.10-1-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `cedbd11` (2026-10-06T05-29-10Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| collision_storm | 0.774 | 1.055 | 1.356 | 2.533 | 0.508 | 2.386 | 0.171 | 0.293 | 1400 | 31.9 | +0 | **REGRESSION**: frame_ms.p50 0.682->0.774 ms (+13%) |
| graze_storm | 0.985 | 5.465 | 8.219 | 11.223 | 1.240 | 11.221 | 0.274 | 1.456 | 7867 | 39.5 | +0 | frame p50 +6%, tick p50 +15% |
| hit_handlers_2k | 1.332 | 1.813 | 2.689 | 3.050 | 0.533 | 2.978 | 0.383 | 0.623 | 1870 | 37.2 | +318 | **REGRESSION**: factory_tick_ms.p50 0.297->0.383 ms (+29%) |
| lifetime_signal_10k | 0.598 | 0.789 | 4.446 | 4.978 | 2.682 | 2.562 | 0.278 | 2.192 | 7375 | 39.7 | +0 | frame p50 +3%, tick p50 +6% |

## Scenario extras

- **collision_storm**: records_per_frame=200.000
- **graze_storm**: events=142000
- **hit_handlers_2k**: handler_calls=38157, healed=4769
- **lifetime_signal_10k**: bullets_listed=30000, handler_calls=30

## Scenarios

- **collision_storm**: 200-bullet volley/frame dying on walls (collision drain + pool churn)
- **graze_storm**: graze storm: 2k-bullet rings spawned on the player every 0.25 s
- **hit_handlers_2k**: 100-bullet volley/frame into walls, a GDScript hit handler per bullet (live emit + kill decision)
- **lifetime_signal_10k**: 10k bullets expiring on one frame with a live life_time_over handler per volley (3 waves)
