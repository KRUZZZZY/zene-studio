/*
 * TwoTrackAlsaCaptureProbe.cpp - REAL HARDWARE capture probe (task #556)
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

// This probe is a REAL hardware capture, but NOT through the LMMS audio
// backend: LMMS's ALSA backend has no capture path at this commit
// (AudioAlsa.cpp contains no snd_pcm_readi), so nothing can feed
// AudioEngine::pushInputFrames() under ALSA. The probe therefore opens the
// device with libasound directly and drives the prototype's real audio-thread
// entry point (MultiTrackRecorder::processInput) from a thread that stands in
// for the engine's audio thread, using real device samples.
//
// A reference stereo WAV is written from the same pre-allocated frame buffer
// the capture loop filled, so every track WAV can be checked sample-exactly
// against the channel it was supposed to record.
//
// Usage: TwoTrackAlsaCaptureProbe [device] [seconds] [outDir]
//        defaults: hw:1,0 3 /tmp/lmms-recording-alsa

#include "MultiTrackRecorder.h"

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <alsa/asoundlib.h>
#include <sndfile.h>

#include "AllocationProbe.h"
#include "SampleFrame.h"

namespace
{

constexpr int SampleRate = 48000;
constexpr snd_pcm_uframes_t PeriodFrames = 512;
constexpr snd_pcm_uframes_t BufferFrames = 4096;

int g_failures = 0;

void check(bool ok, const std::string& what)
{
	std::printf("[%s] %s\n", ok ? "OK" : "FAIL", what.c_str());
	if (!ok) { ++g_failures; }
}

} // namespace


int main(int argc, char** argv)
{
	const std::string device = argc > 1 ? argv[1] : "hw:1,0";
	const int seconds = argc > 2 ? std::atoi(argv[2]) : 3;
	const std::string outDir = argc > 3 ? argv[3] : "/tmp/lmms-recording-alsa";
	const std::uint64_t totalFrames = static_cast<std::uint64_t>(seconds) * SampleRate;

	std::error_code ec;
	std::filesystem::create_directories(outDir, ec);
	if (ec)
	{
		std::printf("FAIL: cannot create output dir %s: %s\n", outDir.c_str(), ec.message().c_str());
		return 1;
	}

	std::printf("=============================================================\n");
	std::printf("  REAL HARDWARE TWO-TRACK CAPTURE PROBE (task #556)\n");
	std::printf("  device=%s seconds=%d frames=%" PRIu64 "\n", device.c_str(), seconds, totalFrames);
	std::printf("  NOTE: libasound capture, driving the prototype's audio-thread\n");
	std::printf("  entry point directly (LMMS ALSA backend has no capture path).\n");
	std::printf("=============================================================\n");

	// --- open and configure the device -------------------------------------
	snd_pcm_t* pcm = nullptr;
	int err = snd_pcm_open(&pcm, device.c_str(), SND_PCM_STREAM_CAPTURE, 0);
	if (err < 0)
	{
		std::printf("FAIL: snd_pcm_open(%s): %s\n", device.c_str(), snd_strerror(err));
		return 1;
	}

	snd_pcm_hw_params_t* hw = nullptr;
	snd_pcm_hw_params_alloca(&hw);
	snd_pcm_hw_params_any(pcm, hw);
	snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE);
	const unsigned int channels = 2;
	snd_pcm_hw_params_set_channels(pcm, hw, channels);
	unsigned int rate = SampleRate;
	snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, nullptr);
	snd_pcm_uframes_t period = PeriodFrames;
	snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, nullptr);
	snd_pcm_uframes_t buffer = BufferFrames;
	snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &buffer);

	err = snd_pcm_hw_params(pcm, hw);
	if (err < 0)
	{
		std::printf("FAIL: snd_pcm_hw_params: %s\n", snd_strerror(err));
		return 1;
	}

	// Report what the device actually granted.
	unsigned int actualRate = 0;
	unsigned int actualChannels = 0;
	snd_pcm_uframes_t actualPeriod = 0;
	snd_pcm_uframes_t actualBuffer = 0;
	snd_pcm_hw_params_get_rate(hw, &actualRate, nullptr);
	snd_pcm_hw_params_get_channels(hw, &actualChannels);
	snd_pcm_hw_params_get_period_size(hw, &actualPeriod, nullptr);
	snd_pcm_hw_params_get_buffer_size(hw, &actualBuffer);
	std::printf("granted: rate=%u channels=%u period=%lu buffer=%lu format=S16_LE\n",
		actualRate, actualChannels, static_cast<unsigned long>(actualPeriod),
		static_cast<unsigned long>(actualBuffer));
	check(actualChannels == 2, "device really has 2 capture channels");
	check(actualRate == SampleRate, "device runs at 48000 Hz");

	// --- pre-allocate everything before the capture loop -------------------
	const auto periodSize = static_cast<std::size_t>(actualPeriod);
	std::vector<std::int16_t> raw(periodSize * actualChannels);
	std::vector<lmms::SampleFrame> block(periodSize);
	std::vector<lmms::SampleFrame> reference(totalFrames); // ground truth copy

	lmms::MultiTrackRecorder recorder;
	const auto path0 = outDir + "/track0_ch1.wav";
	const auto path1 = outDir + "/track1_ch0.wav";
	const auto refPath = outDir + "/reference_stereo.wav";
	check(recorder.armTrack(0, path0, SampleRate, 1), "arm track 0 -> input channel 1");
	check(recorder.armTrack(1, path1, SampleRate, 0), "arm track 1 -> input channel 0");

	err = snd_pcm_prepare(pcm);
	if (err < 0)
	{
		std::printf("FAIL: snd_pcm_prepare: %s\n", snd_strerror(err));
		return 1;
	}
	err = snd_pcm_start(pcm);
	if (err < 0)
	{
		std::printf("FAIL: snd_pcm_start: %s\n", snd_strerror(err));
		return 1;
	}

	// --- capture thread: stands in for the audio thread --------------------
	std::uint64_t captured = 0;
	std::uint64_t xruns = 0;
	std::uint64_t producerAllocations = 0;
	{
		std::thread captureThread([&] {
			lmms::test::resetAllocationCount();
			lmms::test::tlCountAllocations = true;
			while (captured < totalFrames)
			{
				const auto want = static_cast<snd_pcm_uframes_t>(
					std::min<std::uint64_t>(periodSize, totalFrames - captured));
				const auto got = snd_pcm_readi(pcm, raw.data(), want);
				if (got < 0)
				{
					++xruns;
					snd_pcm_prepare(pcm);
					snd_pcm_start(pcm);
					continue;
				}
				for (snd_pcm_uframes_t i = 0; i < static_cast<snd_pcm_uframes_t>(got); ++i)
				{
					block[i].left() = static_cast<float>(raw[i * actualChannels]) / 32768.0f;
					block[i].right() = static_cast<float>(raw[i * actualChannels + 1]) / 32768.0f;
					reference[captured + i] = block[i]; // pre-allocated, no alloc
				}
				recorder.processInput(block.data(), static_cast<lmms::f_cnt_t>(got));
				captured += static_cast<std::uint64_t>(got);
			}
			producerAllocations = lmms::test::tlAllocationCount;
			lmms::test::tlCountAllocations = false;
		});
		captureThread.join();
	}

	snd_pcm_drop(pcm);
	snd_pcm_close(pcm);

	recorder.disarmAll();

	std::printf("\ncaptured frames      : %" PRIu64 " (expected %" PRIu64 ")\n", captured, totalFrames);
	std::printf("xruns                : %" PRIu64 "\n", xruns);
	std::printf("capture-thread allocs: %" PRIu64 " (expected 0)\n", producerAllocations);
	check(captured == totalFrames, "captured exactly the requested number of frames");
	check(producerAllocations == 0, "capture path did not allocate (dynamic probe)");

	for (int t = 0; t < lmms::MultiTrackRecorder::NumTracks; ++t)
	{
		const auto& track = recorder.track(t);
		std::printf("track %d (channel %d): pushed=%" PRIu64 " recorded=%" PRIu64
			" overflow=%" PRIu64 " writeErrors=%" PRIu64 " file=%s\n",
			t, track.inputChannel(), track.framesPushed(), track.framesRecorded(),
			track.overflowCount(), track.writeErrorCount(), track.filePath().c_str());
		check(track.framesPushed() == totalFrames, "track " + std::to_string(t) + " pushed all frames");
		check(track.framesRecorded() == totalFrames, "track " + std::to_string(t) + " recorded all frames");
		check(track.overflowCount() == 0, "track " + std::to_string(t) + " ring never overflowed");
		check(track.writeErrorCount() == 0, "track " + std::to_string(t) + " had no write errors");
	}

	// --- reference stereo WAV from the exact captured frames ---------------
	SF_INFO info{};
	info.samplerate = SampleRate;
	info.channels = 2;
	info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;
	SNDFILE* ref = sf_open(refPath.c_str(), SFM_WRITE, &info);
	if (ref == nullptr)
	{
		std::printf("FAIL: cannot open reference WAV: %s\n", sf_strerror(nullptr));
		return 1;
	}
	std::vector<float> interleaved(totalFrames * 2);
	for (std::uint64_t i = 0; i < totalFrames; ++i)
	{
		interleaved[i * 2] = reference[i].left();
		interleaved[i * 2 + 1] = reference[i].right();
	}
	const auto refWritten = sf_writef_float(ref, interleaved.data(), static_cast<sf_count_t>(totalFrames));
	sf_write_sync(ref);
	sf_close(ref);
	check(static_cast<std::uint64_t>(refWritten) == totalFrames, "reference stereo WAV written");

	std::printf("\nreference: %s (stereo ground truth of the same frames)\n", refPath.c_str());
	std::printf("tracks   : %s, %s\n", path0.c_str(), path1.c_str());

	std::printf("\n=============================================================\n");
	if (g_failures == 0)
	{
		std::printf("  RESULT: PASS - real hardware capture of %" PRIu64 " frames\n", totalFrames);
		std::printf("  (device %s, 2 channels, xruns=%" PRIu64 ")\n", device.c_str(), xruns);
		std::printf("  Sample-exact comparison against reference_stereo.wav:\n");
		std::printf("  run recording/verify_alsa_probe.py\n");
		std::printf("=============================================================\n");
		return 0;
	}
	std::printf("  RESULT: FAIL - %d check(s) failed\n", g_failures);
	std::printf("=============================================================\n");
	return 1;
}
