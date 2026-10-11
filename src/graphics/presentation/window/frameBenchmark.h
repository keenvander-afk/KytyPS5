#ifndef EMULATOR_SRC_GRAPHICS_PRESENTATION_WINDOW_FRAMEBENCHMARK_H_
#define EMULATOR_SRC_GRAPHICS_PRESENTATION_WINDOW_FRAMEBENCHMARK_H_

#include <chrono>
#include <cstdint>

// Headless-friendly frame benchmark, enabled through environment variables so
// automated test runs can measure a title without touching the GUI:
//
//   KYTY_BENCH_SECONDS=N   measure N seconds of presented frames, then exit
//   KYTY_BENCH_WARMUP=N    ignore the first N seconds after the first frame (default 0)
//   KYTY_BENCH_OUT=path    write per-frame CSV to path and a summary to path.summary.json
//                          (default: kyty_bench.csv in the working directory)
//
// Shader translation and pipeline creation report their wall time through
// RecordStall, so each frame row shows how much compile work landed before it was
// presented and the summary can attribute frame-time spikes to compilation.
//
// Without KYTY_BENCH_SECONDS every call is a cheap no-op.
namespace Libs::Graphics::FrameBenchmark {

enum class Stall : uint8_t { ShaderCompile, PipelineCreate };

// Presentation thread: called once per successfully presented frame.
void OnPresent();

// Main window loop: flushes samples and returns true when the run is complete
// and the emulator should exit.
bool Poll();

// Any thread: account `ns` of blocking work of the given kind to the next presented frame.
void RecordStall(Stall kind, uint64_t ns);

// True once the benchmark is known to be enabled; lets callers skip timing entirely.
bool Enabled();

class ScopedStall {
public:
	explicit ScopedStall(Stall kind)
	    : m_kind(kind), m_active(Enabled()),
	      m_start(m_active ? std::chrono::steady_clock::now()
	                       : std::chrono::steady_clock::time_point {}) {}
	~ScopedStall() {
		if (m_active) {
			RecordStall(m_kind,
			            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
			                                      std::chrono::steady_clock::now() - m_start)
			                                      .count()));
		}
	}
	ScopedStall(const ScopedStall&)            = delete;
	ScopedStall& operator=(const ScopedStall&) = delete;

private:
	Stall                                 m_kind;
	bool                                  m_active;
	std::chrono::steady_clock::time_point m_start;
};

} // namespace Libs::Graphics::FrameBenchmark

#endif // EMULATOR_SRC_GRAPHICS_PRESENTATION_WINDOW_FRAMEBENCHMARK_H_
