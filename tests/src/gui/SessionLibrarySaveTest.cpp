/*
 * SessionLibrarySaveTest.cpp - Q050-273.1: an audio launcher cell saved into the shared library and reused
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

/*! Q050-273.1, driven the way a person drives it: right-click a populated AUDIO launcher cell, choose
 *  "Save to Library...", confirm the name in the command form's own Run button, and the file appears in
 *  the shared Clips library. The saved entry is then read back through the browser, the project is saved
 *  and reopened, and the entry is dragged from the browser onto another cell - the grid's own drop path -
 *  where its source is the library file and its decoded audio is the source's audio frame for frame.
 *
 *  The menu entry runs browser.save_clip with {track, scene} fixed, so the GUI and an agent on the socket
 *  are the same command; the agent half (no window at all) is tests/control-session-library-save.py.
 *  An empty cell and a MIDI cell offer no entry: one has no content, and a MIDI cell's PatternStore notes
 *  are stage 2 of Q050-273 (the command refuses one by name).
 *
 *  The failure matrix is part of the feature, not an afterthought: a missing, empty, damaged, unsupported
 *  or over-limit source, a duplicate, an allocation failure and a short or uncommitted write all have to
 *  leave no partial output, no tag mutation and a working undo history. The seam that bounds DECODED
 *  bytes is exercised with a file whose encoded size is BELOW the bound and whose decoded size is above
 *  it - the distinction the previous draft's input-size check got wrong.
 */

#include <QtTest>

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolButton>

#include <cmath>
#include <limits>
#include <span>
#include <vector>

#include <sndfile.h>

#include "../core/AllocationProbe.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "CommandForm.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "MenuTestSupport.h"
#include "SampleDecoder.h"
#include "SampleTrack.h"
#include "SessionAudioCells.h"
#include "SessionGridView.h"
#include "Song.h"
#include "StringPairDrag.h"
#include <QScopeGuard>
#include "../core/TakeLaneTestSupport.h"

using namespace lmms;
using namespace lmms::gui;
using lmms::test::menuAction;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

QJsonObject slotState(int track, int scene)
{
	for (const QJsonValue& value : run(QStringLiteral("session.get_state")).result.value(QStringLiteral("slots")).toArray())
	{
		const QJsonObject slot = value.toObject();
		if (slot.value(QStringLiteral("track")).toInt() == track && slot.value(QStringLiteral("scene")).toInt() == scene) { return slot; }
	}
	return {};
}

//! The visible command form the menu entry opened, or nullptr.
CommandForm* visibleForm()
{
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		if (auto* form = qobject_cast<CommandForm*>(widget); form != nullptr && form->isVisible()) { return form; }
	}
	return nullptr;
}

//! Click the form's own Run button: the control a person presses, not the handler.
void clickRun(CommandForm* form)
{
	for (QPushButton* button : form->findChildren<QPushButton*>())
	{
		if (button->text().remove(QLatin1Char('&')) == QStringLiteral("Run")) { QTest::mouseClick(button, Qt::LeftButton); }
	}
}

//! The largest |a - b| of any channel over two decoded files; -1 when either cannot be decoded or their
//! lengths differ. The saved WAV is stereo, so a mono source is compared as the stereo it became.
double maxFrameDifference(const QString& a, const QString& b, int* rateA, int* rateB, std::size_t* framesA, std::size_t* framesB)
{
	const auto left = SampleDecoder::decode(a);
	const auto right = SampleDecoder::decode(b);
	if (!left || !right) { return -1.0; }
	*rateA = left->sampleRate;
	*rateB = right->sampleRate;
	*framesA = left->data.size();
	*framesB = right->data.size();
	if (left->data.size() != right->data.size()) { return -1.0; }
	double worst = 0.0;
	for (std::size_t i = 0; i < left->data.size(); ++i)
	{
		for (int channel = 0; channel < 2; ++channel)
		{
			worst = std::max(worst, std::abs(static_cast<double>(left->data[i][channel]) - right->data[i][channel]));
		}
	}
	return worst;
}

