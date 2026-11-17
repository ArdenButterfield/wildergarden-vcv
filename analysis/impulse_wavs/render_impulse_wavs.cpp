// Renders the FilterDistort feedback loop's impulse response to 1 second, 44.1 kHz WAV files, for
// each solver in src/feedback_solvers over a grid of distortion gain and shape values.
//
// Each render starts from a reset filter and solver, then feeds in one sample of amplitude 1
// followed by silence. The per-sample processing mirrors FilterDistort::process(), with the filter
// at FREQ Hz and Q in each of the module's filter modes (lowpass, bandpass, highpass).
//
// Build (from the repo root, with RACK_DIR pointing at the Rack SDK; one command, wrapped here).
// Only the SDK headers are needed, not libRack: see the nanovg definitions below. With MSYS2's
// g++ run from Git Bash, put /c/msys64/mingw64/bin first in PATH, or cc1plus picks up Git's DLLs
// and fails without an error message.
//   g++ -std=c++11 -O3 -funsafe-math-optimizations -march=nehalem -D_USE_MATH_DEFINES
//       -I"$RACK_DIR/include" -I"$RACK_DIR/dep/include"
//       analysis/impulse_wavs/render_impulse_wavs.cpp -o analysis/impulse_wavs/render_impulse_wavs.exe
// Run:
//   analysis/impulse_wavs/render_impulse_wavs.exe [output directory, default analysis/impulse_wavs/out]
//       [--gain G] [--sample-rates MIN MAX COUNT]
// --gain renders only that distortion gain instead of the 0-5 grid. --sample-rates renders COUNT
// linearly spaced sample rates from MIN to MAX Hz (each file still 1 second long) instead of
// 44.1 kHz, and adds _sr<rate> to the file names.
//
// Files are 32-bit float mono WAVs of the module's output voltage divided by 5, so the distortion's
// +-5 V range maps to full scale. Names are
// <solver>_<filter mode>_freq<Hz>_q<Q>_gain<gain>_shape<shape>.wav.

#include "../../src/feedback_solvers/FeedbackLoop.hpp"
#include "../../src/feedback_solvers/NaiveSolver.hpp"
#include "../../src/feedback_solvers/NewtonZonesSolver.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define MAKE_DIR(path) mkdir(path, 0755)
#endif

// rack.hpp builds color constants with these at static initialization. Defining them here (as
// nanovg does) means the tool doesn't have to link against libRack.
NVGcolor nvgRGBA(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
	NVGcolor color;
	color.r = r / 255.f;
	color.g = g / 255.f;
	color.b = b / 255.f;
	color.a = a / 255.f;
	return color;
}

NVGcolor nvgRGB(unsigned char r, unsigned char g, unsigned char b) {
	return nvgRGBA(r, g, b, 255);
}

static constexpr int DEFAULT_SAMPLE_RATE = 44100;
static constexpr float FREQ = 100.f;
static constexpr float Q = 3.f;
// The module's FEEDBACK_PARAM and SHAPE_PARAM ranges
static constexpr float MIN_GAIN = 0.f;
static constexpr float MAX_GAIN = 5.f;
static constexpr float MIN_SHAPE = 0.f;
static constexpr float MAX_SHAPE = 1.f;
// Steps of 0.1 over each range
static constexpr int GAIN_POINTS = 51;
static constexpr int SHAPE_POINTS = 11;
static constexpr float OUTPUT_SCALE = 0.2f;

// One step of FilterDistort::process() for whichever solver is passed in
template <typename Solve>
static float processSample(dsp::BiquadFilter& filter, NaiveSolver& naiveSolver, const float input, const float shape, const float gain, Solve solve) {
	const FeedbackLoop loop(filter, input, shape, gain);
	int iterations;
	auto feedback = solve(loop, iterations);
	if (!std::isfinite(feedback)) {
		feedback = naiveSolver.previousFeedback;
	}
	const auto filtered = filter.process(input + feedback);
	const auto distorted = FeedbackLoop::distort(filtered * 0.2f, shape) * 5.f;
	naiveSolver.update(distorted * gain);
	return distorted;
}

enum Solver {
	SOLVER_NAIVE,
	SOLVER_NEWTON_ZONES
};

static std::vector<float> render(const int sampleRate, const Solver solver, const dsp::BiquadFilter::Type type, const float shape, const float gain) {
	dsp::BiquadFilter filter;
	filter.reset();
	filter.setParameters(type, FREQ / sampleRate, Q, 1.f);
	NaiveSolver naiveSolver;
	NewtonZonesSolver newtonZonesSolver;

	std::vector<float> out(sampleRate);
	for (int i = 0; i < sampleRate; i++) {
		const auto input = i == 0 ? 1.f : 0.f;
		if (solver == SOLVER_NEWTON_ZONES) {
			out[i] = processSample(filter, naiveSolver, input, shape, gain, [&](const FeedbackLoop& loop, int& iterations) {
				return newtonZonesSolver.solve(loop, iterations);
			});
		}
		else {
			out[i] = processSample(filter, naiveSolver, input, shape, gain, [&](const FeedbackLoop& loop, int& iterations) {
				return naiveSolver.solve(loop, iterations);
			});
		}
	}
	return out;
}

