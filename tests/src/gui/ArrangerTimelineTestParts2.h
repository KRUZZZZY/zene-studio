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

void ArrangerTimelineTest::anEmptyPatternStorePaintDoesNotRaiseInvalidFloatException() {
	QCOMPARE(Engine::patternStore()->tracks().size(), size_t(0));
	PatternTrack* patternTrack = nullptr;
	for (Track* track : Engine::getSong()->tracks())
	{
		patternTrack = dynamic_cast<PatternTrack*>(track);
		if (patternTrack != nullptr) { break; }
	}
	if (patternTrack == nullptr)
	{
		Engine::getSong()->addPatternTrack();
		QCoreApplication::processEvents();
		for (Track* track : Engine::getSong()->tracks())
		{
			patternTrack = dynamic_cast<PatternTrack*>(track);
			if (patternTrack != nullptr) { break; }
		}
	}
	QVERIFY(patternTrack != nullptr);

	auto* patternClip = dynamic_cast<PatternClip*>(patternTrack->createClip(TimePos(0)));
	QVERIFY(patternClip != nullptr);
	QCoreApplication::processEvents();
	PatternClipView* view = nullptr;
	for (PatternClipView* candidate : m_editor->findChildren<PatternClipView*>())
	{
		if (candidate->getClip() == patternClip) { view = candidate; break; }
	}
	QVERIFY(view != nullptr);
	view->setFixedWidth(40);
	view->setFixedHeight(40);
	view->setNeedsUpdate(true);

	QImage image(view->size(), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	std::fenv_t previousEnvironment;
	QVERIFY(std::fegetenv(&previousEnvironment) == 0);
	QVERIFY(std::feclearexcept(FE_ALL_EXCEPT) == 0);
	view->render(&painter);
	const int raisedExceptions = std::fetestexcept(FE_INVALID);
	QVERIFY(std::fesetenv(&previousEnvironment) == 0);
	painter.end();
	QCOMPARE(raisedExceptions, 0);
}
void ArrangerTimelineTest::aZeroLengthMidiClipWithNotesPreviewsWithoutDividingByItsLength() {
	QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 64},
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
	midiClip->addNote(Note(TimePos(96), TimePos(36), 60, 100), false);
	midiClip->rearrangeAllNotes();
	QVERIFY(!midiClip->notes().empty());
	// Auto-resize is off, so nothing restores a bar: the clip keeps its notes and has no length.
	midiClip->setAutoResize(false);
	midiClip->changeLength(TimePos(0));
	QCOMPARE(midiClip->length().getTicks(), 0);

	auto* patternClip = dynamic_cast<PatternClip*>(patternTrack->createClip(TimePos(0)));
	QVERIFY(patternClip != nullptr);
	QCoreApplication::processEvents();

	PatternClipView* view = nullptr;
	for (PatternClipView* candidate : m_editor->findChildren<PatternClipView*>())
	{
		if (candidate->getClip() == patternClip) { view = candidate; break; }
	}
	QVERIFY(view != nullptr);
	view->setGradient(false);
	view->setProperty("noteColor", QColor(255, 0, 255));
	view->setFixedWidth(128);
	view->setFixedHeight(40);
	view->setNeedsUpdate(true);

	QImage image(view->size(), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	view->render(&painter); // Before the fix this divides by the zero clip length and the process dies with SIGFPE.
	painter.end();

	int notePixelX = -1;
	for (int y = 3; y < image.height() - 3 && notePixelX < 0; ++y)
	{
		for (int x = 0; x < image.width(); ++x)
		{
			if (image.pixelColor(x, y) == QColor(255, 0, 255)) { notePixelX = x; break; }
		}
	}
	QVERIFY2(notePixelX >= 0, "the note of a zero-length MIDI clip was not painted");
}
void ArrangerTimelineTest::theDragPreviewShowsWhatTheDropWillDo() {
	QVERIFY(run(QStringLiteral("arrangement.modes"), {{QStringLiteral("overlap"), QStringLiteral("trim")}}).ok);
	Clip* under = clipAt(m_track, 0, 2 * Bar);
	Clip* moved = clipAt(m_track, 4 * Bar, 2 * Bar);
	QCoreApplication::processEvents();
	moved->movePosition(TimePos(Bar));  // where the drag has it now
	m_editor->realignTracks();
	QCoreApplication::processEvents();
	ClipView* view = viewOf(moved);
	QVERIFY(view != nullptr);
	auto* preview = m_editor->findChild<ArrangerEditPreview*>();
	QVERIFY2(preview != nullptr, "no drag preview on the song editor");

	arrangement::SnapHit snap;
	snap.start = Bar;
	snap.line = 2 * Bar;
	snap.kind = arrangement::SnapHit::Kind::ClipEdge;
	ArrangerEditPreview::showDrag(view, 4 * Bar, snap);
	QVERIFY(preview->isVisible());
	QVERIFY2(!preview->ghostRect().isEmpty(), "no ghost where the clip started");
	QCOMPARE(preview->effectRects().size(), 1);  // the second bar of 'under' will be trimmed away
	QVERIFY(std::abs(preview->effectRects().first().width() - m_editor->pixelsPerBar()) <= 2);  // one bar wide
	QVERIFY(preview->snapLineX() >= 0);
	QVERIFY(!preview->refused());

	QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), control::clipIdOf(under)}}).ok);
	ArrangerEditPreview::showDrag(view, 4 * Bar, snap);
	QVERIFY2(preview->refused(), "a locked clip under the drop was not shown as a refusal");
	ArrangerEditPreview::endDrag(view);
	QVERIFY(!preview->isVisible());
}
void ArrangerTimelineTest::theDragPreviewDoesNotWrapAnEndNearTheTickLimit() {
	constexpr tick_t InitialStart = 1'610'612'641;
	Clip* clip = clipAt(m_track, InitialStart, 447);
	QCoreApplication::processEvents();
	ClipView* view = viewOf(clip);
	QVERIFY(view != nullptr);
	auto* preview = m_editor->findChild<ArrangerEditPreview*>();
	QVERIFY(preview != nullptr);

	const arrangement::Modes previousModes = arrangement::modes();
	arrangement::Modes previewModes = previousModes;
	previewModes.overlap = arrangement::Overlap::Free;
	previewModes.snapToEdges = false;
	arrangement::setModes(previewModes);

	QVERIFY(run(QStringLiteral("arrangement.zoom"), {{QStringLiteral("pixels_per_bar"), 4}, {QStringLiteral("anchor"), QStringLiteral("start")}}).ok);
	QCoreApplication::processEvents();
	view->setSelected(false);
	const QPoint pressPos(view->width() / 2, view->height() / 2);
	const QPoint pressGlobal = view->mapToGlobal(pressPos);
	QMouseEvent press(QEvent::MouseButtonPress, QPointF(pressPos), QPointF(pressGlobal), Qt::LeftButton,
		Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(view, &press);

	// At four pixels per bar this moves it by 11,184,810 * 48 ticks, to max(tick_t) - 126.
	constexpr int DragPixels = 11'184'810;
	const QPoint dragPos = pressPos + QPoint(DragPixels, 0);
	const QPoint dragGlobal = view->mapToGlobal(dragPos);
	QMouseEvent move(QEvent::MouseMove, QPointF(dragPos), QPointF(dragGlobal), Qt::NoButton,
		Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(view, &move);

	const tick_t movedStart = clip->startPosition().getTicks();
	const int snapLine = preview->snapLineX();
	const int expectedSnapLine = preview->mapFromGlobal(view->mapToGlobal(QPoint(0, 0))).x();
	clip->movePosition(TimePos(InitialStart));  // keep the release and the next test away from the boundary
	const QPoint releaseGlobal = view->mapToGlobal(pressPos);
	QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pressPos), QPointF(releaseGlobal), Qt::LeftButton,
		Qt::NoButton, Qt::NoModifier);
	QCoreApplication::sendEvent(view, &release);
	arrangement::setModes(previousModes);

	QCOMPARE(movedStart, 2'147'483'521);
	QCOMPARE(snapLine, expectedSnapLine);
}
void ArrangerTimelineTest::aLockedClipWearsAPadlockAndStillSelects() {
	Clip* clip = clipAt(m_track, 0, 2 * Bar);
	QCoreApplication::processEvents();
	ClipView* view = viewOf(clip);
	QVERIFY(view != nullptr);
	QVERIFY(!cliplocks::badgeShown(view));
	QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), control::clipIdOf(clip)}}).ok);
	QVERIFY2(cliplocks::badgeShown(view), "a locked clip shows no padlock");

	m_editor->selectAllClips(false);
	QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, QPoint(view->width() / 2, view->height() * 3 / 4));
	QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, QPoint(view->width() / 2, view->height() * 3 / 4));
	QVERIFY2(view->isSelected(), "a click did not select the locked clip");
	QCOMPARE(clip->startPosition().getTicks(), 0);

	QVERIFY(run(QStringLiteral("control.undo")).ok);  // the lock's own undo step
	QVERIFY2(!cliplocks::badgeShown(view), "the padlock stayed after the lock was undone");
}
void ArrangerTimelineTest::hiddenTrackRowsDoNotClaimHitTestSpace() {
	Track* second = Track::create(Track::Type::Instrument, Engine::getSong());
	QCoreApplication::processEvents();
	TrackView* firstView = nullptr;
	TrackView* secondView = nullptr;
	for (TrackView* view : m_editor->trackViews())
	{
		if (view->getTrack() == m_track) { firstView = view; }
		if (view->getTrack() == second) { secondView = view; }
	}
	QVERIFY(firstView != nullptr && secondView != nullptr);
	m_track->setVisible(false);
	m_editor->realignTracks();
	QCoreApplication::processEvents();
	QVERIFY(!firstView->isVisible());
	QCOMPARE(m_editor->trackViewAt(0), secondView);
}
void ArrangerTimelineTest::deletingAClipSelectionCreatesOneUndoStep() {
	Clip* first = clipAt(m_track, 0, Bar);
	Clip* second = clipAt(m_track, 2 * Bar, Bar);
	QCoreApplication::processEvents();
	ClipView* firstView = viewOf(first);
	ClipView* secondView = viewOf(second);
	QVERIFY(firstView != nullptr && secondView != nullptr);
	ProjectJournal* journal = Engine::projectJournal();
	QVERIFY(journal != nullptr);
	const int depthBefore = journal->undoDepth();

	ClipView::remove(QVector<ClipView*>{firstView, secondView});
	QCoreApplication::processEvents();
	QCOMPARE(m_track->numOfClips(), 0);
	QCOMPARE(journal->undoDepth(), depthBefore + 1);
	journal->undo();
	QCOMPARE(m_track->numOfClips(), 2);
}
void ArrangerTimelineTest::deletingAClipSelectionAtUndoCapStillCreatesOneStep() {
	Clip* first = clipAt(m_track, 0, Bar);
	Clip* second = clipAt(m_track, 2 * Bar, Bar);
	QCoreApplication::processEvents();
	ClipView* firstView = viewOf(first);
	ClipView* secondView = viewOf(second);
	QVERIFY(firstView != nullptr && secondView != nullptr);
	ProjectJournal* journal = Engine::projectJournal();
	QVERIFY(journal != nullptr);
	journal->clearJournal();
	QVERIFY(journal->setMaxUndoStates(ProjectJournal::MAX_UNDO_STATES));
	for (int i = 0; i < ProjectJournal::MAX_UNDO_STATES; ++i)
	{
		journal->addJournalAction([] {}, [] {});
	}
	QCOMPARE(journal->undoDepth(), ProjectJournal::MAX_UNDO_STATES);

	ClipView::remove(QVector<ClipView*>{firstView, secondView});
	QCoreApplication::processEvents();
	QCOMPARE(m_track->numOfClips(), 0);
	journal->undo();
	QCOMPARE(m_track->numOfClips(), 2);
	QCOMPARE(journal->undoDepth(), ProjectJournal::MAX_UNDO_STATES - 2);
}
void ArrangerTimelineTest::theLockTooltipFollowsTheLockHolder() {
	Clip* clip = clipAt(m_track, 0, 2 * Bar);
	QCoreApplication::processEvents();
	ClipView* view = viewOf(clip);
	QVERIFY(view != nullptr);

	QVERIFY(run(QStringLiteral("track.set_locked"), {{QStringLiteral("track"), control::trackIdOf(m_track)}}).ok);
	QVERIFY(cliplocks::badgeShown(view));
	auto* badge = view->findChild<QWidget*>(QStringLiteral("clipLockBadge"));
	QVERIFY(badge != nullptr);
	QVERIFY2(badge->toolTip().contains(QLatin1String("track")), qPrintable(badge->toolTip()));

	// The clip is locked too: still locked, but now it is this clip that holds the lock.
	QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), control::clipIdOf(clip)}}).ok);
	QVERIFY2(cliplocks::badgeShown(view), "the padlock went when the clip took over the lock");
	QVERIFY2(badge->toolTip().contains(QLatin1String("This clip is locked")), qPrintable(badge->toolTip()));

	// And back again: the track takes the lock back when the clip is unlocked.
	QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("locked"), false}}).ok);
	QVERIFY2(badge->toolTip().contains(QLatin1String("track")), qPrintable(badge->toolTip()));
}
void ArrangerTimelineTest::aLockedClipKeepsEveryMutatingMenuAction() {
	Clip* clip = clipAt(m_track, 0, 2 * Bar);
	QCoreApplication::processEvents();
	ClipView* view = viewOf(clip);
	QVERIFY(view != nullptr);
	QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), control::clipIdOf(clip)}}).ok);
	QVERIFY(cliplocks::badgeShown(view));

	// Auto-resize: the menu item that changes the clicked clips without asking their locks.
	QCOMPARE(clip->getAutoResize(), false);
	view->toggleSelectedAutoResize();
	QCOMPARE(clip->getAutoResize(), false);

	// Mute: the same edit the Ctrl+middle-click gesture already refuses at the mouse press.
	QCOMPARE(clip->isMuted(), false);
	view->toggleMute(QVector<ClipView*>{view});
	QCOMPARE(clip->isMuted(), false);

	// And the color menu, which rewrites every clicked clip too (randomizeColor -> setColor).
	view->randomizeColor();
	QVERIFY2(!clip->color().has_value(), "a locked clip was recolored");

	// An unlocked clip still takes all three, so the lock is what refuses them and not the fixture.
	QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("locked"), false}}).ok);
	view->toggleSelectedAutoResize();
	QCOMPARE(clip->getAutoResize(), true);
	view->toggleMute(QVector<ClipView*>{view});
	QCOMPARE(clip->isMuted(), true);
	view->randomizeColor();
	QVERIFY(clip->color().has_value());
}
void ArrangerTimelineTest::aLockedClipIsNotMutedByCtrlMiddleClick() {
	Clip* clip = clipAt(m_track, 0, 2 * Bar);
	QCoreApplication::processEvents();
	ClipView* view = viewOf(clip);
	QVERIFY(view != nullptr);
	QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), control::clipIdOf(clip)}}).ok);
	QVERIFY(EditLocks::instance().locks(clip));

	// The mouse gesture on a locked clip: refused, and the clip stays where it is.
	const QPoint center(view->width() / 2, view->height() * 3 / 4);
	QTest::mouseClick(view, Qt::MiddleButton, Qt::ControlModifier, center);
	QVERIFY2(!clip->isMuted(), "a locked clip was muted by the Ctrl+middle-click gesture");
	QCOMPARE(m_track->getClips().size(), std::size_t{1});
	QCOMPARE(clip->startPosition().getTicks(), 0);

	// The menu path on the same locked clip: the same answer, so the finding's premise (that the
	// context menu mutes what the mouse refuses) does not hold here.
	view->toggleMute(QVector<ClipView*>{view});
	QVERIFY2(!clip->isMuted(), "a locked clip was muted by the menu path");

	// An unlocked clip takes the very same gesture, so the lock is what refuses it and not the fixture.
	QVERIFY(run(QStringLiteral("clip.set_locked"),
		{{QStringLiteral("clip"), control::clipIdOf(clip)}, {QStringLiteral("locked"), false}}).ok);
	QTest::mouseClick(view, Qt::MiddleButton, Qt::ControlModifier, center);
	QVERIFY2(clip->isMuted(), "an unlocked clip was not muted by the Ctrl+middle-click gesture");
}
void ArrangerTimelineTest::aLockedClipInASelectionStaysWhileTheOthersChange() {
	Clip* locked = clipAt(m_track, 0, Bar);
	Clip* free_ = clipAt(m_track, 2 * Bar, Bar);
	QCoreApplication::processEvents();
	ClipView* lockedView = viewOf(locked);
	ClipView* freeView = viewOf(free_);
	QVERIFY(lockedView != nullptr && freeView != nullptr);
	QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), control::clipIdOf(locked)}}).ok);

	m_editor->selectAllClips(false);
	lockedView->setSelected(true);
	freeView->setSelected(true);

	freeView->toggleSelectedAutoResize();
	QCOMPARE(locked->getAutoResize(), false);
	QCOMPARE(free_->getAutoResize(), true);

	lockedView->toggleMute(QVector<ClipView*>{lockedView, freeView});
	QCOMPARE(locked->isMuted(), false);
	QCOMPARE(free_->isMuted(), true);
}
void ArrangerTimelineTest::recoloringOnlyLockedClipsKeepsTheSongClean() {
	Clip* clip = clipAt(m_track, 0, 2 * Bar);
	QCoreApplication::processEvents();
	ClipView* view = viewOf(clip);
	QVERIFY(view != nullptr);
	QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), control::clipIdOf(clip)}}).ok);
	QVERIFY(EditLocks::instance().locks(clip));

	m_editor->selectAllClips(false);
	QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, QPoint(view->width() / 2, view->height() * 3 / 4));
	QVERIFY(view->isSelected());

	const QString projectPath = m_home.filePath(QStringLiteral("locked-color.mmp"));
	Song* song = Engine::getSong();
	QVERIFY(song->saveProjectFile(projectPath));
	song->noteProjectSaved(projectPath);
	QVERIFY(!song->isModified());

	const auto colorBefore = clip->color();
	view->randomizeColor();
	QVERIFY2(!song->isModified(), "recoloring only locked clips marked the song modified");
	QVERIFY(clip->color() == colorBefore);
}
void ArrangerTimelineTest::aWheelZoomIsContinuousAroundThePointer() {
	clipAt(m_track, 40 * Bar, Bar);
	const tick_t anchor = 4 * Bar;
	const int x = m_editor->timeLine()->markerX(TimePos(anchor));
	const int value = m_editor->zoomingModel()->value();
	const int history = arrangerzoom::historySize();
	arrangerzoom::wheelZoom(m_editor, 12, false, anchor);  // a tenth of a notch: half a step, nothing yet
	QCOMPARE(m_editor->zoomingModel()->value(), value);
	for (int i = 0; i < 19; ++i) { arrangerzoom::wheelZoom(m_editor, 12, false, anchor); }  // twenty tenths: two notches
	QCOMPARE(m_editor->zoomingModel()->value(), value + 2 * arrangerzoom::WheelNotchSteps);
	QVERIFY2(arrangerzoom::historySize() - history <= 1, "one wheel gesture left more than one zoom to go back to");
	QVERIFY2(std::abs(m_editor->timeLine()->markerX(TimePos(anchor)) - x) <= 2, "the tick under the pointer moved");

	// The real wheel over the arranger: Ctrl and one notch, a new gesture, zooms in by a notch's steps.
	QTest::qWait(arrangerzoom::WheelBurstMs + 100);
	const int before = m_editor->zoomingModel()->value();
	const QPoint at = m_editor->timeLine()->mapTo(m_editor, QPoint(m_editor->timeLine()->markerX(TimePos(anchor)), 0))
		+ QPoint(0, m_editor->height() / 2);
	QWheelEvent wheel(at, m_editor->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier,
		Qt::NoScrollPhase, false);
	QCoreApplication::sendEvent(m_editor, &wheel);
	QCOMPARE(m_editor->zoomingModel()->value(), before + arrangerzoom::WheelNotchSteps);
}
void ArrangerTimelineTest::aCrowdedLaneNamesWhatItLeftOutInItsTooltip() {
	auto* lanes = m_editor->findChild<ArrangerRulerLanes*>();
	QVERIFY(lanes != nullptr);
	ProjectMarkers::instance().add(2 * Bar, QStringLiteral("Verse"), QString());
	ProjectMarkers::instance().add(2 * Bar + 4, QStringLiteral("Drop"), QString());  // under a pixel apart at 40 px a bar
	ProjectMarkers::instance().add(8 * Bar, QStringLiteral("Outro"), QString());
	QCOMPARE(lanes->markerCluster(0), (QStringList{QStringLiteral("Verse"), QStringLiteral("Drop")}));
	QCOMPARE(lanes->markerCluster(2), QStringList{QStringLiteral("Outro")});

	const int x = m_editor->timeLine()->markerX(TimePos(3 * Bar));
	const QString tip = lanes->tempoTip(QPoint(x, lanes->laneRect(ArrangerRulerLanes::Lane::Tempo).center().y()));
	QVERIFY2(tip.contains(QString::number(Engine::getSong()->getTempo())), qPrintable(tip));
	QVERIFY(lanes->tempoTip(QPoint(x, lanes->laneRect(ArrangerRulerLanes::Lane::Markers).center().y())).isEmpty());
}
