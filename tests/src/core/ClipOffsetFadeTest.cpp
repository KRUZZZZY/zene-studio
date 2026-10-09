/*
 * ClipOffsetFadeTest.cpp - contained crossfades clamp by default; the optional
 *                          container gesture uses paired offset fades.
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

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "AllocationProbe.h"
#include "ArrangementEdits.h"
#include "AudioEngine.h"
#include "ClipEdits.h"
#include "ClipPreFades.h"
#include "ControlClipLinkSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "OutputSettings.h"
#include "ProjectRenderer.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SampleClipOverlays.h"
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

//! preFadedGainAt exactly as it was before the offset fades existed.
float preFadedBeforeOffsetFades(const PreFadeSpan& span, std::int64_t clipPos, FadeShape fadeInShape, FadeShape fadeOutShape)
{
	double value = 1.0;
	const std::int64_t inLength = span.preInFrames + span.fadeInFrames;
	const std::int64_t intoIn = clipPos + span.preInFrames;
	if (inLength > 0 && intoIn < inLength)
	{
		value *= intoIn <= 0 ? 0.0 : fadeShapeGain(fadeInShape, static_cast<double>(intoIn) / static_cast<double>(inLength));
	}
	const std::int64_t outLength = span.fadeOutFrames + span.preOutFrames;
	const std::int64_t left = span.clipFrames + span.preOutFrames - clipPos;
	if (outLength > 0 && left < outLength)
	{
		value *= left <= 0 ? 0.0 : fadeShapeGain(fadeOutShape, static_cast<double>(left) / static_cast<double>(outLength));
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

	//! The owner ruling makes ordinary contained crossfades use clip-edge fades with zero offsets.
	void aDefaultContainedCrossfadeClampsToClipEdges()
	{
		const int second = oneSecondTicks();
		SampleClip* container = buildScene(second, second / 4, second / 4);
		QCOMPARE(arrangement::applyOverlap(m_contained, arrangement::Overlap::Crossfade), 1);

		const ClipEdits out = container->clipEdits();
		const ClipEdits in = m_contained->clipEdits();
		const int overlap = second / 4;
		QCOMPARE(out.fadeOutTicks, overlap);
		QCOMPARE(out.fadeOutOffsetTicks, 0);
		QCOMPARE(out.fadeInTicks, 0);
		QCOMPARE(out.fadeInOffsetTicks, 0);
		QCOMPARE(in.fadeInTicks, overlap);
		QCOMPARE(in.fadeInOffsetTicks, 0);
		QCOMPARE(in.fadeOutTicks, 0);

		const QByteArray wav = render();
		QVERIFY2(!wav.isEmpty(), "the export produced no file");
		const int start = framesAt(overlap);
		QVERIFY2(std::fabs(wavLeft(wav, start) - kContainerLevel) < 0.005f,
			"the contained edge starts with the container at unity");
		const double innerHalf = kContainerLevel + kContainedLevel * std::sin(kHalfPi / 2.0);
		QVERIFY2(std::fabs(wavLeft(wav, start + framesAt(overlap / 2)) - innerHalf) < 0.02f,
			"the container must not duck under the contained clip in default mode");
		QVERIFY2(std::fabs(wavLeft(wav, framesAt(second * 5 / 8)) - kContainerLevel) < 0.005f,
			"the container fade stays at its own end");
		QVERIFY2(std::fabs(wavLeft(wav, framesAt(second * 7 / 8)) - kContainerLevel * std::sin(kHalfPi / 2.0)) < 0.02f,
			"the container's ordinary fade is anchored to its edge");
	}

	//! Alt is the arranger's fast gesture for the old V-shaped container fade.
	void altContainerGestureUsesTheInnerClipsAuthoredFadeLengths()
	{
		SampleClip* container = buildScene(400, 100, 100);
		ClipEdits authored;
		authored.fadeInTicks = 30;
		authored.fadeOutTicks = 40;
		m_contained->setClipEdits(authored);
		QString why;
		QCOMPARE(arrangement::applyOverlap(m_contained, arrangement::Overlap::Trim, &why, true), 1);
		QVERIFY(why.isEmpty());
		const ClipEdits out = container->clipEdits();
		const ClipEdits in = m_contained->clipEdits();
		QCOMPARE(out.fadeOutTicks, 30);
		QCOMPARE(out.fadeInTicks, 40);
		QCOMPARE(in.fadeInTicks, 30);
		QCOMPARE(in.fadeOutTicks, 40);
		QCOMPARE(out.fadeOutOffsetTicks, 400 - (100 + 30));
		QCOMPARE(out.fadeInOffsetTicks, 100 + 100 - 40);
		QVERIFY(out.fadeOffsetGap);
		QCOMPARE(withValidOffsetFades(out, 400), out);
		QCOMPARE(clipFadeGainAt(130, 400, out.fadeInTicks, out.fadeOutTicks, out.fadeInShape,
			out.fadeOutShape, out.fadeInOffsetTicks, out.fadeOutOffsetTicks, out.fadeOffsetGap), 0.0f);
		QCOMPARE(clipFadeGainAt(145, 400, out.fadeInTicks, out.fadeOutTicks, out.fadeInShape,
			out.fadeOutShape, out.fadeInOffsetTicks, out.fadeOutOffsetTicks, out.fadeOffsetGap), 0.0f);
		QCOMPARE(in.fadeInOffsetTicks, 0);
		QCOMPARE(in.fadeOutOffsetTicks, 0);
	}

	//! A pre-fade must not bypass the silent span between a container's paired ramps.
	void aContainerCrossfadeGapSilencesAPreFadedContainer()
	{
		const int second = oneSecondTicks();
		SampleClip* container = buildScene(second, second / 4, second / 2);
		ClipEdits outer;
		outer.preFadeOutTicks = 24;
		container->setClipEdits(outer);
		ClipEdits inner;
		inner.fadeInTicks = 24;
		inner.fadeOutTicks = 24;
		m_contained->setClipEdits(inner);
		QCOMPARE(arrangement::applyOverlap(m_contained, arrangement::Overlap::Crossfade, nullptr, true), 1);
		QVERIFY(container->clipEdits().fadeOffsetGap);
		QVERIFY(prefades::engaged(*container));

		const QByteArray wav = render();
		QVERIFY2(!wav.isEmpty(), "the export produced no file");
		const int insideGap = framesAt(second / 2);
		QVERIFY2(std::fabs(wavLeft(wav, insideGap) - kContainedLevel) < 0.01f,
			"only the contained clip should sound inside the container's silent gap");
	}

	//! The render path allocates nothing with an offset fade live (the handle is built outside the probe).
	void theOffsetFadeRendersWithoutAllocating()
	{
		const int second = oneSecondTicks();
		SampleClip* container = buildScene(second, second / 4, second / 4);
		ClipEdits edits;
		edits.fadeInTicks = 48;
		edits.fadeInOffsetTicks = second - 192 + 96;
		edits.fadeOutTicks = 96;
		edits.fadeOutOffsetTicks = 192;
		edits.fadeOutShape = FadeShape::EqualPower;
		edits.fadeOffsetGap = true;
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

	//! clip.crossfade defaults to clamp; mode=container uses the inner clip's authored ramps and one undo restores them.
	void theCrossfadeCommandUsesTheRampsAndUndoRestoresThem()
	{
		const int second = oneSecondTicks();
		SampleClip* container = buildScene(second, second / 4, second / 4);
		const int span = second / 4;
		const QString containerId = control::clipIdOf(container);
		const QString containedId = control::clipIdOf(m_contained);
		ClipEdits authored;
		authored.fadeInTicks = 6;
		authored.fadeOutTicks = span - authored.fadeInTicks;
		const auto crossfade = [&](const QJsonObject& more, SampleClip* out, SampleClip* in) {
			QJsonObject args{{QStringLiteral("out"), control::clipIdOf(out)}, {QStringLiteral("in"), control::clipIdOf(in)}};
			for (auto it = more.begin(); it != more.end(); ++it) { args.insert(it.key(), it.value()); }
			return ControlRegistry::instance()->invoke(QStringLiteral("clip.crossfade"), args);
		};
		const auto undoAndResolve = [&]() {
			const ControlResult undone = ControlRegistry::instance()->invoke(QStringLiteral("control.undo"));
			QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
			ControlResult error;
			control::ClipRef ref;
			QVERIFY2(control::resolveClip(containerId, &ref, &error), qPrintable(error.errorMessage));
			container = dynamic_cast<SampleClip*>(ref.clip);
			QVERIFY(container != nullptr);
			QVERIFY2(control::resolveClip(containedId, &ref, &error), qPrintable(error.errorMessage));
			m_contained = dynamic_cast<SampleClip*>(ref.clip);
			QVERIFY(m_contained != nullptr);
		};
		const ControlResult done = crossfade({}, container, m_contained);
		QVERIFY2(done.ok, qPrintable(done.errorMessage));
		QCOMPARE(done.result.value(QStringLiteral("mode")).toString(), QStringLiteral("clamp"));
		QCOMPARE(done.result.value(QStringLiteral("overlap")).toInt(), span);
		QCOMPARE(container->clipEdits().fadeOutTicks, span);
		QCOMPARE(container->clipEdits().fadeOutOffsetTicks, 0);
		QCOMPARE(container->clipEdits().fadeInOffsetTicks, 0);
		QCOMPARE(m_contained->clipEdits().fadeInTicks, span);
		QCOMPARE(m_contained->clipEdits().fadeInOffsetTicks, 0);
		undoAndResolve();
		QCOMPARE(container->clipEdits(), ClipEdits{});
		QCOMPARE(m_contained->clipEdits(), ClipEdits{});

		m_contained->setClipEdits(authored);
		const ControlResult nested = crossfade({{QStringLiteral("mode"), QStringLiteral("container")}}, container, m_contained);
		QVERIFY2(nested.ok, qPrintable(nested.errorMessage));
		QCOMPARE(nested.result.value(QStringLiteral("mode")).toString(), QStringLiteral("container"));
		QCOMPARE(container->clipEdits().fadeOutTicks, authored.fadeInTicks);
		QCOMPARE(container->clipEdits().fadeInTicks, authored.fadeOutTicks);
		QCOMPARE(m_contained->clipEdits().fadeInTicks, authored.fadeInTicks);
		QCOMPARE(m_contained->clipEdits().fadeOutTicks, authored.fadeOutTicks);
		QCOMPARE(nested.result.value(QStringLiteral("out_fade_out_offset")).toInt(), container->clipEdits().fadeOutOffsetTicks);
		QVERIFY(container->clipEdits().fadeInOffsetTicks > 0);
		undoAndResolve();
		QCOMPARE(container->clipEdits(), ClipEdits{});
		QCOMPARE(m_contained->clipEdits(), authored);

		// A one-sided override changes only that authored ramp; the other keeps its inner-clip length.
		QVERIFY(crossfade({{QStringLiteral("mode"), QStringLiteral("container")}, {QStringLiteral("out_length"), 5}},
			container, m_contained).ok);
		QCOMPARE(container->clipEdits().fadeOutTicks, 5);
		QCOMPARE(container->clipEdits().fadeInTicks, span - 5);
		undoAndResolve();
		QVERIFY(crossfade({{QStringLiteral("mode"), QStringLiteral("container")}, {QStringLiteral("in_length"), 4}},
			m_contained, container).ok);
		QCOMPARE(container->clipEdits().fadeInTicks, 4);
		QCOMPARE(container->clipEdits().fadeOutTicks, span - 4);
		undoAndResolve();

		// What cannot fit, or says two different lengths for one requested ramp, is refused and writes nothing.
		const ClipEdits kept = container->clipEdits();
		QVERIFY(!crossfade({{QStringLiteral("mode"), QStringLiteral("container")}, {QStringLiteral("out_length"), span}},
			container, m_contained).ok);
		QVERIFY(!crossfade({{QStringLiteral("mode"), QStringLiteral("container")}, {QStringLiteral("out_length"), 3},
			{QStringLiteral("in_length"), 4}}, container, m_contained).ok);
		QCOMPARE(container->clipEdits(), kept);
	}

	//! Container mode requires both authored inner ramps; it has no half-length fallback.
	void aContainedClipsOwnFadeInAndFadeOutAreTheEntryAndExitRamps()
	{
		const int second = oneSecondTicks();
		SampleClip* container = buildScene(second, second / 4, second / 4);
		ClipEdits own;
		own.fadeInTicks = 5;
		own.fadeOutTicks = 6;
		m_contained->setClipEdits(own);
		QCOMPARE(arrangement::applyOverlap(m_contained, arrangement::Overlap::Crossfade, nullptr, true), 1);
		QCOMPARE(m_contained->clipEdits().fadeInTicks, 5);
		QCOMPARE(m_contained->clipEdits().fadeOutTicks, 6);
		QCOMPARE(container->clipEdits().fadeOutTicks, 5);
		QCOMPARE(container->clipEdits().fadeInTicks, 6);
		QVERIFY(container->clipEdits().fadeOffsetGap);
	}

	//! Across the whole container the envelope never steps by more than one frame of its ramps' steepest slope,
	//! and the container and the contained clip stay equal-power complementary through both crossfades.
	void theEnvelopeIsContinuousAndEqualPowerThroughBothCrossfades()
	{
		SampleClip* container = buildScene(400, 100, 100);  // ticks stand in for frames: the ramps are ratios
		ClipEdits authored;
		authored.fadeInTicks = 40;
		authored.fadeOutTicks = 60;
		m_contained->setClipEdits(authored);
		QCOMPARE(arrangement::applyOverlap(m_contained, arrangement::Overlap::Crossfade, nullptr, true), 1);
		const ClipEdits out = container->clipEdits();
		const ClipEdits in = m_contained->clipEdits();
		const double slope = kHalfPi / std::min(out.fadeOutTicks, out.fadeInTicks);
		float previous = 1.0f;
		for (f_cnt_t frame = 0; frame < 400; ++frame)
		{
			const float gain = clipFadeGainAt(frame, 400, out.fadeInTicks, out.fadeOutTicks, out.fadeInShape, out.fadeOutShape,
				out.fadeInOffsetTicks, out.fadeOutOffsetTicks);
			QVERIFY2(std::fabs(gain - previous) <= slope * 1.05, qPrintable(QStringLiteral("the container steps %1 at frame %2").arg(gain - previous).arg(frame)));
			previous = gain;
			if (frame >= 100 && frame < 200)
			{
				const float contained = clipFadeGainAt(frame - 100, 100, in.fadeInTicks, in.fadeOutTicks, in.fadeInShape, in.fadeOutShape);
				QVERIFY2(std::fabs(gain * gain + contained * contained - 1.0f) < 0.06f,
					qPrintable(QStringLiteral("not equal power at frame %1: %2 and %3").arg(frame).arg(gain).arg(contained)));
			}
		}
		QCOMPARE(previous, 1.0f);  // the container ends at unity: its tail is not faded
	}

	//! A bare offset fade-out (clip.set_fade fade_out_offset): the ramp sits before the clip's end and the clip is
	//! unfaded outside it. Bridging the ramp's end back to unity is the caller's (a crossfade adds the return ramp).
	void anOffsetFadeOutRampSitsBeforeTheClipEnd()
	{
		const auto gain = [](f_cnt_t frame, f_cnt_t offset) {
			return clipFadeGainAt(frame, 1000, 0, 100, FadeShape::Linear, FadeShape::Linear, 0, offset);
		};
		QCOMPARE(gain(999, 0), 0.01f);                 // no offset: the old tail-anchored ramp, silent past the end
		QCOMPARE(gain(1000, 0), 0.0f);
		QCOMPARE(gain(699, 200), 1.0f);                // offset 200: the ramp is [700, 800)
		QVERIFY(gain(750, 200) < 1.0f);
		QVERIFY(gain(750, 200) > gain(790, 200));      // monotonic down
		QCOMPARE(gain(900, 200), 1.0f);                // the stretch after it is unfaded
		QCOMPARE(gain(999, 200), 1.0f);
	}

	//! A pre-fade still applies to a clip with an offset fade: the clip stays engaged, the lead is a ramp of its
	//! own before the clip, and the in-clip fade-in sits at its offset.
	void aPreFadeStillAppliesToAClipWithAnOffsetFade()
	{
		const int second = oneSecondTicks();
		SampleClip* container = buildScene(second, second / 4, second / 4);
		ClipEdits edits;
		edits.preFadeInTicks = 40;
		edits.fadeInTicks = 10;
		edits.fadeInOffsetTicks = 30;
		container->setClipEdits(edits);
		QVERIFY2(prefades::engaged(*container), "an offset fade switched the pre-fades off");

		const PreFadeSpan span{1000, 40, 0, 10, 0, 30, 0};
		const auto gain = [&](std::int64_t at) { return preFadedGainAt(span, at, FadeShape::Linear, FadeShape::Linear); };
		QCOMPARE(gain(-40), 0.0f);                    // the lead starts silent ...
		QVERIFY(gain(-20) > 0.4f && gain(-20) < 0.6f);
		QVERIFY(gain(-1) > 0.97f);                    // ... reaches unity at the clip's first frame
		QCOMPARE(gain(10), 1.0f);                     // unfaded until the in-clip ramp
		QCOMPARE(gain(30), 0.0f);                     // which starts at its offset
		QCOMPARE(gain(40), 1.0f);
		const PreFadeSpan tail{1000, 0, 40, 0, 10, 0, 30};
		const auto out = [&](std::int64_t at) { return preFadedGainAt(tail, at, FadeShape::Linear, FadeShape::Linear); };
		QCOMPARE(out(900), 1.0f);                     // before the ramp
		QVERIFY(out(965) < 1.0f && out(965) > 0.0f);  // inside it (it ends 30 before the end)
		QCOMPARE(out(975), 1.0f);                     // after it: the unfaded stretch ...
		QCOMPARE(out(1000), 1.0f);                    // ... into the pre-fade-out, which starts at unity
		QVERIFY(out(1020) > 0.4f && out(1020) < 0.6f);
		QCOMPARE(out(1040), 0.0f);
	}

	//! The fade handles of a clip with an offset fade are where its ramps are, and a drag sets the ramp's length
	//! from the offset, not from the clip's edge.
	void theFadeHandlesOfAnOffsetFadeCanBeDragged()
	{
		ClipEdits edits;
		edits.fadeInTicks = 10;
		edits.fadeInOffsetTicks = 30;
		edits.fadeOutTicks = 20;
		edits.fadeOutOffsetTicks = 50;
		const gui::ClipOverlayFrame frame{1.f, 0, 192, 2, 60};
		// The in handle is at the END of the fade-in (tick 40), the out handle at the START of the fade-out (192-50-20).
		QCOMPARE(gui::fadeHandleAt(edits, 192, frame, QPoint(40, 4)), gui::FadeHandle::In);
		QCOMPARE(gui::fadeHandleAt(edits, 192, frame, QPoint(122, 4)), gui::FadeHandle::Out);
		QCOMPARE(gui::fadeTicksAt(gui::FadeHandle::In, 55, 192, frame, 30), 25);    // dragged to tick 55: a 25-tick ramp from 30
		QCOMPARE(gui::fadeTicksAt(gui::FadeHandle::Out, 100, 192, frame, 50), 42);  // dragged to 100: ends at 142, so 42 long
		QCOMPARE(gui::fadeTicksAt(gui::FadeHandle::In, 10, 192, frame, 30), 0);     // before the offset: no ramp
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

		ClipEdits gap;
		const int clipLength = container->length().getTicks();
		gap.fadeInTicks = 5;
		gap.fadeInOffsetTicks = clipLength - 10;
		gap.fadeOutTicks = 10;
		gap.fadeOutOffsetTicks = 20;
		gap.fadeOffsetGap = true;
		container->setClipEdits(gap);
		QDomElement gapped = doc.createElement(QStringLiteral("clip"));
		container->saveSettings(doc, gapped);
		QCOMPARE(gapped.attribute(QStringLiteral("fadeoffsetgap")), QStringLiteral("1"));
		container->setClipEdits(ClipEdits{});
		container->loadSettings(gapped);
		QCOMPARE(container->clipEdits().fadeInTicks, gap.fadeInTicks);
		QCOMPARE(container->clipEdits().fadeOutTicks, gap.fadeOutTicks);
		QCOMPARE(container->clipEdits().fadeInOffsetTicks, gap.fadeInOffsetTicks);
		QCOMPARE(container->clipEdits().fadeOutOffsetTicks, gap.fadeOutOffsetTicks);
		QCOMPARE(container->clipEdits().fadeOffsetGap, gap.fadeOffsetGap);
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

	//! The same for a pre-faded clip with no offsets: its envelope is bit for bit the old one.
	void anOldProjectsPreFadedEnvelopeIsBitForBitUnchanged()
	{
		const std::int64_t counts[] = {0, 1, 7, 40};
		for (const std::int64_t preIn : counts)
		for (const std::int64_t preOut : counts)
		for (const std::int64_t fadeIn : counts)
		for (const std::int64_t fadeOut : counts)
		for (std::int64_t at = -50; at <= 150; ++at)
		{
			const PreFadeSpan span{100, preIn, preOut, fadeIn, fadeOut};
			const float old = preFadedBeforeOffsetFades(span, at, FadeShape::EqualPower, FadeShape::SCurve);
			const float now = preFadedGainAt(span, at, FadeShape::EqualPower, FadeShape::SCurve);
			QVERIFY2(std::memcmp(&old, &now, sizeof(float)) == 0, qPrintable(QStringLiteral("pre-faded envelope moved at %1").arg(at)));
		}
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
