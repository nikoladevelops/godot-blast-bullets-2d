# BlastBullets2D benchmark: latest run

- run: `2026-10-06T17-49-06Z` commit `46e2f26`
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.9-1-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `cedbd11` (2026-10-06T05-29-10Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| pool_churn | 0.778 | 0.841 | 0.910 | 2.530 | 0.360 | 2.449 | 0.276 | 0.315 | 9000 | 33.6 | +0 | frame p50 +2%, tick p50 +3% |
| spawner_aimed_regen_2k | 2.667 | 2.815 | 4.536 | 4.675 | 2.072 | 4.244 | 1.164 | 1.340 | 36000 | 41.7 | +0 | frame p50 +2%, tick p50 +2% |
| spawner_warm_shot_5k | 13.468 | 16.591 | 18.667 | 20.093 | 3.471 | 18.395 | 3.522 | 5.450 | 90000 | 158.7 | +0 | frame p50 +6%, tick p50 +7% |

## Scenarios

- **pool_churn**: one 300-bullet volley per frame, 0.5 s life (warm-pool spawn + expiry)
- **spawner_aimed_regen_2k**: 2000-bullet aimed spawner regenerating its pattern every frame (uncacheable source)
- **spawner_warm_shot_5k**: 5000-bullet heart spawner, shoot_once every frame, warm pool
