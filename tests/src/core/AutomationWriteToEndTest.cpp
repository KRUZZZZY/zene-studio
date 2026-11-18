/*
 * AutomationWriteToEndTest.cpp - automation.write_to_end, the write-pass helper (ED-201)
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

#include <QtTest>

#include <QJsonArray>

#include "AutomationClip.h"
#include "AutomationTrack.h"
#include "ControlAutomationSupport.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The first parameter automation.get_state names for @a track (the instrument's own), or empty.
QString firstParameter(const QString& track)
{
	const QJsonArray parameters = run(QStringLiteral("automation.get_state"), {{QStringLiteral("track"), track}}).result
		.value(QStringLiteral("tracks")).toArray().first().toObject().value(QStringLiteral("parameters")).toArray();
	return parameters.isEmpty() ? QString() : parameters.first().toObject().value(QStringLiteral("id")).toString();
}

//! @a extra plus the track and parameter an automation command addresses.
QJsonObject addressed(const QString& track, const QString& parameter, QJsonObject extra = {})
{
	extra.insert(QStringLiteral("track"), track);
	extra.insert(QStringLiteral("parameter"), parameter);
	return extra;
}

//! The model behind @a parameter of @a track, through the commands' own resolver.
AutomatableModel* modelOf(const QString& track, const QString& parameter)
{
	ControlTarget target;
	control::AutomationParameter found;
	ControlResult error;
	return control::resolveAutomationParameter(addressed(track, parameter), &target, &found, &error) ? found.model : nullptr;
}

} // namespace

class AutomationWriteToEndTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);  // a real instrument, whose parameters automation addresses
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
		Engine::getSong()->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		m_track->loadInstrument(QStringLiteral("tripleoscillator"));
	}

	/*! ED-201, write to end: from a tick to the song's end a control holds one value. Every later point goes, the
	 *  point at the tick steps from the curve's value there to the new one, a point at the song's end holds it, and
	 *  one undo step puts the clip back. A value outside the control's range is refused and writes nothing. */
	void writeToEndHoldsAValueToTheSongsEnd()
	{
		const QString id = control::trackIdOf(m_track);
		const QString parameter = firstParameter(id);
		QVERIFY(!parameter.isEmpty());
		AutomatableModel* model = modelOf(id, parameter);
		QVERIFY(model != nullptr);
		const double low = model->minValue<double>();
		const double high = model->maxValue<double>();
		Clip* midi = m_track->createClip(TimePos(0));  // the song: four bars
		midi->changeLength(TimePos(4 * 192));
		Engine::getSong()->updateLength();
		for (int tick : {0, 192, 384, 576})
		{
			QVERIFY(run(QStringLiteral("automation.add_point"), addressed(id, parameter,
				{{QStringLiteral("ticks"), tick}, {QStringLiteral("value"), low + (high - low) * tick / 576}})).ok);
		}
		AutomationClip* clip = control::existingAutomationClip(model);
		QVERIFY(clip != nullptr);
		const float curveThere = clip->valueAt(TimePos(288));
		const double held = low + (high - low) / 4;
		const float stored = model->inverseScaledValue(static_cast<float>(held));
		const auto ticksOf = [clip]() {
			QList<int> ticks;
			for (auto it = clip->getTimeMap().constBegin(); it != clip->getTimeMap().constEnd(); ++it) { ticks.append(it.key()); }
			return ticks;
		};

		const ControlResult wrote = run(QStringLiteral("automation.write_to_end"), addressed(id, parameter,
			{{QStringLiteral("ticks"), 288}, {QStringLiteral("value"), held}}));
		QVERIFY2(wrote.ok, qPrintable(wrote.errorMessage));
		QCOMPARE(wrote.result.value(QStringLiteral("end")).toInt(), 768);
		QCOMPARE(wrote.result.value(QStringLiteral("removed")).toInt(), 2);
		QCOMPARE(ticksOf(), (QList<int>{0, 192, 288, 768}));
		QCOMPARE(clip->getTimeMap().value(TimePos(288)).getInValue(), curveThere);  // the curve arrives where it was
		QCOMPARE(clip->getTimeMap().value(TimePos(288)).getOutValue(), stored);    // and steps to the held value
		QCOMPARE(clip->getTimeMap().value(TimePos(768)).getInValue(), stored);
		QCOMPARE(clip->valueAt(TimePos(500)), stored);

		QVERIFY(run(QStringLiteral("control.undo")).ok);  // one step
		QCOMPARE(ticksOf(), (QList<int>{0, 192, 384, 576}));
		QVERIFY(!run(QStringLiteral("automation.write_to_end"), addressed(id, parameter,
			{{QStringLiteral("ticks"), 288}, {QStringLiteral("value"), high + 1}})).ok);
		QCOMPARE(clip->getTimeMap().size(), 4);
	}

private:
	InstrumentTrack* m_track = nullptr;
};

QTEST_MAIN(AutomationWriteToEndTest)
#include "AutomationWriteToEndTest.moc"
