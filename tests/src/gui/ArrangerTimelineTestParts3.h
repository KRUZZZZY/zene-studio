/*
 * ArrangerTimelineTest.cpp - the arranger's ruler lanes, anchored zoom, drag preview and lock badges
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

// Complete fixture method definitions; included once after the fixture declaration.

void ArrangerTimelineTest::aDoubleClickZoomsToASectionAndFitSelectionFitsTheRange() {
	clipAt(m_track, 40 * Bar, Bar);
	auto* lanes = m_editor->findChild<ArrangerRulerLanes*>();
	QVERIFY(lanes != nullptr);
	QVERIFY(run(QStringLiteral("section.add"), {{QStringLiteral("start"), 4 * Bar}, {QStringLiteral("end"), 8 * Bar},
		{QStringLiteral("name"), QStringLiteral("Chorus")}}).ok);
	QCoreApplication::processEvents();
	const int y = lanes->laneRect(ArrangerRulerLanes::Lane::Sections).center().y();
	QTest::mouseDClick(lanes, Qt::LeftButton, Qt::NoModifier, QPoint(m_editor->timeLine()->markerX(TimePos(5 * Bar)), y));
	QTRY_COMPARE(m_editor->currentPosition().getTicks(), 4 * Bar);
	const tick_t shown = arrangerzoom::visibleTicks(m_editor);
	QVERIFY2(std::abs(shown - 4 * Bar) < Bar / 2, qPrintable(QStringLiteral("the section's 4 bars show as %1 ticks").arg(shown)));

	m_editor->selectAllClips(false);
	ArrangerRange::instance().set(10 * Bar, 12 * Bar, {}, true);
	QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("fit"), QStringLiteral("selection")}}).ok);
	QTRY_COMPARE(m_editor->currentPosition().getTicks(), 10 * Bar);
	ArrangerRange::instance().clear();
}
void ArrangerTimelineTest::theRulerShowsASectionCanBeMovedAndAClickStillSelects() {
	Track* second = nullptr;
	Track* third = nullptr;
	sectionSong(&second, &third);
	auto* lanes = m_editor->findChild<ArrangerRulerLanes*>();
	const int y = lanes->laneRect(ArrangerRulerLanes::Lane::Sections).center().y();
	const QPoint on(m_editor->timeLine()->markerX(TimePos(5 * Bar)), y);
	const QPoint off(m_editor->timeLine()->markerX(TimePos(20 * Bar)), y);
	const auto hover = [lanes](const QPoint& pos) {
		QMouseEvent moved(QEvent::MouseMove, pos, lanes->mapToGlobal(pos), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
		QApplication::sendEvent(lanes, &moved);
	};
	hover(on);
	QCOMPARE(lanes->cursor().shape(), Qt::OpenHandCursor);
	hover(off);
	QVERIFY(lanes->cursor().shape() != Qt::OpenHandCursor);
	QTest::mouseClick(lanes, Qt::LeftButton, Qt::NoModifier, on);
	QVERIFY(ArrangerRange::instance().active());
	QCOMPARE(ArrangerRange::instance().start(), 4 * Bar);
	QCOMPARE(ArrangerRange::instance().end(), 8 * Bar);
	QCOMPARE(ArrangementSections::instance().at(5 * Bar)->name, QStringLiteral("Chorus"));  // nothing moved
}
void ArrangerTimelineTest::aSectionDragInGapModeMovesOnlyItsMaterial() {
	QVERIFY(run(QStringLiteral("arrangement.modes"), {{QStringLiteral("ripple"), QStringLiteral("off")}}).ok);
	Track* second = nullptr;
	Track* third = nullptr;
	const QString id = sectionSong(&second, &third);
	const auto before = std::make_tuple(layoutOf(m_track), layoutOf(second), layoutOf(third));
	dragChorus(8);
	using Layout = std::vector<std::pair<tick_t, tick_t>>;
	QCOMPARE(layoutOf(m_track), (Layout{{0, 2 * Bar}, {8 * Bar, 2 * Bar}, {12 * Bar, 4 * Bar}}));
	QCOMPARE(layoutOf(second), (Layout{{3 * Bar, Bar}, {12 * Bar, 2 * Bar}}));  // cut at the edge: only the inside moved
	QCOMPARE(layoutOf(third), (Layout{{9 * Bar, Bar}}));
	QCOMPARE(ArrangementSections::instance().at(13 * Bar)->name, QStringLiteral("Chorus"));
	QCOMPARE(ArrangementSections::instance().at(13 * Bar)->color, QStringLiteral("#336699"));
	QCOMPARE(ArrangementSections::instance().at(9 * Bar)->name, QStringLiteral("Bridge"));  // gap: the other stays
	QVERIFY(ArrangementSections::instance().at(5 * Bar) == nullptr);

	QVERIFY(run(QStringLiteral("control.undo")).ok);  // ONE step: every clip and the section
	QCOMPARE(std::make_tuple(layoutOf(m_track), layoutOf(second), layoutOf(third)), before);
	QCOMPARE(ArrangementSections::instance().at(5 * Bar)->name, QStringLiteral("Chorus"));
	QCOMPARE(ArrangementSections::instance().at(5 * Bar)->end, 8 * Bar);
	QVERIFY(ArrangementSections::instance().at(13 * Bar) == nullptr);
}
void ArrangerTimelineTest::aSectionDragInRippleModeReordersTheSong() {
	QVERIFY(run(QStringLiteral("arrangement.modes"), {{QStringLiteral("ripple"), QStringLiteral("all")}}).ok);
	Track* second = nullptr;
	Track* third = nullptr;
	sectionSong(&second, &third);
	const auto before = std::make_tuple(layoutOf(m_track), layoutOf(second), layoutOf(third));
	using Layout = std::vector<std::pair<tick_t, tick_t>>;

	dragChorus(8);  // later, to bar 12: what lay in bars 8..16 comes 4 bars earlier, closing the gap
	QCOMPARE(layoutOf(m_track), (Layout{{0, 2 * Bar}, {4 * Bar, 2 * Bar}, {12 * Bar, 4 * Bar}}));
	QCOMPARE(layoutOf(second), (Layout{{3 * Bar, Bar}, {12 * Bar, 2 * Bar}}));
	QCOMPARE(layoutOf(third), (Layout{{5 * Bar, Bar}}));
	QCOMPARE(ArrangementSections::instance().at(13 * Bar)->name, QStringLiteral("Chorus"));
	QCOMPARE(ArrangementSections::instance().at(5 * Bar)->name, QStringLiteral("Bridge"));  // went with its material
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QCOMPARE(std::make_tuple(layoutOf(m_track), layoutOf(second), layoutOf(third)), before);
	QCOMPARE(ArrangementSections::instance().at(9 * Bar)->name, QStringLiteral("Bridge"));

	dragChorus(-4);  // earlier, to bar 0: what lay in bars 0..4 goes 4 bars later
	QCOMPARE(layoutOf(m_track), (Layout{{0, 4 * Bar}, {4 * Bar, 2 * Bar}, {8 * Bar, 2 * Bar}}));
	QCOMPARE(layoutOf(second), (Layout{{0, 2 * Bar}, {7 * Bar, Bar}}));
	QCOMPARE(layoutOf(third), (Layout{{9 * Bar, Bar}}));
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QCOMPARE(std::make_tuple(layoutOf(m_track), layoutOf(second), layoutOf(third)), before);

	dragChorus(8, Qt::ShiftModifier);  // Shift: the other behaviour for this drag - a gap
	QCOMPARE(layoutOf(third), (Layout{{9 * Bar, Bar}}));
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QVERIFY(run(QStringLiteral("arrangement.modes"), {{QStringLiteral("ripple"), QStringLiteral("off")}}).ok);
}
void ArrangerTimelineTest::aSectionMoveIsRefusedOverALockedClip() {
	Track* second = nullptr;
	Track* third = nullptr;
	const QString id = sectionSong(&second, &third);
	const auto before = layoutOf(second);
	QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), control::clipIdOf(second->getClips().front())}, {QStringLiteral("locked"), true}}).ok);
	QCOMPARE(run(QStringLiteral("section.move"), {{QStringLiteral("section"), id}, {QStringLiteral("start"), 12 * Bar}}).errorKind, ControlErrorKind::Refused);
	QCOMPARE(layoutOf(second), before);
	QCOMPARE(ArrangementSections::instance().at(5 * Bar)->name, QStringLiteral("Chorus"));
}
void ArrangerTimelineTest::theLanesMenuChoosesGapOrRipple() {
	QVERIFY(run(QStringLiteral("arrangement.modes"), {{QStringLiteral("ripple"), QStringLiteral("off")}}).ok);
	auto* lanes = m_editor->findChild<ArrangerRulerLanes*>();
	bool triggered = false;
	QTimer::singleShot(0, this, [&triggered] {
		auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
		if (menu == nullptr) { return; }
		for (QAction* action : menu->actions())
		{
			if (action->text().startsWith(QStringLiteral("Ripple"))) { triggered = true; action->trigger(); }
		}
		menu->close();
	});
	const QPoint at(m_editor->timeLine()->markerX(TimePos(Bar)), lanes->laneRect(ArrangerRulerLanes::Lane::Sections).center().y());
	QContextMenuEvent event(QContextMenuEvent::Mouse, at, lanes->mapToGlobal(at));
	QApplication::sendEvent(lanes, &event);
	QVERIFY(triggered);
	QCOMPARE(run(QStringLiteral("arrangement.modes")).result.value(QStringLiteral("ripple")).toString(), QStringLiteral("all"));
	QVERIFY(run(QStringLiteral("arrangement.modes"), {{QStringLiteral("ripple"), QStringLiteral("off")}}).ok);
}
void ArrangerTimelineTest::theLandmarksAreFoundAndNamed() {
	using landmarks::Landmark;
	QVERIFY(run(QStringLiteral("section.add"), {{QStringLiteral("start"), 4 * Bar}, {QStringLiteral("end"), 8 * Bar},
		{QStringLiteral("name"), QStringLiteral("Chorus")}}).ok);
	ProjectMarkers::instance().add(12 * Bar, QStringLiteral("Drop"), QString());
	ProjectMarkers::instance().add(16 * Bar, QStringLiteral("Outro"), QString());
	Timeline& timeline = Engine::getSong()->getTimeline(Song::PlayMode::Song);
	timeline.setLoopPoints(TimePos(20 * Bar), TimePos(24 * Bar));
	timeline.setLoopEnabled(false);

	const Landmark marker = landmarks::at(12 * Bar + 10, 20);
	QCOMPARE(marker.kind, Landmark::Kind::Marker);
	QCOMPARE(marker.name, QStringLiteral("Drop"));
	QCOMPARE(marker.end, 16 * Bar);  // up to the next marker
	const Landmark section = landmarks::at(5 * Bar, 0);
	QCOMPARE(section.kind, Landmark::Kind::Section);
	QCOMPARE(section.start, 4 * Bar);
	QVERIFY(landmarks::describe(section).contains(QStringLiteral("Chorus")));
	QCOMPARE(landmarks::at(21 * Bar, 0).kind, Landmark::Kind::None);
	timeline.setLoopEnabled(true);
	QCOMPARE(landmarks::at(21 * Bar, 0).kind, Landmark::Kind::Loop);
	timeline.setLoopEnabled(false);
}
void ArrangerTimelineTest::theNavigatorIsAReadableSliderOverTheSong() {
	installArrangerNavigator(getGUI()->songEditor());
	auto* bar = getGUI()->songEditor()->findChild<QToolBar*>(QStringLiteral("arrangerNavigator"));
	QVERIFY(bar != nullptr);
	auto* navigator = bar->findChild<QAbstractSlider*>();
	QVERIFY2(navigator != nullptr, "the navigator is not a slider");
	QVERIFY(navigator->minimumHeight() >= tokens::targetSize());
	QVERIFY(navigator->property("zeneFocusRing").toBool());

	clipAt(m_track, 40 * Bar, Bar);
	QVERIFY(run(QStringLiteral("arrangement.scroll"), {{QStringLiteral("position"), 8 * Bar}}).ok);
	QTRY_COMPARE(navigator->value(), static_cast<int>(m_editor->currentPosition().getTicks()));
	navigator->setValue(2 * Bar);  // what a screen reader's value setting does
	QTRY_COMPARE(m_editor->currentPosition().getTicks(), 2 * Bar);
	QVERIFY2(navigator->accessibleDescription().startsWith(QStringLiteral("Showing bars 3 ")), qPrintable(navigator->accessibleDescription()));
}
void ArrangerTimelineTest::theNavigatorNamesTheLastBarItShows() {
	installArrangerNavigator(getGUI()->songEditor());
	auto* bar = getGUI()->songEditor()->findChild<QToolBar*>(QStringLiteral("arrangerNavigator"));
	QVERIFY(bar != nullptr);
	auto* navigator = bar->findChild<QAbstractSlider*>();
	QVERIFY2(navigator != nullptr, "the navigator is not a slider");
	clipAt(m_track, 40 * Bar, Bar);
	QVERIFY(run(QStringLiteral("arrangement.scroll"), {{QStringLiteral("position"), 0}}).ok);

	// A zoom whose view ends exactly on a bar line: the boundary the text has to treat as exclusive.
	tick_t view = 0;
	for (int ppb = 4; ppb <= 400; ++ppb)
	{
		QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), ppb},
			{QStringLiteral("anchor"), QStringLiteral("start")}}).ok);
		view = arrangerzoom::visibleTicks(m_editor);
		if (view > 0 && view % Bar == 0) { break; }
	}
	QVERIFY2(view > 0 && view % Bar == 0, "no zoom ended the view exactly on a bar line");
	QCOMPARE(m_editor->currentPosition().getTicks(), 0);

	static const QRegularExpression said(QStringLiteral("Showing bars (\\d+) to (\\d+) of (\\d+)"));
	const QRegularExpressionMatch found = said.match(navigator->accessibleDescription());
	QVERIFY2(found.hasMatch(), qPrintable(navigator->accessibleDescription()));
	QCOMPARE(found.captured(1).toInt(), 1);
	QCOMPARE(found.captured(2).toInt(), static_cast<int>(view / Bar));
}
void ArrangerTimelineTest::aRangeWithBothEndsOffTheRowsReachesTheBoundaryTrack() {
	auto* overlay = m_editor->findChild<ArrangerRangeOverlay*>();
	QVERIFY2(overlay != nullptr, "no range overlay on the song editor");
	Track::create(Track::Type::Instrument, Engine::getSong());
	QCoreApplication::processEvents();
	const QList<TrackView*> rows = m_editor->trackViews();
	QVERIFY(rows.size() >= 2);
	TrackView* last = rows.last();
	const int below = last->mapTo(overlay->parentWidget(), QPoint(0, 0)).y() + last->height();
	QVERIFY2(below + 8 < overlay->height(), "no space below the last track to drag off the rows");

	QVERIFY(run(QStringLiteral("arrangement.set_tool"), {{QStringLiteral("tool"), QStringLiteral("range")}}).ok);
	const int x = overlay->width() / 4;
	// Both presses land below the last row, where trackAt() finds no track either way.
	QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(x, below + 2));
	QTest::mouseMove(overlay, QPoint(x + overlay->width() / 2, below + 8));
	QVERIFY2(ArrangerRange::instance().active(), "a range dragged off the rows dispatched no range at all");
	QCOMPARE(ArrangerRange::instance().tracks(), QVector<Track*>{last->getTrack()});
	QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(x + overlay->width() / 2, below + 8));
	ArrangerRange::instance().clear();
	QVERIFY(run(QStringLiteral("arrangement.set_tool"), {{QStringLiteral("tool"), QStringLiteral("select")}}).ok);
}
void ArrangerTimelineTest::aRangeDragCutShortByTheGrabBeingTakenStops() {
	auto* overlay = m_editor->findChild<ArrangerRangeOverlay*>();
	QVERIFY2(overlay != nullptr, "no range overlay on the song editor");
	const TrackView* row = m_editor->trackViews().first();
	const int onRow = row->mapTo(overlay->parentWidget(), QPoint(0, 0)).y() + row->height() / 2;
	QVERIFY(run(QStringLiteral("arrangement.set_tool"), {{QStringLiteral("tool"), QStringLiteral("range")}}).ok);

	const int x = overlay->width() / 4;
	QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(x, onRow));
	QEvent grabLost(QEvent::UngrabMouse);  // a popup took the mouse grab away
	QCoreApplication::sendEvent(overlay, &grabLost);
	QTest::mouseMove(overlay, QPoint(x + overlay->width() / 2, onRow));
	QVERIFY2(!ArrangerRange::instance().active(), "a drag that lost its grab still dispatched a range");
	QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(x + overlay->width() / 2, onRow));
	ArrangerRange::instance().clear();
	QVERIFY(run(QStringLiteral("arrangement.set_tool"), {{QStringLiteral("tool"), QStringLiteral("select")}}).ok);
}
void ArrangerTimelineTest::switchingToolsDuringAHeldAuditionStopsIt() {
	auto* overlay = m_editor->findChild<ArrangerRangeOverlay*>();
	QVERIFY2(overlay != nullptr, "no range overlay on the song editor");
	Track* other = Track::create(Track::Type::Instrument, Engine::getSong());
	QCoreApplication::processEvents();
	clipAt(m_track, 40 * Bar, Bar);
	// The press lands on the row the audition silences every other track for.
	const TrackView* row = m_editor->trackViews().first();
	const QPoint press(overlay->width() / 2, row->mapTo(overlay->parentWidget(), QPoint(0, 0)).y() + row->height() / 2);

	QVERIFY(run(QStringLiteral("arrangement.set_tool"), {{QStringLiteral("tool"), QStringLiteral("audition")}}).ok);
	QCOMPARE(overlay->tool(), ArrangerRangeOverlay::Tool::Audition);
	QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, press);
	QVERIFY2(other->isMuted(), "the held audition did not silence the other tracks");
	QVERIFY(run(QStringLiteral("transport.audition_stop")).ok);  // tidy, should this case fail halfway

	QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, press);
	QVERIFY2(other->isMuted(), "the second held audition did not silence the other tracks");
	QVERIFY(run(QStringLiteral("arrangement.set_tool"), {{QStringLiteral("tool"), QStringLiteral("range")}}).ok);
	QCOMPARE(overlay->tool(), ArrangerRangeOverlay::Tool::Range);
	QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(press.x() + 40, press.y()));

	QVERIFY2(!other->isMuted(), "the audition was still sounding after the release that ended the gesture");
	QVERIFY2(!ArrangerRange::instance().active(), "a tool change mid-audition still dispatched a range");
	QVERIFY(run(QStringLiteral("transport.audition_stop")).ok);  // tidy whatever happened
	QVERIFY(run(QStringLiteral("arrangement.set_tool"), {{QStringLiteral("tool"), QStringLiteral("select")}}).ok);
}
void ArrangerTimelineTest::positionLineZoomRefusesNonFiniteAndClampsOversized() {
	PositionLine* line = m_editor->positionLine();
	QVERIFY(line != nullptr);

	line->zoomChange(1.0f);
	QCOMPARE(line->width(), 8);
	line->move(40, 0);

	const float nonFinite[] = {
		std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity(),
		-std::numeric_limits<float>::infinity(),
	};
	for (const float zoom : nonFinite)
	{
		line->zoomChange(zoom);
		QCOMPARE(line->width(), 8);
		QCOMPARE(line->x(), 40);
	}

	// 3.0e8 * 8 is past the int range; FLT_MAX is far past it.
	for (const float zoom : {3.0e8f, std::numeric_limits<float>::max()})
	{
		line->zoomChange(zoom);
		QCOMPARE(line->width(), QWIDGETSIZE_MAX);
	}

	// Put the line back where the editor's own zoom path leaves it (SongEditor.cpp: pixelsPerBar / 128).
	line->zoomChange(m_editor->pixelsPerBar() / 128.0f);
}
void ArrangerTimelineTest::everyTimelineTestIsAPrivateSlot() {
	for (const char* name : {"aBurstOfRealNudgesIsOneUndoStep()", "theNavigatorRangeIncludesSongsNearTheTickLimit()",
		"oversizedScrollPositionsReachTheEndOfTheArranger()"})
	{
		QVERIFY2(staticMetaObject.indexOfSlot(name) >= 0, name);
	}
}
void ArrangerTimelineTest::aBurstOfRealNudgesIsOneUndoStep() {
	Clip* clip = clipAt(m_track, 2 * Bar, Bar);
	Clip* other = clipAt(m_track, 8 * Bar, Bar);
	QCoreApplication::processEvents();
	ProjectJournal* journal = Engine::projectJournal();
	QVERIFY(journal != nullptr);
	const QJsonObject press{{QStringLiteral("direction"), QStringLiteral("right")}, {QStringLiteral("amount"), 48}};
	const int start = clip->startPosition().getTicks();
	ShellSelection::instance()->setClip(clip);
	const int depthBefore = journal->undoDepth();
	const int recordsBefore = ControlRegistry::instance()->transactions().size();

	for (int i = 0; i < 3; ++i) { QVERIFY2(run(QStringLiteral("arrangement.nudge"), press).ok, "a press did not nudge"); }
	QCOMPARE(clip->startPosition().getTicks(), start + 3 * 48);
	QCOMPARE(journal->undoDepth() - depthBefore, 1);
	QCOMPARE(ControlRegistry::instance()->transactions().size() - recordsBefore, 1);
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QCOMPARE(clip->startPosition().getTicks(), start);
	QCOMPARE(journal->undoDepth(), depthBefore);

	ShellSelection::instance()->setClip(other);
	QVERIFY2(run(QStringLiteral("arrangement.nudge"), press).ok, "the other clip did not nudge");
	QCOMPARE(journal->undoDepth() - depthBefore, 1);
	QCOMPARE(ControlRegistry::instance()->transactions().size() - recordsBefore, 2);
	ShellSelection::instance()->setClip(nullptr);
}
void ArrangerTimelineTest::theNavigatorRangeIncludesSongsNearTheTickLimit() {
	installArrangerNavigator(getGUI()->songEditor());
	auto* bar = getGUI()->songEditor()->findChild<QToolBar*>(QStringLiteral("arrangerNavigator"));
	QVERIFY(bar != nullptr);
	auto* navigator = bar->findChild<QAbstractSlider*>();
	QVERIFY(navigator != nullptr);

	const tick_t maxTick = std::numeric_limits<tick_t>::max();
	clipAt(m_track, maxTick - Bar, Bar);
	QCOMPARE(Engine::getSong()->length(), maxTick / Bar);
	QTRY_VERIFY(navigator->maximum() > 1'000'000'000);
}


void ArrangerTimelineTest::oversizedScrollPositionsReachTheEndOfTheArranger()
{
	clipAt(m_track, 40 * Bar, Bar);
	auto* scrollbar = m_editor->horizontalScroll();
	QVERIFY(scrollbar->maximum() > 0);

	const ControlResult result = run(QStringLiteral("arrangement.scroll"), {{QStringLiteral("position"), 2147483648LL}});
	QVERIFY(result.ok);
	QCOMPARE(scrollbar->value(), scrollbar->maximum());
}
