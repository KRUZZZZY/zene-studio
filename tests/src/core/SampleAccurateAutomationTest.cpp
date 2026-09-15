/*! SampleAccurateAutomationTest.cpp - feature-list row 9 / board task #646:
 *  sample-accurate automation, measured rather than asserted.
 *
 *  THE MEASUREMENT. `AutomatableModel::valueBuffer()` is the per-sample buffer the
 *  audio path multiplies with (MixerChannel::updatePostFaderBuffer, the fx chains).
 *  Before this feature it was an interpolation from the value the previous block
 *  ended on to the value this block's first tick applied: a whole-block smear, and
 *  a block LATE. This file renders real blocks through `Song::processNextBuffer()`
 *  - the entry AudioEngine::renderStageNoteSetup() calls on the render thread, with
 *  the dummy device stopped so one thread walks the song - and compares that buffer
 *  with the curve the clip holds, at every frame of every block:
 *
 *   * in `sample` mode the deviation is inside tolerance and the value MOVES
 *     inside the block, on the frame the curve moves on;
 *   * the SAME run in `block` mode reports the smear, which is what makes the
 *     first claim non-vacuous - a regression to the block-quantised behaviour
 *     fails this file;
 *   * the ramp builder is checked directly, for both progression types;
 *   * the ramp path allocates nothing over 64 blocks (AllocationProbe.h), and a
 *     full ramp REFUSES a knot rather than growing.
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
 */

#include <QtTest>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <QJsonObject>
#include <QString>

#include "AllocationProbe.h"
#include "AutomatableModel.h"
#include "AutomationClip.h"
#include "AutomationRamp.h"
#include "ControlAutomationSupport.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "RackTestSupport.h"
#include "Song.h"
#include "TrackContainer.h"

using namespace lmms;
using namespace racktest;

namespace
{

//! One evidence line, flushed: the tests print what they measured.
void evidence(const char* label, const QString& detail)
{
	std::fprintf(stdout, "AUTOMATION_EVIDENCE %s %s\n", label, detail.toUtf8().constData());
	std::fflush(stdout);
}

//! The curve every rig here is driven with, in the MODEL's own unit (the rack
//! fixture's fx-0 Gain parameter, range 0..100).
struct CurveNode
{
	double ticks;
	double value;
};

const std::vector<CurveNode> kCurve{{0.0, 20.0}, {24.0, 80.0}, {48.0, 35.0}, {72.0, 95.0}};

//! @a discrete picks the shape: Discrete HOLDS a node's value until the next node
//! (the engine's default, and what `automation.add_point` writes), Linear moves
//! along the straight line between two of them.
double curveModelValue(const std::vector<CurveNode>& nodes, double relTicks, bool discrete)
{
	if (nodes.empty()) { return 0.0; }
	if (relTicks <= nodes.front().ticks) { return nodes.front().value; }
	for (std::size_t i = 1; i < nodes.size(); ++i)
	{
		if (relTicks <= nodes[i].ticks)
		{
			const CurveNode& a = nodes[i - 1];
			const CurveNode& b = nodes[i];
			if (discrete) { return a.value; }
			const double span = b.ticks - a.ticks;
			return span <= 0.0 ? b.value
				: a.value + (relTicks - a.ticks) / span * (b.value - a.value);
		}
	}
	return nodes.back().value;
}

//! "ch-<kChannel>", spelled the way the commands parse it.
QString channelIdOf() { return QStringLiteral("ch-") + QString::number(kChannel); }

//! The channel's own fx-0 gain parameter: the model the audio path reads and the id
//! the surface addresses, resolved through the same resolver the commands use.
bool channelGainParameter(control::AutomationParameter* out, QString* why)
{
	ControlTarget target;
	ControlResult error;
	if (!resolveControlTarget(channelIdOf(), &target, &error))
	{
		*why = error.errorMessage;
		return false;
	}
	for (const control::AutomationParameter& candidate : control::automationParameters(target))
	{
		if (candidate.pluginId == QStringLiteral("fx-0")
			&& candidate.model->displayName() == QStringLiteral("Gain"))
		{
			*out = candidate;
			return true;
		}
	}
	*why = QStringLiteral("the channel's fx-0 effect exposes no parameter named Gain");
	return false;
}

//! Invoke a command through the registry, exactly as the socket does.
ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}
//! The arguments automation.ramp_set takes, for one parameter and mode.
QJsonObject rampArgs(const QString& parameterId, const QString& mode)
{
	return QJsonObject{{QStringLiteral("track"), channelIdOf()},
		{QStringLiteral("parameter"), parameterId},
		{QStringLiteral("mode"), mode}};
}

