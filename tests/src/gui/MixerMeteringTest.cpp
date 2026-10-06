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
 *  the others) and the strips' meters move by them. Needs the real application (a Fader cannot be built
 *  without it). */

#include <QtTest>

#include <QJsonDocument>
#include <QSignalSpy>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Fader.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "MixerChannelView.h"
#include "MixerView.h"
#include "MixerViewOptions.h"

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
};

QTEST_MAIN(MixerMeteringTest)
#include "MixerMeteringTest.moc"
