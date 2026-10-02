# BlastBullets2D benchmark: latest run

- run: `2026-10-02T16-11-40Z` commit `b366c8a` (dirty src/)
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.8-2-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 3 (median)
- baseline: none (run with --update-baseline)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = whole engine frame; `tick` = BulletFactory2D physics tick only.

| scenario | frame p50 | frame p99 | frame max | tick p50 | tick p99 | bullets | mem peak MB | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| attachments_500 | 0.049 | 0.242 | 1.067 | 0.026 | 0.154 | 453 | 32.7 | new |
| block_10k_flight | 0.496 | 0.553 | 2.182 | 0.214 | 0.244 | 10000 | 36.5 | new |
| bounce_box_2k | 0.123 | 0.153 | 0.353 | 0.053 | 0.060 | 2000 | 30.3 | new |
| cold_spawn_scaling | 0.006 | 0.015 | 0.015 | 0.000 | 0.001 | 0 | 51.1 | new |
| collision_storm | 4.222 | 7.799 | 9.043 | 1.302 | 3.066 | 31240 | 58.5 | new |
| dir_10k_flight | 0.558 | 0.670 | 1.693 | 0.263 | 0.278 | 10000 | 36.7 | new |
| homing_2k_moving_targets | 0.171 | 0.191 | 0.207 | 0.097 | 0.110 | 2000 | 30.3 | new |
| kitchen_sink | 0.486 | 1.366 | 1.966 | 0.269 | 0.349 | 7488 | 36.6 | new |
| mass_expiry_10k | 0.580 | 13.523 | 218.179 | 0.260 | 1.992 | 7375 | 37.0 | new |
| pool_churn | 1.099 | 1.231 | 2.898 | 0.301 | 0.340 | 9000 | 36.3 | new |
| spawner_moving_preview_1500 | 1.240 | 45.148 | 48.596 | 0.304 | 0.640 | 11550 | 40.2 | new |
| spawner_spin_preview_1500 | 0.942 | 51.018 | 53.979 | 0.309 | 0.676 | 11550 | 48.0 | new |
| trails_fx_2k | 0.381 | 5.825 | 6.130 | 0.309 | 0.960 | 1967 | 30.4 | new |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=19.609, cold_2000_ms=76.946, cold_4000_ms=316.833, cold_8000_ms=1544.605, scaling_8k_over_1k=78.941

## Scenarios

- **attachments_500**: 500 bullets with node attachments, refired every 90 frames
- **block_10k_flight**: 10k block bullets in flight (rigid volleys)
- **bounce_box_2k**: 2k bullets bouncing inside a box (bounce resolution every hit)
- **cold_spawn_scaling**: cold (pool-miss) spawn time for 1k/2k/4k/8k-bullet volleys
- **collision_storm**: 200-bullet volley/frame dying on walls (collision drain + pool churn)
- **dir_10k_flight**: 10k directional bullets in flight, no overlaps
- **homing_2k_moving_targets**: 2k homing bullets chasing 4 moving targets
- **kitchen_sink**: 3 spawners (spin spiral, fan, homing ring) + moving player + walls
- **mass_expiry_10k**: 10k bullets expiring on the same frame (3 waves)
- **pool_churn**: one 300-bullet volley per frame, 0.5 s life (warm-pool spawn + expiry)
- **spawner_moving_preview_1500**: 1500-bullet spawner on a moving parent, runtime preview on, firing 4/s
- **spawner_spin_preview_1500**: 1500-bullet heart spawner, spin + runtime preview, firing 4/s
- **trails_fx_2k**: 2k bullets with trail + spawn-flash layers, refired every 60 frames
