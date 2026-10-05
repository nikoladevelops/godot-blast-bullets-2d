# BlastBullets2D benchmark: latest run

- run: `2026-10-05T10-36-56Z` commit `72ed46e`
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.9-1-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `5ed265d` (2026-10-02T16-53-28Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| attachments_500 | 0.046 | 0.052 | 0.127 | 0.364 | 0.239 | 0.127 | 0.027 | 0.078 | 453 | 32.9 | +0 | frame p50 +0%, tick p50 +8% |
| bounce_box_2k | 0.116 | 0.125 | 0.133 | 0.233 | 0.001 | 0.233 | 0.055 | 0.063 | 2000 | 29.7 | +0 | frame p50 -7%, tick p50 +0% |
| cold_spawn_scaling | 0.004 | 0.004 | 0.004 | 0.004 | 0.001 | 0.004 | 0.000 | 0.000 | 0 | 51.2 | +0 | frame p50 +0%, tick p50 +0% |
| collision_storm | 0.682 | 1.276 | 1.524 | 2.737 | 0.160 | 2.600 | 0.144 | 0.306 | 1400 | 30.5 | +0 | frame p50 -8%, tick p50 -20% |
| factory_warm_spawn_5k | 12.516 | 15.001 | 15.485 | 15.853 | 3.324 | 14.412 | 3.134 | 5.062 | 90000 | 69.7 | +0 | frame p50 -28%, tick p50 -27% |
| hit_handlers_2k | 1.415 | 2.088 | 2.851 | 3.546 | 0.881 | 3.308 | 0.315 | 0.592 | 1870 | 36.5 | +318 | new |
| homing_2k_moving_targets | 0.169 | 0.179 | 0.197 | 1.028 | 0.010 | 1.027 | 0.104 | 0.119 | 2000 | 29.7 | +0 | frame p50 +1%, tick p50 +7% |
| kitchen_sink | 0.472 | 0.622 | 0.941 | 2.152 | 0.004 | 2.151 | 0.279 | 0.332 | 7488 | 33.1 | +171 | frame p50 -5%, tick p50 +1% |
| lifetime_signal_10k | 0.581 | 0.763 | 4.783 | 5.439 | 3.177 | 2.389 | 0.268 | 2.124 | 7375 | 38.8 | +0 | new |
| mass_expiry_10k | 0.589 | 0.766 | 4.574 | 5.220 | 2.930 | 2.491 | 0.275 | 0.755 | 7375 | 38.8 | +0 | frame p50 -1%, tick p50 +3% |
| pool_churn | 0.783 | 0.840 | 0.897 | 2.573 | 0.812 | 2.457 | 0.275 | 0.325 | 9000 | 33.9 | +0 | frame p50 -13%, tick p50 -12% |
| spawner_aimed_regen_2k | 2.764 | 2.943 | 4.684 | 4.761 | 1.907 | 4.223 | 1.179 | 2.980 | 36000 | 43.4 | +0 | new |
| spawner_moving_preview_1500 | 0.567 | 1.299 | 2.628 | 3.284 | 0.004 | 3.284 | 0.324 | 0.457 | 11550 | 34.9 | +15 | frame p50 -56%, tick p50 +2% |
| spawner_path_move_1500 | 0.853 | 1.645 | 3.204 | 3.699 | 0.002 | 3.698 | 0.484 | 0.647 | 17780 | 37.7 | +15 | new |
| spawner_spin_preview_1500 | 0.581 | 1.519 | 4.652 | 9.027 | 0.002 | 9.026 | 0.321 | 0.477 | 11550 | 39.8 | +15 | frame p50 -40%, tick p50 +1% |
| spawner_warm_shot_5k | 13.572 | 16.050 | 17.188 | 17.959 | 3.666 | 16.035 | 3.409 | 5.416 | 90000 | 163.5 | +0 | new |
| trails_fx_2k | 0.353 | 0.611 | 3.605 | 3.841 | 3.245 | 1.638 | 0.285 | 0.554 | 1967 | 30.9 | +0 | frame p50 -13%, tick p50 -15% |
| volley_10k_flight | 0.556 | 0.588 | 0.623 | 2.253 | 0.002 | 2.253 | 0.271 | 0.298 | 10000 | 32.8 | +0 | frame p50 -1%, tick p50 +1% |

## Scenario extras

- **cold_spawn_scaling**: cold_1000_ms=0.600, cold_2000_ms=1.385, cold_4000_ms=3.601, cold_8000_ms=10.683, scaling_8k_over_1k=17.388
- **collision_storm**: records_per_frame=200.000
- **hit_handlers_2k**: handler_calls=38157, healed=4769
- **lifetime_signal_10k**: bullets_listed=30000, handler_calls=30
- **spawner_path_move_1500**: pattern_cache_hits=47, pattern_cache_misses=2, preview_rebuilds=1

## Scenarios

- **attachments_500**: 500 bullets with node attachments, refired every 90 frames
- **bounce_box_2k**: 2k bullets bouncing inside a box (bounce resolution every hit)
- **cold_spawn_scaling**: cold (pool-miss) spawn time for 1k/2k/4k/8k-bullet volleys
- **collision_storm**: 200-bullet volley/frame dying on walls (collision drain + pool churn)
- **factory_warm_spawn_5k**: 5000-bullet TypedArray spawn every frame (script API), warm pool
- **hit_handlers_2k**: 100-bullet volley/frame into walls, a GDScript hit handler per bullet (live emit + kill decision)
- **homing_2k_moving_targets**: 2k homing bullets chasing 4 moving targets
- **kitchen_sink**: 3 spawners (spin spiral, fan, homing ring) + moving player + walls
- **lifetime_signal_10k**: 10k bullets expiring on one frame with a live life_time_over handler per volley (3 waves)
- **mass_expiry_10k**: 10k bullets expiring on the same frame (3 waves)
- **pool_churn**: one 300-bullet volley per frame, 0.5 s life (warm-pool spawn + expiry)
- **spawner_aimed_regen_2k**: 2000-bullet aimed spawner regenerating its pattern every frame (uncacheable source)
- **spawner_moving_preview_1500**: 1500-bullet spawner on a moving parent, runtime preview on, firing 4/s
- **spawner_path_move_1500**: 1500-bullet star spawner moving along a Path2D, preview on, firing 8/s
- **spawner_spin_preview_1500**: 1500-bullet heart spawner, spin + runtime preview, firing 4/s
- **spawner_warm_shot_5k**: 5000-bullet heart spawner, shoot_once every frame, warm pool
- **trails_fx_2k**: 2k bullets with trail + spawn-flash layers, refired every 60 frames
- **volley_10k_flight**: 10k bullets in flight, no overlaps
