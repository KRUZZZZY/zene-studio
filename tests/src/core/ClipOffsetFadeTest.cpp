/*
 * ClipOffsetFadeTest.cpp - the offset fade: a crossfade into a clip that lies
 *                          wholly inside another one fades the CONTAINER where
 *                          the contained clip is and leaves its tail unfaded
 *                          (owner decision 2026-10-08, core lane item 3).
 *
 * Copyright (c) 2026 Zene Studio contributors
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "AllocationProbe.h"
#include "ArrangementEdits.h"
#include "AudioEngine.h"
#include "ClipEdits.h"
#include "ControlClipLinkSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "OutputSettings.h"
#include "ProjectRenderer.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

constexpr int kSampleRate = 44100;
constexpr float kContainerLevel = 0.5f;
constexpr float kContainedLevel = 0.25f;
constexpr double kHalfPi = 1.5707963267948966;

int oneSecondTicks() { return static_cast<int>(kSampleRate / Engine::framesPerTick(kSampleRate)); }
int framesAt(int ticks) { return static_cast<int>(ticks * Engine::framesPerTick(kSampleRate)); }

std::shared_ptr<const SampleBuffer> constantBuffer(float level, int seconds)
{
	const auto frames = static_cast<std::size_t>(seconds) * kSampleRate;
	return std::make_shared<const SampleBuffer>(
		std::vector<SampleFrame>(frames, SampleFrame(level, level)).data(), frames, kSampleRate);
}

int dataOffset(const QByteArray& wav)
{
	int offset = 12;
	while (offset + 8 <= wav.size())
	{
		qint32 size = 0;
		std::memcpy(&size, wav.constData() + offset + 4, sizeof(size));
		if (wav.mid(offset, 4) == QByteArrayLiteral("data")) { return offset + 8; }
		offset += 8 + size + (size & 1);
	}
	return -1;
}

float wavLeft(const QByteArray& wav, int frame)
{
	const int offset = dataOffset(wav);
	if (offset < 0) { return 0.0f; }
	qint16 value = 0;
	std::memcpy(&value, wav.constData() + offset + frame * 4, sizeof(value));
	return static_cast<float>(value) / 32768.0f;
}

//! clipFadeGainAt exactly as it was before the offset fades existed (the body this release replaced),
//! kept here as the reference an old project's envelope is held to.
float envelopeBeforeOffsetFades(f_cnt_t frame, f_cnt_t clipFrames, f_cnt_t fadeInFrames, f_cnt_t fadeOutFrames,
	FadeShape fadeInShape, FadeShape fadeOutShape)
{
	if (clipFrames == 0) { return 1.0f; }
	double value = 1.0;
	if (fadeInFrames > 0 && frame < fadeInFrames)
	{
		value *= fadeShapeGain(fadeInShape, static_cast<double>(frame) / static_cast<double>(fadeInFrames));
	}
	if (fadeOutFrames > 0)
	{
		const f_cnt_t fadeOutStart = clipFrames > fadeOutFrames ? clipFrames - fadeOutFrames : 0;
		if (frame >= fadeOutStart)
		{
			const auto remaining = clipFrames > frame ? clipFrames - frame : 0;
			value *= fadeShapeGain(fadeOutShape, static_cast<double>(remaining) / static_cast<double>(fadeOutFrames));
		}
	}
	return static_cast<float>(value);
}

} // namespace

class ClipOffsetFadeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The render: a container one second long with a clip over its second quarter, crossfaded the way
	//! the arranger crossfades a drop. The container is ducked under the contained clip by an equal-power
	//! ramp and is audible - not faded - from the contained clip's end to its own.
	void aContainerFadesUnderTheContainedClipAndKeepsItsTail()
	{
		const int second = oneSecondTicks();
		SampleClip* container = buildScene(second, second / 4, second / 4);
		QCOMPARE(arrangement::applyOverlap(m_contained, arrangement::Overlap::Crossfade), 1);

		const ClipEdits out = container->clipEdits();
		QCOMPARE(out.fadeOutTicks, second / 4);
		QCOMPARE(out.fadeOutOffsetTicks, second - second / 2);  // the container ends 1/2 s after the contained clip does
		QCOMPARE(m_contained->clipEdits().fadeInTicks, second / 4);
		QCOMPARE(m_contained->clipEdits().fadeInOffsetTicks, 0);

		const QByteArray wav = render();
		QVERIFY2(!wav.isEmpty(), "the export produced no file");
		const int start = framesAt(second / 4);
		const int span = framesAt(second / 4);
		// Before the contained clip: the container alone, unfaded.
		QVERIFY2(std::fabs(wavLeft(wav, start / 2) - kContainerLevel) < 0.005f, "the container is not at unity before the crossfade");
		// Inside it: the sum of the two equal-power ramps (container going down, contained coming up).
		for (int i = 1; i < 8; ++i)
		{
			const double x = i / 8.0;
			const double expected = kContainerLevel * std::sin((1.0 - x) * kHalfPi) + kContainedLevel * std::sin(x * kHalfPi);
			const float measured = wavLeft(wav, start + static_cast<int>(span * x));
			QVERIFY2(std::fabs(measured - expected) < 0.02f,
				qPrintable(QStringLiteral("crossfade at x=%1 measured %2, expected %3").arg(x).arg(measured).arg(expected)));
		}
		// After the contained clip ends the container is back at unity, all the way to its own end.
		for (const int at : {start + span + 2048, framesAt(second * 3 / 4), framesAt(second) - 4096})
		{
			QVERIFY2(std::fabs(wavLeft(wav, at) - kContainerLevel) < 0.005f,
				qPrintable(QStringLiteral("the container is not audible and unfaded at frame %1 (measured %2)").arg(at).arg(wavLeft(wav, at))));
		}
	}

	//! The render path allocates nothing with an offset fade live (the handle is built outside the probe).
	void theOffsetFadeRendersWithoutAllocating()
	{
		const int second = oneSecondTicks();
		SampleClip* container = buildScene(second, second / 4, second / 4);
		ClipEdits edits;
		edits.fadeInTicks = 48;
		edits.fadeInOffsetTicks = 24;
		edits.fadeOutTicks = 96;
		edits.fadeOutOffsetTicks = 192;
		edits.fadeOutShape = FadeShape::EqualPower;
		container->setClipEdits(edits);

		SamplePlayHandle handle(container);
		std::vector<SampleFrame> buffer(1024, SampleFrame(0.0f, 0.0f));
		const auto span = std::span<SampleFrame>(buffer);
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		for (int period = 0; period < 64; ++period) { handle.play(span); }
		test::tlCountAllocations = false;
		QCOMPARE(static_cast<qulonglong>(test::tlAllocationCount), static_cast<qulonglong>(0));
	}

	//! clip.crossfade writes the same offset, reports it, and one undo takes it away.
	void theCrossfadeCommandUsesTheOffsetAndUndoRestoresIt()
	{
		const int second = oneSecondTicks();
		SampleClip* container = buildScene(second, second / 4, second / 4);
		const ControlResult done = ControlRegistry::instance()->invoke(QStringLiteral("clip.crossfade"),
			QJsonObject{{QStringLiteral("out"), control::clipIdOf(container)}, {QStringLiteral("in"), control::clipIdOf(m_contained)}});
		QVERIFY2(done.ok, qPrintable(done.errorMessage));
		QCOMPARE(done.result.value(QStringLiteral("overlap")).toInt(), second / 4);
		QCOMPARE(done.result.value(QStringLiteral("out_fade_out_offset")).toInt(), second / 2);
		QVERIFY(!done.result.contains(QStringLiteral("in_fade_in_offset")));
		QCOMPARE(container->clipEdits().fadeOutOffsetTicks, second / 2);

		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo")).ok);
		QCOMPARE(container->clipEdits().fadeOutTicks, 0);
		QCOMPARE(container->clipEdits().fadeOutOffsetTicks, 0);
		QCOMPARE(m_contained->clipEdits().fadeInTicks, 0);
	}

	//! The container as the INCOMING clip (the contained clip fades out into it): its fade-in starts where the
	//! contained clip starts, and it is unfaded before that.
	void aContainerThatIsTheIncomingClipFadesInAtTheOverlap()
	{
		const int second = oneSecondTicks();
		SampleClip* container = buildScene(second, second / 4, second / 4);
		const ControlResult done = ControlRegistry::instance()->invoke(QStringLiteral("clip.crossfade"),
			QJsonObject{{QStringLiteral("out"), control::clipIdOf(m_contained)}, {QStringLiteral("in"), control::clipIdOf(container)}});
		QVERIFY2(done.ok, qPrintable(done.errorMessage));
		QCOMPARE(container->clipEdits().fadeInTicks, second / 4);
		QCOMPARE(container->clipEdits().fadeInOffsetTicks, second / 4);
		QCOMPARE(container->clipEdits().fadeOutOffsetTicks, 0);
		QCOMPARE(clipFadeGainAt(10, 1000, 100, 0, FadeShape::Linear, FadeShape::Linear, 300, 0), 1.0f);   // before the ramp
		QCOMPARE(clipFadeGainAt(300, 1000, 100, 0, FadeShape::Linear, FadeShape::Linear, 300, 0), 0.0f);  // its first frame
		QCOMPARE(clipFadeGainAt(400, 1000, 100, 0, FadeShape::Linear, FadeShape::Linear, 300, 0), 1.0f);  // past it
	}

	//! The envelope function: with an offset the ramp ends before the clip and the tail is unfaded; with
	//! none it is exactly what it was (the tail frames stay silent past the end).
	void theFadeOutEndsBeforeTheClipEndByItsOffset()
	{
		const auto gain = [](f_cnt_t frame, f_cnt_t offset) {
			return clipFadeGainAt(frame, 1000, 0, 100, FadeShape::Linear, FadeShape::Linear, 0, offset);
		};
		QCOMPARE(gain(999, 0), 0.01f);                 // the old tail-anchored ramp: nearly silent at the last frame
		QCOMPARE(gain(1000, 0), 0.0f);                 // and silent past the end
		QCOMPARE(gain(700, 200), 1.0f);                // offset 200: the ramp is [700, 800) ...
		QCOMPARE(gain(799, 200), 0.01f);
		QCOMPARE(gain(800, 200), 1.0f);                // ... and the tail is unfaded
		QCOMPARE(gain(999, 200), 1.0f);
		QVERIFY(gain(750, 200) < 1.0f);
		QVERIFY(gain(750, 200) > gain(790, 200));      // monotonic down
	}

	//! The offsets are saved only when set and read back; a clip written without them (every project
	//! made before this) loads as offset 0.
	void theOffsetsRoundTripAndAreAbsentWhenZero()
	{
		const int second = oneSecondTicks();
		SampleClip* container = buildScene(second, second / 4, second / 4);
		QDomDocument doc;
		QDomElement plain = doc.createElement(QStringLiteral("clip"));
		container->saveSettings(doc, plain);
		QVERIFY(!plain.hasAttribute(QStringLiteral("fadeinoffset")));
		QVERIFY(!plain.hasAttribute(QStringLiteral("fadeoutoffset")));

		ClipEdits edits;
		edits.fadeInTicks = 10;
		edits.fadeInOffsetTicks = 20;
		edits.fadeOutTicks = 30;
		edits.fadeOutOffsetTicks = 40;
		container->setClipEdits(edits);
		QDomElement written = doc.createElement(QStringLiteral("clip"));
		container->saveSettings(doc, written);
		QCOMPARE(written.attribute(QStringLiteral("fadeinoffset")).toInt(), 20);
		QCOMPARE(written.attribute(QStringLiteral("fadeoutoffset")).toInt(), 40);

		container->setClipEdits(ClipEdits{});
		container->loadSettings(written);
		QCOMPARE(container->clipEdits(), edits);
		container->loadSettings(plain);  // the old file: no attribute at all
		QCOMPARE(container->clipEdits().fadeInOffsetTicks, 0);
		QCOMPARE(container->clipEdits().fadeOutOffsetTicks, 0);
	}

	//! An old project - no offsets anywhere - gets bit for bit the envelope it always got, for every clip
	//! length, fade length, shape pair and frame of a dense grid (including the frames past the end).
	void anOldProjectsEnvelopeIsBitForBitUnchanged()
	{
		const FadeShape shapes[] = {FadeShape::Linear, FadeShape::Exponential, FadeShape::EqualPower, FadeShape::Logarithmic, FadeShape::SCurve};
		const f_cnt_t clips[] = {0, 1, 10, 64, 300};
		const f_cnt_t fades[] = {0, 1, 5, 40, 299, 300, 400};
		int compared = 0;
		for (const f_cnt_t clip : clips)
		for (const f_cnt_t in : fades)
		for (const f_cnt_t out : fades)
		for (const FadeShape inShape : shapes)
		for (const FadeShape outShape : shapes)
		for (f_cnt_t frame = 0; frame <= clip + 3; ++frame)
		{
			const float old = envelopeBeforeOffsetFades(frame, clip, in, out, inShape, outShape);
			const float now = clipFadeGainAt(frame, clip, in, out, inShape, outShape);
			const float explicitZero = clipFadeGainAt(frame, clip, in, out, inShape, outShape, 0, 0);
			QVERIFY2(std::memcmp(&old, &now, sizeof(float)) == 0 && std::memcmp(&old, &explicitZero, sizeof(float)) == 0,
				qPrintable(QStringLiteral("envelope moved at frame %1 of %2 (in %3, out %4)").arg(frame).arg(clip).arg(in).arg(out)));
			++compared;
		}
		QVERIFY(compared > 100000);
	}

private:
	//! One sample track: a container clip of \p length from tick 0 and a contained one of \p contained
	//! ticks starting \p at ticks in, each a constant level so the sample value names the envelope.
	SampleClip* buildScene(int length, int at, int contained)
	{
		Engine::getSong()->clearProject();
		auto* track = new SampleTrack(Engine::getSong());
		auto* container = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		container->setSampleBuffer(constantBuffer(kContainerLevel, 2));
		container->changeLength(TimePos(length));
		container->setAutoResize(false);
		m_contained = dynamic_cast<SampleClip*>(track->createClip(TimePos(at)));
		m_contained->setSampleBuffer(constantBuffer(kContainedLevel, 2));
		m_contained->changeLength(TimePos(contained));
		m_contained->setAutoResize(false);
		Engine::getSong()->updateLength();
		return container;
	}

	QByteArray render()
	{
		const QString path = m_dir.filePath(QStringLiteral("offset-fade.wav"));
		QFile::remove(path);
		const OutputSettings settings(kSampleRate, 192, OutputSettings::BitDepth::Depth16Bit, OutputSettings::StereoMode::Stereo);
		ProjectRenderer renderer(settings, ProjectRenderer::ExportFileFormat::Wave, path);
		if (!renderer.isReady()) { return QByteArray(); }
		renderer.startProcessing();
		renderer.wait();
		QFile file(path);
		return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
	}

	SampleClip* m_contained = nullptr;
	QTemporaryDir m_dir;
};

QTEST_MAIN(ClipOffsetFadeTest)
#include "ClipOffsetFadeTest.moc"
