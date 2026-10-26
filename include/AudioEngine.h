/*
 * AudioEngine.h - device-independent audio engine for LMMS
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

#ifndef LMMS_AUDIO_ENGINE_H
#define LMMS_AUDIO_ENGINE_H

#include <mutex>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include <QThread>
#include <QTimer>
#include <samplerate.h>

#include <memory>
#include <vector>

#include "AudioBufferView.h"
#include "AudioDevice.h"
#include "AudioResampler.h"
#include "LmmsTypes.h"
#include "SampleFrame.h"
#include "LocklessList.h"
#include "AudioEngineProfiler.h"
#include "MasterLoudnessTap.h"
#include "PunchWindow.h"
#include "PlayHandle.h"
#include "AudioWideInputStage.h"
#include "HardwareOutputBus.h"
#include "MultiTrackRecorder.h"
#include "RetroAudioCapture.h"
#include "SamplerTakeRecorder.h"
#include "SampleFrameRingBuffer.h"

#ifdef LMMS_TESTING
class AudioBusPlayHandleQueueTest;
class ArrangeToolsTest;
#endif

namespace lmms
{

class MidiClient;
class AudioBusHandle;  // IWYU pragma: keep
class AudioEngineWorkerThread;
class SampleBuffer;
class SamplePlayHandle;
class Track;
class AutomatableModel;

constexpr f_cnt_t MINIMUM_BUFFER_SIZE = 32;
constexpr f_cnt_t DEFAULT_BUFFER_SIZE = 256;
constexpr f_cnt_t MAXIMUM_BUFFER_SIZE = 4096;

constexpr int BYTES_PER_SAMPLE = sizeof(sample_t);
constexpr int BYTES_PER_INT_SAMPLE = sizeof(int_sample_t);
constexpr int BYTES_PER_FRAME = sizeof(SampleFrame);

constexpr float OUTPUT_SAMPLE_MULTIPLIER = 32767.0f;

constexpr auto SUPPORTED_SAMPLERATES = std::array{44100, 48000, 88200, 96000, 192000};
constexpr auto SUPPORTED_BITRATES = std::array{64, 128, 160, 192, 256, 320};

class LMMS_EXPORT AudioEngine : public QObject
{
	Q_OBJECT
public:
	//! Preconstructed sample-voice partitions shared by metronome and frozen takes.
	static constexpr std::size_t MetronomeSampleVoicePoolCapacity = 32;
	static constexpr std::size_t FrozenSampleVoicePoolCapacity = 480;

	/**
	 * @brief RAII helper for requestChangesInModel.
	 * Used by AudioEngine::requestChangesGuard.
	 */
	class RequestChangesGuard {
		friend class AudioEngine;

	private:
		RequestChangesGuard(AudioEngine* audioEngine)
			: m_audioEngine{audioEngine}
		{
			m_audioEngine->requestChangeInModel();
		}
	public:

#ifdef LMMS_TESTING
		/*! Test-only guard: it takes the change lock exactly like the ordinary
		 *  guard, but it reports a CONTENDED acquisition - the lock was held by
		 *  another thread (the render thread) when the acquisition was
		 *  attempted - through @a contendedProbe.
		 *
		 *  The report is made AT the attempt, on the acquiring thread: a
		 *  report sent before the attempt could not tell a thread that is
		 *  blocked on the change lock from one that has not run yet.
		 */
		RequestChangesGuard(AudioEngine* audioEngine, void (*contendedProbe)(void*), void* context)
			: m_audioEngine{audioEngine}
		{
			m_audioEngine->requestChangeInModelForTest(contendedProbe, context);
		}
#endif

		RequestChangesGuard()
			: m_audioEngine{nullptr}
		{
		}

		RequestChangesGuard(RequestChangesGuard&& other)
			: RequestChangesGuard()
		{
			std::swap(other.m_audioEngine, m_audioEngine);
		}

		// Disallow copy.
		RequestChangesGuard(const RequestChangesGuard&) = delete;
		RequestChangesGuard& operator=(const RequestChangesGuard&) = delete;

		~RequestChangesGuard() {
			if (m_audioEngine) {
				m_audioEngine->doneChangeInModel();
			}
		}

	private:
		AudioEngine* m_audioEngine;
	};

	void initDevices();
	void clear();
	void clearNewPlayHandles();


	// audio-device-stuff

	bool renderOnly() const { return m_renderOnly; }
	//! The fixed render contexts: the worker threads plus the caller-side context.
	int renderThreadCount() const noexcept;
	// Returns the current audio device's name. This is not necessarily
	// the user's preferred audio device, in case you were thinking that.
	inline const QString & audioDevName() const
	{
		return m_audioDevName;
	}
	inline bool audioDevStartFailed() const
	{
		return m_audioDevStartFailed;
	}
	//! The device name the configuration asked for (may differ from
	//! audioDevName() after a fallback). Needed so a failure report can name the
	//! backend that failed to open instead of saying only "an audio device".
	inline const QString & audioDevRequestName() const
	{
		return m_audioDevRequestName;
	}
	//! Why the configured device could not be used, when it could not: the text
	//! control.ping and the typed 'requires' refusal hand to an agent. Empty when
	//! the device started normally.
	inline const QString & audioDevStartReason() const
	{
		return m_audioDevStartReason;
	}

	//! Set new audio device. Old device will be deleted,
	//! unless it's stored using storeAudioDevice
	void setAudioDevice(AudioDevice* _dev, bool startNow);
	void storeAudioDevice();
	void restoreAudioDevice();
	inline AudioDevice * audioDev()
	{
		return m_audioDev;
	}

	//! 0.5.0 F1 (MX-040): the device outputs beyond the master - direct outs, cue mixes, hardware
	//! insert sends (HardwareOutputBus.h). The mixer writes it on the render thread; the device feed
	//! (renderNextBuffer) reads it. Nothing else may touch it while the engine renders.
	HardwareOutputBus& hardwareOutputs() { return m_hardwareOut; }
	const HardwareOutputBus& hardwareOutputs() const { return m_hardwareOut; }

	/*! The PASSIVE loudness tap on the master mix (feature row 24 of
	 *  docs/FEATURE-LIST-0.3.0.md): the live LUFS-I / LUFS-M / LUFS-S / true-peak
	 *  readout, fed one period per rendered period out of renderStageMix().
	 *
	 *  It is constructed once, here, with the engine's PROCESSING rate and
	 *  stereo channel count, and never replaced - so no allocation, no lock and
	 *  no re-seating happens while audio is running, and a render that never
	 *  arms it is bit-for-bit the render it always was (the tap only reads, and
	 *  only while it is armed). It is DISARMED at construction: nothing in this
	 *  release measures the master until a caller asks for it, which is the rule
	 *  docs/LUFS-METER.md states ("the meter is opt-in") kept intact for the
	 *  live path. Never null for a constructed engine; a caller that wants to
	 *  handle a missing engine handles a missing engine. */
	MasterLoudnessTap* masterLoudness() const { return m_masterLoudness.get(); }


	// audio-bus-handle-stuff
	void addAudioBusHandle(AudioBusHandle* busHandle);

	void removeAudioBusHandle(AudioBusHandle* busHandle);

	//! Read-only view of the registered handles (#605). The audio thread
	//! iterates this under m_changeMutex (see renderStageEffects), which is the
	//! same protection the add/remove path takes.
	const std::vector<AudioBusHandle*>& audioBusHandles() const { return m_audioBusHandles; }


	// MIDI-client-stuff
	inline const QString & midiClientName() const
	{
		return m_midiClientName;
	}

	inline MidiClient * midiClient()
	{
		return m_midiClient;
	}


	// play-handle stuff
	bool addPlayHandle( PlayHandle* handle );
	//! Accepted engine-list edits deferred by capacity and out-of-bound overflow, respectively.
	std::uint64_t capacityDeferredPlayHandles() const noexcept
	{
		return m_capacityDeferredPlayHandleCountTotal.load(std::memory_order_relaxed);
	}
	std::uint64_t droppedPlayHandles() const noexcept
	{
		return m_droppedPlayHandleCount.load(std::memory_order_relaxed);
	}
	//! Count of play-handle edits rejected after a bus's fixed storage bounds filled.
	std::uint64_t droppedPlayHandleEdits() const noexcept
	{
		return m_droppedPlayHandleEditCount.load(std::memory_order_relaxed);
	}
	//! SlicerT notes refused because the instrument's fixed playback-state pool was full.
	void recordSlicerTStateOverflow() noexcept
	{
		m_slicerTStateOverflowCount.fetch_add(1, std::memory_order_relaxed);
	}
	std::uint64_t slicerTStateOverflows() const noexcept
	{
		return m_slicerTStateOverflowCount.load(std::memory_order_relaxed);
	}
	//! Claim and arm one preconstructed metronome or frozen-take voice on the render path.
	SamplePlayHandle* acquirePooledSampleVoice(const std::shared_ptr<const SampleBuffer>& buffer,
		int sourceIn, int sourceOut, float amplification, f_cnt_t offset, Track* track, bool metronome) noexcept;
	//! Return a detached pool handle; realtime returns are retired for the main-thread recycler.
	void recyclePooledSampleVoice(SamplePlayHandle* handle, bool realtime) noexcept;

	//! Reconfigure every retained sample converter off the audio thread.
	auto applyConverterModeToRetainedStates(AudioResampler::Mode mode, SrcQuality quality) noexcept -> bool;
	static auto onSrcQualityChanged(SrcQuality quality) noexcept -> bool;
	std::uint64_t converterModePropagationFailures() const noexcept
	{
		return m_converterModePropagationFailures.load(std::memory_order_relaxed);
	}
	//! Whether @a handle is the private bus used by the fixed sample-voice pool.
	bool isSampleVoicePoolBus(const AudioBusHandle* handle) const noexcept;
	void recordSampleVoiceOverflow() noexcept
	{
		m_sampleVoiceOverflowCount.fetch_add(1, std::memory_order_relaxed);
	}
	std::uint64_t sampleVoiceOverflows() const noexcept
	{
		return m_sampleVoiceOverflowCount.load(std::memory_order_relaxed);
	}
	std::uint64_t sampleVoiceQueueInvariantFailures() const noexcept
	{
		return m_sampleVoiceQueueInvariantFailureCount.load(std::memory_order_relaxed);
	}
	//! Release the pool bus while Engine's global audio-engine pointer and device are still valid.
	void shutdownSampleVoicePool() noexcept;
	//! Prepare, adopt, and discard form the engine-wide converter transaction.
	bool prepareConverterModeForRetainedStates(AudioResampler::Mode mode) noexcept;
	void adoptPreparedConverterModeForRetainedStates() noexcept;
	void discardPreparedConverterModeForRetainedStates() noexcept;
	enum class ConverterModePhase { Prepare, Adopt, Discard };
	static auto applyConverterModeToPlayHandle(PlayHandle* handle, AudioResampler::Mode mode,
		ConverterModePhase phase) noexcept -> bool;
	void recordDroppedPlayHandleEdit() noexcept
	{
		m_droppedPlayHandleEditCount.fetch_add(1, std::memory_order_relaxed);
	}
	//! Realtime-safe request for the engine-thread capacity service.
	void requestPlayHandleCapacityRefresh() noexcept
	{
		m_playHandleCapacityRefreshRequested.store(true, std::memory_order_relaxed);
	}

	void removePlayHandle( PlayHandle* handle );

	inline PlayHandleList& playHandles()
	{
		return m_playHandles;
	}

	void removePlayHandlesOfTypes(Track * track, PlayHandle::Types types);

	AudioEngineProfiler& profiler()
	{
		return m_profiler;
	}

	int cpuLoad() const
	{
		return m_profiler.cpuLoad();
	}

	int detailLoad(const AudioEngineProfiler::DetailType type) const
	{
		return m_profiler.detailLoad(type);
	}

	sample_rate_t baseSampleRate() const { return m_baseSampleRate; }


	sample_rate_t outputSampleRate() const
	{
		return m_audioDev != nullptr ? m_audioDev->sampleRate() : m_baseSampleRate;
	}
	

	sample_rate_t inputSampleRate() const	
	{
		return m_audioDev != nullptr ? m_audioDev->sampleRate() : m_baseSampleRate;
	}


	inline float masterGain() const
	{
		return m_masterGain;
	}

	inline void setMasterGain(const float mo)
	{
		m_masterGain = mo;
	}


	static inline sample_t clip(const sample_t s)
	{
		if (s > 1.0f)
		{
			return 1.0f;
		}
		else if (s < -1.0f)
		{
			return -1.0f;
		}
		return s;
	}


	bool criticalXRuns() const;

	//! Whether criticalXRuns() may refuse new play handles (on by default). It is a wall-clock
	//! decision: a build whose period takes longer than the device's period budget sheds the
	//! clips that start in it. A benchmark has no device deadline and measures the render path,
	//! so EngineRenderBenchmarkTest turns the shed off; the product never does.
	void setOverloadSheddingEnabled(bool enabled) { m_overloadShedding.store(enabled, std::memory_order_relaxed); }

	void pushInputFrames( const SampleFrame* _ab, const f_cnt_t _frames ) noexcept;

	inline const SampleFrame* inputBuffer()
	{
		return m_inputBuffer[ m_inputBufferRead ];
	}

	inline f_cnt_t inputBufferFrames() const
	{
		return m_inputBufferFrames[ m_inputBufferRead ];
	}

	//! Frames the capture backends have staged for the next rendered period
	//! (prototype input path). Bounded by InputStageCapacityFrames: the staging
	//! ring is fixed-capacity and pre-allocated, so the input path can neither
	//! lock nor grow without bound.
	std::size_t inputFramesStaged() const noexcept;

	//! Frames the capture path had to drop because the staging ring was full.
	std::uint64_t inputFramesDropped() const noexcept;

	//! Capacity of the capture staging ring. pushInputFrames() can never make
	//! the input path hold more than this, which is what bounds the memory the
	//! capture path can use (D9a: a stopped JACK device used to grow the input
	//! buffer without bound).
	static constexpr f_cnt_t InputStageCapacityFrames = 16384;

	//! Prototype two-track recorder (task #556). The engine feeds it once per
	//! period from the audio thread; see renderNextPeriod().
	MultiTrackRecorder& recorder() { return m_recorder; }
	const MultiTrackRecorder& recorder() const { return m_recorder; }

	/*! R2.4: the punch region the recorders are gated by, as the render thread
	 *  reads it (include/PunchWindow.h). Published from the control thread by the
	 *  Song whenever its timeline's punch changes; one atomic word, so the audio
	 *  thread never reads the Timeline's own fields, nor a begin from one publish
	 *  with an end from another. */
	void setPunchWindow(bool armed, tick_t begin, tick_t end) noexcept;
	//! Render thread, inside a period: the frames of this period's @a frames the punch region
	//! lets a recorder take - the one gate the record routes and the record-armed clips share
	//! (0.5.0, engine audit E3). Every frame when no region is armed.
	PunchFrames punchWindow(f_cnt_t frames) const noexcept;
	//! 0.5.0 L06: whether song tick @a tick is inside the published punch region (any thread).
	bool punchCapturesTick(tick_t tick) const noexcept
	{
		const std::uint64_t punch = m_punch.load(std::memory_order_relaxed);  // disarmed is the empty span 0..0
		return tick >= punchBeginOf(punch) && tick < punchEndOf(punch);
	}

	// -----------------------------------------------------------------------
	// The N-CHANNEL capture IN path (0.3.0, feature row 64 "Arbitrary input
	// count / multiple simultaneous inputs") and the retrospective AUDIO window
	// that rides on it (feature row 16). Both are fed from the audio thread in
	// renderNextPeriod(); the staging stage itself is
	// include/AudioWideInputStage.h. With no capture backend the wide path is
	// empty (inputWideFrames() == 0) and the stereo path stays the one that is
	// used, which is the state every consumer was in before this existed.
	// -----------------------------------------------------------------------
	//! Capture thread: stage one interleaved N-channel block. Realtime-safe. A
	//! block wider than AudioInputPath::MaxChannels, or of a different width
	//! than the one already staged, is refused whole and counted.
	//! \returns the frames the stage took (0 when it refused the block whole).
	//! The publisher needs that number: the stereo representation of a capture
	//! block is published for EXACTLY the frames the wide path took, so the two
	//! paths are always one block (cluster 3705).
	std::size_t pushInputFramesWide(const float* interleaved, int channels, f_cnt_t frames) noexcept;
	//! Render thread: the N-channel input of the most recently rendered period.
	const float* inputWideBuffer() const noexcept;
	f_cnt_t inputWideFrames() const noexcept;
	int inputWideChannels() const noexcept;
	//! Frames staged by the capture path and not yet drained by a render.
	std::size_t inputWideFramesStaged() const noexcept;
	//! Frames the wide path refused (width mismatch) or dropped (stage full).
	std::uint64_t inputWideFramesDropped() const noexcept;

	//! Retrospective AUDIO capture (feature row 16): the last N frames of the
	//! engine's input, kept only while armed. Off by default.
	RetroAudioCapture& retroCapture() noexcept { return m_retroCapture; }
	const RetroAudioCapture& retroCapture() const noexcept { return m_retroCapture; }

	//! 0.5.0 L10 (Q050-156): the input take being recorded straight into a sampler
	//! instrument (AudioFileProcessor / SlicerT). Idle unless one is in hand.
	SamplerTakeRecorder& samplerTakeRecorder() noexcept { return m_samplerTakeRecorder; }
	const SamplerTakeRecorder& samplerTakeRecorder() const noexcept { return m_samplerTakeRecorder; }

	/**
	 * @returns The internal buffer size used by audio plugins and other processing done within the audio engine.
	 * Its value is @ref DEFAULT_BUFFER_SIZE or @ref framesPerAudioBuffer(), whichever is lower.
	 * @see renderNextPeriod()
	 */
	f_cnt_t framesPerPeriod() const { return m_framesPerPeriod; }

	/**
	 * @returns The buffer size used by the configured @ref AudioDevice "output audio device". It is in the range
	 * of @ref MINIMUM_BUFFER_SIZE and @ref MAXIMUM_BUFFER_SIZE.
	 * @note This should not be needed for a majority of cases. Currently, it's only being used to set up the
	 * audio devices. This member function may be removed in a later refactor.
	 */
	f_cnt_t framesPerAudioBuffer() const { return m_framesPerAudioBuffer; }

	/*! R2.2: how far a recorded take lags what the engine was playing when it was captured -
	 *  the device's output and input latency, one capture block of input staging
	 *  (AudioDevice.h) and one period of the engine's own output double buffer. A take shifted back by this lines up with the playback it was
	 *  performed against. 0 with no device. */
	f_cnt_t recordingLatencyFrames() const { return m_recordingLatency.load(std::memory_order_relaxed); }

	/*! BUGS_FOUND 11.29: false from a device start until its capture first delivers frames.
	 *  A period rendered before that has no input to read, and that is not "input that lags":
	 *  the round trip's capture-staging term (recordingLatencyFrames) is a block the stream
	 *  never held, so a take that starts there must not skip it. Read on the render thread;
	 *  every device start (AudioDevice::startProcessing) resets it. */
	bool inputStreamStarted() const { return m_inputStreamStarted.load(std::memory_order_relaxed); }
	/*! A device (re)start: the capture path has delivered nothing yet, and the
	 *  next device publishes its own blocks. The wide-path flag follows it, so
	 *  a device that does not use the wide path at all drains the stereo ring
	 *  on its own (cluster 3705). */
	void resetInputStream()
	{
		m_inputStreamStarted.store(false, std::memory_order_relaxed);
		m_captureWideInUse.store(false, std::memory_order_relaxed);
	}

	/**
	 * @brief Renders the next audio period.
	 *
	 * An audio period is a fixed-size chunk of audio the engine generates, and represents a single cycle of the
	 * engine's output. The rendering is chunked into smaller periods to timely handle per-buffer updates like
	 * non-sample-accurate automation, as well as to improve memory cache performance.
	 *
	 * The audio period generated is interleaved and stereo.
	 *
	 * @note The audio period returned is non-owning and will be changed on subsequent calls to @ref renderNextPeriod()
	 * and @ref renderNextBuffer(). Callers must copy the data into their own local buffers if they need it to persist.
	 *
	 * @returns A non-owning buffer to the next audio period.
	 */
	std::span<const SampleFrame> renderNextPeriod();

	//! Register models from their base constructor; growth is control-thread-only.
	bool registerAutomatableModel(AutomatableModel* model) noexcept;
	//! Remove a model slot; realtime teardown only tombstones its preallocated slot.
	void unregisterAutomatableModel(AutomatableModel* model) noexcept;
	//! Serial cache pass. The normal caller already owns m_changeMutex.
	void prepareAutomatableModelBuffers() noexcept;
	void recordUnregisteredAutomatableBufferRequest() noexcept
	{
		m_unregisteredAutomatableBufferRequests.fetch_add(1, std::memory_order_relaxed);
	}
	void recordUnpreparedAutomatableBufferRequest() noexcept
	{
		m_unpreparedAutomatableBufferRequests.fetch_add(1, std::memory_order_relaxed);
	}
	void recordUnpreparedControllerBufferRequest() noexcept
	{
		m_unpreparedControllerBufferRequests.fetch_add(1, std::memory_order_relaxed);
	}
	std::uint64_t unregisteredAutomatableBufferRequests() const noexcept
	{
		return m_unregisteredAutomatableBufferRequests.load(std::memory_order_relaxed);
	}
	std::uint64_t unpreparedAutomatableBufferRequests() const noexcept
	{
		return m_unpreparedAutomatableBufferRequests.load(std::memory_order_relaxed);
	}
	std::uint64_t unpreparedControllerBufferRequests() const noexcept
	{
		return m_unpreparedControllerBufferRequests.load(std::memory_order_relaxed);
	}
	std::uint64_t registeredAutomatableModelCount() const noexcept
	{
		return m_registeredAutomatableModelCount.load(std::memory_order_relaxed);
	}
	std::uint64_t automatableModelRegistrationFailures() const noexcept
	{
		return m_automatableModelRegistrationFailures.load(std::memory_order_relaxed);
	}
	//! TEST SEAM: make model registration fail before it can acquire the change lock.
	void setAutomatableModelRegistrationFailureForTest(bool fail) noexcept;
	//! TEST SEAM: signal before registration attempts to acquire the change lock.
	void setAutomatableModelRegistrationAttemptStateForTest(std::atomic<int>* state) noexcept;
	bool isRenderThread() const noexcept;

	/**
	 * @brief Renders an audio buffer into @a dst.
	 *
	 * Renders @ref renderNextAudioPeriod() "audio periods" into @a dst. If @a dst is not a multiple of the period
	 * size, the remaining frames are partially rendered (an extra period may be rendered in such cases, which can
	 * degrade performance).
	 *
	 * If @a dst has 1 channel, the channels are averaged to mono.
	 * If @a dst has 2 channels, the channels are directly copied.
	 * If @a dst has more than 2 channels, the stereo channels are copied and the rest are zero-filled -
	 * unless the mixer routed something to the hardware outputs (0.5.0 F1, HardwareOutputBus): then
	 * each device channel carries what is routed there, the master on its own pair (1/2 by default).
	 *
	 * @param dst An audio buffer view to write into. Both interleaved and planar overloads are provided.
	 */
	void renderNextBuffer(InterleavedBufferView<float> dst) { renderNextBuffer<InterleavedBufferView<float>>(dst); }

	//! @copydoc renderNextBuffer(InterleavedBufferView<float>)
	void renderNextBuffer(PlanarBufferView<float> dst) { renderNextBuffer<PlanarBufferView<float>>(dst); }

	//! Block until a change in model can be done (i.e. wait for audio thread)
	void requestChangeInModel();
	void doneChangeInModel();

	RequestChangesGuard requestChangesGuard()
	{
		return RequestChangesGuard{this};
	}

	//! @returns how many control threads are parked inside
	//! requestChangeInModel(), waiting for the render period's model
	//! ownership. A test seam: it lets a test observe that another control
	//! thread reached the model-change guard, so a concurrency regression can
	//! decide on an ordering of events instead of a clock deadline.
	int modelChangeWaiters() const
	{
		return m_modelChangeWaiters.load(std::memory_order_relaxed);
	}

