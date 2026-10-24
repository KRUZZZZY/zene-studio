/*
 * AudioFileProcessor.cpp - instrument for using audio files
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

#include "AudioFileProcessor.h"
#include "AudioFileProcessorView.h"

#include "InstrumentTrack.h"
#include "PathUtil.h"
#include "Song.h"

#include "LmmsTypes.h"
#include "plugin_export.h"

#include <QDomElement>

#include "ExportRenderSettings.h"

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <new>


namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT audiofileprocessor_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"AudioFileProcessor",
	QT_TRANSLATE_NOOP( "PluginBrowser",
				"Simple sampler with various settings for "
				"using samples (e.g. drums) in an "
				"instrument-track" ),
	"Tobias Doerffel <tobydox/at/users.sf.net>",
	0x0100,
	Plugin::Type::Instrument,
	new PluginPixmapLoader( "logo" ),
	"wav,ogg,ds,spx,au,voc,aif,aiff,flac,raw"
#ifdef LMMS_HAVE_SNDFILE_MP3
	",mp3"
#endif
	,
	nullptr,
} ;

}




AudioFileProcessor::AudioFileProcessor( InstrumentTrack * _instrument_track ) :
	Instrument( _instrument_track, &audiofileprocessor_plugin_descriptor ),
	m_ampModel( 100, 0, 500, 1, this, tr( "Amplify" ) ),
	m_startPointModel( 0, 0, 1, 0.0000001f, this, tr( "Start of sample" ) ),
	m_endPointModel( 1, 0, 1, 0.0000001f, this, tr( "End of sample" ) ),
	m_loopPointModel( 0, 0, 1, 0.0000001f, this, tr( "Loopback point" ) ),
	m_reverseModel( false, this, tr( "Reverse sample" ) ),
	m_loopModel( 0, 0, 2, this, tr( "Loop mode" ) ),
	m_stutterModel( false, this, tr( "Stutter" ) ),
	m_interpolationModel( this, tr( "Interpolation mode" ) ),
	// The release default is the 3 ms the sampler always applied; attack 0 changes nothing.
	m_attackModel( 0.f, 0.f, 2000.f, 0.1f, this, tr( "Attack" ) ),
	m_releaseModel( 3.f, 0.f, 5000.f, 0.1f, this, tr( "Release" ) ),
	m_voiceLimitModel( SamplerVoicePool::MaxVoices, 1, SamplerVoicePool::MaxVoices, this, tr( "Voice limit" ) ),
	// Built for the converter Sample::play asks for, so a note start never re-creates a converter state.
	m_voices( std::make_unique<SamplerVoicePool>(
		AudioResampler::modeForSrcQuality( ExportRenderSettings::srcQuality() ) ) ),
	m_nextPlayStartPoint( 0 ),
	m_nextPlayBackwards( false )
{
	connect( &m_reverseModel, SIGNAL( dataChanged() ),
				this, SLOT( reverseModelChanged() ), Qt::DirectConnection );
	connect( &m_ampModel, SIGNAL( dataChanged() ),
				this, SLOT( ampModelChanged() ), Qt::DirectConnection );
	connect( &m_startPointModel, SIGNAL( dataChanged() ),
				this, SLOT( startPointChanged() ), Qt::DirectConnection );
	connect( &m_endPointModel, SIGNAL( dataChanged() ),
				this, SLOT( endPointChanged() ), Qt::DirectConnection );
	connect( &m_loopPointModel, SIGNAL( dataChanged() ),
				this, SLOT( loopPointChanged() ), Qt::DirectConnection );
	connect( &m_stutterModel, SIGNAL( dataChanged() ),
				this, SLOT( stutterModelChanged() ), Qt::DirectConnection );

//interpolation modes
	m_interpolationModel.addItem( tr( "None" ) );
	m_interpolationModel.addItem( tr( "Linear" ) );
	m_interpolationModel.addItem( tr( "Sinc" ) );
	m_interpolationModel.setValue( 1 );

	// The three envelope / voice models are automatable: an automation tick changes them on the render thread, where no
	// signal may be emitted. They hold the change signal back there; the window's poll timer (pollPlayhead) emits it so the
	// knobs follow the automation. Nothing the render path reads depends on the signal - it reads the values.
	for (AutomatableModel* model : std::initializer_list<AutomatableModel*>{&m_attackModel, &m_releaseModel, &m_voiceLimitModel})
	{
		model->setDeferChangeSignalOnRenderThread();
	}

	pointChanged();
}




int AudioFileProcessor::activeVoices() const
{
	return m_voices->activeVoices();
}




void AudioFileProcessor::applyAttack(SampleFrame* dst, f_cnt_t frames, f_cnt_t firstFrame) const
{
	const float attackMs = m_attackModel.value();
	if (attackMs <= 0.f) { return; }

	const f_cnt_t attackFrames = std::max<f_cnt_t>(1, static_cast<f_cnt_t>(std::ceil(attackMs * getSampleRate() / 1000.f)));
	if (firstFrame >= attackFrames) { return; }

	const f_cnt_t last = std::min(frames, attackFrames - firstFrame);
	for (f_cnt_t f = 0; f < last; ++f)
	{
		const float gain = static_cast<float>(firstFrame + f) / static_cast<float>(attackFrames);
		for (ch_cnt_t ch = 0; ch < DEFAULT_CHANNELS; ++ch) { dst[f][ch] *= gain; }
	}
}




void AudioFileProcessor::playNoteImpl(NotePlayHandle* nph, std::span<SampleFrame> out)
{
	const f_cnt_t frames = nph->framesLeftForCurrentPeriod();
	const f_cnt_t offset = nph->noteOffset();

	// Magic key - a frequency < 20 (say, the bottom piano note if using
	// a A4 base tuning) restarts the start point. The note is not actually
	// played.
	if( m_stutterModel.value() == true && nph->frequency() < 20.0 )
	{
		m_nextPlayStartPoint = m_sample.startFrame();
		m_nextPlayBackwards = false;
		return;
	}

	SamplerVoicePool* const pool = m_voices.get();

	auto* handle = static_cast<NoteVoice*>(nph->m_pluginData);
	if( !handle )
	{
		// First period of this note: take a voice (a free one, or the reuse policy's) - no allocation.
		handle = std::construct_at(static_cast<NoteVoice*>(nph->pluginDataStorage()));
		nph->m_pluginData = handle;
		handle->claim = pool->acquire(m_voiceLimitModel.value());

		if (m_stutterModel.value() == true && m_nextPlayStartPoint >= static_cast<std::size_t>(m_sample.endFrame()))
		{
			// Restart playing the note if in stutter mode, not in loop mode,
			// and we're at the end of the sample.
			m_nextPlayStartPoint = m_sample.startFrame();
			m_nextPlayBackwards = false;
		}
		if (handle->claim.valid())
		{
			auto& state = pool->voice(handle->claim.index).state;
			state.setFrameIndex(m_nextPlayStartPoint);
			state.setBackwards(m_nextPlayBackwards);
		}
	}

	// The note's voice is gone: the reuse policy took it (and the fade-out finished), or every voice was taken when the
	// note started. Either way the note is silent from here.
	if (!pool->owns(handle->claim))
	{
		zeroSampleFrames(out.data(), frames + offset);
		publishPlayhead( 0 );
		return;
	}

	auto& voice = pool->voice(handle->claim.index);
	if (nph->isReleased()) { pool->markReleased(handle->claim); }

	if( ! nph->isFinished() )
	{
		if (m_sample.play(out.data() + offset, &voice.state,
						frames, static_cast<Sample::Loop>(m_loopModel.value()),
						DefaultBaseFreq / nph->frequency()))
		{
			applyAttack(out.data() + offset, frames, nph->totalFramesPlayed());
			applyRelease(out.data(), nph);
			publishPlayhead(voice.state.frameIndex());
		}
		else
		{
			zeroSampleFrames(out.data(), frames + offset);
			publishPlayhead( 0 );
		}
	}
	else
	{
		publishPlayhead( 0 );
	}

	if (pool->isStolen(handle->claim))
	{
		// The reuse policy chose this note's voice: fade the note out over a few milliseconds, then hand the voice on.
		const f_cnt_t fadeFrames = std::max<f_cnt_t>(1,
			static_cast<f_cnt_t>(std::ceil(SamplerVoicePool::StealFadeMs * getSampleRate() / 1000.f)));
		f_cnt_t done = voice.stealFramesDone;
		SampleFrame* dst = out.data() + offset;
		for (f_cnt_t f = 0; f < frames; ++f, ++done)
		{
			const float gain = done >= fadeFrames ? 0.f : 1.f - static_cast<float>(done) / static_cast<float>(fadeFrames);
			for (ch_cnt_t ch = 0; ch < DEFAULT_CHANNELS; ++ch) { dst[f][ch] *= gain; }
		}
		voice.stealFramesDone = done;
		if (done >= fadeFrames)
		{
			pool->free(handle->claim);
			return;
		}
	}

	if( m_stutterModel.value() == true )
	{
		m_nextPlayStartPoint = voice.state.frameIndex();
		m_nextPlayBackwards = voice.state.backwards();
	}
}




void AudioFileProcessor::flushDeferredSignals()
{
	m_attackModel.flushDeferredChange();
	m_releaseModel.flushDeferredChange();
	m_voiceLimitModel.flushDeferredChange();
}




void AudioFileProcessor::publishPlayhead( f_cnt_t _current_frame ) noexcept
{
	m_playheadFrame.store( _current_frame, std::memory_order_relaxed );
}

void AudioFileProcessor::pollPlayhead()
{
	flushDeferredSignals();
	const f_cnt_t current = m_playheadFrame.load( std::memory_order_relaxed );
	if( current == m_playheadShown ) { return; }
	m_playheadShown = current;
	emit isPlaying( current );
}


void AudioFileProcessor::deleteNotePluginData( NotePlayHandle * _n )
{
	auto* handle = static_cast<NoteVoice*>(_n->m_pluginData);
	m_voices->free(handle->claim);
	std::destroy_at(handle);
}

auto AudioFileProcessor::prepareConverterMode(AudioResampler::Mode mode) noexcept -> bool
{
	return m_voices->prepareConverterMode(mode);
}

void AudioFileProcessor::adoptPreparedConverterMode() noexcept
{
	m_voices->adoptPreparedConverterMode();
}

void AudioFileProcessor::discardPreparedConverterMode() noexcept
{
	m_voices->discardPreparedConverterMode();
}




void AudioFileProcessor::saveSettings(QDomDocument& doc, QDomElement& elem)
{
	elem.setAttribute("src", m_sample.sampleFile());
	if (m_sample.sampleFile().isEmpty())
	{
		elem.setAttribute("sampledata", m_sample.toBase64());
	}
	m_reverseModel.saveSettings(doc, elem, "reversed");
	m_loopModel.saveSettings(doc, elem, "looped");
	m_ampModel.saveSettings(doc, elem, "amp");
	m_startPointModel.saveSettings(doc, elem, "sframe");
	m_endPointModel.saveSettings(doc, elem, "eframe");
	m_loopPointModel.saveSettings(doc, elem, "lframe");
	m_stutterModel.saveSettings(doc, elem, "stutter");
	m_interpolationModel.saveSettings(doc, elem, "interp");
	m_attackModel.saveSettings(doc, elem, "atk");
	m_releaseModel.saveSettings(doc, elem, "rel");
	m_voiceLimitModel.saveSettings(doc, elem, "voices");
}




void AudioFileProcessor::loadSettings(const QDomElement& elem)
{
	if (auto srcFile = elem.attribute("src"); !srcFile.isEmpty())
	{
		if (QFileInfo(PathUtil::toAbsolute(srcFile)).exists())
		{
			setAudioFile(srcFile, false);
		}
		else { Engine::getSong()->collectError(QString("%1: %2").arg(tr("Sample not found"), srcFile)); }
	}
	else if (auto sampleData = elem.attribute("sampledata"); !sampleData.isEmpty())
	{
		m_sample = Sample(SampleBuffer::fromBase64(sampleData));
	}

	m_loopModel.loadSettings(elem, "looped");
	m_ampModel.loadSettings(elem, "amp");
	m_endPointModel.loadSettings(elem, "eframe");
	m_startPointModel.loadSettings(elem, "sframe");

	// compat code for not having a separate loopback point
	if (elem.hasAttribute("lframe") || !elem.firstChildElement("lframe").isNull())
	{
		m_loopPointModel.loadSettings(elem, "lframe");
	}
	else
	{
		m_loopPointModel.loadSettings(elem, "sframe");
	}

	m_reverseModel.loadSettings(elem, "reversed");

	m_stutterModel.loadSettings(elem, "stutter");
	if (elem.hasAttribute("interp") || !elem.firstChildElement("interp").isNull())
	{
		m_interpolationModel.loadSettings(elem, "interp");
	}
	else
	{
		m_interpolationModel.setValue(1.0f); // linear by default
	}

	// Projects from before Q050-103 carry none of these: a missing one loads its default (attack 0, release 3 ms,
	// the full voice limit), which renders the sampler exactly as those projects always did.
	m_attackModel.loadSettings(elem, "atk");
	m_releaseModel.loadSettings(elem, "rel");
	m_voiceLimitModel.loadSettings(elem, "voices");

	pointChanged();
	emit sampleUpdated();
}




void AudioFileProcessor::loadFile( const QString & _file )
{
	setAudioFile( _file );
}




QString AudioFileProcessor::nodeName() const
{
	return audiofileprocessor_plugin_descriptor.name;
}




auto AudioFileProcessor::beatLen(NotePlayHandle* note) const -> f_cnt_t
{
	// If we can play indefinitely, use the default beat note duration
	if (static_cast<Sample::Loop>(m_loopModel.value()) != Sample::Loop::Off) { return 0; }

	// Otherwise, use the remaining sample duration
	const auto baseFreq = instrumentTrack()->baseFreq();
	const auto freqFactor = baseFreq / note->frequency()
		* Engine::audioEngine()->outputSampleRate()
		/ Engine::audioEngine()->baseSampleRate();
	const auto sampleRateRatio = static_cast<double>(Engine::audioEngine()->outputSampleRate()) / m_sample.sampleRate();

	const auto startFrame = m_nextPlayStartPoint >= static_cast<std::size_t>(m_sample.endFrame())
		? m_sample.startFrame()
		: m_nextPlayStartPoint;
	const auto duration = m_sample.endFrame() - startFrame;

	return static_cast<f_cnt_t>(std::floor(duration * freqFactor * sampleRateRatio));
}




gui::PluginView* AudioFileProcessor::instantiateView( QWidget * _parent )
{
	return new gui::AudioFileProcessorView( this, _parent );
}

void AudioFileProcessor::setAudioFile(const QString& _audio_file, bool _rename)
{
	// is current channel-name equal to previous-filename??
	if( _rename &&
		( instrumentTrack()->name() ==
			QFileInfo(m_sample.sampleFile()).fileName() ||
				m_sample.sampleFile().isEmpty()))
	{
		// then set it to new one
		instrumentTrack()->setName( PathUtil::cleanName( _audio_file ) );
	}
	// else we don't touch the track-name, because the user named it self

	m_sample = Sample(SampleBuffer::fromFile(_audio_file));
	loopPointChanged();
	ampModelChanged();
	reverseModelChanged();
	emit sampleUpdated();
}




void AudioFileProcessor::reverseModelChanged()
{
	m_sample.setReversed(m_reverseModel.value());
	m_nextPlayStartPoint = m_sample.startFrame();
	m_nextPlayBackwards = false;
	emit sampleUpdated();
}




void AudioFileProcessor::ampModelChanged()
{
	m_sample.setAmplification(m_ampModel.value() / 100.0f);
	emit sampleUpdated();
}


void AudioFileProcessor::stutterModelChanged()
{
	m_nextPlayStartPoint = m_sample.startFrame();
	m_nextPlayBackwards = false;
}


void AudioFileProcessor::startPointChanged()
{
	// check if start is over end and swap values if so
	if( m_startPointModel.value() > m_endPointModel.value() )
	{
		float tmp = m_endPointModel.value();
		m_endPointModel.setValue( m_startPointModel.value() );
		m_startPointModel.setValue( tmp );
	}

	// nudge loop point with end
	if( m_loopPointModel.value() >= m_endPointModel.value() )
	{
		m_loopPointModel.setValue( qMax( m_endPointModel.value() - 0.001f, 0.0f ) );
	}

	// nudge loop point with start
	if( m_loopPointModel.value() < m_startPointModel.value() )
	{
		m_loopPointModel.setValue( m_startPointModel.value() );
	}

	// check if start & end overlap and nudge end up if so
	if( m_startPointModel.value() == m_endPointModel.value() )
	{
		m_endPointModel.setValue( qMin( m_endPointModel.value() + 0.001f, 1.0f ) );
	}

	pointChanged();

}

void AudioFileProcessor::endPointChanged()
{
	// same as start, for now
	startPointChanged();

}

void AudioFileProcessor::loopPointChanged()
{

	// check that loop point is between start-end points and not overlapping with endpoint
	// ...and move start/end points ahead if loop point is moved over them
	if( m_loopPointModel.value() >= m_endPointModel.value() )
	{
		m_endPointModel.setValue( m_loopPointModel.value() + 0.001f );
		if( m_endPointModel.value() == 1.0f )
		{
			m_loopPointModel.setValue( 1.0f - 0.001f );
		}
	}

	// nudge start point with loop
	if( m_loopPointModel.value() < m_startPointModel.value() )
	{
		m_startPointModel.setValue( m_loopPointModel.value() );
	}

	pointChanged();
}

void AudioFileProcessor::pointChanged()
{
	const auto f_start = static_cast<f_cnt_t>(m_startPointModel.value() * m_sample.sampleSize());
	const auto f_end = static_cast<f_cnt_t>(m_endPointModel.value() * m_sample.sampleSize());
	const auto f_loop = static_cast<f_cnt_t>(m_loopPointModel.value() * m_sample.sampleSize());

	m_nextPlayStartPoint = f_start;
	m_nextPlayBackwards = false;

	m_sample.setAllPointFrames(f_start, f_end, f_loop, f_end);
	emit dataChanged();
}


extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main(Model * model, void *)
{
	return new AudioFileProcessor(static_cast<InstrumentTrack *>(model));
}


}


} // namespace lmms
