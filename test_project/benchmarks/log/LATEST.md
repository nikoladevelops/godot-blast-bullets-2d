# BlastBullets2D benchmark: latest run

- run: `2026-11-06T13-38-10Z` commit `d999a6b` (dirty src/)
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.3.1-1-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `33f0df0` (2026-11-06T11-33-54Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| emitter_storm | 3.069 | 5.756 | 7.183 | 9.113 | 0.182 | 9.088 | 1.919 | 4.773 | 12532 | 55.0 | +0 | **REGRESSION**: frame_ms.p95 3.996->5.756 ms (+44%); factory_tick_ms.p95 2.481->3.846 ms (+55%) |
| spawner_warm_shot_5k | 12.966 | 18.059 | 19.505 | 21.855 | 1.426 | 20.809 | 3.525 | 5.796 | 90000 | 161.7 | +0 | frame p50 +7%, tick p50 +3% |

## Scenario extras

- **emitter_storm**: emitted=10212

## Scenarios

- **emitter_storm**: 256 emitter bullets firing 8-rings every 0.25 s (~8k children)
- **spawner_warm_shot_5k**: 5000-bullet heart spawner, shoot_once every frame, warm pool
