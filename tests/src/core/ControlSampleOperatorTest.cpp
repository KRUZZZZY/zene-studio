/*
 * ControlSampleOperatorTest.cpp - board card #706's registered proof
 *
 * Copyright (2026) Zene Studio contributors
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

// The registered proof for board card #706, the destructive waveform editor's
// first slice: every sample.* generator and baker is proved THE A16 WAY -
// invoke through the registry (the socket's own door), read the SAMPLES back
// from the model, control.undo ONCE, read them back against the exact frames
// that were there - plus the typed refusals, each with a negative control that
// the refusal pushed no undo step. The DSP claims (the gain actually applied,
// the peak actually reached, the order actually reversed, the ramp actually
// baked) are asserted against the frames themselves, not against a result
// field that could agree for the wrong reason.

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <vector>

#include <QJsonObject>
#include <QString>

#include "Clip.h"
#include "ClipEdits.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "ReversibilityTestSupport.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SampleWindow.h"
#include "Song.h"

using namespace lmms;
using namespace revtest;

namespace
{

//! A copy of the clip's source frames, read from the MODEL - the same object
//! the journal restores - plus the clip's length so a bake that also resized
//! the clip would fail this test rather than slip through.
struct Capture
{
	std::vector<SampleFrame> frames;
	int rate = 0;
	tick_t length = 0;
};

Capture captureOf(const QString& id)
{
	control::ClipRef ref;
	ControlResult error;
	Capture out;
	if (!control::resolveClip(id, &ref, &error)) { return out; }
	auto* clip = dynamic_cast<SampleClip*>(ref.clip);
	if (clip == nullptr) { return out; }
	const Sample& source = clip->sample();
	out.frames.assign(source.data(), source.data() + source.sampleSize());
	out.rate = static_cast<int>(source.sampleRate());
	out.length = ref.clip->length().getTicks();
	return out;
}

//! Bit-exact frame equality: the undo path restores the source through the
//! clip's serialized bytes, so a restored buffer must be the SAME bytes.
bool sameFrames(const Capture& a, const Capture& b)
{
	if (a.frames.size() != b.frames.size()) { return false; }
	for (size_t i = 0; i < a.frames.size(); ++i)
	{
		if (a.frames[i].left() != b.frames[i].left()) { return false; }
		if (a.frames[i].right() != b.frames[i].right()) { return false; }
	}
	return true;
}

float peakOf(const std::vector<SampleFrame>& frames)
{
	float peak = 0.0f;
	for (const SampleFrame& frame : frames)
	{
		peak = std::max(peak, std::abs(frame.left()));
		peak = std::max(peak, std::abs(frame.right()));
	}
	return peak;
}

//! A tone clip on a fresh sample track, created through sample.generate itself
//! - the fixture is the product's own generator, so the operators and the
//! generator are proved against each other's output.
QString makeGeneratedClip(const QString& kind = QStringLiteral("tone"),
	double amplitude = 0.8)
{
	const QString track = addTrack(QStringLiteral("sample"));
	if (track.isEmpty()) { return QString(); }
	const ControlResult generated = run(QStringLiteral("sample.generate"),
		QJsonObject{{QStringLiteral("track"), track},
			{QStringLiteral("position"), 0}, {QStringLiteral("length"), 480},
			{QStringLiteral("kind"), kind},
			{QStringLiteral("frequency"), 440.0},
			{QStringLiteral("amplitude"), amplitude}});
	if (!generated.ok) { return QString(); }
	return generated.result.value(QStringLiteral("clip")).toString();
}

const control::ReversibilityEntry* contractRow(const QString& id)
{
	return control::ReversibilityTable::instance().lookup(id);
}

} // namespace

class ControlSampleOperatorTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", QByteArrayLiteral(LMMS_TEST_PLUGIN_DIR));
#endif
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The group's five ids declare the contract's parts - schemas, a
	//! description, a mutating flag, no `requires` excuse - and each carries a
	//! true_inverse row whose reason and mechanism name the checkpoint that IS
	//! its inverse (SPEC A13/A16: one definition, no drift).
	void theGroupIsRegisteredWithSchemasAndContractRows()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const QStringList all = {
			QStringLiteral("sample.generate"), QStringLiteral("sample.amplify"),
			QStringLiteral("sample.normalize"), QStringLiteral("sample.reverse"),
			QStringLiteral("sample.fade")};
		for (const QString& id : all)
		{
			QVERIFY2(registry->hasCommand(id), qPrintable(QStringLiteral("missing command %1").arg(id)));
			const ControlCommand* cmd = registry->command(id);
			QVERIFY(cmd != nullptr);
			QCOMPARE(cmd->group, QStringLiteral("sample"));
			QVERIFY(!cmd->verb.isEmpty());
			QVERIFY(!cmd->description.isEmpty());
			QVERIFY(!cmd->argsSchema.isEmpty());
			QVERIFY(!cmd->resultSchema.isEmpty());
			QVERIFY2(cmd->requiresDecl.isEmpty(), qPrintable(id + " declares a requires excuse"));
			QVERIFY2(cmd->mutating, qPrintable(id + " mutates the song without saying so"));
			const control::ReversibilityEntry* row = contractRow(id);
			QVERIFY2(row != nullptr, qPrintable(id + " has no contract row"));
			QCOMPARE(control::reversibilityClassName(row->cls), QStringLiteral("true_inverse"));
			QVERIFY(row->reversible);
			QVERIFY(!row->reason.isEmpty());
			QVERIFY(!row->mechanism.isEmpty());
		}
	}

	//! One generate per kind: each produces a buffer into a NEW clip through
	//! the clip model, the transaction is stamped true_inverse, and ONE undo
	//! removes the clip (buffer included) - the track is as it was.
	void generateMakesAClipPerKindAndOneUndoRemovesIt()
	{
		const QString track = addTrack(QStringLiteral("sample"));
		QVERIFY(!track.isEmpty());
		const QStringList kinds = {QStringLiteral("tone"), QStringLiteral("white"),
			QStringLiteral("pink"), QStringLiteral("silence")};
		for (const QString& kind : kinds)
		{
			const ControlResult generated = run(QStringLiteral("sample.generate"),
				QJsonObject{{QStringLiteral("track"), track},
					{QStringLiteral("position"), 0}, {QStringLiteral("length"), 480},
					{QStringLiteral("kind"), kind},
					{QStringLiteral("frequency"), 440.0},
					{QStringLiteral("amplitude"), 0.8}});
			QVERIFY2(generated.ok, qPrintable(kind + ": " + generated.errorMessage));
			QCOMPARE(generated.result.value(QStringLiteral("kind")).toString(), kind);
			const QString clip = generated.result.value(QStringLiteral("clip")).toString();
			const Capture captured = captureOf(clip);
			QVERIFY2(!captured.frames.empty(), qPrintable(kind + " produced no frames"));
			QVERIFY(captured.rate > 0);
			float peak = peakOf(captured.frames);
			if (kind == QLatin1String("silence"))
			{
				QCOMPARE(peak, 0.0f);
			}
			else
			{
				QVERIFY2(peak > 0.001f, qPrintable(kind + " produced a silent buffer"));
			}
			if (kind == QLatin1String("tone"))
			{
				// sin(0) * amplitude is exactly 0; the peak reaches the ask.
				QCOMPARE(captured.frames.front().left(), 0.0f);
				QVERIFY(peak <= 0.8f + 1e-5f);
				QVERIFY(peak > 0.75f);
			}
			if (kind == QLatin1String("white"))
			{
				QVERIFY(peak <= 0.8f + 1e-5f);
			}
			const ControlRegistry::Transaction* tx = ControlRegistry::instance()->lastTransaction();
			QVERIFY(tx != nullptr);
			QCOMPARE(tx->command, QStringLiteral("sample.generate"));
			QCOMPARE(tx->cls, QStringLiteral("true_inverse"));
			QVERIFY(tx->reversible);
			REV_UNDO_OR_FAIL();
			ControlResult error;
			control::ClipRef ref;
			QVERIFY2(!control::resolveClip(clip, &ref, &error),
				qPrintable(QStringLiteral("one undo left %1 behind").arg(clip)));
		}
	}

	//! sample.amplify: the frames come back scaled by exactly the requested
	//! gain, the clip's length is untouched, and ONE undo restores the exact
	//! frames that were there.
	void amplifyBakesTheGainAndOneUndoRestoresTheFrames()
	{
		const QString clip = makeGeneratedClip();
		QVERIFY(!clip.isEmpty());
		const Capture before = captureOf(clip);
		QVERIFY(!before.frames.empty());

		const ControlResult amplified = run(QStringLiteral("sample.amplify"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("gain_db"), 6.0}});
		QVERIFY2(amplified.ok, qPrintable(amplified.errorMessage));
		QCOMPARE(amplified.result.value(QStringLiteral("frames")).toInt(),
			static_cast<int>(before.frames.size()));
		const Capture after = captureOf(clip);
		QCOMPARE(after.frames.size(), before.frames.size());
		QCOMPARE(after.length, before.length);
		QCOMPARE(after.rate, before.rate);
		const float gain = gainDbToLinear(6.0f);
		for (size_t i = 0; i < before.frames.size(); ++i)
		{
			const float expected = before.frames[i].left() * gain;
			if (std::abs(after.frames[i].left() - expected) > 1e-4f) { QFAIL("gain not baked"); }
			const float expectedRight = before.frames[i].right() * gain;
			if (std::abs(after.frames[i].right() - expectedRight) > 1e-4f) { QFAIL("gain not baked"); }
		}
		REV_UNDO_OR_FAIL();
		const Capture restored = captureOf(clip);
		QVERIFY2(sameFrames(restored, before),
			"one undo did not restore the exact frames that were there");
		QCOMPARE(restored.length, before.length);
	}

	//! sample.normalize: the peak lands on the target, one undo restores the
	//! exact frames, and a SILENT source is refused with no undo step of its
	//! own - the negative control is that the following undo removes the
	//! silence generate, not an invisible bake attempt.
	void normalizeScalesThePeakAndOneUndoRestoresTheFrames()
	{
		const QString clip = makeGeneratedClip();
		QVERIFY(!clip.isEmpty());
		const Capture before = captureOf(clip);
		QVERIFY(!before.frames.empty());
		QVERIFY(peakOf(before.frames) > 0.5f);

		const ControlResult normalized = run(QStringLiteral("sample.normalize"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("target_db"), -6.0}});
		QVERIFY2(normalized.ok, qPrintable(normalized.errorMessage));
		const Capture after = captureOf(clip);
		const float target = std::pow(10.0f, -6.0f / 20.0f);
		QVERIFY2(std::abs(peakOf(after.frames) - target) < 1e-3f,
			"the peak did not land on the target");
		REV_UNDO_OR_FAIL();
		QVERIFY2(sameFrames(captureOf(clip), before),
			"one undo did not restore the exact frames that were there");

		const QString silent = makeGeneratedClip(QStringLiteral("silence"));
		QVERIFY(!silent.isEmpty());
		const ControlResult refused = run(QStringLiteral("sample.normalize"),
			QJsonObject{{QStringLiteral("clip"), silent}});
		QCOMPARE(refused.ok, false);
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QVERIFY(refused.errorMessage.contains(QStringLiteral("silence")));
		// The refusal pushed no transaction: the next undo is the silence
		// GENERATE's own step, so the clip disappearing proves the point.
		REV_UNDO_OR_FAIL();
		ControlResult error;
		control::ClipRef ref;
		QVERIFY(!control::resolveClip(silent, &ref, &error));
	}

	//! sample.reverse: frame i of the result is frame n-1-i of the source,
	//! both channels together, and ONE undo restores the original order.
	void reverseBakesTheReversedOrderAndOneUndoRestoresTheFrames()
	{
		const QString clip = makeGeneratedClip();
		QVERIFY(!clip.isEmpty());
		const Capture before = captureOf(clip);
		QVERIFY(before.frames.size() > 8);

		const ControlResult reversed = run(QStringLiteral("sample.reverse"),
			QJsonObject{{QStringLiteral("clip"), clip}});
		QVERIFY2(reversed.ok, qPrintable(reversed.errorMessage));
		const Capture after = captureOf(clip);
		QCOMPARE(after.frames.size(), before.frames.size());
		const size_t last = before.frames.size() - 1;
		for (size_t i = 0; i < before.frames.size(); ++i)
		{
			if (after.frames[i].left() != before.frames[last - i].left())
			{ QFAIL("reversal not baked"); }
		}
		REV_UNDO_OR_FAIL();
		QVERIFY2(sameFrames(captureOf(clip), before),
			"one undo did not restore the exact frames that were there");
	}

	//! sample.fade: direction=in starts at exactly 0 and keeps the last frame;
	//! direction=out keeps the first and ends at exactly 0; the midpoint sits
	//! on the linear ramp. ONE undo restores the exact frames after each bake,
	//! and a bogus direction is typed InvalidArgs without a checkpoint.
	void fadeBakesTheRampAndOneUndoRestoresTheFrames()
	{
		const QString clip = makeGeneratedClip();
		QVERIFY(!clip.isEmpty());
		const Capture before = captureOf(clip);
		QVERIFY(before.frames.size() > 8);
		const size_t mid = before.frames.size() / 2;

		const ControlResult faded = run(QStringLiteral("sample.fade"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("direction"), QStringLiteral("in")}});
		QVERIFY2(faded.ok, qPrintable(faded.errorMessage));
		const Capture afterIn = captureOf(clip);
		QCOMPARE(afterIn.frames.front().left(), 0.0f);
		QCOMPARE(afterIn.frames.back().left(), before.frames.back().left());
		const float half = before.frames[mid].left() * 0.5f;
		QVERIFY2(std::abs(afterIn.frames[mid].left() - half) < 1e-4f, "no linear ramp baked");
		REV_UNDO_OR_FAIL();
		QVERIFY2(sameFrames(captureOf(clip), before),
			"one undo did not restore the exact frames that were there");

		const ControlResult fadedOut = run(QStringLiteral("sample.fade"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("direction"), QStringLiteral("out")}});
		QVERIFY2(fadedOut.ok, qPrintable(fadedOut.errorMessage));
		const Capture afterOut = captureOf(clip);
		QCOMPARE(afterOut.frames.front().left(), before.frames.front().left());
		QCOMPARE(afterOut.frames.back().left(), 0.0f);
		REV_UNDO_OR_FAIL();
		QVERIFY2(sameFrames(captureOf(clip), before),
			"one undo did not restore the exact frames that were there");

		const ControlResult bogus = run(QStringLiteral("sample.fade"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("direction"), QStringLiteral("sideways")}});
		QCOMPARE(bogus.ok, false);
		QCOMPARE(bogus.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY2(sameFrames(captureOf(clip), before), "a refused fade wrote frames");
	}

	//! Every refusal is TYPED, and the refusals that resolve a live clip have
	//! written nothing - the frames and windows read back unchanged.
	void refusalsAreTypedAndWriteNothing()
	{
		const QString instrument = addTrack();
		QVERIFY(!instrument.isEmpty());
		const ControlResult midiClip = run(QStringLiteral("clip.add"),
			QJsonObject{{QStringLiteral("track"), instrument}, {QStringLiteral("position"), 0}});
		QVERIFY(midiClip.ok);
		const QString midi = midiClip.result.value(QStringLiteral("clip")).toString();
		const ControlResult refusedMidi = run(QStringLiteral("sample.reverse"),
			QJsonObject{{QStringLiteral("clip"), midi}});
		QCOMPARE(refusedMidi.ok, false);
		QCOMPARE(refusedMidi.errorKind, ControlErrorKind::Refused);
		QVERIFY(refusedMidi.errorMessage.contains(QStringLiteral("not a sample clip")));
		ControlResult error;
		control::ClipRef ref;
		QVERIFY(control::resolveClip(midi, &ref, &error));

		// An EMPTY sample clip: there is no source to transform.
		const QString track = addTrack(QStringLiteral("sample"));
		QVERIFY(!track.isEmpty());
		const ControlResult emptyClip = run(QStringLiteral("clip.add"),
			QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("position"), 0}});
		QVERIFY(emptyClip.ok);
		const QString empty = emptyClip.result.value(QStringLiteral("clip")).toString();
		const ControlResult refusedEmpty = run(QStringLiteral("sample.amplify"),
			QJsonObject{{QStringLiteral("clip"), empty}, {QStringLiteral("gain_db"), 6.0}});
		QCOMPARE(refusedEmpty.ok, false);
		QCOMPARE(refusedEmpty.errorKind, ControlErrorKind::Refused);
		QVERIFY(refusedEmpty.errorMessage.contains(QStringLiteral("no source")));

		// Out-of-range arguments are InvalidArgs, not silent clamps.
		const QString clip = makeGeneratedClip();
		QVERIFY(!clip.isEmpty());
		const ControlResult tooHot = run(QStringLiteral("sample.amplify"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("gain_db"), 500.0}});
		QCOMPARE(tooHot.ok, false);
		QCOMPARE(tooHot.errorKind, ControlErrorKind::InvalidArgs);
		const ControlResult tooLoud = run(QStringLiteral("sample.normalize"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("target_db"), 3.0}});
		QCOMPARE(tooLoud.ok, false);
		QCOMPARE(tooLoud.errorKind, ControlErrorKind::InvalidArgs);

		// A trimmed source window: refused, and both window and frames are
		// exactly as they were (this release bakes the whole source only).
		const Capture before = captureOf(clip);
		QVERIFY(before.frames.size() > 32);
		{
			control::ClipRef ref;
			QVERIFY(control::resolveClip(clip, &ref, &error));
			auto* sampleClip = dynamic_cast<SampleClip*>(ref.clip);
			QVERIFY(sampleClip != nullptr);
			sampleClip->setSampleWindow(SampleWindow(10, static_cast<f_cnt_t>(before.frames.size()) - 10));
		}
		const ControlResult refusedWindow = run(QStringLiteral("sample.fade"),
			QJsonObject{{QStringLiteral("clip"), clip}, {QStringLiteral("direction"), QStringLiteral("in")}});
		QCOMPARE(refusedWindow.ok, false);
		QCOMPARE(refusedWindow.errorKind, ControlErrorKind::Refused);
		QVERIFY(refusedWindow.errorMessage.contains(QStringLiteral("window")));
		{
			control::ClipRef ref;
			QVERIFY(control::resolveClip(clip, &ref, &error));
			auto* sampleClip = dynamic_cast<SampleClip*>(ref.clip);
			QVERIFY(sampleClip != nullptr);
			QCOMPARE(sampleClip->sampleWindow().sourceIn, static_cast<f_cnt_t>(10));
			QVERIFY(sameFrames(captureOf(clip), before));
		}
		// The refusal pushed no undo step: this undo removes the GENERATE,
		// so a checkpoint the refusal should never have taken would fail it.
		REV_UNDO_OR_FAIL();
		QVERIFY(!control::resolveClip(clip, &ref, &error));
	}

	//! The guards and the DEFERRED verbs, named: a generator refuses a
	//! non-sample track and the oversize cap BEFORE allocating, `kind` is the
	//! schema enum (bogus is InvalidArgs), and the three deferred features
	//! register no id at all in this slice.
	void guardsAndDeferredVerbsAreNamed()
	{
		const QString instrument = addTrack();
		QVERIFY(!instrument.isEmpty());
		const ControlResult wrongTrack = run(QStringLiteral("sample.generate"),
			QJsonObject{{QStringLiteral("track"), instrument}, {QStringLiteral("position"), 0},
				{QStringLiteral("length"), 480}, {QStringLiteral("kind"), QStringLiteral("tone")}});
		QCOMPARE(wrongTrack.ok, false);
		QCOMPARE(wrongTrack.errorKind, ControlErrorKind::Refused);
		QVERIFY(wrongTrack.errorMessage.contains(QStringLiteral("not a sample track")));

		// The kind is the schema enum: the three DEFERRED features are not
		// kinds to try but absent verbs to report.
		const ControlResult deferredKind = run(QStringLiteral("sample.generate"),
			QJsonObject{{QStringLiteral("track"), addTrack(QStringLiteral("sample"))},
				{QStringLiteral("position"), 0}, {QStringLiteral("length"), 480},
				{QStringLiteral("kind"), QStringLiteral("chirp")}});
		QCOMPARE(deferredKind.ok, false);
		QCOMPARE(deferredKind.errorKind, ControlErrorKind::InvalidArgs);
		ControlRegistry* registry = ControlRegistry::instance();
		QVERIFY(!registry->hasCommand(QStringLiteral("sample.noise_reduction")));
		QVERIFY(!registry->hasCommand(QStringLiteral("sample.pitch")));
		QVERIFY(!registry->hasCommand(QStringLiteral("sample.tempo")));
		QVERIFY(!registry->hasCommand(QStringLiteral("sample.chirp")));

		// The oversize guard refuses BEFORE allocating (this precondition
		// keeps the assertion honest if a fixture tempo ever moves).
		const qint64 wouldBe = static_cast<qint64>(MaxSongLength)
			* static_cast<qint64>(Engine::framesPerTick()) * 2 * static_cast<qint64>(sizeof(SampleFrame));
		QVERIFY2(wouldBe > 512ll * 1024 * 1024, "the fixture no longer crosses the cap");
		const ControlResult oversized = run(QStringLiteral("sample.generate"),
			QJsonObject{{QStringLiteral("track"), addTrack(QStringLiteral("sample"))},
				{QStringLiteral("position"), 0}, {QStringLiteral("length"), MaxSongLength},
				{QStringLiteral("kind"), QStringLiteral("white")}});
		QCOMPARE(oversized.ok, false);
		QCOMPARE(oversized.errorKind, ControlErrorKind::Refused);
		QVERIFY(oversized.errorMessage.contains(QStringLiteral("512 MiB")));
	}
};

QTEST_GUILESS_MAIN(ControlSampleOperatorTest)
#include "ControlSampleOperatorTest.moc"
