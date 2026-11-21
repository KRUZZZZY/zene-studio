/*
 * Sf2Player.h - a soundfont2 player using fluidSynth
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


#ifndef SF2_PLAYER_H
#define SF2_PLAYER_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <fluidsynth.h>
#include <memory>
#include <QByteArray>
#include <QString>
#include <mutex>
#include <samplerate.h>
#include <vector>

#include "ArrayVector.h"
#include "AudioEngine.h"
#include "AudioResampler.h"
#include "Instrument.h"
#include "InstrumentView.h"
#include "LcdSpinBox.h"
#include "MidiHandoffQueue.h"
#include "SampleFrame.h"

class QLabel;

namespace lmms
{


struct Sf2PluginData;
class NotePlayHandle;

namespace gui
{
class Knob;
class PixmapButton;
class Sf2InstrumentView;
class PatchesDialog;
} // namespace gui


/**
 * A bounded single-producer/single-consumer queue of pointers.
 *
 * The instrument hands synths between the UI thread and the render thread with
 * it. Storage is fixed in the object: pushing and popping copy a pointer and
 * update one atomic index, so neither side allocates or waits. Each direction
 * has exactly one producer and one consumer, which is the whole synchronisation
 * the synth hand-off needs.
 */
template<typename T, std::size_t Capacity>
class Sf2PointerQueue
{
	static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
		"the capacity must be a power of two, at least 2");

public:
	//! Queues \a value. @return false when the queue is full.
	auto push(T value) noexcept -> bool
	{
		const std::size_t tail = m_tail.load(std::memory_order_relaxed);
		const std::size_t head = m_head.load(std::memory_order_acquire);
		if (tail - head >= Capacity) { return false; }
		m_items[tail & Mask] = value;
		m_tail.store(tail + 1, std::memory_order_release);
		return true;
	}

	//! Takes the oldest value into \a value. @return false when the queue is empty.
	auto pop(T& value) noexcept -> bool
	{
		const std::size_t head = m_head.load(std::memory_order_relaxed);
		const std::size_t tail = m_tail.load(std::memory_order_acquire);
		if (head == tail) { return false; }
		value = m_items[head & Mask];
		m_head.store(head + 1, std::memory_order_release);
		return true;
	}

private:
	static constexpr std::size_t Mask = Capacity - 1;
	std::array<T, Capacity> m_items{};
	alignas(64) std::atomic<std::size_t> m_head{0};
	alignas(64) std::atomic<std::size_t> m_tail{0};
};

//! One loaded preset, kept on the UI side so patch names never touch the audio synth.
struct Sf2PresetInfo
{
	int bank = 0;
	int program = 0;
	QString name;
};

/**
 * A synth the UI thread created, together with the settings it was built with.
 *
 * FluidSynth requires that a settings object is used by one synth only, so
 * every synth gets its own settings object and keeps it until the synth is
 * deleted.
 */
struct Sf2OwnedSynth
{
	fluid_synth_t* synth = nullptr;
	fluid_settings_t* settings = nullptr;
};


/**
 * A synth handed from the UI thread to the render thread.
 *
 * The UI builds and configures the synth completely before publishing it; the
 * render thread adopts it and is the only thread that uses it afterwards. The
 * old synth travels back on the retire queue, where the UI thread deletes it
 * once the render thread has stopped using it.
 */
struct Sf2SynthSwap
{
	fluid_synth_t* synth = nullptr;
	int fontId = -1;
	sample_rate_t sampleRate = 0;
};

/**
 * A non-owning reference to a single FluidSynth voice. Captures some initial
 * properties of the referenced voice to help manage changes to it over time.
 */
class FluidVoice
{
public:
	//! Create a reference to the voice currently pointed at by `voice`.
	explicit FluidVoice(fluid_voice_t* voice) :
		m_voice{voice},
		m_id{fluid_voice_get_id(voice)},
		m_coarseTune{fluid_voice_gen_get(voice, GEN_COARSETUNE)}
	{ }

