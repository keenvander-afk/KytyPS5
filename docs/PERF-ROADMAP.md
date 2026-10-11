# Performance roadmap

Ordered by expected impact on real titles. Each item names the evidence to
collect with `tools/bench/kyty_bench.py` before and after the change. Keep a
change only if the suite improves and nothing regresses.

The guest CPU code runs natively, so almost all of the gap to full speed is on
the GPU side.

## 1. Shader and pipeline compile stutter
*Evidence*: `spike_frames_with_compiles`, `shader_compile_ms`,
`pipeline_create_ms` in the bench summary (added in this fork).

- **Persistent translation cache.** `ProgramCache` (`src/graphics/host_gpu/renderer/pipeline/pipelineCache.cpp`)
  keeps RDNA→SPIR-V translations only in memory, so every session
  retranslates every shader on first use. Serialize
  `{ProgramKey, specialization, push-data start} -> {SPIR-V, CompiledShaderInfo}`
  per title, next to the driver `VkPipelineCache` that is already persisted.
  Prior art: Jetsku/KytyPS5 `KYTY_PROGRAM_CACHE`.
- **Translate once, specialize copies.** A source with a new resource
  specialization re-runs the full translation. Cache the translated IR per
  source and only re-run specialization and SPIR-V emission
  (Jetsku "Pipeline stutter 5/7").
- **Compile outside the draw path.** Move SPIR-V emission and `vkCreate*Pipeline`
  to worker threads and skip or defer the draw until the pipeline is ready,
  behind a setting, the way Dolphin and Yuzu do async shaders. SharpEmu v0.0.5
  already moved shader-plan compilation to the background.
- **Graphics pipeline libraries** (`VK_EXT_graphics_pipeline_library`): link
  pre-compiled stages quickly and optimize in the background
  (Jetsku "Pipeline stutter 6/7", `KYTY_PIPELINE_FAST_FIRST`).
- **Normalize pipeline keys.** Make state dynamic so it stops multiplying
  pipelines: cull mode and depth bounds are already dynamic in Jetsku 4/7.

## 2. CPU↔GPU memory coherence
*Evidence*: frame-time p50 in scenes without compiles, plus readback counts
in `emulator.log`.

- Replace whole-page readbacks with dirty-range tracking. Merge nearby readback
  regions (Jetsku `KYTY_READBACK_MERGE_GAP_KB`).
- Bound vertex and index uploads to the ranges the shader can actually reach.
  PS5PCEM reported 53→198 FPS from this in Dreaming Sarah (idea only: GPL-3,
  reimplement from scratch).
- Batch uploads behind one barrier pair (Jetsku "buffers: batch CPU-dirty uploads").
- Zero-copy host-visible memory on ReBAR/UMA hosts (SharpEmu v0.0.5
  "unified memory readback without copy").

## 3. Command processing overhead
*Evidence*: CPU time of the GPU thread (Tracy, `--profile`) on draw-heavy
scenes.

- Copy only the draw state a prepared draw uses (Jetsku `KYTY_RENDER_STATE_FAST`).
- Descriptor and binding reuse across draws with unchanged resources.
- Batch occlusion-query resets and reductions (Jetsku `KYTY_OCCLUSION_BATCH`).

## 4. Features that block whole titles
These are compatibility rather than speed, but they gate the big games: ray
tracing via `VK_KHR_ray_query` (GTA V and others), HDR→SDR, mixed MSAA sample
counts, tessellation, denormal preservation.

## Porting rules
Jetsku's branch shares history with upstream but conflicts heavily (it carries
its own diagnostics infrastructure). Port by reading the change and
reimplementing it against current upstream, one item per PR, each with bench
numbers. See `CLAUDE.md` for licensing.
