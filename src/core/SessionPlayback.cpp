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

#include "Engine.h"
#include "SessionAudioCells.h"
#include "SessionScheduler.h"
#include "Song.h"

namespace lmms
{

namespace
{

bool hasCurrentPlaybackTake( const SlotLaunchState& state ) noexcept
{
	return state.phase == SlotPhase::Playing
		|| state.phase == SlotPhase::StopPending
		|| ( state.phase == SlotPhase::LaunchPending && state.startCount > 0 );
}

} // namespace


bool SessionSlotContentTable::install( int track, int scene, int patternId,
	tick_t loopLengthTicks, bool legato, tick_t startTicks, const SampleBuffer* clip, const SessionAudioShape& audio ) noexcept
{
	// A cell has something to render when it holds a pattern OR an audio clip (WF-055: an
	// audio cell publishes patternId -1 with its clip, so patternId alone cannot tell an
	// audio cell from an empty one).
	const bool hasContent = patternId >= 0 || clip != nullptr;
	// Replace in place when the cell is already known: re-publishing a cell is
	// the ordinary case (the model edits a slot), and it must not consume a
	// second entry.
	for( std::size_t i = 0; i < m_count; ++i )
	{
		if( m_entries[i].track == track && m_entries[i].scene == scene )
		{
			if( !hasContent )
			{
				// A cell that LOSES its content gives its entry back: the last
				// entry moves into the hole and the count drops. A bounded move
				// over fixed storage, so the audio thread neither allocates nor
				// locks, and a churn of real -> empty cells cannot fill the table.
				m_entries[i] = m_entries[m_count - 1];
				m_entries[m_count - 1] = SessionSlotContent{};
				--m_count;
				return true;
			}
			m_entries[i].patternId = patternId;
			m_entries[i].loopLengthTicks = loopLengthTicks;
			m_entries[i].legato = legato;
			m_entries[i].startTicks = startTicks;
			m_entries[i].clip = clip;
			m_entries[i].audio = audio;
			return true;
		}
	}
	// A cell with NOTHING to render takes no entry: an absent entry already means
	// patternId < 0 to every reader (find() returning nullptr is the pre-#597
	// "nothing to render" the render path handles). The model publishes EVERY
	// cell of the grid, empty ones included, so claiming an entry for each one
	// filled the fixed table with cells that render nothing - and the MIDI cell
	// added to a grid one column wider was refused and played silently.
	if( !hasContent )
	{
		return true;
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
	m_entries[m_count].clip = clip;
	m_entries[m_count].audio = audio;
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
	tick_t loopLengthTicks, bool legato, tick_t startTicks, const SampleBuffer* clip, const SessionAudioShape& audio ) noexcept
{
	// One queue push, exactly like requestLaunch/installFollowPlan: no
	// allocation, no lock, no syscall. The audio thread applies it into the
	// fixed table, so a cell's content is never read from model-thread storage.
	Command command;
	command.track = track;
	command.scene = scene;
	command.type = LaunchCommandType::Content;
	command.content = SessionSlotContent{ track, scene, patternId, loopLengthTicks, legato, startTicks, clip, audio };
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
		content.loopLengthTicks, content.legato, content.startTicks, content.clip, content.audio );
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
		// StopPending counts as playing: the clip has not stopped yet. A pending
		// retrigger also keeps rendering the last started take until its grid
		// line. These are the same active-state rules as trackIsSessionActive().
		if( !hasCurrentPlaybackTake( slot.state ) )
		{
			continue;
		}

		view.playing = true;
		view.scene = slot.scene;
		view.startedTick = slot.state.startedTick;

		// What the cell holds. Absent content leaves patternId < 0, which is the
		// pre-#597 behaviour: the column is taken over and renders nothing.
		const SessionSlotContent* content = m_slotContent.find( slot.track, slot.scene );
		tick_t start = 0;
		if( content != nullptr )
		{
			view.patternId = content->patternId;
			view.loopLengthTicks = content->loopLengthTicks;
			view.audio = content->audio;
			view.clip = content->clip;  // WF-055: an audio cell's clip, rendered by SessionCellPlayHandle
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

bool publishSessionCell( SessionScheduler& scheduler, int track, int scene, const ClipSlot& slot ) noexcept
{
	// A MIDI cell publishes the pattern it references. Anything else -
	// an empty cell, or an audio cell (whose playback path is a later
	// slice) - publishes "nothing to render" explicitly, so a cell that
	// changed kind stops rendering instead of keeping a stale pattern.
	const int patternId = slot.isEmpty() ? -1 : slot.patternId();
	// WF-055: an audio cell's clip, loaded (and its column's track given a renderer) here, on the
	// model thread; nullptr for every other cell, so a cell that stopped being audio stops sounding.
	Song* song = Engine::getSong();
	sessionaudio::Prepared audio;
	if( song != nullptr && slot.type() == ClipSlot::Type::Audio )
	{
		audio = sessionaudio::prepare( *song, track, slot, song->getTempo() );
	}
	const SampleBuffer* clip = audio.clip;

	// The loop length is left 0: PatternStore::play wraps the position
	// by the pattern's own length (src/core/PatternStore.cpp:56), so the
	// render is correct without the model computing one, and a length
	// published here would be a second source of truth for it.
	return scheduler.publishSlotContent( track, scene, patternId, 0, slot.legato(), slot.loopStart(), clip, audio.shape );
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
			if( publishSessionCell( scheduler, track, scene, model.slot( track, scene ) ) )
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