	//! Get a pointer to the referenced voice.
	fluid_voice_t* get() const noexcept { return m_voice; }

	//! Get the original coarse tuning of the referenced voice.
	float coarseTune() const noexcept { return m_coarseTune; }

	//! Test whether this object still refers to the original voice.
	bool isValid() const
	{
		return fluid_voice_get_id(m_voice) == m_id && fluid_voice_is_playing(m_voice);
	}

private:
	fluid_voice_t* m_voice;
	unsigned int m_id;
	float m_coarseTune;
};

//! The state the SF2 player keeps for one playing note.
struct Sf2PluginData
{
	int midiNote;
	int lastPanning;
	float lastVelocity;
	// The soundfonts I checked used at most two voices per note, so space for
	// four should be safe. This may need to be increased if a soundfont with
	// more voices per note is found.
	ArrayVector<FluidVoice, 4> fluidVoices;
	//! Set by the render thread once the note-on reached the synth.
	std::atomic<bool> noteOnDone{false};
	//! Set when a note-off is queued (slot event or the note's teardown).
	std::atomic<bool> noteOffSent{false};
	//! Set by the render thread once a slot note-off was processed.
	std::atomic<bool> noteOffDone{false};
	panning_t panning;
};

/**
 * A preallocated slot holding one note's plugin data.
 *
 * The note path runs on the audio thread, so a note's data cannot be allocated
 * there: the slots are owned by the instrument and a note claims one of them
 * for as long as it lives. The state word carries the slot's generation, so an
 * event that names a slot which was released (and possibly claimed by another
 * note since) is recognised as stale instead of touching a dead note.
 */
struct Sf2PluginDataSlot
{
	Sf2PluginData data{};
	//! 0 when the slot is free; otherwise (generation << 1) | 1 while a note owns it.
	std::atomic<std::uint32_t> state{0};
};

/**
 * One note event, handed from the note stage to the render stage.
 *
 * The event carries the frame within the period the note happens at and
 * whether it starts or stops the note, so the render stage does not have to
 * read the note's mutable state to order the events.
 */
struct Sf2NoteEvent
{
	NotePlayHandle* handle = nullptr;
	Sf2PluginDataSlot* slot = nullptr;
	//! The slot's generation when the event was queued.
	std::uint32_t generation = 0;
	//! The frame within the period the event happens at.
	f_cnt_t offset = 0;
	//! The MIDI pitch, so a teardown note-off does not need the dead note handle.
	int midiNote = 0;
	//! True for a note-on, false for a note-off.
	bool isNew = false;
	//! True for a note-off queued when the note handle is destroyed: no slot check.
	bool standalone = false;
};


class Sf2Instrument : public Instrument
{
	Q_OBJECT
	mapPropertyFromModel(int,getBank,setBank,m_bankNum);
	mapPropertyFromModel(int,getPatch,setPatch,m_patchNum);

public:
	Sf2Instrument( InstrumentTrack * _instrument_track );
	~Sf2Instrument() override;

	void deleteNotePluginData( NotePlayHandle * _n ) override;

	void saveSettings( QDomDocument & _doc, QDomElement & _parent ) override;
	void loadSettings( const QDomElement & _this ) override;

	void loadFile( const QString & _file ) override;

	AutomatableModel * childModel( const QString & _modelName ) override;

	QString nodeName() const override;

	gui::PluginView* instantiateView( QWidget * _parent ) override;
	
	void setParameter( const QString & _param, const QString & _value );


public slots:
	//! The name of the patch the player has loaded. The SF2 realtime test
	//! reads it to see that the preset list survived a reload.
	QString getCurrentPatchName();

	void openFile( const QString & _sf2File, bool updateTrackName = true );
	void updatePatch();
	void reloadSynth();

	// We can't really support sample-exact with the way IPH and FS work.
	// So, sig/slots work just fine for the synth settings right now.
	void updateReverbOn();
	void updateReverb();
	void updateChorusOn();
	void updateChorus();
	void updateGain();
	void updateTuning();

