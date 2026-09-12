/*
 * SampleClip.h
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

#ifndef LMMS_SAMPLE_CLIP_H
#define LMMS_SAMPLE_CLIP_H

#include <memory>
#include "Clip.h"
#include "Sample.h"
#include "SampleWindow.h"

namespace lmms
{

class SampleBuffer;

namespace gui
{

class SampleClipView;

} // namespace gui


class SampleClip : public Clip
{
	Q_OBJECT
	mapPropertyFromModel(bool,isRecord,setRecord,m_recordModel);
public:
	SampleClip(Track* track, Sample sample, bool isPlaying);
	SampleClip(Track* track);
	~SampleClip() override;

	SampleClip& operator=( const SampleClip& that ) = delete;

	void changeLength( const TimePos & _length ) override;
	void updateLength() override;

	const QString& sampleFile() const;
	bool hasSampleFileLoaded(const QString & filename) const;

	void saveSettings( QDomDocument & _doc, QDomElement & _parent ) override;
	void loadSettings( const QDomElement & _this ) override;
	inline QString nodeName() const override
	{
		return "sampleclip";
	}

	Sample& sample()
	{
		return m_sample;
	}

	TimePos sampleLength() const;

	/*! The clip's authored window into its source audio (design §2.2).
	 *
	 *  This is the state a trim, a slip or a split authors. The playback path
	 *  reads it and never writes it: a playback pass derives the window it renders
	 *  and hands it to the play handle as a snapshot (I1). */
	SampleWindow sampleWindow() const { return m_window; }

	/*! Authors the window; the single place the well-formedness rule (I4) lives.
	 *
	 *  An edit that would leave the window empty is rejected outright. A window
	 *  that is not the whole buffer is a manual edit, so it also clears
	 *  auto-resize (design §2.6 / OQ-4), which is what keeps it stable across a
	 *  tempo change and a reload. */
	void setSampleWindow(const SampleWindow& window);

	//! The legacy absolute setters: the window's start frame, and its END frame
	//! (`length` is a source frame index, as it always was - not a frame count).
	void setSampleStartFrame( f_cnt_t startFrame );
	void setSamplePlayLength( f_cnt_t length );

	f_cnt_t sourceFrameAt(TimePos timelinePos) const override;
	TimePos timelinePosAt(f_cnt_t sourceFrame) const override;

	void setStartTimeOffset(const TimePos& startTimeOffset) override;
	gui::ClipView * createView( gui::TrackView * _tv ) override;


	bool isPlaying() const;
	void setIsPlaying(bool isPlaying);
	void setSampleBuffer(std::shared_ptr<const SampleBuffer> sb);

	SampleClip* clone() override
	{
		return new SampleClip(*this);
	}

public slots:
	void setSampleFile(const QString& sf);
	void toggleRecord();
	void playbackPositionChanged();
	void updateTrackClips();
	void tempoChanged();

protected:
	SampleClip( const SampleClip& orig );

private:
	Sample m_sample;
	//! The authored window (design §2.2, Slice 0). Written only by this class -
	//! never by the playback path - and mirrored into Sample's render-time frame
	//! fields so drawing and Sample::render see it.
	SampleWindow m_window;
	BoolModel m_recordModel;
	bool m_isPlaying;
	int m_startFrameOffset;

	//! Points the window (and Sample's frame fields) at the whole buffer; called
	//! wherever the source buffer is replaced.
	void resetWindowToFullBuffer();

	friend class gui::SampleClipView;


signals:
	void sampleChanged();
	void wasReversed();
} ;


} // namespace lmms

#endif // LMMS_SAMPLE_CLIP_H
