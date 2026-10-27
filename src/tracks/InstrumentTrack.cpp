/*
 * InstrumentTrack.cpp - implementation of InstrumentTrack class
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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
#include "InstrumentTrack.h"
#include "TakeLane.h"

#include "AudioEngine.h"
#include "AutomationClip.h"
#include "ConfigManager.h"
#include "ControllerConnection.h"
#include "DataFile.h"
#include "Mixer.h"
#include "InstrumentTrackView.h"
#include "Instrument.h"
#include "Keymap.h"
#include "MidiClient.h"
#include "MidiClip.h"
#include "MidiPlayback.h"
#include "MixHelpers.h"
#include "NoteRandom.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "Pitch.h"
#include "Song.h"
#include "MissingDevices.h"
#include "PluginVendors.h"

namespace lmms
{


InstrumentTrack::InstrumentTrack(TrackContainer* tc) :
	Track(Track::Type::Instrument, tc),
	MidiEventProcessor(),
	m_midiPort(tr("unnamed_track"), Engine::audioEngine()->midiClient(), this, this),
	m_notes(),
	m_sustainPedalPressed(false),
	m_silentBuffersProcessed(false),
	m_previewMode(false),
	m_baseNoteModel(0, 0, NumKeys - 1, this, tr("Base note")),
	m_firstKeyModel(0, 0, NumKeys - 1, this, tr("First note")),
	m_lastKeyModel(0, 0, NumKeys - 1, this, tr("Last note")),
	m_hasAutoMidiDev(false),
	m_volumeModel(DefaultVolume, MinVolume, MaxVolume, 0.1f, this, tr("Volume")),
	m_panningModel(DefaultPanning, PanningLeft, PanningRight, 0.1f, this, tr("Panning")),
	m_audioBusHandle(tr("unnamed_track"), true, &m_volumeModel, &m_panningModel, &m_mutedModel),
	m_pitchModel(0, MinPitchDefault, MaxPitchDefault, 1, this, tr("Pitch")),
	m_pitchRangeModel(1, 1, 60, this, tr("Pitch range")),
	m_mixerChannelModel(0, 0, 0, this, tr("Mixer channel")),
	m_useMasterPitchModel(true, this, tr("Master pitch")),
	m_instrument(nullptr),
	m_soundShaping(this),
	m_arpeggio(this),
	m_noteStacking(this),
	m_piano(this),
	m_microtuner()
{
	m_pitchModel.setCenterValue( 0 );
	m_pitchModel.setStrictStepSize(true);
	m_panningModel.setCenterValue( DefaultPanning );
	m_baseNoteModel.setInitValue( DefaultKey );
	m_firstKeyModel.setInitValue(0);
	m_lastKeyModel.setInitValue(NumKeys - 1);

	m_mixerChannelModel.setRange( 0, Engine::mixer()->numChannels()-1, 1);

	for( int i = 0; i < NumKeys; ++i )
	{
		m_notes[i] = nullptr;
		m_runningMidiNotes[i] = 0;
	}


	// Initialize the m_midiCCEnabled variable, but it's actually going to be connected
	// to a LedButton
	m_midiCCEnable = std::make_unique<BoolModel>(false, nullptr, tr("Enable/Disable MIDI CC"));

	// Initialize the MIDI CC controller models and connect them to the method that processes
	// the midi cc events
	for (int i = 0; i < MidiControllerCount; ++i)
	{
		m_midiCCModel[i] = std::make_unique<FloatModel>(0.0f, 0.0f, 127.0f, 1.0f,
			nullptr, tr("CC Controller %1").arg(i));

		connect(m_midiCCModel[i].get(), &FloatModel::dataChanged,
			this, [this, i]{ processCCEvent(i); }, Qt::DirectConnection);
	}

	setName( tr( "Default preset" ) );

	connect(&m_baseNoteModel, SIGNAL(dataChanged()), this, SLOT(updateBaseNote()), Qt::DirectConnection);
	connect(&m_pitchModel, SIGNAL(dataChanged()), this, SLOT(updatePitch()), Qt::DirectConnection);
	connect(&m_pitchRangeModel, SIGNAL(dataChanged()), this, SLOT(updatePitchRange()), Qt::DirectConnection);
	connect(&m_mixerChannelModel, SIGNAL(dataChanged()), this, SLOT(updateMixerChannel()), Qt::DirectConnection);

	// Cluster new-p16-89: play() collects the clips of a pass. Reserve that storage on the control
	// thread as the track's clip list grows, so the audio thread never grows it.
	connect(this, &Track::clipAdded, this, [this](Clip*) {
		if (getClips().size() > m_playClips.capacity()) { m_playClips.reserve(getClips().size()); }
	});

	autoAssignMidiDevice(true);
}



bool InstrumentTrack::keyRangeImport() const
{
	return m_microtuner.enabled() && m_microtuner.keyRangeImport();
}


/** \brief Check if there is a valid mapping for the given key and it is within defined of range.
 */
bool InstrumentTrack::isKeyMapped(int key) const
{
	if (key < firstKey() || key > lastKey()) {return false;}
	if (!m_microtuner.enabled()) {return true;}

	Song *song = Engine::getSong();
	if (!song) {return false;}

	return song->getKeymap(m_microtuner.currentKeymap())->getDegree(key) != -1;
}


/** \brief Return first mapped key, based on currently selected keymap or user selection.
 *	\return Number ranging from 0 to NumKeys -1
 */
int InstrumentTrack::firstKey() const
{
	if (keyRangeImport())
	{
		return Engine::getSong()->getKeymap(m_microtuner.currentKeymap())->getFirstKey();
	}
	else
	{
		return m_firstKeyModel.value();
	}
}


/** \brief Return last mapped key, based on currently selected keymap or user selection.
 *	\return Number ranging from 0 to NumKeys -1
 */
int InstrumentTrack::lastKey() const
{
	if (keyRangeImport())
	{
		return Engine::getSong()->getKeymap(m_microtuner.currentKeymap())->getLastKey();
	}
	else
	{
		return m_lastKeyModel.value();
	}
}


/** \brief Return base key number, based on currently selected keymap or user selection.
 *	\return Number ranging from 0 to NumKeys -1
 */