#ifdef LMMS_TESTING
	//! Test seam: whether the CALLING thread currently holds the change-in-model
	//! guard through requestChangeInModel(). A test uses it to prove a topology
	//! rebuild excludes the audio thread for its whole duration; it is not a
	//! product query (the render thread takes m_changeMutex directly).
	bool isChangeInModelHeld() const noexcept { return s_changeInModelDepth > 0; }

	/*! Test-only: takes the change lock like requestChangeInModel(), but
	 *  reports a CONTENDED acquisition through @a contendedProbe - the probe is
	 *  called from the acquiring thread when the lock was held by another
	 *  thread (the render thread) at the attempt.
	 *
	 *  This is the observation a test needs to tell a thread that is blocked
	 *  on the change lock from one that has not run yet: a timeout on "nothing
	 *  happened" cannot.
	 */
	void requestChangeInModelForTest(void (*contendedProbe)(void*), void* context);

	RequestChangesGuard requestChangesGuardForTest(void (*contendedProbe)(void*), void* context)
	{
		return RequestChangesGuard{this, contendedProbe, context};
	}
#endif

	static bool isAudioDevNameValid(QString name);
	static bool isMidiDevNameValid(QString name);

	//! @returns true if sanitization of inf/nan values is enabled
	bool sanitizationEnabled() { return m_sanitizationEnabled.load(std::memory_order_relaxed); }

	//! Enable/disable sanitization of inf/nan values
	void setSanitizationEnabled(bool enabled) { m_sanitizationEnabled.store(enabled, std::memory_order_relaxed); }

