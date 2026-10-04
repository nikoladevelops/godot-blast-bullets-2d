# BlastBullets2D benchmark: latest run

- run: `2026-10-04T05-01-51Z` commit `1fb7461`
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.8-2-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `5ed265d` (2026-10-02T16-53-28Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| attachments_500 | 0.044 | 0.048 | 0.143 | 0.360 | 0.242 | 0.142 | 0.025 | 0.111 | 453 | 32.9 | +0 | frame p50 -4%, tick p50 +0% |
| bounce_box_2k | 0.115 | 0.122 | 0.132 | 0.137 | 0.001 | 0.137 | 0.054 | 0.060 | 2000 | 29.7 | +0 | frame p50 -8%, tick p50 -2% |
| cold_spawn_scaling | 0.004 | 0.004 | 0.005 | 0.005 | 0.001 | 0.005 | 0.000 | 0.000 | 0 | 51.2 | +0 | frame p50 +0%, tick p50 +0% |
| collision_storm | 0.681 | 1.285 | 1.375 | 2.415 | 0.361 | 2.340 | 0.147 | 0.320 | 1400 | 30.4 | +0 | frame p50 -8%, tick p50 -18% |
| factory_warm_spawn_5k | 13.935 | 16.524 | 17.635 | 18.591 | 3.368 | 16.740 | 3.646 | 5.627 | 90000 | 69.6 | +0 | frame p50 -19%, tick p50 -15% |
| homing_2k_moving_targets | 0.168 | 0.182 | 0.194 | 0.283 | 0.005 | 0.281 | 0.103 | 0.123 | 2000 | 29.6 | +0 | frame p50 +1%, tick p50 +6% |
| kitchen_sink | 0.478 | 0.672 | 0.870 | 2.279 | 0.005 | 2.276 | 0.282 | 0.416 | 7488 | 33.0 | +171 | frame p50 -3%, tick p50 +2% |
| mass_expiry_10k | 0.585 | 0.790 | 4.790 | 5.749 | 3.312 | 2.598 | 0.270 | 1.441 | 7375 | 38.7 | +0 | frame p50 -1%, tick p50 +1% |
| pool_churn | 0.802 | 0.864 | 0.933 | 2.608 | 0.197 | 2.457 | 0.294 | 0.331 | 9000 | 33.8 | +0 | frame p50 -11%, tick p50 -6% |
| spawner_moving_preview_1500 | 0.565 | 1.423 | 2.676 | 3.304 | 0.004 | 3.303 | 0.321 | 0.577 | 11550 | 34.8 | +15 | frame p50 -56%, tick p50 +1% |
| spawner_path_move_1500 | 0.854 | 1.784 | 3.291 | 3.676 | 0.001 | 3.675 | 0.485 | 0.766 | 17780 | 37.6 | +15 | new |
| spawner_spin_preview_1500 | 0.584 | 1.669 | 4.683 | 9.225 | 0.002 | 9.224 | 0.323 | 0.608 | 11550 | 39.7 | +15 | frame p50 -39%, tick p50 +2% |
| spawner_warm_shot_5k | 14.270 | 16.818 | 17.766 | 18.592 | 3.969 | 16.420 | 3.830 | 5.743 | 90000 | 163.6 | +0 | new |
| trails_fx_2k | 0.359 | 0.623 | 3.716 | 4.095 | 3.411 | 2.087 | 0.287 | 0.693 | 1967 | 30.9 | +0 | frame p50 -11%, tick p50 -14% |
| volley_10k_flight | 0.556 | 0.584 | 0.626 | 2.264 | 0.002 | 2.264 | 0.270 | 0.308 | 10000 | 32.7 | +0 | frame p50 -1%, tick p50 +1% |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=0.618, cold_2000_ms=1.495, cold_4000_ms=3.677, cold_8000_ms=10.493, scaling_8k_over_1k=16.979
- **collision_storm**: records_per_frame=200.000
- **spawner_path_move_1500**: pattern_cache_hits=47, pattern_cache_misses=2, preview_rebuilds=1

## Scenarios

- **attachments_500**: 500 bullets with node attachments, refired every 90 frames
- **bounce_box_2k**: 2k bullets bouncing inside a box (bounce resolution every hit)
- **cold_spawn_scaling**: cold (pool-miss) spawn time for 1k/2k/4k/8k-bullet volleys
- **collision_storm**: 200-bullet volley/frame dying on walls (collision drain + pool churn)
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
- **volley_10k_flight**: 10k bullets in flight, no overlaps