int InstrumentTrack::baseNote() const
{
	int mp = m_useMasterPitchModel.value() ? Engine::getSong()->masterPitch() : 0;

	if (keyRangeImport())
	{
		return Engine::getSong()->getKeymap(m_microtuner.currentKeymap())->getBaseKey() - mp;
	}
	else
	{
		return m_baseNoteModel.value() - mp;
	}
}


/** \brief Return frequency assigned to the base key, based on currently selected keymap.
 *	\return Frequency in Hz
 */
float InstrumentTrack::baseFreq() const
{
	if (m_microtuner.enabled())
	{
		return Engine::getSong()->getKeymap(m_microtuner.currentKeymap())->getBaseFreq();
	}
	else
	{
		return DefaultBaseFreq;
	}
}



InstrumentTrack::~InstrumentTrack()
{
	// De-assign midi device
	if (s_autoAssignedTrack == this)
	{
		if (m_hasAutoMidiDev)
		{
			autoAssignMidiDevice(false);
		}
		s_autoAssignedTrack = nullptr;
	}

	// kill all running notes and the iph
	silenceAllNotes( true );
	// Q050-013: and the launcher-cell renderer an audio cell on this column gave the track
	Engine::audioEngine()->requestChangeInModel();
	Engine::audioEngine()->removePlayHandlesOfTypes( this, PlayHandle::Type::SessionCellHandle );
	Engine::audioEngine()->doneChangeInModel();

	// now we're save deleting the instrument
	destroyInstrument();
}


/*! cluster 5186 (new-p12-159): the ONE place this track's instrument is destroyed.
 *
 *  The per-voice modulation target that points at the instrument is closed BEFORE the instrument's
 *  own teardown starts. The audio thread's block calls the instrument under a pin on that target,
 *  and the pin keeps the TARGET alive - not the instrument: a target that outlives the instrument
 *  is a call into a half-destroyed object. Closing the target here (the notification retires the
 *  slot and waits for the in-flight write to drain) means that from the first line of the
 *  instrument's destructor onwards the block skips the entry.
 *
 *  The notification cannot come from Instrument's own destructor: a base destructor runs AFTER the
 *  derived one, so by then the derived teardown has already begun. The owner knows when the
 *  teardown starts, so the owner notifies. */
void InstrumentTrack::destroyInstrument()
{
	if (m_instrument == nullptr) { return; }
	emit m_instrument->aboutToBeDestroyed();
	delete m_instrument;
	m_instrument = nullptr;
}




void InstrumentTrack::processAudioBuffer( SampleFrame* buf, const f_cnt_t frames, NotePlayHandle* n )
{
	// we must not play the sound if this InstrumentTrack is muted...
	if( isMuted() || ( Engine::getSong()->playMode() != Song::PlayMode::MidiClip &&
				n && n->isPatternTrackMuted() ) || ! m_instrument )
	{
		return;
	}

	// Test for silent input data if instrument provides a single stream only (i.e. driven by InstrumentPlayHandle)
	// We could do that in all other cases as well but the overhead for silence test is bigger than
	// what we potentially save. While playing a note, a NotePlayHandle-driven instrument will produce sound in
	// 99 of 100 cases so that test would be a waste of time.
	if (m_instrument->isSingleStreamed() && MixHelpers::isSilent(buf, frames))
	{
		// at least pass one silent buffer to allow
		if( m_silentBuffersProcessed )
		{
			// skip further processing
			return;
		}
		m_silentBuffersProcessed = true;
	}
	else
	{
		m_silentBuffersProcessed = false;
	}

	// get volume knob data
	static const float DefaultVolumeRatio = 1.0f / DefaultVolume;
	/*ValueBuffer * volBuf = m_volumeModel.valueBuffer();
	float v_scale = volBuf
		? 1.0f
		: getVolume() * DefaultVolumeRatio;*/

	// instruments using instrument-play-handles will call this method
	// without any knowledge about notes, so they pass NULL for n, which
	// is no problem for us since we just bypass the envelopes+LFOs
	if (!m_instrument->isSingleStreamed() && n != nullptr)
	{
		const f_cnt_t offset = n->noteOffset();
		m_soundShaping.processAudioBuffer( buf + offset, frames - offset, n );
		const float vol = ( (float) n->getVolume() * DefaultVolumeRatio );
		const panning_t pan = std::clamp(n->getPanning(), PanningLeft, PanningRight);
		StereoVolumeVector vv = panningToVolumeVector( pan, vol );
		for( f_cnt_t f = offset; f < frames; ++f )
		{
			for( int c = 0; c < 2; ++c )
			{
				buf[f][c] *= vv.vol[c];
			}
		}
	}
}




MidiEvent InstrumentTrack::applyMasterKey( const MidiEvent& event )
{
	MidiEvent copy( event );
	switch( event.type() )
	{
		case MidiNoteOn:
		case MidiNoteOff:
		case MidiKeyPressure:
			copy.setKey( masterKey( event.key() ) );
			break;
		default:
			break;
	}
	return copy;
}




void InstrumentTrack::processCCEvent(int controller)
{
	// Does nothing if the LED is disabled
	if (!m_midiCCEnable->value()) { return; }

	auto channel = static_cast<uint8_t>(midiPort()->realOutputChannel());
	auto cc = static_cast<uint16_t>(controller);
	auto value = static_cast<uint16_t>(m_midiCCModel[controller]->value());

	// Process the MIDI CC event as an input event but with source set to Internal
	// so we can know LMMS generated the event, not a controller, and can process it during
	// the project export
	processInEvent(MidiEvent(MidiControlChange, channel, cc, value, nullptr, MidiEvent::Source::Internal));
}




