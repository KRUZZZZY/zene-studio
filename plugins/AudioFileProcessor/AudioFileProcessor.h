/*
 * AudioFileProcessor.h - declaration of class AudioFileProcessor
 *                          (instrument-plugin for using audio-files)
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

#ifndef LMMS_AUDIO_FILE_PROCESSOR_H
#define LMMS_AUDIO_FILE_PROCESSOR_H


#include "AutomatableModel.h"
#include "ComboBoxModel.h"

#include "Instrument.h"
#include "Sample.h"
#include "SamplerVoicePool.h"
#include "LmmsTypes.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#ifdef LMMS_TESTING
#include <functional>
#endif


namespace lmms
{

namespace gui { class AudioFileProcessorView; }

class AudioFileProcessor : public Instrument
{
	Q_OBJECT
public:
	AudioFileProcessor( InstrumentTrack * _instrument_track );

	void deleteNotePluginData( NotePlayHandle * _n ) override;
	auto prepareConverterMode(AudioResampler::Mode mode) noexcept -> bool override;
	void adoptPreparedConverterMode() noexcept override;
	void discardPreparedConverterMode() noexcept override;

	//! Slide (portamento) notes are meaningless on a raw sample player: opt
	//! out so slide notes play as regular notes (SPEC-slide-notes D-3)
	bool supportsSlideNotes() const override { return false; }

	void saveSettings(QDomDocument& doc, QDomElement& elem) override;
	void loadSettings(const QDomElement& elem) override;

	void loadFile( const QString & _file ) override;

	QString nodeName() const override;

	auto beatLen(NotePlayHandle* note) const -> f_cnt_t override;

	//! The user's release time. Its default is the 3 ms every earlier version used.
	float desiredReleaseTimeMs() const override
	{
		return m_releaseModel.value();
	}

	gui::PluginView* instantiateView( QWidget * _parent ) override;

	Sample const & sample() const { return m_sample; }

	FloatModel & ampModel() { return m_ampModel; }
	FloatModel & startPointModel() { return m_startPointModel; }
	FloatModel & endPointModel() { return m_endPointModel; }
	FloatModel & loopPointModel() { return m_loopPointModel; }
	BoolModel & reverseModel() { return m_reverseModel; }
	IntModel & loopModel() { return m_loopModel; }
	BoolModel & stutterModel() { return m_stutterModel; }
	ComboBoxModel & interpolationModel() { return m_interpolationModel; }
	FloatModel & attackModel() { return m_attackModel; }
	FloatModel & releaseModel() { return m_releaseModel; }
	IntModel & voiceLimitModel() { return m_voiceLimitModel; }

	//! Playback voices in use (stolen ones still fading included). Control-thread / test use.
	Q_INVOKABLE int activeVoices() const;

	//! One publication of the stutter continuation: the frame a note starts from and the direction it plays it in.
	//! The two belong together, so they are published and read as one value (cluster new-p15-260-port).
	struct StutterStart
	{
		f_cnt_t frame = 0;
		bool backwards = false;
	};


public slots:
	void setAudioFile(const QString& _audio_file, bool _rename = true);

private slots:
	void reverseModelChanged();
	void ampModelChanged();
	void loopPointChanged();
	void startPointChanged();
	void endPointChanged();
	void pointChanged();
	void stutterModelChanged();


signals:
	void isPlaying( lmms::f_cnt_t _current_frame );
	void sampleUpdated();

private:
	void playNoteImpl(NotePlayHandle* nph, std::span<SampleFrame> out) override;

	//! Audio-thread handoff for the playhead (cluster p12-315): the render thread
	//! publishes here instead of emitting isPlaying; the GUI thread polls.
	void publishPlayhead(f_cnt_t _current_frame) noexcept;
	//! GUI-thread poll entry point (invoked by the view's timer, and by tests):
	//! emits isPlaying only when the published frame changed since the last poll.
	Q_INVOKABLE void pollPlayhead();
public:
	//! GUI-thread: emit the change signals of the envelope / voice-limit models that an automation tick changed on the
	//! render thread (where they are held back). pollPlayhead() calls it; a test calls it directly.
	Q_INVOKABLE void flushDeferredSignals();
private:
	std::atomic<f_cnt_t> m_playheadFrame{0};
	//! GUI-thread-only last emitted value (coalescing).
	f_cnt_t m_playheadShown{0};

	friend class gui::AudioFileProcessorView;

	//! What a note keeps in its inline storage: the voice it plays on (Q050-103) and the source it
	//! started on (cluster new-p9-552). Replacing the instrument's file must neither switch a
	//! sounding note to the new buffer nor free the old one under it; the note renders from its own
	//! snapshot for its whole life.
	struct NoteVoice
	{
		SamplerVoicePool::Claim claim;
		Sample sample;
		int loopMode = 0;
	};
	static_assert(sizeof(NoteVoice) <= 1024, "NoteVoice must fit NotePlayHandle::PluginDataStorageSize");

	//! Bit 63 of the continuation word carries the playback direction and bits 0-62 carry the frame (a frame count of
	//! 2^62 is beyond any sample). The pair is published and read as ONE word, so a note start can never combine a
	//! frame from one publication with a direction from another, even while other note jobs of this instrument
	//! publish their own position (cluster new-p15-260-port).
	static constexpr std::uint64_t StutterBackwardsBit = std::uint64_t{1} << 63;
	static constexpr std::uint64_t StutterFrameMask = StutterBackwardsBit - 1;

	static auto packStutterStart(StutterStart start) noexcept -> std::uint64_t
	{
		return (static_cast<std::uint64_t>(start.frame) & StutterFrameMask)
			| (start.backwards ? StutterBackwardsBit : 0);
	}

	//! The stutter continuation as one coherent pair: one relaxed load of the shared word.
	auto stutterContinuation() const noexcept -> StutterStart
	{
		const std::uint64_t word = m_stutterContinuation.load(std::memory_order_relaxed);
		return StutterStart{word & StutterFrameMask, (word & StutterBackwardsBit) != 0};
	}

	//! Publish \a start as the stutter continuation: one relaxed store of the shared word.
	void publishStutterContinuation(StutterStart start) noexcept
	{
		m_stutterContinuation.store(packStutterStart(start), std::memory_order_relaxed);
	}

	//! Read the shared continuation once, restarting it at the sample start when stutter is on and it is at the end.
	//! The render path calls this on a note's first period; the returned pair is exactly what the note starts from.
	auto stutterStartForNote(bool stutter) noexcept -> StutterStart
	{
		StutterStart start = stutterContinuation();
		if (stutter && start.frame >= static_cast<std::size_t>(m_sample.endFrame()))
		{
			// Restart playing the note if in stutter mode, not in loop mode, and we're at the end of the sample.
			start.frame = m_sample.startFrame();
			start.backwards = false;
			publishStutterContinuation(start);
		}
		return start;
	}

	//! Test seam: runs where a note start has read the continuation and is about to write its voice state, so a
	//! regression test can publish another continuation exactly there (SamplerStutterContinuationTest). It does
	//! nothing in a build without the test seams.
	void noteStartHook() const;

#ifdef LMMS_TESTING
protected:
	//! Test seams (cluster new-p15-260-port): the function a regression test installs at the note-start point, and the
	//! reads that let it see what a note start consumed, what it published and the voice state it published from.
	std::function<void()> m_noteStartHook;
	auto testStutterContinuation() const noexcept -> StutterStart { return stutterContinuation(); }
	void testPublishStutterContinuation(StutterStart start) noexcept { publishStutterContinuation(start); }
	auto testVoiceState(const NotePlayHandle* nph) const noexcept -> StutterStart;
#endif

private:
	//! Control thread: publish \a next as the instrument's sample. A sounding note keeps the buffer
	//! it captured; the replaced one is retired until no note holds it and only then freed here.
	void replaceSample(Sample next);

	void applyAttack(SampleFrame* dst, f_cnt_t frames, f_cnt_t firstFrame) const;

	Sample m_sample;
	//! The current source buffer's sample rate, mirrored for beatLen() (a live MIDI note may be built
	//! off the render thread while the control thread replaces the buffer).
	std::atomic<int> m_sampleRate{m_sample.sampleRate()};
	//! Buffers replaced while a sounding note still captures them. Only the control thread touches
	//! this list, so the note's last reference is dropped on the audio thread without freeing.
	std::vector<std::shared_ptr<const SampleBuffer>> m_retiredSamples;

	FloatModel m_ampModel;
	FloatModel m_startPointModel;
	FloatModel m_endPointModel;
	FloatModel m_loopPointModel;
	BoolModel m_reverseModel;
	IntModel m_loopModel;
	BoolModel m_stutterModel;
	ComboBoxModel m_interpolationModel;
	//! Appended after the models above on purpose: parameter indices follow construction order, and the
	//! agent surface (plugin.param_*) addresses the earlier ones by index.
	FloatModel m_attackModel;
	FloatModel m_releaseModel;
	IntModel m_voiceLimitModel;

	//! Built once, in the converter mode Sample::play requests, so the render path only claims and releases voices.
	std::unique_ptr<SamplerVoicePool> m_voices;

	//! The stutter continuation: ONE lock-free word (bit 63 the direction, bits 0-62 the frame), written by the
	//! control-thread reset slots and the render thread and read by note starts. Notes of one instrument render on
	//! several engine threads at once, so this must never be a plain field or a lock, and a note start must never
	//! read the frame and the direction as two separate loads (cluster new-p15-260-port).
	std::atomic<std::uint64_t> m_stutterContinuation{0};
} ;

} // namespace lmms

#endif // LMMS_AUDIO_FILE_PROCESSOR_H
