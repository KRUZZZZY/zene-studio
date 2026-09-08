/*
 * nam_harness.cpp - headless test harness for the NeuralAmp .nam engine
 *
 * Loads a real .nam model, processes a deterministic test signal and reports
 *   (a) output sanity (finite, non-zero),
 *   (b) CPU time per block and CPU percentage of one core at 48 kHz,
 *   (c) how the model colours the signal versus the input,
 *   (d) optional sample-exact comparison against a reference render (the
 *       MIT NeuralAmpModelerCore `render` tool) via --compare-ref.
 *
 * Exit code 0 = all checks passed, 1 = a check failed, 2 = usage/load error.
 *
 * Build (from the repo root, see NEURAL-AMP.md):
 *   g++ -O3 -march=native -std=c++20 -I plugins/NeuralAmp \
 *       -I plugins/NeuralAmp/rtneural/modules/Eigen \
 *       plugins/NeuralAmp/tests/nam_harness.cpp \
 *       plugins/NeuralAmp/nam/NamModel.cpp \
 *       plugins/NeuralAmp/nam/NamModelLoader.cpp -o build/nam_harness
 *
 * Copyright (c) 2026 AI-KOS Team. GPL-2.0-or-later (see plugin sources).
 */

#include "nam/NamModel.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace
{

constexpr double kTwoPi = 6.283185307179586476925286766559;

struct Options
{
	std::string modelPath;
	std::string wavPath;         // legacy: write int16 output WAV
	std::string writeInputWav;   // write the generated input as float32 WAV
	std::string wavRun;          // process this float32/int WAV instead of the generated signal
	std::string wavOut;          // write the wav-run output as float32 WAV
	std::string compareRef;      // compare output against this reference WAV
	int blockSize = 512;
	int blocks = 2000;
	int warmupBlocks = 20;
	double sampleRate = 48000.0;
};

void usage(const char* argv0)
{
	std::printf("usage: %s <model.nam> [--block N] [--blocks N] [--warmup N] "
	            "[--sample-rate HZ] [--wav out.wav] [--write-input-wav in.wav]\n"
	            "       %s <model.nam> --wav-run in.wav [--wav-out out.wav] "
	            "[--compare-ref ref.wav] [--block N]\n", argv0, argv0);
}

/// Deterministic plucked-string-ish test signal (no randomness across runs).
double testSignal(int64_t sample, double sampleRate)
{
	const double t = static_cast<double>(sample) / sampleRate;
	// A pluck every 0.5 s with harmonics 1..6, exponential decay.
	const double pluckPeriod = 0.5;
	const double phase = std::fmod(t, pluckPeriod);
	const double envelope = std::exp(-phase * 6.0);
	const double f0 = 110.0;
	double value = 0.0;
	for (int h = 1; h <= 6; ++h)
	{
		value += std::sin(kTwoPi * f0 * h * t) / static_cast<double>(h);
	}
	value *= envelope * 0.2 / 2.449;  // normalise the harmonic sum to ~1

	// Deterministic low-level noise (LCG), -60 dBFS-ish.
	const uint32_t r = static_cast<uint32_t>(sample * 1664525u + 1013904223u);
	const double noise = (static_cast<double>(r) / 4294967295.0 - 0.5) * 2.0;
	return value + noise * 0.001;
}

/// Goertzel magnitude (linear) of frequency f over x.
double goertzel(const std::vector<float>& x, double f, double sampleRate)
{
	const double w = kTwoPi * f / sampleRate;
	const double cw = std::cos(w);
	const double coeff = 2.0 * cw;
	double s0 = 0.0, s1 = 0.0, s2 = 0.0;
	for (float v : x)
	{
		s0 = static_cast<double>(v) + coeff * s1 - s2;
		s2 = s1;
		s1 = s0;
	}
	const double real = s1 - s2 * cw;
	const double imag = s2 * std::sin(w);
	return std::sqrt(real * real + imag * imag) / static_cast<double>(x.size()) * 2.0;
}

double rms(const std::vector<float>& x)
{
	double sum = 0.0;
	for (float v : x)
	{
		sum += static_cast<double>(v) * static_cast<double>(v);
	}
	return std::sqrt(sum / static_cast<double>(x.size()));
}

double peak(const std::vector<float>& x)
{
	double p = 0.0;
	for (float v : x)
	{
		p = std::max(p, std::fabs(static_cast<double>(v)));
	}
	return p;
}

// ---------------------------------------------------------------- WAV I/O

void putU32(std::vector<uint8_t>& b, uint32_t v)
{
	b.push_back(v & 0xff); b.push_back((v >> 8) & 0xff);
	b.push_back((v >> 16) & 0xff); b.push_back((v >> 24) & 0xff);
}
void putU16(std::vector<uint8_t>& b, uint16_t v)
{
	b.push_back(v & 0xff); b.push_back((v >> 8) & 0xff);
}

bool writeWavF32(const std::string& path, const std::vector<float>& x, double sampleRate)
{
	std::vector<uint8_t> hdr;
	const uint32_t dataBytes = static_cast<uint32_t>(x.size() * 4);
	hdr.reserve(44);
	const char* riff = "RIFF";
	hdr.insert(hdr.end(), riff, riff + 4);
	putU32(hdr, 36 + dataBytes);
	const char* wave = "WAVEfmt ";
	hdr.insert(hdr.end(), wave, wave + 8);
	putU32(hdr, 16);
	putU16(hdr, 3);                       // IEEE float
	putU16(hdr, 1);                       // mono
	putU32(hdr, static_cast<uint32_t>(sampleRate));
	putU32(hdr, static_cast<uint32_t>(sampleRate) * 4);
	putU16(hdr, 4);
	putU16(hdr, 32);
	const char* data = "data";
	hdr.insert(hdr.end(), data, data + 4);
	putU32(hdr, dataBytes);

	std::FILE* f = std::fopen(path.c_str(), "wb");
	if (f == nullptr) { return false; }
	std::fwrite(hdr.data(), 1, hdr.size(), f);
	std::fwrite(x.data(), sizeof(float), x.size(), f);
	std::fclose(f);
	return true;
}

bool writeWav16(const std::string& path, const std::vector<float>& x, double sampleRate)
{
	std::FILE* f = std::fopen(path.c_str(), "wb");
	if (f == nullptr) { return false; }
	const uint32_t dataBytes = static_cast<uint32_t>(x.size() * 2);
	const uint32_t chunk = 36 + dataBytes;
	const uint16_t bits = 16, channels = 1;
	const uint32_t rate = static_cast<uint32_t>(sampleRate);
	const uint32_t byteRate = rate * 2;
	const uint16_t blockAlign = 2;
	std::fwrite("RIFF", 1, 4, f);
	std::fwrite(&chunk, 4, 1, f);
	std::fwrite("WAVEfmt ", 1, 8, f);
	const uint32_t fmtSize = 16;
	const uint16_t fmt = 1;
	std::fwrite(&fmtSize, 4, 1, f);
	std::fwrite(&fmt, 2, 1, f);
	std::fwrite(&channels, 2, 1, f);
	std::fwrite(&rate, 4, 1, f);
	std::fwrite(&byteRate, 4, 1, f);
	std::fwrite(&blockAlign, 2, 1, f);
	std::fwrite(&bits, 2, 1, f);
	std::fwrite("data", 1, 4, f);
	std::fwrite(&dataBytes, 4, 1, f);
	for (float v : x)
	{
		const double clipped = std::max(-1.0, std::min(1.0, static_cast<double>(v)));
		const int16_t s = static_cast<int16_t>(std::lround(clipped * 32767.0));
		std::fwrite(&s, 2, 1, f);
	}
	std::fclose(f);
	return true;
}

uint32_t getU32(const std::vector<uint8_t>& b, size_t off)
{
	return static_cast<uint32_t>(b[off]) | (static_cast<uint32_t>(b[off + 1]) << 8) |
		(static_cast<uint32_t>(b[off + 2]) << 16) | (static_cast<uint32_t>(b[off + 3]) << 24);
}
uint16_t getU16(const std::vector<uint8_t>& b, size_t off)
{
	return static_cast<uint16_t>(b[off]) | (static_cast<uint16_t>(b[off + 1]) << 8);
}

/// Minimal RIFF reader: mono PCM16/PCM24/PCM32/IEEE-float32.
bool readWav(const std::string& path, std::vector<float>& out, double* sampleRate)
{
	std::FILE* f = std::fopen(path.c_str(), "rb");
	if (f == nullptr) { return false; }
	std::fseek(f, 0, SEEK_END);
	const long size = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	if (size <= 44) { std::fclose(f); return false; }
	std::vector<uint8_t> b(static_cast<size_t>(size));
	if (std::fread(b.data(), 1, b.size(), f) != b.size()) { std::fclose(f); return false; }
	std::fclose(f);

	if (std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0)
	{
		return false;
	}
	size_t pos = 12;
	uint16_t fmt = 0, channels = 0, bits = 0;
	uint32_t rate = 0;
	size_t dataOff = 0, dataLen = 0;
	while (pos + 8 <= b.size())
	{
		const uint32_t chunkLen = getU32(b, pos + 4);
		const char* id = reinterpret_cast<const char*>(b.data() + pos);
		if (std::memcmp(id, "fmt ", 4) == 0 && pos + 8 + 16 <= b.size())
		{
			fmt = getU16(b, pos + 8);
			channels = getU16(b, pos + 10);
			rate = getU32(b, pos + 12);
			bits = getU16(b, pos + 22);
		}
		else if (std::memcmp(id, "data", 4) == 0)
		{
			dataOff = pos + 8;
			dataLen = chunkLen;
		}
		pos += 8 + chunkLen + (chunkLen & 1u);
	}
	if (dataOff == 0 || channels != 1) { return false; }
	if (sampleRate != nullptr) { *sampleRate = static_cast<double>(rate); }
	dataLen = std::min(dataLen, b.size() - dataOff);

	if (fmt == 3 && bits == 32)
	{
		const size_t n = dataLen / 4;
		out.resize(n);
		for (size_t i = 0; i < n; ++i)
		{
			uint32_t u = getU32(b, dataOff + i * 4);
			float v;
			std::memcpy(&v, &u, 4);
			out[i] = v;
		}
		return true;
	}
	if (fmt == 1 && bits == 16)
	{
		const size_t n = dataLen / 2;
		out.resize(n);
		for (size_t i = 0; i < n; ++i)
		{
			const int16_t s = static_cast<int16_t>(getU16(b, dataOff + i * 2));
			out[i] = static_cast<float>(s / 32768.0);
		}
		return true;
	}
	if (fmt == 1 && bits == 24)
	{
		const size_t n = dataLen / 3;
		out.resize(n);
		for (size_t i = 0; i < n; ++i)
		{
			int32_t s = static_cast<int32_t>(b[dataOff + i * 3]) |
				(static_cast<int32_t>(b[dataOff + i * 3 + 1]) << 8) |
				(static_cast<int32_t>(b[dataOff + i * 3 + 2]) << 16);
			if (s & 0x800000) { s |= ~0xffffff; }
			out[i] = static_cast<float>(s / 8388608.0);
		}
		return true;
	}
	if (fmt == 1 && bits == 32)
	{
		const size_t n = dataLen / 4;
		out.resize(n);
		for (size_t i = 0; i < n; ++i)
		{
			const int32_t s = static_cast<int32_t>(getU32(b, dataOff + i * 4));
			out[i] = static_cast<float>(s / 2147483648.0);
		}
		return true;
	}
	return false;
}

}  // namespace