bool InstrumentTrack::trackMpeInputEvent( const MidiEvent& event )
{
	if( !MpeExpression::isEnabled() )
	{
		return false;
	}

	switch( event.type() )
	{
		case MidiNoteOn:
			// A note-on with velocity 0 is a note-off (MidiClientRaw parses it
			// that way and the switch below falls through for the same reason).
			if( event.velocity() > 0 )
			{
				m_mpeExpression.noteOn( event.channel(), event.key() );
			}
			else
			{
				m_mpeExpression.noteOff( event.channel(), event.key() );
			}
			return false;

		case MidiNoteOff:
			// 0.5.0 Q050-039: the note-off's velocity is release velocity. The
			// note model can store it (Note::setReleaseVelocity) and playback
			// already sends it back (NotePlayHandle::noteOff), but live MPE
			// input dropped it here.
			captureMpeReleaseVelocity( event );
			m_mpeExpression.noteOff( event.channel(), event.key() );
			return false;

		default:
			break;
	}

	if( !m_mpeExpression.handleExpressionEvent( event ) )
	{
		return false;
	}

	loadMpeExpressionOntoChannelNotes( event.channel() );
	return true;
}




void InstrumentTrack::captureMpeReleaseVelocity( const MidiEvent& event )
{
	if( !m_mpeExpression.isMemberChannel( event.channel() ) )
	{
		return;
	}
	if( event.key() < 0 || event.key() >= NumKeys )
	{
		return;
	}
	// Velocity 0 is what an MPE controller sends when it has no release
	// velocity to report, so the note keeps its "unset" release and playback
	// sends velocity 0 exactly as it did before this capture existed.
	const int velocity = event.velocity();
	if( velocity <= 0 )
	{
		return;
	}

	NotePlayHandle* note = m_notes[event.key()];
	if( note == nullptr || note->midiChannel() != event.channel() )
	{
		// Released, never started, or another channel's note holding this key
		// - the same window loadMpeExpressionOntoChannelNotes() lives with.
		return;
	}

	// The velocity is stored on the note's own volume scale (0..200, 100 =
	// default) through the conversion that is the exact inverse of the
	// playback emission (Note::midiReleaseVelocity), so the velocity the
	// note-off carried is the velocity playback sends (Q050-039).
	note->setReleaseVelocity(
		Note::releaseVelocityFromMidi( velocity, midiPort()->baseVelocity() ) );
}




void InstrumentTrack::loadMpeExpressionOntoChannelNotes( int channel )
{
	const MpeNoteExpression expression = m_mpeExpression.current( channel );
	const int count = m_mpeExpression.activeNoteCount( channel );

	for( int i = 0; i < count; ++i )
	{
		const int key = m_mpeExpression.activeNote( channel, i );
		if( key < 0 || key >= NumKeys )
		{
			continue;
		}

		NotePlayHandle* note = m_notes[key];
		if( note == nullptr || note->midiChannel() != channel )
		{
			// Released (or never started) between the event and this lookup -
			// the same window MidiKeyPressure below already lives with. A note
			// on another channel holding this key is not this channel's note.
			continue;
		}

		const MpeNoteExpression before = note->mpeExpression();
		note->setMpeExpression( expression );
		note->setFrequencyUpdate();

		// MPE (task #649): the pitch axis reaches playback through the
		// frequency above. Pressure and timbre have no frequency to ride on,
		// so they are sent to the instrument as MIDI on the note's own
		// channel - but only when they actually changed: a bend-only update
		// (every pitch gesture, and the common case) sends nothing extra.
		if( before.pressure != expression.pressure || before.timbre != expression.timbre )
		{
			note->sendMpeExpressionMidi();
		}
	}
}




