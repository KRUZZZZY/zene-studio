/*
 * MidiClipLinkedEditTest.cpp - the arranger's own trim of a linked MIDI clip
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <QtTest>

#include <QCoreApplication>

#include "ClipLinks.h"
#include "ClipView.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "MidiClip.h"
#include "MidiClipView.h"
#include "Note.h"
#include "Song.h"
#include "SongEditor.h"
#include "Track.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

constexpr tick_t Bar = 192;

MidiClip* midiClipAt(Track* track, tick_t start, tick_t length)
{
	auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(start)));
	clip->changeLength(TimePos(length));
	clip->setAutoResize(false);
	return clip;
}

int noteCountOf(const Clip* clip)
{
	return static_cast<int>(static_cast<const MidiClip*>(clip)->notes().size());
}

MidiClipView* viewOf(const Clip* clip)
{
	for (MidiClipView* view : getGUI()->songEditor()->m_editor->findChildren<MidiClipView*>())
	{
		if (view->getClip() == clip) { return view; }
	}
	return nullptr;
}

} // namespace

//! The arranger's trim (MidiClipView::bulkClearNotesOutOfBounds) is driven
//! through the real song editor. Its clone joins the clip's link group as it is
//! made, so before the fix the clone's clear emptied the whole group before the
//! trim read the clip's notes.
class MidiClipLinkedEditTest : public QObject
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
		Engine::getSong()->clearProject();
		m_track = Track::create(Track::Type::Instrument, Engine::getSong());
		QCoreApplication::processEvents();
	}

	void aTrimOfALinkedClipKeepsItsNotesAndItsPeersNotes()
	{
		MidiClip* a = midiClipAt(m_track, 0, Bar);
		MidiClip* b = midiClipAt(m_track, 4 * Bar, Bar);
		const int group = ClipLinks::allocateGroupId();
		a->setLinkId(group);
		b->setLinkId(group);
		// Linked before the notes, so each add mirrors onto b.
		a->addNote(Note(TimePos(24), TimePos(0), 60), false);
		a->addNote(Note(TimePos(24), TimePos(48), 64), false);
		QCOMPARE(noteCountOf(a), 2);
		QCOMPARE(noteCountOf(b), 2);

		m_editor->realignTracks();
		QCoreApplication::processEvents();
		MidiClipView* view = viewOf(a);
		QVERIFY2(view != nullptr, "the song editor built no view for the linked clip");

		// The arranger's own trim, every note inside the clip's bounds.
		MidiClipView::bulkClearNotesOutOfBounds(QVector<ClipView*>{view});
		QCoreApplication::processEvents();

		// The trimmed replacement and b are still the group, and both keep the
		// two notes: the trim neither emptied the group nor dropped its content.
		const QVector<Clip*> members = ClipLinks::group(group);
		QCOMPARE(static_cast<int>(members.size()), 2);
		for (const Clip* member : members)
		{
			QCOMPARE(noteCountOf(member), 2);
		}
		QCOMPARE(ClipLinks::contentFingerprint(members[0]), ClipLinks::contentFingerprint(members[1]));
	}

private:
	QTemporaryDir m_home;
	SongEditor* m_editor = nullptr;
	Track* m_track = nullptr;
};

QTEST_MAIN(MidiClipLinkedEditTest)
#include "MidiClipLinkedEditTest.moc"
