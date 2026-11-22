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

void ArrangerTimelineTest::initTestCase() {
	QVERIFY(m_home.isValid());
	guitest::startGui(m_home);
	ControlRegistry::setReady(true);
	getGUI()->mainWindow()->show();
	getGUI()->songEditor()->parentWidget()->show();
	m_editor = getGUI()->songEditor()->m_editor;
	installArrangerOverlays(m_editor);  // what the shell does once it is shown; installs once
	QCoreApplication::processEvents();
}
void ArrangerTimelineTest::cleanupTestCase() {
	ControlRegistry::setReady(false);
	Engine::destroy();
}
void ArrangerTimelineTest::init() {
	Engine::getSong()->clearProject();  // and with it the locks and the markers (project sections)
	ArrangerRange::instance().clear();  // the range is a singleton: a case must not inherit the last one's
	m_track = Track::create(Track::Type::Instrument, Engine::getSong());
	QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 40}, {QStringLiteral("anchor"), QStringLiteral("start")}}).ok);
	QVERIFY(run(QStringLiteral("arrangement.scroll"), {{QStringLiteral("position"), 0}}).ok);
	QCoreApplication::processEvents();
}
void ArrangerTimelineTest::trackHeaderAndRulerOpenMenusFromTheKeyboard() {
	const auto popup = [] {
		for (QWidget* widget : QApplication::topLevelWidgets())
		{
			auto* menu = qobject_cast<QMenu*>(widget);
			if (menu != nullptr && menu->isVisible()) { return menu; }
		}
		return static_cast<QMenu*>(nullptr);
	};
	const auto actionTexts = [](QMenu* menu) {
		QStringList texts;
		for (QAction* action : menu->actions()) { texts << action->text(); }
		return texts;
	};
	TrackView* view = nullptr;
	for (TrackView* candidate : m_editor->trackViews())
	{
		if (candidate->getTrack() == m_track) { view = candidate; }
	}
	QVERIFY(view != nullptr);
	auto* label = view->findChild<TrackLabelButton*>();
	QVERIFY(label != nullptr);
	QVERIFY((label->focusPolicy() & Qt::TabFocus) != 0);
	QVERIFY(!label->accessibleName().isEmpty());
	QVERIFY(label->isVisible());
	QVERIFY(TrackLabelButton::isMenuKey(Qt::Key_Menu, Qt::NoModifier));
	QVERIFY(TrackLabelButton::isMenuKey(Qt::Key_F10, Qt::ShiftModifier));
	QVERIFY(!TrackLabelButton::isMenuKey(Qt::Key_F10, Qt::NoModifier));
	label->openActionsMenu();
	QTRY_VERIFY(popup() != nullptr);
	QStringList keyboardActions = actionTexts(popup());
	QVERIFY2(keyboardActions.contains(QStringLiteral("Clone this track")), qPrintable(keyboardActions.join(QStringLiteral(" | "))));
	QVERIFY(keyboardActions.contains(QStringLiteral("Remove this track")));
	popup()->close();
	QTRY_VERIFY(popup() == nullptr);  // let a popup's queued hide finish before reopening

	label->openActionsMenu();
	QTRY_VERIFY(popup() != nullptr);
	QCOMPARE(actionTexts(popup()), keyboardActions);
	popup()->close();
	QTRY_VERIFY(popup() == nullptr);  // let a popup's queued hide finish before reopening

	// The mouse route opens that same menu now, rather than the old right-click rename.
	TestTrackLabel probe(view, view->getTrackSettingsWidget());
	probe.show();
	QMouseEvent rightPress(QEvent::MouseButtonPress, QPointF(1, 1), Qt::RightButton, Qt::NoButton, Qt::NoModifier);
	probe.mousePressEvent(&rightPress);
	QTRY_VERIFY(popup() != nullptr);
	QCOMPARE(actionTexts(popup()), keyboardActions);
	popup()->close();
	QTRY_VERIFY(popup() == nullptr);  // let a popup's queued hide finish before reopening

	TimeLineWidget* ruler = m_editor->timeLine();
	QVERIFY((ruler->focusPolicy() & Qt::TabFocus) != 0);
	QCOMPARE(ruler->accessibleName(), QStringLiteral("Time ruler"));
	Engine::getSong()->setPlayPos(2 * Bar, Song::PlayMode::Song);
	QVERIFY(TimeLineWidget::isMenuKey(Qt::Key_Menu, Qt::NoModifier));
	QVERIFY(TimeLineWidget::isMenuKey(Qt::Key_F10, Qt::ShiftModifier));
	QVERIFY(!TimeLineWidget::isMenuKey(Qt::Key_F10, Qt::NoModifier));
	ruler->openRulerMenu();
	QTRY_VERIFY(popup() != nullptr);
	const QStringList rulerActions = actionTexts(popup());
	QVERIFY(rulerActions.contains(QStringLiteral("Set loop begin here")));
	QVERIFY(rulerActions.contains(QStringLiteral("Set loop end here")));
	// The shared builder keeps the punch range's own actions (Q050-147), so the keyboard menu has them too.
	QVERIFY2(rulerActions.contains(QStringLiteral("Set punch in here")), qPrintable(rulerActions.join(QStringLiteral(" | "))));
	QVERIFY(rulerActions.contains(QStringLiteral("Clear the punch range")));
	popup()->close();
	QTRY_VERIFY(popup() == nullptr);  // let a popup's queued hide finish before reopening
	ruler->openRulerMenu();
	QTRY_VERIFY(popup() != nullptr);
	QCOMPARE(actionTexts(popup()), rulerActions);
	popup()->close();
}
void ArrangerTimelineTest::aCanceledPinchKeepsItsStartingZoomInHistory() {
	const int start = m_editor->zoomingModel()->value();
	const int changed = start + 2;
	arrangerzoom::remember(start + 1);  // make the history result unambiguous if cancellation is ignored
	arrangerzoom::zoomAround(m_editor, changed, 0);
	QCOMPARE(m_editor->zoomingModel()->value(), changed);
#ifndef QT_NO_GESTURES
	arrangerzoom::finishPinch(start, changed, Qt::GestureCanceled);
#else
	arrangerzoom::finishPinch(start, changed, true);  // a Qt without gestures has no Qt::GestureState
#endif
	const ControlResult back = run(QStringLiteral("arrangement.zoom_back"));
	QVERIFY2(back.ok, qPrintable(back.errorMessage));
	QCOMPARE(m_editor->zoomingModel()->value(), start);
}
void ArrangerTimelineTest::backAndForwardWalkSeveralPriorZooms() {
	// The walk is a per-process static: drain it, so the assertions below are exact however many
	// prior extents the cases before this one left in it.
	QVERIFY2(drainZoomWalk(), "the back walk did not drain within its bound");
	const int first = m_editor->zoomingModel()->value();
	QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), -6}}).ok);
	const int second = m_editor->zoomingModel()->value();
	QCOMPARE(second, first - 6);
	QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), -6}}).ok);
	const int third = m_editor->zoomingModel()->value();
	QCOMPARE(third, first - 12);

	// Back takes each prior extent in turn: the walk's last entry first, refused when it runs out.
	QVERIFY(run(QStringLiteral("arrangement.zoom_back")).ok);
	QCOMPARE(m_editor->zoomingModel()->value(), second);
	QVERIFY(run(QStringLiteral("arrangement.zoom_back")).ok);
	QCOMPARE(m_editor->zoomingModel()->value(), first);
	const ControlResult noEarlier = run(QStringLiteral("arrangement.zoom_back"));
	QVERIFY2(!noEarlier.ok, qPrintable(noEarlier.errorMessage));
	QCOMPARE(m_editor->zoomingModel()->value(), first);

	// Forward returns over them in order, one entry a step.
	QVERIFY(run(QStringLiteral("arrangement.zoom_forward")).ok);
	QCOMPARE(m_editor->zoomingModel()->value(), second);
	QVERIFY(run(QStringLiteral("arrangement.zoom_forward")).ok);
	QCOMPARE(m_editor->zoomingModel()->value(), third);
	QVERIFY2(!run(QStringLiteral("arrangement.zoom_forward")).ok, "nothing further was walked past");
	// ... and the walk back over the same two is exact again.
	QVERIFY(run(QStringLiteral("arrangement.zoom_back")).ok);
	QCOMPARE(m_editor->zoomingModel()->value(), second);
	QVERIFY(run(QStringLiteral("arrangement.zoom_back")).ok);
	QCOMPARE(m_editor->zoomingModel()->value(), first);
	QVERIFY2(!run(QStringLiteral("arrangement.zoom_back")).ok, "the walk back is drained to its bottom");

	// Sorry! A zoom resets the forward walk: what you walked past is unreachable now.
	QVERIFY(run(QStringLiteral("arrangement.zoom_forward")).ok);  // to second, so back works below
	QVERIFY(run(QStringLiteral("arrangement.zoom_back")).ok);  // and back to the bottom
	QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), -3}}).ok);
	QVERIFY2(!run(QStringLiteral("arrangement.zoom_forward")).ok, "a new zoom cut the forward walk off");
}
void ArrangerTimelineTest::zoomToggleSwapsTheCurrentAndPriorExtent() {
	// The toggle is a per-process static: drain the walk, then re-remember the walk's bottom (the
	// zoom the view is at), so the toggle has exactly nothing to swap with however the cases before
	// this one left it.
	QVERIFY2(drainZoomWalk(), "the back walk did not drain within its bound");
	const int first = m_editor->zoomingModel()->value();
	arrangerzoom::remember(first);
	QVERIFY2(!run(QStringLiteral("arrangement.zoom_toggle")).ok,
		"before a zoom is left behind there is nothing to swap with");
	QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), -6}}).ok);
	const int after = m_editor->zoomingModel()->value();
	QCOMPARE(m_editor->zoomingModel()->value(), first - 6);
	// The zoom left the first extent behind: the toggle goes there, and back again.
	QVERIFY(run(QStringLiteral("arrangement.zoom_toggle")).ok);
	QCOMPARE(m_editor->zoomingModel()->value(), first);
	QVERIFY(run(QStringLiteral("arrangement.zoom_toggle")).ok);
	QCOMPARE(m_editor->zoomingModel()->value(), after);

	// A back step leaves its extent behind too: from here the toggle swaps with it.
	QVERIFY(run(QStringLiteral("arrangement.zoom_back")).ok);
	QCOMPARE(m_editor->zoomingModel()->value(), first);
	QVERIFY(run(QStringLiteral("arrangement.zoom_toggle")).ok);
	QCOMPARE(m_editor->zoomingModel()->value(), after);
	QVERIFY(run(QStringLiteral("arrangement.zoom_toggle")).ok);
	QCOMPARE(m_editor->zoomingModel()->value(), first);
}
void ArrangerTimelineTest::backAndForwardAfterTheToggleNeverNoOp() {
	QVERIFY2(drainZoomWalk(), "the back walk did not drain within its bound");
	const int bottom = m_editor->zoomingModel()->value();
	QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), -6}}).ok);
	const int middle = m_editor->zoomingModel()->value();
	QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), -6}}).ok);
	QCOMPARE(m_editor->zoomingModel()->value(), bottom - 12);

	// The review's repro, one level along: toggle from the deep extent to the middle, then Back.
	// The swap took the walk's entry for the middle (the view is at it now), so Back goes on to the
	// bottom - with the unreconciled walk it succeeded and left the zoom where it was.
	QVERIFY(run(QStringLiteral("arrangement.zoom_toggle")).ok);
	QCOMPARE(m_editor->zoomingModel()->value(), middle);
	const int beforeBack = m_editor->zoomingModel()->value();
	const ControlResult back = run(QStringLiteral("arrangement.zoom_back"));
	QVERIFY2(!back.ok || m_editor->zoomingModel()->value() != beforeBack,
		qPrintable(back.ok ? QStringLiteral("Back after the toggle succeeded without changing the zoom")
						   : back.errorMessage));
	const int beforeForward = m_editor->zoomingModel()->value();
	const ControlResult forward = run(QStringLiteral("arrangement.zoom_forward"));
	QVERIFY2(!forward.ok || m_editor->zoomingModel()->value() != beforeForward,
		qPrintable(forward.ok ? QStringLiteral("Forward after the toggle succeeded without changing the zoom")
							  : forward.errorMessage));

	// Toggled all the way to the walk's bottom, Back has nothing earlier and must say so.
	QVERIFY(run(QStringLiteral("arrangement.zoom_toggle")).ok);
	QCOMPARE(m_editor->zoomingModel()->value(), bottom);
	QVERIFY2(!run(QStringLiteral("arrangement.zoom_back")).ok,
		"Back after the toggle to the walk's bottom succeeded, where it would repeat the zoom");
}
void ArrangerTimelineTest::theRulerHasALaneEachUnderTheBarRuler() {
	auto* lanes = m_editor->findChild<ArrangerRulerLanes*>();
	QVERIFY2(lanes != nullptr, "no ruler lanes on the song editor");
	using Lane = ArrangerRulerLanes::Lane;
	const QRect sections = lanes->laneRect(Lane::Sections);
	const QRect markers = lanes->laneRect(Lane::Markers);
	const QRect tempo = lanes->laneRect(Lane::Tempo);
	QVERIFY(!sections.isEmpty() && !markers.isEmpty() && !tempo.isEmpty());
	QVERIFY(sections.bottom() < markers.top());
	QVERIFY(markers.bottom() < tempo.top());
	QTRY_VERIFY(lanes->geometry().top() > m_editor->timeLine()->geometry().bottom() - 1);  // under the ruler, not over it

	ProjectMarkers::instance().add(2 * Bar, QStringLiteral("Drop"), QString());
	lanes->update();
	QTRY_COMPARE(lanes->drawnMarkers().size(), 1);
	QCOMPARE(lanes->drawnMarkers().first(), m_editor->timeLine()->markerX(TimePos(2 * Bar)));

	QVERIFY(run(QStringLiteral("arrangement.ruler_lanes"), {{QStringLiteral("tempo"), false}}).ok);
	QVERIFY(lanes->laneRect(Lane::Tempo).isEmpty());
	QCOMPARE(lanes->height(), sections.height() + markers.height());
	QCOMPARE(run(QStringLiteral("arrangement.ruler_lanes")).result.value(QStringLiteral("tempo")).toBool(), false);
	QVERIFY(run(QStringLiteral("arrangement.ruler_lanes"), {{QStringLiteral("tempo"), true}}).ok);
	QCOMPARE(lanes->height(), sections.height() + markers.height() + tempo.height());
}
void ArrangerTimelineTest::theBarRulerHasAUsableHeightAndSeeksWhereItIsDragged() {
	constexpr int MinBarRulerHeight = 24;  // a bar number, the loop brace and the punch band fit in it
	TimeLineWidget* ruler = m_editor->timeLine();
	QVERIFY(ruler->isVisible());

	auto* box = qobject_cast<QVBoxLayout*>(m_editor->layout());
	QVERIFY(box != nullptr);
	int entries = 0;
	for (int i = 0; i < box->count(); ++i) { entries += box->itemAt(i)->widget() == ruler ? 1 : 0; }
	QCOMPARE(entries, 1);

	QVERIFY2(ruler->height() >= MinBarRulerHeight,
		qPrintable(QStringLiteral("the bar ruler is %1 px tall, under %2").arg(ruler->height()).arg(MinBarRulerHeight)));

	const int y = ruler->height() / 2;
	QTest::mouseClick(ruler, Qt::LeftButton, Qt::NoModifier, QPoint(ruler->markerX(TimePos(2 * Bar)), y));
	QVERIFY2(std::abs(Engine::getSong()->getPlayPos().getTicks() - 2 * Bar) <= Bar / 8,
		qPrintable(QStringLiteral("a click at bar 3 left the play position at tick %1").arg(Engine::getSong()->getPlayPos().getTicks())));

	QTest::mousePress(ruler, Qt::LeftButton, Qt::NoModifier, QPoint(ruler->markerX(TimePos(2 * Bar)), y));
	const QPoint dragTo(ruler->markerX(TimePos(4 * Bar)), y);
	QMouseEvent drag(QEvent::MouseMove, QPointF(dragTo), QPointF(ruler->mapToGlobal(dragTo)), Qt::NoButton,
		Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(ruler, &drag);
	QTest::mouseRelease(ruler, Qt::LeftButton, Qt::NoModifier, dragTo);
	QVERIFY2(std::abs(Engine::getSong()->getPlayPos().getTicks() - 4 * Bar) <= Bar / 8,
		qPrintable(QStringLiteral("a drag to bar 5 left the play position at tick %1").arg(Engine::getSong()->getPlayPos().getTicks())));
}
void ArrangerTimelineTest::theRulerHasAUsableLayoutHeight() {
	TimeLineWidget* ruler = m_editor->timeLine();
	QCoreApplication::processEvents();
	QVERIFY(ruler->isVisible());
	QVERIFY2(ruler->height() >= scaledPixels(32),
		qPrintable(QStringLiteral("the ruler has %1 px of layout height").arg(ruler->height())));
}
void ArrangerTimelineTest::theChordTrackIsDrawnInItsLane() {
	auto* lanes = m_editor->findChild<ArrangerRulerLanes*>();
	QVERIFY(lanes != nullptr);
	using Lane = ArrangerRulerLanes::Lane;
	QVERIFY2(lanes->laneRect(Lane::Chords).isEmpty(), "an empty chord track took room");
	const int without = lanes->height();

	QVERIFY(run(QStringLiteral("chord.set"), {{QStringLiteral("pos"), 2 * Bar}, {QStringLiteral("chord"), QStringLiteral("Major")},
		{QStringLiteral("root"), 7}, {QStringLiteral("length"), 2 * Bar}}).ok);
	QTRY_VERIFY(!lanes->laneRect(Lane::Chords).isEmpty());
	QTRY_COMPARE(lanes->height(), without + lanes->laneRect(Lane::Chords).height());
	QVERIFY(lanes->laneRect(Lane::Tempo).bottom() < lanes->laneRect(Lane::Chords).top());

	// A click on the chord moves the play position to it.
	const QRect lane = lanes->laneRect(Lane::Chords);
	const int x = m_editor->timeLine()->markerX(TimePos(3 * Bar));
	QTest::mouseClick(lanes, Qt::LeftButton, Qt::NoModifier, QPoint(x, lane.center().y()));
	QCOMPARE(Engine::getSong()->getPlayPos().getTicks(), 2 * Bar);

	QVERIFY(run(QStringLiteral("control.undo")).ok);  // the seek
	QVERIFY(run(QStringLiteral("control.undo")).ok);  // the chord
	QTRY_VERIFY(lanes->laneRect(Lane::Chords).isEmpty());
	QTRY_COMPARE(lanes->height(), without);
}
void ArrangerTimelineTest::aRulerWithEveryLaneHiddenKeepsItsContextMenuReachable() {
	auto* lanes = m_editor->findChild<ArrangerRulerLanes*>();
	QVERIFY(lanes != nullptr);
	using Lane = ArrangerRulerLanes::Lane;
	const QJsonObject allLanesOn{{QStringLiteral("sections"), true}, {QStringLiteral("markers"), true},
		{QStringLiteral("tempo"), true}, {QStringLiteral("chords"), true}};
	// However the case ends, every lane shows again: the cases after it read the lanes.
	struct LanesShownAgain
	{
		const QJsonObject& lanesOn;
		~LanesShownAgain() { run(QStringLiteral("arrangement.ruler_lanes"), lanesOn); }
	} shownAgain{allLanesOn};
	const QJsonObject everyLaneOff{{QStringLiteral("sections"), false}, {QStringLiteral("markers"), false},
		{QStringLiteral("tempo"), false}, {QStringLiteral("chords"), false}};
	QVERIFY(run(QStringLiteral("arrangement.ruler_lanes"), everyLaneOff).ok);
	QVERIFY(lanes->laneRect(Lane::Sections).isEmpty());
	QVERIFY2(!lanes->isHidden(), "hiding every lane hid the ruler, which took its context menu with it");
	QVERIFY2(lanes->height() > 0, "no room is left under the ruler to right-click");

	QVERIFY(run(QStringLiteral("arrangement.ruler_lanes"), {{QStringLiteral("markers"), true}}).ok);
	QVERIFY(!lanes->laneRect(Lane::Markers).isEmpty());
	QVERIFY(lanes->laneRect(Lane::Sections).isEmpty());
	QVERIFY(run(QStringLiteral("arrangement.ruler_lanes"), allLanesOn).ok);
	QVERIFY(!lanes->isHidden());
	QVERIFY(!lanes->laneRect(Lane::Sections).isEmpty());
}
void ArrangerTimelineTest::aKeyboardZoomKeepsThePlayheadInPlace() {
	clipAt(m_track, 40 * Bar, Bar);  // a song long enough to scroll
	m_editor->selectAllClips(false);
	ArrangerRange::instance().clear();
	Engine::getSong()->setPlayPos(6 * Bar, Song::PlayMode::Song);
	const int before = m_editor->timeLine()->markerX(TimePos(6 * Bar));
	const ControlResult zoomed = run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), 20}});
	QVERIFY2(zoomed.ok, qPrintable(zoomed.errorMessage));
	QCOMPARE(zoomed.result.value(QStringLiteral("anchor")).toInt(), 6 * Bar);
	QVERIFY(m_editor->pixelsPerBar() > 40);
	QVERIFY2(std::abs(m_editor->timeLine()->markerX(TimePos(6 * Bar)) - before) <= 2,
		qPrintable(QStringLiteral("the playhead moved on screen from x=%1 to x=%2").arg(before).arg(m_editor->timeLine()->markerX(TimePos(6 * Bar)))));
}
void ArrangerTimelineTest::anOversizedZoomStepClampsToMaximum() {
	IntModel* model = m_editor->zoomingModel();
	const ControlResult zoomed = run(QStringLiteral("arrangement.zoom"),
		{{QStringLiteral("step"), std::numeric_limits<int>::max()}});
	QVERIFY2(zoomed.ok, qPrintable(zoomed.errorMessage));
	QCOMPARE(zoomed.result.value(QStringLiteral("zoom")).toInt(), model->maxValue());
	QCOMPARE(model->value(), model->maxValue());
}
void ArrangerTimelineTest::aZoomStepAtItsBoundIsRefusedNotASilentSuccess() {
	IntModel* model = m_editor->zoomingModel();
	model->setValue(model->minValue());
	const ControlResult out = run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), -4}});
	QVERIFY2(!out.ok, "Zoom Out at the minimum zoom reported success without zooming");
	QCOMPARE(out.errorKind, ControlErrorKind::Refused);
	QCOMPARE(model->value(), model->minValue());
	const ControlResult in = run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), 4}});
	QVERIFY2(in.ok, qPrintable(in.errorMessage));
	QCOMPARE(model->value(), model->minValue() + 4);

	model->setValue(model->maxValue());
	const ControlResult past = run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("step"), 4}});
	QVERIFY2(!past.ok, "Zoom In at the maximum zoom reported success without zooming");
	QCOMPARE(past.errorKind, ControlErrorKind::Refused);
	QCOMPARE(model->value(), model->maxValue());
	// An absolute zoom to the extent already shown is not a step and stays a success.
	QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 40}}).ok);
	QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 40}}).ok);
}
void ArrangerTimelineTest::aPatternPreviewCombinesOffsetAndNoteBeforeRasterizing() {
	QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 4},
		{QStringLiteral("anchor"), QStringLiteral("start")}}).ok);
	auto* instrumentTrack = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::patternStore()));
	QVERIFY(instrumentTrack != nullptr);
	QCoreApplication::processEvents();

	PatternTrack* patternTrack = nullptr;
	for (Track* track : Engine::getSong()->tracks())
	{
		patternTrack = dynamic_cast<PatternTrack*>(track);
		if (patternTrack != nullptr) { break; }
	}
	QVERIFY(patternTrack != nullptr);

	auto* midiClip = dynamic_cast<MidiClip*>(instrumentTrack->getClip(patternTrack->patternIndex()));
	QVERIFY(midiClip != nullptr);
	midiClip->addNote(Note(TimePos(48), TimePos(36), 60, 100), false);
	midiClip->rearrangeAllNotes();
	QVERIFY(!midiClip->notes().empty());

	auto* patternClip = dynamic_cast<PatternClip*>(patternTrack->createClip(TimePos(0)));
	QVERIFY(patternClip != nullptr);
	patternClip->setStartTimeOffset(TimePos(36));
	QCoreApplication::processEvents();

	PatternClipView* view = nullptr;
	for (PatternClipView* candidate : m_editor->findChildren<PatternClipView*>())
	{
		if (candidate->getClip() == patternClip) { view = candidate; break; }
	}
	QVERIFY(view != nullptr);
	view->setGradient(false);
	view->setProperty("noteColor", QColor(255, 0, 255));
	view->setFixedWidth(40); // Show later repetitions past the clip's left border.
	view->setFixedHeight(40);
	view->setNeedsUpdate(true);

	QImage image(view->size(), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	view->render(&painter);
	painter.end();

	int notePixelX = -1;
	for (int y = 3; y < image.height() - 3 && notePixelX < 0; ++y)
	{
		for (int x = 0; x < image.width(); ++x)
		{
			if (image.pixelColor(x, y) == QColor(255, 0, 255)) { notePixelX = x; break; }
		}
	}
	QCOMPARE(notePixelX - 4, 1); // x=1 repeats at x=5; x=1 itself lies under the clip border.
}