void InstrumentTrack::processInEvent( const MidiEvent& event, const TimePos& time, f_cnt_t offset )
{
	if (Engine::getSong()->isExporting() && event.source() == MidiEvent::Source::External)
	{
		return;
	}
	// 0.5.0 L07 (ED-145): live input through the track's MIDI input transform.
	if (handleInputTransform(event, time, offset)) { return; }
	// R2.1: monitoring Off keeps live notes out of the instrument. Only the NOTE-ON is
	// dropped - a note-off still reaches a note that sounded before the mode changed.
	if (event.type() == MidiNoteOn && event.source() == MidiEvent::Source::External
		&& monitorMode() == MonitorMode::Off)
	{
		return;
	}

	// MPE per-note expression (task #601). Off unless MpeExpression::isEnabled();
	// when it is on, a bend / channel pressure / CC74 arriving on the note's own
	// member channel is the note's expression, so it is consumed here and never
	// reaches the channel-wide handlers below (which would bend every note of
	// the instrument at once).
	if (trackMpeInputEvent(event))
	{
		// MPE expression is consumed before the ordinary layer-forwarding
		// path, so pass it to the live layer notes before returning.
		m_layers.forwardInEvent(event, time, offset);
		return;
	}

	// 0.5.0 MX-112: a live note (and its note-off) reaches the layers whose zones claim it.
	m_layers.forwardInEvent(event, time, offset);

	bool eventHandled = false;

	switch( event.type() )
	{
		// we don't send MidiNoteOn, MidiNoteOff and MidiKeyPressure
		// events to instrument as NotePlayHandle will send them on its
		// own
		case MidiNoteOn:
			if( event.velocity() > 0 )
			{
				// play a note only if it is not already playing and if it is within configured bounds
				if (m_notes[event.key()] == nullptr && event.key() >= firstKey() && event.key() <= lastKey())
				{
					NotePlayHandle* nph =
						NotePlayHandleManager::acquire(
								this, offset,
								std::numeric_limits<f_cnt_t>::max() / 2,
								Note(TimePos(), Engine::getSong()->getPlayPos(Engine::getSong()->playMode()),
										event.key(), event.volume(midiPort()->baseVelocity())),
								nullptr, event.channel(),
								NotePlayHandle::Origin::MidiInput);
					m_notes[event.key()] = nph;
					// MPE (task #601): the note is born with whatever
					// expression its own channel already holds; later bend /
					// pressure / CC74 on that channel update it in place.
					// setFrequencyUpdate() because the handle computed its
					// frequency before we could stamp this on it.
					if( MpeExpression::isEnabled() && m_mpeExpression.isMemberChannel( event.channel() ) )
					{
						nph->setMpeExpression( m_mpeExpression.current( event.channel() ) );
						nph->setFrequencyUpdate();
					}
					if( ! Engine::audioEngine()->addPlayHandle( nph ) )
					{
						m_notes[event.key()] = nullptr;
					}
				}
				eventHandled = true;
				break;
			}

		case MidiNoteOff:
			if( m_notes[event.key()] != nullptr )
			{
				// do actual note off and remove internal reference to NotePlayHandle (which itself will
				// be deleted later automatically)
				Engine::audioEngine()->requestChangeInModel();
				m_notes[event.key()]->noteOff( offset );
				if (isSustainPedalPressed() &&
					m_notes[event.key()]->origin() ==
					NotePlayHandle::Origin::MidiInput)
				{
					m_sustainedNotes << m_notes[event.key()];
				}
				m_notes[event.key()] = nullptr;
				Engine::audioEngine()->doneChangeInModel();
			}
			eventHandled = true;
			break;

		case MidiKeyPressure:
			if( m_notes[event.key()] != nullptr )
			{
				// setVolume() calls processOutEvent() with MidiKeyPressure so the
				// attached instrument will receive the event as well
				m_notes[event.key()]->setVolume( event.volume( midiPort()->baseVelocity() ) );
			}
			eventHandled = true;
			break;

		case MidiPitchBend:
			// updatePitch() is connected to m_pitchModel::dataChanged() which will send out
			// MidiPitchBend events
			m_pitchModel.setValue( m_pitchModel.minValue() + event.pitchBend() * m_pitchModel.range() / MidiMaxPitchBend );
			break;

		case MidiControlChange:
			if( event.controllerNumber() == MidiControllerSustain )
			{
				if( event.controllerValue() > MidiMaxControllerValue/2 )
				{
					m_sustainPedalPressed = true;
				}
				else if (isSustainPedalPressed())
				{
					for (NotePlayHandle* nph : m_sustainedNotes)
					{
						if (nph && nph->isReleased())
						{
							if( nph->origin() ==
								NotePlayHandle::Origin::MidiInput)
							{
								nph->setLength(
									TimePos( static_cast<f_cnt_t>(
									nph->totalFramesPlayed() /
									Engine::framesPerTick() ) ) );
								midiNoteOff( *nph );
							}
						}
					}
					m_sustainedNotes.clear();
					m_sustainPedalPressed = false;
				}
			}
			if( event.controllerNumber() == MidiControllerAllSoundOff ||
				event.controllerNumber() == MidiControllerAllNotesOff ||
				event.controllerNumber() == MidiControllerOmniOn ||
				event.controllerNumber() == MidiControllerOmniOff ||
				event.controllerNumber() == MidiControllerMonoOn ||
				event.controllerNumber() == MidiControllerPolyOn )
			{
				silenceAllNotes();
			}
			break;

		case MidiMetaEvent:
			// handle special cases such as note panning
			switch( event.metaEvent() )
			{
				case MidiNotePanning:
					if( m_notes[event.key()] != nullptr )
					{
						eventHandled = true;
						m_notes[event.key()]->setPanning( event.panning() );
					}
					break;
				default:
					qWarning( "InstrumentTrack: unhandled MIDI meta event: %i", event.metaEvent() );
					break;
			}
			break;

		default:
			break;
	}

	// If the event wasn't handled, check if there's a loaded instrument and if so send the
	// event to it. If it returns false means the instrument didn't handle the event, so we trigger a warning.
	if (eventHandled == false && !(instrument() && instrument()->handleMidiEvent(event, time, offset)))
	{
		qWarning("InstrumentTrack: unhandled MIDI event %d", event.type());
	}

}




void InstrumentTrack::processOutEvent( const MidiEvent& event, const TimePos& time, f_cnt_t offset )
{
	processOutEventImpl(event, time, offset, nullptr);
}

void InstrumentTrack::processOutEventForNote(const MidiEvent& event, const TimePos& time, f_cnt_t offset,
	std::atomic_bool& baseInstrumentAccepted)
{
	processOutEventImpl(event, time, offset, &baseInstrumentAccepted);
}

void InstrumentTrack::processOutEventImpl(const MidiEvent& event, const TimePos& time, f_cnt_t offset,
	std::atomic_bool* baseInstrumentAccepted)
{
	// do nothing if we do not have an instrument instance (e.g. when loading settings)
	if( m_instrument == nullptr )
	{
		return;
	}

	const MidiEvent transposedEvent = applyMasterKey( event );
	const int key = transposedEvent.key();

	// If we have a selected output midi channel between 1-16, we will use that channel to handle the midi event.
	// But if our selected midi output channel is 0 ("--"), we will use the event channel instead.
	const auto handleEventOutputChannel = midiPort()->outputChannel() == 0
		? event.channel()
		: midiPort()->realOutputChannel();

	switch( event.type() )
	{
		// 0.5.0 (MX-112 layer zones, MX-114 MIDI parts, MX-120 voices): the note paths are in
		// InstrumentTrackNotes.cpp, moved out of this function unchanged but for those three.
		case MidiNoteOn:
			sendNoteOn( event, key, handleEventOutputChannel, time, offset, baseInstrumentAccepted );
			break;

		case MidiNoteOff:
			sendNoteOff( event, key, handleEventOutputChannel, time, offset, baseInstrumentAccepted );
			break;

		default:
			sendChannelEvent( transposedEvent, time, offset );
			break;
	}

	// if appropriate, midi-port does further routing
	m_midiPort.processOutEvent( event, time, offset );
}




void InstrumentTrack::silenceAllNotes( bool removeIPH )
{
	m_midiNotesMutex.lock();
	for( int i = 0; i < NumKeys; ++i )
	{
		m_notes[i] = nullptr;
		m_runningMidiNotes[i] = 0;
	}
	m_midiNotesMutex.unlock();

	Engine::audioEngine()->requestChangeInModel();
	// invalidate all NotePlayHandles and PresetPreviewHandles linked to this track
	m_processHandles.clear();

	auto flags = PlayHandle::Type::NotePlayHandle | PlayHandle::Type::PresetPreviewHandle;
	if( removeIPH )
	{
		flags |= PlayHandle::Type::InstrumentPlayHandle;
	}
	Engine::audioEngine()->removePlayHandlesOfTypes( this, flags );
	Engine::audioEngine()->doneChangeInModel();
}




f_cnt_t InstrumentTrack::beatLen( NotePlayHandle * _n ) const
{
	if( m_instrument != nullptr )
	{
		const f_cnt_t len = m_instrument->beatLen( _n );
		if( len > 0 )
		{
			return len;
		}
	}
	return m_soundShaping.envFrames();
}




