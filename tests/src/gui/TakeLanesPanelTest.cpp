/*
 * TakeLanesPanelTest.cpp - M3 item 6: the take lanes panel, driven like a person drives it
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

/*! Two takes on a sample track, then only the panel's own buttons: add two lanes, comp one
 *  half from each lane, audition a lane, and consolidate - each checked against the
 *  socket-visible state (comp.get_state, the track's clips), which is the panel's contract.
 */

#include <QtTest>

#include <QComboBox>
#include <QJsonArray>
#include <QSpinBox>
#include <QToolButton>

#include <memory>
#include <vector>

#include "AudioEngine.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "TakeLanesPanel.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

SampleClip* addTake(Track* track, float level)
{
	auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
	const int rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	std::vector<SampleFrame> data(static_cast<std::size_t>(rate / 2), SampleFrame(level, level));
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
	return clip;
}

} // namespace

class TakeLanesPanelTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void thePanelCompsAuditionsAndConsolidatesThroughTheRegistry()
	{
		const QString id = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}})
			.result.value(QStringLiteral("track")).toString();
		ControlResult error;
		Track* track = control::resolveTrack(id, &error);
		QVERIFY(track != nullptr);
		SampleClip* first = addTake(track, 0.5f);
		SampleClip* second = addTake(track, 0.25f);
		const int length = first->length().getTicks();

		TakeLanesPanel panel(id);
		QTest::mouseClick(panel.addLaneButton(), Qt::LeftButton);
		QTest::mouseClick(panel.addLaneButton(), Qt::LeftButton);
		panel.refresh();
		QVERIFY(panel.auditionButton(0) != nullptr && panel.auditionButton(1) != nullptr);
		QCOMPARE(panel.laneBox()->count(), 2);
		QVERIFY(run(QStringLiteral("comp.assign"), {{QStringLiteral("clip"), control::clipIdOf(first)},
			{QStringLiteral("lane"), 0}}).ok);
		QVERIFY(run(QStringLiteral("comp.assign"), {{QStringLiteral("clip"), control::clipIdOf(second)},
			{QStringLiteral("lane"), 1}}).ok);

		// Comp the first half from lane 0 and the second from lane 1 - through the panel.
		for (const auto& [from, to, lane] : {std::tuple{0, length / 2, 0}, std::tuple{length / 2, length, 1}})
		{
			panel.beginBox()->setValue(from);
			panel.endBox()->setValue(to);
			panel.laneBox()->setCurrentIndex(lane);
			QTest::mouseClick(panel.compButton(), Qt::LeftButton);
		}
		const QJsonArray segments = run(QStringLiteral("comp.get_state"), {{QStringLiteral("track"), id}})
			.result.value(QStringLiteral("composite")).toObject().value(QStringLiteral("segments")).toArray();
		QCOMPARE(segments.size(), 2);
		QVERIFY2(panel.compositeText().contains(QStringLiteral("lane 1")), qPrintable(panel.compositeText()));

		QTest::mouseClick(panel.auditionButton(1), Qt::LeftButton);
		QCOMPARE(track->takeLanes().auditionLane(), 1);
		QVERIFY(panel.auditionButton(1)->isChecked());
		QTest::mouseClick(panel.auditionButton(1), Qt::LeftButton);  // again: back to the comp
		QCOMPARE(track->takeLanes().auditionLane(), -1);

		QTest::mouseClick(panel.consolidateButton(), Qt::LeftButton);
		QVERIFY2(panel.statusText().contains(QStringLiteral("done")), qPrintable(panel.statusText()));
		QCOMPARE(static_cast<int>(track->getClips().size()), 1);
		for (QToolButton* button : panel.findChildren<QToolButton*>())
		{
			QVERIFY2(!button->property("controlCommand").toString().isEmpty(),
				qPrintable(QStringLiteral("an undeclared button: ") + button->text()));
		}
	}
};

QTEST_MAIN(TakeLanesPanelTest)
#include "TakeLanesPanelTest.moc"
