/*
 * MixerStripFeaturesTest.cpp - the strip's Console section, its own meter mode, the delay offsets and the input meters, driven from the interface
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

/*! The interface halves of four 0.5.0 units, each through the controls a user touches:
 *  - Q050-088: the mixer view's "Show Console Section" menu choice shows the strip's Console section (kept in
 *    ui/mixerview, restored from there); its knobs and switches edit the channel through mixer.console_strip -
 *    a drag one undo step - and the section follows an agent's edit;
 *  - Q050-108: the strip menu's "Meter Mode" sets that strip's meter alone; the strip's bars and meter line show
 *    the VU or loudness reading;
 *  - Q050-339: the track and channel menus' delay entries show the stored offset and set it through their form;
 *  - Q050-340: the Input Meters window (the mixer view's menu) draws a meter per capture channel and per armed
 *    track, and window.input_meters answers what it shows.
 *  Needs the real application; loads the build's console module for the Console's own controls. */

#include <QtTest>

#include <QJsonArray>
#include <QLabel>
#include <QJsonDocument>
#include <QMenu>
#include <QTemporaryDir>
#include <QToolButton>

#include <array>
#include <cmath>
#include <vector>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "CommandForm.h"
#include "ConfigManager.h"
#include "ConsoleStripSection.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Fader.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InputChannelMeters.h"
#include "InputMeter.h"
#include "InputReadings.h"
#include "Knob.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "MixerChannelView.h"
#include "MixerStrip.h"
#include "MixerView.h"
#include "MixerViewOptions.h"
#include "MultiTrackRecorder.h"
#include "SampleTrack.h"
#include "ShellCommandMenus.h"
#include "Song.h"
#include "StripMeters.h"
#include "Track.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString ch(int index) { return control::channelIdOf(Engine::mixer()->mixerChannel(index)); }

void syncMixer()
{
	getGUI()->mixerView()->syncWithMixer();
	getGUI()->mixerView()->syncWithMixer();
}

MixerChannelView* strip(int index) { return getGUI()->mixerView()->channelView(index); }

int undoDepth() { return run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(); }

//! The action whose text is @a text under the submenus @a path of @a menu (each matched by its title), or nullptr.
QAction* findAction(QMenu* menu, const QStringList& path, const QString& text)
{
	QMenu* at = menu;
	for (const QString& title : path)
	{
		QMenu* next = nullptr;
		for (QAction* action : at->actions())
		{
			if (action->menu() != nullptr && action->text().remove(QLatin1Char('&')) == title) { next = action->menu(); }
		}
		if (next == nullptr) { return nullptr; }
		at = next;
	}
	for (QAction* action : at->actions())
	{
		if (action->text().remove(QLatin1Char('&')).startsWith(text)) { return action; }
	}
	return nullptr;
}

QString consoleOn(int index)
{
	for (const QJsonValue& device : run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")}})
		.result.value(QStringLiteral("devices")).toArray())
	{
		if (device.toObject().value(QStringLiteral("name")).toString() != QLatin1String("console")) { continue; }
		return run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), ch(index)},
			{QStringLiteral("device"), device.toObject().value(QStringLiteral("id")).toString()}}).result.value(QStringLiteral("id")).toString();
	}
	return QString();
}

QJsonObject consoleStrip(int index)
{
	return run(QStringLiteral("mixer.console_strip"), {{QStringLiteral("channel"), ch(index)}}).result;
}

class FakeCaptureDevice : public AudioDevice
{
public:
	explicit FakeCaptureDevice(AudioEngine* engine) : AudioDevice(DEFAULT_CHANNELS, engine) {}
	void prepare(f_cnt_t frames, int left, int right) { prepareCapture(frames, left, right); }
	void publish(const std::vector<float>& block, int channels)
	{
		publishCaptured(block.data(), channels, static_cast<f_cnt_t>(block.size() / static_cast<std::size_t>(channels)));
	}

private:
	void startProcessingImpl() override {}
	void stopProcessingImpl() override {}
};

} // namespace

class MixerStripFeaturesTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		ControlRegistry::setReady(true);
		getGUI()->mainWindow()->show();
		getGUI()->mainWindow()->revealPanel(QStringLiteral("mixer"));
		while (Engine::mixer()->numChannels() < 3) { Engine::mixer()->createChannel(); }
		syncMixer();
		QVERIFY(strip(1) != nullptr && strip(2) != nullptr);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Q050-088: the mixer view menu's "Show Console Section" shows the section on every strip and hides it again;
	//! the choice is kept in ui/mixerview and a fresh read of that setting restores it.
	void theConsoleSectionFollowsTheViewMenu()
	{
		QCOMPARE(mixerviewoptions::options().console, false);  // optional: off by default
		syncMixer();
		QVERIFY(!strip(1)->consoleSection()->isVisible());
		QMenu menu(strip(1));
		ShellCommandMenus::populateChannel(&menu, ch(1));
		QAction* on = findAction(&menu, {QStringLiteral("Mixer View"), QStringLiteral("Show Console Section")}, QStringLiteral("On"));
		QVERIFY(on != nullptr);
		on->trigger();
		syncMixer();
		QVERIFY(strip(1)->consoleSection()->isVisible());
		QVERIFY(strip(2)->consoleSection()->isVisible());
		QCOMPARE(run(QStringLiteral("window.mixer_view")).result.value(QStringLiteral("show_console")).toBool(), true);
		const QJsonObject saved = QJsonDocument::fromJson(ConfigManager::inst()->value(QStringLiteral("ui"), QStringLiteral("mixerview")).toUtf8()).object();
		QCOMPARE(saved.value(QStringLiteral("show_console")).toBool(), true);
		mixerviewoptions::Options restored;  // what options() builds from the setting at the next start
		QString error;
		QVERIFY(mixerviewoptions::merge(saved, &restored, &error));
		QCOMPARE(restored.console, true);
		// The compact density leaves it out, as it does the inserts.
		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("density"), QStringLiteral("compact")}}).ok);
		syncMixer();
		QVERIFY(!strip(1)->consoleSection()->isVisible());
		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("density"), QStringLiteral("expanded")}}).ok);
		syncMixer();
		QVERIFY(strip(1)->consoleSection()->isVisible());
	}

	//! Q050-088: the section's controls edit their own channel through mixer.console_strip: a trim drag is one undo
	//! step, the polarity switch flips the side, an agent's edit shows on the strip; without a Console its five
	//! controls are disabled; with one, the glue switch and the drive knob reach that Console.
	void theConsoleSectionEditsItsChannel()
	{
		QVERIFY(run(QStringLiteral("window.mixer_view"), {{QStringLiteral("show_console"), true}}).ok);
		syncMixer();
		ConsoleStripSection* section = strip(1)->consoleSection();
		auto* trim = qobject_cast<Knob*>(section->control(QStringLiteral("trim_db")));
		QVERIFY(trim != nullptr);
		QCOMPARE(trim->property("controlCommand").toString(), QStringLiteral("mixer.console_strip"));
		QVERIFY(run(QStringLiteral("control.set_undo_coalescing"), {{QStringLiteral("window_ms"), 400}}).ok);
		const int depth = undoDepth();
		for (const float db : {1.f, 2.5f, 4.f}) { trim->model()->setValue(db); }  // a drag
		QCOMPARE(Engine::mixer()->mixerChannel(1)->m_trimModel.value(), 4.f);
		QCOMPARE(Engine::mixer()->mixerChannel(2)->m_trimModel.value(), 0.f);
		QCOMPARE(undoDepth(), depth + 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(Engine::mixer()->mixerChannel(1)->m_trimModel.value(), 0.f);
		QVERIFY(run(QStringLiteral("control.set_undo_coalescing"), {{QStringLiteral("window_ms"), 0}}).ok);
		auto* polarity = qobject_cast<QToolButton*>(section->control(QStringLiteral("polarity_right")));
		QVERIFY(polarity != nullptr);
		QTest::mouseClick(polarity, Qt::LeftButton);
		QVERIFY(Engine::mixer()->mixerChannel(1)->m_polarityRightModel.value());
		QVERIFY(!Engine::mixer()->mixerChannel(1)->m_polarityLeftModel.value());
		QVERIFY(run(QStringLiteral("mixer.console_strip"), {{QStringLiteral("channel"), ch(1)}, {QStringLiteral("eq_low_db"), -5.0}}).ok);
		syncMixer();
		QCOMPARE(qobject_cast<Knob*>(section->control(QStringLiteral("eq_low_db")))->model()->value(), -5.f);
		QVERIFY(!section->control(QStringLiteral("glue"))->isEnabled());  // no Console on the channel yet
		if (consoleOn(1).isEmpty()) { QSKIP("this build has no console module"); }
		syncMixer();
		auto* glue = qobject_cast<QToolButton*>(section->control(QStringLiteral("glue")));
		QVERIFY(glue->isEnabled());
		QTest::mouseClick(glue, Qt::LeftButton);
		QCOMPARE(consoleStrip(1).value(QStringLiteral("glue")).toBool(), true);
		qobject_cast<Knob*>(section->control(QStringLiteral("drive")))->model()->setValue(64.f);
		QCOMPARE(consoleStrip(1).value(QStringLiteral("drive")).toDouble(), 64.0);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		syncMixer();
		QCOMPARE(qobject_cast<Knob*>(section->control(QStringLiteral("drive")))->model()->value(), 30.f);  // the strip follows
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		syncMixer();
		QVERIFY(!glue->isChecked());
	}

	//! Q050-108: the strip menu's "Meter Mode" sets this strip's meter alone; in VU its bars and meter line show the
	//! needle, in LUFS the loudness.
	void theStripMenuSetsThatStripsMeterMode()
	{
		QMenu menu(strip(1));
		ShellCommandMenus::populateChannel(&menu, ch(1));
		QAction* vu = findAction(&menu, {QStringLiteral("Strip"), QStringLiteral("Meter Mode")}, QStringLiteral("vu"));
		QVERIFY(vu != nullptr);
		vu->trigger();
		QCOMPARE(stripmeters::mode(*Engine::mixer()->mixerChannel(1)), static_cast<int>(stripmeters::Mode::Vu));
		QCOMPARE(stripmeters::mode(*Engine::mixer()->mixerChannel(2)), 0);
		QCOMPARE(mixerviewoptions::meterModeOf(Engine::mixer()->mixerChannel(1)), QStringLiteral("vu"));
		QCOMPARE(mixerviewoptions::meterModeOf(Engine::mixer()->mixerChannel(2)), mixerviewoptions::options().meterMode);
		// A steady -18 dBFS sine (the reference) for a second: the needle settles at 0 VU, the bar at its level.
		MixerChannel* channel = Engine::mixer()->mixerChannel(1);
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> block(static_cast<std::size_t>(fpp));
		const double rate = Engine::audioEngine()->outputSampleRate();
		const float level = std::pow(10.f, -18.f / 20.f);
		for (long at = 0; at < static_cast<long>(rate);)
		{
			for (SampleFrame& frame : block)
			{
				const auto x = static_cast<float>(level * std::sin(6.283185307179586 * 1000.0 * static_cast<double>(at++) / rate));
				frame = SampleFrame(x, x);
			}
			mixerstrip::updateMeters(*channel, block.data(), 1.f, fpp);
		}
		QVERIFY(mixerviewoptions::feedMeter(strip(1)->fader(), channel));
		QVERIFY(std::abs(20.f * std::log10(strip(1)->fader()->getPeak_L()) + 18.f) < 0.2f);
		syncMixer();
		QVERIFY2(strip(1)->meterInfo()->text().contains(QStringLiteral("VU 0.0")) || strip(1)->meterInfo()->text().contains(QStringLiteral("VU -0.0")),
			qPrintable(strip(1)->meterInfo()->text()));
		QAction* lufs = findAction(&menu, {QStringLiteral("Strip"), QStringLiteral("Meter Mode")}, QStringLiteral("lufs"));
		QVERIFY(lufs != nullptr);
		lufs->trigger();
		QCOMPARE(run(QStringLiteral("mixer.strip_get"), {{QStringLiteral("channel"), ch(1)}}).result.value(QStringLiteral("meter_mode")).toString(),
			QStringLiteral("lufs"));
		syncMixer();
		QVERIFY(strip(1)->meterInfo()->text().contains(QStringLiteral("LUFS")));
	}

	//! Q050-339: the track and channel menus show the stored delay in their entry and set it through their form.
	void theDelayEntriesShowAndSetTheOffset()
	{
		const QString track = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}}).result.value(QStringLiteral("track")).toString();
		Track* added = nullptr;
		for (Track* candidate : Engine::getSong()->tracks()) { if (control::trackIdOf(candidate) == track) { added = candidate; } }
		QVERIFY(added != nullptr);
		QWidget host;
		QMenu menu(&host);
		ShellCommandMenus::populateTrack(&menu, added);
		QAction* entry = findAction(&menu, {}, QStringLiteral("Track Delay (0 samples)"));
		QVERIFY(entry != nullptr);
		QCOMPARE(entry->property("controlCommand").toString(), QStringLiteral("pdc.set_offset"));
		entry->trigger();
		auto* form = host.findChild<CommandForm*>();
		QVERIFY(form != nullptr);
		QVERIFY(form->setField(QStringLiteral("offset_samples"), 37));
		form->accept();
		QCOMPARE(static_cast<SampleTrack*>(added)->audioBusHandle()->offsetFrames(), 37);
		QMenu again(&host);
		ShellCommandMenus::populateTrack(&again, added);
		QVERIFY(findAction(&again, {}, QStringLiteral("Track Delay (37 samples)")) != nullptr);
		// A channel's entry, in its Strip menu (none for the master).
		QWidget channelHost;
		QMenu channelMenu(&channelHost);
		ShellCommandMenus::populateChannel(&channelMenu, ch(2));
		QAction* channelEntry = findAction(&channelMenu, {QStringLiteral("Strip")}, QStringLiteral("Channel Delay (0 samples)"));
		QVERIFY(channelEntry != nullptr);
		channelEntry->trigger();
		auto* channelForm = channelHost.findChild<CommandForm*>();
		QVERIFY(channelForm != nullptr);
		QVERIFY(channelForm->setField(QStringLiteral("offset_samples"), -21));
		channelForm->accept();
		QCOMPARE(Engine::mixer()->mixerChannel(2)->offsetFrames(), -21);
		QMenu master(&channelHost);
		ShellCommandMenus::populateChannel(&master, ch(0));
		QVERIFY(findAction(&master, {QStringLiteral("Strip")}, QStringLiteral("Channel Delay")) == nullptr);
	}

	//! Q050-340: the Input Meters window, opened from the mixer view's menu, shows each capture channel at its own
	//! level and each armed track's selected input; window.input_meters answers the same meters.
	void theInputMetersWindowShowsEachInput()
	{
		QMenu menu(strip(1));
		ShellCommandMenus::populateChannel(&menu, ch(1));
		QAction* open = findAction(&menu, {QStringLiteral("Mixer View")}, QStringLiteral("Input Meters"));
		QVERIFY(open != nullptr);
		open->trigger();
		InputChannelMeters* window = openInputMeters();
		QVERIFY(window != nullptr && window->isVisible());
		Engine::audioEngine()->audioDev()->stopProcessing();
		MultiTrackRecorder& recorder = Engine::audioEngine()->recorder();
		recorder.setInputChannelCapacity(4);
		QTemporaryDir dir;
		QVERIFY(recorder.track(0).arm(dir.filePath(QStringLiteral("in.wav")).toStdString(),
			static_cast<int>(Engine::audioEngine()->outputSampleRate()), 2));
		FakeCaptureDevice device(Engine::audioEngine());
		device.prepare(64, 0, 1);
		Engine::audioEngine()->renderNextPeriod();
		window->poll();  // take what was there before
		std::vector<float> block(64 * 4);
		const std::array<float, 4> levels{0.5f, 0.25f, 0.125f, 1.f};
		for (std::size_t f = 0; f < 64; ++f) { for (std::size_t c = 0; c < 4; ++c) { block[f * 4 + c] = (f % 2 == 0 ? 1.f : -1.f) * levels[c]; } }
		device.publish(block, 4);
		Engine::audioEngine()->renderNextPeriod();
		window->poll();
		QCOMPARE(static_cast<int>(window->channels().size()), 4);
		for (std::size_t c = 0; c < 4; ++c)
		{
			QCOMPARE(window->channels()[c].level, levels[c]);
			QCOMPARE(window->channels()[c].label, QStringLiteral("In %1").arg(c + 1));
		}
		QCOMPARE(static_cast<int>(window->armed().size()), 1);
		QCOMPARE(window->armed()[0].inputChannel, 2);
		QCOMPARE(window->armed()[0].level, 0.125f);
		QVERIFY(window->armed()[0].label.endsWith(QStringLiteral("In 3")));
		QVERIFY(window->accessibleDescription().contains(QStringLiteral("In 4 0.0 dB")));
		const ControlResult shown = run(QStringLiteral("window.input_meters"));
		QVERIFY2(shown.ok, qPrintable(shown.errorMessage));
		QCOMPARE(shown.result.value(QStringLiteral("channels")).toArray().size(), 4);
		QCOMPARE(shown.result.value(QStringLiteral("armed")).toArray().at(0).toObject().value(QStringLiteral("input_channel")).toInt(), 2);
		// Silence from here on: every bar falls, to nothing.
		for (int tick = 0; tick < 200; ++tick)
		{
			Engine::audioEngine()->renderNextPeriod();
			window->poll();
		}
		for (const auto& meter : window->channels()) { QCOMPARE(meter.level, 0.f); }
		QCOMPARE(window->armed()[0].level, 0.f);
		recorder.track(0).disarm();
		window->close();
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(MixerStripFeaturesTest)
#include "MixerStripFeaturesTest.moc"
