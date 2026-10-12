# BlastBullets2D benchmark: latest run

- run: `2026-10-12T17-24-40Z` commit `6e657fc` (dirty src/)
- machine: AMD Ryzen 7 8840HS w/ Radeon 780M Graphics (16 threads), Linux-7.2.9-2-cachyos-x86_64-with-glibc2.44
- godot: 4.7.2-stable (arch_linux), debug build: True, repeats: 5 (median)
- baseline: `cedbd11` (2026-10-06T05-29-10Z)

Times are milliseconds of CPU per frame (simulated time, `--fixed-fps 60`).
`frame` = step + engine; `step` = the scenario's own plugin calls (spawns...);
`engine` = physics server step + factory tick + drain + render; `tick` = factory physics tick only.

| scenario | frame p50 | frame p95 | frame p99 | frame max | step max | engine max | tick p50 | tick p99 | bullets | mem peak MB | objects +/- | vs baseline |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| flock_10k_flight | 4.681 | 5.255 | 5.376 | 5.427 | 0.010 | 5.424 | 4.081 | 4.761 | 10000 | 33.3 | +0 | new |
| flock_2k_swarm | 1.372 | 1.429 | 1.453 | 1.487 | 0.008 | 1.485 | 1.249 | 1.313 | 2000 | 30.7 | +0 | new |
| flock_5k_stacked | 2.295 | 2.382 | 2.428 | 2.453 | 0.010 | 2.451 | 1.972 | 2.058 | 5000 | 34.2 | +0 | new |
| graze_10k_flight | 0.893 | 1.012 | 1.043 | 1.088 | 0.008 | 1.087 | 0.456 | 0.529 | 10000 | 33.3 | +0 | **REGRESSION**: frame_ms.p50 0.587->0.893 ms (+52%); frame_ms.p95 0.612->1.012 ms (+65%); factory_tick_ms.p50 0.294->0.456 ms (+55%) |
| trails_fx_2k | 0.539 | 0.913 | 5.473 | 5.627 | 4.678 | 0.981 | 0.433 | 0.822 | 1967 | 31.9 | +0 | **REGRESSION**: frame_ms.p50 0.341->0.539 ms (+58%); frame_ms.p95 0.589->0.913 ms (+55%); factory_tick_ms.p50 0.274->0.433 ms (+58%) |
| volley_10k_flight | 0.855 | 0.985 | 1.010 | 1.029 | 0.003 | 1.029 | 0.410 | 0.473 | 10000 | 33.3 | +0 | **REGRESSION**: frame_ms.p50 0.548->0.855 ms (+56%); frame_ms.p95 0.567->0.985 ms (+74%); factory_tick_ms.p50 0.259->0.410 ms (+58%) |

## Scenario extras

- **flock_10k_flight**: flock_pass_usec=3741, neighbor_checks=370801
- **flock_2k_swarm**: flock_pass_usec=1030, neighbor_checks=72174
- **flock_5k_stacked**: flock_pass_usec=1527, neighbor_checks=165966
- **graze_10k_flight**: grazes=500

## Scenarios

- **flock_10k_flight**: 10k bullets in flight, 10 Per Volley flocks (dense, capped)
- **flock_2k_swarm**: 2k bullets, one Shared flock (default rules)
- **flock_5k_stacked**: 5k bullets stacked on one point, one flock (cap bound)
- **graze_10k_flight**: 10k bullets in flight, graze armed (3 rings, 1 moving target)
- **trails_fx_2k**: 2k bullets with trail + spawn-flash layers, refired every 60 frames
- **volley_10k_flight**: 10k bullets in flight, no overlaps
