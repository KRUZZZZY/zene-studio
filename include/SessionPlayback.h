/*
 * SessionPlayback.h - what a launched session slot PLAYS (board card #597).
 *
 * THE GAP THIS CLOSES. The launch engine knows *that* a cell is playing and
 * *since when*; nothing knows *what it holds*. So a launched slot took its track
 * over (SPEC A1) and rendered silence - the "rendered audio matches the session
 * playback" half of #596's acceptance was unmet (docs/KNOWN-LIMITATIONS.md).
 *
 * WHY A TABLE AND NOT A MODEL READ. The audio thread may not read SessionModel:
 * it is model-thread storage (a QVector), and the scheduler's own contract is
 * that nothing but a lock-free hand-off crosses over (see the file comment in
 * include/SessionScheduler.h). A cell's content therefore crosses the same way a
 * Follow Actions plan already does (installFollowPlan -> m_followPlans): a fixed
 * table written through the command queue and read on the audio thread. This
 * header carries that table and the view the render path reads; the scheduler
 * owns one of each.
 *
 * WHAT A MIDI SLOT'S CONTENT IS. A ClipSlot stores a PatternStore id and nothing
 * else - `session.set_slot`'s own refusal names it: "type 'midi' needs
 * 'pattern': the PatternStore id the slot references". A pattern's notes live on
 * the pattern store's tracks, each with its own instrument, so that is what a
 * launched MIDI slot renders. The column decides WHICH cell launched and which
 * song track its arrangement content is suppressed on; it does not choose the
 * instrument. See docs/SESSION-PLAYBACK-SLICE0.md for the full reasoning.
 */
#ifndef LMMS_SESSION_PLAYBACK_H
#define LMMS_SESSION_PLAYBACK_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "LmmsTypes.h"

namespace lmms
{

class SampleBuffer;

/*! How an AUDIO cell's clip is played (Q050-013): the cell's gain, and its loop region as frames of the clip
 *  that is published (which is already warped and transposed, on the model thread: SessionAudioBake.h). A
 *  plain value, so the audio thread reads it without a lock. */
struct SessionAudioShape
{
	float gain = 1.0f;  //!< linear
	std::int64_t loopStartFrames = 0;
	std::int64_t loopLengthFrames = 0;  //!< 0: the loop is the whole clip
};

/*! One cell's content, as the audio thread sees it. */
struct SessionSlotContent
{
	int track = -1;
	int scene = -1;
	/*! The PatternStore id this slot renders. Negative means "nothing to
	 *  render" - an empty cell, or a slot whose content was never published. */
	int patternId = -1;
	/*! The clip's loop length in ticks, or 0 when the model did not publish one
	 *  (the render then leaves the wrapping to the pattern store). */
	tick_t loopLengthTicks = 0;
	/*! Legato (engine audit E1): launching this cell while another clip plays on its column
	 *  continues that clip's phase instead of starting the new one from its top. */
	bool legato = false;
	//! WF-060: where in the clip a launch starts (the cell's loop start), in ticks.
	tick_t startTicks = 0;
	/*! WF-055: an AUDIO cell's clip at the engine's rate, or nullptr. Owned on the model thread by
	 *  include/SessionAudioCells.h, which keeps it alive while the scheduler can name it. */
	const SampleBuffer* clip = nullptr;
	SessionAudioShape audio;  //!< Q050-013: the audio cell's gain and loop region
};


/*! The published content of every cell the engine knows about.
 *
 * A FIXED table, not a container that grows: an install must not allocate, and a
 * full table must refuse and be countable rather than quietly drop a cell. The
 * capacity is the engine's active-slot bound (SessionScheduler::MaxActiveSlots,
 * 64) doubled, so a project can hold content for its whole grid while only a
 * fraction of it can ever play at once; a project with more non-empty MIDI cells
 * than this still renders the cells that fit and reports the rest as unpublishable
 * (install() returns false).
 */
class SessionSlotContentTable
{
public:
	static constexpr std::size_t Capacity = 128;

	/*! Stores one cell's content, REPLACING any previous entry for that cell.
	 *  A cell with neither a patternId (>= 0) nor an audio clip has nothing to
	 *  render: its entry is removed (or none is taken), so the table holds only
	 *  cells that do. An audio cell publishes patternId -1 WITH its clip, and keeps its entry.
	 *  Audio thread (applied from the scheduler's queue). No allocation, no lock.
	 *  False when the table has no free entry - the caller counts it. */
	bool install( int track, int scene, int patternId, tick_t loopLengthTicks, bool legato = false, tick_t startTicks = 0,
		const SampleBuffer* clip = nullptr, const SessionAudioShape& audio = {} ) noexcept;

