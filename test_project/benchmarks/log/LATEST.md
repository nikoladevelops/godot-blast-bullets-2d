# BlastBullets2D benchmark: latest run

- run: `2026-10-09T11-37-57Z` commit `1d4443f`
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.9-2-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `cedbd11` (2026-10-06T05-29-10Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| graze_10k_flight | 0.887 | 0.943 | 1.001 | 2.296 | 0.006 | 2.289 | 0.448 | 0.499 | 10000 | 32.8 | +0 | **REGRESSION**: frame_ms.p50 0.587->0.887 ms (+51%); frame_ms.p95 0.612->0.943 ms (+54%); factory_tick_ms.p50 0.294->0.448 ms (+52%) |

## Scenario extras

- **graze_10k_flight**: grazes=500

## Scenarios

- **graze_10k_flight**: 10k bullets in flight, graze armed (3 rings, 1 moving target)
