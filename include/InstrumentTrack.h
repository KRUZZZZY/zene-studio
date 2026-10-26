/*
 * InstrumentTrack.h - declaration of class InstrumentTrack, a track which
 *                     holds an instrument-plugin
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

#ifndef LMMS_INSTRUMENT_TRACK_H
#define LMMS_INSTRUMENT_TRACK_H

#include <atomic>
#include <cstdint>

#include <memory>
#include "ArticulationMap.h"
#include "AudioBusHandle.h"
#include "InstrumentFunctions.h"
#include "InstrumentLayers.h"
#include "InstrumentMidiParts.h"
#include "InstrumentSoundShaping.h"
#include "Microtuner.h"
#include "Midi.h"
#include "MidiEventProcessor.h"
#include "MidiInputTransform.h"
#include "MidiPort.h"
#include "MpeExpression.h"
#include "NotePlayHandle.h"
#include "Piano.h"
#include "Plugin.h"
#include "Track.h"

//! cluster 5186: the modulation-lifetime regression installs a watchable instrument in a track.
class ModulationAudioViewTest;

namespace lmms
{


class Instrument;
class DataFile;
class MidiClip;

namespace gui
{

class InstrumentTrackView;
class InstrumentTrackWindow;
class InstrumentTuningView;
class MidiCCRackView;

} // namespace gui


class LMMS_EXPORT InstrumentTrack : public Track, public MidiEventProcessor
{
	Q_OBJECT
	mapPropertyFromModel(int,getVolume,setVolume,m_volumeModel);
public:
	InstrumentTrack( TrackContainer* tc );
	~InstrumentTrack() override;

	// used by instrument
	void processAudioBuffer( SampleFrame* _buf, const f_cnt_t _frames,
							NotePlayHandle * _n );

	MidiEvent applyMasterKey( const MidiEvent& event );

	void processInEvent( const MidiEvent& event, const TimePos& time = TimePos(), f_cnt_t offset = 0 ) override;
	void processOutEvent( const MidiEvent& event, const TimePos& time = TimePos(), f_cnt_t offset = 0 ) override;
	//! NotePlayHandle path: preserve this handle's base-instrument decision through its own note-off.
	void processOutEventForNote( const MidiEvent& event, const TimePos& time, f_cnt_t offset,
		std::atomic_bool& baseInstrumentAccepted );

	/*! MPE per-note expression capture (task #601).
	 *
	 *  The per-channel expression state of this track's MIDI input. Only fed
	 *  while MpeExpression::isEnabled(); with MPE off it stays untouched and
	 *  the input path behaves exactly as before. */
	MpeExpression& mpeExpression() { return m_mpeExpression; }
	const MpeExpression& mpeExpression() const { return m_mpeExpression; }
	//! 0.5.0 L07 (ED-145): what this track does to live MIDI input (MidiInputTransform.h).
	MidiInputTransform& inputTransform() { return m_inputTransform; }
	//! 0.5.0 L09 (MX-035): every note of the track started this many milliseconds later (0..500) - a
	//! manual offset for a slow instrument or to sit a part behind the beat. Saved only when set.
	int delayMs() const { return m_delayMs.load(std::memory_order_relaxed); }
	void setDelayMs(int ms) { m_delayMs.store(std::clamp(ms, 0, 500), std::memory_order_relaxed); }
	//! 0.5.0 L07 (ED-121): the track's articulations (ArticulationMap.h); write with the track locked.
	ArticulationMap& articulations() { return m_articulations; }
	const ArticulationMap& articulations() const { return m_articulations; }

	//! 0.5.0 MX-114: the track's MIDI parts - zones sending notes to several channels of a multitimbral plug-in.
	InstrumentMidiParts& midiParts() { return m_midiParts; }
	const InstrumentMidiParts& midiParts() const { return m_midiParts; }
	//! 0.5.0 MX-112: the track's instrument layer rack (InstrumentLayers.h).
	InstrumentLayers& layers() { return m_layers; }
	const InstrumentLayers& layers() const { return m_layers; }

	/*! The live note handle playing \a key, or nullptr. Readback for the
	 *  captured expression (and what a piano-roll expression editor would
	 *  read while the note sounds). */
	NotePlayHandle* playingNote( int key ) const
	{
		return ( key >= 0 && key < NumKeys ) ? m_notes[key] : nullptr;
	}

	/*! Mark every NotePlayHandle this track is currently driving for a
	 *  frequency recomputation (board card #712). The session-wide tuning
	 *  table calls this from the CONTROL thread after a table flip; each
	 *  handle picks the flag up on the AUDIO thread's next period
	 *  (NotePlayHandle::play -> updateFrequency), so the write is a plain
	 *  bool store on both sides - no allocation, no lock, and no frequency is
	 *  computed off the control thread. Both lists are walked: the keyed MIDI
	 *  notes (m_notes) and every handle the engine is playing this period
	 *  (m_processHandles, which is where clip-driven notes live). */
	void retunePlayingNotes()
	{
		for (int key = 0; key < NumKeys; ++key)
		{
			if (m_notes[key] != nullptr) { m_notes[key]->setFrequencyUpdate(); }
		}
		for (NotePlayHandle* handle : m_processHandles)
		{
			handle->setFrequencyUpdate();
		}
	}

	// silence all running notes played by this track
	void silenceAllNotes( bool removeIPH = false );

	bool isSustainPedalPressed() const
	{
		return m_sustainPedalPressed;
	}

	f_cnt_t beatLen( NotePlayHandle * _n ) const;


	// for capturing note-play-events -> need that for arpeggio,
	// filter and so on
	void playNote( NotePlayHandle * _n, std::span<SampleFrame> workingBuffer );

	QString instrumentName() const;
	//! 0.5.0 Q050-018: the original `<instrument>` XML the file carried, kept while the instrument is a placeholder
	//! (its module or hosted plug-in binary is missing) and written back unchanged on save; null otherwise.
	const QDomElement& preservedInstrument() const { return m_preservedInstrument; }
	const Instrument *instrument() const
	{
		return m_instrument;
	}

	Instrument *instrument()
	{
		return m_instrument;
	}

	void deleteNotePluginData( NotePlayHandle * _n );

	// name-stuff
	void setName( const QString & _new_name ) override;

	// translate given key of a note-event to absolute key (i.e.
	// add global master-pitch and base-note of this instrument track)
	int masterKey( int _midi_key ) const;

	// translate pitch to midi-pitch [0,16383]
	int midiPitch() const
	{
		return static_cast<int>( ( ( m_pitchModel.value() + m_pitchModel.range()/2 ) * MidiMaxPitchBend ) / m_pitchModel.range() );
	}

	/*! \brief Returns current range for pitch bend in semitones */
	int midiPitchRange() const
	{
		return m_pitchRangeModel.value();
	}

	// play everything in given frame-range - creates note-play-handles
	bool play( const TimePos & _start, const f_cnt_t _frames,
						const f_cnt_t _frame_base, int _clip_num = -1 ) override;
	//! R3.3: whether a note of @a clip at @a songTick plays under the track's
	//! composite (and audition). True for every note of a track with neither.
	bool compPlays(const MidiClip* clip, int songTick) const;
	//! What play() applies to a fresh note play handle beyond the note itself: velocity jitter,
	//! the slide source, the song-global offset (@a songOffset: the song position of the clip's own
	//! time 0 for the pass playing) and the clip's transpose (InstrumentTrackNotes.cpp).
	void decorateNoteHandle(NotePlayHandle* handle, const MidiClip* clip, NoteVector::const_iterator note,
		NoteVector::const_iterator first, bool songGlobal, const TimePos& songOffset, uint32_t midiSeed) const;
	//! 0.5.0 L07 (ED-130): the clip's controller lanes at @a tick (clip time), each sent to the
	//! instrument when its value changed or the playback jumped (InstrumentTrackNotes.cpp).
	void playClipControllers(const MidiClip* clip, tick_t tick, f_cnt_t offset);
	//! One note play() starts: its clip and place in it, and how it is played.
	struct NoteTrigger
	{
		const MidiClip* clip;
		NoteVector::const_iterator note;
		NoteVector::const_iterator first;
		bool songGlobal;
		uint32_t midiSeed;
		class PatternTrack* patternTrack;
		tick_t songTick;  //!< the song position playing now: a looped clip's pass included (swing's phase)
	};
	//! Whether a note starts at @a curStart: on it, under the clip's own start, or chased
	//! (InstrumentTrackNotes.cpp). @a chaseNow is a start, a jump, a loop or an unmute this
	//! pass (Q050-040); a note an already-sounding handle covers is not started again.
	bool noteStartsNow(const Note& note, const TimePos& curStart, const MidiClip* clip, bool chaseNow) const;
	//! Whether a live (not released) note play handle of this track already plays @a note:
	//! the chase must not double a note that a seek or loop left sounding (Q050-040). The handle's
	//! originating clip must be @a clip itself: a same-key note in another overlapping clip is a
	//! different note (Q050-040 r2).
	bool noteIsAlreadySounding(const Note& note, const MidiClip* clip) const;
	//! Whether the note plays this time: not muted, on this pass, past its chance roll (InstrumentTrackNotes.cpp).
	bool notePlaysNow(const Note& note, uint32_t midiSeed) const;
	//! Start the note: its articulation, its handle and its repeats, delayed by swing and the MIDI offset.
	void triggerNote(const NoteTrigger& trigger, f_cnt_t offset, f_cnt_t frames, const TimePos& curStart);
	//! Audio thread (Q050-015): an articulation's program change, control change and keyswitch press, at @a offset
	//! in the period and @a time, for the note on @a key at @a volume; true when a keyswitch is down and
	//! releaseKeyswitch() is owed. Nothing is sent when a layer rack leaves this track's instrument out of the note.
	bool sendArticulationEvents(const ArticulationEvents& events, int channel, int key, int volume, const TimePos& time, f_cnt_t offset);
	void releaseKeyswitch(int key, int channel, const TimePos& time, f_cnt_t offset);
	static constexpr int KeyswitchVelocity = 100;
	//! 0.5.0 ED-029: @a position (clip time) folded into the clip's loop, when it has one.
	static TimePos loopedPosition(const MidiClip* clip, const TimePos& position);
	//! Where a note starting in the pass at @a passBase (clip time) is cut: the clip's end, and on a
	//! @a looped clip (song playback) the end of that pass's loop (InstrumentTrackNotes.cpp).
	static tick_t passEnd(const MidiClip* clip, tick_t passBase, bool looped);
	// create new view for me
	gui::TrackView* createView( gui::TrackContainerView* tcv ) override;

	// create new track-content-object = clip
	Clip* createClip(const TimePos & pos) override;


	// called by track
	void saveTrackSpecificSettings(QDomDocument& doc, QDomElement& parent, bool presetMode) override;
	void loadTrackSpecificSettings( const QDomElement & _this ) override;
	void loadTrackSpecificSettings(const QDomElement& _this, bool presetMode) override;

	using Track::setJournalling;


	// load instrument whose name matches given one
	Instrument * loadInstrument(const QString & _instrument_name,
				const Plugin::Descriptor::SubPluginFeatures::Key* key = nullptr,
				bool keyFromDnd = false);

	AudioBusHandle* audioBusHandle()
	{
		return &m_audioBusHandle;
	}

	MidiPort * midiPort()
	{
		return &m_midiPort;
	}

	const IntModel *baseNoteModel() const
	{
		return &m_baseNoteModel;
	}

	IntModel *baseNoteModel()
	{
		return &m_baseNoteModel;
	}

	IntModel *firstKeyModel()
	{
		return &m_firstKeyModel;
	}

	IntModel *lastKeyModel()
	{
		return &m_lastKeyModel;
	}

	bool keyRangeImport() const;
	bool isKeyMapped(int key) const;
	int firstKey() const;
	int lastKey() const;
	int baseNote() const;
	float baseFreq() const;

	Piano *pianoModel()
	{
		return &m_piano;
	}

	Microtuner *microtuner()
	{
		return &m_microtuner;
	}

	bool isArpeggioEnabled() const
	{
		return m_arpeggio.m_arpEnabledModel.value();
	}

	// simple helper for removing midiport-XML-node when loading presets
	static void removeMidiPortNode( DataFile& dataFile );

	FloatModel * pitchModel()
	{
		return &m_pitchModel;
	}

	FloatModel * volumeModel()
	{
		return &m_volumeModel;
	}

	FloatModel * panningModel()
	{
		return &m_panningModel;
	}

	IntModel* pitchRangeModel()
	{
		return &m_pitchRangeModel;
	}

	IntModel * mixerChannelModel() override
	{
		return &m_mixerChannelModel;
	}

	BoolModel* useMasterPitchModel()
	{
		return &m_useMasterPitchModel;
	}

	void setPreviewMode( const bool );

	bool isPreviewMode() const
	{
		return m_previewMode;
	}
	
	void replaceInstrument(DataFile dataFile);

	void autoAssignMidiDevice( bool );
	//! R5.4: the track the auto-assigned MIDI input follows - the one last created or last focused
	//! in the piano roll or piano view, i.e. the track being monitored; nullptr when there is none.
	static InstrumentTrack* autoAssignedTrack() { return s_autoAssignedTrack; }

	//! Returns a non-owning pointer to the model for the knob at the given index in the track's MIDI CC rack
	FloatModel* midiCCModel(int index) const { return m_midiCCModel[index].get(); }

	/*! Whether the piano roll is recording right now.
	 *
	 *  PUBLISHED BY THE GUI, READ BY THE RENDER THREAD. The piano roll's own flag
	 *  (PianoRoll::m_recording) is a plain bool the GUI thread writes, so the render
	 *  path must not read it: InstrumentTrack::play() runs on the audio thread, and
	 *  asking a QWidget from there is both a data race and a Qt call on the wrong
	 *  thread (cluster new-p15-83). The state is published here instead, as one
	 *  atomic word with relaxed ordering - a flag, with no order to keep. */
	static void setPianoRollRecording(bool recording) noexcept
	{
		s_pianoRollRecording.store(recording, std::memory_order_relaxed);
	}
	//! The published piano-roll recording state (see setPianoRollRecording).
	static bool isPianoRollRecording() noexcept
	{
		return s_pianoRollRecording.load(std::memory_order_relaxed);
	}