bool setMode(const QString& parameterId, const QString& mode, ControlResult* result)
{
	*result = run(QStringLiteral("automation.ramp_set"), rampArgs(parameterId, mode));
	return result->ok;
}

//! The mode automation.ramp_get reports for @a parameterId, or "<absent>".
QString reportedMode(const QString& parameterId)
{
	const ControlResult listed = run(QStringLiteral("automation.ramp_get"), QJsonObject());
	if (!listed.ok) { return QStringLiteral("<get failed>"); }
	for (const QJsonValue& value : listed.result.value(QStringLiteral("parameters")).toArray())
	{
		const QJsonObject entry = value.toObject();
		if (entry.value(QStringLiteral("parameter")).toString() == parameterId)
		{
			return entry.value(QStringLiteral("mode")).toString();
		}
	}
	return QStringLiteral("<absent>");
}

//! Write kCurve into the parameter's clip through the commands a client uses.
bool writeCurveThroughTheSurface(const QString& parameterId, QString* why)
{
	for (const CurveNode& node : kCurve)
	{
		const ControlResult added = run(QStringLiteral("automation.add_point"),
			QJsonObject{{QStringLiteral("track"), channelIdOf()},
				{QStringLiteral("parameter"), parameterId},
				{QStringLiteral("ticks"), node.ticks},
				{QStringLiteral("value"), node.value}});
		if (!added.ok)
		{
			*why = QStringLiteral("automation.add_point refused tick %1: %2")
				.arg(node.ticks).arg(added.errorMessage);
			return false;
		}
	}
	return true;
}

//! What a run measured: the largest distance between the audio path's own value
//! and the curve, the frames outside tolerance, and the first frame that moved.
struct Measurement
{
	double maxDeviation = 0.0;
	int framesOffCurve = 0;
	int frames = 0;
	int firstMoveFrame = -1;
	double atTick = 0.0;
};

/*! Render @a periods blocks from the song's start and measure EVERY one of them
 *  against the curve, so the run need not guess which block holds a node.
 */
Measurement measureRender(Song* song, AutomationClip* clip, AutomatableModel* model,
	int periods, bool discrete, double tolerance)
{
	song->stop();
	song->playSong();
	song->getTimeline().setTicks(0);

	const double framesPerTick = static_cast<double>(Engine::framesPerTick());
	const double clipStart = static_cast<double>(clip->startPosition().getTicks());

	Measurement total;
	for (int period = 0; period < periods; ++period)
	{
		const double startTicks = static_cast<double>(song->getPlayPos().getTicks());
		const int frameOffset = static_cast<int>(song->getTimeline().frameOffset());
		song->processNextBuffer();

		// The per-sample source the audio path reads. nullptr means the engine
		// has no sample-exact data and a consumer uses the static value(), which
		// is what this measures instead - the real path either way.
		ValueBuffer* buffer = model->valueBuffer();
		const float staticValue = model->value<float>();
		const int blockFrames = buffer != nullptr ? buffer->length()
			: static_cast<int>(Engine::audioEngine()->framesPerPeriod());
		const double first = buffer != nullptr ? static_cast<double>(buffer->value(0))
			: static_cast<double>(staticValue);

		int moveFrame = -1;
		int offCurve = 0;
		double worst = 0.0;
		for (int f = 0; f < blockFrames; ++f)
		{
			const double relTicks = startTicks - clipStart
				+ (static_cast<double>(frameOffset) + f) / framesPerTick;
			const double got = buffer != nullptr ? static_cast<double>(buffer->value(f))
				: static_cast<double>(staticValue);
			const double deviation =
				std::fabs(got - curveModelValue(kCurve, relTicks, discrete));
			if (f > 0 && moveFrame < 0 && std::fabs(got - first) > tolerance) { moveFrame = f; }
			if (deviation > worst) { worst = deviation; }
			if (deviation > tolerance) { ++offCurve; }
		}

		total.frames += blockFrames;
		total.framesOffCurve += offCurve;
		if (worst > total.maxDeviation) { total.maxDeviation = worst; total.atTick = startTicks; }
		if (total.firstMoveFrame < 0
			|| (moveFrame >= 0 && moveFrame < total.firstMoveFrame))
		{
			total.firstMoveFrame = moveFrame;
		}
	}
	return total;
}

