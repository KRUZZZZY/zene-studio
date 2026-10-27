/*
 * ItemListPanelTest.cpp - the editable item list, the staged accessibility audit and the announcement policy
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

/*! Theme 10: the list beside a canvas is a synchronised, fully editable equivalent - its rows are the canvas's items,
 *  a typed cell, Insert and Delete are each the canvas's command and one undo step, a refusal is shown, and the
 *  selection follows both ways. Theme 17: the audit runs its checks separately (duplicate names among them), and
 *  announcements follow the level, the repeat window and the song's transitions. */

#include <stdexcept>

#include <QtTest>

#include <QJsonArray>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>

#include "AccessibilityAudit.h"
#include "AccessibleWidgets.h"
#include "CompSwipeStrip.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "ItemList.h"
#include "MidiClip.h"
#include "MusicalTime.h"
#include "Note.h"
#include "NoteCanvas.h"
#include "NoteEditor.h"
#include "NoteGrid.h"
#include "NotePlayHandle.h"
#include "Song.h"
#include "../../../src/gui/ValueAnnouncementState.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult invoke(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

bool undo() { return invoke(QStringLiteral("control.undo")).ok; }

Note* noteAt(MidiClip* clip, int position, int key)
{
	for (Note* note : clip->notes())
	{
		if (note->pos().getTicks() == position && note->key() == key) { return note; }
	}
	return nullptr;
}

QString cell(ItemListPanel* panel, int row, int column)
{
	return panel->table()->model()->index(row, column).data().toString();
}

bool edit(ItemListPanel* panel, int row, int column, const QString& text)
{
	QAbstractItemModel* model = panel->table()->model();
	return model->setData(model->index(row, column), text, Qt::EditRole);
}

} // namespace

class ItemListPanelTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		qputenv("HOME", m_home.path().toLocal8Bit());
		ConfigManager::inst()->loadConfigFile(m_home.filePath(QStringLiteral("zene.xml")));
		Engine::init(true);
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(m_track != nullptr);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		m_clip = dynamic_cast<MidiClip*>(m_track->createClip(TimePos(0)));
		QVERIFY(m_clip != nullptr);
		m_clip->addNote(Note(TimePos(48), TimePos(0), 60, 100), false);
		m_clip->addNote(Note(TimePos(48), TimePos(0), 64, 100), false);
		m_clip->addNote(Note(TimePos(96), TimePos(192), 62, 100), false);
		m_clip->rearrangeAllNotes();
		m_editor = new NoteEditor;
		m_editor->resize(1000, 600);
		m_editor->show();
		QVERIFY(QTest::qWaitForWindowExposed(m_editor));
		m_editor->setClip(m_clip);
		m_editor->setGridTicks(12);
		m_panel = ItemListPanel::open(m_editor->canvas(), QStringLiteral("notes"),
			[this](QObject* parent) { return itemlists::notes(m_editor, parent); });
		m_panel->show();
	}

	void cleanup()
	{
		delete m_editor;  // the list goes with the canvas it lists
		m_editor = nullptr;
		m_panel = nullptr;
		delete m_clip;
		m_clip = nullptr;
	}

	void theNoteListMirrorsTheClip()
	{
		QCOMPARE(m_panel->table()->model()->rowCount(), 3);
		QCOMPARE(m_panel->table()->model()->columnCount(), 4);
		QCOMPARE(cell(m_panel, 0, 1), NoteGrid::keyName(60));  // start, then pitch
		QCOMPARE(cell(m_panel, 1, 1), NoteGrid::keyName(64));
		QCOMPARE(cell(m_panel, 2, 1), NoteGrid::keyName(62));
		QCOMPARE(cell(m_panel, 2, 3), QStringLiteral("100"));
		// Changed elsewhere (here: the socket), the list re-reads.
		QVERIFY(invoke(QStringLiteral("note.edit"), {{QStringLiteral("clip"), m_editor->clipId()}, {QStringLiteral("edits"),
			QJsonArray{QJsonObject{{QStringLiteral("note"), control::noteIdOf(noteAt(m_clip, 192, 62))}, {QStringLiteral("velocity"), 80}}}}}).ok);
		QTRY_COMPARE(cell(m_panel, 2, 3), QStringLiteral("80"));
		// The expression lane's value is a column while the lane shows one.
		m_editor->setLane(NoteLane::Pan);
		QTRY_COMPARE(m_panel->table()->model()->columnCount(), 5);
	}

	void aTypedCellIsOneEditAndOneStep()
	{
		QVERIFY(edit(m_panel, 2, 3, QStringLiteral("90")));
		QCOMPARE(static_cast<int>(noteAt(m_clip, 192, 62)->getVolume()), 90);
		QVERIFY(edit(m_panel, 2, 1, QStringLiteral("72")));  // a pitch typed as a number
		QVERIFY(noteAt(m_clip, 192, 72) != nullptr);
		QVERIFY(undo());
		QVERIFY(noteAt(m_clip, 192, 62) != nullptr);
		QVERIFY(undo());
		QCOMPARE(static_cast<int>(noteAt(m_clip, 192, 62)->getVolume()), 100);
		// Text that is no value changes nothing and says so.
		QVERIFY(!edit(m_panel, 2, 3, QStringLiteral("loud")));
		QTRY_VERIFY(m_panel->statusText().startsWith(QStringLiteral("Not done")));
	}

	void numericEditsRejectPercentCharacters()
	{
		QVERIFY(!edit(m_panel, 2, 3, QStringLiteral("1%2")));
		QCOMPARE(static_cast<int>(noteAt(m_clip, 192, 62)->getVolume()), 100);

		int parsed = -1;
		QVERIFY(ItemListSource::parse(ItemColumn::Kind::Percent, QStringLiteral("42 %"), &parsed));
		QCOMPARE(parsed, 42);
		QVERIFY(!ItemListSource::parse(ItemColumn::Kind::Number, QStringLiteral("42%"), &parsed));
		QVERIFY(!ItemListSource::parse(ItemColumn::Kind::Percent, QStringLiteral("4%%"), &parsed));
		QVERIFY(!ItemListSource::parse(ItemColumn::Kind::Percent, QStringLiteral("4%2"), &parsed));
	}

	void insertAndDeleteAreOneStepEach()
	{
		m_panel->table()->setCurrentIndex(m_panel->table()->model()->index(2, 0));
		QVERIFY(m_panel->insertItem());
		QCOMPARE(m_clip->notes().size(), std::size_t{4});
		QVERIFY(noteAt(m_clip, 288, 62) != nullptr);  // after the current note, on its key
		QVERIFY(undo());
		QCOMPARE(m_clip->notes().size(), std::size_t{3});
		m_panel->reload();
		m_panel->table()->selectRow(0);
		QVERIFY(m_panel->removeItems());
		QCOMPARE(m_clip->notes().size(), std::size_t{2});
		QVERIFY(noteAt(m_clip, 0, 60) == nullptr);
		QVERIFY(undo());
		QCOMPARE(m_clip->notes().size(), std::size_t{3});
	}

	void theClipListDeletesEverySelectedClip()
	{
		constexpr int firstPosition = 384;
		constexpr int secondPosition = 768;
		Clip* first = m_track->createClip(TimePos(firstPosition));
		Clip* second = m_track->createClip(TimePos(secondPosition));
		QVERIFY(first != nullptr);
		QVERIFY(second != nullptr);
		ItemListPanel* clips = ItemListPanel::open(m_editor, QStringLiteral("clips"),
			[](QObject* parent) { return itemlists::clips(parent); });
		const bool multipleSelectionAvailable = clips->table()->selectionMode() == QAbstractItemView::ExtendedSelection;
		const QString firstId = control::clipIdOf(first);
		const QString secondId = control::clipIdOf(second);
		const auto rowFor = [clips](const QString& id) {
			for (int row = 0; row < clips->source()->rowCount(); ++row)
			{
				if (clips->source()->rowId(row) == id) { return row; }
			}
			return -1;
		};
		const int firstRow = rowFor(firstId);
		const int secondRow = rowFor(secondId);
		const bool rowsFound = firstRow >= 0 && secondRow >= 0 && firstRow != secondRow;
		if (rowsFound)
		{
			QItemSelection selection;
			for (const int row : {firstRow, secondRow})
			{
				selection.select(clips->table()->model()->index(row, 0),
					clips->table()->model()->index(row, clips->table()->model()->columnCount() - 1));
			}
			clips->table()->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
		}
		const bool selectedBoth = clips->table()->selectionModel()->selectedRows().size() == 2;
		const bool removed = selectedBoth && clips->removeItems();
		const auto hasRow = [clips](const QString& id) {
			for (int row = 0; row < clips->source()->rowCount(); ++row)
			{
				if (clips->source()->rowId(row) == id) { return true; }
			}
			return false;
		};
		const bool firstRemains = hasRow(firstId);
		const bool secondRemains = hasRow(secondId);
		bool undoOk = true;
		for (int deleted = 0; deleted < static_cast<int>(!firstRemains) + static_cast<int>(!secondRemains); ++deleted)
		{
			undoOk = undo() && undoOk;
		}
		const auto clipAt = [this](int position) -> Clip* {
			for (Clip* clip : m_track->getClips())
			{
				if (clip->startPosition().getTicks() == position) { return clip; }
			}
			return nullptr;
		};
		// Undo restores the track from its checkpoint (Track::restoreState re-loads the clips as new objects), so the
		// fixture's clip is the restored object now at its start; deleting the old pointer would be a use-after-free.
		if (Clip* restored = clipAt(0)) { m_clip = dynamic_cast<MidiClip*>(restored); }
		const auto discardAt = [this, clipAt](int position) {
			if (Clip* clip = clipAt(position))
			{
				m_track->removeClip(clip);
				delete clip;
			}
		};
		discardAt(firstPosition);
		discardAt(secondPosition);
		QVERIFY(rowsFound);
		QVERIFY(selectedBoth);
		QVERIFY(removed);
		QVERIFY(!firstRemains);
		QVERIFY(!secondRemains);
		QVERIFY(undoOk);
		QVERIFY(multipleSelectionAvailable);
	}

	void insertRefusesOneTickCompSegment()
	{
		const QString track = invoke(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}})
			.result.value(QStringLiteral("track")).toString();
		QVERIFY(!track.isEmpty());
		QVERIFY(invoke(QStringLiteral("comp.lane_add"), {{QStringLiteral("track"), track}}).ok);
		QVERIFY(invoke(QStringLiteral("comp.lane_add"), {{QStringLiteral("track"), track}}).ok);
		QVERIFY(invoke(QStringLiteral("comp.select"), {{QStringLiteral("track"), track}, {QStringLiteral("begin"), 10},
			{QStringLiteral("end"), 11}, {QStringLiteral("lane"), 0}}).ok);

		CompSwipeStrip strip(track, nullptr);
		strip.setState(invoke(QStringLiteral("comp.get_state"), {{QStringLiteral("track"), track}}).result);
		ItemListPanel panel(itemlists::compSegments(&strip, nullptr), &strip, QStringLiteral("comp"));
		QCOMPARE(panel.table()->model()->rowCount(), 1);
		panel.table()->setCurrentIndex(panel.table()->model()->index(0, 0));

		QVERIFY(!panel.insertItem());
		QVERIFY(panel.statusText().startsWith(QStringLiteral("Not done:")));
		const QJsonArray segments = invoke(QStringLiteral("comp.get_state"), {{QStringLiteral("track"), track}}).result
			.value(QStringLiteral("composite")).toObject().value(QStringLiteral("segments")).toArray();
		QCOMPARE(segments.size(), 1);
		QCOMPARE(segments.first().toObject().value(QStringLiteral("begin")).toInt(), 10);
		QCOMPARE(segments.first().toObject().value(QStringLiteral("end")).toInt(), 11);
		QCOMPARE(segments.first().toObject().value(QStringLiteral("lane")).toInt(), 0);
	}

	void aNegativeCompRowIsRefusedNotIndexed()
	{
		const QString track = invoke(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}})
			.result.value(QStringLiteral("track")).toString();
		QVERIFY(!track.isEmpty());
		QVERIFY(invoke(QStringLiteral("comp.lane_add"), {{QStringLiteral("track"), track}}).ok);
		QVERIFY(invoke(QStringLiteral("comp.lane_add"), {{QStringLiteral("track"), track}}).ok);
		QVERIFY(invoke(QStringLiteral("comp.select"), {{QStringLiteral("track"), track}, {QStringLiteral("begin"), 10},
			{QStringLiteral("end"), 11}, {QStringLiteral("lane"), 0}}).ok);

		CompSwipeStrip strip(track, nullptr);
		strip.setState(invoke(QStringLiteral("comp.get_state"), {{QStringLiteral("track"), track}}).result);
		ItemListSource* source = itemlists::compSegments(&strip, &strip);
		const int rows = source->rowCount();
		QVERIFY(rows >= 1);
		const QJsonObject before = invoke(QStringLiteral("comp.get_state"), {{QStringLiteral("track"), track}}).result;

		bool accepted = true;
		try { accepted = source->setValue(-1, 0, 5); }
		catch (const std::out_of_range&) { QFAIL("a negative row reached the segment list and threw out_of_range"); }
		QVERIFY(!accepted);
		QCOMPARE(source->rowCount(), rows);
		QVERIFY(invoke(QStringLiteral("comp.get_state"), {{QStringLiteral("track"), track}}).result == before);
	}

	void theSelectionFollowsBothWays()
	{
		m_panel->table()->selectRow(1);
		QVERIFY(noteAt(m_clip, 0, 64)->selected());
		QVERIFY(!noteAt(m_clip, 0, 60)->selected());
		QVERIFY(invoke(QStringLiteral("note.select"), {{QStringLiteral("clip"), m_editor->clipId()},
			{QStringLiteral("notes"), QJsonArray{control::noteIdOf(noteAt(m_clip, 192, 62))}}}).ok);
		m_panel->reload();
		const QModelIndexList rows = m_panel->table()->selectionModel()->selectedRows();
		QCOMPARE(rows.size(), 1);
		QCOMPARE(rows.first().row(), 2);
	}

	void ctrlClickClearsTheOnlySelectedRow()
	{
		QTableView* table = m_panel->table();
		const QModelIndex index = table->model()->index(1, 0);
		table->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
		table->selectRow(1);
		QVERIFY(noteAt(m_clip, 0, 64)->selected());

		QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::ControlModifier, table->visualRect(index).center());

		QTRY_VERIFY(!noteAt(m_clip, 0, 64)->selected());
		QVERIFY(table->selectionModel()->selectedRows().isEmpty());
	}

	void theControllerListEditsTheLane()
	{
		m_editor->setControllerLane(QStringLiteral("cc:7"));
		ItemListPanel* points = ItemListPanel::open(m_editor, QStringLiteral("controller"),
			[this](QObject* parent) { return itemlists::controllerPoints(m_editor, parent); });
		QCOMPARE(points->table()->model()->rowCount(), 0);
		QVERIFY(points->insertItem());
		points->reload();
		QCOMPARE(points->table()->model()->rowCount(), 1);
		QVERIFY(edit(points, 0, 1, QStringLiteral("100")));
		const ControllerLane* lane = m_clip->controllers().lane(ControllerKind::ControlChange, 7);
		QVERIFY(lane != nullptr);
		QCOMPARE(lane->points.front().value, 100);
		points->reload();
		points->table()->selectRow(0);
		QVERIFY(points->removeItems());
		QVERIFY(m_clip->controllers().lane(ControllerKind::ControlChange, 7) == nullptr);  // no points: no lane
		QVERIFY(undo());
		QVERIFY(m_clip->controllers().lane(ControllerKind::ControlChange, 7) != nullptr);
	}

	void aControllerPositionCollisionDoesNotDiscardEitherPoint()
	{
		m_editor->setControllerLane(QStringLiteral("cc:7"));
		const QJsonArray initial{QJsonObject{{QStringLiteral("tick"), 0}, {QStringLiteral("value"), 10}},
			QJsonObject{{QStringLiteral("tick"), 12}, {QStringLiteral("value"), 100}}};
		QVERIFY(invoke(QStringLiteral("clip.controllers_set"), {{QStringLiteral("clip"), m_editor->clipId()},
			{QStringLiteral("lane"), QStringLiteral("cc:7")}, {QStringLiteral("points"), initial}}).ok);
		ItemListPanel* points = ItemListPanel::open(m_editor, QStringLiteral("controller"),
			[this](QObject* parent) { return itemlists::controllerPoints(m_editor, parent); });
		QCOMPARE(points->table()->model()->rowCount(), 2);
		tick_t collisionTick = -1;
		QVERIFY(musicaltime::positionFromText(QStringLiteral("12 ticks"), &collisionTick));
		QCOMPARE(collisionTick, tick_t{12});
		edit(points, 0, 0, QStringLiteral("12 ticks"));

		const ControllerLane* lane = m_clip->controllers().lane(ControllerKind::ControlChange, 7);
		QVERIFY(lane != nullptr);
		QCOMPARE(lane->points.size(), std::size_t{2});
		QVERIFY(lane->points[0].tick != lane->points[1].tick);
		bool keptTen = false;
		bool keptHundred = false;
		for (const ControllerPoint& point : lane->points)
		{
			keptTen = keptTen || point.value == 10;
			keptHundred = keptHundred || point.value == 100;
		}
		QVERIFY(keptTen);
		QVERIFY(keptHundred);
	}

	void insertingAfterAControllerPointDoesNotDiscardTheInsertion()
	{
		m_editor->setControllerLane(QStringLiteral("cc:7"));
		const QJsonArray initial{QJsonObject{{QStringLiteral("tick"), 0}, {QStringLiteral("value"), 10}},
			QJsonObject{{QStringLiteral("tick"), 12}, {QStringLiteral("value"), 100}}};
		QVERIFY(invoke(QStringLiteral("clip.controllers_set"), {{QStringLiteral("clip"), m_editor->clipId()},
			{QStringLiteral("lane"), QStringLiteral("cc:7")}, {QStringLiteral("points"), initial}}).ok);
		ItemListPanel* points = ItemListPanel::open(m_editor, QStringLiteral("controller"),
			[this](QObject* parent) { return itemlists::controllerPoints(m_editor, parent); });
		points->table()->setCurrentIndex(points->table()->model()->index(0, 0));
		const bool inserted = points->insertItem();

		const ControllerLane* lane = m_clip->controllers().lane(ControllerKind::ControlChange, 7);
		QVERIFY(lane != nullptr);
		QCOMPARE(lane->points.size(), inserted ? std::size_t{3} : std::size_t{2});
		for (std::size_t i = 0; i < lane->points.size(); ++i)
		{
			for (std::size_t j = i + 1; j < lane->points.size(); ++j) { QVERIFY(lane->points[i].tick != lane->points[j].tick); }
		}
		bool keptTen = false;
		bool keptHundred = false;
		for (const ControllerPoint& point : lane->points)
		{
			keptTen = keptTen || point.value == 10;
			keptHundred = keptHundred || point.value == 100;
		}
		QVERIFY(keptTen);
		QVERIFY(keptHundred);
	}

	void theCommandOpensAndClosesTheList()
	{
		m_panel->close();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		const ControlResult opened = invoke(QStringLiteral("window.item_list"), {{QStringLiteral("surface"), QStringLiteral("notes")}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		QVERIFY(opened.result.value(QStringLiteral("visible")).toBool());
		QCOMPARE(opened.result.value(QStringLiteral("count")).toInt(), 3);
		QCOMPARE(opened.result.value(QStringLiteral("rows")).toArray().first().toObject().value(QStringLiteral("key")).toString(),
			NoteGrid::keyName(60));
		QVERIFY(ItemListPanel::find(m_editor->canvas(), QStringLiteral("notes")) != nullptr);
		const ControlResult closed = invoke(QStringLiteral("window.item_list"), {{QStringLiteral("surface"), QStringLiteral("notes")}});
		QVERIFY(closed.ok);
		QVERIFY(!closed.result.value(QStringLiteral("visible")).toBool());  // toggled
		QCOMPARE(invoke(QStringLiteral("window.item_list"), {{QStringLiteral("surface"), QStringLiteral("xy")}}).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(invoke(QStringLiteral("window.item_list"), {{QStringLiteral("surface"), QStringLiteral("piano")}}).errorKind,
			ControlErrorKind::InvalidArgs);
	}

	void theAuditRunsItsChecksSeparately()
	{
		QWidget root;
		for (int strip = 0; strip < 2; ++strip)
		{
			auto* group = new QWidget(&root);
			auto* mute = new QPushButton(QStringLiteral("Mute"), group);
			mute->setObjectName(QStringLiteral("mute%1").arg(strip));
		}
		root.show();
		QJsonObject report = auditAccessibility(&root, false, {QStringLiteral("duplicates")});
		QCOMPARE(report.value(QStringLiteral("duplicates_count")).toInt(), 2);  // nothing tells the two apart
		QVERIFY(!report.contains(QStringLiteral("unnamed")));  // a check not run is not reported
		QCOMPARE(report.value(QStringLiteral("summary")).toObject().keys(), QStringList{QStringLiteral("duplicates")});
		int strip = 0;
		for (QWidget* group : root.findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
		{
			group->setAccessibleName(QStringLiteral("Strip %1").arg(++strip));
		}
		report = auditAccessibility(&root, false, {QStringLiteral("duplicates"), QStringLiteral("focus")});
		QCOMPARE(report.value(QStringLiteral("duplicates_count")).toInt(), 0);  // each strip's name tells them apart
		QVERIFY(report.contains(QStringLiteral("unreachable")));
		QCOMPARE(accessibilityCheckNames().size(), 5);
		QVERIFY(auditAccessibility(&root, false).contains(QStringLiteral("unnamed_count")));  // the old form runs them all
	}

	void announcementsFollowTheLevelAndTheRepeatWindow()
	{
		QVERIFY(a11y::shouldAnnounce(QStringLiteral("Saved one"), a11y::Politeness::Polite));
		QVERIFY2(!a11y::shouldAnnounce(QStringLiteral("Saved one"), a11y::Politeness::Polite), "the same message within a second");
		const ControlResult errorsOnly = invoke(QStringLiteral("view.announcements"), {{QStringLiteral("level"), QStringLiteral("errors")}});
		QVERIFY2(errorsOnly.ok, qPrintable(errorsOnly.errorMessage));
		QCOMPARE(a11y::announceLevel(), a11y::AnnounceLevel::Errors);
		QVERIFY(!a11y::shouldAnnounce(QStringLiteral("Recording"), a11y::Politeness::Polite));
		QVERIFY(a11y::shouldAnnounce(QStringLiteral("Not done: locked"), a11y::Politeness::Assertive));
		QVERIFY(undo());
		QCOMPARE(a11y::announceLevel(), a11y::AnnounceLevel::All);
		QCOMPARE(invoke(QStringLiteral("view.announcements"), {{QStringLiteral("level"), QStringLiteral("loud")}}).errorKind,
			ControlErrorKind::InvalidArgs);
	}

	void returningToLastAnnouncedValueCancelsPendingAnnouncement()
	{
		bool pending = false;
		using a11y::detail::ValueChangeAction;
		using a11y::detail::valueChangeAction;
		QCOMPARE(valueChangeAction(QStringLiteral("B"), QStringLiteral("A"), false, pending), ValueChangeAction::Announce);
		pending = false;  // announce() clears the pending flag
		QCOMPARE(valueChangeAction(QStringLiteral("A"), QStringLiteral("B"), true, pending), ValueChangeAction::Defer);
		QVERIFY(pending);
		QCOMPARE(valueChangeAction(QStringLiteral("B"), QStringLiteral("B"), true, pending), ValueChangeAction::NoChange);
		QVERIFY2(!pending, "returning to the settled value must leave nothing for the timer to announce");
	}

	void aSaveIsAnnounced()
	{
		a11y::watchSongTransitions();
		Song* song = Engine::getSong();
		song->setModified();
		song->noteProjectSaved(m_home.filePath(QStringLiteral("tune.mmpz")));
		QTRY_COMPARE(a11y::lastAnnouncement(), QStringLiteral("Saved tune.mmpz"));
	}

private:
	QTemporaryDir m_home;
	InstrumentTrack* m_track = nullptr;
	MidiClip* m_clip = nullptr;
	NoteEditor* m_editor = nullptr;
	ItemListPanel* m_panel = nullptr;
};

QTEST_MAIN(ItemListPanelTest)
#include "ItemListPanelTest.moc"