signals:
	void instrumentChanged();
	void midiNoteOn( const lmms::Note& );
	void midiNoteOff( const lmms::Note& );
	void newNote();
	void endNote();

protected:
	QString nodeName() const override
	{
		return "instrumenttrack";
	}

	// get the name of the instrument in the saved data
	QString getSavedInstrumentName(const QDomElement & thisElement) const;
	void capturePlaceholder(const QDomElement& instrumentNode);


protected slots:
	void updateBaseNote();
	void updatePitch();
	void updatePitchRange();
	void updateMixerChannel();


private:
	struct NoteActivitySnapshot
	{
		int activeNotes;
		std::uint32_t noteOnGeneration;
	};

	//! GUI-polled activity state; the note event paths update it without emitting Qt signals.
	void recordNoteOnActivity();
	void recordNoteOffActivity();
	NoteActivitySnapshot noteActivitySnapshot() const;

	/*! cluster 5186 (new-p12-159): destroys this track's instrument - the ONE place an instrument
	 *  is destroyed, whatever the reason (the track goes, the file names another instrument, the
	 *  user picks another one). The modulation target that points at the instrument is closed
	 *  FIRST, so the audio thread's block can no longer call into an instrument that is tearing
	 *  down: the base destructor is too late for that, because it runs after the derived one. */
	void destroyInstrument();

	void processCCEvent(int controller);
	void processOutEventImpl(const MidiEvent& event, const TimePos& time, f_cnt_t offset,
		std::atomic_bool* baseInstrumentAccepted);

	// MPE capture (task #601). Both run on the MIDI-input thread and only
	// touch fixed arrays plus plain int writes on live note handles.
	bool trackMpeInputEvent( const MidiEvent& event );
	//! 0.5.0 Q050-039: stores the release velocity an MPE note-off arrived with
	//! on the released note's handle, so playback (and a record pass) keeps it.
	void captureMpeReleaseVelocity( const MidiEvent& event );
	//! Live input through the track's MIDI input transform (InstrumentTrackNotes.cpp): true when
	//! the event was handled here - transformed and played, or dropped.
	bool handleInputTransform( const MidiEvent& event, const TimePos& time, f_cnt_t offset );
	void loadMpeExpressionOntoChannelNotes( int channel );

	// 0.5.0 MX-112/MX-114/MX-120 (InstrumentTrackNotes.cpp)
	//! processOutEvent's note-on, note-off and other messages to the instrument (@a fallbackChannel is the
	//! channel the track would use without MIDI parts).
	void sendNoteOn( const MidiEvent& event, int key, int fallbackChannel, const TimePos& time, f_cnt_t offset,
		std::atomic_bool* baseInstrumentAccepted );
	void sendNoteOff( const MidiEvent& event, int key, int fallbackChannel, const TimePos& time, f_cnt_t offset,
		std::atomic_bool* baseInstrumentAccepted );
	void sendChannelEvent( const MidiEvent& event, const TimePos& time, f_cnt_t offset );
	//! The <midicontrollers> element: the MIDI CC rack's knob values.
	void loadMidiControllers( const QDomElement& element );
	//! A channel-wide or per-key message sent to the MIDI parts' channels; false when there are no parts.
	bool sendToParts( const MidiEvent& event, const TimePos& time, f_cnt_t offset );
	//! A note-on/off sent to the instrument, told to the per-voice modulators that follow this track.
	void noteVoiceChanged( bool on, int channel, int key, f_cnt_t offset );
	//! The layers, MIDI parts and output-bus routes, saved beside the instrument.
	void saveZeneInstrumentSettings( QDomDocument& doc, QDomElement& element, bool presetMode );
	void loadZeneInstrumentSettings(const QDomElement& element, bool presetMode);
	//! Whether @a name is one of those elements (the loader must not take it for an instrument plug-in).
	static bool isZeneInstrumentNode( const QString& name );

	MidiPort m_midiPort;

	MpeExpression m_mpeExpression;
	MidiInputTransform m_inputTransform;
	ArticulationMap m_articulations;
	std::atomic<int> m_delayMs{0};
	std::atomic<int> m_activeUiNotes{0};
	std::atomic<std::uint32_t> m_noteOnGeneration{0};

	NotePlayHandle* m_notes[NumKeys];
	NotePlayHandleList m_sustainedNotes;

	int m_runningMidiNotes[NumKeys];
	QMutex m_midiNotesMutex;

	bool m_sustainPedalPressed;

	bool m_silentBuffersProcessed;

	//! 0.5.0 Q050-040: this track's mute as the render thread saw it one play() pass back.
	//! Render thread's own (Song::processNextBuffer is the one scheduler), so a plain bool.
	bool m_mutedLastPlay = false;

	bool m_previewMode;

	IntModel m_baseNoteModel;	//!< The "A4" or "440 Hz" key (default 69)
	IntModel m_firstKeyModel;	//!< First key the instrument reacts to
	IntModel m_lastKeyModel;	//!< Last key the instrument reacts to

	bool m_hasAutoMidiDev;
	static InstrumentTrack *s_autoAssignedTrack;

	//! The published piano-roll recording state (see setPianoRollRecording): the
	//! GUI writes it, the render thread reads it, and nothing else touches it.
	inline static std::atomic_bool s_pianoRollRecording{false};

	NotePlayHandleList m_processHandles;

	//! The clips of the pass playing, in getClipsInRange's order. Its capacity is kept at this track's
	//! clip count on the control thread (see the clipAdded connection in the constructor), so play()
	//! never grows it on the audio thread.
	clipVector m_playClips;

	FloatModel m_volumeModel;
	FloatModel m_panningModel;

	AudioBusHandle m_audioBusHandle;

	FloatModel m_pitchModel;
	IntModel m_pitchRangeModel;
	IntModel m_mixerChannelModel;
	BoolModel m_useMasterPitchModel;

	Instrument * m_instrument;
	//! 0.5.0 Q050-018: the file's original instrument element while the instrument is only a placeholder (null otherwise)
	QDomElement m_preservedInstrument;
	InstrumentSoundShaping m_soundShaping;
	InstrumentFunctionArpeggio m_arpeggio;
	InstrumentFunctionNoteStacking m_noteStacking;

	Piano m_piano;

	Microtuner m_microtuner;

	std::unique_ptr<BoolModel> m_midiCCEnable;
	std::unique_ptr<FloatModel> m_midiCCModel[MidiControllerCount];

	// 0.5.0: declared last, so they go first - the layers unlink from the models above while those still exist.
	InstrumentMidiParts m_midiParts;   //!< MX-114
	InstrumentLayers m_layers{this};   //!< MX-112

	friend class gui::InstrumentTrackView;
	friend class gui::InstrumentTrackWindow;
	friend class NotePlayHandle;
	friend class gui::InstrumentTuningView;
	friend class gui::MidiCCRackView;
	//! cluster 5186: the regression needs an instrument in the track it can watch from its own
	//! destructor, so that it can see the modulation target close BEFORE the teardown starts.
	friend class ::ModulationAudioViewTest;

} ;



} // namespace lmms

#endif // LMMS_INSTRUMENT_TRACK_H
