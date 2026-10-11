#include "graphics/presentation/window/frameBenchmark.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>

namespace {

namespace FB = Libs::Graphics::FrameBenchmark;

void Check(bool value, const char *message) {
  if (!value) {
    std::fprintf(stderr, "FrameBenchmarkTests: failed: %s\n", message);
    std::abort();
  }
}

std::string ReadFile(const std::filesystem::path &path) {
  std::ifstream in(path);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Extracts an unsigned integer field from the flat summary JSON.
unsigned long JsonUint(const std::string &json, const std::string &key) {
  const auto pos = json.find("\"" + key + "\":");
  Check(pos != std::string::npos, "summary field missing");
  return std::strtoul(json.c_str() + pos + key.size() + 3, nullptr, 10);
}

void SetEnv(const char *name, const std::string &value) {
#if defined(_WIN32)
  _putenv_s(name, value.c_str());
#else
  setenv(name, value.c_str(), 1);
#endif
}

void TestRunMeasuresFramesAndAttributesCompiles() {
  const auto dir =
      std::filesystem::temp_directory_path() / "kyty_frame_benchmark_test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const auto csv = dir / "frames.csv";

  SetEnv("KYTY_BENCH_SECONDS", "0.3");
  SetEnv("KYTY_BENCH_WARMUP", "0.05");
  SetEnv("KYTY_BENCH_OUT", csv.string());

  Check(FB::Enabled(), "benchmark should be enabled by KYTY_BENCH_SECONDS");

  // ~4 ms frames; every 10th frame also records a shader compile and a pipeline
  // create, and is made slow so it counts as a spike.
  bool finished = false;
  unsigned presents = 0;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!finished && std::chrono::steady_clock::now() < deadline) {
    const bool compile_frame = presents % 10 == 5;
    if (compile_frame) {
      FB::RecordStall(FB::Stall::ShaderCompile, 2'000'000);
      FB::RecordStall(FB::Stall::PipelineCreate, 1'000'000);
    }
    std::this_thread::sleep_for(
        std::chrono::milliseconds(compile_frame ? 20 : 4));
    FB::OnPresent();
    presents++;
    finished = FB::Poll();
  }
  Check(finished, "Poll never reported the run as finished");
  Check(FB::Poll(), "Poll must keep reporting finished");

  const auto summary = ReadFile(dir / "frames.csv.summary.json");
  Check(!summary.empty(), "summary JSON not written");
  const auto frames = JsonUint(summary, "frames");
  Check(frames > 20, "too few measured frames");
  Check(frames < presents, "warmup frames must not be measured");
  const auto shaders = JsonUint(summary, "shader_compiles");
  const auto pipelines = JsonUint(summary, "pipeline_creates");
  Check(shaders > 0 && shaders == pipelines,
        "compile counts not attributed to measured frames");
  Check(JsonUint(summary, "frames_with_compiles") == shaders,
        "one compile per compile frame expected");
  const auto spikes = JsonUint(summary, "spike_frames");
  Check(spikes >= shaders, "slow compile frames must count as spikes");
  Check(JsonUint(summary, "spike_frames_with_compiles") == shaders,
        "every compile frame is a spike that overlaps a compile");

  // Header plus one row per presented frame after the first.
  const auto rows = ReadFile(csv);
  size_t lines = 0;
  for (char c : rows) {
    lines += c == '\n' ? 1u : 0u;
  }
  Check(lines >= frames + 1, "CSV is missing rows");
  Check(rows.rfind("frame,time_s,frame_ms,measured,shader_ms,shaders,pipeline_"
                   "ms,pipelines\n",
                   0) == 0,
        "CSV header changed");

  std::filesystem::remove_all(dir);
}

} // namespace

int main() {
  TestRunMeasuresFramesAndAttributesCompiles();
  std::printf("FrameBenchmarkTests: ok\n");
  return 0;
}
