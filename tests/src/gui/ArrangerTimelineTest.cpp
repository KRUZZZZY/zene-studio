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

/*! 0.5.0 themes 2 and 9, in the real application:
 *  - the ruler lanes: sections, markers and tempo each have a lane under the bar ruler, a marker is drawn
 *    in its lane, and a lane hidden over the control surface (arrangement.ruler_lanes) closes up;
 *  - a keyboard zoom (arrangement.zoom step) keeps the playhead where it is on screen;
 *  - the drag preview shows the ghost, what the drop trims and the snap line, and marks the drop refused
 *    when the clip under it is locked;
 *  - a locked clip wears a padlock (EditLocks' change hook), and a click still selects it without moving it.
 *  Wave 3 (theme 9 finished):
 *  - the wheel zooms by the turn's size around the pointer, one history entry a gesture;
 *  - a crowded lane names what it could not label in its tooltip, and the tempo lane names the tempo;
 *  - a double-click on a section zooms to it; Fit Selection fits the time range;
 *  - the landmarks are found and named, and the navigator is a readable slider a screen reader can set. */

#include <QtTest>

#include <cfenv>
#include <cstdlib>
#include <limits>
#include <tuple>
#include <utility>
#include <vector>

#include <QAbstractSlider>
#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
#include <QRegularExpression>
#include <QMouseEvent>
#include <QImage>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "../core/AudioBurstSupport.h"
#include "ArrangementEdits.h"
#include "ArrangerEditPreview.h"
#include "ArrangerLandmarks.h"
#include "ArrangerNavigator.h"
#include "ArrangementSections.h"
#include "ArrangerOverlays.h"
#include "ArrangerRulerLanes.h"
#include "ArrangerZoom.h"
#include "Clip.h"
#include "ClipLockFeedback.h"
#include "ClipView.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "ProjectJournal.h"
#include "ShellSelection.h"
#include "DesignTokens.h"
#include "DpiHelper.h"
#include "Engine.h"
#include "EditLocks.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InstrumentTrack.h"
#include "MainWindow.h"
#include "MidiClip.h"
#include "Note.h"
#include "PatternClip.h"
#include "PatternClipView.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "PositionLine.h"
#include "ProjectMarkers.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleClipView.h"
#include "SampleFrame.h"
#include "SampleTrack.h"
#include "Song.h"
#include "SongEditor.h"
#include "TrackLabelButton.h"
#include "TrackView.h"
#include "TimeLineWidget.h"
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

//! Drains the per-process zoom walk (the review round 1: an unbounded drain spins against a walk that
//! never runs dry). The walk's back stack holds at most its cap of 32 entries, so twice over is ample;
//! false when it did not drain within the bound, so the case fails here instead of hanging.
bool drainZoomWalk()
{
	constexpr int WalkBound = 64;  // twice the walk's 32-entry cap, plus change
	for (int i = 0; i < WalkBound; ++i)
	{
		if (!run(QStringLiteral("arrangement.zoom_back")).ok) { return true; }
	}
	return false;
}

Clip* clipAt(Track* track, tick_t start, tick_t length)
{
	Clip* clip = track->createClip(TimePos(start));
	clip->changeLength(TimePos(length));
	clip->setAutoResize(false);
	return clip;
}

ClipView* viewOf(const Clip* clip)
{
	for (ClipView* view : getGUI()->songEditor()->m_editor->findChildren<ClipView*>())
	{
		if (view->getClip() == clip) { return view; }
	}
	return nullptr;
}

} // namespace

class TestTrackLabel final : public TrackLabelButton
{
public:
	using TrackLabelButton::TrackLabelButton;
	using TrackLabelButton::mousePressEvent;
};

class ArrangerTimelineTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();

	void cleanupTestCase();

	void init();

	//! Q050-322: a focused track header and the ruler both open their real menu with Menu and
	//! Shift+F10, with the same actions the mouse path offers.
	void trackHeaderAndRulerOpenMenusFromTheKeyboard();

	//! A canceled pinch that has already zoomed still leaves its starting zoom for arrangement.zoom_back.
	void aCanceledPinchKeepsItsStartingZoomInHistory();

	//! 0.5.0 L13 (WF-094): back and forward step over the several prior extents, each restored exactly,
	//! forward refused where nothing was walked past, and a new zoom cuts the forward walk off.
	void backAndForwardWalkSeveralPriorZooms();

	//! 0.5.0 L13 (WF-094): the toggle swaps the current extent with the one it last left, and swaps back.
	void zoomToggleSwapsTheCurrentAndPriorExtent();

	//! 0.5.0 L13 (WF-094, the round-1 review): the toggle's swap reconciles the walk, so Back or
	//! Forward straight after it either is refused or really changes the zoom - never a successful no-op.
	void backAndForwardAfterTheToggleNeverNoOp();

	void theRulerHasALaneEachUnderTheBarRuler();

	//! The numbered bar ruler, not the lanes, is what a click seeks on, so it has to be a band of its own with
	//! a usable height: listed once in the song editor's layout, and clicked near bar 3 it puts the play position
	//! there, and a drag along it follows the pointer.
	void theBarRulerHasAUsableHeightAndSeeksWhereItIsDragged();

	//! The ruler is laid out by the arranger's column and nothing else sizes it: a plain widget has no minimum, so
	//! the column used to give it no height and its bar numbers and loop brace were not visible or clickable.
	//! Checked with no test-side resize (the punch-band test below resizes the ruler itself to get a height).
	void theRulerHasAUsableLayoutHeight();

	//! 0.5.0 E23: the chord track has a lane under Tempo - it takes room only while the chord track holds a
	//! chord, follows a chord set and an undo without a signal, and a click on a chord seeks there.
	void theChordTrackIsDrawnInItsLane();

	//! Hiding every lane keeps the ruler on screen as a strip: its context menu is the only in-UI way to show a lane
	//! again, and a hidden ruler takes no right-click.
	void aRulerWithEveryLaneHiddenKeepsItsContextMenuReachable();

	void aKeyboardZoomKeepsThePlayheadInPlace();

	void anOversizedZoomStepClampsToMaximum();

	//! gui-1106: Navigate > Zoom Out (step -4) at the minimum zoom - the 4 px per bar a fresh project can
	//! inherit from the saved UI setting - must say it cannot zoom further instead of reporting a success
	//! that left the view unchanged; likewise Zoom In at the maximum. A step off the bound still zooms.
	void aZoomStepAtItsBoundIsRefusedNotASilentSuccess();

	void aPatternPreviewCombinesOffsetAndNoteBeforeRasterizing();

	void anEmptyPatternStorePaintDoesNotRaiseInvalidFloatException();

	void aZeroLengthMidiClipWithNotesPreviewsWithoutDividingByItsLength();

	void theDragPreviewShowsWhatTheDropWillDo();

	void theDragPreviewDoesNotWrapAnEndNearTheTickLimit();

	void aLockedClipWearsAPadlockAndStillSelects();

	//! Hidden rows keep QWidget height, but must not claim hit-test space in the arranger.
	void hiddenTrackRowsDoNotClaimHitTestSpace();

	//! One Delete gesture over a selection is one undo step, even though each clip removal journals its track.
	void deletingAClipSelectionCreatesOneUndoStep();

	//! The cap can evict steps under a selection delete; its merge window is still one undo.
	void deletingAClipSelectionAtUndoCapStillCreatesOneStep();

	//! Luna cluster 4674: the badge stays locked either way, so the tooltip must be refreshed when the
	//! lock holder moves from the track to the clip - not only when the locked state itself flips.
	void theLockTooltipFollowsTheLockHolder();

	//! Luna cluster 5578: the lock check hides Delete/Cut and disables Paste, but every other mutating
	//! context-menu action went through anyway - so a right-clicked locked clip could be muted, recolored or
	//! toggled to auto-resize, and the mouse's own Ctrl+middle-click mute was refused where the menu's was not.
	void aLockedClipKeepsEveryMutatingMenuAction();

	//! Cluster new-p11-8 (ported): the Ctrl+middle-click mute is refused for a locked clip, and the
	//! menu path refuses it too - the edit must not depend on how it was asked for (Luna cluster 5578).
	//! The mouse gesture and the menu are one answer, and an unlocked clip takes the very same gesture.
	void aLockedClipIsNotMutedByCtrlMiddleClick();

	//! Luna cluster 5578: a lock in a SELECTION keeps the locked clip as it is and still changes the
	//! unlocked ones, exactly like Delete (WF-044) - the whole selection is not refused just because one
	//! member is locked.
	void aLockedClipInASelectionStaysWhileTheOthersChange();

	void recoloringOnlyLockedClipsKeepsTheSongClean();

	//! 0.5.0 theme 9 (wave 3): a wheel turn zooms by its size - a touchpad's tenth-of-a-notch deltas add up to a
	//! notch's steps - around the tick under the pointer, and one gesture joins the zoom history once.
	void aWheelZoomIsContinuousAroundThePointer();

	//! Wave 3: no label collides - what a crowded lane leaves out, its tooltip names.
	void aCrowdedLaneNamesWhatItLeftOutInItsTooltip();

	//! Wave 3 (Bitwig's Alt-click on a cue): a double-click on a section fills the arranger with it, and Fit Selection
	//! fits the time range when there is one.
	void aDoubleClickZoomsToASectionAndFitSelectionFitsTheRange();

	void theRulerShowsASectionCanBeMovedAndAClickStillSelects();

	void aSectionDragInGapModeMovesOnlyItsMaterial();

	void aSectionDragInRippleModeReordersTheSong();

	void aSectionMoveIsRefusedOverALockedClip();

	void theLanesMenuChoosesGapOrRipple();

	void theLandmarksAreFoundAndNamed();

	//! Wave 3: the navigator keeps a readable size from the design tokens, rings its keyboard focus, and is a
	//! slider - the arranger's start - that follows the arranger and that a screen reader can set.
	void theNavigatorIsAReadableSliderOverTheSong();

	//! The navigator names the bars it SHOWS. The view's end is exclusive, so a view that ends exactly on a
	//! bar line shows one bar fewer than its length in bars - naming the end tick's own bar adds a bar that is
	//! not on screen.
	void theNavigatorNamesTheLastBarItShows();

	//! A range whose BOTH ends fall off the track rows resolves each to the nearest boundary track, so the
	//! gesture reaches the tracks it was dragged across instead of selecting none.
	void aRangeWithBothEndsOffTheRowsReachesTheBoundaryTrack();

	//! A drag cut short - the grab taken away by a popup, the overlay hidden - does not go on dispatching:
	//! the gesture belongs to the press that began it.
	void aRangeDragCutShortByTheGrabBeingTakenStops();

	//! A tool chosen while an audition is held ends it: the gesture belongs to the tool that began it, so the
	//! release finishes the audition instead of dispatching a range with the tool that replaced it.
	void switchingToolsDuringAHeldAuditionStopsIt();

	//! BUG new-p10-302: the song editor's position line takes its width from the zoom. A NaN or infinite zoom is refused
	//! (the line keeps its width and place), and a huge finite zoom is clamped to the widget limit instead of being
	//! converted to an int out of range. The real zoom path (arrangement.zoom) still sets the width as before.
	void positionLineZoomRefusesNonFiniteAndClampsOversized();

	//! QtTest runs only the private slots. A test method declared under `private:` is never run, so this asks the
	//! meta-object for each of the timeline tests that must run.
	void everyTimelineTestIsAPrivateSlot();

	//! The REAL arrangement.nudge (its production declaration, through the arranger): three presses on one clip are
	//! one undo step and one record, and control.undo puts the clip back where it started. A press on another clip
	//! starts its own run.
	void aBurstOfRealNudgesIsOneUndoStep();

	void theNavigatorRangeIncludesSongsNearTheTickLimit();

	void oversizedScrollPositionsReachTheEndOfTheArranger();

	/*! ED-150: the transient overlay of a sample clip shows the transients of the audio the clip
	 *  PLAYS. A reversed clip sounds the MIRROR of its window (Sample::render reverses the whole
	 *  buffer), so the overlay's analysed range moves with the direction - and the overlay's cache
	 *  (analysed once per audio and per range) has to move with it: the placement alone only
	 *  re-maps the frames the cache has, and those are the frames of the range the clip played
	 *  BEFORE the reversal.
	 *
	 *  A trimmed clip makes the difference visible: its window covers the first half of the
	 *  source, so before the reversal the overlay shows the two bursts of that half and after it
	 *  the two bursts of the other half, heard backwards. */
	void aReversedTrimmedClipDrawsTheTransientsOfTheAudioItPlays();

