/*
 * AudioFileProcessorWaveViewTest.cpp - leftward waveform marker drags keep offsets signed
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>

#include <cmath>
#include <memory>
#include <vector>

#include "AudioFileProcessorWaveView.h"
#include "AutomatableModel.h"
#include "Engine.h"
#include "Sample.h"
#include "SampleBuffer.h"

using namespace lmms;
using namespace lmms::gui;

class AudioFileProcessorWaveViewTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()	{ Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }

	void draggingStartMarkerLeftPreservesTheSignedFrameOffset()
	{
		constexpr int SampleRate = 44100;
		constexpr int FrameCount = 8'000'000;
		std::vector<SampleFrame> frames(FrameCount, SampleFrame{0.25f, 0.25f});
		const auto buffer = std::make_shared<const SampleBuffer>(std::move(frames), SampleRate);
		Sample sample(buffer);
		sample.setAllPointFrames(400'000, FrameCount, 4'000'000, FrameCount);

		FloatModel startModel(0.05f, 0.0f, 1.0f, 0.0000001f);
		FloatModel endModel(1.0f, 0.0f, 1.0f, 0.0000001f);
		FloatModel loopModel(0.5f, 0.0f, 1.0f, 0.0000001f);
		AudioFileProcessorWaveView::knob startKnob(nullptr);
		AudioFileProcessorWaveView::knob endKnob(nullptr);
		AudioFileProcessorWaveView::knob loopKnob(nullptr);
		startKnob.setModel(&startModel);
		endKnob.setModel(&endModel);
		loopKnob.setModel(&loopModel);

		AudioFileProcessorWaveView view(nullptr, 245, 75, &sample,
			&startKnob, &endKnob, &loopKnob);
		view.show();
		view.grab();

		const auto initialValue = startModel.value();
		const QPoint markerPosition(14, 37);
		const QPoint draggedPosition(13, 37);
		QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, markerPosition);
		QTest::mouseMove(&view, draggedPosition);
		QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, draggedPosition);

		const long expectedFrameOffset = static_cast<long>((-1.0 / view.width()) * FrameCount);
		const auto expectedValue = initialValue
			+ static_cast<float>(static_cast<double>(expectedFrameOffset) / sample.sampleSize());
		QVERIFY(startModel.value() < initialValue);
		QVERIFY(std::abs(startModel.value() - expectedValue) < 0.000001f);
	}
};

QTEST_MAIN(AudioFileProcessorWaveViewTest)
#include "AudioFileProcessorWaveViewTest.moc"
