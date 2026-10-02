# BlastBullets2D benchmark: latest run

- run: `2026-10-02T16-56-21Z` commit `d5b6112`
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.8-2-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `5ed265d` (2026-10-02T16-53-28Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| attachments_500 | 0.045 | 0.055 | 0.206 | 0.485 | 0.331 | 0.206 | 0.025 | 0.139 | 453 | 32.8 | +0 | frame p50 -2%, tick p50 +0% |
| block_10k_flight | 0.498 | 0.547 | 0.595 | 0.621 | 0.002 | 0.620 | 0.218 | 0.249 | 10000 | 32.5 | +0 | frame p50 -1%, tick p50 +0% |
| bounce_box_2k | 0.118 | 0.132 | 0.142 | 0.154 | 0.001 | 0.154 | 0.054 | 0.070 | 2000 | 29.6 | +0 | frame p50 -6%, tick p50 -2% |
| cold_spawn_scaling | 0.004 | 0.004 | 0.005 | 0.005 | 0.001 | 0.004 | 0.000 | 0.001 | 0 | 51.1 | +0 | frame p50 +0%, tick p50 +0% |
| collision_storm | 0.701 | 1.314 | 1.365 | 2.548 | 0.233 | 2.240 | 0.155 | 0.347 | 1400 | 30.4 | +0 | frame p50 -5%, tick p50 -14% |
| dir_10k_flight | 0.559 | 0.597 | 0.616 | 0.647 | 0.001 | 0.646 | 0.265 | 0.302 | 10000 | 32.6 | +0 | frame p50 -1%, tick p50 -1% |
| factory_warm_spawn_5k | 15.058 | 19.095 | 19.864 | 21.330 | 5.160 | 17.443 | 3.917 | 5.514 | 90000 | 69.5 | +0 | frame p50 -13%, tick p50 -9% |
| homing_2k_moving_targets | 0.162 | 0.173 | 0.188 | 0.201 | 0.006 | 0.200 | 0.097 | 0.109 | 2000 | 29.6 | +0 | frame p50 -3%, tick p50 +0% |
| kitchen_sink | 0.467 | 0.639 | 0.809 | 1.858 | 0.006 | 1.851 | 0.274 | 0.354 | 7488 | 33.0 | +171 | frame p50 -6%, tick p50 -1% |
| mass_expiry_10k | 0.575 | 0.762 | 4.809 | 6.327 | 4.479 | 2.704 | 0.261 | 1.765 | 7375 | 38.7 | +0 | frame p50 -3%, tick p50 -3% |
| pool_churn | 0.809 | 0.881 | 1.340 | 2.523 | 0.813 | 2.399 | 0.298 | 0.351 | 9000 | 33.7 | +0 | frame p50 -10%, tick p50 -5% |
| spawner_moving_preview_1500 | 0.561 | 1.492 | 3.071 | 3.531 | 0.011 | 3.531 | 0.315 | 0.648 | 11550 | 34.8 | +15 | frame p50 -56%, tick p50 -1% |
| spawner_spin_preview_1500 | 0.588 | 1.874 | 4.880 | 9.245 | 0.002 | 9.245 | 0.318 | 0.712 | 11550 | 39.7 | +15 | frame p50 -39%, tick p50 +0% |
| spawner_warm_shot_5k | 14.070 | 19.110 | 21.236 | 22.952 | 3.840 | 20.785 | 4.022 | 5.999 | 90000 | 163.5 | +0 | new |
| trails_fx_2k | 0.388 | 0.788 | 3.824 | 4.030 | 3.368 | 0.862 | 0.319 | 0.818 | 1967 | 30.8 | +0 | frame p50 -4%, tick p50 -4% |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=0.620, cold_2000_ms=1.399, cold_4000_ms=3.813, cold_8000_ms=10.975, scaling_8k_over_1k=17.686
- **collision_storm**: records_per_frame=200.000

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
- **spawner_spin_preview_1500**: 1500-bullet heart spawner, spin + runtime preview, firing 4/s
- **spawner_warm_shot_5k**: 5000-bullet heart spawner, shoot_once every frame, warm pool
- **trails_fx_2k**: 2k bullets with trail + spawn-flash layers, refired every 60 frames
