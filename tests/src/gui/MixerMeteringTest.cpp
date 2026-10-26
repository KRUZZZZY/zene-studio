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
 *  the engine's input live, with the mixer's ballistics. MX-137: a goniometer (window.phase_scope) shows one
 *  channel's stereo picture beside its correlation, fed by a realtime-safe tap (PhaseScopeTapTest holds the
 *  tap). Needs the real application (a Fader cannot be built without it). */

#include <QtTest>

#include <vector>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QAccessible>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>

#include "AccessibleWidgets.h"
#include "AudioEngine.h"
#include "ConfigManager.h"
#include "ControlMeterSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Fader.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InputLevelMeter.h"
#include "InputMeter.h"
#include "MainWindow.h"
#include "MasterLoudnessTap.h"
#include "MeterSpeech.h"
#include "Mixer.h"
#include "MixerChannelView.h"
#include "MixerModeBar.h"
#include "MixerView.h"
#include "MixerViewOptions.h"
#include "MixerWindows.h"
#include "PhaseScopeTap.h"
#include "PhaseScopeView.h"

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

QStringList s_emitted;

//! Stands in for the platform bridge: the text a screen reader would be given for an announcement.
void recordAccessibilityEvent(QAccessibleEvent* event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
	if (event->type() == QAccessible::Announcement)
	{
		s_emitted << static_cast<QAccessibleAnnouncementEvent*>(event)->message();
	}
#else
	if (event->type() == QAccessible::Alert && event->accessibleInterface() != nullptr)
	{
		s_emitted << event->accessibleInterface()->text(QAccessible::Name);
	}
#endif
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

	//! Q050-100: Speak Meter reports the focused channel's post-fader peak and the
	//! master's LUFS-S, and does not arm the tap. The action and its Ctrl+Shift+M
	//! shortcut dispatch the same registered command.
	void speakMeterReportsTheFocusedChannelAndMasterLufs()
	{
		MasterLoudnessTap* tap = Engine::audioEngine()->masterLoudness();
		QVERIFY(tap != nullptr);
		tap->setEnabled(true);
		const sample_rate_t rate = tap->sampleRate();
		std::vector<SampleFrame> signal(static_cast<std::size_t>(rate * 3) + 4096);
		for (std::size_t i = 0; i < signal.size(); ++i)
		{
			const auto value = static_cast<sample_t>(0.1 * std::sin(2.0 * M_PI * 1000.0 * static_cast<double>(i) / rate));
			signal[i] = SampleFrame(value, value);
		}
		for (std::size_t at = 0; at < signal.size(); at += 512)
		{
			tap->feed(signal.data() + at, static_cast<f_cnt_t>(std::min<std::size_t>(512, signal.size() - at)));
		}
		const double expectedLufs = tap->snapshot().shortTermLufs;
		QVERIFY(std::isfinite(expectedLufs));
		MixerChannel* channel = Engine::mixer()->mixerChannel(m_channel);
		QVERIFY(channel != nullptr);
		AudioDevice* device = Engine::audioEngine()->audioDev();
		QVERIFY(device != nullptr);
		device->stopProcessing();
		// A real controlled signal through mixer processing, so the published post-fader peak is the mixer's own.
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> input(fpp, SampleFrame(0.5f, 0.25f));
		std::vector<SampleFrame> masterOut(fpp);
		SampleFrame* busData[1] = {input.data()};
		for (int period = 0; period < 2; ++period)
		{
			Engine::mixer()->mixToChannel(AudioBus{busData, 1, fpp}, static_cast<mix_ch_t>(m_channel));
			Engine::mixer()->prepareMasterMix();
			zeroSampleFrames(masterOut.data(), fpp);
			Engine::mixer()->masterMix(masterOut.data());
			AutomatableModel::incrementPeriodCounter();
		}
		getGUI()->mixerView()->setCurrentMixerChannel(m_channel);

		QVERIFY(run(QStringLiteral("view.announcements"), {{QStringLiteral("level"), QStringLiteral("all")}}).ok);
		const ControlResult spoken = run(QStringLiteral("meter.speak"), {{QStringLiteral("channel"), ch(m_channel)}});
		QVERIFY2(spoken.ok, qPrintable(spoken.errorMessage));
		QVERIFY(std::fabs(spoken.result.value(QStringLiteral("peak_db")).toDouble() - (-6.0206)) < 0.01);
		QVERIFY(spoken.result.value(QStringLiteral("master_lufs_available")).toBool());
		QVERIFY(std::fabs(spoken.result.value(QStringLiteral("master_lufs")).toDouble() - expectedLufs) < 0.001);
		QVERIFY(spoken.result.value(QStringLiteral("spoken")).toString().contains(QStringLiteral("Master short-term loudness")));
		QCOMPARE(a11y::lastAnnouncement(), spoken.result.value(QStringLiteral("spoken")).toString());
		QVERIFY(tap->enabled());  // a report never arms or resets the tap

		// The disarmed case says why, and reports null rather than a plausible zero.
		tap->setEnabled(false);
		const ControlResult unavailable = run(QStringLiteral("meter.speak"), {{QStringLiteral("channel"), ch(m_channel)}});
		QVERIFY(unavailable.ok);
		QVERIFY(!unavailable.result.value(QStringLiteral("master_lufs_available")).toBool());
		QVERIFY(unavailable.result.value(QStringLiteral("master_lufs")).isNull());
		QVERIFY(unavailable.result.value(QStringLiteral("spoken")).toString().contains(QStringLiteral("not armed")));

		// The strip's action and the shortcut both go through the real GUI control.
		QAction* action = getGUI()->mixerView()->speakMeterAction();
		QVERIFY(action != nullptr);
		QCOMPARE(action->shortcut(), QKeySequence(QStringLiteral("Ctrl+Shift+M")));
		QCOMPARE(action->property("controlCommand").toString(), QStringLiteral("meter.speak"));
		getGUI()->mainWindow()->revealPanel(QStringLiteral("mixer"));
		getGUI()->mainWindow()->show();
		QApplication::setActiveWindow(getGUI()->mainWindow());
		getGUI()->mixerView()->channelView(m_channel)->setFocus();
		QTest::qWait(50);
		QSignalSpy triggered(action, &QAction::triggered);
		QTest::keySequence(getGUI()->mixerView()->channelView(m_channel), action->shortcut());
		QTRY_COMPARE(triggered.count(), 1);
		action->trigger();
		QTRY_COMPARE(triggered.count(), 2);
		device->startProcessing();
		tap->setEnabled(false);
	}

	//! Q050-100: one announcement per rising crossing for input and master; staying
	//! above says nothing, falling below re-arms, and view.announcements gates it.
	void peakWatchAnnouncesOncePerCrossingAndHonoursTheLevel()
	{
		QVERIFY(run(QStringLiteral("view.announcements"), {{QStringLiteral("level"), QStringLiteral("all")}}).ok);
		QVERIFY(run(QStringLiteral("meter.set_peak_watch"), {{QStringLiteral("enabled"), true},
			{QStringLiteral("threshold_dbfs"), -6}}).ok);
		meterspeech::reset();
		a11y::announceStatus(getGUI()->mainWindow(), QStringLiteral("peak watch marker zero"));
		a11y::announceStatus(getGUI()->mainWindow(), QStringLiteral("peak watch marker one"));
		const QString marker = a11y::lastAnnouncement();
		meterspeech::observe(0.25f, 0.0f);  // below -12 dBFS
		QCOMPARE(a11y::lastAnnouncement(), marker);
		meterspeech::observe(0.6f, 0.0f);  // input rises through -6
		QVERIFY2(a11y::lastAnnouncement().startsWith(QStringLiteral("Input peak")), qPrintable(a11y::lastAnnouncement()));
		const QString firstCrossing = a11y::lastAnnouncement();
		meterspeech::observe(0.7f, 0.0f);  // still above: no second word
		QCOMPARE(a11y::lastAnnouncement(), firstCrossing);
		meterspeech::observe(0.0f, 0.0f);  // falls below: re-arms
		meterspeech::observe(0.0f, 0.6f);  // master rises
		QVERIFY2(a11y::lastAnnouncement().startsWith(QStringLiteral("Master peak")), qPrintable(a11y::lastAnnouncement()));
		const QString masterCrossing = a11y::lastAnnouncement();
		meterspeech::observe(0.0f, 0.7f);
		QCOMPARE(a11y::lastAnnouncement(), masterCrossing);

		// The strip menu sets the same preference through meter.set_peak_watch.
		QMenu watch;
		getGUI()->mixerView()->populatePeakWatchMenu(&watch);
		QAction* minus12 = watch.findChild<QAction*>(QStringLiteral("peakWatchMinus12"));
		QVERIFY(minus12 != nullptr);
		minus12->trigger();
		QVERIFY(control::meterPeakWatchEnabled());
		QCOMPARE(control::meterPeakWatchThresholdDbfs(), -12.0);
		QAction* off = watch.findChild<QAction*>(QStringLiteral("peakWatchOff"));
		QVERIFY(off != nullptr);
		off->trigger();
		QVERIFY(!control::meterPeakWatchEnabled());

		// Put the watch back for the level checks below.
		QVERIFY(run(QStringLiteral("meter.set_peak_watch"), {{QStringLiteral("enabled"), true},
			{QStringLiteral("threshold_dbfs"), -6}}).ok);

		// errors: an ordinary peak crossing is suppressed; off: nothing is spoken.
		QVERIFY(run(QStringLiteral("view.announcements"), {{QStringLiteral("level"), QStringLiteral("errors")}}).ok);
		meterspeech::reset();
		a11y::announceStatus(getGUI()->mainWindow(), QStringLiteral("peak watch marker two"));
		const QString errorsMarker = a11y::lastAnnouncement();
		meterspeech::observe(0.8f, 0.8f);
		QCOMPARE(a11y::lastAnnouncement(), errorsMarker);
		QVERIFY(!meterspeech::lastMessage().isEmpty());  // the crossing was evaluated, just not spoken
		QVERIFY(run(QStringLiteral("view.announcements"), {{QStringLiteral("level"), QStringLiteral("off")}}).ok);
		meterspeech::reset();
		a11y::announceStatus(getGUI()->mainWindow(), QStringLiteral("peak watch marker three"));
		const QString offMarker = a11y::lastAnnouncement();
		meterspeech::observe(0.9f, 0.9f);
		QCOMPARE(a11y::lastAnnouncement(), offMarker);

		// Threshold changes are one undo step and restore both values.
		QVERIFY(run(QStringLiteral("view.announcements"), {{QStringLiteral("level"), QStringLiteral("all")}}).ok);
		const ControlResult set = run(QStringLiteral("meter.set_peak_watch"), {{QStringLiteral("enabled"), true},
			{QStringLiteral("threshold_dbfs"), -3}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(set.result.value(QStringLiteral("threshold_dbfs")).toDouble(), -3.0);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		const ControlResult afterUndo = run(QStringLiteral("meter.set_peak_watch"));
		QVERIFY(afterUndo.ok);
		QCOMPARE(afterUndo.result.value(QStringLiteral("threshold_dbfs")).toDouble(), -6.0);  // the value before the set
		QVERIFY(run(QStringLiteral("meter.set_peak_watch"), {{QStringLiteral("enabled"), false},
			{QStringLiteral("threshold_dbfs"), -6}}).ok);
	}

	//! Q050-100: the REAL master tap drives the watch, not only the observe() seam: a loud period
	//! crosses, a quiet one lets the latch fall, and a later loud period crosses again. This is the
	//! property that a held-maximum sample peak would fail.
	void theRealMasterTapReArmsTheWatch()
	{
		MasterLoudnessTap* tap = Engine::audioEngine()->masterLoudness();
		QVERIFY(tap != nullptr);
		AudioDevice* device = Engine::audioEngine()->audioDev();
		QVERIFY(device != nullptr);
		device->stopProcessing();
		QVERIFY(run(QStringLiteral("view.announcements"), {{QStringLiteral("level"), QStringLiteral("all")}}).ok);
		QVERIFY(run(QStringLiteral("meter.set_peak_watch"), {{QStringLiteral("enabled"), true},
			{QStringLiteral("threshold_dbfs"), -6}}).ok);
		tap->setEnabled(false);
		tap->reset();
		float left = 0.f;
		float right = 0.f;
		inputmeter::watch(&left, &right);  // drain any input peak from the engine
		meterspeech::reset();
		const auto feed = [tap](float peak) {
			std::vector<SampleFrame> block(512);
			for (std::size_t i = 0; i < block.size(); ++i)
			{
				const auto value = static_cast<sample_t>(i % 2 == 0 ? peak : -peak);
				block[i] = SampleFrame(value, value);
			}
			tap->feed(block.data(), static_cast<f_cnt_t>(block.size()));
			meterspeech::poll();
		};
		feed(0.6f);
		QVERIFY2(a11y::lastAnnouncement().startsWith(QStringLiteral("Master peak")), qPrintable(a11y::lastAnnouncement()));
		const QString first = a11y::lastAnnouncement();
		feed(0.0f);  // below: re-arms, says nothing
		QCOMPARE(a11y::lastAnnouncement(), first);
		const int spokenBefore = a11y::announcementCount();
		feed(0.6f);  // the SAME peak, well inside the repeat window: a new crossing is still spoken
		QCOMPARE(a11y::lastAnnouncement(), first);
		QCOMPARE(a11y::announcementCount(), spokenBefore + 1);
		feed(0.6f);  // still above: no further word
		QCOMPARE(a11y::announcementCount(), spokenBefore + 1);
		QVERIFY(run(QStringLiteral("meter.set_peak_watch"), {{QStringLiteral("enabled"), false},
			{QStringLiteral("threshold_dbfs"), -6}}).ok);
		device->startProcessing();
	}

	//! Q050-100: the policy's repeat window drops a re-sent status but not an explicit event.
	void aRepeatCanBeAllowedPastTheDebounce()
	{
		QVERIFY(run(QStringLiteral("view.announcements"), {{QStringLiteral("level"), QStringLiteral("all")}}).ok);
		const QString text = QStringLiteral("repeat policy marker");
		const int before = a11y::announcementCount();
		QVERIFY(a11y::shouldAnnounce(text, a11y::Politeness::Polite));
		QVERIFY(!a11y::shouldAnnounce(text, a11y::Politeness::Polite));
		QVERIFY(a11y::shouldAnnounce(text, a11y::Politeness::Polite, a11y::Repeat::Allow));
		QCOMPARE(a11y::announcementCount(), before + 2);
	}

	//! Q050-100/Q050-320: on Qt before 6.8 the alert is raised on a source whose accessible name is the message,
	//! so the screen reader reads the text; a source that already shows the message is used as it is.
	void theAlertSourceCarriesTheMessage()
	{
		getGUI()->mainWindow()->show();
		QApplication::setActiveWindow(getGUI()->mainWindow());
		const QString message = QStringLiteral("Input peak -3.0 dBFS crossed the -6.0 dBFS threshold.");
		QObject* source = a11y::announcementSource(QCoreApplication::instance(), message);
		QVERIFY(source != nullptr);
		QVERIFY(source != QCoreApplication::instance());
		QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(source);
		QVERIFY(iface != nullptr);
		QCOMPARE(iface->text(QAccessible::Name), message);
		// The next message re-names the same region.
		const QString next = QStringLiteral("Master peak -2.0 dBFS crossed the -6.0 dBFS threshold.");
		QCOMPARE(a11y::announcementSource(QCoreApplication::instance(), next), source);
		QCOMPARE(QAccessible::queryAccessibleInterface(source)->text(QAccessible::Name), next);
		// A label that already shows the message is its own source.
		QLabel shown(next);
		QCOMPARE(a11y::announcementSource(&shown, next), static_cast<QObject*>(&shown));
	}

	//! Q050-100: the Speak Meter shortcut speaks the strip that has keyboard focus, even when a different
	//! strip is the selected one (focus moves by keyboard, only a mouse press selects).
	void theShortcutSpeaksTheFocusedStrip()
	{
		QVERIFY(run(QStringLiteral("view.announcements"), {{QStringLiteral("level"), QStringLiteral("all")}}).ok);
		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("filter"), QStringLiteral("all")}}).ok);
		syncMixer();
		MixerView* view = getGUI()->mixerView();
		getGUI()->mainWindow()->revealPanel(QStringLiteral("mixer"));
		getGUI()->mainWindow()->show();
		QApplication::setActiveWindow(getGUI()->mainWindow());
		view->setCurrentMixerChannel(m_channel);
		MixerChannelView* focused = view->channelView(m_other);
		focused->setFocus();
		QTest::qWait(50);
		QCOMPARE(view->currentMixerChannel(), view->channelView(m_channel));
		QCOMPARE(view->focusedChannelView(), focused);
		a11y::announceStatus(getGUI()->mainWindow(), QStringLiteral("focused strip marker"));
		QTest::keySequence(focused, view->speakMeterAction()->shortcut());
		const QString expected = QStringLiteral("Channel %1 peak").arg(Engine::mixer()->mixerChannel(m_other)->m_name);
		QTRY_VERIFY2(a11y::lastAnnouncement().startsWith(expected), qPrintable(a11y::lastAnnouncement()));
	}

	//! Q050-100: the live input handoff. Audio-thread measure() -> the poller's read of the watch pair ->
	//! one announcement per rising crossing; held above says nothing; a quiet period re-arms.
	void theLiveInputPeakReachesThePollerAndReArms()
	{
		AudioDevice* device = Engine::audioEngine()->audioDev();
		QVERIFY(device != nullptr);
		device->stopProcessing();
		QVERIFY(run(QStringLiteral("view.announcements"), {{QStringLiteral("level"), QStringLiteral("all")}}).ok);
		QVERIFY(run(QStringLiteral("meter.set_peak_watch"), {{QStringLiteral("enabled"), true},
			{QStringLiteral("threshold_dbfs"), -6}}).ok);
		Engine::audioEngine()->masterLoudness()->setEnabled(false);
		meterspeech::poll();  // drain whatever the engine measured before
		meterspeech::reset();
		const auto measure = [](float peak) {
			std::vector<SampleFrame> block(256, SampleFrame(peak, 0.f));
			inputmeter::measure(block.data(), static_cast<f_cnt_t>(block.size()));
			meterspeech::poll();
		};
		const int before = a11y::announcementCount();
		measure(0.25f);  // below -6 dBFS
		QCOMPARE(a11y::announcementCount(), before);
		measure(0.6f);  // rises through -6
		QCOMPARE(a11y::announcementCount(), before + 1);
		QVERIFY2(a11y::lastAnnouncement().startsWith(QStringLiteral("Input peak")), qPrintable(a11y::lastAnnouncement()));
		measure(0.7f);  // held above
		QCOMPARE(a11y::announcementCount(), before + 1);
		meterspeech::poll();  // a quiet period: the latch falls
		measure(0.6f);  // rises again, same text as the first crossing
		QCOMPARE(a11y::announcementCount(), before + 2);
		QVERIFY(run(QStringLiteral("meter.set_peak_watch"), {{QStringLiteral("enabled"), false},
			{QStringLiteral("threshold_dbfs"), -6}}).ok);
		device->startProcessing();
	}

	//! Q050-100/Q050-320: the dynamic message reaches Qt's accessibility layer. A test update handler stands in
	//! for the platform bridge and records what a screen reader would be given.
	void theAnnouncementIsEmittedToTheAccessibilityLayer()
	{
		QVERIFY(run(QStringLiteral("view.announcements"), {{QStringLiteral("level"), QStringLiteral("all")}}).ok);
		getGUI()->mainWindow()->show();
		QApplication::setActiveWindow(getGUI()->mainWindow());
		const bool wasActive = QAccessible::isActive();
		QAccessible::setActive(true);
		s_emitted.clear();
		const QAccessible::UpdateHandler previous = QAccessible::installUpdateHandler(&recordAccessibilityEvent);
		const QString message = QStringLiteral("Master peak -3.0 dBFS crossed the -6.0 dBFS threshold.");
		a11y::announceStatus(QCoreApplication::instance(), message, a11y::Politeness::Polite, a11y::Repeat::Allow);
		QAccessible::installUpdateHandler(previous);
		QAccessible::setActive(wasActive);
		QCOMPARE(s_emitted, QStringList{message});
	}

	//! MX-137: the goniometer turns the stereo picture 45 degrees - mono stands up the middle, out of phase lies flat.
	void theGoniometerShowsMonoUprightAndOutOfPhaseFlat()
	{
		PhaseScopeView view(nullptr);
		const std::vector<float> left{0.5f, -0.25f, 0.1f};
		view.setFrames(left.data(), left.data(), left.size());
		QCOMPARE(view.pointCount(), left.size());
		for (std::size_t i = 0; i < left.size(); ++i)
		{
			QVERIFY(qAbs(view.point(i).x()) < 1e-6);
			QVERIFY(qAbs(view.point(i).y()) > 0.01);
		}
		const std::vector<float> inverted{-0.5f, 0.25f, -0.1f};
		view.setFrames(left.data(), inverted.data(), left.size());
		for (std::size_t i = 0; i < left.size(); ++i)
		{
			QVERIFY(qAbs(view.point(i).y()) < 1e-6);
			QVERIFY(qAbs(view.point(i).x()) > 0.01);
		}
		// Left only leans one way, right only the other.
		const std::vector<float> silent(left.size(), 0.f);
		view.setFrames(left.data(), silent.data(), 1);
		const double leftLean = view.point(0).x();
		view.setFrames(silent.data(), left.data(), 1);
		QVERIFY(leftLean * view.point(0).x() < 0.0);
	}

	//! MX-137: window.phase_scope opens the view on a channel and points the tap at it; closing it lets go.
	void thePhaseScopeListensToTheChosenChannel()
	{
		const ControlResult opened = run(QStringLiteral("window.phase_scope"), {{QStringLiteral("channel"), ch(m_channel)}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		QVERIFY(opened.result.value(QStringLiteral("shown")).toBool());
		QCOMPARE(opened.result.value(QStringLiteral("channel")).toString(), ch(m_channel));
		QVERIFY(opened.result.value(QStringLiteral("correlation")).isDouble());
		QVERIFY(opened.result.value(QStringLiteral("frames_seen")).isDouble());
		QCOMPARE(PhaseScopeTap::instance().channel(), m_channel);
		auto* window = getGUI()->mainWindow()->findChild<PhaseScopeWindow*>();
		QVERIFY(window != nullptr);
		QCOMPARE(window->channel(), m_channel);
		QCOMPARE(run(QStringLiteral("window.phase_scope"), {{QStringLiteral("channel"), QStringLiteral("ch-9999")}}).errorKind,
			ControlErrorKind::NotFound);
		QCOMPARE(window->channel(), m_channel);  // a refused call moves nothing
		// The master, by default; the real mixer feeds the tap from its workers.
		const ControlResult master = run(QStringLiteral("window.phase_scope"), {{QStringLiteral("channel"), ch(0)}});
		QVERIFY(master.ok);
		QCOMPARE(PhaseScopeTap::instance().channel(), 0);
		QTRY_VERIFY_WITH_TIMEOUT(PhaseScopeTap::instance().written() > 0, 5000);
		// Asked again (no channel: the open one's), it keeps its picture and its count.
		QVERIFY(run(QStringLiteral("window.phase_scope")).result.value(QStringLiteral("frames_seen")).toDouble() > 0);
		QVERIFY(!window->refresh().isEmpty());
		delete window;
		QCOMPARE(PhaseScopeTap::instance().channel(), PhaseScopeTap::Off);  // nothing left to look at
	}

	//! MX-137: the Mixer View menu opens it, beside Large Meters, declaring its command.
	void theMixerMenuOffersThePhaseScope()
	{
		QMenu menu;
		addMixerWindows(&menu);
		QAction* scope = nullptr;
		for (QAction* action : menu.actions())
		{
			if (action->text() == QStringLiteral("Phase Scope...")) { scope = action; }
		}
		QVERIFY(scope != nullptr);
		QCOMPARE(scope->property("controlCommand").toString(), QStringLiteral("window.phase_scope"));
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
