/*
 * ArrangerClipboardKeysTest.cpp - the arranger's clipboard keys (Ctrl+C, Ctrl+X, Ctrl+V)
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

/*! Cluster gui-1106: a clip copied in the arranger (its context menu's Copy, or Ctrl+C) is pasted by Ctrl+V at the
 *  play position, on the selected clip's track (else the track it was copied from); Ctrl+X cuts. The keys go through
 *  the arranger's own key handler, the path a key press in the focused arranger takes. The copy can be deleted
 *  with Delete and the original stays. A clipboard holding no clips leaves the song as it is. */

#include <QtTest>

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QTemporaryDir>

#include "Clip.h"
#include "ClipView.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "ShellSelection.h"
#include "Song.h"
#include "SongEditor.h"
#include "Track.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

constexpr tick_t Bar = 192;

std::vector<tick_t> startsOn(const Track* track)
{
	std::vector<tick_t> starts;
	for (const Clip* clip : track->getClips()) { starts.push_back(clip->startPosition().getTicks()); }
	std::sort(starts.begin(), starts.end());
	return starts;
}

} // namespace

class ArrangerClipboardKeysTest : public QObject
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
		m_editor = getGUI()->songEditor()->m_editor;
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
		QCoreApplication::processEvents();
		QApplication::clipboard()->clear();
	}

	//! The finding's path: the context menu's Copy, then Ctrl+V; then the copy is deleted and the original stays.
	void ctrlVPastesACopiedClipAtThePlayPosition()
	{
		Track* track = newTrack();
		Clip* original = clipAt(track, 0, Bar);
		ClipView* view = viewOf(original);
		QVERIFY(view != nullptr);
		view->copy({view});  // what the context menu's Copy does
		m_editor->selectAllClips(false);
		Engine::getSong()->setPlayPos(4 * Bar, Song::PlayMode::Song);

		press(Qt::Key_V, Qt::ControlModifier);
		QCOMPARE(startsOn(track), (std::vector<tick_t>{0, 4 * Bar}));

		// Delete the copy: the original stays where it was.
		Clip* copy = clipStartingAt(track, 4 * Bar);
		QVERIFY(copy != nullptr && copy != original);
		m_editor->selectAllClips(false);
		viewOf(copy)->setSelected(true);
		press(Qt::Key_Delete, Qt::NoModifier);
		QCoreApplication::processEvents();
		QCOMPARE(startsOn(track), (std::vector<tick_t>{0}));
		QCOMPARE(track->getClips().front(), original);
	}

	//! Ctrl+C copies the selection; Ctrl+V lands on the selected clip's track, even when the copy came from another.
	void ctrlCCopiesAndCtrlVFollowsTheSelectedTrack()
	{
		Track* first = newTrack();
		Track* second = newTrack();
		Clip* source = clipAt(first, 0, Bar);
		Clip* target = clipAt(second, 8 * Bar, Bar);
		m_editor->selectAllClips(false);
		viewOf(source)->setSelected(true);
		press(Qt::Key_C, Qt::ControlModifier);
		QCOMPARE(startsOn(first), (std::vector<tick_t>{0}));  // a copy changes nothing

		m_editor->selectAllClips(false);
		viewOf(target)->setSelected(true);
		Engine::getSong()->setPlayPos(2 * Bar, Song::PlayMode::Song);
		press(Qt::Key_V, Qt::ControlModifier);
		QCOMPARE(startsOn(first), (std::vector<tick_t>{0}));
		QCOMPARE(startsOn(second), (std::vector<tick_t>{2 * Bar, 8 * Bar}));
	}

	//! Ctrl+X takes the clip out; Ctrl+V with nothing selected puts it back on the track it came from.
	void ctrlXCutsAndCtrlVPutsItBack()
	{
		Track* track = newTrack();
		Clip* clip = clipAt(track, 2 * Bar, Bar);
		m_editor->selectAllClips(false);
		viewOf(clip)->setSelected(true);
		press(Qt::Key_X, Qt::ControlModifier);
		QCoreApplication::processEvents();
		QVERIFY(track->getClips().empty());

		Engine::getSong()->setPlayPos(6 * Bar, Song::PlayMode::Song);
		press(Qt::Key_V, Qt::ControlModifier);
		QCOMPARE(startsOn(track), (std::vector<tick_t>{6 * Bar}));
	}

	//! A clipboard holding text (no clips) is not pasted, and nothing else happens either.
	void ctrlVWithoutCopiedClipsChangesNothing()
	{
		Track* track = newTrack();
		clipAt(track, 0, Bar);
		QApplication::clipboard()->setText(QStringLiteral("not a clip"));
		Engine::getSong()->setPlayPos(4 * Bar, Song::PlayMode::Song);
		press(Qt::Key_V, Qt::ControlModifier);
		QCOMPARE(startsOn(track), (std::vector<tick_t>{0}));
	}

private:
	Track* newTrack()
	{
		Track* t = Track::create(Track::Type::Instrument, Engine::getSong());
		QCoreApplication::processEvents();
		return t;
	}

	static Clip* clipAt(Track* track, tick_t start, tick_t length)
	{
		Clip* clip = track->createClip(TimePos(start));
		clip->changeLength(TimePos(length));
		clip->setAutoResize(false);
		QCoreApplication::processEvents();
		return clip;
	}

	static Clip* clipStartingAt(const Track* track, tick_t start)
	{
		for (Clip* clip : track->getClips())
		{
			if (clip->startPosition().getTicks() == start) { return clip; }
		}
		return nullptr;
	}

	ClipView* viewOf(const Clip* clip) const
	{
		for (ClipView* view : m_editor->findChildren<ClipView*>())
		{
			if (view->getClip() == clip) { return view; }
		}
		return nullptr;
	}

	//! A key press delivered to the arranger, as the focused arranger receives it.
	void press(int key, Qt::KeyboardModifiers mods)
	{
		QKeyEvent event(QEvent::KeyPress, key, mods);
		QApplication::sendEvent(m_editor, &event);
		QCoreApplication::processEvents();
	}

	QTemporaryDir m_home;
	SongEditor* m_editor = nullptr;
};

QTEST_MAIN(ArrangerClipboardKeysTest)
#include "ArrangerClipboardKeysTest.moc"
