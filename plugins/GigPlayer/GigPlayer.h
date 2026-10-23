/*
 * GigPlayer.h - a GIG player using libgig (based on Sf2 player plugin)
 *
 * Copyright (c) 2008 Paul Giblock <drfaygo/at/gmail/dot/com>
 * Copyright (c) 2009-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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


#ifndef GIG_PLAYER_H
#define GIG_PLAYER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <array>
#include <QList>
#include <samplerate.h>

#include "AudioEngine.h"
#include "AudioResampler.h"
#include "Instrument.h"
#include "PixmapButton.h"
#include "InstrumentView.h"
#include "Knob.h"
#include "LcdSpinBox.h"
#include "SampleFrame.h"
#include "gig.h"


class QLabel;


namespace lmms
{


class NotePlayHandle;

namespace gui
{
class PatchesDialog;
class GigInstrumentView;
}




struct GIGPluginData
{
	int midiNote;
} ;




// Load a GIG file using libgig
class GigInstance
{
public:
	GigInstance( QString filename ) :
		riff( filename.toUtf8().constData() ),
		gig( &riff )
	{}

private:
	RIFF::File riff;

public:
	gig::File gig;
} ;


//! Notes and samples (voices) live in segments. A segment is allocated and
//! filled on the control thread and only published once it is complete; it is
//! never moved or freed while the instrument exists, so a note or a voice that
//! is already sounding keeps its address when the next segment is added. The
//! audio thread only claims and releases entries. When a pool runs out it asks
//! the control thread for as many segments as it is missing and retries on a
//! later period, so playback is never silently limited to a pool size.
constexpr std::size_t GigNoteSegmentShift = 9;	// 512 note slots per segment
constexpr std::size_t GigNoteSegmentSize = std::size_t{1} << GigNoteSegmentShift;
constexpr std::uint32_t GigNoteSegmentMask = static_cast<std::uint32_t>(GigNoteSegmentSize - 1);

constexpr std::size_t GigVoiceSegmentShift = 6;	// 64 voices per segment
constexpr std::size_t GigVoiceSegmentSize = std::size_t{1} << GigVoiceSegmentShift;
constexpr std::uint32_t GigVoiceSegmentMask = static_cast<std::uint32_t>(GigVoiceSegmentSize - 1);

//! How many segments a pool can hold. A segment is never freed while the
//! instrument lives, so this bounds the pool's memory, not its behaviour: the
//! audio thread asks for another segment and starts the note on a later period
//! long before this is reached (1024 segments are 524288 notes or 65536 voices).
constexpr std::size_t MaxGigSegments = 1024;

//! The segments a pool is built with. These are starting sizes, not ceilings.
constexpr std::size_t InitialGigNoteSegments = 1;
constexpr std::size_t InitialGigVoiceSegments = 8;

//! An index that refers to no note, voice or list entry.
constexpr std::uint32_t GigNoIndex = 0xFFFFFFFFu;




// Stores options for the notes, e.g. velocity and release time
struct Dimension
{
	Dimension() :
		release( false )
	{
		for( int i = 0; i < 8; ++i )
		{
			DimValues[i] = 0;
		}
	}

	uint DimValues[8];
	bool release;
} ;




// Takes information from the GIG file for a certain note and provides the
// amplitude (0-1) to multiply the signal by (internally incrementing the
// position in the envelope when asking for the amplitude).
class ADSR
{
	// From the file
	float preattack; // initial amplitude (0-1)
	float attack; // 0-60s
	float decay1; // 0-60s
	float decay2; // 0-60s
	bool infiniteSustain; // i.e., no decay2
	float sustain; // sustain amplitude (0-1)
	float release; // 0-60s

	// Used to calculate current amplitude
	float amplitude;
	bool isAttack;
	bool isRelease;
	bool isDone;
	f_cnt_t attackPosition;
	f_cnt_t attackLength;
	f_cnt_t decayLength;
	f_cnt_t releasePosition;
	f_cnt_t releaseLength;

public:
	ADSR();
	ADSR( gig::DimensionRegion * region, int sampleRate );
	void keyup(); // We will begin releasing starting now
	bool done(); // Is this sample done playing?
	float value(); // What's the current amplitude
	void inc( f_cnt_t num ); // Increment internal positions by num
} ;




// The sample from the GIG file with our current position in both the sample
// and the envelope.
//
// A GigSample is a reusable voice: the resampler and the two frame buffers
// are built once, when the instrument is created, so that attaching a sample
// to a voice on the audio thread neither allocates nor frees. Attaching a
// sample resets the resampler, which makes the voice indistinguishable from a
// freshly created one.
class GigSample
{
public:
	GigSample();
	~GigSample() = default;

	// A voice owns its resampler, so it is neither copyable nor movable.
	GigSample( const GigSample& ) = delete;
	GigSample& operator=( const GigSample& ) = delete;

	// Attaches a sample to this voice. This does exactly what the old
	// GigSample constructor did, except that the resampler is reset instead of
	// being built from scratch.
	void attach( gig::Sample* pSample, gig::DimensionRegion* pDimRegion, float attenuation,
			float desiredFreq );

	gig::Sample * sample;
	gig::DimensionRegion * region;
	float attenuation;
	ADSR adsr;

	// The position in sample
	f_cnt_t pos;

	// Whether to change the pitch of the samples, e.g. if there's only one
	// sample per octave and you want that sample pitch shifted for the rest of
	// the notes in the octave, this will be true
	bool pitchtrack;

	// Used to convert sample rates
	AudioResampler m_resampler;
	std::array<SampleFrame, DEFAULT_BUFFER_SIZE> m_sourceBuffer;
	std::array<SampleFrame, DEFAULT_BUFFER_SIZE> m_mixBuffer;
	std::span<SampleFrame> m_sourceBufferView;
	std::span<SampleFrame> m_mixBufferView;

	// Used changing the pitch of the note if desired
	float sampleFreq;
	float freqFactor;
} ;




// What portion of a note are we in?
enum class GigState
{
	// We just pressed the key
	KeyDown,
	// The note is currently playing
	PlayingKeyDown,
	// We just released the key
	KeyUp,
	// The note is being released, e.g. a release sample is playing
	PlayingKeyUp,
	// The note is done playing, you can delete this note now
	Completed
} ;




// Corresponds to a certain midi note pressed, but may contain multiple samples
class GigNote
{
public:
	int midiNote;
	int velocity;
	bool release; // Whether to trigger a release sample on key up
	bool isRelease; // Whether this is a release sample, changes when we delete it
	GigState state;
	float frequency;

	GigNote( int midiNote, int velocity, float frequency )
		: midiNote( midiNote ), velocity( velocity ),
		  release( false ), isRelease( false ), state( GigState::KeyDown ),
		  frequency( frequency )
	{
	}
} ;


//! The file that is open and the instrument selected in it. The state is
//! published to the audio thread by swapping the pointer, so a file that is
//! loaded while the audio thread renders never makes the audio thread wait.
struct GigFileState;


//! One segment of the note pool. A slot's packed index (segment * size +
//! offset) is stable for the life of the instrument, so a note can be
//! addressed from any thread by that index alone. A segment is filled on the
//! control thread and published only once it is complete, and it is never
//! moved or freed while the instrument lives.
//!
//! Everything in here is owned by the audio thread; the three atomics are the
//! only parts another thread touches: the GUI thread asks for a note to be
//! released by storing its generation in releaseGeneration, and the audio
//! thread picks that request up.
struct GigNoteSlot
{
	GigNote note { 0, 0, 0.0f };

	//! The first and last voice of this note's voice list. There is no per-note
	//! ceiling: a note takes one voice per matching region of the file.
	std::uint32_t firstVoice = GigNoIndex;
	std::uint32_t lastVoice = GigNoIndex;

	//! How many voices this note plays right now
	std::uint32_t voiceCount = 0;

	//! The neighbours of this note in the note-order list
	std::uint32_t orderPrev = GigNoIndex;
	std::uint32_t orderNext = GigNoIndex;

	//! Whether this slot currently holds a note
	bool active = false;

	//! Whether the release pass of this note still has to add its samples. The
	//! pass is retried on the next period when the voice pool had no room, so
	//! the note is not completed before its release samples played.
	bool releasePassPending = false;

	//! The file the note was started in. The notes of a file that is not open
	//! any more are dropped by the audio thread.
	GigFileState* fileState = nullptr;

	//! Counts how often this slot was used, so that a release request for an
	//! older note in this slot is not mistaken for the current one
	std::atomic<std::uint32_t> generation { 0 };

	//! Set by another thread to ask for this note to be released
	std::atomic<std::uint32_t> releaseGeneration { 0 };

	//! The note handle this slot belongs to
	std::atomic<NotePlayHandle*> handle { nullptr };
} ;


//! One segment of the note pool, with the indices of the slots that are free.
struct GigNoteSegment
{
	//! The slots of this segment, built before the segment is published
	std::unique_ptr<GigNoteSlot[]> noteSlots;

	//! The free slots of this segment. Audio thread only.
	std::unique_ptr<std::uint32_t[]> freeSlots;

	//! How many slots of this segment are free
	std::uint32_t freeCount = GigNoteSegmentSize;
} ;


//! One voice of the voice pool. A voice belongs to one note and is built when
//! its segment is filled on the control thread, so the audio thread never
//! constructs a GigSample. The voices of one note are a doubly linked list
//! through the pool, so a note can take as many as the file has matching
//! regions; there is no per-note ceiling.
struct GigVoice
{
	GigSample sample;

	//! The neighbours of this voice in its note's voice list
	std::uint32_t nextInNote = GigNoIndex;
	std::uint32_t prevInNote = GigNoIndex;

	//! The note that owns this voice
	std::uint32_t ownerSlot = GigNoIndex;
} ;


//! One segment of the voice pool, with the indices of the voices that are free.
struct GigVoiceSegment
{
	//! The voices of this segment, built before the segment is published
	std::unique_ptr<GigVoice[]> voices;

	//! The free voices of this segment. Audio thread only.
	std::unique_ptr<std::uint32_t[]> freeVoices;

	//! How many voices of this segment are free
	std::uint32_t freeCount = GigVoiceSegmentSize;
} ;


//! Which samples a note still owes: the normal pass of a key-down, or the
//! release pass of a key-up. A note that cannot get its voices keeps the pass
//! and retries on the next period, so a layer is never silently dropped.
enum class GigSamplePass : std::uint8_t
{
	None,
	Normal,
	Release,
} ;


//! The GIG file that is open and the instrument selected in it. The state is
//! published to the audio thread by swapping the pointer, so a file that is
//! loaded while the audio thread renders never makes the audio thread wait.
struct GigFileState
{
	//! The open GIG file, owned by this state
	GigInstance* instance = nullptr;

	//! The instrument selected in the file
	std::atomic<gig::Instrument*> instrument { nullptr };

	~GigFileState()
	{
		delete instance;
	}
} ;




class GigInstrument : public Instrument
{
	Q_OBJECT

	mapPropertyFromModel( int, getBank, setBank, m_bankNum );
	mapPropertyFromModel( int, getPatch, setPatch, m_patchNum );

public:
	GigInstrument( InstrumentTrack * _instrument_track );
	~GigInstrument() override;

	void deleteNotePluginData( NotePlayHandle * _n ) override;

	void saveSettings( QDomDocument & _doc, QDomElement & _parent ) override;
	void loadSettings( const QDomElement & _this ) override;

	void loadFile( const QString & _file ) override;

	AutomatableModel * childModel( const QString & _modelName ) override;

	QString nodeName() const override;

	gui::PluginView* instantiateView( QWidget * _parent ) override;

	QString getCurrentPatchName();


	void setParameter( const QString & _param, const QString & _value );

	//! A hook that is called while a GIG file is opened, before the file is
	//! read. It lets a test pause the file preparation and watch the audio
	//! thread, which must go on rendering while the file is prepared.
	using FileLoadHook = void (*)(void* context);

	//! Installs the hook that is called while a GIG file is opened. Passing a
	//! null hook removes it. The hook is called on the thread that opens the
	//! file, never on the audio thread.
	static void setFileLoadHook( FileLoadHook _hook, void* _context );


public slots:
	void openFile( const QString & _gigFile, bool updateTrackName = true );
	void updatePatch();
	void updateSampleRate();

	//! Runs the pool growth service on the calling thread.
	//!
	//! The instrument grows its pools from a timer, because growing allocates
	//! and must not happen on the audio thread. A test has no event loop, so it
	//! calls this instead; it refuses to run inside a render.
	void servicePoolGrowthForTest();



private:
	void playImpl(std::span<SampleFrame> out) override;
	void playNoteImpl(NotePlayHandle* _n, std::span<SampleFrame> out) override;

	// The GIG file and the instrument selected in it. The state is published
	// to the audio thread by swapping the pointer, so loading a file never
	// makes the audio thread wait.
	std::atomic<GigFileState*> m_fileState { nullptr };

	// The number of audio threads that are currently rendering with the
	// published state. A file state is only deleted when no audio thread is
	// inside a render, so the audio thread never waits for the deletion.
	std::atomic<int> m_fileReaders { 0 };

	// File states that were replaced but may still be read by an audio thread
	QList<GigFileState*> m_retiredStates;

	// Part of the UI
	QString m_filename;

	gui::LcdSpinBoxModel m_bankNum;
	gui::LcdSpinBoxModel m_patchNum;

	FloatModel m_gain;

	// The note pool. The audio thread owns it; a note is addressed by the
	// packed index of its slot, which never moves. Segments are published one
	// at a time, once they are complete, so a reader always sees a whole slot.
	std::array<std::atomic<GigNoteSegment*>, MaxGigSegments> m_noteSegments {};
	std::atomic<std::uint32_t> m_noteSegmentCount { 0 };

	//! The first note segment the audio thread looks in
	std::uint32_t m_noteHint = 0;

	// The playing notes, in the order they were started. The order is an
	// intrusive list through the slots, so starting and removing a note never
	// makes a container grow on the audio thread.
	std::uint32_t m_noteOrderHead = GigNoIndex;
	std::uint32_t m_noteOrderTail = GigNoIndex;

	// The file state the notes belong to. When another file is published, the
	// audio thread drops its notes.
	GigFileState* m_activeState = nullptr;

	// The pool of voices (samples). The voices are built when a segment is
	// filled on the control thread, so that the audio thread never constructs
	// one. A voice is addressed by the packed index of its slot.
	std::array<std::atomic<GigVoiceSegment*>, MaxGigSegments> m_voiceSegments {};
	std::atomic<std::uint32_t> m_voiceSegmentCount { 0 };

	//! The first voice segment the audio thread looks in
	std::uint32_t m_voiceHint = 0;

	// Set by the audio thread when a pool ran out or ran low; the control-thread
	// service appends the segments and clears the request. A request is a hint,
	// not a command: the audio thread keeps retrying either way.
	std::atomic<bool> m_notePoolGrowthRequested { false };
	std::atomic<bool> m_voicePoolGrowthRequested { false };

	// Drives servicePoolGrowth() on the thread the instrument lives on. It
	// never runs on the audio thread, because growing a pool allocates.
	QTimer m_poolGrowthTimer;

	// Set by another thread to ask the audio thread to drop all notes
	std::atomic<bool> m_clearNotes { false };

	// The number of notes that are playing. It is only used to decide
	// whether a request to drop the notes is worth making.
	std::atomic<int> m_activeNotes { 0 };

	// Used when determining which samples to use
	uint32_t m_RandomSeed;
	float m_currentKeyDimension;

private:
	// Delete the current GIG instance if one is open
	void freeInstance();

	// Publish a new file state and retire the previous one
	void publishFileState( GigFileState * _state );

	// Delete the retired file states that no audio thread is reading
	void reapRetiredStates();

	// The instrument selected in the given file
	gig::Instrument * selectInstrument( GigInstance * _instance ) const;

	// Open the instrument in the currently-open GIG file
	void getInstrument();

	// Drop every note and give its voices back to the voice pool
	void clearNotes();

	// Build the pools the instrument starts with. Control thread only.
	void buildInitialPools();

	// Append segments to a pool. Control thread only: appending allocates.
	// Both keep the published segments complete before they publish them.
	void growNotePool( std::size_t _additionalSegments );
	void growVoicePool( std::size_t _additionalSegments );

	// Append the segments the audio thread asked for. Control thread only.
	void servicePoolGrowth();

	// The slot or voice with the given packed index. The caller makes sure the
	// segment is published.
	GigNoteSlot & noteSlot( std::uint32_t _slot ) const;
	GigVoice & voice( std::uint32_t _voice ) const;

	// Put a note or a voice back into its pool. Audio thread only.
	void releaseNote( std::uint32_t _slot );
	void releaseVoice( std::uint32_t _voice );

	// Take a note slot or a voice, or return GigNoIndex when none is free.
	// Audio thread only: these never allocate.
	std::uint32_t acquireNoteSlot();
	std::uint32_t acquireVoice();

	// The note-order list. Audio thread only.
	void appendToNoteOrder( std::uint32_t _slot );
	void removeFromNoteOrder( std::uint32_t _slot );

	// Append a voice to the note's voice list, or take one away.
	void appendVoiceToNote( std::uint32_t _slotIndex, std::uint32_t _voice );
	void releaseVoiceOfNote( GigNoteSlot & _slot, std::uint32_t _voice );

	// Take away the voices a pass added after the given tail. Audio thread only.
	void rollBackVoices( GigNoteSlot & _slot, std::uint32_t _tailBefore );

	// Ask the control thread for room. Audio thread safe: one atomic store.
	void requestNotePoolGrowth();
	void requestVoicePoolGrowth();

	// Create "dimension" to select desired samples from GIG file based on
	// parameters such as velocity
	Dimension getDimensions( gig::Region * pRegion, int velocity, bool release );

	// Load sample data from the Gig file, looping the sample where needed
	void loadSample( GigSample& sample, SampleFrame* sampleData, f_cnt_t samples );
	void loadBidirectionalSample( gig::Sample& sample, std::int8_t* buffer, f_cnt_t position,
		f_cnt_t samples, f_cnt_t loopStart, f_cnt_t loopEnd );
	f_cnt_t getLoopedIndex( f_cnt_t index, f_cnt_t startf, f_cnt_t endf ) const;
	f_cnt_t getPingPongIndex( f_cnt_t index, f_cnt_t startf, f_cnt_t endf ) const;

	// Add every matching sample to the note, either normal samples or release
	// samples. Returns false when the voice pool had no room: the voices taken
	// by the pass are given back and the caller retries it on a later period.
	bool addSamples( std::uint32_t _slotIndex, bool wantReleaseSample,
		gig::Instrument * _instrument );

	friend class gui::GigInstrumentView;

signals:
	void fileLoading();
	void fileChanged();
	void patchChanged();

} ;


namespace gui
{


class GigInstrumentView : public InstrumentViewFixedSize
{
	Q_OBJECT
public:
	GigInstrumentView( Instrument * _instrument,
					QWidget * _parent );
	~GigInstrumentView() override = default;

private:
	void modelChanged() override;

	PixmapButton * m_fileDialogButton;
	PixmapButton * m_patchDialogButton;

	LcdSpinBox * m_bankNumLcd;
	LcdSpinBox * m_patchNumLcd;

	QLabel * m_filenameLabel;
	QLabel * m_patchLabel;

	Knob * m_gainKnob;

	static PatchesDialog * s_patchDialog;

protected slots:
	void invalidateFile();
	void showFileDialog();
	void showPatchDialog();
	void updateFilename();
	void updatePatchName();
} ;


} // namespace gui

} // namespace lmms

#endif
