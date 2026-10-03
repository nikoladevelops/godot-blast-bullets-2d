# BlastBullets2D benchmark: latest run

- run: `2026-10-03T10-47-21Z` commit `0600057`
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.8-2-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `5ed265d` (2026-10-02T16-53-28Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| attachments_500 | 0.047 | 0.057 | 0.194 | 0.622 | 0.483 | 0.193 | 0.026 | 0.148 | 453 | 32.9 | +0 | frame p50 +2%, tick p50 +4% |
| block_10k_flight | 0.504 | 0.567 | 0.608 | 2.075 | 0.002 | 2.074 | 0.223 | 0.269 | 10000 | 32.6 | +0 | frame p50 +0%, tick p50 +2% |
| bounce_box_2k | 0.120 | 0.133 | 0.146 | 0.167 | 0.001 | 0.167 | 0.056 | 0.071 | 2000 | 29.7 | +0 | frame p50 -4%, tick p50 +2% |
| cold_spawn_scaling | 0.004 | 0.004 | 0.009 | 0.009 | 0.001 | 0.005 | 0.000 | 0.001 | 0 | 51.2 | +0 | frame p50 +0%, tick p50 +0% |
| collision_storm | 0.769 | 1.329 | 1.566 | 2.781 | 0.230 | 2.692 | 0.166 | 0.354 | 1400 | 30.5 | +0 | frame p50 +4%, tick p50 -8% |
| dir_10k_flight | 0.561 | 0.603 | 0.623 | 0.681 | 0.001 | 0.681 | 0.267 | 0.303 | 10000 | 32.8 | +0 | frame p50 -1%, tick p50 +0% |
| factory_warm_spawn_5k | 13.079 | 15.135 | 16.229 | 17.545 | 3.343 | 15.248 | 3.550 | 5.255 | 90000 | 69.6 | +0 | frame p50 -24%, tick p50 -17% |
| homing_2k_moving_targets | 0.164 | 0.173 | 0.177 | 0.206 | 0.004 | 0.205 | 0.099 | 0.108 | 2000 | 29.7 | +0 | frame p50 -2%, tick p50 +2% |
| kitchen_sink | 0.467 | 0.624 | 0.820 | 2.253 | 0.004 | 2.252 | 0.274 | 0.328 | 7488 | 33.1 | +171 | frame p50 -6%, tick p50 -1% |
| mass_expiry_10k | 0.575 | 0.744 | 4.560 | 5.209 | 2.817 | 2.573 | 0.260 | 1.413 | 7375 | 38.8 | +0 | frame p50 -3%, tick p50 -3% |
| pool_churn | 0.853 | 0.920 | 1.018 | 2.617 | 0.231 | 2.190 | 0.304 | 0.359 | 9000 | 33.9 | +0 | frame p50 -5%, tick p50 -3% |
| spawner_moving_preview_1500 | 0.577 | 1.523 | 2.829 | 3.516 | 0.006 | 3.515 | 0.323 | 0.595 | 11550 | 34.9 | +15 | frame p50 -55%, tick p50 +2% |
| spawner_path_move_1500 | 0.872 | 1.902 | 3.549 | 3.918 | 0.003 | 3.917 | 0.487 | 0.799 | 17780 | 37.7 | +15 | new |
| spawner_spin_preview_1500 | 0.588 | 1.760 | 4.885 | 9.572 | 0.002 | 9.572 | 0.319 | 0.623 | 11550 | 39.8 | +15 | frame p50 -39%, tick p50 +1% |
| spawner_warm_shot_5k | 13.438 | 15.648 | 17.208 | 18.340 | 3.974 | 16.294 | 3.761 | 5.673 | 90000 | 163.6 | +0 | new |
| trails_fx_2k | 0.377 | 0.749 | 3.616 | 3.800 | 3.163 | 0.803 | 0.310 | 0.705 | 1967 | 30.9 | +0 | frame p50 -7%, tick p50 -7% |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=0.684, cold_2000_ms=1.630, cold_4000_ms=4.127, cold_8000_ms=11.202, scaling_8k_over_1k=16.733
- **collision_storm**: records_per_frame=200.000
- **spawner_path_move_1500**: pattern_cache_hits=47, pattern_cache_misses=2, preview_rebuilds=1

## Scenarios

- **attachments_500**: 500 bullets with node attachments, refired every 90 frames
- **block_10k_flight**: 10k block bullets in flight (rigid volleys)
- **bounce_box_2k**: 2k bullets bouncing inside a box (bounce resolution every hit)
- **cold_spawn_scaling**: cold (pool-miss) spawn time for 1k/2k/4k/8k-bullet volleys
- **collision_storm**: 200-bullet volley/frame dying on walls (collision drain + pool churn)
- **dir_10k_flight**: 10k directional bullets in flight, no overlaps
- **factory_warm_spawn_5k**: 5000-bullet TypedArray spawn every frame (script API), warm pool
- **homing_2k_moving_targets**: 2k homing bullets chasing 4 moving targets
- **kitchen_sink**: 3 spawners (spin spiral, fan, homing ring) + moving player + walls
- **mass_expiry_10k**: 10k bullets expiring on the same frame (3 waves)
- **pool_churn**: one 300-bullet volley per frame, 0.5 s life (warm-pool spawn + expiry)
- **spawner_moving_preview_1500**: 1500-bullet spawner on a moving parent, runtime preview on, firing 4/s
- **spawner_path_move_1500**: 1500-bullet star spawner moving along a Path2D, preview on, firing 8/s
- **spawner_spin_preview_1500**: 1500-bullet heart spawner, spin + runtime preview, firing 4/s
- **spawner_warm_shot_5k**: 5000-bullet heart spawner, shoot_once every frame, warm pool
- **trails_fx_2k**: 2k bullets with trail + spawn-flash layers, refired every 60 frames
