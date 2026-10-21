/*
 * TimelineSpeechTest.cpp - spoken timeline position, selection, and markers (Q050-080)
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

/*! Q050-080: hear the selected item, the edit cursor, the transport and the
 *  markers. Selecting and seeking says the item and its bar/beat position;
 *  play, stop, locate and scrub say the configured position; jumping to and
 *  playing across a marker says its name only when view.announcements permits. */

#include <QtTest>

#include <QMouseEvent>
#include <QTemporaryDir>

#include "AccessibleWidgets.h"
#include "ClipView.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InstrumentTrack.h"
#include "MainWindow.h"
#include "MarkerActions.h"
#include "MidiClip.h"
#include "MusicalTime.h"
#include "Note.h"
#include "PianoRoll.h"
#include "ProjectMarkers.h"
#include "ShellSelection.h"
#include "Song.h"
#include "SongEditor.h"
#include "TimelineSpeech.h"
#include "Track.h"
#include "TrackView.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

constexpr tick_t Bar = 192;

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

void setAnnouncements(const QString& level)
{
	const ControlResult result = run(QStringLiteral("view.announcements"), {{QStringLiteral("level"), level}});
	QVERIFY2(result.ok, qPrintable(result.errorMessage));
}

QString announced() { return a11y::lastAnnouncement(); }

//! Press @a widget's middle through the real application event-filter path - the same
//! MouseButtonPress ShellSelection::trackClicks sees for a GUI mouse click.
void guiClick(QWidget* target)
{
	QVERIFY(target != nullptr);
	ShellSelection::instance()->trackClicks();
	const QPoint at(target->width() / 2, target->height() / 2);
	const QPoint global = target->mapToGlobal(at);
	QMouseEvent press(QEvent::MouseButtonPress, QPointF(at), QPointF(global), Qt::LeftButton,
		Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(target, &press);
	QCoreApplication::processEvents();
}

ClipView* clipViewOf(const Clip* clip)
{
	for (ClipView* view : getGUI()->songEditor()->m_editor->findChildren<ClipView*>())
	{
		if (view->getClip() == clip) { return view; }
	}
	return nullptr;
}

TrackView* trackViewOf(const Track* track)
{
	for (TrackView* view : getGUI()->songEditor()->m_editor->trackViews())
	{
		if (view->getTrack() == track) { return view; }
	}
	return nullptr;
}

} // namespace

class TimelineSpeechTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
		getGUI()->mainWindow()->show();
		getGUI()->songEditor()->parentWidget()->show();
		QCoreApplication::processEvents();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		ShellSelection::instance()->setClip(nullptr);
		ShellSelection::instance()->setTrack(nullptr);
		QCoreApplication::processEvents();
		Engine::getSong()->clearProject();
		ProjectMarkers::instance().clear();
		QCoreApplication::processEvents();
		setAnnouncements(QStringLiteral("all"));
		QVERIFY(run(QStringLiteral("transport.time_format"), {{QStringLiteral("format"), QStringLiteral("bars_beats")}}).ok);
		QVERIFY(run(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), 0}}).ok);
		QVERIFY(run(QStringLiteral("transport.stop")).ok);
	}

	void selectionAndEditCursorAreAnnouncedWithBarBeat()
	{
		Track* track = Track::create(Track::Type::Instrument, Engine::getSong());
		QVERIFY(track != nullptr);
		track->setName(QStringLiteral("Bass"));
		Clip* clip = track->createClip(TimePos(2 * Bar));
		QVERIFY(clip != nullptr);
		clip->changeLength(TimePos(2 * Bar));
		clip->setName(QStringLiteral("Riff"));
		QCoreApplication::processEvents();
		const QString clipId = control::clipIdOf(clip);

		QVERIFY(run(QStringLiteral("window.select"), {{QStringLiteral("clip"), clipId}}).ok);
		const QString said = announced();
		QVERIFY2(said.contains(QStringLiteral("Riff")), qPrintable(said));
		QVERIFY2(said.contains(QStringLiteral("Bass")), qPrintable(said));
		QVERIFY2(said.contains(speech::positionSpeech(2 * Bar)), qPrintable(said));

		QVERIFY(run(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), 4 * Bar}}).ok);
		const QString moved = announced();
		QVERIFY2(moved.contains(speech::positionSpeech(4 * Bar)), qPrintable(moved));

		auto* midi = dynamic_cast<MidiClip*>(clip);
		QVERIFY(midi != nullptr);
		midi->addNote(Note(TimePos(48), TimePos(Bar), 60, 100), false);
		QCoreApplication::processEvents();
		QVERIFY(!midi->notes().empty());
		const Note* picked = midi->notes().front();
		QVERIFY(run(QStringLiteral("note.select"),
			{{QStringLiteral("clip"), clipId},
				{QStringLiteral("notes"), QJsonArray{control::noteIdOf(picked)}}}).ok);
		const QString notes = announced();
		QVERIFY2(notes.contains(speech::positionSpeech(Bar)), qPrintable(notes));
	}

	void guiMouseSelectionAnnouncesItemAndPositionOnce()
	{
		// Q050-080 rework: the standard GUI path (a mouse press on a clip or a track header)
		// must reach the same shared announcer as window.select, exactly once. Sending the
		// press through the application event filter fails while ShellSelection::eventFilter
		// calls setClip/setTrack directly without announcing.
		Track* bass = Track::create(Track::Type::Instrument, Engine::getSong());
		QVERIFY(bass != nullptr);
		bass->setName(QStringLiteral("Bass"));
		Track* lead = Track::create(Track::Type::Instrument, Engine::getSong());
		QVERIFY(lead != nullptr);
		lead->setName(QStringLiteral("Lead"));
		Clip* clip = bass->createClip(TimePos(2 * Bar));
		QVERIFY(clip != nullptr);
		clip->changeLength(TimePos(2 * Bar));
		clip->setName(QStringLiteral("Riff"));
		QCoreApplication::processEvents();

		guiClick(clipViewOf(clip));
		QCOMPARE(ShellSelection::instance()->clip(), clip);
		// One message carrying the item and its bar/beat position: an exact match proves no
		// second message overwrote it and no part is missing.
		QCOMPARE(announced(), speech::clipSpeech(clip));
		QVERIFY2(announced().contains(speech::positionSpeech(2 * Bar)), qPrintable(announced()));

		// The command path for the same selection speaks nothing new: the setter no-ops and
		// window.select keeps no announcement of its own, so there is no duplicate speech.
		const QString clipId = control::clipIdOf(clip);
		QVERIFY(run(QStringLiteral("window.select"), {{QStringLiteral("clip"), clipId}}).ok);
		QCOMPARE(announced(), speech::clipSpeech(clip));

		guiClick(trackViewOf(lead));
		QCOMPARE(ShellSelection::instance()->track(), lead);
		QVERIFY(ShellSelection::instance()->clip() == nullptr);
		const QString expected = QCoreApplication::translate("SelectionSpeech", "Track %1, edit cursor %2")
			.arg(QStringLiteral("Lead"),
				speech::positionSpeech(Engine::getSong()->getPlayPos().getTicks()));
		QCOMPARE(announced(), expected);
		QVERIFY2(announced().contains(QStringLiteral("Lead")), qPrintable(announced()));
	}

	void guiCursorKeysAnnounceConfiguredPosition()
	{
		// Q050-080 rework: the arranger's Left/Right/Home keys are the registered transport.seek
		// command, not a direct Song::setPlayPos, so a musician hears the new bar/beat position.
		QVERIFY(run(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), Bar}}).ok);
		SongEditor* editor = getGUI()->songEditor()->m_editor;
		QVERIFY(editor != nullptr);

		QTest::keyClick(editor, Qt::Key_Right);
		QCOMPARE(Engine::getSong()->getPlayPos(Song::PlayMode::Song).getTicks(), 2 * Bar);
		QVERIFY2(announced().contains(QStringLiteral("Playhead")), qPrintable(announced()));
		QVERIFY2(announced().contains(speech::positionSpeech(2 * Bar)), qPrintable(announced()));

		QTest::keyClick(editor, Qt::Key_Left);
		QCOMPARE(Engine::getSong()->getPlayPos(Song::PlayMode::Song).getTicks(), Bar);
		QVERIFY2(announced().contains(speech::positionSpeech(Bar)), qPrintable(announced()));

		QTest::keyClick(editor, Qt::Key_Home);
		QCOMPARE(Engine::getSong()->getPlayPos(Song::PlayMode::Song).getTicks(), 0);
		QVERIFY2(announced().contains(speech::positionSpeech(0)), qPrintable(announced()));
	}

	void pianoRollHomeAnnouncesConfiguredPosition()
	{
		// The note editor's Home key is the same registered seek, so it speaks the position too.
		QVERIFY(run(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), 2 * Bar}}).ok);
		PianoRollWindow window;
		auto* roll = window.findChild<PianoRoll*>();
		QVERIFY(roll != nullptr);
		QTest::keyClick(roll, Qt::Key_Home);
		QCOMPARE(Engine::getSong()->getPlayPos().getTicks(), 0);
		QVERIFY2(announced().contains(QStringLiteral("Playhead")), qPrintable(announced()));
		QVERIFY2(announced().contains(speech::positionSpeech(0)), qPrintable(announced()));
	}

	void transportStartStopLocateScrubAnnouncesConfiguredPosition()
	{
		QVERIFY(run(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), 0}}).ok);
		QVERIFY(announced().contains(speech::positionSpeech(0)));

		const ControlResult played = run(QStringLiteral("transport.play"));
		QVERIFY2(played.ok, qPrintable(played.errorMessage));
		QVERIFY2(announced().contains(QStringLiteral("Playing")), qPrintable(announced()));
		QVERIFY2(announced().contains(speech::positionSpeech(Engine::getSong()->getPlayPos().getTicks())),
			qPrintable(announced()));

		QVERIFY(run(QStringLiteral("transport.stop")).ok);
		QVERIFY2(announced().contains(QStringLiteral("Stopped")), qPrintable(announced()));

		QVERIFY(run(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), Bar}}).ok);
		QVERIFY2(announced().contains(speech::positionSpeech(Bar)), qPrintable(announced()));
		QVERIFY(run(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), Bar + 10}}).ok);
		QVERIFY2(announced().contains(speech::positionSpeech(Bar + 10)), qPrintable(announced()));
		QVERIFY(run(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), Bar + 20}}).ok);
		QVERIFY2(announced().contains(speech::positionSpeech(Bar + 20)), qPrintable(announced()));

		QVERIFY(run(QStringLiteral("transport.time_format"), {{QStringLiteral("format"), QStringLiteral("ticks")}}).ok);
		QVERIFY(run(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), 2 * Bar}}).ok);
		QVERIFY2(announced().contains(QStringLiteral("ticks")), qPrintable(announced()));
		QVERIFY2(announced().contains(speech::positionSpeech(2 * Bar)), qPrintable(announced()));
		QVERIFY(run(QStringLiteral("transport.time_format"), {{QStringLiteral("format"), QStringLiteral("bars_beats")}}).ok);
	}

	void markerJumpAndCrossingAnnounceNameOnlyWhenPermitted()
	{
		const int first = ProjectMarkers::instance().add(Bar, QStringLiteral("Verse"), QString());
		const int second = ProjectMarkers::instance().add(2 * Bar, QStringLiteral("Chorus"), QString());
		QVERIFY(first >= 0);
		QVERIFY(second >= 0);

		QVERIFY(run(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), 0}}).ok);
		QVERIFY(run(QStringLiteral("marker.jump"), {{QStringLiteral("direction"), QStringLiteral("next")}}).ok);
		QVERIFY2(announced().contains(QStringLiteral("Verse")), qPrintable(announced()));
		QVERIFY2(announced().contains(speech::positionSpeech(Bar)), qPrintable(announced()));

		setAnnouncements(QStringLiteral("errors"));
		const QString before = announced();
		QVERIFY(run(QStringLiteral("marker.jump"), {{QStringLiteral("direction"), QStringLiteral("next")}}).ok);
		QCOMPARE(Engine::getSong()->getPlayPos().getTicks(), 2 * Bar);
		QCOMPARE(announced(), before);

		setAnnouncements(QStringLiteral("all"));
		QVERIFY(run(QStringLiteral("marker.jump"), {{QStringLiteral("direction"), QStringLiteral("previous")}}).ok);
		QVERIFY2(announced().contains(QStringLiteral("Verse")), qPrintable(announced()));

		markeractions::advance(0, false);
		markeractions::advance(10, true);
		QVERIFY(run(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), 10}}).ok);
		setAnnouncements(QStringLiteral("all"));
		markeractions::advance(Bar + 10, true);
		QVERIFY2(announced().contains(QStringLiteral("Verse")), qPrintable(announced()));

		setAnnouncements(QStringLiteral("errors"));
		const QString held = announced();
		markeractions::advance(0, false);
		markeractions::advance(10, true);
		markeractions::advance(2 * Bar + 10, true);
		QCOMPARE(announced(), held);
		setAnnouncements(QStringLiteral("all"));
	}

	void markerCrossingKeepsItsNameWhenAnActionReplacesTheProject()
	{
		const int id = ProjectMarkers::instance().add(Bar, QStringLiteral("Old project marker"), QString());
		QVERIFY(id >= 0);
		Marker marker = *ProjectMarkers::instance().find(id);
		marker.action = QStringLiteral("project.new");
		QVERIFY(ProjectMarkers::instance().replace(id, marker));

		markeractions::advance(0, false);
		markeractions::advance(10, true);
		markeractions::advance(Bar + 10, true);
		QVERIFY(ProjectMarkers::instance().markers().empty());
		QVERIFY2(announced().contains(QStringLiteral("Old project marker")), qPrintable(announced()));
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(TimelineSpeechTest)
#include "TimelineSpeechTest.moc"
