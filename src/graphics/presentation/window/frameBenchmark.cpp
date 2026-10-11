#include "graphics/presentation/window/frameBenchmark.h"

#include <SDL3/SDL.h>

#include "common/logging/log.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace Libs::Graphics::FrameBenchmark {

namespace {

struct State {
	bool                  initialized = false;
	bool                  enabled     = false;
	bool                  finished    = false;
	uint64_t              warmup_ns   = 0;
	uint64_t              duration_ns = 0;
	std::string           out_path;
	std::FILE*            csv           = nullptr;
	uint64_t              first_ns      = 0;
	uint64_t              last_ns       = 0;
	uint64_t              frame_index   = 0;
	std::vector<double>   frame_ms;     // measured window only
	std::mutex            pending_mutex;
	std::vector<uint64_t> pending;      // present timestamps not yet processed
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
	s.initialized = true;
	s.duration_ns = EnvSeconds("KYTY_BENCH_SECONDS", 0);
	if (s.duration_ns == 0) {
		return;
	}
	s.enabled   = true;
	s.warmup_ns = EnvSeconds("KYTY_BENCH_WARMUP", 0);
	const char* out = std::getenv("KYTY_BENCH_OUT");
	s.out_path      = (out != nullptr && *out != '\0') ? out : "kyty_bench.csv";
	s.csv           = std::fopen(s.out_path.c_str(), "w");
	if (s.csv != nullptr) {
		std::fprintf(s.csv, "frame,time_s,frame_ms,measured\n");
	}
	s.frame_ms.reserve(1 << 16);
	LOGF("FrameBenchmark: warmup %.1fs, duration %.1fs, output %s\n", static_cast<double>(s.warmup_ns) / 1e9,
	     static_cast<double>(s.duration_ns) / 1e9, s.out_path.c_str());
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
	const auto&  ms     = s.frame_ms;
	double       total  = 0.0;
	for (double m: ms) {
		total += m;
	}
	const double avg_ms  = ms.empty() ? 0.0 : total / static_cast<double>(ms.size());
	const double avg_fps = total > 0.0 ? 1000.0 * static_cast<double>(ms.size()) / total : 0.0;
	// "1% low": average FPS of the slowest 1% of frames.
	std::vector<double> sorted = ms;
	std::sort(sorted.begin(), sorted.end(), std::greater<>());
	const size_t n_low  = std::max<size_t>(1, sorted.size() / 100);
	double       low_ms = 0.0;
	for (size_t i = 0; i < n_low && i < sorted.size(); i++) {
		low_ms += sorted[i];
	}
	low_ms /= static_cast<double>(n_low);
	const double p50 = Percentile(ms, 0.50);
	const double p99 = Percentile(ms, 0.99);

	const std::string path = s.out_path + ".summary.json";
	if (std::FILE* f = std::fopen(path.c_str(), "w"); f != nullptr) {
		std::fprintf(f,
		             "{\n  \"frames\": %zu,\n  \"seconds\": %.3f,\n  \"avg_fps\": %.2f,\n  \"one_percent_low_fps\": %.2f,\n"
		             "  \"avg_frame_ms\": %.3f,\n  \"p50_frame_ms\": %.3f,\n  \"p99_frame_ms\": %.3f,\n"
		             "  \"max_frame_ms\": %.3f\n}\n",
		             ms.size(), total / 1000.0, avg_fps, low_ms > 0.0 ? 1000.0 / low_ms : 0.0, avg_ms, p50, p99,
		             sorted.empty() ? 0.0 : sorted.front());
		std::fclose(f);
	}
	LOGF("FrameBenchmark: %zu frames, avg %.2f fps, 1%% low %.2f fps, p99 %.2f ms -> %s\n", ms.size(), avg_fps,
	     low_ms > 0.0 ? 1000.0 / low_ms : 0.0, p99, path.c_str());
}

} // namespace

void OnPresent() {
	auto& s = GetState();
	if (!s.initialized) {
		Init(s);
	}
	if (!s.enabled) {
		return;
	}
	const uint64_t   now = SDL_GetTicksNS();
	std::scoped_lock lock(s.pending_mutex);
	s.pending.push_back(now);
}

bool Poll() {
	auto& s = GetState();
	if (!s.initialized) {
		Init(s);
	}
	if (!s.enabled) {
		return false;
	}
	if (s.finished) {
		return true;
	}

	std::vector<uint64_t> batch;
	{
		std::scoped_lock lock(s.pending_mutex);
		batch.swap(s.pending);
	}
	for (uint64_t t: batch) {
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
			s.frame_ms.push_back(frame_ms);
		}
		if (s.csv != nullptr) {
			std::fprintf(s.csv, "%llu,%.6f,%.3f,%d\n", static_cast<unsigned long long>(s.frame_index),
			             static_cast<double>(since) / 1e9, frame_ms, measured ? 1 : 0);
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
