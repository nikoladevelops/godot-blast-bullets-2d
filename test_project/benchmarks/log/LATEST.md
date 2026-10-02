# BlastBullets2D benchmark: latest run

- run: `2026-10-02T16-26-04Z` commit `5ed265d` (dirty src/)
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.8-2-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 3 (median)
- baseline: `b366c8a` (2026-10-02T16-11-40Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = whole engine frame; `tick` = BulletFactory2D physics tick only.

| scenario | frame p50 | frame p99 | frame max | tick p50 | tick p99 | bullets | mem peak MB | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| attachments_500 | 0.048 | 0.184 | 0.922 | 0.026 | 0.147 | 453 | 32.7 | frame p50 -2%, tick p50 +0% |
| block_10k_flight | 0.503 | 0.550 | 2.194 | 0.218 | 0.236 | 10000 | 36.5 | frame p50 +1%, tick p50 +2% |
| bounce_box_2k | 0.126 | 0.142 | 0.168 | 0.054 | 0.063 | 2000 | 30.3 | frame p50 +2%, tick p50 +2% |
| cold_spawn_scaling | 0.006 | 0.022 | 0.022 | 0.000 | 0.001 | 0 | 51.1 | frame p50 +0%, tick p50 +0% |
| collision_storm | 4.268 | 7.654 | 8.196 | 1.303 | 3.002 | 31240 | 58.5 | frame p50 +1%, tick p50 +0% |
| dir_10k_flight | 0.568 | 0.687 | 2.308 | 0.264 | 0.300 | 10000 | 36.7 | frame p50 +2%, tick p50 +0% |
| homing_2k_moving_targets | 0.170 | 0.183 | 0.188 | 0.097 | 0.103 | 2000 | 30.3 | frame p50 -1%, tick p50 +0% |
| kitchen_sink | 0.496 | 1.375 | 2.259 | 0.279 | 0.362 | 7488 | 36.6 | frame p50 +2%, tick p50 +4% |
| mass_expiry_10k | 0.589 | 13.978 | 216.283 | 0.264 | 2.025 | 7375 | 37.0 | frame p50 +2%, tick p50 +2% |
| pool_churn | 1.123 | 1.573 | 2.931 | 0.310 | 0.374 | 9000 | 36.3 | **REGRESSION**: frame_ms.p99 1.231->1.573 ms (+28%) |
| spawner_moving_preview_1500 | 0.570 | 45.254 | 48.392 | 0.312 | 0.647 | 11550 | 40.4 | frame p50 -54%, tick p50 +3% |
| spawner_spin_preview_1500 | 0.591 | 52.331 | 54.448 | 0.313 | 0.688 | 11550 | 48.3 | frame p50 -37%, tick p50 +1% |
| trails_fx_2k | 0.424 | 5.707 | 6.002 | 0.349 | 1.016 | 1967 | 30.4 | frame p50 +11%, tick p50 +13% |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=19.715, cold_2000_ms=79.059, cold_4000_ms=322.270, cold_8000_ms=1547.041, scaling_8k_over_1k=78.470

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
