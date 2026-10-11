#ifndef EMULATOR_SRC_GRAPHICS_PRESENTATION_WINDOW_FRAMEBENCHMARK_H_
#define EMULATOR_SRC_GRAPHICS_PRESENTATION_WINDOW_FRAMEBENCHMARK_H_

#include <cstdint>

// Headless-friendly frame benchmark, enabled through environment variables so
// automated test runs can measure a title without touching the GUI:
//
//   KYTY_BENCH_SECONDS=N   measure N seconds of presented frames, then exit
//   KYTY_BENCH_WARMUP=N    ignore the first N seconds after the first frame (default 0)
//   KYTY_BENCH_OUT=path    write per-frame CSV to path and a summary to path.summary.json
//                          (default: kyty_bench.csv in the working directory)
//
// Without KYTY_BENCH_SECONDS every call is a cheap no-op.
namespace Libs::Graphics::FrameBenchmark {

// Presentation thread: called once per successfully presented frame.
void OnPresent();

// Main window loop: flushes samples and returns true when the run is complete
// and the emulator should exit.
bool Poll();

} // namespace Libs::Graphics::FrameBenchmark

#endif // EMULATOR_SRC_GRAPHICS_PRESENTATION_WINDOW_FRAMEBENCHMARK_H_