void InstrumentTrack::playNote( NotePlayHandle* n, std::span<SampleFrame> workingBuffer )
{
	// Note: under certain circumstances the working buffer is a nullptr.
	// These cases are triggered in PlayHandle::doProcessing when the play method is called with a nullptr.
	// TODO: Find out if we can skip processing at a higher level if the buffer is nullptr.

	// arpeggio- and chord-widget has to do its work -> adding sub-notes
	// for chords/arpeggios
	m_noteStacking.processNote( n );
	m_arpeggio.processNote( n );

	// 0.5.0 MX-112: a layer rack's zones can leave the track's own instrument out of a note.
	if( n->isMasterNote() == false && m_instrument != nullptr && m_layers.basePlays(n->key(), n->getVolume()) )
	{
		// all is done, so now lets play the note!
		m_instrument->playNote( n, workingBuffer );

		// This is effectively the same as checking if workingBuffer is not a nullptr.
		// Calling processAudioBuffer with a nullptr leads to crashes. Hence the check.
		if (n->usesBuffer())
		{
			const f_cnt_t frames = n->framesLeftForCurrentPeriod();
			const f_cnt_t offset = n->noteOffset();
			processAudioBuffer(workingBuffer.data(), frames + offset, n);
		}
	}
}




QString InstrumentTrack::instrumentName() const
{
	if( m_instrument != nullptr )
	{
		return m_instrument->displayName();
	}
	return QString();
}




void InstrumentTrack::deleteNotePluginData( NotePlayHandle* n )
{
	if( m_instrument != nullptr )
	{
		m_instrument->deleteNotePluginData( n );
	}
}




void InstrumentTrack::setName(const QString& new_name)
{
	Track::setName(new_name);
	m_midiPort.setName(name());
	m_audioBusHandle.setName(name());
}






void InstrumentTrack::updateBaseNote()
{
	Engine::audioEngine()->requestChangeInModel();
	for (const auto& processHandle : m_processHandles)
	{
		processHandle->setFrequencyUpdate();
	}
	Engine::audioEngine()->doneChangeInModel();
}




void InstrumentTrack::updatePitch()
{
	updateBaseNote();

	processOutEvent( MidiEvent( MidiPitchBend, midiPort()->realOutputChannel(), midiPitch() ) );
}




void InstrumentTrack::updatePitchRange()
{
	const int r = m_pitchRangeModel.value();
	m_pitchModel.setRange( MinPitchDefault * r, MaxPitchDefault * r );

	processOutEvent( MidiEvent( MidiControlChange, midiPort()->realOutputChannel(),
								MidiControllerRegisteredParameterNumberLSB, MidiPitchBendSensitivityRPN & 0x7F ) );
	processOutEvent( MidiEvent( MidiControlChange, midiPort()->realOutputChannel(),
								MidiControllerRegisteredParameterNumberMSB, ( MidiPitchBendSensitivityRPN >> 8 ) & 0x7F ) );
	processOutEvent( MidiEvent( MidiControlChange, midiPort()->realOutputChannel(), MidiControllerDataEntry, midiPitchRange() ) );
}




void InstrumentTrack::updateMixerChannel()
{
	m_audioBusHandle.setNextMixerChannel(m_mixerChannelModel.value());
	m_layers.followMixerChannel(m_mixerChannelModel.value());  // 0.5.0 MX-112: the layers play where the track does
}




int InstrumentTrack::masterKey( int _midi_key ) const
{

	int key = baseNote();
	return std::clamp(_midi_key - (key - DefaultKey), 0, NumKeys - 1);
}




void InstrumentTrack::removeMidiPortNode( DataFile & _dataFile )
{
	QDomNodeList n = _dataFile.elementsByTagName( "midiport" );
	n.item( 0 ).parentNode().removeChild( n.item( 0 ) );
}




bool InstrumentTrack::compPlays(const MidiClip* clip, int songTick) const
{
	const TakeLaneModel& comp = takeLanes();
	if (comp.auditionLane() >= 0) { return clip->laneIndex() == comp.auditionLane(); }
	if (comp.segments().empty()) { return true; }
	int lane = -1;
	int offset = 0;
	return comp.resolve(songTick, &lane, &offset) && lane == clip->laneIndex();
}




