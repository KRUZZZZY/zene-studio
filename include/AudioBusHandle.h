/*
 * AudioBusHandle.h - ThreadableJob between PlayHandle and MixerChannel
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2025 Johannes Lorenz <jlsf2013$users.sourceforge.net, $=@>
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

#ifndef LMMS_AUDIO_BUS_HANDLE_H
#define LMMS_AUDIO_BUS_HANDLE_H

#include <atomic>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include <QString>

#include "AudioBus.h"
#include "LatencyCompensation.h"
#include "MpscQueue.h"
#include "PlayHandle.h"

namespace lmms
{

class EffectChain;
class FloatModel;
class BoolModel;
class AudioEngine;

/**
	@brief Job between @ref PlayHandle and @ref MixerChannel

	A @ref ThreadableJob class located at the exit point of each @ref PlayHandle into a @ref MixerChannel
	(or into an audio device, in case of @ref AudioJack, but this is not supported in AudioBusHandle yet).
	It contains an optional @ref EffectChain which is e.g. visualized in the
	@ref InstrumentTrackWindow or @ref SampleTrackWindow.
	For processing, it adds all input play handles into an internal buffer,
	processes the @ref EffectChain (if existing) on that buffer
	and finally merges the buffer into its @ref MixerChannel.
*/
class AudioBusHandle : public ThreadableJob
{
public:
	AudioBusHandle(const QString& name, bool hasEffectChain = true,
		FloatModel* volumeModel = nullptr, FloatModel* panningModel = nullptr,
		BoolModel* mutedModel = nullptr);
	virtual ~AudioBusHandle();

	// indicate whether JACK & Co should provide output-buffer at ext. port
	bool extOutputEnabled() const { return m_extOutputEnabled; }
	void setExtOutputEnabled(bool enabled);

	// next mixer-channel after this audio-bus-handle
	// (-1 = none  0 = master)
	mix_ch_t nextMixerChannel() const { return m_nextMixerChannel; }
	void setNextMixerChannel(const mix_ch_t chnl) { m_nextMixerChannel = chnl; }

	const QString& name() const { return m_name; }
	void setName(const QString& newName);

	EffectChain* effects() { return m_effects.get(); }
	bool processEffects();

	// ThreadableJob stuff
	void doProcessing() override;
	bool requiresProcessing() const override { return true; }

	//! Any thread. The render owner applies directly, worker edits use fixed buffers, and other off-stage edits use the queue (cluster 5467).
	void addPlayHandle(PlayHandle* handle);
	void removePlayHandle(PlayHandle* handle);
	std::uint64_t droppedPlayHandleEdits() const noexcept
	{
		return m_droppedPlayHandleEdits.load(std::memory_order_relaxed);
	}
	std::uint64_t capacityDeferredPlayHandleEdits() const noexcept
	{
		return m_capacityDeferredPlayHandleEditCountTotal.load(std::memory_order_relaxed);
	}
	//! How many times a producer drained the edit queue itself (nothing was consuming it). Zero while a render drains.
	std::uint64_t producerDrainedPlayHandleEdits() const noexcept
	{
		return m_producerDrainedPlayHandleEdits.load(std::memory_order_relaxed);
	}

	//! @returns a span over the mixed buffer of this audio bus handle
	std::span<SampleFrame> buffer() { return m_buffer; }

	//! Frames of latency this track adds to the signal path (#605): its effect
	//! chain's, plus the latency of the source that plays into it (MX-157, a
	//! hosted instrument's own). Read by the mixer's PDC graph on the audio thread.
	int latencyFrames() const;

	//! MX-157 (BUGS 11.34): the latency of the SOURCE that plays into this handle -
	//! a hosted instrument's own, reported through Instrument::reportLatency() - in
	//! frames, ahead of the effect chain. Control thread; a lock-free value the
	//! audio thread reads. 0 (the default) leaves the handle exactly as it was.
	void setSourceLatencyFrames(int frames);
	int sourceLatencyFrames() const { return m_sourceLatencyFrames.load(std::memory_order_relaxed); }

	/*! Q050-339: the track's manual delay, in frames at the engine's rate: positive plays it later, negative
	 *  earlier (the other paths are then delayed instead - the offset is added to the PDC graph, never a
	 *  replacement for it). 0 (the default, and every older project) leaves the handle exactly as it was.
	 *  Control thread; a lock-free value the audio thread reads. Clamped to +-MaxManualOffsetFrames. */
	void setOffsetFrames(int frames);
	int offsetFrames() const { return m_offsetFrames.load(std::memory_order_relaxed); }
	//! What the PDC graph aligns this path by: its latency less its manual offset (may be negative).
	int alignedLatencyFrames() const { return latencyFrames() - offsetFrames(); }

	/*! Q050-174 (MX-159): low-latency monitoring of a latent instrument. While set, this handle applies NO
	 *  alignment delay of its own (the delay PDC adds so the path lands on a later path's alignment point),
	 *  so what is played through it is heard after the source's own latency only. Its latency still counts
	 *  in the graph, so every other path is aligned exactly as before. Session state set by
	 *  transport.low_latency on the control thread; a lock-free value the audio thread reads. Never saved. */
	void setMonitorUnaligned(bool on) { m_monitorUnaligned.store(on, std::memory_order_relaxed); }
	bool monitorUnaligned() const { return m_monitorUnaligned.load(std::memory_order_relaxed); }