QString describe(const char* label, const Measurement& m)
{
	return QStringLiteral("%1 frames=%2 max_deviation=%3 (tick %4) off_curve=%5 move_frame=%6")
		.arg(QString::fromLatin1(label)).arg(m.frames).arg(m.maxDeviation, 0, 'g', 6)
		.arg(m.atTick, 0, 'f', 0).arg(m.framesOffCurve).arg(m.firstMoveFrame);
}

//! The tolerance, in the gain model's own unit (range 0..100). A knot is written
//! on the frame the boundary falls on, so a Discrete step and a Linear slope both
//! reproduce exactly: this is slack for float round-off, while the block-quantised
//! render misses by tens of units because it carries a whole block's lag.
constexpr double kTolerance = 0.25;

} // namespace

class SampleAccurateAutomationTest : public QObject
{
	Q_OBJECT

private slots:

	void initTestCase()
	{
		QString why;
		QVERIFY2(initRackFixture(&why), qPrintable(why));
	}

	void cleanupTestCase() { teardownRackFixture(); }

	//! THE LOAD-BEARING CASE, with its own negative control in the same run.
	void theAudioPathFollowsTheCurveInsideTheBlock()
	{
		control::AutomationParameter gain;
		QString why;
		QVERIFY2(channelGainParameter(&gain, &why), qPrintable(why));
		QVERIFY2(writeCurveThroughTheSurface(gain.id(), &why), qPrintable(why));

		AutomationClip* clip = control::existingAutomationClip(gain.model);
		QVERIFY(clip != nullptr);
		QVERIFY2(clip->hasAutomation(), "the clip the surface just wrote holds no automation");

		ControlResult result;
		QVERIFY2(setMode(gain.id(), QStringLiteral("sample"), &result),
			qPrintable(result.errorMessage));
		const int periods = 8;
		const Measurement sample = measureRender(Engine::getSong(), clip, gain.model,
			periods, true, kTolerance);
		evidence("sample-mode", describe("sample", sample));

		QVERIFY2(sample.frames == periods * static_cast<int>(Engine::audioEngine()->framesPerPeriod()),
			"the measured blocks are not eight audio periods");
		QVERIFY2(sample.maxDeviation <= kTolerance,
			qPrintable(QStringLiteral("the audio path does not carry the curve at sample ")
				+ QStringLiteral("precision: ") + describe("sample", sample)));
		QVERIFY2(sample.framesOffCurve == 0, qPrintable(describe("sample", sample)));
		QVERIFY2(sample.firstMoveFrame > 0,
			"the per-sample value never moved inside a block, so it is not a ramp");

		// The control: the identical run, block-quantised, must measure far worse
		// than this one - or the claim above is vacuous.
		QVERIFY2(setMode(gain.id(), QStringLiteral("block"), &result),
			qPrintable(result.errorMessage));
		const Measurement block = measureRender(Engine::getSong(), clip, gain.model,
			periods, true, kTolerance);
		evidence("block-mode", describe("block", block));

		QVERIFY2(block.maxDeviation > 10.0 * sample.maxDeviation,
			qPrintable(QStringLiteral("the measurement cannot tell the block-quantised render ")
				+ QStringLiteral("from the sample-accurate one - the claim above would be ")
				+ QStringLiteral("vacuous: ") + describe("block", block)));
	}

