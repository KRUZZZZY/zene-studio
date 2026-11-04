/*
 * Copyright (c) 2026 Zene Studio contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>
#include <samplerate.h>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "AllocationProbe.h"
#include "AudioResampler.h"
#include "Engine.h"
#include "ExportRenderSettings.h"
#include "PlayHandle.h"
#include "RealtimeScope.h"

using namespace lmms;

class AudioResamplerPoolMemoryTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase()
	{
		ExportRenderSettings::reset();
		Engine::destroy();
	}

	// Exercise every available slot in a single burst, with no service tick.
	// Merely checking mode() would miss a converter that reports SincBest but
	// processes its first block with the old quality, so compare its samples too.
	void everySlotStartsWithSelectedQuality()
	{
		const auto capacity = AudioResampler::realtimeVoiceConverterCapacityForTest();
		const auto available = AudioResampler::freeRealtimeVoiceConvertersForTest();
		QVERIFY(capacity > 2 * PlayHandle::MaxNumber);
		QVERIFY(available > 2 * PlayHandle::MaxNumber);
		QVERIFY(ExportRenderSettings::setSrcQuality(SrcQuality::SincBest));
		QCOMPARE(AudioResampler::realtimeVoiceConverterCapacityForTest(), capacity);
		QCOMPARE(AudioResampler::freeRealtimeVoiceConvertersForTest(), available);

		std::array<float, 1024> input;
		for (std::size_t i = 0; i < input.size(); ++i) { input[i] = std::sin(i * 0.11); }
		std::array<float, 128> expected{};
		AudioResampler reference(AudioResampler::Mode::SincBest);
		const auto result = reference.process({input.data(), 2, 512}, {expected.data(), 2, 64});
		QCOMPARE(result.outputFramesGenerated, f_cnt_t{64});

		std::vector<AudioResampler> voices;
		voices.reserve(available);
		std::size_t wrongQuality = 0;
		std::size_t wrongSamples = 0;
		std::array<float, 128> output;
		AudioResampler::resetRealtimeConverterAllocationsForTest();
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		{
			const RealtimeScope realtime;
			for (std::size_t i = 0; i < available; ++i)
			{
				auto& voice = voices.emplace_back(AudioResampler::RealtimeVoiceTag{});
				wrongQuality += voice.mode() != AudioResampler::Mode::SincBest;
				const auto actual = voice.process({input.data(), 2, 512}, {output.data(), 2, 64});
				wrongSamples += actual.inputFramesUsed != result.inputFramesUsed
					|| actual.outputFramesGenerated != result.outputFramesGenerated
					|| std::memcmp(output.data(), expected.data(), sizeof(output)) != 0;
			}
		}
		test::tlCountAllocations = false;
		QCOMPARE(wrongQuality, std::size_t{0});
		QCOMPARE(wrongSamples, std::size_t{0});
		QCOMPARE(test::tlAllocationCount, std::uint64_t{0});
		QCOMPARE(test::tlFreeCount, std::uint64_t{0});
		QCOMPARE(AudioResampler::realtimeConverterAllocationsForTest(), std::uint64_t{0});
		voices.clear();
		QCOMPARE(AudioResampler::freeRealtimeVoiceConvertersForTest(), available);
		QVERIFY(ExportRenderSettings::setSrcQuality(SrcQuality::Linear));
	}

	void failedSwitchPreservesOldQuality_data()
	{
		QTest::addColumn<int>("skip");
		QTest::newRow("first") << 0;
		QTest::newRow("after-one-hundred") << 100;
	}

	void failedSwitchPreservesOldQuality()
	{
		QFETCH(int, skip);
		const auto capacity = AudioResampler::realtimeVoiceConverterCapacityForTest();
		AudioResampler voice(AudioResampler::RealtimeVoiceTag{});
		AudioResampler::forceConverterAllocationFailuresForTest(1, skip);
		const bool switched = ExportRenderSettings::setSrcQuality(SrcQuality::SincBest);
		AudioResampler::forceConverterAllocationFailuresForTest(0);
		QVERIFY(!switched);
		QCOMPARE(ExportRenderSettings::srcQuality(), SrcQuality::Linear);
		QCOMPARE(voice.mode(), AudioResampler::Mode::Linear);
		QCOMPARE(AudioResampler::realtimeVoiceConverterCapacityForTest(), capacity);
		QVERIFY(ExportRenderSettings::setSrcQuality(SrcQuality::SincBest));
		{
			AudioResampler next(AudioResampler::RealtimeVoiceTag{});
			QCOMPARE(next.mode(), AudioResampler::Mode::SincBest);
		}
		QVERIFY(ExportRenderSettings::setSrcQuality(SrcQuality::Linear));
	}

	void resetAndStreamingMatchSystemLibrary_data()
	{
		QTest::addColumn<int>("converter");
		QTest::addColumn<int>("channels");
		for (const int converter : {SRC_SINC_FASTEST, SRC_SINC_MEDIUM_QUALITY, SRC_SINC_BEST_QUALITY})
		{
			for (const int channels : {1, 2, 4, 6, 8})
			{
				QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(converter).arg(channels)))
					<< converter << channels;
			}
		}
	}

	// Keep the unmodified system backend as an independent numerical oracle.
	// Small input pieces, compaction, ratio changes, extreme supported ratios,
	// empty calls and reset-after-use must all preserve counts AND sample bits.
	void resetAndStreamingMatchSystemLibrary()
	{
		QFETCH(int, converter);
		QFETCH(int, channels);
		const auto mode = converter == SRC_SINC_FASTEST ? AudioResampler::Mode::SincFastest
			: converter == SRC_SINC_MEDIUM_QUALITY ? AudioResampler::Mode::SincMedium
			: AudioResampler::Mode::SincBest;
		AudioResampler actual(mode, channels);
		int error = 0;
		std::unique_ptr<SRC_STATE, decltype(&src_delete)> reference(src_new(converter, channels, &error), src_delete);
		QVERIFY(reference != nullptr);
		std::vector<float> input(8192 * channels);
		for (std::size_t i = 0; i < input.size(); ++i) { input[i] = std::sin(i * 0.07) + 0.1f; }
		std::vector<float> expected(256 * channels);
		std::vector<float> output(expected.size());
		for (int pass = 0; pass < 2; ++pass)
		{
			actual.reset();
			QCOMPARE(src_reset(reference.get()), 0);
			for (const double ratio : {1.0, 0.5, 2.0, 1.0 / 256, 256.0, 0.01, 1.3})
			{
				actual.setRatio(ratio);
				for (int piece = 0; piece < 40; ++piece)
				{
					SRC_DATA data{};
					data.data_in = input.data();
					data.data_out = expected.data();
					data.input_frames = piece % 4 == 0 ? 0 : piece % 3 == 0 ? 8192 : 47;
					data.output_frames = 256;
					data.src_ratio = ratio;
					QCOMPARE(src_process(reference.get(), &data), 0);
					const auto result = actual.process({input.data(), ch_cnt_t(channels), f_cnt_t(data.input_frames)},
						{output.data(), ch_cnt_t(channels), 256});
					QCOMPARE(long(result.inputFramesUsed), data.input_frames_used);
					QCOMPARE(long(result.outputFramesGenerated), data.output_frames_gen);
					QVERIFY(std::memcmp(output.data(), expected.data(), data.output_frames_gen * channels * sizeof(float)) == 0);
				}
			}
		}
	}
};

QTEST_GUILESS_MAIN(AudioResamplerPoolMemoryTest)
#include "AudioResamplerPoolMemoryTest.moc"
