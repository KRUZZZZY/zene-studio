/*
 * MixerMeteringTest.cpp - 0.5.0 L08 metering: the meters' ballistics, the Selected channel filter,
 *                         the live input meters and the stereo phase view
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

/*! MX-128: the meters' fall-off and peak hold are view options (window.mixer_view, kept in ui/mixerview with
 *  the others) and the strips' meters move by them. MX-005: a "Selected" filter shows only the channels picked
 *  for it - by window.mixer_view, a Ctrl+click on a strip or the mode bar's picker. MX-134: Large Meters shows
 *  the engine's input live, with the mixer's ballistics. Needs the real application (a Fader cannot be built
 *  without it). */

#include <QtTest>

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Fader.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InputLevelMeter.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "MixerChannelView.h"
#include "MixerModeBar.h"
#include "MixerView.h"
#include "MixerViewOptions.h"
#include "MixerWindows.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The view's half-second sync, now: a count change rebuilds the strips, the next pass applies the options.
void syncMixer()
{
	getGUI()->mixerView()->syncWithMixer();
	getGUI()->mixerView()->syncWithMixer();
}

//! One meter tick (the main window's 60 Hz periodicUpdate drives it).
void tick()
{
	QVERIFY(QMetaObject::invokeMethod(getGUI()->mixerView(), "updateFaders", Qt::DirectConnection));
}

QString ch(int index) { return control::channelIdOf(Engine::mixer()->mixerChannel(index)); }

QJsonArray selection() { return run(QStringLiteral("window.mixer_view")).result.value(QStringLiteral("selected_channels")).toArray(); }

QJsonObject savedOptions()
{
	return QJsonDocument::fromJson(ConfigManager::inst()->value(QStringLiteral("ui"), QStringLiteral("mixerview")).toUtf8()).object();
}

} // namespace

class MixerMeteringTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		ControlRegistry::setReady(true);
		getGUI()->mainWindow()->show();
		getGUI()->mainWindow()->revealPanel(QStringLiteral("mixer"));
		m_channel = Engine::mixer()->createChannel();
		syncMixer();
		QVERIFY(getGUI()->mixerView()->channelView(m_channel) != nullptr);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! MX-128: the defaults are the inherited ballistics - a 1.25 fall-off per tick, a 1.5 s hold.
	void theDefaultsAreTheInheritedBallistics()
	{
		const ControlResult state = run(QStringLiteral("window.mixer_view"));
		QVERIFY2(state.ok, qPrintable(state.errorMessage));
		QCOMPARE(state.result.value(QStringLiteral("meter_falloff")).toString(), QStringLiteral("fast"));
		QCOMPARE(state.result.value(QStringLiteral("peak_hold")).toString(), QStringLiteral("1.5s"));
		QCOMPARE(mixerviewoptions::meterFallOff(), 1.25f);
		syncMixer();
		QCOMPARE(fader()->peakHoldMs(), 1500);
	}

	//! MX-128: both are view options - set by window.mixer_view, answered, refused by a bad name, kept in ui/mixerview.
	void theBallisticsAreKeptWithTheViewOptions()
	{
		const ControlResult set = run(QStringLiteral("window.mixer_view"),
			{{QStringLiteral("meter_falloff"), QStringLiteral("slow")}, {QStringLiteral("peak_hold"), QStringLiteral("3s")}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(set.result.value(QStringLiteral("meter_falloff")).toString(), QStringLiteral("slow"));
		QCOMPARE(set.result.value(QStringLiteral("peak_hold")).toString(), QStringLiteral("3s"));
		QCOMPARE(savedOptions().value(QStringLiteral("meter_falloff")).toString(), QStringLiteral("slow"));
		QCOMPARE(savedOptions().value(QStringLiteral("peak_hold")).toString(), QStringLiteral("3s"));
		const ControlResult refused = run(QStringLiteral("window.mixer_view"), {{QStringLiteral("meter_falloff"), QStringLiteral("glacial")}});
		QVERIFY(!refused.ok);
		QCOMPARE(mixerviewoptions::options().meterFallOff, QStringLiteral("slow"));  // a refused call changes nothing
		syncMixer();
		QCOMPARE(fader()->peakHoldMs(), 3000);
	}

	//! MX-128: the strip's meter falls by the chosen factor per tick, in the peak meter and the pre-fader one.
	void theMetersFallByTheChosenFallOff_data()
	{
		QTest::addColumn<QString>("falloff");
		QTest::addColumn<QString>("mode");
		for (const char* mode : {"peak", "pre_fader"})
		{
			for (const char* falloff : {"fast", "medium", "slow"})
			{
				QTest::addRow("%s/%s", mode, falloff) << QString::fromLatin1(falloff) << QString::fromLatin1(mode);
			}
		}
	}

	void theMetersFallByTheChosenFallOff()
	{
		QFETCH(QString, falloff);
		QFETCH(QString, mode);
		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("meter_falloff"), falloff}, {QStringLiteral("meter_mode"), mode}}).ok);
		const float factor = mixerviewoptions::meterFallOff();
		QVERIFY(factor > 1.f);
		MixerChannel* channel = Engine::mixer()->mixerChannel(m_channel);
		feed(channel, mode, 1.f);
		tick();
		QCOMPARE(fader()->getPeak_L(), 1.f);
		feed(channel, mode, 0.f);
		tick();
		QVERIFY2(qAbs(fader()->getPeak_L() - 1.f / factor) < 1e-4f, qPrintable(QString::number(fader()->getPeak_L())));
		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("meter_mode"), QStringLiteral("peak")}}).ok);
	}

	//! MX-128: the three fall-offs differ, the slowest slowest.
	void theFallOffsAreOrdered()
	{
		const auto factorOf = [](const char* name) {
			[[maybe_unused]] const bool ok = run(QStringLiteral("window.mixer_view"), {{QStringLiteral("meter_falloff"), QLatin1String(name)}}).ok;
			return mixerviewoptions::meterFallOff();
		};
		const float fast = factorOf("fast");
		const float medium = factorOf("medium");
		const float slow = factorOf("slow");
		QVERIFY(fast > medium);
		QVERIFY(medium > slow);
	}

	//! MX-128: the hold line - held for the chosen time, or (off) following the level down at once.
	void thePeakHoldIsTheChosenTime()
	{
		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("peak_hold"), QStringLiteral("off")}}).ok);
		syncMixer();
		QCOMPARE(fader()->peakHoldMs(), 0);
		QCOMPARE(heldAfterADrop(), 0.5f);  // no hold: the line is the level
		for (const char* hold : {"1.5s", "3s", "6s"})
		{
			QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("peak_hold"), QLatin1String(hold)}}).ok);
			syncMixer();
			QVERIFY(fader()->peakHoldMs() >= 1500);
			QCOMPARE(heldAfterADrop(), 1.f);  // held
		}
	}

	//! MX-005: the Selected filter shows the master and the channels picked for it, nothing else.
	void theSelectedFilterShowsOnlyThePickedChannels()
	{
		m_other = Engine::mixer()->createChannel();
		syncMixer();
		const ControlResult set = run(QStringLiteral("window.mixer_view"),
			{{QStringLiteral("selected_channels"), QJsonArray{ch(m_channel)}}, {QStringLiteral("filter"), QStringLiteral("selected")}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(set.result.value(QStringLiteral("filter")).toString(), QStringLiteral("selected"));
		QCOMPARE(set.result.value(QStringLiteral("selected_channels")).toArray(), (QJsonArray{ch(m_channel)}));
		syncMixer();
		MixerView* view = getGUI()->mixerView();
		QVERIFY(view->channelView(0)->isVisibleTo(view));
		QVERIFY(view->channelView(m_channel)->isVisibleTo(view));
		QVERIFY(!view->channelView(m_other)->isVisibleTo(view));
		QVERIFY(mixerviewoptions::isSelected(m_channel));
		QVERIFY(!mixerviewoptions::isSelected(m_other));
	}

	//! MX-005: a channel that does not exist, or the master, is refused, typed, and the selection stays as it was.
	void anUnknownChannelIsRefused()
	{
		const ControlResult refused = run(QStringLiteral("window.mixer_view"), {{QStringLiteral("selected_channels"), QJsonArray{QStringLiteral("ch-9999")}}});
		QVERIFY(!refused.ok);
		QCOMPARE(refused.errorKind, ControlErrorKind::NotFound);  // the channel vocabulary's typed answer
		// The master always shows: it is never part of the selection.
		QCOMPARE(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("selected_channels"), QJsonArray{ch(0)}}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(selection(), (QJsonArray{ch(m_channel)}));
	}

	//! MX-005: the mode bar lists the filter and picks its channels through the command.
	void theModeBarOffersTheSelectedFilter()
	{
		QComboBox* filter = getGUI()->mixerView()->modeBar()->filterBox();
		const int row = filter->findData(QStringLiteral("selected"));
		QVERIFY(row >= 0);
		QCOMPARE(filter->itemText(row), QStringLiteral("Selected"));
		auto* pick = getGUI()->mixerView()->modeBar()->findChild<QPushButton*>(QStringLiteral("mixerModePickSelected"));
		QVERIFY(pick != nullptr);
		QCOMPARE(pick->property("controlCommand").toString(), QStringLiteral("window.mixer_view"));
		QVERIFY(!pick->accessibleName().isEmpty());
	}

	//! MX-005: Ctrl+click on a strip adds its channel to the selection and takes it out again; a plain click does not.
	void ctrlClickTogglesAStrip()
	{
		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("filter"), QStringLiteral("all")}}).ok);
		syncMixer();
		MixerChannelView* other = getGUI()->mixerView()->channelView(m_other);
		QTest::mouseClick(other, Qt::LeftButton, Qt::NoModifier, QPoint(4, 4));
		QCOMPARE(selection().size(), 1);
		QTest::mouseClick(other, Qt::LeftButton, Qt::ControlModifier, QPoint(4, 4));
		QCOMPARE(selection(), (QJsonArray{ch(m_channel), ch(m_other)}));
		QTest::mouseClick(other, Qt::LeftButton, Qt::ControlModifier, QPoint(4, 4));
		QCOMPARE(selection(), (QJsonArray{ch(m_channel)}));
	}

	//! MX-005: the selection follows a channel, not a position - another channel's removal keeps it.
	void theSelectionFollowsTheChannel()
	{
		const int before = Engine::mixer()->createChannel();
		Engine::mixer()->moveChannelLeft(before);  // now sits in front of m_other
		syncMixer();
		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("selected_channels"), QJsonArray{ch(m_other)}}}).ok);
		const QString id = ch(m_other);
		Engine::mixer()->deleteChannel(before);
		syncMixer();
		QCOMPARE(selection(), (QJsonArray{id}));
	}

	//! MX-134: the input meter takes each reading, falls by the mixer's fall-off and holds its peak line.
	void theInputMeterFollowsTheInput()
	{
		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("meter_falloff"), QStringLiteral("medium")},
			{QStringLiteral("peak_hold"), QStringLiteral("6s")}}).ok);
		InputLevelMeter meter(nullptr);
		QCOMPARE(meter.accessibleName(), QStringLiteral("Input level"));
		meter.tick(0.5f, 0.25f);
		QCOMPARE(meter.level(0), 0.5f);
		QCOMPARE(meter.level(1), 0.25f);
		meter.tick(0.f, 0.f);
		QVERIFY2(qAbs(meter.level(0) - 0.5f / mixerviewoptions::meterFallOff()) < 1e-5f, qPrintable(QString::number(meter.level(0))));
		QCOMPARE(meter.held(0), 0.5f);  // a 6 s hold
		QVERIFY2(meter.accessibleDescription().contains(QStringLiteral("dB")), qPrintable(meter.accessibleDescription()));
		QVERIFY(!meter.clipped());
		meter.tick(1.f, 0.f);
		QVERIFY(meter.clipped());
		QVERIFY(meter.accessibleDescription().contains(QStringLiteral("clipped")));
		QTest::mouseClick(&meter, Qt::LeftButton);  // a click clears the clip light
		QVERIFY(!meter.clipped());
		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("peak_hold"), QStringLiteral("off")}}).ok);
		meter.tick(0.f, 0.f);
		QCOMPARE(meter.held(0), meter.level(0));  // no hold: the line is the level
		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("meter_falloff"), QStringLiteral("fast")},
			{QStringLiteral("peak_hold"), QStringLiteral("1.5s")}}).ok);
	}

	//! MX-134: Large Meters shows the input beside the master.
	void largeMetersShowTheInput()
	{
		QMenu menu;
		addMixerWindows(&menu);
		QAction* open = nullptr;
		for (QAction* action : menu.actions())
		{
			if (action->text() == QStringLiteral("Large Meters...")) { open = action; }
		}
		QVERIFY(open != nullptr);
		open->trigger();
		QDialog* meters = nullptr;
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			auto* dialog = qobject_cast<QDialog*>(widget);
			if (dialog != nullptr && dialog->windowTitle() == QStringLiteral("Large Meters") && dialog->isVisible()) { meters = dialog; }
		}
		QVERIFY(meters != nullptr);
		QVERIFY(meters->findChild<InputLevelMeter*>() != nullptr);
		meters->close();
	}

	//! MX-005: a project cleared from under the mixer forgets the selection (the ids start again).
	void clearingTheMixerForgetsTheSelection()
	{
		getGUI()->mixerView()->clear();
		QVERIFY(selection().isEmpty());
	}

private:
	Fader* fader() const { return getGUI()->mixerView()->channelView(m_channel)->fader(); }

	//! What the meter reads, by the mode the strips show.
	static void feed(MixerChannel* channel, const QString& mode, float level)
	{
		if (mode == QLatin1String("pre_fader"))
		{
			channel->m_preMeterLeft.set(level);
			channel->m_preMeterRight.set(level);
			return;
		}
		channel->m_peakLeft = level;
		channel->m_peakRight = level;
	}

	//! The last held level the fader announced after a full-scale peak and a drop to half.
	float heldAfterADrop()
	{
		fader()->setPeak_L(0.f);
		fader()->setPeak_R(0.f);
		QSignalSpy spy(fader(), &Fader::peakChanged);
		fader()->setPeak_L(1.f);
		fader()->setPeak_L(0.5f);
		return spy.isEmpty() ? -1.f : spy.last().at(0).toFloat();
	}

	QTemporaryDir m_home;
	int m_channel = -1;
	int m_other = -1;
};

QTEST_MAIN(MixerMeteringTest)
#include "MixerMeteringTest.moc"
