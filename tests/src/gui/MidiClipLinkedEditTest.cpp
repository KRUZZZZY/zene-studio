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

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QContextMenuEvent>
#include <QImage>
#include <QMenu>
#include <QMouseEvent>
#include <QTimer>

#include "ArrangementEdits.h"
#include "ClipEdits.h"
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
#include "SampleClip.h"
#include "SampleTrack.h"
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

ClipView* clipViewOf(const Clip* clip)
{
	for (ClipView* view : getGUI()->songEditor()->m_editor->findChildren<ClipView*>())
	{
		if (view->getClip() == clip) { return view; }
	}
	return nullptr;
}

QImage widgetImage(QWidget* widget)
{
	QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	widget->render(&image);
	return image;
}

int markerPixelDifferences(const QImage& linked, const QImage& unique, int width)
{
	int differences = 0;
	for (int y = 2; y < 15; ++y)
	for (int x = width - 22; x < width - 2; ++x)
	{
		if (linked.pixel(x, y) != unique.pixel(x, y)) { ++differences; }
	}
	return differences;
}

bool openContextMenuAndTrigger(QWidget* target, const QString& wantedAction)
{
	bool found = false;
	QTimer::singleShot(0, [&]() {
		auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
		if (menu == nullptr)
		{
			for (QWidget* top : QApplication::topLevelWidgets())
			{
				if (auto* candidate = qobject_cast<QMenu*>(top); candidate != nullptr && candidate->isVisible())
				{
					menu = candidate;
					break;
				}
			}
		}
		if (menu == nullptr) { return; }
		for (QAction* action : menu->actions())
		{
			if (action->text() == wantedAction)
			{
				found = true;
				action->trigger();
				break;
			}
		}
		menu->close();
	});
	QTimer::singleShot(1500, []() {
		if (QWidget* popup = QApplication::activePopupWidget()) { popup->close(); }
	});
	const QPoint at(target->width() / 2, target->height() / 2);
	QContextMenuEvent context(QContextMenuEvent::Mouse, at, target->mapToGlobal(at), Qt::NoModifier);
	QCoreApplication::sendEvent(target, &context);
	return found;
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

	void aDestructiveSplitDividesAndRebasesControllerCurves()
	{
		MidiClip* clip = midiClipAt(m_track, 0, Bar);
		clip->setControllerPoints(ControllerKind::ControlChange, 74, {{0, 0}, {96, 127}});
		m_editor->realignTracks();
		QCoreApplication::processEvents();
		MidiClipView* view = viewOf(clip);
		QVERIFY(view != nullptr);

		m_editor->setEditModeKnife();
		const QPoint at(view->width() / 4, view->height() / 2);
		QTest::mousePress(view, Qt::LeftButton, Qt::ShiftModifier, at);
		QTest::mouseRelease(view, Qt::LeftButton, Qt::ShiftModifier, at);
		m_editor->setEditModeDraw();
		QCoreApplication::processEvents();

		QCOMPARE(clipsOnTrack(), 2);
		MidiClip* left = nullptr;
		MidiClip* right = nullptr;
		for (Clip* item : m_track->getClips())
		{
			auto* midi = static_cast<MidiClip*>(item);
			if (midi->startPosition().getTicks() == 0) { left = midi; }
			else { right = midi; }
		}
		QVERIFY(left != nullptr && right != nullptr);
		QCOMPARE(right->startPosition().getTicks(), 48);

		const ControllerLane* leftLane = left->controllers().lane(ControllerKind::ControlChange, 74);
		const ControllerLane* rightLane = right->controllers().lane(ControllerKind::ControlChange, 74);
		QVERIFY(leftLane != nullptr && rightLane != nullptr);
		QCOMPARE(static_cast<int>(leftLane->points.size()), 2);
		QCOMPARE(leftLane->points[0].tick, tick_t{0});
		QCOMPARE(leftLane->points[0].value, 0);
		QCOMPARE(leftLane->points[1].tick, tick_t{48});
		QCOMPARE(leftLane->points[1].value, 64);
		QCOMPARE(static_cast<int>(rightLane->points.size()), 2);
		QCOMPARE(rightLane->points[0].tick, tick_t{0});
		QCOMPARE(rightLane->points[0].value, 64);
		QCOMPARE(rightLane->points[1].tick, tick_t{48});
		QCOMPARE(rightLane->points[1].value, 127);
	}

	void trimmingAClipCropsAndRebasesControllerCurves()
	{
		MidiClip* clip = midiClipAt(m_track, 0, Bar);
		clip->setStartTimeOffset(TimePos(-24));
		clip->setControllerPoints(ControllerKind::ControlChange, 74, {{0, 0}, {96, 127}, {240, 40}});
		m_editor->realignTracks();
		QCoreApplication::processEvents();
		MidiClipView* view = viewOf(clip);
		QVERIFY(view != nullptr);

		MidiClipView::bulkClearNotesOutOfBounds(QVector<ClipView*>{view});
		QCoreApplication::processEvents();

		QCOMPARE(clipsOnTrack(), 1);
		auto* trimmed = static_cast<MidiClip*>(m_track->getClip(0));
		const ControllerLane* lane = trimmed->controllers().lane(ControllerKind::ControlChange, 74);
		QVERIFY(lane != nullptr);
		QCOMPARE(static_cast<int>(lane->points.size()), 3);
		QCOMPARE(lane->points[0].tick, tick_t{0});
		QCOMPARE(lane->points[0].value, 32);
		QCOMPARE(lane->points[1].tick, tick_t{72});
		QCOMPARE(lane->points[1].value, 127);
		QCOMPARE(lane->points[2].tick, tick_t{192});
		QCOMPARE(lane->points[2].value, 55);
	}

	//! The owner ruling changes the former peer-compensation assertion: shared rebased notes do not rewrite a peer's window.
	void aHeadTrimmedLinkedClipLeavesThePeerWindowAlone()
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
		// The peer's source window is independent and is not moved by this instance's content trim.
		const MidiClip* peer = nullptr;
		for (const Clip* member : members)
		{
			if (member->startPosition().getTicks() == 4 * Bar) { peer = static_cast<const MidiClip*>(member); }
		}
		QVERIFY(peer != nullptr);
		QCOMPARE(peer->startTimeOffset().getTicks(), 0);
	}

	//! The 2026-10-09 owner ruling changes the former group-cut assertion: both selected pieces retain the shared content and link.
	void theKnifeCutsOnlyTheSelectedInstanceAndKeepsBothPiecesLinked()
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

		QCOMPARE(clipsOnTrack(), 3);
		const QVector<Clip*> members = ClipLinks::group(group);
		QCOMPARE(static_cast<int>(members.size()), 3);  // left, right, and untouched peer
		Clip* right = nullptr;
		for (Clip* clip : m_track->getClips())
		{
			QCOMPARE(clip->linkId(), group);
			if (clip->startPosition().getTicks() == 2 * Bar) { right = clip; }
		}
		QVERIFY(right != nullptr);
		QCOMPARE(right->length().getTicks(), 2 * Bar);
		QCOMPARE(noteCountOf(a), 2);
		QCOMPARE(noteCountOf(right), 2);
		QCOMPARE(ClipLinks::contentFingerprint(a), ClipLinks::contentFingerprint(right));
	}

	//! The owner ruling changes the former apply-to-all assertion: a left-edge drag trims only this instance.
	void aLeftEdgeDragTrimsOnlyTheSelectedWindow()
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
		QCOMPARE(b->startPosition().getTicks(), 8 * Bar);
		QCOMPARE(b->length().getTicks(), 4 * Bar);
		QCOMPARE(a->length().getTicks(), 4 * Bar - moved);
		QCOMPARE(b->startTimeOffset().getTicks(), 0);

		QCOMPARE(Engine::projectJournal()->undoDepth(), undoBefore + 1);
		Engine::projectJournal()->undo();
		QCOMPARE(a->startPosition().getTicks(), 0);
		QCOMPARE(b->startPosition().getTicks(), 8 * Bar);
		QCOMPARE(b->length().getTicks(), 4 * Bar);
	}

	//! The owner ruling changes the former apply-to-all assertion: a right-edge drag resizes only this instance.
	void aRightEdgeDragResizesOnlyTheSelectedWindow()
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

		const QPoint grab(view->width() - 2, view->height() / 2);
		const QPoint drop(grab.x() - view->width() / 4, grab.y());
		const auto send = [view](QEvent::Type type, const QPoint& where, Qt::MouseButtons buttons) {
			QMouseEvent event(type, QPointF(where), QPointF(view->mapToGlobal(where)), Qt::LeftButton, buttons, Qt::NoModifier);
			QCoreApplication::sendEvent(view, &event);
		};
		const int undoBefore = Engine::projectJournal()->undoDepth();
		send(QEvent::MouseButtonPress, grab, Qt::LeftButton);
		send(QEvent::MouseMove, drop, Qt::LeftButton);
		send(QEvent::MouseButtonRelease, drop, Qt::NoButton);
		QCoreApplication::processEvents();

		QVERIFY2(a->length().getTicks() < 4 * Bar, "the drag did not shorten the dragged clip");
		QCOMPARE(b->length().getTicks(), 4 * Bar);
		QCOMPARE(b->startPosition().getTicks(), 8 * Bar);
		QCOMPARE(Engine::projectJournal()->undoDepth(), undoBefore + 1);  // one step, not one per clip
		Engine::projectJournal()->undo();
		QCOMPARE(a->length().getTicks(), 4 * Bar);
		QCOMPARE(b->length().getTicks(), 4 * Bar);
	}

	//! A short linked peer no longer blocks the selected instance's edge drag.
	void aShortLinkedPeerDoesNotBlockTheEdgeDrag()
	{
		MidiClip* a = midiClipAt(m_track, 0, 4 * Bar);
		MidiClip* b = midiClipAt(m_track, 8 * Bar, Bar / 4);  // a quarter bar long
		const int group = ClipLinks::allocateGroupId();
		a->setLinkId(group);
		b->setLinkId(group);
		m_editor->realignTracks();
		QCoreApplication::processEvents();
		MidiClipView* view = viewOf(a);
		QVERIFY(view != nullptr);
		const QPoint grab(view->width() - 2, view->height() / 2);
		const QPoint drop(1, grab.y());  // nearly the whole clip: the peer would lose more than it has
		const auto send = [view](QEvent::Type type, const QPoint& where, Qt::MouseButtons buttons) {
			QMouseEvent event(type, QPointF(where), QPointF(view->mapToGlobal(where)), Qt::LeftButton, buttons, Qt::NoModifier);
			QCoreApplication::sendEvent(view, &event);
		};
		send(QEvent::MouseButtonPress, grab, Qt::LeftButton);
		send(QEvent::MouseMove, drop, Qt::LeftButton);
		send(QEvent::MouseButtonRelease, drop, Qt::NoButton);
		QCoreApplication::processEvents();
		QVERIFY(a->length().getTicks() < 4 * Bar);  // the selected window changed
		QCOMPARE(b->length().getTicks(), Bar / 4);   // peer window stayed independent
	}

	void linkedClipWidgetShowsMarkerAndAccessibleState()
	{
		MidiClip* a = midiClipAt(m_track, 0, Bar);
		MidiClip* b = midiClipAt(m_track, 4 * Bar, Bar);
		MidiClip* c = midiClipAt(m_track, 8 * Bar, Bar);
		for (MidiClip* clip : {a, b, c}) { clip->setName(QStringLiteral("Shared phrase")); }
		const int group = ClipLinks::allocateGroupId();
		for (MidiClip* clip : {a, b, c}) { clip->setLinkId(group); }
		a->addNote(Note(TimePos(24), TimePos(0), 60), false);
		m_editor->realignTracks();
		QCoreApplication::processEvents();
		MidiClipView* view = viewOf(b);
		QVERIFY(view != nullptr);
		QVERIFY(view->width() > 24 && view->height() > 16);
		QCOMPARE(view->accessibleName(), QStringLiteral("Linked MIDI clip: Shared phrase"));
		QVERIFY(view->accessibleDescription().contains(QStringLiteral("notes, name and colour are shared")));

		const QImage linked = widgetImage(view);
		b->setLinkId(0);
		view->update();
		QCoreApplication::processEvents();
		QCOMPARE(view->accessibleName(), QStringLiteral("MIDI clip: Shared phrase"));
		const QImage unique = widgetImage(view);
		QVERIFY2(markerPixelDifferences(linked, unique, view->width()) > 0,
			"the linked arranger widget must paint its chain marker");
		b->setLinkId(group);
		view->update();
		QCoreApplication::processEvents();
	}

	void makeUniqueContextMenuActionDetachesOnlyTheSelectedClip()
	{
		MidiClip* a = midiClipAt(m_track, 0, Bar);
		MidiClip* b = midiClipAt(m_track, 4 * Bar, Bar);
		MidiClip* c = midiClipAt(m_track, 8 * Bar, Bar);
		for (MidiClip* clip : {a, b, c}) { clip->setName(QStringLiteral("Shared phrase")); }
		const int group = ClipLinks::allocateGroupId();
		for (MidiClip* clip : {a, b, c}) { clip->setLinkId(group); }
		a->addNote(Note(TimePos(24), TimePos(0), 60), false);
		m_editor->realignTracks();
		QCoreApplication::processEvents();
		MidiClipView* view = viewOf(b);
		QVERIFY(view != nullptr);
		QVERIFY2(openContextMenuAndTrigger(view, QStringLiteral("Make Unique")),
			"the real MIDI clip context menu must expose Make Unique");
		QCOMPARE(b->linkId(), 0);
		QCOMPARE(a->linkId(), group);
		QCOMPARE(c->linkId(), group);
		QCOMPARE(noteCountOf(b), 1);
	}

	void altDragOnTheRealSampleClipWidgetUsesContainerCrossfade()
	{
		auto* track = new SampleTrack(Engine::getSong());
		auto* outer = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		auto* inner = dynamic_cast<SampleClip*>(track->createClip(TimePos(10 * Bar)));
		QVERIFY(outer != nullptr && inner != nullptr);
		outer->changeLength(TimePos(8 * Bar));
		outer->setAutoResize(false);
		inner->changeLength(TimePos(4 * Bar));
		inner->setAutoResize(false);
		ClipEdits authored;
		authored.fadeInTicks = 24;
		authored.fadeOutTicks = 32;
		inner->setClipEdits(authored);
		m_editor->realignTracks();
		QCoreApplication::processEvents();
		ClipView* view = clipViewOf(inner);
		QVERIFY(view != nullptr);
		QVERIFY(view->width() > 24);

		const QPoint grab(view->width() / 2, view->height() / 2);
		const double pixelsPerBar = static_cast<double>(view->width() - 1) * Bar / inner->length().getTicks();
		const int dx = qRound(-8.0 * pixelsPerBar);
		const QPoint drop(grab.x() + dx, grab.y());
		const auto send = [view](QEvent::Type type, const QPoint& where, Qt::MouseButton button,
			Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers) {
			QMouseEvent event(type, QPointF(where), QPointF(view->mapToGlobal(where)), button, buttons, modifiers);
			QCoreApplication::sendEvent(view, &event);
		};
		send(QEvent::MouseButtonPress, grab, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		send(QEvent::MouseMove, drop, Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
		send(QEvent::MouseButtonRelease, drop, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
		QCoreApplication::processEvents();

		QCOMPARE(inner->startPosition().getTicks(), 2 * Bar);
		QVERIFY2(outer->clipEdits().fadeOffsetGap,
			"holding Alt while dropping a contained sample clip must select the V-shaped container fade");
	}

private:
	QTemporaryDir m_home;
	SongEditor* m_editor = nullptr;
	Track* m_track = nullptr;

	int clipsOnTrack() const { return m_track->numOfClips(); }
};

QTEST_MAIN(MidiClipLinkedEditTest)
#include "MidiClipLinkedEditTest.moc"