static void writeLE(std::ofstream& f, const uint32_t value, const int bytes) {
	for (int i = 0; i < bytes; i++) {
		f.put((char) ((value >> (8 * i)) & 0xff));
	}
}

// Mono 32-bit IEEE float WAV
static bool writeWav(const std::string& path, const int sampleRate, const std::vector<float>& samples) {
	std::ofstream f(path, std::ios::binary);
	if (!f) {
		return false;
	}
	const uint32_t dataBytes = samples.size() * 4;
	f.write("RIFF", 4);
	writeLE(f, 36 + dataBytes, 4);
	f.write("WAVE", 4);
	f.write("fmt ", 4);
	writeLE(f, 16, 4);
	writeLE(f, 3, 2); // WAVE_FORMAT_IEEE_FLOAT
	writeLE(f, 1, 2); // channels
	writeLE(f, sampleRate, 4);
	writeLE(f, sampleRate * 4, 4); // byte rate
	writeLE(f, 4, 2); // block align
	writeLE(f, 32, 2); // bits per sample
	f.write("data", 4);
	writeLE(f, dataBytes, 4);
	for (const auto sample : samples) {
		const auto scaled = std::isfinite(sample) ? sample * OUTPUT_SCALE : 0.f;
		uint32_t bits;
		std::memcpy(&bits, &scaled, 4);
		writeLE(f, bits, 4);
	}
	f.close();
	return !f.fail();
}

static std::string formatValue(const char* format, const float value) {
	char buffer[32];
	snprintf(buffer, sizeof(buffer), format, value);
	return buffer;
}

int main(int argc, char** argv) {
	std::string outDir = "analysis/impulse_wavs/out";
	bool fixedGain = false;
	float gainArg = 0.f;
	std::vector<int> sampleRates = {DEFAULT_SAMPLE_RATE};
	bool sweepRates = false;
	for (int i = 1; i < argc; i++) {
		const std::string arg = argv[i];
		if (arg == "--gain" && i + 1 < argc) {
			fixedGain = true;
			gainArg = std::stof(argv[++i]);
		}
		else if (arg == "--sample-rates" && i + 3 < argc) {
			const double minRate = std::stod(argv[++i]);
			const double maxRate = std::stod(argv[++i]);
			const int count = std::stoi(argv[++i]);
			sampleRates.clear();
			for (int k = 0; k < count; k++) {
				sampleRates.push_back((int) std::lround(count > 1 ? minRate + (maxRate - minRate) * k / (count - 1) : minRate));
			}
			sweepRates = true;
		}
		else if (arg.compare(0, 2, "--") == 0) {
			fprintf(stderr, "Unknown or incomplete option %s\n", arg.c_str());
			return 1;
		}
		else {
			outDir = arg;
		}
	}
	MAKE_DIR(outDir.c_str());

	const struct {
		Solver solver;
		const char* name;
	} solvers[] = {
		{SOLVER_NAIVE, "naive"},
		{SOLVER_NEWTON_ZONES, "newtonzones"},
	};

	// The module's FILTER_MODE_PARAM options
	const struct {
		dsp::BiquadFilter::Type type;
		const char* name;
	} modes[] = {
		{dsp::BiquadFilter::HIGHPASS, "highpass"},
	};

	int written = 0;
	const int gainPoints = fixedGain ? 1 : GAIN_POINTS;
	for (const auto sampleRate : sampleRates) {
	for (const auto& s : solvers) {
		for (const auto& m : modes) {
			for (int g = 0; g < gainPoints; g++) {
				const auto gain = fixedGain ? gainArg : MIN_GAIN + (MAX_GAIN - MIN_GAIN) * g / (GAIN_POINTS - 1);
				for (int k = 0; k < SHAPE_POINTS; k++) {
					const auto shape = MIN_SHAPE + (MAX_SHAPE - MIN_SHAPE) * k / (SHAPE_POINTS - 1);
					const auto samples = render(sampleRate, s.solver, m.type, shape, gain);
					const auto path = outDir + "/" + s.name + "_" + m.name + "_freq" + formatValue("%g", FREQ) + "_q" + formatValue("%g", Q) + "_gain" + formatValue("%.3f", gain) + "_shape" + formatValue("%.3f", shape) + (sweepRates ? "_sr" + std::to_string(sampleRate) : "") + ".wav";
					if (!writeWav(path, sampleRate, samples)) {
						fprintf(stderr, "Couldn't write %s\n", path.c_str());
						return 1;
					}
					written++;
				}
			}
		}
	}
	}
	printf("Wrote %d files to %s\n", written, outDir.c_str());
	return 0;
}
