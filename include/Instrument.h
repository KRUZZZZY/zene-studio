/*
 * Instrument.h - declaration of class Instrument, which provides a
 *                standard interface for all instrument plugins
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_INSTRUMENT_H
#define LMMS_INSTRUMENT_H

#include <QString>

#include <cmath>
#include <cstdint>
#include <span>

#include "AudioResampler.h"
#include "Flags.h"
#include "lmms_export.h"
#include "LmmsTypes.h"
#include "Plugin.h"
#include "TimePos.h"


namespace lmms
{

// forward-declarations
class AudioPortsModel;
class AutomatableModel;
class InstrumentOutputBuses;
class InstrumentTrack;
class MidiEvent;
class NotePlayHandle;
class Track;
class SampleFrame;


class LMMS_EXPORT Instrument : public Plugin
{
	Q_OBJECT
public:
	enum class Flag
	{
		NoFlags = 0x00,
		IsSingleStreamed = 0x01,	/*! Instrument provides a single audio stream for all notes */
		IsMidiBased = 0x02,			/*! Instrument is controlled by MIDI events rather than NotePlayHandles */
		IsNotBendable = 0x04,		/*! Instrument can't react to pitch bend changes */
	};

	using Flags = lmms::Flags<Flag>;

	Instrument(InstrumentTrack * _instrument_track,
			const Descriptor * _descriptor,
			const Descriptor::SubPluginFeatures::Key * key = nullptr,
			Flags flags = Flag::NoFlags);
	//! MX-157: takes the latency this instrument reported off its track (reportLatency).
	~Instrument() override;

	// --------------------------------------------------------------------
	// functions that can/should be re-implemented:
	// --------------------------------------------------------------------

	virtual bool hasNoteInput() const { return true; }

	//! Whether slide (portamento) notes are meaningful on this instrument.
	//! Instruments that play discrete samples (e.g. AudioFileProcessor)
	//! return false: slide notes then play as regular notes, no glide
	//! (SPEC-slide-notes D-3 per-instrument opt-out).
	virtual bool supportsSlideNotes() const { return true; }

	// if the plugin doesn't play each note, it can create an instrument-
	// play-handle and re-implement this method, so that it mixes its
	// output buffer only once per audio engine period
	virtual void play( SampleFrame* _working_buffer );

	//! Span-based entry point; calls playImpl(). Used by the audio engine
	//! for all play handles.
	void play(std::span<SampleFrame> out)
	{
		playImpl(out);
	}

	// to be implemented by actual plugin
	virtual void playNote( NotePlayHandle * /* _note_to_play */,
						SampleFrame* /* _working_buf */ )
	{
	}

	//! Span-based entry point; calls playNoteImpl(). Used by the audio engine
	//! for all note play handles.
	void playNote( NotePlayHandle* noteToPlay, std::span<SampleFrame> out )
	{
		playNoteImpl(noteToPlay, out);
	}

	// needed for deleting plugin-specific-data of a note - plugin has to
	// cast void-ptr so that the plugin-data is deleted properly
	// (call of dtor if it's a class etc.)
	virtual void deleteNotePluginData( NotePlayHandle * _note_to_play );

	//! Change any playback states retained by this instrument away from the play path.
	virtual void applyConverterMode(AudioResampler::Mode mode) { (void)mode; }
	//! Prepare the retained states as a set; adoption and discard cannot fail.
	virtual auto prepareConverterMode(AudioResampler::Mode mode) noexcept -> bool
	{
		(void)mode;
		return true;
	}
	virtual void adoptPreparedConverterMode() noexcept {}
	virtual void discardPreparedConverterMode() noexcept {}

	//! Change the playback state retained by a live note, if this instrument owns one.
	virtual void applyConverterModeToNote(NotePlayHandle* note, AudioResampler::Mode mode)
	{
		(void)note;
		(void)mode;
	}
	virtual auto prepareConverterModeForNote(NotePlayHandle* note, AudioResampler::Mode mode) noexcept -> bool
	{
		(void)note;
		(void)mode;
		return true;
	}
	virtual void adoptPreparedConverterModeForNote(NotePlayHandle* note) noexcept { (void)note; }
	virtual void discardPreparedConverterModeForNote(NotePlayHandle* note) noexcept { (void)note; }

	// Get number of sample-frames that should be used when playing beat
	// (note with unspecified length)
	// Per default this function returns 0. In this case, channel is using
	// the length of the longest envelope (if one active).
	virtual f_cnt_t beatLen( NotePlayHandle * _n ) const;


	// This method can be overridden by instruments that need a certain
	// release time even if no envelope is active. It returns the time
	// in milliseconds that these instruments would like to have for
	// their release stage.
	virtual float desiredReleaseTimeMs() const
	{
		return 0.f;
	}

	// Converts the desired release time in milliseconds to the corresponding
	// number of frames depending on the sample rate.
	f_cnt_t desiredReleaseFrames() const
	{
		const sample_rate_t sampleRate = getSampleRate();

		return static_cast<f_cnt_t>(std::ceil(desiredReleaseTimeMs() * sampleRate / 1000.f));
	}

	sample_rate_t getSampleRate() const;

	bool isSingleStreamed() const
	{
		return m_flags.testFlag(Instrument::Flag::IsSingleStreamed);
	}

	bool isMidiBased() const
	{
		return m_flags.testFlag(Instrument::Flag::IsMidiBased);
	}

	//! Returns the audio ports model of this instrument or nullptr if it does
	//! not have any audio ports (e.g. legacy instruments)
	virtual auto audioPortsModel() const -> const AudioPortsModel*
	{
		return nullptr;
	}

	//! 0.5.0 MX-114: the instrument's extra audio output buses (InstrumentOutputBuses.h), or nullptr for an
	//! instrument with one output - every built-in. The hosts (CLAP, VST3, LV2) return theirs.
	virtual auto outputBuses() -> InstrumentOutputBuses* { return nullptr; }

	/*! 0.5.0 MX-120: the handle this instrument takes PER-NOTE values for @a parameter under - a CLAP
	 *  parameter id the plug-in marks modulatable per key or note, a VST3 note-expression type - or -1 when
	 *  it has no per-note path for it (a per-voice modulator then falls back to the newest note). Control
	 *  thread; the handle is what the audio thread passes to modulateNote(). */
	virtual auto perNoteTarget(const AutomatableModel*) const -> std::int64_t { return -1; }
	/*! Audio thread, allocation-free: the note on (@a channel, @a key) gets @a target's per-note value -
	 *  @a offset in the parameter's own units (CLAP note modulation adds it to the parameter), @a normalized
	 *  as 0..1 (a VST3 note expression's absolute value). */
	virtual void modulateNote(std::int64_t /*target*/, int /*channel*/, int /*key*/, float /*offset*/,
		float /*normalized*/)
	{
	}

	/*! MX-157 (BUGS 11.34), control thread: the latency in frames the hosted plug-in reports. The CLAP,
	 *  VST3 and LV2 hosts call it after every (re)prepare and when the plug-in signals a change. It becomes
	 *  the track's source latency (AudioBusHandle::setSourceLatencyFrames), which the mixer's PDC graph
	 *  adds to the track's own chain, and the latency of the instrument's routed output buses
	 *  (InstrumentOutputBuses::setSourceLatencyFrames). Both are lock-free values the audio thread only
	 *  reads. An instrument that never calls it reports 0; the value goes with the instrument. */
	void reportLatency(int frames);
	//! MX-157: what the last reportLatency() published on this instrument's track; 0 by default.
	int reportedLatencyFrames() const;

	bool isBendable() const
	{
		return !m_flags.testFlag(Instrument::Flag::IsNotBendable);
	}

	// sub-classes can re-implement this for receiving all incoming
	// MIDI-events
	inline virtual bool handleMidiEvent( const MidiEvent&, const TimePos& = TimePos(), f_cnt_t offset = 0 )
	{
		return true;
	}

	QString fullDisplayName() const override;

	// --------------------------------------------------------------------
	// provided functions:
	// --------------------------------------------------------------------

	//! instantiate instrument-plugin with given name or return NULL
	//! on failure
	static Instrument * instantiate(const QString & _plugin_name,
		InstrumentTrack * _instrument_track,
		const Plugin::Descriptor::SubPluginFeatures::Key* key,
		bool keyFromDnd = false);

	virtual bool isFromTrack( const Track * _track ) const;

	//! 0.5.0 Q050-018: empty when the instrument is what the project asked for; otherwise why it is only a
	//! placeholder (its module, or the plug-in binary a VST/VST3/CLAP wrapper hosts, is not available).
	//! The track then keeps the file's original instrument XML and writes it back on save.
	virtual QString unavailableReason() const { return {}; }

	inline InstrumentTrack * instrumentTrack() const
	{
		return m_instrumentTrack;
	}

	// --------------------------------------------------------------------
	// Parameter enumeration (Lua scripting API v0, spec section 3)
	//
	// Instruments are enumerated by index rather than by name: the base class
	// has no name-based parameter lookup, and Plugin::childModel() answers a
	// static dummy model for every plugin that does not override it, so a
	// name-based binding would silently read and write a shared throwaway
	// model. Only models with a non-empty display name are counted; the order
	// is the QObject child order and is stable for the instrument's lifetime.
	//
	// These accessors walk the QObject tree and therefore allocate: they are
	// for the scripting worker thread and the apply side only, never for the
	// audio thread.
	// --------------------------------------------------------------------

	//! Number of named automatable parameters this instrument exposes.
	int parameterCount() const;

	//! Parameter model at \a index, or nullptr when out of range.
	AutomatableModel* parameterModel(int index) const;

	//! Display name of the parameter at \a index, or an empty string.
	QString parameterName(int index) const;


