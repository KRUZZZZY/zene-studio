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
#include <memory>


namespace lmms
{

namespace gui { class AudioFileProcessorView; }

class AudioFileProcessor : public Instrument
{
	Q_OBJECT
public:
	AudioFileProcessor( InstrumentTrack * _instrument_track );

	void deleteNotePluginData( NotePlayHandle * _n ) override;

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

	//! What a note keeps in its inline storage: the voice it plays on (Q050-103).
	struct NoteVoice
	{
		SamplerVoicePool::Claim claim;
	};

	void applyAttack(SampleFrame* dst, f_cnt_t frames, f_cnt_t firstFrame) const;

	Sample m_sample;

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

	f_cnt_t m_nextPlayStartPoint;
	bool m_nextPlayBackwards;
} ;

} // namespace lmms

#endif // LMMS_AUDIO_FILE_PROCESSOR_H
