/*
 * ControllerConversionTest.cpp - clip.controllers_to_automation / controllers_from_automation
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

/*! Q050-291.1 (R-editing-93) over a real tripleoscillator track: a MIDI clip's CC lane converted to a device
 *  parameter's automation and back. Each case asserts exact ticks and values through the commands' own read-backs
 *  (clip.controllers_get, automation.get_state):
 *  - to_automation: value mapping, song ticks (clip start, start-time offset, a moved automation clip), the lane stays,
 *    one undo removes the created track;
 *  - an existing automation clip: nodes inside the span are replaced, the rest kept, one undo restores it all;
 *  - refusals change nothing and record no undo step: an ambiguous target, a pitch-bend lane, a switch parameter, a lane
 *    without points, a point before the automation clip, a non-linear clip with nodes outside the span;
 *  - from_automation: the nodes inside the clip, the value at a cut edge, quantisation to CC steps, one undo, the single
 *    automated parameter taken when none is named;
 *  - a save/load keeps the lane, the automation points and the parameter link. */

#include <QtTest>

#include <array>
#include <cmath>

#include <QDomDocument>
#include <QJsonArray>
#include <QTemporaryDir>

#include "AutomationClip.h"
#include "ComboBoxModel.h"
#include "ControlAutomationSupport.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Song.h"
#include "TrackContainer.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! "tick:value" for every point of a lane / automation read-back.
QStringList lane(const QJsonArray& points, const char* tickKey = "tick")
{
	QStringList out;
	for (const QJsonValue& value : points)
	{
		const QJsonObject point = value.toObject();
		out << QStringLiteral("%1:%2").arg(point.value(QLatin1String(tickKey)).toInt()).arg(std::lround(point.value(QStringLiteral("value")).toDouble() * 1000));
	}
	return out;
}

} // namespace

class ControllerConversionTest : public QObject
{
	Q_OBJECT

private:
	InstrumentTrack* m_track = nullptr;
	MidiClip* m_clip = nullptr;
	QString m_parameter;  //!< the parameter converted onto: a continuous one with a wide range
	double m_low = 0;
	double m_high = 1;

	QString clip() const { return control::clipIdOf(m_clip); }
	QString track() const { return control::trackIdOf(m_track); }

	QJsonObject args(const QString& laneId, const QString& parameter = QString()) const
	{
		QJsonObject out{{QStringLiteral("clip"), clip()}, {QStringLiteral("lane"), laneId}};
		if (!parameter.isEmpty()) { out.insert(QStringLiteral("parameter"), parameter); }
		return out;
	}

	QJsonObject ok(const QString& id, const QJsonObject& in)
	{
		const ControlResult result = run(id, in);
		if (!result.ok) { qWarning("%s refused: %s", qPrintable(id), qPrintable(result.errorMessage)); }
		return result.ok ? result.result : QJsonObject{{QStringLiteral("refused"), true}};
	}

	int depth() const { return run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(); }
	int trackCount() const { return static_cast<int>(Engine::getSong()->tracks().size()); }

	QJsonArray cc(int number) const
	{
		for (const QJsonValue& value : run(QStringLiteral("clip.controllers_get"), {{QStringLiteral("clip"), clip()}}).result.value(QStringLiteral("lanes")).toArray())
		{
			if (value.toObject().value(QStringLiteral("lane")).toString() == QStringLiteral("cc:%1").arg(number)) { return value.toObject().value(QStringLiteral("points")).toArray(); }
		}
		return {};
	}

	//! The parameter's automation as automation.get_state reports it (empty points when it has none).
	QJsonArray automated(const QString& parameter) const
	{
		const QJsonArray parameters = run(QStringLiteral("automation.get_state"), {{QStringLiteral("track"), track()}}).result
			.value(QStringLiteral("tracks")).toArray().first().toObject().value(QStringLiteral("parameters")).toArray();
		for (const QJsonValue& value : parameters)
		{
			if (value.toObject().value(QStringLiteral("id")).toString() == parameter)
			{
				return value.toObject().value(QStringLiteral("automation")).toObject().value(QStringLiteral("points")).toArray();
			}
		}
		return {};
	}

