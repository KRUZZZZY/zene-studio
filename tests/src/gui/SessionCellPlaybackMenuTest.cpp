/*
 * SessionCellPlaybackMenuTest.cpp - an audio cell's gain, pitch, loop and warp from the launcher's menu (Q050-493)
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

/*! Q050-493: an audio cell's right-click menu has Playback and Warp - a form holding every playback field
 *  session.set_slot accepts (gain_db, transpose, detune, loop_start, loop_length, warp, warp_bpm, ram) at the cell's
 *  current values, Warp to Song Tempo and RAM Mode On/Off, and a reset. The test drives those menu entries and the
 *  form they open (not the handler), reads the cell back through session.get_state, undoes each edit as one step,
 *  saves and reloads the project, checks the cell's text and accessible name, and renders the cell on the Dummy
 *  device (the test drives the periods) to hear what Q050-013's engine tests expect: -6.02 dB is exact half level,
 *  a loop region plays exactly its frames, +12 semitones doubles the frequency, and a warp at twice the clip's
 *  tempo halves its length. Without the menu entries none of the GUI paths exists and every case fails. */

#include <QtTest>

#include <QApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QTemporaryDir>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

#include <sndfile.h>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "CommandForm.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "MenuTestSupport.h"
#include "SampleFrame.h"
#include "SessionGridView.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;
using lmms::test::menuAction;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

const QString Playback = QStringLiteral("Playback and Warp");
const QString FormEntry = QStringLiteral("Gain, Pitch, Loop and Warp...");

//! Two steps of a float file read back, plus float mixing.
constexpr float Tolerance = 4.0f / 8388608.0f;

bool writeFloatWav(const QString& path, const std::vector<SampleFrame>& frames, int rate)
{
	SF_INFO info{};
	info.channels = 2;
	info.samplerate = rate;
	info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
	SNDFILE* file = sf_open(path.toUtf8().constData(), SFM_WRITE, &info);
	if (file == nullptr) { return false; }
	const sf_count_t written = sf_writef_float(file, &frames[0][0], static_cast<sf_count_t>(frames.size()));
	sf_close(file);
	return written == static_cast<sf_count_t>(frames.size());
}

std::vector<SampleFrame> rampClip(std::size_t n)
{
	std::vector<SampleFrame> clip(n);
	for (std::size_t i = 0; i < n; ++i)
	{
		clip[i] = SampleFrame(static_cast<float>(i % 997 + 1) / 2048.0f, -static_cast<float>(i % 1009 + 1) / 2048.0f);
	}
	return clip;
}

//! @a frames of a sine of @a periodFrames frames per cycle at 0.5, then @a silent frames of silence.
std::vector<SampleFrame> sineClip(int frames, int periodFrames, int silent = 0)
{
	std::vector<SampleFrame> clip(static_cast<std::size_t>(frames + silent));
	for (int i = 0; i < frames; ++i)
	{
		const float v = 0.5f * std::sin(6.2831853f * static_cast<float>(i % periodFrames) / static_cast<float>(periodFrames));
		clip[static_cast<std::size_t>(i)] = SampleFrame(v, v);
	}
	return clip;
}

QJsonObject cellState(int scene = 0)
{
	for (const QJsonValue& value : run(QStringLiteral("session.get_state")).result.value(QStringLiteral("slots")).toArray())
	{
		const QJsonObject slot = value.toObject();
		if (slot.value(QStringLiteral("track")).toInt() == 0 && slot.value(QStringLiteral("scene")).toInt() == scene) { return slot; }
	}
	return {};
}

void closeForms()
{
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		if (qobject_cast<CommandForm*>(widget) != nullptr) { widget->close(); }
	}
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

} // namespace

class SessionCellPlaybackMenuTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test drives the periods itself
		ControlRegistry::setReady(true);
		QVERIFY(m_dir.isValid());
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		closeForms();
		Engine::getSong()->clearProject();
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}}).ok);
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 1}, {QStringLiteral("scenes"), 2}}).ok);
		QVERIFY(run(QStringLiteral("session.set_quantisation"), {{QStringLiteral("quantisation"), QStringLiteral("none")}}).ok);
		QVERIFY(run(QStringLiteral("transport.set_tempo"), {{QStringLiteral("bpm"), 120}}).ok);
		for (int i = 0; i < 4; ++i) { period(); }
	}

	//! The menu offers every playback field set_slot accepts, at the cell's values; a MIDI cell has no such entry.
	void theFormHoldsEveryPlaybackFieldAtTheCellsValues()
	{
		setAudioCell(rampClip(600), QStringLiteral("all.wav"));
		const QJsonObject values{{QStringLiteral("gain_db"), -3.0}, {QStringLiteral("transpose"), 7},
			{QStringLiteral("detune"), -20}, {QStringLiteral("loop_start"), 2}, {QStringLiteral("loop_length"), 1},
			{QStringLiteral("warp"), true}, {QStringLiteral("warp_bpm"), 97.5}, {QStringLiteral("ram"), true}};
		QJsonObject args = values;
		args.insert(QStringLiteral("track"), 0);
		args.insert(QStringLiteral("scene"), 0);
		QVERIFY(run(QStringLiteral("session.set_slot"), args).ok);
		CommandForm* form = openForm();
		QVERIFY(form != nullptr);
		QJsonObject collected;
		QString error;
		QVERIFY2(form->collect(&collected, &error), qPrintable(error));
		for (auto it = values.begin(); it != values.end(); ++it)
		{
			QVERIFY2(form->fields().contains(it.key()), qPrintable(it.key()));
			QVERIFY2(collected.value(it.key()) == it.value(), qPrintable(QStringLiteral("%1: form %2").arg(it.key())
				.arg(collected.value(it.key()).toVariant().toString())));
		}
		closeForms();

		QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 1},
			{QStringLiteral("type"), QStringLiteral("midi")}, {QStringLiteral("pattern"), 0}}).ok);
		SessionGridView view;
		view.refresh();
		QMenu menu;
		view.populateMenu(&menu, view.cellButton(0, 1));
		QVERIFY2(menuAction(&menu, {Playback, FormEntry}) == nullptr, "a MIDI cell offered audio playback settings");
	}

	//! Each field, edited in the form and run: the cell holds it (session.get_state), its text and accessible name say
	//! so, and one control.undo takes back exactly that edit.
	void eachFieldEditedInTheFormIsOneUndoStep()
	{
		setAudioCell(rampClip(600), QStringLiteral("edit.wav"));
		struct Case { const char* key; QJsonValue value; QString badge; QString words; };
		const std::vector<Case> cases = {
			{"gain_db", -6.0, QStringLiteral("-6.0dB"), QStringLiteral("gain -6.0 dB")},
			{"transpose", 12, QStringLiteral("+12st"), QStringLiteral("transposed +12 semitones")},
			{"detune", 50, QStringLiteral("+50ct"), QStringLiteral("detuned +50 cents")},
			{"loop_start", 48, QStringLiteral("⟲"), QStringLiteral("looping from tick 48 to the end")},
			{"loop_length", 192, QStringLiteral("⟲"), QStringLiteral("looping 192 ticks from tick 0")},
			{"warp", true, QStringLiteral("W120"), QStringLiteral("warped to the song tempo from 120 BPM")},
			{"ram", true, QString(), QString()},
		};
		for (const Case& c : cases)
		{
			const QString key = QLatin1String(c.key);
			const QJsonValue before = cellState().value(key);
			submitForm({{key, c.value}});
			QCOMPARE(cellState().value(key), c.value);
			SessionGridView view;
			view.refresh();
			const QToolButton* cell = view.cellButton(0, 0);
			QVERIFY2(cell->text().endsWith(c.badge), qPrintable(key + QStringLiteral(": ") + cell->text()));
			QVERIFY2(cell->accessibleName().endsWith(c.words), qPrintable(key + QStringLiteral(": ") + cell->accessibleName()));
			QVERIFY2(c.words.isEmpty() || cell->toolTip().contains(c.words), qPrintable(cell->toolTip()));
			QVERIFY(run(QStringLiteral("control.undo")).ok);
			QCOMPARE(cellState().value(key), before);
		}
		// warp_bpm, with warp on (the badge shows the clip's tempo)
		submitForm({{QStringLiteral("warp"), true}, {QStringLiteral("warp_bpm"), 98.5}});
		QCOMPARE(cellState().value(QStringLiteral("warp_bpm")).toDouble(), 98.5);
		SessionGridView view;
		view.refresh();
		QVERIFY2(view.cellButton(0, 0)->text().endsWith(QStringLiteral("W98.5")), qPrintable(view.cellButton(0, 0)->text()));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(cellState().value(QStringLiteral("warp")).toBool(), false);
		QCOMPARE(cellState().value(QStringLiteral("warp_bpm")).toDouble(), 120.0);
	}

	//! Warp to Song Tempo and RAM Mode On/Off, and Reset, from the submenu: each one undo step.
	void theSwitchesAndTheResetAreOneUndoStepEach()
	{
		setAudioCell(rampClip(600), QStringLiteral("switch.wav"));
		trigger({Playback, QStringLiteral("Warp to Song Tempo"), QStringLiteral("On")});
		QCOMPARE(cellState().value(QStringLiteral("warp")).toBool(), true);
		trigger({Playback, QStringLiteral("Warp to Song Tempo"), QStringLiteral("Off")});
		QCOMPARE(cellState().value(QStringLiteral("warp")).toBool(), false);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(cellState().value(QStringLiteral("warp")).toBool(), true);
		trigger({Playback, QStringLiteral("RAM Mode"), QStringLiteral("On")});
		QCOMPARE(cellState().value(QStringLiteral("ram")).toBool(), true);

		submitForm({{QStringLiteral("gain_db"), -2.0}, {QStringLiteral("transpose"), -5}, {QStringLiteral("detune"), 30},
			{QStringLiteral("loop_start"), 4}, {QStringLiteral("loop_length"), 8}});
		trigger({Playback, QStringLiteral("Reset Gain, Pitch, Loop and Warp")});
		const QJsonObject reset = cellState();
		QCOMPARE(reset.value(QStringLiteral("gain_db")).toDouble(), 0.0);
		QCOMPARE(reset.value(QStringLiteral("transpose")).toInt(), 0);
		QCOMPARE(reset.value(QStringLiteral("detune")).toInt(), 0);
		QCOMPARE(reset.value(QStringLiteral("loop_start")).toInt(), 0);
		QCOMPARE(reset.value(QStringLiteral("loop_length")).toInt(), 0);
		QCOMPARE(reset.value(QStringLiteral("warp")).toBool(), false);
		SessionGridView view;
		view.refresh();
		QVERIFY2(view.cellButton(0, 0)->text().endsWith(QStringLiteral("switch")), qPrintable(view.cellButton(0, 0)->text()));
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // one step brings every field back
		const QJsonObject back = cellState();
		QCOMPARE(back.value(QStringLiteral("gain_db")).toDouble(), -2.0);
		QCOMPARE(back.value(QStringLiteral("transpose")).toInt(), -5);
		QCOMPARE(back.value(QStringLiteral("detune")).toInt(), 30);
		QCOMPARE(back.value(QStringLiteral("loop_start")).toInt(), 4);
		QCOMPARE(back.value(QStringLiteral("loop_length")).toInt(), 8);
		QCOMPARE(back.value(QStringLiteral("warp")).toBool(), true);
	}

	//! What the form set is saved with the project and read back on load, and the reloaded cell shows it.
	void theEditsSurviveSaveAndLoad()
	{
		setAudioCell(rampClip(600), QStringLiteral("saved.wav"));
		const QJsonObject values{{QStringLiteral("gain_db"), -4.5}, {QStringLiteral("transpose"), 3},
			{QStringLiteral("detune"), -15}, {QStringLiteral("loop_start"), 6}, {QStringLiteral("loop_length"), 12},
			{QStringLiteral("warp"), true}, {QStringLiteral("warp_bpm"), 132.0}, {QStringLiteral("ram"), true}};
		submitForm(values);
		const QString path = m_dir.filePath(QStringLiteral("cell.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(path));
		Engine::getSong()->clearProject();
		QVERIFY(cellState().value(QStringLiteral("type")).toString() != QStringLiteral("audio"));
		Engine::getSong()->loadProject(path);
		const QJsonObject loaded = cellState();
		for (auto it = values.begin(); it != values.end(); ++it)
		{
			QVERIFY2(loaded.value(it.key()).toVariant() == it.value().toVariant(), qPrintable(QStringLiteral("%1: %2")
				.arg(it.key(), loaded.value(it.key()).toVariant().toString())));
		}
		SessionGridView view;
		view.refresh();
		QVERIFY2(view.cellButton(0, 0)->text().endsWith(QStringLiteral("-4.5dB +3st -15ct ⟲ W132")),
			qPrintable(view.cellButton(0, 0)->text()));
	}

	//! Gain and loop region set from the form render exactly as SessionAudioCellPlaybackTest expects.
	void gainAndLoopFromTheFormRenderAsTheEngineExpects()
	{
		const std::vector<SampleFrame> clip = rampClip(1500);
		setAudioCell(clip, QStringLiteral("gain.wav"));
		submitForm({{QStringLiteral("gain_db"), -6.020599913}});
		std::vector<SampleFrame> half = clip;
		for (SampleFrame& f : half) { f = f * 0.5f; }
		launchAndCompare(half);

		submitForm({{QStringLiteral("gain_db"), 0.0}, {QStringLiteral("loop_start"), 2}, {QStringLiteral("loop_length"), 1}});
		const float fpt = Engine::framesPerTick();
		const auto start = static_cast<std::size_t>(std::llround(2 * fpt));
		const auto length = static_cast<std::size_t>(std::llround(1 * fpt));
		QVERIFY(start + length < clip.size());
		launchAndCompare(std::vector<SampleFrame>(clip.begin() + static_cast<long>(start), clip.begin() + static_cast<long>(start + length)));
	}

	//! Transpose from the form: +12 semitones renders a 100 Hz clip at 200 Hz; detune +100 cents one semitone up.
	void transposeAndDetuneFromTheFormRepitch()
	{
		const auto rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		setAudioCell(sineClip(rate * 2, rate / 100), QStringLiteral("tone.wav"));
		const double plain = crossingsPerSecond(launchAndCollect(rate));
		QVERIFY2(std::fabs(plain - 100.0) < 2.0, qPrintable(QString::number(plain)));
		submitForm({{QStringLiteral("transpose"), 12}});
		const double octave = crossingsPerSecond(launchAndCollect(rate));
		QVERIFY2(std::fabs(octave / 100.0 - 2.0) < 0.04, qPrintable(QString::number(octave)));
		submitForm({{QStringLiteral("transpose"), 0}, {QStringLiteral("detune"), 100}});
		const double cents = crossingsPerSecond(launchAndCollect(rate));
		QVERIFY2(std::fabs(cents / 100.0 - std::exp2(1.0 / 12.0)) < 0.02 * std::exp2(1.0 / 12.0), qPrintable(QString::number(cents)));
	}

	//! Warp to Song Tempo from the menu, at 240 BPM against a 120 BPM clip: the clip's sound lasts half as long,
	//! at its own pitch; switched off it plays at its own length again.
	void warpFromTheMenuFollowsTheSongTempo()
	{
		const auto rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		QVERIFY(run(QStringLiteral("transport.set_tempo"), {{QStringLiteral("bpm"), 240}}).ok);
		setAudioCell(sineClip(rate / 2, rate / 400, rate / 2), QStringLiteral("warp.wav"));  // half a second of 400 Hz, half silence
		const std::size_t unwarped = soundLength(launchAndCollect(rate));
		QVERIFY2(std::fabs(static_cast<double>(unwarped) - rate / 2.0) < rate * 0.02, qPrintable(QString::number(unwarped)));
		trigger({Playback, QStringLiteral("Warp to Song Tempo"), QStringLiteral("On")});
		const std::vector<SampleFrame> out = launchAndCollect(rate);
		const std::size_t warped = soundLength(out);
		QVERIFY2(std::fabs(static_cast<double>(warped) - rate / 4.0) < rate * 0.03,
			qPrintable(QStringLiteral("warped sound %1 frames, expected %2").arg(warped).arg(rate / 4)));
		const std::vector<SampleFrame> sound(out.begin(), out.begin() + static_cast<long>(warped));
		QVERIFY2(std::fabs(crossingsPerSecond(sound) - 400.0) < 12.0, qPrintable(QString::number(crossingsPerSecond(sound))));
		trigger({Playback, QStringLiteral("Warp to Song Tempo"), QStringLiteral("Off")});
		QVERIFY(std::fabs(static_cast<double>(soundLength(launchAndCollect(rate))) - rate / 2.0) < rate * 0.02);
		QVERIFY(run(QStringLiteral("transport.set_tempo"), {{QStringLiteral("bpm"), 120}}).ok);
	}

private:
	void setAudioCell(const std::vector<SampleFrame>& clip, const QString& name)
	{
		const QString path = m_dir.filePath(name);
		QVERIFY(writeFloatWav(path, clip, static_cast<int>(Engine::audioEngine()->outputSampleRate())));
		QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("type"), QStringLiteral("audio")}, {QStringLiteral("source"), path},
			{QStringLiteral("name"), QFileInfo(name).completeBaseName()}}).ok);
	}

	//! The cell's menu, as a right-click on cell (0, 0) builds it, and the entry at @a path triggered.
	void trigger(const QStringList& path)
	{
		SessionGridView view;
		view.refresh();
		QMenu menu;
		view.populateMenu(&menu, view.cellButton(0, 0));
		QAction* action = menuAction(&menu, path);
		QVERIFY2(action != nullptr, qPrintable(QStringLiteral("no menu entry ") + path.join(QStringLiteral(" > "))));
		QCOMPARE(action->property("controlCommand").toString(), QStringLiteral("session.set_slot"));
		QVERIFY(action->isEnabled());
		action->trigger();
	}

	//! Playback and Warp > Gain, Pitch, Loop and Warp... on cell (0, 0): the form it opens.
	CommandForm* openForm()
	{
		closeForms();
		trigger({Playback, FormEntry});
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			auto* form = qobject_cast<CommandForm*>(widget);
			if (form != nullptr && form->isVisible()) { return form; }
		}
		return nullptr;
	}

	//! The form opened from the menu, @a fields filled in, run (its Run button: accept()).
	void submitForm(const QJsonObject& fields)
	{
		CommandForm* form = openForm();
		QVERIFY2(form != nullptr, "Playback and Warp opened no form");
		for (auto it = fields.begin(); it != fields.end(); ++it)
		{
			QVERIFY2(form->setField(it.key(), it.value()), qPrintable(it.key()));
		}
		form->accept();
		QVERIFY2(form->result().ok, qPrintable(form->result().errorMessage));
		closeForms();
	}

	void period()
	{
		const std::span<const SampleFrame> out = Engine::audioEngine()->renderNextPeriod();
		m_out.insert(m_out.end(), out.begin(), out.end());
	}

	//! Launches cell (0, 0) and returns @a frames of its output from the first sound on, then stops it.
	std::vector<SampleFrame> launchAndCollect(int frames)
	{
		m_out.clear();
		const ControlResult launched = run(QStringLiteral("session.launch_slot"), {{QStringLiteral("track"), 0},
			{QStringLiteral("scene"), 0}, {QStringLiteral("quantisation"), QStringLiteral("none")}});
		if (!launched.ok) { QTest::qFail(qPrintable(launched.errorMessage), __FILE__, __LINE__); return {}; }
		const auto need = static_cast<std::size_t>(frames) + 16 * static_cast<std::size_t>(Engine::audioEngine()->framesPerPeriod());
		while (m_out.size() < need) { period(); }
		std::size_t first = 0;
		while (first < m_out.size() && m_out[first].left() == 0.0f && m_out[first].right() == 0.0f) { ++first; }
		std::vector<SampleFrame> out(m_out.begin() + static_cast<long>(first), m_out.end());
		out.resize(std::min(out.size(), static_cast<std::size_t>(frames)));
		run(QStringLiteral("session.stop_all"));
		period();
		return out;
	}

	//! Launches cell (0, 0): its output, from its first sound, is @a clip twice round - frame for frame.
	void launchAndCompare(const std::vector<SampleFrame>& clip)
	{
		const std::vector<SampleFrame> out = launchAndCollect(static_cast<int>(2 * clip.size()));
		QVERIFY2(out.size() == 2 * clip.size(), "the launched audio cell rendered too little");
		for (std::size_t i = 0; i < out.size(); ++i)
		{
			const SampleFrame& want = clip[i % clip.size()];
			if (std::fabs(out[i].left() - want.left()) > Tolerance || std::fabs(out[i].right() - want.right()) > Tolerance)
			{
				QFAIL(qPrintable(QStringLiteral("output frame %1 is (%2, %3), expected (%4, %5)").arg(i)
					.arg(out[i].left()).arg(out[i].right()).arg(want.left()).arg(want.right())));
			}
		}
	}

	//! Rising zero crossings per second of the left channel, past the first eighth (clear of a stretcher's onset).
	static double crossingsPerSecond(const std::vector<SampleFrame>& out)
	{
		const auto rate = static_cast<double>(Engine::audioEngine()->outputSampleRate());
		const std::size_t from = out.size() / 8;
		int crossings = 0;
		for (std::size_t i = from + 1; i < out.size(); ++i)
		{
			if (out[i - 1].left() < 0.0f && out[i].left() >= 0.0f) { ++crossings; }
		}
		return out.size() > from ? crossings * rate / static_cast<double>(out.size() - from) : 0.0;
	}

	//! Frames from the top of @a out to the first run of 256 silent frames (a 400 Hz sine never holds still that long).
	static std::size_t soundLength(const std::vector<SampleFrame>& out)
	{
		std::size_t quiet = 0;
		for (std::size_t i = 0; i < out.size(); ++i)
		{
			quiet = std::fabs(out[i].left()) < 1e-4f ? quiet + 1 : 0;
			if (quiet == 256) { return i + 1 - quiet; }
		}
		return out.size();
	}

	QTemporaryDir m_dir;
	std::vector<SampleFrame> m_out;
};

QTEST_MAIN(SessionCellPlaybackMenuTest)
#include "SessionCellPlaybackMenuTest.moc"