	//! The ramp builder itself, against the curve, at every frame of a block
	//! whose start is deliberately inside a tick (37 frames into tick 10).
	void theRampReproducesTheCurveAtEveryFrame()
	{
		control::AutomationParameter gain;
		QString why;
		QVERIFY2(channelGainParameter(&gain, &why), qPrintable(why));
		QVERIFY2(writeCurveThroughTheSurface(gain.id(), &why), qPrintable(why));

		AutomationClip* clip = control::existingAutomationClip(gain.model);
		QVERIFY(clip != nullptr);

		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		const double framesPerTick = static_cast<double>(Engine::framesPerTick());
		for (const bool discrete : {true, false})
		{
			clip->setProgressionType(discrete ? AutomationClip::ProgressionType::Discrete
				: AutomationClip::ProgressionType::Linear);
			AutomationRamp ramp;
			clip->writeBlockRamp(ramp, TimePos(10), frames, framesPerTick, 37);

			QVERIFY2(ramp.knotCount() > 2, "a multi-tick block produced no knots");
			QCOMPARE(ramp.refusals(), std::uint32_t{0});
			QVERIFY(ramp.sampleAccurate());
			QCOMPARE(ramp.frames(), frames);

			double worst = 0.0;
			for (f_cnt_t f = 0; f < frames; ++f)
			{
				const double relTicks = 10.0 + (37.0 + static_cast<double>(f)) / framesPerTick;
				// The ramp carries the clip's STORED value, so the reference goes
				// through the same inverse scaling the command used.
				const double expected = static_cast<double>(gain.model->inverseScaledValue(
					static_cast<float>(curveModelValue(kCurve, relTicks, discrete))));
				worst = std::max(worst,
					std::fabs(expected - static_cast<double>(ramp.valueAt(f))));
			}
			evidence(discrete ? "ramp-versus-curve-discrete" : "ramp-versus-curve-linear",
				QStringLiteral("knots=%1 frames=%2 max_deviation=%3")
					.arg(ramp.knotCount()).arg(frames).arg(worst, 0, 'g', 6));
			QVERIFY2(worst <= 1.0e-4, qPrintable(QStringLiteral("max deviation %1 stored units")
				.arg(worst, 0, 'g', 6)));
		}
	}

	//! Workspace rule 4, measured on the path the audio thread takes: the ramp
	//! build AND the per-sample fill, 64 blocks, zero allocations.
	void theSampleAccuratePathAllocatesNothing()
	{
		control::AutomationParameter gain;
		QString why;
		QVERIFY2(channelGainParameter(&gain, &why), qPrintable(why));
		QVERIFY2(writeCurveThroughTheSurface(gain.id(), &why), qPrintable(why));

		ControlResult result;
		QVERIFY2(setMode(gain.id(), QStringLiteral("sample"), &result),
			qPrintable(result.errorMessage));

		Song* song = Engine::getSong();
		const TrackContainer::TrackList& tracks = song->tracks();
		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();

		lmms::test::resetAllocationCount();
		for (int block = 0; block < 64; ++block)
		{
			// Moving the play head is NOT part of the measurement: only the two
			// calls an audio block would make are inside the counted window.
			song->getTimeline().setTicks(block * 2);
			lmms::test::tlCountAllocations = true;
			AutomatableModel::incrementPeriodCounter();
			song->buildAutomationRamps(tracks, song->getPlayPos(), frames,
				static_cast<int>(song->getTimeline().frameOffset()));
			(void)gain.model->valueBuffer();
			lmms::test::tlCountAllocations = false;
		}
		const std::uint64_t allocations = lmms::test::tlAllocationCount;

		evidence("ramp-allocations",
			QStringLiteral("allocations=%1 over 64 blocks of ramp build + per-sample fill")
				.arg(static_cast<qulonglong>(allocations)));
		QCOMPARE(allocations, std::uint64_t{0});
	}