	//! @returns true if the processing outputted corrupted audio (infs/nans).
	bool isCorrupted() const { return m_corrupted.load(std::memory_order_relaxed); }

	//! 0.5.0 F2 (an instrument layer's mute, solo and level, InstrumentLayers::setMix): a gain applied with the
	//! volume and panning, 1 = unchanged (bit-identical). Any thread; the audio thread reads it once a period.
	void setGain(float gain) { m_gain.store(gain, std::memory_order_relaxed); }
	float gain() const { return m_gain.load(std::memory_order_relaxed); }

private:
	friend class AudioEngine;
	//! AudioEngine calls this with rendering quiesced or from its owning render stage.
	void removePlayHandleUnderEngineChangeLock(PlayHandle* handle);
	//! The track's volume and panning, applied to m_buffer in place
	void applyVolumeAndPanning(f_cnt_t fpp);

	volatile bool m_bufferUsage;

	//! Interleaved stereo buffer (one track channel pair) that play handles are mixed into
	std::vector<SampleFrame> m_buffer;
	//! First track channel pair of the audio bus
	SampleFrame* m_trackChannels;

	//! Audio bus view over m_buffer, used for effect processing and mixing
	AudioBus m_bus;

	bool m_extOutputEnabled;
	mix_ch_t m_nextMixerChannel;

	QString m_name;

	std::unique_ptr<EffectChain> m_effects;

	PlayHandleList m_playHandles;
	//! One allocation-free slot for a standalone bus created on the audio thread.
	PlayHandle* m_inlinePlayHandle = nullptr;
	//! One play-handle list edit posted by addPlayHandle() / removePlayHandle().
	struct PlayHandleEdit
	{
		PlayHandle* handle;
		bool add;
	};
	//! Routes one edit: owner-stage direct, worker-stage buffered, other off-stage queued.
	void postPlayHandleEdit(PlayHandleEdit edit);
	void applyPlayHandleEdit(const PlayHandleEdit& edit);
	//! Applies every queued edit in the order it was posted. The queue has ONE consumer at a time: the render
	//! owner in doProcessing(), or - only when no render is draining the queue - a producer that holds the
	//! engine change lock, which keeps the render out. Never a producer racing a live consumer.
	void applyPlayHandleEdits();
	//! Pushes \a edit, waiting while the consumer is making progress. False when it is not (nothing is
	//! rendering, or the render is blocked behind this thread's own change lock): then the caller drains the
	//! queue itself under that lock instead of racing a consumer that is not there.
	bool pushPlayHandleEditWhileConsumerDrains(const PlayHandleEdit& edit);
	//! Grow only from a control thread, after the render lock makes the QList quiescent.
	void topUpPlayHandleCapacityOnControlThread();
	void topUpPlayHandleCapacityUnderEngineChangeLock();
	bool appendPlayHandleWithoutGrowth(PlayHandle* handle) noexcept;
	//! Marks the render-stage owner, where no bus worker is reading this list.
	class RenderStageScope
	{
	public:
		RenderStageScope() noexcept;
		~RenderStageScope() noexcept;
		RenderStageScope(const RenderStageScope&) = delete;
		RenderStageScope& operator=(const RenderStageScope&) = delete;

	private:
		bool m_previous;
	};
	static thread_local bool s_renderStageOwnsList;
	//! Non-worker off-stage producers post here; doProcessing() is the consumer.
	MpscQueue<PlayHandleEdit, PlayHandle::MaxNumber> m_playHandleEdits;
	//! A full list defers accepted additions until cleanup frees a reserved slot.
	std::array<PlayHandleEdit, PlayHandle::MaxNumber> m_capacityDeferredPlayHandleEdits{};
	std::size_t m_capacityDeferredPlayHandleEditCount = 0;
	std::atomic<bool> m_playHandleCapacityLow{false};
	std::atomic<std::uint64_t> m_capacityDeferredPlayHandleEditCountTotal{0};
	std::atomic<std::uint64_t> m_droppedPlayHandleEdits{0};
	//! Edits the consumer has popped so far: a producer watches it move to tell a draining consumer from a stopped one.
	std::atomic<std::uint64_t> m_playHandleEditsConsumed{0};
	//! Edits a producer drained itself, under the change lock, because no consumer was making progress.
	std::atomic<std::uint64_t> m_producerDrainedPlayHandleEdits{0};

	FloatModel* m_volumeModel;
	FloatModel* m_panningModel;
	BoolModel* m_mutedModel;

	//! PDC (#605): delays this track's output to the alignment point of the
	//! mixer channel it feeds. Preallocated in the constructor; zero delay is
	//! an exact no-op.
	LatencyCompensation m_compensation;

	std::atomic<bool> m_corrupted = false;
	std::atomic<float> m_gain = 1.0f;

	//! MX-157: see setSourceLatencyFrames().
	std::atomic<int> m_sourceLatencyFrames{0};
	//! Q050-339: see setOffsetFrames().
	std::atomic<int> m_offsetFrames{0};
	//! Q050-174: see setMonitorUnaligned().
	std::atomic<bool> m_monitorUnaligned{false};

	friend class AudioEngine;
	friend class AudioEngineWorkerThread;
};

} // namespace lmms

#endif // LMMS_AUDIO_BUS_HANDLE_H
