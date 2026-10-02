# BlastBullets2D benchmark: latest run

- run: `2026-10-02T16-32-08Z` commit `aef9c17` (dirty src/)
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.8-2-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `b366c8a` (2026-10-02T16-11-40Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = whole engine frame; `tick` = BulletFactory2D physics tick only.

| scenario | frame p50 | frame p99 | frame max | tick p50 | tick p99 | bullets | mem peak MB | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| attachments_500 | 0.048 | 0.196 | 0.933 | 0.026 | 0.147 | 453 | 32.5 | frame p50 -2%, tick p50 +0% |
| block_10k_flight | 0.498 | 0.604 | 2.231 | 0.216 | 0.246 | 10000 | 32.4 | frame p50 +0%, tick p50 +1% |
| bounce_box_2k | 0.120 | 0.145 | 0.171 | 0.054 | 0.068 | 2000 | 29.6 | frame p50 -2%, tick p50 +2% |
| cold_spawn_scaling | 0.006 | 0.016 | 0.016 | 0.000 | 0.001 | 0 | 51.1 | frame p50 +0%, tick p50 +0% |
| collision_storm | 3.608 | 6.177 | 6.749 | 1.258 | 2.779 | 31240 | 43.8 | frame p50 -15%, tick p50 -3% |
| dir_10k_flight | 0.558 | 0.618 | 2.264 | 0.265 | 0.294 | 10000 | 32.6 | frame p50 +0%, tick p50 +1% |
| homing_2k_moving_targets | 0.165 | 0.183 | 0.203 | 0.097 | 0.110 | 2000 | 29.6 | frame p50 -4%, tick p50 +0% |
| kitchen_sink | 0.476 | 0.958 | 2.350 | 0.271 | 0.356 | 7488 | 33.0 | frame p50 -2%, tick p50 +1% |
| mass_expiry_10k | 0.584 | 13.634 | 35.662 | 0.261 | 2.031 | 7375 | 33.3 | frame p50 +1%, tick p50 +0% |
| pool_churn | 1.110 | 2.410 | 2.921 | 0.309 | 0.363 | 9000 | 32.5 | frame p50 +1%, tick p50 +3% |
| spawner_moving_preview_1500 | 0.562 | 2.886 | 3.528 | 0.313 | 0.660 | 11550 | 35.0 | frame p50 -55%, tick p50 +3% |
| spawner_spin_preview_1500 | 0.580 | 4.810 | 9.330 | 0.315 | 0.687 | 11550 | 39.8 | frame p50 -38%, tick p50 +2% |
| trails_fx_2k | 0.385 | 5.591 | 5.975 | 0.313 | 0.965 | 1967 | 29.7 | frame p50 +1%, tick p50 +1% |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=0.656, cold_2000_ms=1.466, cold_4000_ms=3.732, cold_8000_ms=11.146, scaling_8k_over_1k=17.154

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