bool InstrumentTrack::play( const TimePos & _start, const f_cnt_t _frames,
							const f_cnt_t _offset, int _clip_num )
{
	// 0.5.0 Q050-040 (L07): playing with chase on, a note already under the playhead starts when its
	// track or clip is unmuted. The render thread's own view of the mute, one pass back, and the
	// one-pass unmute edge: a start, a jump, a loop and an unmute all chase (noteStartsNow).
	const bool muted = isMuted();
	const bool chase = MidiPlayback::instance().chase();
	const bool trackUnmutedNow = chase && !muted && m_mutedLastPlay;

	// The frozen take (freeze / bounce-in-place): the render IS this track's
	// output, so a pass inside the take's window plays the take and never
	// reaches the instrument - which is what "the source is disabled" means in
	// the engine. A single-clip playback (_clip_num >= 0: the piano roll's and
	// the beat/bassline editor's own play mode) is NOT the arrangement and keeps
	// playing the source. The take's pass locks nothing of this track, so it still records the mute.
	if (isFrozen() && _clip_num < 0)
	{
		// A whole-track take owns every pass; a region take only its window, and a take whose audio is
		// missing none (frozenTakeSilencesSource): outside that the source plays on.
		const bool played = playFrozenTake(_start, _frames, _offset);
		if (frozenTakeSilencesSource(_start.getTicks()))
		{
			m_mutedLastPlay = muted;
			return played;
		}
	}
	if( ! m_instrument || ! tryLock() )
	{
		// No pass happened - there is no instrument, or another thread holds the processing
		// lock - so the edge is kept for the pass that does take the lock (Q050-040 r2).
		return false;
	}
	m_mutedLastPlay = muted;
	// With chase on a muted track schedules nothing: a silent handle left under the playhead would
	// block the unmute chase below (and double the note on a seek into it) instead of starting once.
	if (chase && muted)
	{
		unlock();
		return false;
	}
	const float frames_per_tick = Engine::framesPerTick();

	clipVector& clips = m_playClips;
	clips.clear();
	class PatternTrack * pattern_track = nullptr;
	if( _clip_num >= 0 )
	{
		// Render thread: a clip this track does not hold has no notes, so nothing is created here.
		// getClip() would construct a MidiClip (and its automatable models) on the audio thread.
		if (_clip_num < numOfClips())
		{
			clips.push_back( getClip( _clip_num ) );
			if (trackContainer() == Engine::patternStore())
			{
				pattern_track = PatternTrack::findPatternTrack(_clip_num);
			}
		}
	}
	else
	{
		getClipsInRange( clips, _start, _start + static_cast<int>(
					_frames / frames_per_tick ) );
	}

	// Handle automation: detuning
	for (const auto& processHandle : m_processHandles)
	{
		// The recording state comes from the published flag, not from the piano roll:
		// this loop runs on the audio thread, and the piano roll is a Qt object the
		// GUI thread owns (cluster new-p15-83).
		processHandle->processTimePos(
			_start, m_pitchModel.value(), isPianoRollRecording());
	}

	if ( clips.size() == 0 )
	{
		unlock();
		return false;
	}

	bool played_a_note = false;	// will be return variable

	for (const auto& clip : clips)
	{
		auto c = dynamic_cast<MidiClip*>(clip);
		if (c == nullptr) { continue; }
		// A MIDI clip playing in the Piano Roll window will always play. Everything else is
		// skipped while muted - and the render thread remembers that, so unmuting it during
		// playback chases the notes already under the playhead once (Q050-040).
		const bool clipMuted = Engine::getSong()->playMode() != Song::PlayMode::MidiClip && clip->isMuted();
		const bool controllerResumed = !clipMuted && clip->playbackWasMuted();
		const bool clipUnmutedNow = chase && !clipMuted && clip->playbackWasMuted();
		clip->setPlaybackWasMuted(clipMuted);
		if (clipMuted)
		{
			continue;
		}
		TimePos cur_start = _start;
		tick_t passBase = 0;  // where the pass of a looped clip playing now starts, in the clip's own time
		if( _clip_num < 0 )
		{
			const TimePos unfolded = cur_start - (c->startPosition() + c->startTimeOffset());
			cur_start = loopedPosition(c, unfolded);  // 0.5.0 ED-029: a looped clip
			passBase = unfolded - cur_start;
		}
		// A start, a jump or a loop (MidiPlayback), this track's or this clip's unmute (Q050-040).
		const bool chaseNow = (chase && MidiPlayback::instance().chasingNow()) || trackUnmutedNow || clipUnmutedNow;
		// 0.5.0 L07: the clip's CC / pitch-bend / aftertouch lanes (InstrumentTrackNotes.cpp).
		// R3.3: a controller lane follows the same composite as the notes - a take the comp (or an
		// audition) does not select must not reach the instrument. The clip's own time at this pass
		// start maps to the song tick playing now.
		if (!c->controllers().empty()
			&& (_clip_num >= 0 || compPlays(c, c->startPosition().getTicks()
				+ c->startTimeOffset().getTicks() + cur_start.getTicks() + passBase)))
		{
			if (controllerResumed)
			{
				for (const ControllerLane& lane : c->controllers().lanes()) { lane.lastSent = -1; }
			}
			playClipControllers(c, cur_start.getTicks(), _offset);
		}

		// get all notes from the given clip...
		const NoteVector & notes = c->notes();
		// ...and set our index to zero
		auto nit = notes.begin();

		// very effective algorithm for playing notes that are
		// posated within the current sample-frame

		if( cur_start > 0 )
		{
			// skip notes which end before start-bar
			while( nit != notes.end() && ( *nit )->endPos() < cur_start )
			{
				++nit;
			}
		}

		while (nit != notes.end() && (*nit)->pos() < c->length() - c->startTimeOffset())
		{
			const auto currentNote = *nit;
			// Skip any notes not at the current time pos, not under the clip's start and not chased.
			if (!noteStartsNow(*currentNote, cur_start, c, chaseNow))
			{
				++nit;
				continue;
			}
			// R3.3, MIDI comping: on a comped track a note plays only where the
			// composite selects its clip's lane at the note's song position (while a
			// lane is auditioned, only that lane's notes play). The choice is made per
			// NOTE: a note that starts inside a selected span plays whole. A track
			// with no composite and no audition skips this entirely.
			if (_clip_num < 0 && !compPlays(c, c->startPosition().getTicks()
				+ c->startTimeOffset().getTicks() + currentNote->pos().getTicks() + passBase))
			{
				++nit;
				continue;
			}

			// Calculate the overlap of the note over the clip end (on a looped clip, over the end of this pass).
			const auto noteOverlap = std::max(0, currentNote->endPos() - passEnd(c, passBase, _clip_num < 0));
			// If the note is a Step Note, frames will be 0 so the NotePlayHandle
			// plays for the whole length of the sample
			const auto noteFrames = currentNote->type() == Note::Type::Step
				? 0
				: (currentNote->endPos() - cur_start - noteOverlap) * frames_per_tick;

			// Muted, recurrence and the stateless chance roll (InstrumentTrackNotes.cpp).
			const uint32_t midiSeed = Engine::getSong()->midiSeed();
			if (!notePlaysNow(*currentNote, midiSeed))
			{
				++nit;
				continue;
			}
			// The handle with its decorations, repeats, articulation and delay (InstrumentTrackNotes.cpp).
			triggerNote({c, nit, notes.begin(), _clip_num < 0, midiSeed, pattern_track, _start.getTicks()}, _offset, noteFrames, cur_start);
			played_a_note = true;
			++nit;
		}
	}
	unlock();
	return played_a_note;
}




Clip* InstrumentTrack::createClip(const TimePos & pos)
{
	auto p = new MidiClip(this);
	p->movePosition(pos);
	return p;
}




gui::TrackView* InstrumentTrack::createView( gui::TrackContainerView* tcv )
{
	return new gui::InstrumentTrackView( this, tcv );
}