private:
	//! Q050-219: the song the section-move cases share - a section over bars 4..8, material inside it on one track,
	//! crossing its start on another, past it on a third, and a second section over bars 9..10.
	QString sectionSong(Track** second, Track** third)
	{
		*second = Track::create(Track::Type::Sample, Engine::getSong());
		*third = Track::create(Track::Type::Sample, Engine::getSong());
		clipAt(m_track, 0, 2 * Bar);
		clipAt(m_track, 4 * Bar, 4 * Bar);
		clipAt(m_track, 8 * Bar, 2 * Bar);
		clipAt(*second, 3 * Bar, 3 * Bar);
		clipAt(*third, 9 * Bar, Bar);
		run(QStringLiteral("section.add"), {{QStringLiteral("start"), 9 * Bar}, {QStringLiteral("end"), 10 * Bar}, {QStringLiteral("name"), QStringLiteral("Bridge")}});
		const ControlResult added = run(QStringLiteral("section.add"), {{QStringLiteral("start"), 4 * Bar}, {QStringLiteral("end"), 8 * Bar},
			{QStringLiteral("name"), QStringLiteral("Chorus")}, {QStringLiteral("color"), QStringLiteral("#336699")}});
		QCoreApplication::processEvents();
		return added.result.value(QStringLiteral("section")).toString();
	}

	//! Press on the Chorus block (bars 4..8), let go @a bars later (or earlier).
	void dragChorus(int bars, Qt::KeyboardModifiers mods = Qt::NoModifier);

	static std::vector<std::pair<tick_t, tick_t>> layoutOf(const Track* track)
	{
		std::vector<std::pair<tick_t, tick_t>> out;
		for (const Clip* clip : track->getClips()) { out.emplace_back(clip->startPosition().getTicks(), clip->length().getTicks()); }
		std::sort(out.begin(), out.end());
		return out;
	}

	QTemporaryDir m_home;
	SongEditor* m_editor = nullptr;
	Track* m_track = nullptr;
};

#include "ArrangerTimelineTestParts1.h"
#include "ArrangerTimelineTestParts2.h"
#include "ArrangerTimelineTestParts3.h"
#include "ArrangerTimelineTestParts4.h"

QTEST_MAIN(ArrangerTimelineTest)
#include "ArrangerTimelineTest.moc"
