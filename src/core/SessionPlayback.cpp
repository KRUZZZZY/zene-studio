/*
 * SessionPlayback.cpp - the published content of session cells, and the view the
 * render path reads (board card #597).
 *
 * WHY SessionScheduler MEMBERS ARE DEFINED IN THIS FILE. Both SessionScheduler.h
 * and SessionScheduler.cpp are held against the whole-tree file-length ratchet
 * (500 lines; the header is AT it and the .cpp is within 9 lines of it), which is
 * the same reason the Follow Actions bodies live in SessionFollow.cpp. The new
 * bodies therefore live here, in the translation unit that owns the feature, and
 * the header carries only the declarations. Member definitions are not required
 * to sit beside their class - see include/SessionScheduler.h's own note about it.
 */
#include "SessionPlayback.h"

#include <algorithm>

#include "SessionScheduler.h"

namespace lmms
{

bool SessionSlotContentTable::install( int track, int scene, int patternId,
	tick_t loopLengthTicks, bool legato, tick_t startTicks ) noexcept
{
	// Replace in place when the cell is already known: re-publishing a cell is
	// the ordinary case (the model edits a slot), and it must not consume a
	// second entry.
	for( std::size_t i = 0; i < m_count; ++i )
	{
		if( m_entries[i].track == track && m_entries[i].scene == scene )
		{
			m_entries[i].patternId = patternId;
			m_entries[i].loopLengthTicks = loopLengthTicks;
			m_entries[i].legato = legato;
			m_entries[i].startTicks = startTicks;
			return true;
		}
	}
	if( m_count >= Capacity )
	{
		return false;
	}
	m_entries[m_count].track = track;
	m_entries[m_count].scene = scene;
	m_entries[m_count].patternId = patternId;
	m_entries[m_count].loopLengthTicks = loopLengthTicks;
	m_entries[m_count].legato = legato;
	m_entries[m_count].startTicks = startTicks;
	++m_count;
	return true;
}


const SessionSlotContent* SessionSlotContentTable::find( int track, int scene ) const noexcept
{
	for( std::size_t i = 0; i < m_count; ++i )
	{
		if( m_entries[i].track == track && m_entries[i].scene == scene )
		{
			return &m_entries[i];
		}
	}
	return nullptr;
}


void SessionSlotContentTable::clear() noexcept
{
	for( std::size_t i = 0; i < m_count; ++i )
	{
		m_entries[i] = SessionSlotContent{};
	}
	m_count = 0;
}


bool SessionScheduler::publishSlotContent( int track, int scene, int patternId,
	tick_t loopLengthTicks, bool legato, tick_t startTicks ) noexcept
{
	// One queue push, exactly like requestLaunch/installFollowPlan: no
	// allocation, no lock, no syscall. The audio thread applies it into the
	// fixed table, so a cell's content is never read from model-thread storage.
	Command command;
	command.track = track;
	command.scene = scene;
	command.type = LaunchCommandType::Content;
	command.content = SessionSlotContent{ track, scene, patternId, loopLengthTicks, legato, startTicks };
	return enqueue( command );
}


bool SessionScheduler::applySlotContent( const SessionSlotContent& content ) noexcept
{
	// Audio thread, from drainCommands(). A bounded walk over fixed storage; a
	// full table refuses rather than growing (the capacity and the consequence
	// are stated in include/SessionPlayback.h). A refused cell keeps the
	// pre-#597 behaviour for that slot: it takes its column over and renders
	// nothing, which is observable rather than a crash or a clamp.
	return m_slotContent.install( content.track, content.scene, content.patternId,
		content.loopLengthTicks, content.legato, content.startTicks );
}


SessionSlotPlayback SessionScheduler::playbackForColumn( int track ) const noexcept
{
	SessionSlotPlayback view;
	for( const auto& slot : m_active )
	{
		if( slot.track != track )
		{
			continue;
		}
		// StopPending counts as playing: the clip has not stopped yet, so the
		// column is still rendering its content (the same rule
		// trackIsSessionActive() states).
		if( slot.state.phase != SlotPhase::Playing
			&& slot.state.phase != SlotPhase::StopPending )
		{
			continue;
		}

		view.playing = true;

		// What the cell holds. Absent content leaves patternId < 0, which is the
		// pre-#597 behaviour: the column is taken over and renders nothing.
		const SessionSlotContent* content = m_slotContent.find( slot.track, slot.scene );
		tick_t start = 0;
		if( content != nullptr )
		{
			view.patternId = content->patternId;
			view.loopLengthTicks = content->loopLengthTicks;
			start = content->startTicks;  // WF-060: the cell's own start inside its clip
		}

		// Where in the clip this period starts: the session clock minus the
		// launch the slot is currently playing - or, for a legato launch, minus
		// the phase origin it inherited from the clip it took over (E1) - wrapped
		// by the clip's own length when the model published one.
		const tick_t origin = slot.phaseOriginFor == slot.state.startedTick ? slot.phaseOrigin : slot.state.startedTick;
		tick_t position = m_positionTicks - origin;
		if( position < 0 )
		{
			position = 0;
		}
		if( view.loopLengthTicks > 0 )
		{
			position %= view.loopLengthTicks;
		}
		position += start;
		view.positionTicks = position;
		return view;   // the column's first active slot is the one playing
	}
	return view;
}

int publishSessionSlotContent( SessionScheduler& scheduler, const SessionModel& model ) noexcept
{
	const int tracks = model.trackCount();
	const int scenes = model.sceneCount();
	int published = 0;
	for( int track = 0; track < tracks; ++track )
	{
		for( int scene = 0; scene < scenes; ++scene )
		{
			const ClipSlot& slot = model.slot( track, scene );

			// A MIDI cell publishes the pattern it references. Anything else -
			// an empty cell, or an audio cell (whose playback path is a later
			// slice) - publishes "nothing to render" explicitly, so a cell that
			// changed kind stops rendering instead of keeping a stale pattern.
			const int patternId = slot.isEmpty() ? -1 : slot.patternId();

			// The loop length is left 0: PatternStore::play wraps the position
			// by the pattern's own length (src/core/PatternStore.cpp:56), so the
			// render is correct without the model computing one, and a length
			// published here would be a second source of truth for it.
			if( scheduler.publishSlotContent( track, scene, patternId, 0, slot.legato(), slot.loopStart() ) )
			{
				++published;
			}
		}
	}
	return published;
}

bool SessionScheduler::requestJump( int track, int scene, tick_t position ) noexcept
{
	Command command{ track, scene, LaunchCommandType::Jump };
	command.position = position;
	return enqueue( command );
}

bool SessionScheduler::applyJump( const Command& command ) noexcept
{
	// Audio thread, from drainCommands(): the playing slot's phase now counts from a tick that puts it
	// `position` into its clip. A slot that is not playing ignores it (nothing to move).
	if( command.type != LaunchCommandType::Jump ) { return false; }
	ActiveSlot* slot = findSlot( command.track, command.scene );
	if( slot != nullptr && ( slot->state.phase == SlotPhase::Playing || slot->state.phase == SlotPhase::StopPending ) )
	{
		// 'position' is into the clip; the rendered position adds the cell's start, so take it off here (review W3-batchA A2).
		const SessionSlotContent* content = m_slotContent.find( command.track, command.scene );
		const tick_t start = content != nullptr ? content->startTicks : 0;
		slot->phaseOrigin = m_positionTicks - std::max<tick_t>( 0, command.position - start );
		slot->phaseOriginFor = slot->state.startedTick;
	}
	return true;
}

} // namespace lmms