	//! Test seam for the SF2 realtime test: the address of the FluidSynth synth
	//! the player uses, so the test can inspect what was configured on it.
	quintptr currentSynthAddress() const { return reinterpret_cast<quintptr>(m_synth); }

	//! Test seam for the SF2 realtime test: the number of notes the player
	//! counts as running, over every pitch. The player counts a note from its
	//! note-on until its note-off, so a note that stays counted is a note that
	//! is still sounding.
	int runningNotesForTest() const;

	//! Test seam for the SF2 realtime test: fills the bounded note event queue
	//! with events the render stage drops, so that the next event the player
	//! publishes is refused by the queue.
	//! @return the number of events the queue holds afterwards.
	int fillNoteEventQueueForTest();

private:
	void playImpl(std::span<SampleFrame> out) override;
	void playNoteImpl(NotePlayHandle* nph, std::span<SampleFrame> out) override;

	AudioResampler m_resampler;
	std::array<SampleFrame, DEFAULT_BUFFER_SIZE> m_buffer;
	std::span<SampleFrame> m_bufferView;

	//! The synth the render thread currently renders. The UI thread owns it and
	//! only borrows it here; the render thread never deletes it.
	fluid_synth_t* m_synth = nullptr;
	//! The soundfont the render thread's synth plays, for the program select.
	int m_activeFontId = -1;

	//! Every synth the UI thread created, with the settings it was built with.
	//! The render thread borrows one of them; the UI thread deletes them all.
	std::vector<Sf2OwnedSynth> m_ownedSynths;

	//! A synth that only holds the loaded soundfont is gone: every synth that
	//! plays the font parses the instrument's own copy of it into a font
	//! instance of its own.
	std::shared_ptr<const QByteArray> m_fontData;

	//! Synthesis parameters the UI requests. The render thread applies them;
	//! the UI never calls into the synth the render thread is using.
	std::atomic<float> m_requestedGain{1.0f};
	std::atomic<int> m_requestedReverbOn{0};
	std::atomic<float> m_requestedReverbRoomSize{0.2f};
	std::atomic<float> m_requestedReverbDamping{0.0f};
	std::atomic<float> m_requestedReverbWidth{0.5f};
	std::atomic<float> m_requestedReverbLevel{0.9f};
	std::atomic<int> m_requestedChorusOn{0};
	std::atomic<float> m_requestedChorusNum{3.0f};
	std::atomic<float> m_requestedChorusLevel{2.0f};
	std::atomic<float> m_requestedChorusSpeed{0.3f};
	std::atomic<float> m_requestedChorusDepth{8.0f};
	std::atomic<int> m_requestedBank{0};
	std::atomic<int> m_requestedPatch{0};
	//! The pitch offset of every MIDI key in cents, relative to the equal
	//! temperament. The UI writes it; the render thread reads it.
	std::array<std::atomic<double>, 128> m_requestedTuning{};
	//! Even while the UI writes the tuning table; the render thread only reads
	//! a complete, even snapshot.
	std::atomic<std::uint32_t> m_tuningGeneration{0};

	//! The values the render thread last sent to the current synth.
	float m_appliedGain = -1.0f;
	int m_appliedReverbOn = -1;
	float m_appliedReverbRoomSize = -1.0f;
	float m_appliedReverbDamping = -1.0f;
	float m_appliedReverbWidth = -1.0f;
	float m_appliedReverbLevel = -1.0f;
	int m_appliedChorusOn = -1;
	float m_appliedChorusNum = -1.0f;
	float m_appliedChorusLevel = -1.0f;
	float m_appliedChorusSpeed = -1.0f;
	float m_appliedChorusDepth = -1.0f;
	int m_appliedBank = -1;
	int m_appliedPatch = -1;
	int m_appliedFontId = -2;