	/*! The content stored for a cell, or nullptr. AUDIO THREAD: a bounded walk
	 *  over fixed storage, so it allocates nothing and locks nothing. */
	const SessionSlotContent* find( int track, int scene ) const noexcept;

	//! Entries currently holding a cell. Audio thread.
	std::size_t installedCount() const noexcept
	{
		return m_count;
	}

	//! Drops every entry (a project change). Model thread, off the audio thread.
	void clear() noexcept;

private:
	std::array<SessionSlotContent, Capacity> m_entries{};
	std::size_t m_count = 0;
};


/*! What one track column should render this period. Audio thread.
 *
 * `playing` false means the column has no slot to render, which is the case for
 * every column in a project where nothing was launched - the render path then
 * behaves exactly as it did before this feature existed. */
struct SessionSlotPlayback
{
	bool playing = false;
	//! < 0: the cell is playing but holds nothing to render (an empty slot, or
	//! one whose content was never published) - the column stays silent.
	int patternId = -1;
	//! Where in the clip this period starts, in ticks.
	tick_t positionTicks = 0;
	//! The published loop length, or 0 when none was published.
	tick_t loopLengthTicks = 0;
	//! WF-055: the audio cell's clip (nullptr for a MIDI or empty cell), and which launch is playing - the
	//! scene and the line it started on - so a renderer can tell a new launch from the same one going on.
	const SampleBuffer* clip = nullptr;
	SessionAudioShape audio;  //!< Q050-013: the clip's gain and loop region
	int scene = -1;
	tick_t startedTick = -1;
};


class SessionScheduler;
class SessionModel;

/*! Publishes every non-empty MIDI cell of \a model to \a scheduler, so a launch
 *  of any of them renders. MODEL THREAD (it reads the session model, which the
 *  audio thread may not). Returns how many cells were queued; a cell whose
 *  queue push failed is left unpublished and renders nothing, as before.
 *
 *  Publishing the whole grid rather than only the cell being launched is what
 *  makes Follow Actions work: a chain fires on the audio thread and chooses a
 *  cell the model thread never named. A cell that became empty or became an
 *  AUDIO cell publishes patternId < 0, so it stops rendering rather than keeping
 *  a stale pattern; an audio cell publishes its clip instead (WF-055,
 *  include/SessionAudioCells.h), loaded here on the model thread. */
int publishSessionSlotContent( SessionScheduler& scheduler, const SessionModel& model ) noexcept;

class ClipSlot;

//! Publishes one cell, as publishSessionSlotContent publishes each. MODEL THREAD; one queue push.
bool publishSessionCell( SessionScheduler& scheduler, int track, int scene, const ClipSlot& slot ) noexcept;

/*! Q050-489: THE transition boundary between the authored grid and the launcher. Every change of the session
 *  model after a project is open - a session.* edit, its undo and its redo, a capture or a landed take, a pattern
 *  deleted or moved under the cells - ends here, with the model as it was (@a before) and as it is (@a after).
 *
 *  For each cell whose CONTENT changed (sameSessionContent) the new content is published, so a playing cell
 *  sounds what the grid now holds instead of what it held at the last launch; a cell that held a clip and now
 *  holds none (cleared, or outside a shrunk grid) is also stopped, exactly as session.stop_slot stops it, so the
 *  launcher never reports - and the track is never taken over by - a cell the grid no longer has. Nothing else is
 *  touched: launch settings, follow chains and scenes are read at the next launch, as before.
 *
 *  MODEL THREAD. Bounded by the cells that changed, and only queue pushes reach the engine (no allocation or
 *  lock on the audio thread). Returns the cells published. */
int syncSessionLauncher( SessionScheduler& scheduler, const SessionModel& before, const SessionModel& after ) noexcept;

class Track;
struct SessionCellCopy;
//! Q050-489: the launcher cells that play @a track's pattern (none for any other track), for its deletion's undo.
std::vector<SessionCellCopy> sessionCellsOfPatternTrack( Track* track );
//! Q050-489: a deleted pattern track's undo - @a cells play @a restored (its new number) again; the launcher follows.
void restoreSessionPatternCells( const std::vector<SessionCellCopy>& cells, Track* restored );

//! Whether two cells render the same thing: the fields publishSessionCell sends (and the audio bake reads).
bool sameSessionContent( const ClipSlot& a, const ClipSlot& b ) noexcept;

} // namespace lmms

#endif // LMMS_SESSION_PLAYBACK_H
