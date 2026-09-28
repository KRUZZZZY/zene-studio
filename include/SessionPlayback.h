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

#include "LmmsTypes.h"

namespace lmms
{

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
	 *  Model thread (called via the scheduler's queue). No allocation, no lock.
	 *  False when the table has no free entry - the caller counts it. */
	bool install( int track, int scene, int patternId, tick_t loopLengthTicks ) noexcept;

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
 *  a stale pattern. */
int publishSessionSlotContent( SessionScheduler& scheduler, const SessionModel& model ) noexcept;

} // namespace lmms

#endif // LMMS_SESSION_PLAYBACK_H