int main(int argc, char** argv)
{
	Options opt;
	std::vector<std::string> positional;

	for (int i = 1; i < argc; ++i)
	{
		const std::string a = argv[i];
		auto next = [&](const char* what) -> std::string {
			if (i + 1 >= argc)
			{
				std::fprintf(stderr, "error: %s needs a value\n", what);
				std::exit(2);
			}
			return argv[++i];
		};
		if (a == "--block") { opt.blockSize = std::stoi(next("--block")); }
		else if (a == "--blocks") { opt.blocks = std::stoi(next("--blocks")); }
		else if (a == "--warmup") { opt.warmupBlocks = std::stoi(next("--warmup")); }
		else if (a == "--sample-rate") { opt.sampleRate = std::stod(next("--sample-rate")); }
		else if (a == "--wav") { opt.wavPath = next("--wav"); }
		else if (a == "--write-input-wav") { opt.writeInputWav = next("--write-input-wav"); }
		else if (a == "--wav-run") { opt.wavRun = next("--wav-run"); }
		else if (a == "--wav-out") { opt.wavOut = next("--wav-out"); }
		else if (a == "--compare-ref") { opt.compareRef = next("--compare-ref"); }
		else if (a == "--help" || a == "-h") { usage(argv[0]); return 0; }
		else if (!a.empty() && a[0] == '-')
		{
			std::fprintf(stderr, "error: unknown option %s\n", a.c_str());
			usage(argv[0]);
			return 2;
		}
		else { positional.push_back(a); }
	}

	if (positional.empty())
	{
		usage(argv[0]);
		return 2;
	}
	opt.modelPath = positional.front();

	if (opt.blockSize < 1 || opt.blockSize > lmms::nam::NamModel::kMaxBlock)
	{
		std::fprintf(stderr, "error: --block must be in [1, %d]\n",
			lmms::nam::NamModel::kMaxBlock);
		return 2;
	}
	if (opt.blocks < 1)
	{
		std::fprintf(stderr, "error: --blocks must be >= 1\n");
		return 2;
	}

	std::printf("== NeuralAmp harness ==\n");
	std::printf("model: %s\n", opt.modelPath.c_str());

	std::string error;
	std::unique_ptr<lmms::nam::NamModel> model =
		lmms::nam::NamModel::loadFromFile(opt.modelPath, &error);
	if (model == nullptr)
	{
		std::fprintf(stderr, "LOAD FAILED: %s\n", error.c_str());
		return 2;
	}

	const lmms::nam::ModelSpec& spec = model->spec();
	std::printf("architecture: %s  version: %s\n", spec.architecture.c_str(),
		spec.version.c_str());
	std::printf("metadata name: %s\n", spec.name.empty() ? "(none)" : spec.name.c_str());
	std::printf("arrays: %zu  weights: %zu  receptive field: %d samples  "
	            "trained sample rate: %.0f Hz\n",
		spec.arrays.size(), spec.weightCount, spec.receptiveField, spec.sampleRate);
	std::printf("head_scale: %.6g\n", static_cast<double>(spec.headScale));
	if (spec.sampleRate > 0.0 && std::fabs(spec.sampleRate - opt.sampleRate) > 0.5)
	{
		std::printf("WARNING: model trained at %.0f Hz, harness running at %.0f Hz "
		            "(rate adaptation is not implemented)\n",
			spec.sampleRate, opt.sampleRate);
	}

	// ------------------------------------------------------------ wav-run mode
	// Process an existing WAV from a fresh model state, optionally write the
	// float32 output and compare it against a reference render.
	if (!opt.wavRun.empty())
	{
		std::vector<float> wavIn;
		double wavRate = 0.0;
		if (!readWav(opt.wavRun, wavIn, &wavRate))
		{
			std::fprintf(stderr, "error: could not read WAV %s (mono PCM16/24/32 or float32)\n",
				opt.wavRun.c_str());
			return 2;
		}
		std::printf("wav-run: %s  %zu samples  %.0f Hz  block %d\n", opt.wavRun.c_str(),
			wavIn.size(), wavRate, opt.blockSize);

		std::vector<float> wavOut(wavIn.size(), 0.0f);
		std::vector<float> inB(opt.blockSize), outB(opt.blockSize);
		size_t pos = 0;
		timespec t0{}, t1{};
		double cpuUs = 0.0;
		while (pos < wavIn.size())
		{
			const size_t n = std::min(static_cast<size_t>(opt.blockSize), wavIn.size() - pos);
			std::memcpy(inB.data(), wavIn.data() + pos, n * sizeof(float));
			clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t0);
			model->process(inB.data(), outB.data(), static_cast<int>(n));
			clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t1);
			cpuUs += (t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3;
			std::memcpy(wavOut.data() + pos, outB.data(), n * sizeof(float));
			pos += n;
		}
		const double audioSeconds = static_cast<double>(wavIn.size()) / wavRate;
		std::printf("wav-run cpu: %.3f ms for %.3f s of audio (%.2f %% of one core)\n",
			cpuUs / 1e3, audioSeconds, (cpuUs / 1e6) / audioSeconds * 100.0);

		if (!opt.wavOut.empty())
		{
			std::printf("wav-out: %s -> %s\n", opt.wavRun.c_str(), opt.wavOut.c_str());
			if (!writeWavF32(opt.wavOut, wavOut, wavRate)) { return 2; }
		}

		if (!opt.compareRef.empty())
		{
			std::vector<float> ref;
			double refRate = 0.0;
			if (!readWav(opt.compareRef, ref, &refRate))
			{
				std::fprintf(stderr, "error: could not read reference WAV %s\n",
					opt.compareRef.c_str());
				return 2;
			}
			if (ref.size() != wavOut.size())
			{
				std::fprintf(stderr, "error: reference has %zu samples, output has %zu\n",
					ref.size(), wavOut.size());
				return 2;
			}
			double maxAbs = 0.0, sumSq = 0.0, refSq = 0.0, dot = 0.0, outSq = 0.0;
			size_t worst = 0;
			for (size_t i = 0; i < ref.size(); ++i)
			{
				const double d = static_cast<double>(wavOut[i]) - static_cast<double>(ref[i]);
				if (std::fabs(d) > maxAbs) { maxAbs = std::fabs(d); worst = i; }
				sumSq += d * d;
				refSq += static_cast<double>(ref[i]) * static_cast<double>(ref[i]);
				outSq += static_cast<double>(wavOut[i]) * static_cast<double>(wavOut[i]);
				dot += static_cast<double>(wavOut[i]) * static_cast<double>(ref[i]);
			}
			const double rmsDiff = std::sqrt(sumSq / static_cast<double>(ref.size()));
			const double refRms = std::sqrt(refSq / static_cast<double>(ref.size()));
			const double relRms = rmsDiff / (refRms + 1e-30);
			const double corr = dot / (std::sqrt(outSq) * std::sqrt(refSq) + 1e-30);
			std::printf("\n(d) reference comparison (vs %s)\n", opt.compareRef.c_str());
			std::printf("  samples: %zu\n", ref.size());
			std::printf("  max abs diff: %.9g (at sample %zu)\n", maxAbs, worst);
			std::printf("  rms diff: %.9g   reference rms: %.9g\n", rmsDiff, refRms);
			std::printf("  relative rms error: %.9g (%.2f dB)\n", relRms,
				20.0 * std::log10(relRms + 1e-30));
			std::printf("  correlation: %.9f\n", corr);
			const bool refOk = (relRms < 1e-3) && (corr > 0.9999);
			std::printf("  reference match: %s\n", refOk ? "PASS" : "FAIL");
			return refOk ? 0 : 1;
		}
		return 0;
	}

	// --------------------------------------------------- generated-signal mode
	std::printf("block size: %d  blocks: %d  warmup: %d  sample rate: %.0f Hz\n",
		opt.blockSize, opt.blocks, opt.warmupBlocks, opt.sampleRate);

	// Deterministic input for the whole run.
	const int64_t totalSamples =
		static_cast<int64_t>(opt.blockSize) * (opt.blocks + opt.warmupBlocks);
	std::vector<float> input(totalSamples);
	for (int64_t i = 0; i < totalSamples; ++i)
	{
		input[i] = static_cast<float>(testSignal(i, opt.sampleRate));
	}
	std::vector<float> output(totalSamples, 0.0f);

	if (!opt.writeInputWav.empty())
	{
		if (!writeWavF32(opt.writeInputWav, input, opt.sampleRate))
		{
			std::fprintf(stderr, "error: could not write %s\n", opt.writeInputWav.c_str());
			return 2;
		}
		std::printf("wrote input WAV %s (%lld samples, float32)\n",
			opt.writeInputWav.c_str(), static_cast<long long>(input.size()));
	}

	std::vector<float> inBlock(opt.blockSize);
	std::vector<float> outBlock(opt.blockSize);

	// Warmup (state settling + cache warm).
	for (int b = 0; b < opt.warmupBlocks; ++b)
	{
		const int64_t off = static_cast<int64_t>(b) * opt.blockSize;
		std::memcpy(inBlock.data(), input.data() + off,
			static_cast<size_t>(opt.blockSize) * sizeof(float));
		model->process(inBlock.data(), outBlock.data(), opt.blockSize);
	}

	// Timed run.
	std::vector<double> blockUs;
	blockUs.reserve(opt.blocks);
	double cpuTotalUs = 0.0;
	double wallTotalUs = 0.0;

	const auto wallStart = std::chrono::steady_clock::now();
	for (int b = 0; b < opt.blocks; ++b)
	{
		const int64_t off =
			static_cast<int64_t>(opt.warmupBlocks + b) * opt.blockSize;
		std::memcpy(inBlock.data(), input.data() + off,
			static_cast<size_t>(opt.blockSize) * sizeof(float));

		timespec t0{};
		timespec t1{};
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t0);
		model->process(inBlock.data(), outBlock.data(), opt.blockSize);
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t1);

		const double cpuUs = (t1.tv_sec - t0.tv_sec) * 1e6 +
			(t1.tv_nsec - t0.tv_nsec) / 1e3;
		cpuTotalUs += cpuUs;
		blockUs.push_back(cpuUs);

		std::memcpy(output.data() + off, outBlock.data(),
			static_cast<size_t>(opt.blockSize) * sizeof(float));
	}
	const auto wallEnd = std::chrono::steady_clock::now();
	wallTotalUs = std::chrono::duration<double, std::micro>(wallEnd - wallStart).count();

	// Measured region only (skip warmup for signal statistics).
	std::vector<float> measuredIn(input.begin() + opt.warmupBlocks * opt.blockSize,
		input.end());
	std::vector<float> measuredOut(output.begin() + opt.warmupBlocks * opt.blockSize,
		output.end());

	// (a) sanity.
	int64_t nonFinite = 0;
	int64_t nonZero = 0;
	for (float v : measuredOut)
	{
		if (!std::isfinite(v)) { ++nonFinite; }
		else if (v != 0.0f) { ++nonZero; }
	}
	const double outPeak = peak(measuredOut);
	const double outRms = rms(measuredOut);
	const double inPeak = peak(measuredIn);
	const double inRms = rms(measuredIn);
	const bool finiteOk = (nonFinite == 0);
	const bool nonZeroOk = (outPeak > 1e-6) &&
		(nonZero > static_cast<int64_t>(measuredOut.size()) * 9 / 10);

	// (c) colouring metrics.
	double dot = 0.0, normIn = 0.0, normOut = 0.0, errNorm = 0.0;
	for (size_t i = 0; i < measuredIn.size(); ++i)
	{
		const double a = measuredIn[i];
		const double b = measuredOut[i];
		dot += a * b;
		normIn += a * a;
		normOut += b * b;
		errNorm += (b - a) * (b - a);
	}
	const double correlation = dot / (std::sqrt(normIn) * std::sqrt(normOut) + 1e-30);
	const double normError = std::sqrt(errNorm) / (std::sqrt(normIn) + 1e-30);
	const double rmsGain = outRms / (inRms + 1e-30);

	const bool coloursOk = (normError > 0.05) && (std::fabs(correlation) > 0.3);

	// Harmonic content at the fundamental and its first overtones.
	const double f0 = 110.0;
	std::printf("\n(a) sanity\n");
	std::printf("  output finite: %s (%lld non-finite of %zu)\n",
		finiteOk ? "yes" : "NO", static_cast<long long>(nonFinite),
		measuredOut.size());
	std::printf("  output non-zero: %s (peak %.6g, rms %.6g, non-zero %.2f%%)\n",
		nonZeroOk ? "yes" : "NO", outPeak, outRms,
		100.0 * static_cast<double>(nonZero) /
			static_cast<double>(measuredOut.size()));
	std::printf("  input  peak %.6g, rms %.6g\n", inPeak, inRms);

	const double audioSeconds =
		static_cast<double>(opt.blocks) * opt.blockSize / opt.sampleRate;
	std::sort(blockUs.begin(), blockUs.end());
	const double medianUs = blockUs[blockUs.size() / 2];
	const double p95Us = blockUs[static_cast<size_t>(
		0.95 * static_cast<double>(blockUs.size() - 1))];
	const double meanUs = cpuTotalUs / static_cast<double>(opt.blocks);
	const double cpuPercent = (cpuTotalUs / 1e6) / audioSeconds * 100.0;

	std::printf("\n(b) cpu\n");
	std::printf("  blocks: %d x %d frames (%.3f s of audio @ %.0f Hz)\n",
		opt.blocks, opt.blockSize, audioSeconds, opt.sampleRate);
	std::printf("  cpu total: %.3f ms   wall: %.3f ms\n", cpuTotalUs / 1e3,
		wallTotalUs / 1e3);
	std::printf("  cpu per block: %.2f us (median %.2f, p95 %.2f)\n", meanUs,
		medianUs, p95Us);
	std::printf("  realtime factor: %.1fx\n",
		audioSeconds / (cpuTotalUs / 1e6));
	std::printf("  cpu of one core: %.4f %%\n", cpuPercent);

	std::printf("\n(c) colouring\n");
	std::printf("  rms gain: %.4fx (in %.6g -> out %.6g)\n", rmsGain, inRms, outRms);
	std::printf("  correlation(in,out): %.4f\n", correlation);
	std::printf("  normalized error ||out-in||/||in||: %.4f\n", normError);
	std::printf("  harmonic magnitudes (dBFS):\n");
	for (int h = 1; h <= 6; ++h)
	{
		const double f = f0 * h;
		const double mi = goertzel(measuredIn, f, opt.sampleRate);
		const double mo = goertzel(measuredOut, f, opt.sampleRate);
		std::printf("    %6.1f Hz: in %8.2f  out %8.2f  delta %+7.2f dB\n", f,
			20.0 * std::log10(mi + 1e-30), 20.0 * std::log10(mo + 1e-30),
			20.0 * std::log10(mo + 1e-30) - 20.0 * std::log10(mi + 1e-30));
	}

	const bool specGate = cpuPercent < 5.0;    // spec §7 T2: <5% single core
	const bool stretchGate = cpuPercent < 1.0; // task stretch target
	std::printf("\nresult\n");
	std::printf("  (a) finite/non-zero: %s\n", (finiteOk && nonZeroOk) ? "PASS" : "FAIL");
	std::printf("  (b) cpu spec gate (<5%% of one core @ %.0f Hz): %s (%.4f %%)\n",
		opt.sampleRate, specGate ? "PASS" : "FAIL", cpuPercent);
	std::printf("      cpu stretch target (<1%% of one core): %s\n",
		stretchGate ? "PASS" : "FAIL");
	std::printf("  (c) signal colouring: %s\n", coloursOk ? "PASS" : "FAIL");
	std::printf("SUMMARY cpu_per_block_us=%.2f cpu_percent=%.4f rms_gain=%.4f "
	            "correlation=%.4f norm_error=%.4f finite=%d nonzero=%d\n",
		meanUs, cpuPercent, rmsGain, correlation, normError,
		finiteOk ? 1 : 0, nonZeroOk ? 1 : 0);

	if (!opt.wavPath.empty())
	{
		const bool ok = writeWav16(opt.wavPath, measuredOut, opt.sampleRate);
		std::printf("  wrote %s: %s\n", opt.wavPath.c_str(), ok ? "yes" : "FAILED");
	}

	return (finiteOk && nonZeroOk && coloursOk) ? 0 : 1;
}