signals:
	void qualitySettingsChanged();
	void sampleRateChanged();
	void nextAudioBuffer(const lmms::SampleFrame* buffer);


private:
	struct SampleVoicePool;
	void serviceSampleVoicePoolOnControlThread() noexcept;
	void disposePlayHandle(PlayHandle* handle, bool realtime) noexcept;
	struct AutomatableModelRegistry;
#ifdef LMMS_TESTING
	friend class ::AudioBusPlayHandleQueueTest;
	// The track-version regression reads the change mutex directly: it is the
	// lock the audio thread takes, and a test needs to see whether a model
	// edit runs inside it.
	friend class ::ArrangeToolsTest;
#endif
	void renderNextBuffer(AudioBufferView<float> auto dst)
	{
		for (auto frame = f_cnt_t{0}; frame < dst.frames(); ++frame)
		{
			if (m_outputBufferReadIndex == m_framesPerPeriod) { m_outputBufferReadIndex = 0; }
			if (m_outputBufferReadIndex == 0) { renderNextPeriod(); }

			const SampleFrame& master = m_outputBufferRead[m_outputBufferReadIndex];
			// 0.5.0 F1 (MX-040): only a period that routes something to the hardware takes the bus;
			// every other period is written exactly as it always was.
			if (m_hardwareOut.active()) { writeHardwareFrame(dst, frame, master); }
			else { writeMasterFrame(dst, frame, master); }

			++m_outputBufferReadIndex;
		}
	}

	//! The master alone on the device: averaged for one channel, on the first two otherwise, silence beyond.
	static void writeMasterFrame(AudioBufferView<float> auto dst, f_cnt_t frame, const SampleFrame& master)
	{
		switch (dst.channels())
		{
		case 0:
			assert(false);
			break;
		case 1:
			dst.sample(0, frame) = master.average();
			break;
		default:
			dst.sample(0, frame) = master[0];
			dst.sample(1, frame) = master[1];
			for (auto channel = 2; channel < dst.channels(); ++channel)
			{
				dst.sample(channel, frame) = 0.f;
			}
			break;
		}
	}

	//! 0.5.0 F1: every device channel from the hardware bus, the master on its placement.
	void writeHardwareFrame(AudioBufferView<float> auto dst, f_cnt_t frame, const SampleFrame& master) const
	{
		if (dst.channels() == 1)
		{
			dst.sample(0, frame) = m_hardwareOut.monoSample(m_outputBufferReadIndex, master);
			return;
		}
		for (auto channel = 0; channel < dst.channels(); ++channel)
		{
			dst.sample(channel, frame) = m_hardwareOut.sample(channel, m_outputBufferReadIndex, master);
		}
	}

	AudioEngine( bool renderOnly );
	~AudioEngine() override;

	void startProcessing()
	{
		m_audioDev->startProcessing();
		refreshRecordingLatency();
	}
	//! R2.2: re-reads the device's figures into the cached round trip (off the audio thread).
	void refreshRecordingLatency();
	void stopProcessing() { m_audioDev->stopProcessing(); }


	AudioDevice * tryAudioDevices();
	MidiClient * tryMidiClients();

	void renderStageNoteSetup();
	void renderStageInstruments();
	void renderStageEffects();
	void renderStageMix();


	void swapBuffers();

	void clearInternal();

	bool m_renderOnly;

	std::vector<AudioBusHandle*> m_audioBusHandles;

	f_cnt_t m_framesPerAudioBuffer;
	//! R2.2: the cached round trip; the audio thread reads it when a take starts.
	std::atomic<f_cnt_t> m_recordingLatency{0};
	std::atomic<bool> m_inputStreamStarted{false};
	f_cnt_t m_framesPerPeriod;
	sample_rate_t m_baseSampleRate;

	SampleFrame* m_inputBuffer[2];
	f_cnt_t m_inputBufferFrames[2];
	f_cnt_t m_inputBufferSize[2];
	int m_inputBufferRead;
	int m_inputBufferWrite;

	//! Capture input staging: a fixed-capacity, pre-allocated, lock-free SPSC
	//! ring between the backends' capture thread and the render thread.
	//! pushInputFrames() only ever writes into it (no lock, no allocation, no
	//! growth); swapBuffers() drains it once per rendered period into
	//! m_inputBuffer[m_inputBufferRead]. Both m_inputBuffer entries are sized
	//! to InputStageCapacityFrames, so a drain can never overrun them.
	std::unique_ptr<SampleFrameRingBuffer> m_inputStage;

	//! The N-CHANNEL capture stage (0.3.0, feature row 64; see
	//! include/AudioWideInputStage.h). Allocated once in the constructor, off
	//! every realtime thread, and drained once per rendered period beside
	//! m_inputStage. May be null only before the constructor has run.
	std::unique_ptr<AudioWideInputStage> m_wideInputStage;

	/*! Whether the capture path publishes BOTH representations of a capture
	 *  block - the N-channel block and the stereo pair (cluster 3705). Set by
	 *  the first pushInputFramesWide(), cleared by every device (re)start.
	 *  While it is set, the two stages are drained as ONE block: as many
	 *  frames as both of them hold, so a render can never take the wide block
	 *  in one period and the stereo pair in the next. A capture path that
	 *  never uses the wide path drains the stereo ring on its own, exactly as
	 *  it always did. */
	std::atomic<bool> m_captureWideInUse{false};

	//! Render thread: drain the capture stages once per rendered period.
	void drainCaptureStages() noexcept;

	//! prototype: N-route capture (task #556; N-wide since 0.3.0)
	MultiTrackRecorder m_recorder;

	//! R2.4: the published punch window (setPunchWindow): begin in the high half, end in the low, and
	//! armed exactly when end > begin (a disarmed window is stored as 0..0) ...
	std::atomic<std::uint64_t> m_punch{0};
	static tick_t punchBeginOf(std::uint64_t punch) noexcept { return static_cast<tick_t>(static_cast<std::uint32_t>(punch >> 32)); }
	static tick_t punchEndOf(std::uint64_t punch) noexcept { return static_cast<tick_t>(static_cast<std::uint32_t>(punch)); }
	//! ... and where the period being rendered STARTED on the song timeline,
	//! taken by the render thread before the song advances (renderStageNoteSetup).
	double m_periodStartFrame = 0.0;
	bool m_periodSongPlaying = false;

	//! Retrospective AUDIO capture (feature row 16). Its ring is allocated in
	//! this object's constructor - i.e. off the audio thread - and the audio
	//! thread's cost while it is disarmed is one relaxed atomic load per period.
	RetroAudioCapture m_retroCapture;

	//! 0.5.0 L10 (Q050-156): the take a sampler instrument (AudioFileProcessor /
	//! SlicerT) records from the input bus. Its ring is made by arm() on the
	//! control thread; the audio thread's cost while idle is one relaxed atomic
	//! load per period (include/SamplerTakeRecorder.h).
	SamplerTakeRecorder m_samplerTakeRecorder;

	std::unique_ptr<SampleFrame[]> m_outputBufferRead;
	std::unique_ptr<SampleFrame[]> m_outputBufferWrite;
	f_cnt_t m_outputBufferReadIndex;
	//! 0.5.0 F1 (MX-040): the hardware outputs beyond the master, flipped with the two buffers above.
	HardwareOutputBus m_hardwareOut;

	/*! The live loudness tap (feature row 24), construction-only and disarmed
	 *  until a caller arms it. Held by unique_ptr rather than by value so the
	 *  engine's own header stays free of the meter's 25 KB of fixed state and
	 *  an engine that never arms it pays one pointer. See masterLoudness(). */
	std::unique_ptr<MasterLoudnessTap> m_masterLoudness;

	// worker thread stuff
	std::vector<AudioEngineWorkerThread *> m_workers;
	int m_numWorkers;
	//! Main-thread poll only takes the change guard when a render-stage low-water mark is raised.
	QTimer m_playHandleCapacityTimer;

	// playhandle stuff
	PlayHandleList m_playHandles;
	//! Handles waiting for a reserved engine-list slot; the owner retries them next period.
	std::array<PlayHandle*, PlayHandle::MaxNumber> m_capacityDeferredPlayHandles{};
	std::size_t m_capacityDeferredPlayHandleCount = 0;
	std::atomic<bool> m_playHandleCapacityLow{false};
	std::atomic<bool> m_playHandleCapacityRefreshRequested{false};
	std::atomic<std::uint64_t> m_capacityDeferredPlayHandleCountTotal{0};
	std::atomic<std::uint64_t> m_droppedPlayHandleCount{0};
	std::atomic<std::uint64_t> m_droppedPlayHandleEditCount{0};
	std::atomic<std::uint64_t> m_slicerTStateOverflowCount{0};
	std::atomic<std::uint64_t> m_sampleVoiceOverflowCount{0};
	std::atomic<std::uint64_t> m_sampleVoiceQueueInvariantFailureCount{0};
	std::atomic<std::uint64_t> m_converterModePropagationFailures{0};
	std::unique_ptr<SampleVoicePool> m_sampleVoicePool;
	// place where new playhandles are added temporarily
	LocklessList<PlayHandle *> m_newPlayHandles;
	ConstPlayHandleList m_playHandlesToRemove;

	float m_masterGain;
	void topUpPlayHandleCapacityOnControlThread();
	bool appendPlayHandleWithoutGrowth(PlayHandle* handle) noexcept;
	bool removeCapacityDeferredPlayHandle(PlayHandle* handle) noexcept;

	// audio device stuff
	void doSetAudioDevice( AudioDevice *_dev );
	AudioDevice * m_audioDev;
	AudioDevice * m_oldAudioDev;
	QString m_audioDevName;
	QString m_audioDevRequestName;
	QString m_audioDevStartReason;
	bool m_audioDevStartFailed;

	// MIDI device stuff
	MidiClient * m_midiClient;
	QString m_midiClientName;

	AudioEngineProfiler m_profiler;

	bool m_clearSignal;
	std::atomic<bool> m_sanitizationEnabled = false;
	std::atomic<bool> m_overloadShedding{true};

	std::recursive_mutex m_changeMutex;
	std::atomic<int> m_modelChangeWaiters{0};
#ifdef LMMS_TESTING
	//! Nesting depth of requestChangeInModel() on the calling thread (the guard
	//! is recursive). Only read by isChangeInModelHeld() in tests.
	static thread_local int s_changeInModelDepth;
#endif
	std::atomic<AutomatableModelRegistry*> m_automatableModelRegistry{nullptr};
	std::atomic<std::uint64_t> m_registeredAutomatableModelCount{0};
	std::atomic<std::uint64_t> m_automatableModelRegistrationFailures{0};
	std::atomic<bool> m_failAutomatableModelRegistrationForTest{false};
	std::atomic<std::atomic<int>*> m_automatableModelRegistrationAttemptStateForTest{nullptr};
	std::atomic<std::uint64_t> m_unregisteredAutomatableBufferRequests{0};
	std::atomic<std::uint64_t> m_unpreparedAutomatableBufferRequests{0};
	std::atomic<std::uint64_t> m_unpreparedControllerBufferRequests{0};

	friend class Engine;
	friend class AutomatableModel;
	friend class AudioBusHandle;
	friend class AudioEngineWorkerThread;
	friend class ProjectRenderer;
} ;

} // namespace lmms

#endif // LMMS_AUDIO_ENGINE_H
