#include "graphics/presentation/window/frameBenchmark.h"

#include "common/logging/log.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace Libs::Graphics::FrameBenchmark {

namespace {

constexpr size_t STALL_KINDS = 2;

struct StallTotals {
	uint64_t ns    = 0;
	uint32_t count = 0;
};

// One presented frame and the compile work that completed since the previous present.
struct Sample {
	uint64_t                             present_ns = 0;
	std::array<StallTotals, STALL_KINDS> stalls {};
};

struct MeasuredFrame {
	double                               frame_ms = 0.0;
	std::array<StallTotals, STALL_KINDS> stalls {};
};

struct State {
	std::once_flag             init_once;
	std::atomic_bool           enabled     = false;
	bool                       finished    = false;
	uint64_t                   warmup_ns   = 0;
	uint64_t                   duration_ns = 0;
	std::string                out_path;
	std::FILE*                 csv         = nullptr;
	uint64_t                   first_ns    = 0;
	uint64_t                   last_ns     = 0;
	uint64_t                   frame_index = 0;
	std::vector<MeasuredFrame> measured;
	std::mutex                 pending_mutex;
	std::vector<Sample>        pending; // presents not yet processed by Poll
	// Compile work since the last present, filled by any thread.
	std::array<std::atomic<uint64_t>, STALL_KINDS> stall_ns {};
	std::array<std::atomic<uint32_t>, STALL_KINDS> stall_count {};
};

State& GetState() {
	static State s;
	return s;
}

uint64_t EnvSeconds(const char* name, uint64_t def) {
	const char* v = std::getenv(name);
	if (v == nullptr || *v == '\0') {
		return def;
	}
	return static_cast<uint64_t>(std::strtod(v, nullptr) * 1e9);
}

void Init(State& s) {
	s.duration_ns = EnvSeconds("KYTY_BENCH_SECONDS", 0);
	if (s.duration_ns == 0) {
		return;
	}
	s.warmup_ns     = EnvSeconds("KYTY_BENCH_WARMUP", 0);
	const char* out = std::getenv("KYTY_BENCH_OUT");
	s.out_path      = (out != nullptr && *out != '\0') ? out : "kyty_bench.csv";
	s.csv           = std::fopen(s.out_path.c_str(), "w");
	if (s.csv != nullptr) {
		std::fprintf(s.csv,
		             "frame,time_s,frame_ms,measured,shader_ms,shaders,pipeline_ms,pipelines\n");
	}
	s.measured.reserve(1 << 16);
	s.enabled.store(true, std::memory_order_release);
	LOGF("FrameBenchmark: warmup %.1fs, duration %.1fs, output %s\n",
	     static_cast<double>(s.warmup_ns) / 1e9, static_cast<double>(s.duration_ns) / 1e9,
	     s.out_path.c_str());
}

State& Ready() {
	auto& s = GetState();
	std::call_once(s.init_once, Init, std::ref(s));
	return s;
}

double Percentile(std::vector<double> v, double p) {
	if (v.empty()) {
		return 0.0;
	}
	std::sort(v.begin(), v.end());
	const auto idx = static_cast<size_t>(p * static_cast<double>(v.size() - 1) + 0.5);
	return v[std::min(idx, v.size() - 1)];
}

void WriteSummary(State& s) {
	std::vector<double> ms;
	ms.reserve(s.measured.size());
	double                               total = 0.0;
	std::array<StallTotals, STALL_KINDS> stalls {};
	uint32_t                             frames_with_compile = 0;
	for (const auto& f: s.measured) {
		ms.push_back(f.frame_ms);
		total += f.frame_ms;
		bool compiled = false;
		for (size_t k = 0; k < STALL_KINDS; k++) {
			stalls[k].ns += f.stalls[k].ns;
			stalls[k].count += f.stalls[k].count;
			compiled |= f.stalls[k].count != 0;
		}
		frames_with_compile += compiled ? 1u : 0u;
	}
	const double avg_ms  = ms.empty() ? 0.0 : total / static_cast<double>(ms.size());
	const double avg_fps = total > 0.0 ? 1000.0 * static_cast<double>(ms.size()) / total : 0.0;
	const double p50     = Percentile(ms, 0.50);
	const double p99     = Percentile(ms, 0.99);

	// "1% low": average FPS of the slowest 1% of frames.
	std::vector<double> sorted = ms;
	std::sort(sorted.begin(), sorted.end(), std::greater<>());
	const size_t n_low  = std::max<size_t>(1, sorted.size() / 100);
	double       low_ms = 0.0;
	for (size_t i = 0; i < n_low && i < sorted.size(); i++) {
		low_ms += sorted[i];
	}
	low_ms /= static_cast<double>(n_low);

	// A spike is a frame taking more than twice the median; split them by whether
	// shader or pipeline compilation landed in that frame.
	uint32_t spikes              = 0;
	uint32_t spikes_with_compile = 0;
	for (const auto& f: s.measured) {
		if (p50 > 0.0 && f.frame_ms > 2.0 * p50) {
			spikes++;
			if (f.stalls[0].count != 0 || f.stalls[1].count != 0) {
				spikes_with_compile++;
			}
		}
	}

	const std::string path = s.out_path + ".summary.json";
	if (std::FILE* f = std::fopen(path.c_str(), "w"); f != nullptr) {
		std::fprintf(f,
		             "{\n  \"frames\": %zu,\n  \"seconds\": %.3f,\n  \"avg_fps\": %.2f,\n"
		             "  \"one_percent_low_fps\": %.2f,\n  \"avg_frame_ms\": %.3f,\n  "
		             "\"p50_frame_ms\": %.3f,\n"
		             "  \"p99_frame_ms\": %.3f,\n  \"max_frame_ms\": %.3f,\n"
		             "  \"shader_compiles\": %u,\n  \"shader_compile_ms\": %.3f,\n"
		             "  \"pipeline_creates\": %u,\n  \"pipeline_create_ms\": %.3f,\n"
		             "  \"frames_with_compiles\": %u,\n  \"spike_frames\": %u,\n"
		             "  \"spike_frames_with_compiles\": %u\n}\n",
		             ms.size(), total / 1000.0, avg_fps, low_ms > 0.0 ? 1000.0 / low_ms : 0.0,
		             avg_ms, p50, p99, sorted.empty() ? 0.0 : sorted.front(), stalls[0].count,
		             static_cast<double>(stalls[0].ns) / 1e6, stalls[1].count,
		             static_cast<double>(stalls[1].ns) / 1e6, frames_with_compile, spikes,
		             spikes_with_compile);
		std::fclose(f);
	}
	LOGF("FrameBenchmark: %zu frames, avg %.2f fps, 1%% low %.2f fps, p99 %.2f ms, "
	     "%u shader compiles (%.1f ms), %u pipelines (%.1f ms), %u/%u spikes during compiles -> "
	     "%s\n",
	     ms.size(), avg_fps, low_ms > 0.0 ? 1000.0 / low_ms : 0.0, p99, stalls[0].count,
	     static_cast<double>(stalls[0].ns) / 1e6, stalls[1].count,
	     static_cast<double>(stalls[1].ns) / 1e6, spikes_with_compile, spikes, path.c_str());
}

} // namespace