//! Every entry of @a dir's name, sorted: what a failed save must not add to (a QSaveFile temporary).
QStringList namesIn(const QString& dir)
{
	QStringList out;
	for (QDirIterator it(dir, QDir::Files | QDir::NoDotAndDotDot); it.hasNext();) { out << QFileInfo(it.next()).fileName(); }
	out.sort();
	return out;
}

} // namespace

class SessionLibrarySaveTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		Engine::init(true);
		// A private working directory: the default library folder (<user samples>/Clips) is this test's
		// own, never the developer's, and the project it saves and reopens lives there too.
		ConfigManager::inst()->setWorkingDir(m_home.path());
		ControlRegistry::setReady(true);
		QVERIFY(QDir().mkpath(m_home.filePath(QStringLiteral("source"))));
		m_tone = taketest::writeToneWav(m_home.filePath(QStringLiteral("source/tone.wav")), 440.0);
		QVERIFY2(!m_tone.isEmpty(), "the audio-cell fixture was not written");

		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}}).ok);
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 2}, {QStringLiteral("scenes"), 2}}).ok);
		QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("type"), QStringLiteral("audio")}, {QStringLiteral("source"), m_tone},
			{QStringLiteral("name"), QStringLiteral("kick")}}).ok);
		QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 1},
			{QStringLiteral("type"), QStringLiteral("midi")}, {QStringLiteral("pattern"), 0},
			{QStringLiteral("name"), QStringLiteral("lead")}}).ok);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Criterion 1: a populated AUDIO cell's menu offers Save to Library..., which runs browser.save_clip
	//! through a form whose name is the cell's; one form Run writes the audio into the library, and one
	//! undo takes the file away again (redo puts it back).
	void anAudioCellSavesItselfToTheLibraryFromItsMenu()
	{
		SessionGridView view;
		view.refresh();
		QMenu menu;
		view.populateMenu(&menu, view.cellButton(0, 0));
		QAction* save = menuAction(&menu, {QStringLiteral("Save to Library...")});
		QVERIFY2(save != nullptr, "a populated audio cell's menu has no Save to Library... entry");
		QCOMPARE(save->property("controlCommand").toString(), QStringLiteral("browser.save_clip"));
		QVERIFY(save->isEnabled());

		save->trigger();  // the menu entry, not the command: it must open the command form
		CommandForm* form = visibleForm();
		QVERIFY2(form != nullptr, "Save to Library... opened no form");
		QVERIFY2(form->findChild<QWidget*>(QStringLiteral("fixed:track")) != nullptr, "the cell's track is not fixed in the form");
		QVERIFY2(form->findChild<QWidget*>(QStringLiteral("fixed:scene")) != nullptr, "the cell's scene is not fixed in the form");
		QVERIFY(!form->fields().contains(QStringLiteral("track")));
		QVERIFY(!form->fields().contains(QStringLiteral("scene")));
		QJsonObject args;
		QString error;
		QVERIFY2(form->collect(&args, &error), qPrintable(error));
		QCOMPARE(args.value(QStringLiteral("track")).toInt(), 0);
		QCOMPARE(args.value(QStringLiteral("scene")).toInt(), 0);
		QCOMPARE(args.value(QStringLiteral("name")).toString(), QStringLiteral("kick"));  // the cell's name, prefilled

		clickRun(form);
		const QString saved = QDir(ConfigManager::inst()->userSamplesDir()).filePath(QStringLiteral("Clips/kick.wav"));
		QVERIFY2(QFileInfo::exists(saved), qPrintable(saved));
		const ControlResult listed = run(QStringLiteral("browser.query"), {{QStringLiteral("root"), QStringLiteral("samples")},
			{QStringLiteral("text"), QStringLiteral("kick.wav")}, {QStringLiteral("kind"), QStringLiteral("audio")}});
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		const QJsonArray items = listed.result.value(QStringLiteral("items")).toArray();
		QCOMPARE(items.size(), 1);
		QCOMPARE(items.first().toObject().value(QStringLiteral("path")).toString(), saved);

		// The library entry is the cell's audio, frame for frame: the saved WAV decodes to the source's frames.
		int rateA = 0;
		int rateB = 0;
		std::size_t framesA = 0;
		std::size_t framesB = 0;
		const double worst = maxFrameDifference(m_tone, saved, &rateA, &rateB, &framesA, &framesB);
		QVERIFY2(worst >= 0.0, "the saved audio could not be decoded");
		QCOMPARE(rateA, rateB);
		QCOMPARE(framesA, framesB);
		QVERIFY2(worst < 1e-6, qPrintable(QStringLiteral("largest frame difference %1").arg(worst)));

		// The save is one true-inverse step: undo removes the file, redo writes it back.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY2(!QFileInfo::exists(saved), "undo left the library file behind");
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QVERIFY2(QFileInfo::exists(saved), "redo did not write the library file back");
	}

	//! Criterion 2: after the project is saved and reopened, the library entry is in the browser and can be
	//! put in ANOTHER audio cell through the browser's own drag, with the source content intact.
	void theSavedEntryIsReusedInAnotherCellAfterReopening()
	{
		SessionGridView view;
		view.refresh();
		QMenu menu;
		view.populateMenu(&menu, view.cellButton(0, 0));
		QAction* save = menuAction(&menu, {QStringLiteral("Save to Library...")});
		QVERIFY2(save != nullptr, "a populated audio cell's menu has no Save to Library... entry");
		save->trigger();
		CommandForm* form = visibleForm();
		QVERIFY2(form != nullptr, "Save to Library... opened no form");
		clickRun(form);
		const QString saved = QDir(ConfigManager::inst()->userSamplesDir()).filePath(QStringLiteral("Clips/kick.wav"));
		QVERIFY2(QFileInfo::exists(saved), qPrintable(saved));

		const QString project = m_home.filePath(QStringLiteral("saved.mmp"));
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), project}}).ok);
		QVERIFY(run(QStringLiteral("project.new")).ok);
		QVERIFY2(run(QStringLiteral("session.get_state")).result.value(QStringLiteral("slots")).toArray().isEmpty(),
			"a new project inherited the previous grid");
		const ControlResult opened = run(QStringLiteral("project.open"), {{QStringLiteral("path"), project}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		QCOMPARE(slotState(0, 0).value(QStringLiteral("source")).toString(), m_tone);

		// After the reopen the entry is still browsable, and the browser's drag (its own "samplefile"
		// payload) puts it in cell (1, 1) through the grid's drop path.
		const ControlResult listed = run(QStringLiteral("browser.query"), {{QStringLiteral("root"), QStringLiteral("samples")},
			{QStringLiteral("text"), QStringLiteral("kick.wav")}, {QStringLiteral("kind"), QStringLiteral("audio")}});
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		QCOMPARE(listed.result.value(QStringLiteral("items")).toArray().size(), 1);

		view.refresh();
		QToolButton* target = view.cellButton(1, 1);
		QVERIFY(target != nullptr);
		QMimeData* mime = StringPairDrag::createMimeData(QStringLiteral("samplefile"), saved);
		QDragEnterEvent enter(QPoint(4, 4), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
		QCoreApplication::sendEvent(target, &enter);
		QVERIFY2(enter.isAccepted(), "the grid refused the browser's own audio drag");
		QDropEvent drop(QPointF(4, 4), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
		QCoreApplication::sendEvent(target, &drop);
		delete mime;

		const QJsonObject reused = slotState(1, 1);
		QCOMPARE(reused.value(QStringLiteral("type")).toString(), QStringLiteral("audio"));
		QCOMPARE(reused.value(QStringLiteral("source")).toString(), saved);
		QCOMPARE(reused.value(QStringLiteral("name")).toString(), QStringLiteral("kick"));
		int rateA = 0;
		int rateB = 0;
		std::size_t framesA = 0;
		std::size_t framesB = 0;
		const double worst = maxFrameDifference(m_tone, reused.value(QStringLiteral("source")).toString(), &rateA, &rateB,
			&framesA, &framesB);
		QVERIFY2(worst >= 0.0, "the reused cell's audio could not be decoded");
		QCOMPARE(framesA, framesB);
		QVERIFY2(worst < 1e-6, qPrintable(QStringLiteral("largest frame difference %1").arg(worst)));

		// Leave the cell empty again, so the menu test below sees the grid this fixture describes.
		QVERIFY(run(QStringLiteral("session.clear_slot"), {{QStringLiteral("track"), 1}, {QStringLiteral("scene"), 1}}).ok);
	}

	//! An empty cell and a MIDI cell have nothing this stage can save, so neither offers the entry (the
	//! command refuses both by name; a menu item that always refused would be a dead entry).
	void onlyAPopulatedAudioCellOffersTheEntry()
	{
		QVERIFY(run(QStringLiteral("session.clear_slot"), {{QStringLiteral("track"), 1}, {QStringLiteral("scene"), 1}}).ok);
		SessionGridView view;
		view.refresh();
		QMenu empty;
		view.populateMenu(&empty, view.cellButton(1, 1));
		QVERIFY2(menuAction(&empty, {QStringLiteral("Save to Library...")}) == nullptr, "an empty cell offered Save to Library...");
		QMenu midi;
		view.populateMenu(&midi, view.cellButton(0, 1));
		QVERIFY2(menuAction(&midi, {QStringLiteral("Save to Library...")}) == nullptr, "a MIDI cell offered Save to Library...");
	}

	//! The command's own refusals, read through the registry the menu runs: nothing to save, a MIDI cell,
	//! an address outside the grid, both addresses at once - and a source that is missing, empty,
	//! damaged/unsupported or over the DECODED-size bound. Every refusal leaves no file behind.
	void theCommandRefusesWhatItCannotSave()
	{
		const QString clips = QDir(ConfigManager::inst()->userSamplesDir()).filePath(QStringLiteral("Clips"));
		QCOMPARE(run(QStringLiteral("browser.save_clip"), {{QStringLiteral("track"), 1}, {QStringLiteral("scene"), 1}}).errorKind,
			ControlErrorKind::Refused);
		const ControlResult midi = run(QStringLiteral("browser.save_clip"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 1}});
		QCOMPARE(midi.errorKind, ControlErrorKind::Refused);
		QVERIFY2(midi.errorMessage.contains(QStringLiteral("MIDI")), qPrintable(midi.errorMessage));
		QCOMPARE(run(QStringLiteral("browser.save_clip"), {{QStringLiteral("track"), 9}, {QStringLiteral("scene"), 0}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("browser.save_clip"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("clip"), QStringLiteral("clip-0")}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("browser.save_clip"), {}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("browser.save_clip"), {{QStringLiteral("track"), 0}}).errorKind, ControlErrorKind::InvalidArgs);

		// A source that is not there / is empty / is not audio at all.
		const QString missing = m_home.filePath(QStringLiteral("source/missing.wav"));
		const QString empty = m_home.filePath(QStringLiteral("source/empty.wav"));
		const QString damaged = m_home.filePath(QStringLiteral("source/damaged.wav"));
		const QString unsupported = m_home.filePath(QStringLiteral("source/notes.txt"));
		QFile(empty).open(QIODevice::WriteOnly);
		QFile text(damaged);
		QVERIFY(text.open(QIODevice::WriteOnly));
		text.write("this is not a RIFF file");
		text.close();
		QFile other(unsupported);
		QVERIFY(other.open(QIODevice::WriteOnly));
		other.write("notes");
		other.close();
		for (const QString& source : {missing, empty, damaged, unsupported})
		{
			setSource(0, 0, source);
			const ControlResult refused = run(QStringLiteral("browser.save_clip"), {{QStringLiteral("track"), 0},
				{QStringLiteral("scene"), 0}, {QStringLiteral("name"), QStringLiteral("refused-source")}});
			QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
			QVERIFY2(!QFileInfo::exists(QDir(clips).filePath(QStringLiteral("refused-source.wav"))),
				qPrintable(QStringLiteral("%1 produced a file").arg(source)));
		}
		setSource(0, 0, m_tone);
	}

	/*! The bound is the DECODED size, not the source file's: a 16-bit mono WAV is 8 bytes a frame once
	 *  decoded (stereo float) but 2 bytes a frame on disk, so a budget between the two refuses this file
	 *  while its encoded size is under the bound. Before the change the check was the input size, so this
	 *  case passed the check and went on to decode the file in full. */
	void theDecodedSizeIsWhatBoundsASave()
	{
		const QString clips = QDir(ConfigManager::inst()->userSamplesDir()).filePath(QStringLiteral("Clips"));
		const qint64 fileBytes = QFileInfo(m_tone).size();
		const auto decodedFrames = static_cast<qint64>(taketest::kRate) * taketest::kSeconds;
		const qint64 decodedBytes = decodedFrames * 8;
		QVERIFY2(fileBytes < decodedBytes / 2, "the fixture is not a compressed-enough source for this proof");
		const qint64 budget = (fileBytes + decodedBytes) / 2;
		QVERIFY(budget > fileBytes && budget < decodedBytes);
		qputenv("ZENE_TEST_LIBRARY_SAVE_MAX_DECODED_BYTES", QByteArray::number(budget));
		const ControlResult refused = run(QStringLiteral("browser.save_clip"), {{QStringLiteral("track"), 0},
			{QStringLiteral("scene"), 0}, {QStringLiteral("name"), QStringLiteral("over-decoded")}});
		const QString path = QDir(clips).filePath(QStringLiteral("over-decoded.wav"));
		qunsetenv("ZENE_TEST_LIBRARY_SAVE_MAX_DECODED_BYTES");
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QVERIFY2(refused.errorMessage.contains(QStringLiteral("decodes to")), qPrintable(refused.errorMessage));
		QVERIFY2(!QFileInfo::exists(path), "an over-limit source wrote a file anyway");
		// The refusal names the measured DECODED frame count, not the file's size.
		QVERIFY2(refused.errorMessage.contains(QString::number(decodedFrames)), qPrintable(refused.errorMessage));

		// The same source under the default bound saves exactly.
		const ControlResult saved = run(QStringLiteral("browser.save_clip"), {{QStringLiteral("track"), 0},
			{QStringLiteral("scene"), 0}, {QStringLiteral("name"), QStringLiteral("under-decoded")}});
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		QVERIFY(QFileInfo::exists(QDir(clips).filePath(QStringLiteral("under-decoded.wav"))));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
	}

	//! A duplicate is refused and leaves the file exactly as it was; an overwrite replaces it in ONE undo
	//! step that puts the previous bytes back, and redo writes the cell's audio again.
	void anOverwriteIsOneUndoStepThatRestoresThePreviousBytes()
	{
		const QString path = QDir(ConfigManager::inst()->userSamplesDir()).filePath(QStringLiteral("Clips/overwrite.wav"));
		QVERIFY(run(QStringLiteral("browser.save_clip"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("name"), QStringLiteral("overwrite")}}).ok);
		QVERIFY(QFileInfo::exists(path));
		QFile earlier(path);
		QVERIFY(earlier.open(QIODevice::WriteOnly));
		earlier.write("earlier bytes");
		earlier.close();
		const QByteArray before = QByteArray("earlier bytes");
		QCOMPARE(run(QStringLiteral("browser.save_clip"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("name"), QStringLiteral("overwrite")}}).errorKind, ControlErrorKind::Refused);
		QFile unchanged(path);
		QVERIFY(unchanged.open(QIODevice::ReadOnly));
		QCOMPARE(unchanged.readAll(), before);
		unchanged.close();

		QVERIFY(run(QStringLiteral("browser.save_clip"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("name"), QStringLiteral("overwrite")}, {QStringLiteral("overwrite"), true}}).ok);
		QVERIFY(QFileInfo(path).size() > before.size());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QFile back(path);
		QVERIFY(back.open(QIODevice::ReadOnly));
		QCOMPARE(back.readAll(), before);
		back.close();
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		int rateA = 0;
		int rateB = 0;
		std::size_t framesA = 0;
		std::size_t framesB = 0;
		QVERIFY(maxFrameDifference(m_tone, path, &rateA, &rateB, &framesA, &framesB) >= 0.0);
		QCOMPARE(framesA, framesB);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
	}

	//! Allocation and write failures are deterministic seams: each is a typed refusal that leaves no partial
	//! output, no temporary file and no tag, does not replace the destination it was asked to overwrite, and
	//! leaves the undo history where it was.
	void allocationAndWriteFailuresLeaveNothingBehind()
	{
		const QString clips = QDir(ConfigManager::inst()->userSamplesDir()).filePath(QStringLiteral("Clips"));
		const QString path = QDir(clips).filePath(QStringLiteral("failure.wav"));
		QFile keep(path);
		QVERIFY(keep.open(QIODevice::WriteOnly));
		keep.write("keep me");
		keep.close();
		const QStringList before = namesIn(clips);
		const int transactions = run(QStringLiteral("control.transactions")).result.value(QStringLiteral("count")).toInt();
		const auto attempt = [&](const char* seam, const char* value, const char* message) {
			qputenv(seam, value);
			const ControlResult refused = run(QStringLiteral("browser.save_clip"), {{QStringLiteral("track"), 0},
				{QStringLiteral("scene"), 0}, {QStringLiteral("name"), QStringLiteral("failure")},
				{QStringLiteral("overwrite"), true}, {QStringLiteral("tags"), QJsonArray{QStringLiteral("lost-tag")}}});
			qunsetenv(seam);
			QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
			QVERIFY2(refused.errorMessage.contains(QLatin1String(message)), qPrintable(refused.errorMessage));
			QCOMPARE(namesIn(clips), before);  // no partial file, no QSaveFile temporary left behind
			QFile untouched(path);
			QVERIFY(untouched.open(QIODevice::ReadOnly));
			QCOMPARE(untouched.readAll(), QByteArray("keep me"));
			QCOMPARE(run(QStringLiteral("control.transactions")).result.value(QStringLiteral("count")).toInt(), transactions);
		};
		attempt("ZENE_TEST_LIBRARY_SAVE_ALLOC_FAULT", "1", "ran out of memory");
		attempt("ZENE_TEST_LIBRARY_SAVE_WRITE_FAULT", "short-write", "failed");
		attempt("ZENE_TEST_LIBRARY_SAVE_WRITE_FAULT", "before-commit", "not completed");
		QVERIFY(run(QStringLiteral("browser.tags")).result.value(QStringLiteral("entries")).toInt() >= 0);
		const QJsonArray tags = run(QStringLiteral("browser.tags")).result.value(QStringLiteral("tags")).toArray();
		for (const QJsonValue& tag : tags) { QVERIFY(tag.toObject().value(QStringLiteral("tag")).toString() != QStringLiteral("lost-tag")); }
	}

	/*! The realtime rule, measured where the code runs: the save is made on the calling (control / model)
	 *  thread while a launched audio cell renders on the test-driven device, and the renderer's own audio-thread
	 *  entry point then allocates and frees nothing. The save path allocates (it decodes and writes), which is
	 *  exactly why it must never be reached from the audio thread: it is a registered command handler, run by
	 *  ControlRegistry::invoke on the UI thread. */
	void savingWhileACellPlaysKeepsRenderingAndLeavesTheAudioPathAlone()
	{
		Engine::audioEngine()->audioDev()->stopProcessing();  // this test drives the periods itself
		const auto restart = qScopeGuard([] { Engine::audioEngine()->audioDev()->startProcessing(); });
		QVERIFY(run(QStringLiteral("session.stop_all")).ok);

		const auto rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		std::vector<SampleFrame> clip(1500);
		for (std::size_t i = 0; i < clip.size(); ++i)
		{
			clip[i] = SampleFrame(static_cast<float>(i % 997) / 2048.0f, -static_cast<float>(i % 1009) / 2048.0f);
		}
		const QString source = m_home.filePath(QStringLiteral("source/probe.wav"));
		writeFloatWav(source, clip, rate);
		setSource(0, 0, source);
		QVERIFY(run(QStringLiteral("session.launch_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("quantisation"), QStringLiteral("none")}}).ok);
		m_out.clear();
		for (int i = 0; i < 3; ++i) { period(); }
		// The save happens while the cell is playing - the control thread, as the socket and the menu do.
		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		const ControlResult saved = run(QStringLiteral("browser.save_clip"), {{QStringLiteral("track"), 0},
			{QStringLiteral("scene"), 0}, {QStringLiteral("name"), QStringLiteral("while-playing")}});
		const std::uint64_t saveAllocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		QVERIFY2(saveAllocations > 0, "the probe did not see the save's own allocations");

		// Two loops of the cell after the save: the stream never stopped and every frame is the clip's.
		QVERIFY2(renderedTwiceFromTheTop(clip), "the launched cell went silent or changed across the save");

		auto* track = dynamic_cast<SampleTrack*>(Engine::getSong()->tracks().front());
		QVERIFY(track != nullptr);
		SessionCellPlayHandle renderer(track);
		std::vector<SampleFrame> buffer(static_cast<std::size_t>(Engine::audioEngine()->framesPerPeriod()));
		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		for (int i = 0; i < 500; ++i) { renderer.play(buffer); }
		const std::uint64_t allocations = lmms::test::tlAllocationCount;
		const std::uint64_t frees = lmms::test::tlFreeCount;
		lmms::test::tlCountAllocations = false;
		QVERIFY2(allocations == 0, qPrintable(QStringLiteral("the cell renderer allocated %1 times after the save").arg(allocations)));
		QVERIFY2(frees == 0, qPrintable(QStringLiteral("the cell renderer freed %1 times after the save").arg(frees)));
		QVERIFY(run(QStringLiteral("session.stop_all")).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
	}

private:
	//! The index in m_out where @a clip's own first sound begins, or npos when it is not there.
	std::size_t clipStart(const std::vector<SampleFrame>& clip) const
	{
		std::size_t first = 0;
		while (first < m_out.size() && m_out[first].left() == 0.0f && m_out[first].right() == 0.0f) { ++first; }
		std::size_t from = 0;
		while (from < clip.size() && clip[from].left() == 0.0f && clip[from].right() == 0.0f) { ++from; }
		return first < from || first >= m_out.size() ? std::numeric_limits<std::size_t>::max() : first - from;
	}

	//! One loop of @a clip at @a at matches frame for frame.
	bool loopMatches(const std::vector<SampleFrame>& clip, std::size_t at) const
	{
		for (std::size_t i = 0; i < clip.size(); ++i)
		{
			const SampleFrame& want = clip[i];
			const SampleFrame& got = m_out[at + i];
			if (std::fabs(got.left() - want.left()) >= 1e-5f || std::fabs(got.right() - want.right()) >= 1e-5f) { return false; }
		}
		return true;
	}

	//! Renders until two loops of @a clip are in m_out, then true when both loops match it exactly.
	bool renderedTwiceFromTheTop(const std::vector<SampleFrame>& clip)
	{
		while (m_out.size() < 2 * clip.size() + 8 * static_cast<std::size_t>(Engine::audioEngine()->framesPerPeriod())) { period(); }
		const std::size_t at = clipStart(clip);
		if (at == std::numeric_limits<std::size_t>::max() || at + 2 * clip.size() > m_out.size()) { return false; }
		return loopMatches(clip, at) && loopMatches(clip, at + clip.size());
	}

	//! Points cell @a track,@a scene at @a source (as session.set_slot does), keeping its type audio.
	void setSource(int track, int scene, const QString& source)
	{
		QVERIFY2(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), track}, {QStringLiteral("scene"), scene},
			{QStringLiteral("type"), QStringLiteral("audio")}, {QStringLiteral("source"), source}}).ok, qPrintable(source));
	}

	void writeFloatWav(const QString& path, const std::vector<SampleFrame>& frames, int rate)
	{
		// A float WAV through libsndfile, written by the test itself (never by the code under test).
		SF_INFO info{};
		info.channels = 2;
		info.samplerate = rate;
		info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
		SNDFILE* file = sf_open(path.toUtf8().constData(), SFM_WRITE, &info);
		QVERIFY(file != nullptr);
		QCOMPARE(sf_writef_float(file, &frames[0][0], static_cast<sf_count_t>(frames.size())),
			static_cast<sf_count_t>(frames.size()));
		QCOMPARE(sf_close(file), 0);
	}

	//! One rendered period on the Dummy device, appended to m_out.
	void period()
	{
		AudioEngine* engine = Engine::audioEngine();
		const f_cnt_t fpp = engine->framesPerPeriod();
		std::vector<SampleFrame> block(static_cast<std::size_t>(fpp));
		engine->pushInputFrames(block.data(), fpp);
		const std::span<const SampleFrame> out = engine->renderNextPeriod();
		m_out.insert(m_out.end(), out.begin(), out.end());
	}

	QTemporaryDir m_home;
	QString m_tone;
	std::vector<SampleFrame> m_out;
};

QTEST_MAIN(SessionLibrarySaveTest)
#include "SessionLibrarySaveTest.moc"
