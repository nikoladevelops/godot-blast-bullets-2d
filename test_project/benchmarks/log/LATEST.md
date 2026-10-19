# BlastBullets2D benchmark: latest run

- run: `2026-10-19T11-56-02Z` commit `56a69bf` (dirty src/)
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.10-1-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `cedbd11` (2026-10-06T05-29-10Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| hit_handlers_2k | 1.310 | 1.730 | 2.724 | 3.198 | 0.691 | 3.143 | 0.352 | 0.563 | 1870 | 37.1 | +318 | **REGRESSION**: factory_tick_ms.p50 0.297->0.352 ms (+19%) |
| spawner_warm_shot_5k | 13.308 | 18.000 | 20.246 | 21.608 | 3.553 | 18.649 | 3.466 | 5.666 | 90000 | 159.9 | +0 | **REGRESSION**: frame_ms.p95 14.745->18.000 ms (+22%); factory_tick_ms.p95 3.594->4.612 ms (+28%) |

## Scenario extras

- **hit_handlers_2k**: handler_calls=38157, healed=4769

## Scenarios

- **hit_handlers_2k**: 100-bullet volley/frame into walls, a GDScript hit handler per bullet (live emit + kill decision)
- **spawner_warm_shot_5k**: 5000-bullet heart spawner, shoot_once every frame, warm pool
