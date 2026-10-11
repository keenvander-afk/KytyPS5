# Frame benchmark

`kyty_bench.py` drives the emulator's built-in frame benchmark
(`src/graphics/presentation/window/frameBenchmark.cpp`) so every change can be
measured on real titles.

The benchmark is controlled by environment variables, which the script sets:

| Variable | Meaning |
|---|---|
| `KYTY_BENCH_SECONDS` | Length of the measured window. Unset means the benchmark is off. |
| `KYTY_BENCH_WARMUP` | Seconds to skip after the first presented frame. |
| `KYTY_BENCH_OUT` | Per-frame CSV path. The summary goes to `<path>.summary.json`. |

The summary records average FPS, 1% low FPS, and p50, p99 and max frame time.
It also records how much shader translation and pipeline creation happened in
the measured window (`shader_compiles`, `shader_compile_ms`, `pipeline_creates`,
`pipeline_create_ms`). Spikes are frames slower than 2× the median, and
`spike_frames_with_compiles` counts how many of them overlap compile work. The
CSV has the same split per frame, so stutter can be blamed on compilation (or
cleared of it) before anyone guesses at a fix.
The emulator exits by itself when the window ends, or 5 s later if frames stop
arriving.

```bash
python tools/bench/kyty_bench.py --emu _Build/windows/install/kyty_emulator.exe \
    --game D:/ps5/PPSA01325 --name "Astro's Playroom" --warmup 90 --seconds 60
python tools/bench/kyty_bench.py --emu ... --suite tools/bench/suite.toml
```

Each run lands in `bench-results/<title>/<timestamp>/`, which holds the frame
CSV, the summary, the emulator log and the stdout, and is appended to
`bench-results/history.jsonl`. The script prints a table comparing each title
against its best previous run. Game dumps and results are git-ignored.