	QString m_filename;
	//! Absolute file the published synth was built from, for a sample-rate reload.
	QString m_sourceFile;
	//! Presets of the published soundfont, so UI queries never read the audio synth.
	std::vector<Sf2PresetInfo> m_presets;

	//! How many notes may be pending for this instrument in one period.
	static constexpr std::size_t NoteEventCapacity = 512;
	//! How many notes may hold plugin data at the same time. FluidSynth's
	//! default polyphony is 256 voices, so the pool has to hold at least that
	//! many notes to keep a full load from being refused.
	static constexpr std::size_t PluginDataSlotCount = 512;
	//! How many synth replacements may wait for the render thread.
	static constexpr std::size_t SynthHandoffCapacity = 8;

	//! The number of notes currently running per MIDI pitch. Atomic: the note
	//! path updates it from the render stage and from the note teardown.
	std::array<std::atomic<int>, 128> m_notesRunning{};
	//! Teardown note-offs the bounded event queue refused, counted per MIDI
	//! pitch. The note teardown adds to the pitch's counter and the render
	//! stage takes the count away again: a started note always gets its
	//! note-off, even when the queue was full. Fixed size: no allocation, no
	//! lock, no waiting.
	std::array<std::atomic<int>, 128> m_pendingTeardownNotes{};
	//! Set when a teardown note-off was added to the counters above, so the
	//! render stage knows that it has to look at them.
	std::atomic<bool> m_pendingTeardownDirty{false};
	//! A note slot that is never claimed: the test seam's filler events name
	//! it, and the render stage drops them because a free slot's generation
	//! never matches an event.
	Sf2PluginDataSlot m_fillerSlot;
	//! Set by the render thread when it adopts a synth; read by renderFrames.
	std::atomic<sample_rate_t> m_internalSampleRate{0};
	int m_lastMidiPitch;
	int m_lastMidiPitchRange;
	int m_channel;


	//! UI -> render: synths ready to be adopted.
	Sf2PointerQueue<Sf2SynthSwap, SynthHandoffCapacity> m_pendingSynths;
	//! Render -> UI: synths the render thread no longer uses.
	Sf2PointerQueue<fluid_synth_t*, SynthHandoffCapacity> m_retiredSynths;
	//! Render-owned synth waiting for retire queue space (one at most).
	Sf2SynthSwap m_deferredSynth;
	//! Serialises UI-side producers/consumers of the two queues; the audio thread never takes it.
	mutable std::mutex m_synthHandoffMutex;

	gui::LcdSpinBoxModel m_bankNum;
	gui::LcdSpinBoxModel m_patchNum;

	FloatModel m_gain;

	BoolModel m_reverbOn;
	FloatModel m_reverbRoomSize;
	FloatModel m_reverbDamping;
	FloatModel m_reverbWidth;
	FloatModel m_reverbLevel;

	BoolModel m_chorusOn;
	FloatModel m_chorusNum;
	FloatModel m_chorusLevel;
	FloatModel m_chorusSpeed;
	FloatModel m_chorusDepth;

	/**
	 * The note events the note stage hands to the render stage.
	 *
	 * A bounded, preallocated, lock-free queue: the note path pushes into it
	 * and the render path drains it, so neither allocates, grows a container
	 * nor waits for a lock. The render stage re-orders the drained events by
	 * their offset, so the order the pushes happened in does not matter.
	 */
	MidiHandoffQueue<NoteEventCapacity, Sf2NoteEvent> m_noteEvents;

	//! The preallocated per-note plugin data. A note claims a slot for its life.
	std::array<Sf2PluginDataSlot, PluginDataSlotCount> m_pluginDataSlots;

