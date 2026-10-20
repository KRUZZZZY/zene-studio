/*
 * SampleTrack.h - class SampleTrack, a track which provides arrangement of samples
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

#ifndef LMMS_SAMPLE_TRACK_H
#define LMMS_SAMPLE_TRACK_H

#include <atomic>
#include <cstdint>
#include "AudioBusHandle.h"
#include "Track.h"


namespace lmms
{

namespace gui
{

class SampleTrackView;
class SampleTrackWindow;

} // namespace gui


class InputMonitorHandle;

class SampleTrack : public Track
{
	Q_OBJECT
public:
	//! Main thread: while set, updateClips() does nothing. Lane takes are built under it: their
	//! construction must not cut the live takes of the next pass (BUGS P6, round 4).
	void setClipUpdatesSuspended( bool suspended ) noexcept { m_clipUpdatesSuspended.store( suspended, std::memory_order_relaxed ); }
	//! True while clip updates are suspended (see setClipUpdatesSuspended). Read on the render thread too.
	bool clipUpdatesSuspended() const noexcept { return m_clipUpdatesSuspended.load( std::memory_order_relaxed ); }

	//! Clip starts the per-pass cap deferred to a later pass, process-wide (see play()).
	static std::uint64_t deferredClipStarts() noexcept;
	SampleTrack( TrackContainer* tc );
	~SampleTrack() override;

	bool play( const TimePos & _start, const f_cnt_t _frames,
						const f_cnt_t _frame_base, int _clip_num = -1 ) override;
	gui::TrackView * createView( gui::TrackContainerView* tcv ) override;
	Clip* createClip(const TimePos & pos) override;


	void saveTrackSpecificSettings(QDomDocument& doc, QDomElement& parent, bool presetMode) override;
	void loadTrackSpecificSettings( const QDomElement & _this ) override;

	inline IntModel * mixerChannelModel() override
	{
		return &m_mixerChannelModel;
	}

	inline AudioBusHandle* audioBusHandle()
	{
		return &m_audioBusHandle;
	}

	//! The track's volume and panning, for the 0.5.0 Inspector's knobs.
	FloatModel* volumeModel() { return &m_volumeModel; }
	FloatModel* panningModel() { return &m_panningModel; }

	QString nodeName() const override
	{
		return "sampletrack";
	}

	bool isPlaying() const noexcept
	{
		return m_isPlaying.load(std::memory_order_relaxed);
	}

	void setPlaying(bool playing) noexcept
	{
		m_isPlaying.store(playing, std::memory_order_relaxed);
	}

	//! R2.1: the monitor handle while the mode is not Off, else nullptr. Model thread.
	const InputMonitorHandle* monitorHandle() const { return m_monitorHandle; }

signals:
	void playingChanged();

public slots:
	//! Drops the track's sample play handles and clears the clips' playing latches, so clips are
	//! scheduled again. Does nothing while clip updates are suspended.
	void updateClips();
	void setPlayingClips( bool isPlaying );
	void updateMixerChannel();

protected:
	void monitorModeChanged() override;

private:
	std::atomic<bool> m_clipUpdatesSuspended{false};
	std::atomic<bool> m_isPlaying{false};
	bool m_lastNotifiedPlaying = false; // GUI thread: compared by the periodic playing-state poll.
	InputMonitorHandle* m_monitorHandle = nullptr;
	FloatModel m_volumeModel;
	FloatModel m_panningModel;
	IntModel m_mixerChannelModel;
	AudioBusHandle m_audioBusHandle;
	friend class gui::SampleTrackView;
	friend class gui::SampleTrackWindow;

private slots:
	//! Emit the GUI-facing state change from SampleTrackView's main-thread periodic poll.
	void notifyPlayingChanged();

} ;


} // namespace lmms

#endif // LMMS_SAMPLE_TRACK_H