	//! The capacity bound: a ramp with no room REFUSES the knot and counts it, so
	//! nothing grows with tempo or block size.
	void theRampRefusesRatherThanGrows()
	{
		AutomationRamp ramp;
		ramp.reset(512);
		for (int i = 0; i < AutomationRamp::MaxKnots + 8; ++i)
		{
			QVERIFY(ramp.addKnot(static_cast<f_cnt_t>(i * 4), static_cast<float>(i)));
		}
		QCOMPARE(ramp.knotCount(), AutomationRamp::MaxKnots);
		QCOMPARE(ramp.refusals(), std::uint32_t{8});
		// A knot that goes BACKWARDS is refused too - the ramp is built forwards.
		QVERIFY(!ramp.addKnot(0, 1.0f));
		QCOMPARE(ramp.refusals(), std::uint32_t{9});
		QVERIFY(ramp.sampleAccurate());
		QCOMPARE(ramp.valueAt(0), 0.0f);
		QCOMPARE(ramp.valueAt(4), 1.0f);
		// Past the last knot the value holds rather than reading past the array.
		QCOMPARE(ramp.valueAt(4000), static_cast<float>(AutomationRamp::MaxKnots - 1));
		QCOMPARE(ramp.knot(AutomationRamp::MaxKnots + 5).value, 0.0f);
		evidence("ramp-bounds",
			QStringLiteral("capacity=%1 knots=%2 refusals=%3")
				.arg(AutomationRamp::MaxKnots).arg(ramp.knotCount()).arg(ramp.refusals()));
	}

	//! The surface half: the ids exist with their schemas, the mode round-trips
	//! through the registry, and SPEC A16's inverse is tested FOR REAL - set,
	//! control.undo, read the state back.
	void theSurfaceDrivesAndReversesTheMode()
	{
		control::AutomationParameter gain;
		QString why;
		QVERIFY2(channelGainParameter(&gain, &why), qPrintable(why));
		QVERIFY2(writeCurveThroughTheSurface(gain.id(), &why), qPrintable(why));

		const ControlCommand* set = ControlRegistry::instance()->command(
			QStringLiteral("automation.ramp_set"));
		const ControlCommand* get = ControlRegistry::instance()->command(
			QStringLiteral("automation.ramp_get"));
		QVERIFY2(set != nullptr, "automation.ramp_set is not registered");
		QVERIFY2(get != nullptr, "automation.ramp_get is not registered");
		QVERIFY(set->mutating);
		QVERIFY(!set->argsSchema.isEmpty());
		QVERIFY(!set->resultSchema.isEmpty());
		QVERIFY2(!get->mutating, "the read half claims to write");

		const QString before = reportedMode(gain.id());
		QVERIFY2(before == QStringLiteral("sample") || before == QStringLiteral("block"),
			qPrintable(QStringLiteral("automation.ramp_get reported '%1'").arg(before)));
		const QString flipped = before == QStringLiteral("sample")
			? QStringLiteral("block") : QStringLiteral("sample");

		ControlResult result;
		QVERIFY2(setMode(gain.id(), flipped, &result), qPrintable(result.errorMessage));
		QCOMPARE(result.result.value(QStringLiteral("changed")).toBool(), true);
		QCOMPARE(result.result.value(QStringLiteral("mode")).toString(), flipped);
		QCOMPARE(reportedMode(gain.id()), flipped);

		// SPEC A16: the inverse is the engine's own undo, not a second history.
		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QCOMPARE(reportedMode(gain.id()), before);
		evidence("a16-inverse",
			QStringLiteral("%1 -> %2 -> control.undo -> %3").arg(before, flipped, before));

		// Typed refusals, not crashes and not silent no-ops: a mode that is not
		// a mode, a device id that is not there, and a parameter that exists but
		// has NO curve - feature row 9 is a property of a curve, so the last one
		// is a typed not_found rather than a write that would change nothing.
		QCOMPARE(run(QStringLiteral("automation.ramp_set"),
			rampArgs(gain.id(), QStringLiteral("sideways"))).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("automation.ramp_set"),
			rampArgs(QStringLiteral("fx-9/0"), QStringLiteral("sample"))).errorKind,
			ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("automation.ramp_set"),
			rampArgs(QStringLiteral("fx-0/1"), QStringLiteral("sample"))).errorKind,
			ControlErrorKind::NotFound);
	}
};

QTEST_MAIN(SampleAccurateAutomationTest)
#include "SampleAccurateAutomationTest.moc"
