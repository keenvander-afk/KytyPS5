# Working on this fork (keenvander-afk/KytyPS5)

This is our fork of KytyPS5, the PS5 emulator. The goal is maximum compatibility
and full frame rate. Research, strategy and upstream intel live in
https://github.com/keenvander-afk/PS5Emu (docs/LANDSCAPE.md, docs/STRATEGY.md,
docs/TEST-RIG.md, data/LATEST.md).

## The loop (on the Windows 11 test rig)

1. Build: `.\tools\bench\build-windows.ps1`. It loads the VS 2022 x64 env and
   uses clang-cl + Ninja, like upstream CI.
2. Smoke test: run `tools\bench\testdata\sharpemu-demo`, a legal homebrew that
   plays itself.
3. Measure: `python tools\bench\kyty_bench.py --emu _Build\windows\install\kyty_emulator.exe --suite tools\bench\suite.toml`.
   Results go to `bench-results\<title>\<timestamp>\`: `frames.csv`, the
   summary JSON, `emulator.log` and stdout, plus `bench-results\history.jsonl`.
4. Diagnose from `emulator.log` (unresolved imports, shader translation
   failures, asserts) and from the frame-time CSV (stutter spikes usually mean
   shader or pipeline compilation, or readbacks).
5. Change one thing, rebuild, and re-measure the same titles. Keep a change only
   when avg FPS or 1% low improves with no regression elsewhere in the suite.
6. Commit with the before/after numbers in the message and push a branch.

## Rules

- Game dumps are the user's own and live outside the repo (e.g. `D:\ps5\`).
  Never copy, upload or commit them, and never commit `bench-results\`.
- Never download games or firmware from anywhere.
- Upstream is KytyPS5/KytyPS5. Keep fork-only changes small and isolated (new
  files, one-line hooks) so upstream merges stay easy.
- Code from PS5PCEM (GPL-3.0) must not be copied in. SharpEmu, AnyPS5 and
  shadPS4 are GPL-2.0-compatible but need attribution.
- The upstream CMake reconfigure bug: if CMake fails with "patch does not
  apply" for zarchive, delete `_Build\windows\_deps\zarchive_source-*` and
  re-run.