	AutomatableModel* modelOf(const QString& parameter) const
	{
		ControlTarget target;
		control::AutomationParameter found;
		ControlResult error;
		const QJsonObject in{{QStringLiteral("track"), track()}, {QStringLiteral("parameter"), parameter}};
		return control::resolveAutomationParameter(in, &target, &found, &error) ? found.model : nullptr;
	}

	//! "tick:cc" for every automation point, the parameter's value taken back to its CC step.
	QStringList autoLane(const QJsonArray& points) const
	{
		QStringList out;
		for (const QJsonValue& value : points)
		{
			const QJsonObject point = value.toObject();
			out << QStringLiteral("%1:%2").arg(point.value(QStringLiteral("ticks")).toInt())
				.arg(std::lround((point.value(QStringLiteral("value")).toDouble() - m_low) / (m_high - m_low) * 127.0));
		}
		return out;
	}

	//! Whether the automation point at index @a i holds CC @a number to within float precision (not just to the nearest step).
	bool pointIsCc(const QJsonArray& points, int i, int number) const
	{
		const double want = m_low + (m_high - m_low) * number / 127.0;
		return std::abs(points.at(i).toObject().value(QStringLiteral("value")).toDouble() - want) <= (m_high - m_low) * 1e-5;
	}

private slots:
	void initTestCase()
	{
		#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);  // a real instrument, whose parameters a lane can take
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, song));
		m_track->loadInstrument(QStringLiteral("tripleoscillator"));
		m_clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(192)));
		m_clip->changeLength(TimePos(384));
		m_parameter.clear();
		const QJsonArray parameters = run(QStringLiteral("automation.get_state"), {{QStringLiteral("track"), track()}}).result
			.value(QStringLiteral("tracks")).toArray().first().toObject().value(QStringLiteral("parameters")).toArray();
		for (const QJsonValue& value : parameters)
		{
			const QJsonObject entry = value.toObject();
			const QString id = entry.value(QStringLiteral("id")).toString();
			const AutomatableModel* model = modelOf(id);
			if (model != nullptr && dynamic_cast<const BoolModel*>(model) == nullptr && dynamic_cast<const ComboBoxModel*>(model) == nullptr
				&& entry.value(QStringLiteral("max")).toDouble() - entry.value(QStringLiteral("min")).toDouble() >= 100)
			{
				m_parameter = id;
				m_low = entry.value(QStringLiteral("min")).toDouble();
				m_high = entry.value(QStringLiteral("max")).toDouble();
				break;
			}
		}
		QVERIFY2(!m_parameter.isEmpty(), "tripleoscillator exposes no wide continuous parameter");
	}

	void aCcLaneBecomesTheParametersAutomationAndOneUndoTakesItBack()
	{
		m_clip->setControllerPoints(ControllerKind::ControlChange, 1, {{0, 0}, {96, 127}, {192, 64}});
		const int tracksBefore = trackCount();
		const int depthBefore = depth();
		const QJsonObject done = ok(QStringLiteral("clip.controllers_to_automation"), args(QStringLiteral("cc:1"), m_parameter));
		QCOMPARE(done.value(QStringLiteral("converted")).toInt(), 3);
		QVERIFY(done.value(QStringLiteral("created_automation_track")).toBool());
		QCOMPARE(done.value(QStringLiteral("track")).toString(), track());
		QCOMPARE(done.value(QStringLiteral("automation")).toObject().value(QStringLiteral("progression")).toString(), QStringLiteral("linear"));
		// The clip starts at song tick 192, the new automation clip at 0: every tick keeps its song time.
		const QStringList expected{QStringLiteral("192:0"), QStringLiteral("288:127"),
			QStringLiteral("384:64")};
		QCOMPARE(autoLane(automated(m_parameter)), expected);  // read back through automation.get_state, not the reply
		QCOMPARE(autoLane(done.value(QStringLiteral("automation")).toObject().value(QStringLiteral("points")).toArray()), expected);
		for (int i = 0; i < 3; ++i) { QVERIFY(pointIsCc(automated(m_parameter), i, std::array<int, 3>{0, 127, 64}[i])); }  // the exact value, not just the step
		QCOMPARE(lane(cc(1)), (QStringList{QStringLiteral("0:0"), QStringLiteral("96:127000"), QStringLiteral("192:64000")}));  // a copy: the lane stays
		QCOMPARE(trackCount(), tracksBefore + 1);
		QCOMPARE(depth(), depthBefore + 1);

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(trackCount(), tracksBefore);
		QVERIFY(automated(m_parameter).isEmpty());
		QCOMPARE(lane(cc(1)).size(), 3);
		QCOMPARE(depth(), depthBefore);
	}

	void clipPositionAndStartOffsetShiftTheTicksAndAnExistingClipKeepsItsOtherNodes()
	{
		ok(QStringLiteral("automation.lane_add"), {{QStringLiteral("track"), track()}, {QStringLiteral("parameter"), m_parameter}});
		AutomationClip* target = control::existingAutomationClip(modelOf(m_parameter));
		QVERIFY(target != nullptr);
		target->movePosition(TimePos(96));
		target->getTimeMap().clear();
		target->setProgressionType(AutomationClip::ProgressionType::Linear);
		const float high = modelOf(m_parameter)->inverseScaledValue(static_cast<float>(m_high));
		target->putValue(TimePos(0), high, false, true);    // song 96: before the span, kept
		target->putValue(TimePos(150), high, false, true);  // song 246: inside the converted span (song 192..288), replaced
		target->putValue(TimePos(900), high, false, true);  // song 996: after the span, kept
		m_clip->setStartTimeOffset(TimePos(-24));  // lane tick 24 is the clip's own start, song tick 192
		m_clip->setControllerPoints(ControllerKind::ControlChange, 7, {{24, 127}, {120, 0}});
		const int depthBefore = depth();
		const int tracksBefore = trackCount();
		const QJsonObject done = ok(QStringLiteral("clip.controllers_to_automation"), args(QStringLiteral("cc:7"), m_parameter));
		QVERIFY(!done.value(QStringLiteral("created_automation_track")).toBool());
		QCOMPARE(trackCount(), tracksBefore);
		// song 192 and 288, minus the automation clip's start (96).
		QCOMPARE(autoLane(automated(m_parameter)), (QStringList{QStringLiteral("0:127"), QStringLiteral("96:127"),
			QStringLiteral("192:0"), QStringLiteral("900:127")}));
		QCOMPARE(depth(), depthBefore + 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(autoLane(automated(m_parameter)), (QStringList{QStringLiteral("0:127"), QStringLiteral("150:127"),
			QStringLiteral("900:127")}));
		QCOMPARE(control::existingAutomationClip(modelOf(m_parameter))->startPosition().getTicks(), 96);
	}

	void refusalsChangeNothingAndRecordNoUndoStep()
	{
		m_clip->setControllerPoints(ControllerKind::ControlChange, 1, {{0, 10}, {96, 100}});
		m_clip->setControllerPoints(ControllerKind::PitchBend, 0, {{0, 8192}});
		const int tracksBefore = trackCount();
		int depthBefore = depth();
		const auto untouched = [&]
		{
			QCOMPARE(trackCount(), tracksBefore);
			QCOMPARE(depth(), depthBefore);
			QVERIFY(automated(m_parameter).isEmpty());
			QCOMPARE(lane(cc(1)), (QStringList{QStringLiteral("0:10000"), QStringLiteral("96:100000")}));
		};

		// Ambiguous: a track with many parameters needs an explicit target, and the refusal names candidates.
		const ControlResult ambiguous = run(QStringLiteral("clip.controllers_to_automation"), args(QStringLiteral("cc:1")));
		QCOMPARE(ambiguous.errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY2(ambiguous.errorMessage.contains(QStringLiteral("'parameter' is required")), qPrintable(ambiguous.errorMessage));
		QVERIFY2(ambiguous.errorMessage.contains(m_parameter) || ambiguous.errorMessage.contains(QStringLiteral("inst/")), qPrintable(ambiguous.errorMessage));
		untouched();
		QCOMPARE(run(QStringLiteral("clip.controllers_from_automation"), args(QStringLiteral("cc:1"))).errorKind, ControlErrorKind::InvalidArgs);  // none automated: no candidate
		untouched();

		QCOMPARE(run(QStringLiteral("clip.controllers_to_automation"), args(QStringLiteral("pitchbend"), m_parameter)).errorKind, ControlErrorKind::Refused);
		QCOMPARE(run(QStringLiteral("clip.controllers_to_automation"), args(QStringLiteral("nonsense"), m_parameter)).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("clip.controllers_to_automation"), args(QStringLiteral("cc:5"), m_parameter)).errorKind, ControlErrorKind::NotFound);  // no such lane
		QCOMPARE(run(QStringLiteral("clip.controllers_to_automation"), args(QStringLiteral("cc:1"), QStringLiteral("inst/99999"))).ok, false);
		QCOMPARE(run(QStringLiteral("clip.controllers_from_automation"), args(QStringLiteral("cc:1"), m_parameter)).errorKind, ControlErrorKind::NotFound);  // not automated
		untouched();

		// A parameter with no range (min == max) has nothing a CC could map onto. (tripleoscillator exposes no switch, so the range
		// is collapsed on a copy of the model's bounds; a BoolModel / ComboBoxModel takes the same refusal branch.)
		auto* flat = dynamic_cast<FloatModel*>(modelOf(QStringLiteral("inst/1")));
		QVERIFY(flat != nullptr);
		const float flatMin = flat->minValue(), flatMax = flat->maxValue(), flatStep = flat->AutomatableModel::step<float>();
		flat->setRange(3.f, 3.f, flatStep);
		QCOMPARE(flat->minValue(), flat->maxValue());
		const int depthFlat = depth();
		const ControlResult refused = run(QStringLiteral("clip.controllers_to_automation"), args(QStringLiteral("cc:1"), QStringLiteral("inst/1")));
		QCOMPARE(depth(), depthFlat);
		flat->setRange(flatMin, flatMax, flatStep);
		depthBefore = depth();  // moving the range is itself journalled; the refusal itself added nothing
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QVERIFY2(refused.errorMessage.contains(QStringLiteral("cannot map")), qPrintable(refused.errorMessage));
		untouched();

		// A point that would land before the automation clip it is written to.
		ok(QStringLiteral("automation.lane_add"), {{QStringLiteral("track"), track()}, {QStringLiteral("parameter"), m_parameter}});
		control::existingAutomationClip(modelOf(m_parameter))->movePosition(TimePos(1000));
		const int depthWithLane = depth();
		const QJsonArray nodesBefore = automated(m_parameter);
		QCOMPARE(run(QStringLiteral("clip.controllers_to_automation"), args(QStringLiteral("cc:1"), m_parameter)).errorKind, ControlErrorKind::Refused);
		QCOMPARE(depth(), depthWithLane);
		QCOMPARE(automated(m_parameter), nodesBefore);
		QCOMPARE(lane(cc(1)).size(), 2);

		// A non-linear clip with a node outside the converted span cannot be made linear without changing that node.
		AutomationClip* target = control::existingAutomationClip(modelOf(m_parameter));
		target->movePosition(TimePos(0));
		target->putValue(TimePos(1500), 0.5f, false, true);
		QCOMPARE(target->progressionType(), AutomationClip::ProgressionType::Discrete);
		const QJsonArray discrete = automated(m_parameter);
		const int depthDiscrete = depth();
		const ControlResult nonLinear = run(QStringLiteral("clip.controllers_to_automation"), args(QStringLiteral("cc:1"), m_parameter));
		QCOMPARE(nonLinear.errorKind, ControlErrorKind::Refused);
		QVERIFY2(nonLinear.errorMessage.contains(QStringLiteral("not linear")), qPrintable(nonLinear.errorMessage));
		QCOMPARE(depth(), depthDiscrete);
		QCOMPARE(automated(m_parameter), discrete);
		QCOMPARE(target->progressionType(), AutomationClip::ProgressionType::Discrete);
	}

	void aNonLinearClipWithNothingOutsideTheSpanIsMadeLinearAndUndoPutsItBack()
	{
		ok(QStringLiteral("automation.lane_add"), {{QStringLiteral("track"), track()}, {QStringLiteral("parameter"), m_parameter}});
		AutomationClip* target = control::existingAutomationClip(modelOf(m_parameter));
		target->getTimeMap().clear();
		target->putValue(TimePos(250), 0.25f, false, true);  // inside the span 192..384
		QCOMPARE(target->progressionType(), AutomationClip::ProgressionType::Discrete);
		m_clip->setControllerPoints(ControllerKind::ControlChange, 11, {{0, 127}, {192, 0}});
		ok(QStringLiteral("clip.controllers_to_automation"), args(QStringLiteral("cc:11"), m_parameter));
		QCOMPARE(autoLane(automated(m_parameter)), (QStringList{QStringLiteral("192:127"), QStringLiteral("384:0")}));
		QCOMPARE(target->progressionType(), AutomationClip::ProgressionType::Linear);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(target->progressionType(), AutomationClip::ProgressionType::Discrete);
		QCOMPARE(target->getTimeMap().size(), 1);
		QVERIFY(target->getTimeMap().contains(250));
	}

	void automationBecomesALaneInsideTheClipAndRoundTrips()
	{
		// The clip spans song ticks 192..576. Nodes at 0 and 96 lie before it, 288 and 384 inside, 800 after it.
		ok(QStringLiteral("automation.lane_add"), {{QStringLiteral("track"), track()}, {QStringLiteral("parameter"), m_parameter}});
		AutomationClip* source = control::existingAutomationClip(modelOf(m_parameter));
		source->getTimeMap().clear();
		source->setProgressionType(AutomationClip::ProgressionType::Linear);
		const AutomatableModel* model = modelOf(m_parameter);
		const auto raw = [&](double fraction) { return model->inverseScaledValue(static_cast<float>(m_low + (m_high - m_low) * fraction)); };
		source->putValue(TimePos(0), raw(0.0), false, true);
		source->putValue(TimePos(288), raw(0.5), false, true);
		source->putValue(TimePos(384), raw(1.0), false, true);
		source->putValue(TimePos(800), raw(0.0), false, true);
		m_clip->setControllerPoints(ControllerKind::ControlChange, 74, {{5, 5}});
		const int depthBefore = depth();
		const QJsonObject done = ok(QStringLiteral("clip.controllers_from_automation"), args(QStringLiteral("cc:74"), m_parameter));
		QCOMPARE(done.value(QStringLiteral("source_progression")).toString(), QStringLiteral("linear"));
		// Lane tick = song tick - 192. 288 -> 96 (0.5 -> CC 64: 63.5 rounds up), 384 -> 192 (127). The curve is cut by the clip's
		// start (song 192: 0 + (288-192)/288 of the way to 0.5) and end (song 576: 1.0 -> 0.0 over 384..800, 192/416 along).
		const QJsonArray points = cc(74);
		QCOMPARE(done.value(QStringLiteral("points")).toArray(), points);  // the reply is what clip.controllers_get reads back
		QCOMPARE(points.size(), 4);
		QCOMPARE(points.at(0).toObject().value(QStringLiteral("tick")).toInt(), 0);
		QCOMPARE(points.at(0).toObject().value(QStringLiteral("value")).toInt(), static_cast<int>(std::lround(127.0 * (0.5 * 192 / 288))));
		QCOMPARE(lane(QJsonArray{points.at(1), points.at(2)}), (QStringList{QStringLiteral("96:64000"), QStringLiteral("192:127000")}));
		QCOMPARE(points.at(3).toObject().value(QStringLiteral("tick")).toInt(), 384);
		QCOMPARE(points.at(3).toObject().value(QStringLiteral("value")).toInt(), static_cast<int>(std::lround(127.0 * (1.0 - 192.0 / 416))));
		QCOMPARE(depth(), depthBefore + 1);

		// The automation is untouched (a copy), and undo restores the lane that was there.
		QCOMPARE(source->getTimeMap().size(), 4);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(lane(cc(74)), (QStringList{QStringLiteral("5:5000")}));
		QCOMPARE(depth(), depthBefore);

		// The single automated parameter is taken when none is named, and a CC lane converts back onto the same points.
		ok(QStringLiteral("clip.controllers_from_automation"), args(QStringLiteral("cc:74")));
		QCOMPARE(cc(74), points);
		ok(QStringLiteral("clip.controllers_to_automation"), args(QStringLiteral("cc:74"), m_parameter));
		ok(QStringLiteral("clip.controllers_from_automation"), args(QStringLiteral("cc:75"), m_parameter));
		QCOMPARE(cc(75), points);  // CC -> automation -> CC lands on the same CC values and ticks
	}

	void convertedDataAndTheParameterLinkSurviveASaveAndLoad()
	{
		m_clip->setControllerPoints(ControllerKind::ControlChange, 1, {{0, 0}, {96, 127}});
		ok(QStringLiteral("clip.controllers_to_automation"), args(QStringLiteral("cc:1"), m_parameter));
		ok(QStringLiteral("clip.controllers_from_automation"), args(QStringLiteral("cc:2"), m_parameter));
		const QStringList automation = autoLane(automated(m_parameter));
		const QStringList copy = lane(cc(2));
		QCOMPARE(automation.size(), 2);

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath(QStringLiteral("converted.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(path));
		QFile written(path);
		QVERIFY(written.open(QIODevice::ReadOnly));
		const QString text = QString::fromUtf8(written.readAll());
		QVERIFY2(text.contains(QStringLiteral("<lane id=\"cc:1\"")) && text.contains(QStringLiteral("<lane id=\"cc:2\"")),
			"the project file does not carry the clip's CC lanes");
		QVERIFY2(text.contains(QStringLiteral("<controllers metadata=\"1\">")), "the lanes lost the metadata flag older builds skip them by");
		written.close();
		Engine::getSong()->clearProject();
		Engine::getSong()->loadProject(path);

		m_track = nullptr;
		for (Track* each : Engine::getSong()->tracks()) { if (m_track == nullptr) { m_track = dynamic_cast<InstrumentTrack*>(each); } }
		QVERIFY(m_track != nullptr);
		m_clip = dynamic_cast<MidiClip*>(m_track->getClips().front());
		QVERIFY(m_clip != nullptr);
		QCOMPARE(lane(cc(1)), (QStringList{QStringLiteral("0:0"), QStringLiteral("96:127000")}));
		QCOMPARE(lane(cc(2)), copy);
		AutomatableModel* model = modelOf(m_parameter);
		QVERIFY(model != nullptr);
		AutomationClip* linked = control::existingAutomationClip(model);
		QVERIFY2(linked != nullptr, "the loaded automation clip is not linked to the parameter");
		QCOMPARE(autoLane(automated(m_parameter)), automation);
		QCOMPARE(linked->progressionType(), AutomationClip::ProgressionType::Linear);
	}
};

QTEST_GUILESS_MAIN(ControllerConversionTest)
#include "ControllerConversionTest.moc"
