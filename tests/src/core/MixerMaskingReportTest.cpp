/*
 * MixerMaskingReportTest.cpp - mixer.masking_report over fixed synthetic channel audio
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
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */
/*! Q050-231.1 (R-mixing-7): three channels each fed a sample track of fixed tones - ch 1 at 200 Hz and 3 kHz,
 *  ch 2 at 200 Hz and 800 Hz, ch 3 at 3 kHz and 10 kHz. The report is the same on every call; each clash names
 *  its pair, its range and the lower-priority channel to cut; equal priority cuts the quieter channel; the
 *  channel's strip EQ shapes what it holds; and reporting changes no EQ and no undo depth. */

#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>
#include <memory>
#include <vector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {}) { return ControlRegistry::instance()->invoke(id, args); }

QString ch(int index) { return control::channelIdOf(Engine::mixer()->mixerChannel(index)); }

//! A sample track on mixer channel @a channel playing @a seconds of the sum of @a tones (each @a level amplitude).
SampleTrack* toneTrack(int channel, const std::vector<double>& tones, float level = 0.25f, double seconds = 1.0)
{
	auto* track = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, Engine::getSong()));
	track->mixerChannelModel()->setValue(channel);
	const int rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	std::vector<SampleFrame> frames(static_cast<std::size_t>(static_cast<double>(rate) * seconds));
	for (std::size_t i = 0; i < frames.size(); ++i)
	{
		float v = 0.f;
		for (const double hz : tones) { v += level * static_cast<float>(std::sin(2.0 * M_PI * hz * static_cast<double>(i) / rate)); }
		frames[i] = SampleFrame(v, v);
	}
	auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(frames.data(), frames.size(), rate));
	return track;
}

QJsonObject report(const QJsonObject& priorities)
{
	const ControlResult result = run(QStringLiteral("mixer.masking_report"),
		{{QStringLiteral("channels"), QJsonArray{ch(3), ch(1), ch(2)}}, {QStringLiteral("priorities"), priorities}});
	if (!result.ok) { qWarning() << result.errorMessage; }
	return result.result;
}

//! The clash between @a a and @a b whose range holds @a hz; empty when there is none.
QJsonObject clashAt(const QJsonObject& result, const QString& a, const QString& b, double hz)
{
	for (const QJsonValue& value : result.value(QStringLiteral("clashes")).toArray())
	{
		const QJsonObject c = value.toObject();
		if (c.value(QStringLiteral("a")).toString() == a && c.value(QStringLiteral("b")).toString() == b
			&& c.value(QStringLiteral("low_hz")).toDouble() <= hz && c.value(QStringLiteral("high_hz")).toDouble() > hz) { return c; }
	}
	return {};
}

std::vector<float> allEq()
{
	std::vector<float> out;
	for (int i = 0; i < Engine::mixer()->numChannels(); ++i)
	{
		const MixerChannel* c = Engine::mixer()->mixerChannel(i);
		out.insert(out.end(), {c->m_eqLowModel.value(), c->m_eqMidModel.value(), c->m_eqMidFreqModel.value(), c->m_eqHighModel.value()});
	}
	return out;
}

int undoDepth() { return run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(); }

} // namespace

class MixerMaskingReportTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		while (Engine::mixer()->numChannels() <= 4) { Engine::mixer()->createChannel(); }
		ControlRegistry::setReady(true);
		toneTrack(1, {200.0, 3000.0});
		toneTrack(2, {200.0, 800.0});
		toneTrack(3, {3000.0, 10000.0});
		auto* instrument = Track::create(Track::Type::Instrument, Engine::getSong());
		instrument->mixerChannelModel()->setValue(3);
		m_instrument = control::trackIdOf(instrument);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Acceptance 1: the same audio and priorities give the same clashing bands, call after call.
	void repeatedReportsAreIdentical()
	{
		const QJsonObject priorities{{ch(1), QStringLiteral("front")}, {ch(2), QStringLiteral("back")}};
		const QJsonObject first = report(priorities);
		QVERIFY(!first.value(QStringLiteral("clashes")).toArray().isEmpty());
		for (int i = 0; i < 3; ++i)
		{
			QCOMPARE(QJsonDocument(report(priorities)).toJson(QJsonDocument::Compact), QJsonDocument(first).toJson(QJsonDocument::Compact));
		}
	}

	//! Acceptance 2: each clash names its pair, its range and the channel the priorities say to cut.
	void clashesNameThePairTheRangeAndTheLowerPriorityTarget()
	{
		const QJsonObject result = report({{ch(1), QStringLiteral("front")}, {ch(2), QStringLiteral("back")}});  // ch 3: middle
		const QJsonObject low = clashAt(result, ch(1), ch(2), 200.0);
		QVERIFY2(!low.isEmpty(), QJsonDocument(result).toJson().constData());
		QCOMPARE(low.value(QStringLiteral("target")).toString(), ch(2));  // back yields to front
		QCOMPARE(low.value(QStringLiteral("protect")).toString(), ch(1));
		QVERIFY(low.value(QStringLiteral("cut_db")).toDouble() <= -1.0 && low.value(QStringLiteral("cut_db")).toDouble() >= -6.0);
		const QJsonObject eq = low.value(QStringLiteral("strip_eq")).toObject();
		QCOMPARE(eq.value(QStringLiteral("channel")).toString(), ch(2));
		QVERIFY(eq.contains(QStringLiteral("mid")));
		QVERIFY(eq.value(QStringLiteral("mid_freq")).toDouble() > low.value(QStringLiteral("low_hz")).toDouble());
		QVERIFY(eq.value(QStringLiteral("mid_freq")).toDouble() < low.value(QStringLiteral("high_hz")).toDouble());
		const QJsonObject high = clashAt(result, ch(1), ch(3), 3000.0);
		QVERIFY(!high.isEmpty());
		QCOMPARE(high.value(QStringLiteral("target")).toString(), ch(3));  // middle yields to front
		// Nothing they do not share: 800 Hz is ch 2's alone, 10 kHz ch 3's, and ch 2 and ch 3 share no tone.
		QVERIFY(clashAt(result, ch(1), ch(2), 800.0).isEmpty());
		QVERIFY(clashAt(result, ch(1), ch(3), 10000.0).isEmpty());
		for (const QJsonValue& c : result.value(QStringLiteral("clashes")).toArray())
		{
			QVERIFY(!(c.toObject().value(QStringLiteral("a")).toString() == ch(2) && c.toObject().value(QStringLiteral("b")).toString() == ch(3)));
		}
		const QJsonArray channels = result.value(QStringLiteral("channels")).toArray();
		QCOMPARE(channels.size(), 3);
		QCOMPARE(channels.at(0).toObject().value(QStringLiteral("channel")).toString(), ch(1));  // mixer order
		QCOMPARE(channels.at(2).toObject().value(QStringLiteral("unanalysed_tracks")).toArray(), QJsonArray{m_instrument});
		// Swap the priorities and the 200 Hz cut moves to the other channel.
		const QJsonObject swapped = report({{ch(1), QStringLiteral("back")}, {ch(2), QStringLiteral("front")}});
		QCOMPARE(clashAt(swapped, ch(1), ch(2), 200.0).value(QStringLiteral("target")).toString(), ch(1));
	}

	//! At equal priority the channel quieter in the range is cut, whichever order the channels are named in.
	void equalPriorityCutsTheQuieterChannel()
	{
		SampleTrack* loud = toneTrack(4, {3000.0}, 0.5f);
		const ControlResult result = run(QStringLiteral("mixer.masking_report"), {{QStringLiteral("channels"), QJsonArray{ch(4), ch(3)}}});
		QVERIFY(result.ok);
		const QJsonObject c = clashAt(result.result, ch(3), ch(4), 3000.0);
		QVERIFY(!c.isEmpty());
		QCOMPARE(c.value(QStringLiteral("target")).toString(), ch(3));
		delete loud;
	}

	//! The strip EQ shapes what a channel holds: an 18 dB cut at 200 Hz on ch 2 ends that clash.
	void theStripEqShapesTheChannel()
	{
		QVERIFY(run(QStringLiteral("mixer.channel_eq"), {{QStringLiteral("channel"), ch(2)}, {QStringLiteral("mid"), -18.0}, {QStringLiteral("mid_freq"), 200.0}}).ok);
		QVERIFY(clashAt(report({}), ch(1), ch(2), 200.0).isEmpty());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!clashAt(report({}), ch(1), ch(2), 200.0).isEmpty());
	}

	//! Acceptance 2: reporting writes no strip EQ and records no undo step.
	void reportingChangesNoEqAndNoUndoDepth()
	{
		const std::vector<float> eq = allEq();
		const int depth = undoDepth();
		const bool modified = Engine::getSong()->isModified();
		QVERIFY(!report({{ch(1), QStringLiteral("front")}}).value(QStringLiteral("clashes")).toArray().isEmpty());
		QCOMPARE(allEq(), eq);
		QCOMPARE(undoDepth(), depth);
		QCOMPARE(Engine::getSong()->isModified(), modified);
	}

	//! Acceptance: one proposal changes only the target's EQ, in one undo step; one undo puts the old EQ back.
	void applyingAProposalChangesOnlyTheTargetAndOneUndoRestoresIt()
	{
		const QJsonObject clash = clashAt(report({{ch(1), QStringLiteral("front")}, {ch(2), QStringLiteral("back")}}), ch(1), ch(2), 200.0);
		QVERIFY(!clash.isEmpty());
		const QJsonObject proposal = clash.value(QStringLiteral("strip_eq")).toObject();
		QCOMPARE(proposal.value(QStringLiteral("channel")).toString(), ch(2));
		const std::vector<float> before = allEq();
		const int depth = undoDepth();
		const ControlResult result = run(QStringLiteral("mixer.apply_masking_proposal"), {{QStringLiteral("strip_eq"), proposal}});
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		QCOMPARE(result.result.value(QStringLiteral("applied")).toBool(), true);
		std::vector<float> expected = before;
		expected[2 * 4 + 1] = static_cast<float>(proposal.value(QStringLiteral("mid")).toDouble());
		expected[2 * 4 + 2] = static_cast<float>(proposal.value(QStringLiteral("mid_freq")).toDouble());
		QCOMPARE(allEq(), expected);
		QCOMPARE(undoDepth(), depth + 1);
		QVERIFY(Engine::getSong()->isModified());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(allEq(), before);
		QCOMPARE(undoDepth(), depth);
	}

	//! Applying the same proposal twice records one step: the second write changes nothing.
	void applyingTheSameProposalAgainChangesNothing()
	{
		const QJsonObject proposal = clashAt(report({{ch(1), QStringLiteral("front")}, {ch(2), QStringLiteral("back")}}),
			ch(1), ch(2), 200.0).value(QStringLiteral("strip_eq")).toObject();
		QVERIFY(!proposal.isEmpty());
		const std::vector<float> before = allEq();
		const int depth = undoDepth();
		const ControlResult first = run(QStringLiteral("mixer.apply_masking_proposal"), {{QStringLiteral("strip_eq"), proposal}});
		QVERIFY(first.ok);
		QCOMPARE(first.result.value(QStringLiteral("applied")).toBool(), true);
		const ControlResult second = run(QStringLiteral("mixer.apply_masking_proposal"), {{QStringLiteral("strip_eq"), proposal}});
		QVERIFY(second.ok);
		QCOMPARE(second.result.value(QStringLiteral("applied")).toBool(), false);
		QCOMPARE(undoDepth(), depth + 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(allEq(), before);
		QCOMPARE(undoDepth(), depth);
	}

	//! The ranges' boundaries are accepted as given (not clamped further, not refused).
	void boundaryProposalsAreAccepted()
	{
		const QString channel = ch(2);
		const std::vector<QJsonObject> proposals{
			{{QStringLiteral("channel"), channel}, {QStringLiteral("low"), -18.0}},
			{{QStringLiteral("channel"), channel}, {QStringLiteral("high"), 18.0}},
			{{QStringLiteral("channel"), channel}, {QStringLiteral("mid"), -18.0}, {QStringLiteral("mid_freq"), 100.0}},
			{{QStringLiteral("channel"), channel}, {QStringLiteral("mid"), 18.0}, {QStringLiteral("mid_freq"), 10000.0}}};
		for (const QJsonObject& proposal : proposals)
		{
			const std::vector<float> before = allEq();
			const int depth = undoDepth();
			const ControlResult result = run(QStringLiteral("mixer.apply_masking_proposal"), {{QStringLiteral("strip_eq"), proposal}});
			QVERIFY2(result.ok, qPrintable(result.errorMessage));
			QCOMPARE(result.result.value(QStringLiteral("applied")).toBool(), true);
			const std::vector<float> after = allEq();
			int changed = 0;
			for (std::size_t i = 0; i < after.size(); ++i) { if (after[i] != before[i]) { ++changed; } }
			QCOMPARE(changed, proposal.contains(QStringLiteral("mid_freq")) ? 2 : 1);
			QCOMPARE(undoDepth(), depth + 1);
			QVERIFY(run(QStringLiteral("control.undo")).ok);
			QCOMPARE(allEq(), before);
			QCOMPARE(undoDepth(), depth);
		}
	}

	//! Malformed or out-of-range proposals are refused and write nothing at all.
	void badProposalsAreRefusedAndWriteNothing()
	{
		const QString channel = ch(2);
		const std::vector<QJsonObject> bad{
			{},
			{{QStringLiteral("strip_eq"), QStringLiteral("nope")}},
			{{QStringLiteral("strip_eq"), QJsonObject{{QStringLiteral("channel"), QStringLiteral("nope")}, {QStringLiteral("low"), 1.0}}}},
			{{QStringLiteral("strip_eq"), QJsonObject{{QStringLiteral("channel"), channel}, {QStringLiteral("gain"), 1.0}}}},
			{{QStringLiteral("strip_eq"), QJsonObject{{QStringLiteral("channel"), channel}}}},
			{{QStringLiteral("strip_eq"), QJsonObject{{QStringLiteral("channel"), channel}, {QStringLiteral("mid_freq"), 200.0}}}},
			{{QStringLiteral("strip_eq"), QJsonObject{{QStringLiteral("channel"), channel}, {QStringLiteral("low"), 18.1}}}},
			{{QStringLiteral("strip_eq"), QJsonObject{{QStringLiteral("channel"), channel}, {QStringLiteral("mid"), 1.0}, {QStringLiteral("mid_freq"), 99.0}}}},
			{{QStringLiteral("strip_eq"), QJsonObject{{QStringLiteral("channel"), channel}, {QStringLiteral("mid"), QStringLiteral("loud")}}}}};
		const std::vector<float> before = allEq();
		const int depth = undoDepth();
		for (const QJsonObject& args : bad)
		{
			const ControlResult result = run(QStringLiteral("mixer.apply_masking_proposal"), args);
			QCOMPARE(result.errorKind, ControlErrorKind::InvalidArgs);
			QCOMPARE(allEq(), before);
			QCOMPARE(undoDepth(), depth);
		}
		//! A well-formed channel id that names no live channel is NotFound, not InvalidArgs, and writes nothing.
		const ControlResult missing = run(QStringLiteral("mixer.apply_masking_proposal"),
			{{QStringLiteral("strip_eq"), QJsonObject{{QStringLiteral("channel"), QStringLiteral("ch-9999")}, {QStringLiteral("low"), 1.0}}}});
		QVERIFY(!missing.ok);
		QCOMPARE(missing.errorKind, ControlErrorKind::NotFound);
		QCOMPARE(allEq(), before);
		QCOMPARE(undoDepth(), depth);
	}

	void badArgumentsAreRefused()
	{
		const auto kind = [](const QJsonObject& args) { return run(QStringLiteral("mixer.masking_report"), args).errorKind; };
		QCOMPARE(kind({{QStringLiteral("channels"), QJsonArray{ch(1)}}}), ControlErrorKind::InvalidArgs);
		QCOMPARE(kind({{QStringLiteral("channels"), QJsonArray{ch(1), ch(1)}}}), ControlErrorKind::InvalidArgs);
		QCOMPARE(kind({{QStringLiteral("channels"), QJsonArray{ch(1), ch(2)}}, {QStringLiteral("priorities"), QJsonObject{{ch(1), QStringLiteral("loud")}}}}),
			ControlErrorKind::InvalidArgs);
		QCOMPARE(kind({{QStringLiteral("channels"), QJsonArray{ch(1), ch(2)}}, {QStringLiteral("priorities"), QJsonObject{{ch(3), QStringLiteral("front")}}}}),
			ControlErrorKind::InvalidArgs);
		QVERIFY(!run(QStringLiteral("mixer.masking_report"), {{QStringLiteral("channels"), QJsonArray{ch(1), QStringLiteral("nope")}}}).ok);
	}

		//! The channel boundary: 2..16 distinct channels are reported; 17 are refused as invalid_args and write nothing.
		void sixteenChannelsAreReportedAndSeventeenRefused()
		{
			while (Engine::mixer()->numChannels() < 17) { Engine::mixer()->createChannel(); }
			QJsonArray sixteen;
			for (int i = 0; i < 16; ++i) { sixteen.append(ch(i)); }
			const std::vector<float> eq = allEq();
			const int depth = undoDepth();
			const ControlResult ok16 = run(QStringLiteral("mixer.masking_report"), {{QStringLiteral("channels"), sixteen}});
			QVERIFY2(ok16.ok, qPrintable(ok16.errorMessage));
			QCOMPARE(ok16.result.value(QStringLiteral("channels")).toArray().size(), 16);
			QJsonArray seventeen = sixteen;
			seventeen.append(ch(16));
			const ControlResult ok17 = run(QStringLiteral("mixer.masking_report"), {{QStringLiteral("channels"), seventeen}});
			QVERIFY(!ok17.ok);
			QCOMPARE(ok17.errorKind, ControlErrorKind::InvalidArgs);
			QCOMPARE(allEq(), eq);
			QCOMPARE(undoDepth(), depth);
		}

		//! A ten-second clip runs far past the 64 frames the analysis reads: the clash is still found, bounded, and the same on every call.
		void aClipBeyondTheAnalysisCapIsBoundedAndDeterministic()
		{
			while (Engine::mixer()->numChannels() < 6) { Engine::mixer()->createChannel(); }
			SampleTrack* longTrack = toneTrack(5, {200.0}, 0.25f, 10.0);
			const QJsonObject priorities{{ch(1), QStringLiteral("front")}, {ch(5), QStringLiteral("back")}};
			const QJsonObject args{{QStringLiteral("channels"), QJsonArray{ch(1), ch(5)}}, {QStringLiteral("priorities"), priorities}};
			const ControlResult first = run(QStringLiteral("mixer.masking_report"), args);
			QVERIFY2(first.ok, qPrintable(first.errorMessage));
			const QJsonObject clash = clashAt(first.result, ch(1), ch(5), 200.0);
			QVERIFY(!clash.isEmpty());
			QCOMPARE(clash.value(QStringLiteral("target")).toString(), ch(5));
			const ControlResult second = run(QStringLiteral("mixer.masking_report"), args);
			QVERIFY(second.ok);
			QCOMPARE(QJsonDocument(second.result).toJson(QJsonDocument::Compact), QJsonDocument(first.result).toJson(QJsonDocument::Compact));
			delete longTrack;
		}

private:
	QString m_instrument;
};

QTEST_MAIN(MixerMaskingReportTest)
#include "MixerMaskingReportTest.moc"
