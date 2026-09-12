/*
 * SampleClip.cpp
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
 
#include "SampleClip.h"

#include <QDomElement>
#include <QFileInfo>

#include "PatternStore.h"
#include "PathUtil.h"
#include "SampleClipView.h"
#include "SampleTrack.h"
#include "Song.h"

namespace lmms
{

SampleClip::SampleClip(Track* _track, Sample sample, bool isPlaying):
	Clip(_track),
	m_sample(std::move(sample)),
	m_window(SampleWindow::full(m_sample.sampleSize())),
	m_isPlaying(false),
	m_startFrameOffset(0)
{
	saveJournallingState( false );
	setSampleFile( "" );
	restoreJournallingState();

	// we need to receive bpm-change-events, because then we have to
	// change length of this Clip
	connect(Engine::getSong(), &Song::tempoChanged, this, &SampleClip::tempoChanged, Qt::DirectConnection);
	connect(Engine::getSong(), &Song::timeSignatureChanged, this, &SampleClip::updateLength);

	//playbutton clicked or space key / on Export Song set isPlaying to false
	connect( Engine::getSong(), SIGNAL(playbackStateChanged()),
			this, SLOT(playbackPositionChanged()), Qt::DirectConnection );
	//care about loops and jumps
	connect(Engine::getSong(), &Song::playbackPositionJumped,
			this, &SampleClip::playbackPositionChanged, Qt::DirectConnection);
	//care about mute Clips
	connect( this, SIGNAL(dataChanged()), this, SLOT(playbackPositionChanged()));
	//care about mute track
	connect( getTrack()->getMutedModel(), SIGNAL(dataChanged()),
			this, SLOT(playbackPositionChanged()), Qt::DirectConnection );
	//care about Clip position
	connect( this, SIGNAL(positionChanged()), this, SLOT(updateTrackClips()));

	updateTrackClips();
}

SampleClip::SampleClip(Track* track)
	: SampleClip(track, Sample(), false)
{
}

SampleClip::SampleClip(const SampleClip& orig) :
	Clip(orig),
	m_sample(std::move(orig.m_sample)),
	m_window(orig.m_window),
	m_isPlaying(orig.m_isPlaying),
	m_startFrameOffset(orig.m_startFrameOffset)
{
	saveJournallingState( false );
	setSampleFile( "" );
	restoreJournallingState();

	// we need to receive bpm-change-events, because then we have to
	// change length of this Clip
	connect(Engine::getSong(), &Song::tempoChanged, this, &SampleClip::tempoChanged, Qt::DirectConnection);
	connect( Engine::getSong(), SIGNAL(timeSignatureChanged(int,int)),
					this, SLOT(updateLength()));

	//playbutton clicked or space key / on Export Song set isPlaying to false
	connect( Engine::getSong(), SIGNAL(playbackStateChanged()),
			this, SLOT(playbackPositionChanged()), Qt::DirectConnection );
	//care about loops and jumps
	connect(Engine::getSong(), &Song::playbackPositionJumped,
			this, &SampleClip::playbackPositionChanged, Qt::DirectConnection);
	//care about mute Clips
	connect( this, SIGNAL(dataChanged()), this, SLOT(playbackPositionChanged()));
	//care about mute track
	connect( getTrack()->getMutedModel(), SIGNAL(dataChanged()),
			this, SLOT(playbackPositionChanged()), Qt::DirectConnection );
	//care about Clip position
	connect( this, SIGNAL(positionChanged()), this, SLOT(updateTrackClips()));

	updateTrackClips();
}




SampleClip::~SampleClip()
{
	auto sampletrack = dynamic_cast<SampleTrack*>(getTrack());
	if ( sampletrack )
	{
		sampletrack->updateClips();
	}
}




void SampleClip::changeLength( const TimePos & _length )
{
	Clip::changeLength(std::max(static_cast<int>(_length), 1));
}



const QString& SampleClip::sampleFile() const
{
	return m_sample.sampleFile();
}

bool SampleClip::hasSampleFileLoaded(const QString & filename) const
{
	return m_sample.sampleFile() == filename;
}

void SampleClip::setSampleBuffer(std::shared_ptr<const SampleBuffer> sb)
{
	{
		const auto guard = Engine::audioEngine()->requestChangesGuard();
		m_sample = Sample(std::move(sb));
		// a new source means a new window: the whole buffer (Slice 0)
		resetWindowToFullBuffer();
	}
	updateLength();

	emit sampleChanged();

	Engine::getSong()->setModified();
}

void SampleClip::setSampleFile(const QString& sf)
{
	// Remove any prior offset in the clip
	setStartTimeOffset(0);
	if (!sf.isEmpty())
	{
		m_sample = Sample(SampleBuffer::fromFile(sf));
		resetWindowToFullBuffer();
		updateLength();
	}
	else
	{
		// If there is no sample, make the clip a bar long
		float nom = Engine::getSong()->getTimeSigModel().getNumerator();
		float den = Engine::getSong()->getTimeSigModel().getDenominator();
		changeLength(DefaultTicksPerBar * (nom / den));
	}

	emit sampleChanged();
	emit playbackPositionChanged();
}




void SampleClip::toggleRecord()
{
	m_recordModel.setValue( !m_recordModel.value() );
	emit dataChanged();
}




void SampleClip::playbackPositionChanged()
{
	Engine::audioEngine()->removePlayHandlesOfTypes( getTrack(), PlayHandle::Type::SamplePlayHandle );
	auto st = dynamic_cast<SampleTrack*>(getTrack());
	st->setPlayingClips( false );
}




void SampleClip::updateTrackClips()
{
	auto sampletrack = dynamic_cast<SampleTrack*>(getTrack());
	if( sampletrack)
	{
		sampletrack->updateClips();
	}
}




bool SampleClip::isPlaying() const
{
	return m_isPlaying;
}




void SampleClip::setIsPlaying(bool isPlaying)
{
	m_isPlaying = isPlaying;
}




void SampleClip::updateLength()
{
	// If the clip has already been manually resized, don't automatically resize it.
	if (getAutoResize())
	{
		if (getTrack()->trackContainer() == Engine::patternStore())
		{
			changeLength(TimePos::ticksPerBar() * Engine::patternStore()->lengthOfPattern(getTrack()->getClipNum(this)));
			return;
		}
		changeLength(sampleLength());
		setStartTimeOffset(0);
	}

	emit sampleChanged();
}


void SampleClip::tempoChanged()
{
	Clip::setStartTimeOffset(std::round(1.0f * m_startFrameOffset / Engine::framesPerTick()));
	updateLength();
	emit sampleChanged();
}

void SampleClip::setStartTimeOffset(const TimePos& startTimeOffset)
{
	m_startFrameOffset = startTimeOffset * Engine::framesPerTick();
	Clip::setStartTimeOffset(startTimeOffset);
}


TimePos SampleClip::sampleLength() const
{
	// The clip's audio length is the length of its window, not of the whole file:
	// for a clip without a trim the two are the same, which is what keeps this
	// behaviour-preserving (Slice 0).
	return static_cast<int>(m_window.length() / Engine::framesPerTick(m_sample.sampleRate()));
}




void SampleClip::setSampleStartFrame(f_cnt_t startFrame)
{
	// The legacy absolute setter for the window's start. Nothing on the playback
	// path calls it any more: the window is authored state (Slice 0).
	setSampleWindow({ startFrame, m_window.sourceOut });
}




void SampleClip::setSamplePlayLength(f_cnt_t length)
{
	// The legacy absolute setter for the window's END frame, as it always was.
	setSampleWindow({ m_window.sourceIn, length });
}




void SampleClip::setSampleWindow(const SampleWindow& window)
{
	const auto bufferFrames = static_cast<f_cnt_t>(m_sample.sampleSize());
	const auto clamped = SampleWindow::clamped(window.sourceIn, window.sourceOut, bufferFrames);
	if (clamped.empty() || clamped == m_window)
	{
		// I4: a window that is not well formed is a rejected edit, not a clamped
		// one - and an edit that changes nothing is not an edit.
		return;
	}

	m_window = clamped;

	// Sample's frame fields are the render-time scratch they already were (OQ-1):
	// pointing them at the window keeps Sample::render, sampleDuration() and the
	// waveform drawing reading the window, while the authored state stays here.
	m_sample.setStartFrame(static_cast<int>(m_window.sourceIn));
	m_sample.setEndFrame(static_cast<int>(m_window.sourceOut));

	if (m_window != SampleWindow::full(bufferFrames))
	{
		// A window that is not the whole buffer is a manual edit, so it must
		// survive a tempo change and a reload (design §2.6, OQ-4).
		setAutoResize(false);
	}

	Engine::getSong()->setModified();
	emit sampleChanged();
}




void SampleClip::resetWindowToFullBuffer()
{
	m_window = SampleWindow::full(static_cast<f_cnt_t>(m_sample.sampleSize()));
	m_sample.setStartFrame(static_cast<int>(m_window.sourceIn));
	m_sample.setEndFrame(static_cast<int>(m_window.sourceOut));
}




f_cnt_t SampleClip::sourceFrameAt(TimePos timelinePos) const
{
	// The one linear mapping of the window onto the timeline (design §2.4). No
	// allocation, no lock: this runs on the audio thread (I8).
	const auto framesPerTick = Engine::framesPerTick(m_sample.sampleRate());
	const auto relativeTicks = timelinePos.getTicks() - startPosition().getTicks()
		- startTimeOffset().getTicks();
	const auto relative = relativeTicks > 0
		? TimePos(relativeTicks).frames(framesPerTick)
		: f_cnt_t{ 0 };
	return std::clamp(m_window.sourceIn + relative, m_window.sourceIn, m_window.sourceOut);
}




TimePos SampleClip::timelinePosAt(f_cnt_t sourceFrame) const
{
	const auto framesPerTick = Engine::framesPerTick(m_sample.sampleRate());
	const auto frame = std::clamp(sourceFrame, m_window.sourceIn, m_window.sourceOut);
	return TimePos(startPosition().getTicks() + startTimeOffset().getTicks()
		+ static_cast<int>(TimePos::fromFrames(frame - m_window.sourceIn, framesPerTick)));
}




void SampleClip::saveSettings( QDomDocument & _doc, QDomElement & _this )
{
	if( _this.parentNode().nodeName() == "clipboard" )
	{
		_this.setAttribute( "pos", -1 );
	}
	else
	{
		_this.setAttribute( "pos", startPosition() );
	}
	_this.setAttribute( "len", length() );
	_this.setAttribute( "muted", isMuted() );
	_this.setAttribute( "src", sampleFile() );
	_this.setAttribute( "off", startTimeOffset() );
	_this.setAttribute("autoresize", QString::number(getAutoResize()));
	if( sampleFile() == "" )
	{
		QString s;
		_this.setAttribute("data", m_sample.toBase64());
	}

	_this.setAttribute( "sample_rate", m_sample.sampleRate());
	// The authored window, in source frames (Slice 1 of task #611). Additive: a
	// clip that plays its whole source writes neither attribute, so a project
	// without a trim serialises exactly as it did before this slice (I9).
	if (m_window != SampleWindow::full(static_cast<f_cnt_t>(m_sample.sampleSize())))
	{
		_this.setAttribute( "srcin", QString::number(m_window.sourceIn) );
		_this.setAttribute( "srcout", QString::number(m_window.sourceOut) );
	}
	if (const auto& c = color())
	{
		_this.setAttribute("color", c->name());
	}
	if (m_sample.reversed())
	{
		_this.setAttribute("reversed", "true");
	}
}




void SampleClip::loadSettings( const QDomElement & _this )
{
	if( _this.attribute( "pos" ).toInt() >= 0 )
	{
		movePosition( _this.attribute( "pos" ).toInt() );
	}

	if (const auto srcFile = _this.attribute("src"); !srcFile.isEmpty())
	{
		if (QFileInfo(PathUtil::toAbsolute(srcFile)).exists())
		{
			setSampleFile(srcFile);
		}
		else { Engine::getSong()->collectError(QString("%1: %2").arg(tr("Sample not found"), srcFile)); }
	}

	if( sampleFile().isEmpty() && _this.hasAttribute( "data" ) )
	{
		auto sampleRate = _this.hasAttribute("sample_rate") ? _this.attribute("sample_rate").toInt() :
			Engine::audioEngine()->outputSampleRate();

		auto buffer = SampleBuffer::fromBase64(_this.attribute("data"), sampleRate);
		m_sample = Sample(std::move(buffer));
		resetWindowToFullBuffer();
	}
	changeLength( _this.attribute( "len" ).toInt() );
	setMuted( _this.attribute( "muted" ).toInt() );
	setStartTimeOffset( _this.attribute( "off" ).toInt() );

	// The authored window (Slice 1 of task #611). Applied after `len` and `off` -
	// which stay authoritative for a file that carries a window, exactly as the
	// design asks (§2.6) - and before `autoresize`, so the file's own value decides
	// that flag rather than the "this was a manual edit" rule in setSampleWindow().
	if (_this.hasAttribute("srcin") || _this.hasAttribute("srcout"))
	{
		const auto srcOut = _this.attribute("srcout");
		setSampleWindow({ _this.attribute("srcin", "0").toULongLong(),
			srcOut.isEmpty() ? static_cast<qulonglong>(m_sample.sampleSize())
				: srcOut.toULongLong() });
	}

	setAutoResize(_this.attribute("autoresize", "1").toInt());

	if (_this.hasAttribute("color"))
	{
		setColor(QColor{_this.attribute("color")});
	}

	if(_this.hasAttribute("reversed"))
	{
		m_sample.setReversed(true);
		emit wasReversed(); // tell SampleClipView to update the view
	}
}




gui::ClipView * SampleClip::createView( gui::TrackView * _tv )
{
	return new gui::SampleClipView( this, _tv );
}


} // namespace lmms
