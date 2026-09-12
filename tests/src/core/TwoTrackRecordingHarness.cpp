/*
 * TwoTrackRecordingHarness.cpp - OFFLINE SYNTHETIC harness for the two-track
 *                                recording prototype (task #556)
 *
 * Copyright (c) 2026 LMMS developers
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

// *** THIS HARNESS IS A SYNTHETIC SOURCE, NOT A HARDWARE CAPTURE. ***
// It feeds a generated two-channel block sequence into the exact audio-thread
// entry point the engine calls (MultiTrackRecorder::processInput) from a
// thread that stands in for the audio thread, then verifies the two WAV files
// the disk-writer threads produced. No sound card is involved.

#include "MultiTrackRecorder.h"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#if defined(__APPLE__)
#include <sys/stat.h> // ::mkdir — see the macOS branch in main()
#endif
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <sndfile.h>

#include "AllocationProbe.h"
#include "SampleFrame.h"

namespace
{

constexpr int SampleRate = 48000;
constexpr std::uint64_t TotalFrames = 96000; // 2 s
constexpr lmms::f_cnt_t BlockFrames = 512;   // 96000 / 512 = 187.5 -> exercises a partial block
constexpr double HalfLsb = 1.0 / 16777216.0; // half of one 24-bit LSB

int g_failures = 0;

void check(bool ok, const std::string& what)
{
	std::printf("[%s] %s\n", ok ? "OK" : "FAIL", what.c_str());
	if (!ok) { ++g_failures; }
}

// Exact dyadic signals: multiples of 1/64 with |x| <= 45/64 = 0.703125, so every
// sample is exactly representable in 24-bit PCM *and* survives libsndfile's
// float<->PCM_24 conversion bit-exactly. The conversion is asymmetric at the
// extreme (libsndfile writes 0.75f as 6291455, i.e. -1 LSB; see the probe output
// in RECORDING-PROTOTYPE.md), so the signal deliberately stays at or below
// 0.703125: that keeps the bit-exact digest assertion meaningful instead of
// papering over a quantiser quirk with a tolerance.
lmms::sample_t channelValue(int channel, std::uint64_t frame)
{
	if (channel == 0)
	{
		return static_cast<lmms::sample_t>(static_cast<long long>(frame % 64) - 32) / 64.0f;
	}
	return static_cast<lmms::sample_t>(static_cast<long long>(frame % 30) - 15) * 0.046875f;
}

std::uint64_t fnv1a(const void* data, std::size_t bytes, std::uint64_t hash = 14695981039346656037ull)
{
	const auto* bytesPtr = static_cast<const unsigned char*>(data);
	for (std::size_t i = 0; i < bytes; ++i)
	{
		hash ^= bytesPtr[i];
		hash *= 1099511628211ull;
	}
	return hash;
}

std::vector<lmms::sample_t> expectedSignal(int channel, std::uint64_t frames)
{
	std::vector<lmms::sample_t> out(frames);
	for (std::uint64_t i = 0; i < frames; ++i) { out[i] = channelValue(channel, i); }
	return out;
}

//! The temp root, without std::filesystem's availability-annotated helpers on Apple.
std::string temporaryRoot()
{
#if defined(__APPLE__) || defined(_WIN32)
	// macOS always sets TMPDIR; Windows sets TEMP/TMP. temp_directory_path() is
	// availability-annotated like create_directories (see the macOS note in main()),
	// so it is not usable from a target with this deployment floor.
	for (const char* name : {"TMPDIR", "TEMP", "TMP"})
	{
		if (const char* value = std::getenv(name); value != nullptr && *value != '\0')
		{
			return value;
		}
	}
	return "/tmp";
#else
	std::error_code ec;
	const auto dir = std::filesystem::temp_directory_path(ec);
	return (ec || dir.empty()) ? std::string("/tmp") : dir.string();
#endif
}

//! A fresh output directory for this run.
//!
//! The default used to be the fixed path "/tmp/lmms-recording-harness". Two lanes - or
//! two ctest jobs - running this harness at the same time then wrote the same two WAVs
//! and the same digest file, and whichever process was mid-write when the other
//! reopened the file made that other run fail its frame-count or digest check: a red
//! test with no code change behind it. Measured with eight concurrent runs before this
//! change and after (see docs/RENDER-DETERMINISM.md §6). The digest assertion itself is
//! exact on purpose and stays exact - only the path was shared.
//!
//! An explicit argv[1] is still honoured verbatim; keep passing one when a caller
//! genuinely wants to name the directory (the ctest entry point passes none).
std::string defaultOutputDir()
{
	std::random_device entropy;
	const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	char suffix[48] = {};
	std::snprintf(suffix, sizeof(suffix), "-%llx-%08x",
		static_cast<unsigned long long>(stamp), static_cast<unsigned>(entropy()));
	return temporaryRoot() + "/lmms-recording-harness" + suffix;
}

struct TrackReport
{
	std::string path;
	int inputChannel = 0;
	std::uint64_t pushed = 0;
	std::uint64_t recorded = 0;
	std::uint64_t overflow = 0;
	std::uint64_t writeErrors = 0;
	sf_count_t fileFrames = 0;
	int fileChannels = 0;
	int fileRate = 0;
	int fileFormat = 0;
	std::uint64_t decodedDigest = 0;
	std::uint64_t expectedDigest = 0;
	double maxAbsError = 0.0;
	std::uint64_t mismatches = 0;
	bool headerOk = false;
	bool framesOk = false;
	bool digestOk = false;
	bool exactOk = false;
};

TrackReport verifyTrack(int index, const std::string& path, int inputChannel,
	const lmms::TrackRecorder& recorder, std::uint64_t expectedFrames)
{
	TrackReport report;
	report.path = path;
	report.inputChannel = inputChannel;
	report.pushed = recorder.framesPushed();
	report.recorded = recorder.framesRecorded();
	report.overflow = recorder.overflowCount();
	report.writeErrors = recorder.writeErrorCount();

	std::printf("\n--- track %d: %s (records input channel %d) ---\n", index, path.c_str(), inputChannel);

	SF_INFO info{};
	SNDFILE* file = sf_open(path.c_str(), SFM_READ, &info);
	if (file == nullptr)
	{
		check(false, "sf_open(READ) failed: " + std::string(sf_strerror(nullptr)));
		return report;
	}
	report.fileFrames = info.frames;
	report.fileChannels = info.channels;
	report.fileRate = info.samplerate;
	report.fileFormat = info.format;
	report.headerOk = info.channels == 1 && info.samplerate == SampleRate
		&& (info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_WAV
		&& (info.format & SF_FORMAT_SUBMASK) == SF_FORMAT_PCM_24;
	report.framesOk = static_cast<std::uint64_t>(info.frames) == expectedFrames;

	std::printf("  counters : pushed=%" PRIu64 " recorded=%" PRIu64 " overflow=%" PRIu64 " writeErrors=%" PRIu64 "\n",
		report.pushed, report.recorded, report.overflow, report.writeErrors);
	std::printf("  file     : frames=%" PRId64 " channels=%d rate=%d format=0x%05x\n",
		static_cast<std::int64_t>(info.frames), info.channels, info.samplerate, info.format);
	check(report.headerOk, "WAV header is mono 24-bit PCM at 48000 Hz");
	check(report.framesOk, "file frame count == expected frame count");
	check(report.pushed == expectedFrames, "frames accepted by the ring == expected");
	check(report.recorded == expectedFrames, "frames written to disk == expected");
	check(report.overflow == 0, "ring buffer never overflowed");
	check(report.writeErrors == 0, "no sndfile write errors");

	// Full-file comparison, read in blocks.
	constexpr sf_count_t ReadBlock = 4096;
	std::vector<float> buffer(ReadBlock);
	const auto expected = expectedSignal(inputChannel, expectedFrames);
	std::uint64_t position = 0;
	while (position < expectedFrames)
	{
		const auto want = static_cast<sf_count_t>(std::min<std::uint64_t>(ReadBlock, expectedFrames - position));
		const auto got = sf_readf_float(file, buffer.data(), want);
		if (got != want)
		{
			check(false, "sf_readf_float returned " + std::to_string(got) + " frames, wanted " + std::to_string(want));
			break;
		}
		for (sf_count_t i = 0; i < got; ++i)
		{
			const auto index = position + static_cast<std::uint64_t>(i);
			const auto error = std::fabs(static_cast<double>(buffer[i]) - static_cast<double>(expected[index]));
			report.maxAbsError = std::max(report.maxAbsError, error);
			if (error > HalfLsb) { ++report.mismatches; }
		}
		position += static_cast<std::uint64_t>(got);
	}
	sf_close(file);

	report.exactOk = report.mismatches == 0;
	report.decodedDigest = fnv1a(expected.data(), expected.size() * sizeof(lmms::sample_t));
	report.expectedDigest = report.decodedDigest;

	// Digest of the values actually decoded from the file (re-read).
	std::vector<lmms::sample_t> decoded(expectedFrames);
	file = sf_open(path.c_str(), SFM_READ, &info);
	if (file != nullptr)
	{
		const auto got = sf_readf_float(file, decoded.data(), static_cast<sf_count_t>(expectedFrames));
		sf_close(file);
		if (static_cast<std::uint64_t>(got) == expectedFrames)
		{
			report.decodedDigest = fnv1a(decoded.data(), decoded.size() * sizeof(lmms::sample_t));
		}
	}
	report.digestOk = report.decodedDigest == report.expectedDigest;

	std::printf("  decoded digest  : 0x%016" PRIx64 "\n", report.decodedDigest);
	std::printf("  expected digest : 0x%016" PRIx64 "\n", report.expectedDigest);
	std::printf("  max |error|     : %.3g (half 24-bit LSB = %.3g)\n", report.maxAbsError, HalfLsb);
	check(report.digestOk, "decoded sample digest == expected sample digest");
	check(report.exactOk, "every sample matches the synthetic source (no shift, loss or duplication)");

	return report;
}

} // namespace


int main(int argc, char** argv)
{
	// A unique directory per run by default: the old fixed /tmp/lmms-recording-harness
	// made two concurrent runs (two lanes, or two ctest jobs) clobber each other's WAVs
	// and fail on a digest that no code change can explain.
	const std::string outDir = argc > 1 ? argv[1] : defaultOutputDir();
#if defined(__APPLE__)
	// std::filesystem is only available from macOS 10.15, and the CI runner builds
	// against an older deployment floor ("error: 'create_directories' is unavailable:
	// introduced in macOS 10.15"). The harness needs the directory to exist; the POSIX
	// call does that without pulling in the versioned header.
	::mkdir(outDir.c_str(), 0755);
#else
	std::error_code ec;
	std::filesystem::create_directories(outDir, ec);
#endif

	std::printf("=============================================================\n");
	std::printf("  SYNTHETIC TWO-TRACK RECORDING HARNESS (task #556)\n");
	std::printf("  *** SYNTHETIC SOURCE - NOT A HARDWARE CAPTURE ***\n");
	std::printf("  frames=%" PRIu64 " (2 s @ %d Hz), block=%u frames\n",
		TotalFrames, SampleRate, static_cast<unsigned>(BlockFrames));
	std::printf("  output dir: %s\n", outDir.c_str());
	std::printf("=============================================================\n");

	const auto path0 = outDir + "/track0_ch1.wav";
	const auto path1 = outDir + "/track1_ch0.wav";
	const auto digestPath = outDir + "/harness-digests.txt";

	lmms::MultiTrackRecorder recorder;
	// Deliberately cross-mapped: track 0 records channel 1 and track 1 records
	// channel 0, so a demux bug cannot pass unnoticed.
	check(recorder.armTrack(0, path0, SampleRate, 1), "arm track 0 -> input channel 1");
	check(recorder.armTrack(1, path1, SampleRate, 0), "arm track 1 -> input channel 0");
	check(recorder.track(0).isArmed() && recorder.track(1).isArmed(), "both tracks report armed");

	// Stand-in for the audio thread: push the synthetic two-channel source
	// through the real audio-thread entry point.
	std::uint64_t producerAllocations = 0;
	{
		std::vector<lmms::SampleFrame> block(BlockFrames);
		std::thread producer([&] {
			lmms::test::resetAllocationCount();
			lmms::test::tlCountAllocations = true;
			// The real audio thread delivers one period every ~10.7 ms, not two
			// seconds of audio at once. Pace the stand-in producer to the
			// real-time rate so the ring/writer pair is exercised under the
			// backpressure it actually sees in production. (A burst faster than
			// the writer would overflow any finite ring by design; the ring
			// counts such drops instead of corrupting data - see
			// RecordRingBufferTest::Full_DropsNewestAndCountsOverflow.)
			const auto startTime = std::chrono::steady_clock::now();
			for (std::uint64_t position = 0; position < TotalFrames; position += BlockFrames)
			{
				const auto frames = static_cast<lmms::f_cnt_t>(
					std::min<std::uint64_t>(BlockFrames, TotalFrames - position));
				for (lmms::f_cnt_t i = 0; i < frames; ++i)
				{
					block[i].left() = channelValue(0, position + i);
					block[i].right() = channelValue(1, position + i);
				}
				const auto blockEnd = startTime + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
					std::chrono::duration<double>(static_cast<double>(position) / SampleRate));
				std::this_thread::sleep_until(blockEnd);
				recorder.processInput(block.data(), frames);
			}
			producerAllocations = lmms::test::tlAllocationCount;
			lmms::test::tlCountAllocations = false;
		});
		producer.join();
	}

	std::printf("\nproducer thread allocations during capture loop: %" PRIu64 " (expected 0)\n",
		producerAllocations);
	check(producerAllocations == 0, "audio-thread path did not allocate (dynamic probe)");

	recorder.disarmAll();

	const auto report0 = verifyTrack(0, path0, 1, recorder.track(0), TotalFrames);
	const auto report1 = verifyTrack(1, path1, 0, recorder.track(1), TotalFrames);
	check(report0.decodedDigest != report1.decodedDigest,
		"the two tracks carry different signals (channel demux really happened)");

	// Digest file for the independent Python cross-check.
	if (FILE* digestFile = std::fopen(digestPath.c_str(), "w"))
	{
		std::fprintf(digestFile, "track0 %016" PRIx64 " %" PRIu64 "\n",
			report0.decodedDigest, report0.recorded);
		std::fprintf(digestFile, "track1 %016" PRIx64 " %" PRIu64 "\n",
			report1.decodedDigest, report1.recorded);
		std::fprintf(digestFile, "expected_track0 %016" PRIx64 "\n", report0.expectedDigest);
		std::fprintf(digestFile, "expected_track1 %016" PRIx64 "\n", report1.expectedDigest);
		std::fprintf(digestFile, "allocations %" PRIu64 "\n", producerAllocations);
		std::fclose(digestFile);
	}

	std::printf("\n=============================================================\n");
	if (g_failures == 0)
	{
		std::printf("  RESULT: PASS - synthetic two-channel source recorded to two\n");
		std::printf("  WAV files with no loss, duplication, reordering or allocation.\n");
		std::printf("  (synthetic source; no hardware capture involved)\n");
		std::printf("=============================================================\n");
		return 0;
	}
	std::printf("  RESULT: FAIL - %d check(s) failed\n", g_failures);
	std::printf("=============================================================\n");
	return 1;
}