	//! The events the render stage drained this period, in processing order.
	std::array<Sf2NoteEvent, NoteEventCapacity> m_pendingEvents;
	std::size_t m_pendingEventCount = 0;

private:
	void noteOn( Sf2PluginData * n );
	void noteOff( Sf2PluginData * n );
	void renderFrames( f_cnt_t frames, SampleFrame* buf );
	//! Claims a free plugin-data slot. Returns nullptr when every slot is taken.
	Sf2PluginDataSlot* acquirePluginDataSlot(std::uint32_t& generation) noexcept;
	//! Gives the slot back to the pool.
	void releasePluginDataSlot(Sf2PluginDataSlot* slot) noexcept;
	//! Queues a note event for the render stage. False when the bounded queue refused it.
	auto publishNoteEvent(NotePlayHandle* handle, Sf2PluginDataSlot* slot,
		std::uint32_t generation, f_cnt_t offset, bool isNew) noexcept -> bool;
	//! Takes the event at \a index out of the render stage's pending list.
	void removePendingEvent(std::size_t index) noexcept;
	//! Render thread: apply the teardown note-offs the bounded event queue
	//! refused, so that a started note never stays sounding.
	void applyPendingTeardownNoteOffs() noexcept;
	//! Render thread: adopt any synth the UI published.
	void serviceSynthHandoff() noexcept;
	//! Render thread: send the requested state (gain, effects, program) to the
	//! synth. Every call is lock free: the synth runs with FluidSynth's
	//! thread-safe API switched off, so the render thread never waits.
	void applyControls() noexcept;
	//! Render thread: forget what was sent, so the next applyControls() sends
	//! the whole state to the synth the render thread adopted.
	void resetAppliedControls() noexcept;
	//! Render thread: the pitch offset of a MIDI key in cents, relative to the
	//! equal temperament.
	double tuningOffsetForKey(int key) const noexcept;
	//! UI thread: read FluidSynth's own defaults into the effect models.
	void readSynthDefaults();
	//! UI thread: build a synth with its own settings object (no font loaded).
	auto createSynth(sample_rate_t outputRate) -> Sf2OwnedSynth;
	//! UI thread: delete a synth and the settings object it was built with.
	void destroySynth(fluid_synth_t* synth) noexcept;
	//! UI thread: publish a fully configured synth to the render thread.
	auto publishSynth(fluid_synth_t* synth, int fontId, sample_rate_t sampleRate) -> bool;
	//! UI thread: read the soundfont into the instrument's own copy of it.
	auto loadFont(const QByteArray& nativePath) -> bool;
	//! UI thread: load the soundfont into a synth, as a font of its own.
	auto addFontTo(fluid_synth_t* synth) -> int;
	//! UI thread: delete synths the render thread has finished with.
	void drainRetiredSynths();
	void drainRetiredSynthsLocked();
	//! UI thread: collect the presets of a loaded synth (no render thread access).
	void collectPresets(fluid_synth_t* synth, std::vector<Sf2PresetInfo>& presets);
	//! UI thread: a private synth for the patch dialog, so the dialog never reads the audio synth.
	auto createPatchDialogSynth() -> Sf2OwnedSynth;

	friend class gui::Sf2InstrumentView;

signals:
	void fileLoading();
	void fileChanged();
	void patchChanged();

} ;


namespace gui
{


class Sf2InstrumentView : public InstrumentViewFixedSize
{
	Q_OBJECT
public:
	Sf2InstrumentView( Instrument * _instrument,
					QWidget * _parent );
	~Sf2InstrumentView() override = default;

private:
	void modelChanged() override;

	PixmapButton * m_fileDialogButton;
	PixmapButton * m_patchDialogButton;

	LcdSpinBox * m_bankNumLcd;
	LcdSpinBox * m_patchNumLcd;

	QLabel * m_filenameLabel;
	QLabel * m_patchLabel;

	Knob	* m_gainKnob;

	PixmapButton * m_reverbButton;
	Knob	* m_reverbRoomSizeKnob;
	Knob	* m_reverbDampingKnob;
	Knob	* m_reverbWidthKnob;
	Knob	* m_reverbLevelKnob;

	PixmapButton * m_chorusButton;
	Knob * m_chorusNumKnob;
	Knob * m_chorusLevelKnob;
	Knob * m_chorusSpeedKnob;
	Knob * m_chorusDepthKnob;

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