signals:
	//! Emitted before Instrument's state is torn down.
	void aboutToBeDestroyed();

protected:
	//! To be implemented by AudioPlugin or plugin implementation.
	//! The default implementation calls the legacy single-buffer
	//! play(SampleFrame*) method so that existing instruments keep working.
	virtual void playImpl(std::span<SampleFrame> out)
	{
		play(out.data());
	}

	//! To be implemented by AudioPlugin or plugin implementation.
	//! The default implementation calls the legacy single-buffer
	//! playNote(NotePlayHandle*, SampleFrame*) method so that existing
	//! instruments keep working.
	virtual void playNoteImpl(NotePlayHandle* noteToPlay, std::span<SampleFrame> out)
	{
		playNote(noteToPlay, out.data());
	}

	// fade in to prevent clicks
	void applyFadeIn(SampleFrame* buf, NotePlayHandle * n);

	// instruments may use this to apply a soft fade out at the end of
	// notes - method does this only if really less or equal
	// desiredReleaseFrames() frames are left
	void applyRelease( SampleFrame* buf, const NotePlayHandle * _n );

	float computeReleaseTimeMsByFrameCount(f_cnt_t frames) const;


private:
	InstrumentTrack * m_instrumentTrack;
	Flags m_flags;
};


LMMS_DECLARE_OPERATORS_FOR_FLAGS(Instrument::Flag)


} // namespace lmms

#endif // LMMS_INSTRUMENT_H