void InstrumentTrack::saveTrackSpecificSettings(QDomDocument& doc, QDomElement& thisElement, bool presetMode)
{
	m_volumeModel.saveSettings( doc, thisElement, "vol" );
	m_panningModel.saveSettings( doc, thisElement, "pan" );
	m_pitchModel.saveSettings( doc, thisElement, "pitch" );
	m_pitchRangeModel.saveSettings( doc, thisElement, "pitchrange" );
	if (!presetMode) { m_inputTransform.save(thisElement); }  // 0.5.0 L07: an attribute, only when set
	m_articulations.save(thisElement);  // 0.5.0 L07 (ED-121): an attribute, only when there is one
	if (delayMs() > 0) { thisElement.setAttribute("delayms", delayMs()); }  // 0.5.0 L09 (MX-035)
	if (m_audioBusHandle.offsetFrames() != 0) { thisElement.setAttribute("offsetframes", m_audioBusHandle.offsetFrames()); }  // Q050-339

	m_mixerChannelModel.saveSettings( doc, thisElement, "mixch" );
	m_baseNoteModel.saveSettings( doc, thisElement, "basenote" );
	m_firstKeyModel.saveSettings(doc, thisElement, "firstkey");
	m_lastKeyModel.saveSettings(doc, thisElement, "lastkey");
	m_useMasterPitchModel.saveSettings( doc, thisElement, "usemasterpitch");
	m_microtuner.saveSettings(doc, thisElement);

	// Save MIDI CC stuff
	m_midiCCEnable->saveSettings(doc, thisElement, "enablecc");
	QDomElement midiCC = doc.createElement("midicontrollers");
	thisElement.appendChild(midiCC);
	for (int i = 0; i < MidiControllerCount; ++i)
	{
		m_midiCCModel[i]->saveSettings(doc, midiCC, "cc" + QString::number(i));
	}

	if (m_instrument != nullptr && !m_preservedInstrument.isNull() && !m_instrument->unavailableReason().isEmpty())
	{
		// 0.5.0 Q050-018: a placeholder writes back what the file had, so nothing of the device is lost.
		thisElement.appendChild(doc.importNode(m_preservedInstrument, true));
	}
	else if( m_instrument != nullptr )
	{
		QDomElement i = doc.createElement( "instrument" );
		i.setAttribute( "name", m_instrument->descriptor()->name );
		QDomElement ins = m_instrument->saveState( doc, i );
		if(m_instrument->key().isValid()) {
			ins.appendChild( m_instrument->key().saveXML( doc ) );
			// 0.5.0 Q050-018: the maker is not in the key (it is the device's identity), so a project that is
			// later opened without the plug-in can still say whose it was.
			const auto& attrs = m_instrument->key().attributes;
			QString vendor = pluginvendors::vendorOf(attrs.value("file"), attrs.value("class"));
			if (vendor.isEmpty()) { vendor = pluginvendors::vendorOf(attrs.value("file"), attrs.value("id")); }
			if (!vendor.isEmpty()) { i.setAttribute("vendor", vendor); }
		}
		thisElement.appendChild( i );
	}
	m_soundShaping.saveState( doc, thisElement );
	m_noteStacking.saveState( doc, thisElement );
	m_arpeggio.saveState( doc, thisElement );
	saveZeneInstrumentSettings(doc, thisElement, presetMode);  // 0.5.0 MX-112/MX-114: layers, parts, buses

	// Save the midi port info if we are not in song saving mode, e.g. in
	// track cloning mode or if we are in song saving mode and the user
	// has chosen not to discard the MIDI connections.
	if (!Engine::getSong()->isSavingProject() ||
	    !Engine::getSong()->getSaveOptions().discardMIDIConnections.value())
	{
		// Don't save auto assigned midi device connection
		bool hasAuto = m_hasAutoMidiDev;
		autoAssignMidiDevice(false);

		// Only save the MIDI port information if we are not saving a preset.
		if (!presetMode)
		{
			m_midiPort.saveState(doc, thisElement);
		}

		autoAssignMidiDevice(hasAuto);
	}

	m_audioBusHandle.effects()->saveState(doc, thisElement);
}




void InstrumentTrack::loadTrackSpecificSettings( const QDomElement & thisElement )
{
	loadTrackSpecificSettings(thisElement, false);
}



