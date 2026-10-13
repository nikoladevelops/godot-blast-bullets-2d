# BlastBullets2D benchmark: latest run

- run: `2026-10-13T18-40-40Z` commit `d73c8da`
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.9-2-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `cedbd11` (2026-10-06T05-29-10Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| hit_handlers_2k | 1.345 | 1.758 | 1.932 | 2.654 | 0.403 | 2.598 | 0.362 | 0.630 | 1870 | 36.9 | +318 | **REGRESSION**: factory_tick_ms.p50 0.297->0.362 ms (+22%) |
| spawner_warm_shot_5k | 15.358 | 18.593 | 19.436 | 20.659 | 2.190 | 18.564 | 3.797 | 4.758 | 90000 | 159.6 | +0 | **REGRESSION**: frame_ms.p50 12.730->15.358 ms (+21%); frame_ms.p95 14.745->18.593 ms (+26%); factory_tick_ms.p50 3.286->3.797 ms (+16%); factory_tick_ms.p95 3.594->4.585 ms (+28%) |

## Scenario extras

- **hit_handlers_2k**: handler_calls=38157, healed=4769

## Scenarios

- **hit_handlers_2k**: 100-bullet volley/frame into walls, a GDScript hit handler per bullet (live emit + kill decision)
- **spawner_warm_shot_5k**: 5000-bullet heart spawner, shoot_once every frame, warm pool
