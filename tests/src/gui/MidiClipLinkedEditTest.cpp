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
#include <QMouseEvent>

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
#include "ProjectJournal.h"
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

	//! The arranger's trim of content reaches every member: a note that crosses the clip's end is
	//! cut in the peers too, so the group still agrees.
	void aTrimOfLinkedContentIsTheGroupsContent()
	{
		MidiClip* a = midiClipAt(m_track, 0, Bar);
		MidiClip* b = midiClipAt(m_track, 4 * Bar, Bar);
		const int group = ClipLinks::allocateGroupId();
		a->setLinkId(group);
		b->setLinkId(group);
		a->addNote(Note(TimePos(24), TimePos(0), 60), false);
		a->addNote(Note(TimePos(48), TimePos(Bar - 12), 64), false);  // 48 ticks long from tick 180: crosses the end at 192
		QCOMPARE(noteCountOf(b), 2);

		m_editor->realignTracks();
		QCoreApplication::processEvents();
		MidiClipView* view = viewOf(a);
		QVERIFY(view != nullptr);
		MidiClipView::bulkClearNotesOutOfBounds(QVector<ClipView*>{view});
		QCoreApplication::processEvents();

		const QVector<Clip*> members = ClipLinks::group(group);
		QCOMPARE(static_cast<int>(members.size()), 2);
		for (const Clip* member : members)
		{
			const auto& notes = static_cast<const MidiClip*>(member)->notes();
			QCOMPARE(static_cast<int>(notes.size()), 2);
			QCOMPARE(notes.back()->length().getTicks(), 12);  // 48 long from 180, cut at 192
		}
		QCOMPARE(ClipLinks::contentFingerprint(members[0]), ClipLinks::contentFingerprint(members[1]));
	}

	//! A clip trimmed on the left bakes its offset into the notes; a peer's offset moves by the same
	//! amount, so the peer plays every note where it did.
	void aHeadTrimmedLinkedClipMovesItsPeersOffsetWithTheNotes()
	{
		MidiClip* a = midiClipAt(m_track, 0, Bar);
		MidiClip* b = midiClipAt(m_track, 4 * Bar, Bar);
		const int group = ClipLinks::allocateGroupId();
		a->setLinkId(group);
		b->setLinkId(group);
		a->addNote(Note(TimePos(24), TimePos(24), 60), false);
		a->addNote(Note(TimePos(24), TimePos(72), 64), false);
		a->setStartTimeOffset(TimePos(-24));
		m_editor->realignTracks();
		QCoreApplication::processEvents();
		MidiClipView* view = viewOf(a);
		QVERIFY(view != nullptr);
		MidiClipView::bulkClearNotesOutOfBounds(QVector<ClipView*>{view});
		QCoreApplication::processEvents();

		const QVector<Clip*> members = ClipLinks::group(group);
		QCOMPARE(static_cast<int>(members.size()), 2);
		for (const Clip* member : members)
		{
			const auto* midi = static_cast<const MidiClip*>(member);
			QCOMPARE(static_cast<int>(midi->notes().size()), 2);
			// The head 24 ticks were baked in: the note that sat at 24 now sits at 0 (a clip played
			// from offset -24 meant it started at the clip's first tick).
			QCOMPARE(midi->notes().front()->pos().getTicks(), 0);
		}
		QCOMPARE(ClipLinks::contentFingerprint(members[0]), ClipLinks::contentFingerprint(members[1]));
		// The peer never had an offset; it takes the baked 24 so the shifted notes play where they did.
		const MidiClip* peer = nullptr;
		for (const Clip* member : members)
		{
			if (member->startPosition().getTicks() == 4 * Bar) { peer = static_cast<const MidiClip*>(member); }
		}
		QVERIFY(peer != nullptr);
		QCOMPARE(peer->startTimeOffset().getTicks(), 24);
	}

	//! The knife (shift = destructive) on one member cuts every member at the same position.
	void theKnifeCutsEveryLinkedMember()
	{
		MidiClip* a = midiClipAt(m_track, 0, 4 * Bar);
		MidiClip* b = midiClipAt(m_track, 8 * Bar, 4 * Bar);
		const int group = ClipLinks::allocateGroupId();
		a->setLinkId(group);
		b->setLinkId(group);
		a->addNote(Note(TimePos(24), TimePos(0), 60), false);
		a->addNote(Note(TimePos(24), TimePos(3 * Bar), 64), false);
		m_editor->realignTracks();
		QCoreApplication::processEvents();
		MidiClipView* view = viewOf(a);
		QVERIFY(view != nullptr);

		m_editor->setEditModeKnife();
		const QPoint at(view->width() / 2, view->height() / 2);
		QTest::mousePress(view, Qt::LeftButton, Qt::ShiftModifier, at);
		QTest::mouseRelease(view, Qt::LeftButton, Qt::ShiftModifier, at);
		m_editor->setEditModeDraw();
		QCoreApplication::processEvents();

		QCOMPARE(clipsOnTrack(), 4);
		const QVector<Clip*> lefts = ClipLinks::group(group);
		QCOMPARE(static_cast<int>(lefts.size()), 2);
		QCOMPARE(lefts[0]->length(), lefts[1]->length());
		int rightGroup = 0;
		for (const Clip* clip : m_track->getClips())
		{
			if (clip->linkId() != group) { rightGroup = clip->linkId(); }
		}
		QVERIFY(rightGroup > 0);
		const QVector<Clip*> rights = ClipLinks::group(rightGroup);
		QCOMPARE(static_cast<int>(rights.size()), 2);
		QCOMPARE(rights[0]->length(), rights[1]->length());
		// Whole content in every half: nothing was lost, so each group agrees inside itself.
		QCOMPARE(ClipLinks::contentFingerprint(lefts[0]), ClipLinks::contentFingerprint(lefts[1]));
		QCOMPARE(ClipLinks::contentFingerprint(rights[0]), ClipLinks::contentFingerprint(rights[1]));
	}

	//! Dragging one member's left edge trims every member; one undo puts them all back.
	void aLeftEdgeDragTrimsEveryLinkedMember()
	{
		MidiClip* a = midiClipAt(m_track, 0, 4 * Bar);
		MidiClip* b = midiClipAt(m_track, 8 * Bar, 4 * Bar);
		const int group = ClipLinks::allocateGroupId();
		a->setLinkId(group);
		b->setLinkId(group);
		m_editor->realignTracks();
		QCoreApplication::processEvents();
		MidiClipView* view = viewOf(a);
		QVERIFY(view != nullptr);

		const QPoint grab(1, view->height() / 2);
		const QPoint drop(grab.x() + view->width() / 4, grab.y());
		const auto send = [view](QEvent::Type type, const QPoint& where, Qt::MouseButtons buttons) {
			QMouseEvent event(type, QPointF(where), QPointF(view->mapToGlobal(where)), Qt::LeftButton, buttons, Qt::NoModifier);
			QCoreApplication::sendEvent(view, &event);
		};
		const int undoBefore = Engine::projectJournal()->undoDepth();
		send(QEvent::MouseButtonPress, grab, Qt::LeftButton);
		send(QEvent::MouseMove, drop, Qt::LeftButton);
		send(QEvent::MouseButtonRelease, drop, Qt::NoButton);
		QCoreApplication::processEvents();

		const int moved = a->startPosition().getTicks();
		QVERIFY2(moved > 0, "the drag did not move the dragged clip's left edge");
		QCOMPARE(b->startPosition().getTicks(), 8 * Bar + moved);
		QCOMPARE(b->length().getTicks(), 4 * Bar - moved);
		QCOMPARE(a->length().getTicks(), 4 * Bar - moved);
		QCOMPARE(b->startTimeOffset(), a->startTimeOffset());

		QVERIFY(Engine::projectJournal()->undoDepth() >= undoBefore + 1);
		Engine::projectJournal()->undo();
		QCOMPARE(a->startPosition().getTicks(), 0);
		QCOMPARE(b->startPosition().getTicks(), 8 * Bar);
		QCOMPARE(b->length().getTicks(), 4 * Bar);
	}

private:
	QTemporaryDir m_home;
	SongEditor* m_editor = nullptr;
	Track* m_track = nullptr;

	int clipsOnTrack() const { return m_track->numOfClips(); }
};

QTEST_MAIN(MidiClipLinkedEditTest)
#include "MidiClipLinkedEditTest.moc"