void InstrumentTrack::loadTrackSpecificSettings(const QDomElement& thisElement, bool presetMode)
{
	// don't delete instrument in preview mode if it's the same
	// we can't do this for other situations due to some issues with linked models
	bool reuseInstrument = m_previewMode && m_instrument && m_instrument->nodeName() == getSavedInstrumentName(thisElement);
	// remove the InstrumentPlayHandle if and only if we need to delete the instrument
	silenceAllNotes(!reuseInstrument);

	lock();

	m_volumeModel.loadSettings( thisElement, "vol" );
	m_panningModel.loadSettings( thisElement, "pan" );
	m_pitchRangeModel.loadSettings( thisElement, "pitchrange" );
	m_pitchModel.loadSettings( thisElement, "pitch" );
	m_mixerChannelModel.setRange( 0, Engine::mixer()->numChannels()-1 );
	if ( !m_previewMode )
	{
		m_mixerChannelModel.loadSettings( thisElement, "mixch" );
	}
	m_baseNoteModel.loadSettings( thisElement, "basenote" );
	m_firstKeyModel.loadSettings(thisElement, "firstkey");
	m_lastKeyModel.loadSettings(thisElement, "lastkey");
	m_useMasterPitchModel.loadSettings( thisElement, "usemasterpitch");
	m_microtuner.loadSettings(thisElement);

	// clear effect-chain just in case we load an old preset without FX-data
	m_audioBusHandle.effects()->clear();

	// We set MIDI CC enable to false so the knobs don't trigger MIDI CC events while
	// they are being loaded. After all knobs are loaded we load the right value of m_midiCCEnable.
	m_midiCCEnable->setValue(false);

	m_inputTransform.load(thisElement);  // 0.5.0 L07 (absent: left as it is)
	m_articulations.load(thisElement);  // 0.5.0 L07 (ED-121; absent: none)
	setDelayMs(thisElement.attribute("delayms", "0").toInt());  // 0.5.0 L09 (MX-035; absent: none)
	m_audioBusHandle.setOffsetFrames(thisElement.attribute("offsetframes", "0").toInt());  // Q050-339 (absent: none)

	QDomNode node = thisElement.firstChild();
	while( !node.isNull() )
	{
		if( node.isElement() )
		{
			if( m_soundShaping.nodeName() == node.nodeName() )
			{
				m_soundShaping.restoreState( node.toElement() );
			}
			else if( m_noteStacking.nodeName() == node.nodeName() )
			{
				m_noteStacking.restoreState( node.toElement() );
			}
			else if( m_arpeggio.nodeName() == node.nodeName() )
			{
				m_arpeggio.restoreState( node.toElement() );
			}
			else if( m_midiPort.nodeName() == node.nodeName() )
			{
				m_midiPort.restoreState( node.toElement() );
			}
			else if (m_audioBusHandle.effects()->nodeName() == node.nodeName())
			{
				m_audioBusHandle.effects()->restoreState(node.toElement());
			}
			else if(node.nodeName() == "instrument")
			{
				using PluginKey = Plugin::Descriptor::SubPluginFeatures::Key;
				PluginKey key(node.toElement().elementsByTagName("key").item(0).toElement());

				if (reuseInstrument)
				{
					m_instrument->restoreState(node.firstChildElement());
				}
				else
				{
					destroyInstrument();
					m_instrument = Instrument::instantiate(
						node.toElement().attribute("name"), this, &key);
					m_instrument->restoreState(node.firstChildElement());
					capturePlaceholder(node.toElement());
					emit instrumentChanged();
				}
			}
			else if (isZeneInstrumentNode(node.nodeName()))
			{
				// 0.5.0 MX-112/MX-114: read after the instrument (loadZeneInstrumentSettings), never as a plug-in name
			}
			else if (node.nodeName() == "midicontrollers")
			{
				loadMidiControllers(node.toElement());
			}
			// compat code - if node-name doesn't match any known
			// one, we assume that it is an instrument-plugin
			// which we'll try to load
			else if(AutomationClip::classNodeName() != node.nodeName() &&
					ControllerConnection::classNodeName() != node.nodeName() &&
					!node.toElement().hasAttribute( "id" ))
			{
				destroyInstrument();
				m_instrument = Instrument::instantiate(
					node.nodeName(), this, nullptr, true);
				if (m_instrument->nodeName() == node.nodeName())
				{
					m_instrument->restoreState(node.toElement());
				}
				capturePlaceholder(node.toElement());
				emit instrumentChanged();
			}
		}
		node = node.nextSibling();
	}

	// Load the right value of m_midiCCEnable
	m_midiCCEnable->loadSettings(thisElement, "enablecc");

	updatePitchRange();
	unlock();

	loadZeneInstrumentSettings(thisElement, presetMode);
}




void InstrumentTrack::capturePlaceholder(const QDomElement& instrumentNode)
{
	// 0.5.0 Q050-018: an instrument the file named but this session could not load keeps its original XML.
	m_preservedInstrument = QDomElement();
	if (m_instrument != nullptr && !m_instrument->unavailableReason().isEmpty())
	{
		m_preservedInstrument = missingdevices::detachedCopy(instrumentNode);
	}
}




void InstrumentTrack::setPreviewMode( const bool value )
{
	m_previewMode = value;
}




void InstrumentTrack::replaceInstrument(DataFile dataFile)
{
	// loadSettings clears the mixer channel, so we save it here and set it back later
	int mixerChannel = mixerChannelModel()->value();

	InstrumentTrack::removeMidiPortNode(dataFile);
	
	//Replacing an instrument shouldn't change the solo/mute state.
	bool oldMute = isMuted();
	bool oldSolo = isSolo();
	bool oldMutedBeforeSolo = isMutedBeforeSolo();

	loadPreset(dataFile.content().toElement());
	
	setMuted(oldMute);
	setSolo(oldSolo);
	setMutedBeforeSolo(oldMutedBeforeSolo);
	
	m_mixerChannelModel.setValue(mixerChannel);
	Engine::getSong()->setModified();
}




QString InstrumentTrack::getSavedInstrumentName(const QDomElement &thisElement) const
{
	QDomElement elem = thisElement.firstChildElement("instrument");
	if (!elem.isNull())
	{
		return elem.attribute("name");
	}
	return "";
}




Instrument * InstrumentTrack::loadInstrument(const QString & _plugin_name,
	const Plugin::Descriptor::SubPluginFeatures::Key *key, bool keyFromDnd)
{
	if(keyFromDnd)
		Q_ASSERT(!key);

	silenceAllNotes( true );

	lock();
	// 0.5.0 MX-120: the old instrument goes while the render is held off - a per-voice modulator's block
	// may be handing it a per-note value (ModulationLayerPublisher.cpp).
	Engine::audioEngine()->requestChangeInModel();
	destroyInstrument();
	m_preservedInstrument = QDomElement();  // 0.5.0 Q050-018: the user chose a new instrument; no placeholder remains
	Engine::audioEngine()->doneChangeInModel();
	m_instrument = Instrument::instantiate(_plugin_name, this,
					key, keyFromDnd);
	unlock();
	setName(m_instrument->displayName());

	emit instrumentChanged();

	return m_instrument;
}



InstrumentTrack *InstrumentTrack::s_autoAssignedTrack = nullptr;

/*! \brief Automatically assign a midi controller to this track, based on the midiautoassign setting
 *
 *  \param assign set to true to connect the midi device, set to false to disconnect
 */
void InstrumentTrack::autoAssignMidiDevice(bool assign)
{
	if (assign)
	{
		if (s_autoAssignedTrack)
		{
			s_autoAssignedTrack->autoAssignMidiDevice(false);
		}
		s_autoAssignedTrack = this;
	}

	const QString &device = ConfigManager::inst()->value("midi", "midiautoassign");
	if ( Engine::audioEngine()->midiClient()->isRaw() && device != "none" )
	{
		m_midiPort.setReadable( assign );
		m_hasAutoMidiDev = assign;
		return;
	}

	// Check if the device exists
	if ( Engine::audioEngine()->midiClient()->readablePorts().indexOf(device) >= 0 )
	{
		m_midiPort.subscribeReadablePort(device, assign);
		m_hasAutoMidiDev = assign;
	}
}


} // namespace lmms