bool Enabled() {
	return Ready().enabled.load(std::memory_order_acquire);
}

void RecordStall(Stall kind, uint64_t ns) {
	auto&      s = GetState();
	const auto k = static_cast<size_t>(kind);
	s.stall_ns[k].fetch_add(ns, std::memory_order_relaxed);
	s.stall_count[k].fetch_add(1, std::memory_order_relaxed);
}

void OnPresent() {
	auto& s = Ready();
	if (!s.enabled.load(std::memory_order_relaxed)) {
		return;
	}
	Sample sample;
	sample.present_ns = SDL_GetTicksNS();
	for (size_t k = 0; k < STALL_KINDS; k++) {
		sample.stalls[k].ns    = s.stall_ns[k].exchange(0, std::memory_order_relaxed);
		sample.stalls[k].count = s.stall_count[k].exchange(0, std::memory_order_relaxed);
	}
	std::scoped_lock lock(s.pending_mutex);
	s.pending.push_back(sample);
}

bool Poll() {
	auto& s = Ready();
	if (!s.enabled.load(std::memory_order_relaxed)) {
		return false;
	}
	if (s.finished) {
		return true;
	}

	std::vector<Sample> batch;
	{
		std::scoped_lock lock(s.pending_mutex);
		batch.swap(s.pending);
	}
	for (const auto& sample: batch) {
		const uint64_t t = sample.present_ns;
		if (s.first_ns == 0) {
			s.first_ns = t;
			s.last_ns  = t;
			continue;
		}
		const double   frame_ms = static_cast<double>(t - s.last_ns) / 1e6;
		const uint64_t since    = t - s.first_ns;
		const bool     measured = since >= s.warmup_ns;
		s.last_ns               = t;
		s.frame_index++;
		if (measured) {
			s.measured.push_back({.frame_ms = frame_ms, .stalls = sample.stalls});
		}
		if (s.csv != nullptr) {
			std::fprintf(s.csv, "%llu,%.6f,%.3f,%d,%.3f,%u,%.3f,%u\n",
			             static_cast<unsigned long long>(s.frame_index),
			             static_cast<double>(since) / 1e9, frame_ms, measured ? 1 : 0,
			             static_cast<double>(sample.stalls[0].ns) / 1e6, sample.stalls[0].count,
			             static_cast<double>(sample.stalls[1].ns) / 1e6, sample.stalls[1].count);
		}
		if (since >= s.warmup_ns + s.duration_ns) {
			s.finished = true;
			break;
		}
	}
	// A title that stops presenting (hang, long load) still ends the run, so the
	// stall shows up in the summary instead of hanging the benchmark forever.
	if (!s.finished && s.first_ns != 0 &&
	    SDL_GetTicksNS() - s.first_ns >= s.warmup_ns + s.duration_ns + 5'000'000'000ull) {
		LOGF("FrameBenchmark: no frame presented for a while, ending run\n");
		s.finished = true;
	}
	if (s.csv != nullptr) {
		std::fflush(s.csv);
	}
	if (s.finished) {
		if (s.csv != nullptr) {
			std::fclose(s.csv);
			s.csv = nullptr;
		}
		WriteSummary(s);
	}
	return s.finished;
}

} // namespace Libs::Graphics::FrameBenchmark
