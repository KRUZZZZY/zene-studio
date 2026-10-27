/*
 * AudioEngine.cpp - device-independent audio engine for LMMS
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

#include "AudioEngine.h"
#include <stdexcept>

#include "AutomatableModel.h"
#include "ControlRoom.h"
#include "RenderPeriodClock.h"
#include "InputMeter.h"
#include "PunchWindow.h"

#include "MixHelpers.h"

#include "lmmsconfig.h"

#include "AudioEngineWorkerThread.h"
#include "CrashReporter.h"
#include "AudioBusHandle.h"
#include "MpscQueue.h"
#include "AudioInputPath.h"
#include "Hardware.h"
#include "Mixer.h"
#include "Song.h"
#include "TunerTap.h"
#include "LinkAudioStream.h"
#include "EnvelopeAndLfoParameters.h"
#include "Engine.h"
#include "ExportRenderSettings.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "NotePlayHandle.h"
#include "RealtimeScope.h"
#include "ConfigManager.h"
#include "SamplePlayHandle.h"
#include "CompSlip.h"
#include "Track.h"

// platform-specific audio-interface-classes
#include "AudioAlsa.h"
#include "AudioJack.h"
#include "AudioOss.h"
#include "AudioSndio.h"
#include "AudioPortAudio.h"
#include "AudioSoundIo.h"
#include "AudioPulseAudio.h"
#include "AudioSdl.h"
#include "AudioDummy.h"

// platform-specific midi-interface-classes
#include "MidiAlsaRaw.h"
#include "MidiAlsaSeq.h"
#include "MidiJack.h"
#include "MidiOss.h"
#include "MidiSndio.h"
#include "MidiWinMM.h"
#include "MidiApple.h"
#include "MidiDummy.h"

#include "BufferManager.h"

#include <limits>
#include <memory>

namespace lmms
{

struct AudioEngine::AutomatableModelRegistry
{
	explicit AutomatableModelRegistry(std::size_t slotCount) : capacity(slotCount),
		modelSlots(std::make_unique<std::atomic<AutomatableModel*>[]>(slotCount))
	{
		for (std::size_t i = 0; i < capacity; ++i) { modelSlots[i].store(nullptr, std::memory_order_relaxed); }
	}

	std::size_t capacity;
	std::unique_ptr<std::atomic<AutomatableModel*>[]> modelSlots;
	static_assert(std::atomic<AutomatableModel*>::is_always_lock_free,
		"the render-owned model registry requires lock-free model pointers");
	static_assert(std::atomic<AutomatableModelRegistry*>::is_always_lock_free,
		"the render-owned model registry pointer must always be lock-free");
};

using LocklessListElement = LocklessList<PlayHandle*>::Element;

template <typename T, std::size_t Capacity>
class SpscQueue
{
	static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0);
public:
	bool push(T value) noexcept
	{
		const std::size_t write = m_write.load(std::memory_order_relaxed);
		const std::size_t read = m_read.load(std::memory_order_acquire);
		if (write - read == Capacity) { return false; }
		m_values[write & (Capacity - 1)] = value;
		m_write.store(write + 1, std::memory_order_release);
		return true;
	}
	bool pop(T* value) noexcept
	{
		const std::size_t read = m_read.load(std::memory_order_relaxed);
		if (read == m_write.load(std::memory_order_acquire)) { return false; }
		*value = m_values[read & (Capacity - 1)];
		m_read.store(read + 1, std::memory_order_release);
		return true;
	}
private:
	std::array<T, Capacity> m_values{};
	alignas(64) std::atomic<std::size_t> m_write{0};
	alignas(64) std::atomic<std::size_t> m_read{0};
};

namespace
{

constexpr qsizetype PlayHandleListLowWater = static_cast<qsizetype>(PlayHandle::MaxNumber / 4);
constexpr std::size_t PendingPlaybackStateCapacity = 2 * PlayHandle::MaxNumber + 1;
constexpr std::size_t RealtimePlaybackStatesPerPlayHandle = 1 + CompSlipReaders::MaxReaders;

auto realtimeVoiceConverterCapacity(qsizetype playHandleCapacity) noexcept -> std::size_t
{
	const std::size_t playHandleStates = static_cast<std::size_t>(playHandleCapacity) + PendingPlaybackStateCapacity;
	return playHandleStates * RealtimePlaybackStatesPerPlayHandle;
}

constexpr std::size_t MetronomeVoicePoolCapacity = AudioEngine::MetronomeSampleVoicePoolCapacity;
constexpr std::size_t SampleVoicePoolCapacity = MetronomeVoicePoolCapacity
	+ AudioEngine::FrozenSampleVoicePoolCapacity;
static_assert(SampleVoicePoolCapacity > MetronomeVoicePoolCapacity);

} // namespace

struct AudioEngine::SampleVoicePool
{
	struct Slot
	{
		Sample sample;
		SamplePlayHandle handle;
		Slot() : handle(&sample, false) {}
	};

	AudioBusHandle bus{"Pooled sample voices", false};
	std::array<Slot, SampleVoicePoolCapacity> m_voiceSlots{};
	SpscQueue<std::uint16_t, MetronomeVoicePoolCapacity> metronomeReady;
	SpscQueue<std::uint16_t, SampleVoicePoolCapacity> frozenReady;
	SpscQueue<std::uint16_t, SampleVoicePoolCapacity> renderRetired;
	MpscQueue<std::uint16_t, SampleVoicePoolCapacity> controlRetired;
	AudioEngine* engine;

	explicit SampleVoicePool(AudioEngine* owner) : engine(owner)
	{
		// The common bus feeds the mixer only; do not expose a permanent hardware output port for pool internals.
		bus.setExtOutputEnabled(false);
		for (std::size_t i = 0; i < m_voiceSlots.size(); ++i)
		{
			Slot& slot = m_voiceSlots[i];
			slot.handle.setAudioBusHandle(&bus);
			slot.handle.setPoolSlot(static_cast<std::uint16_t>(i), i >= MetronomeVoicePoolCapacity);
			const bool queued = i < MetronomeVoicePoolCapacity
				? metronomeReady.push(static_cast<std::uint16_t>(i))
				: frozenReady.push(static_cast<std::uint16_t>(i));
			if (!queued) { engine->m_sampleVoiceQueueInvariantFailureCount.fetch_add(1, std::memory_order_relaxed); }
		}
	}

	SamplePlayHandle* acquire(const std::shared_ptr<const SampleBuffer>& buffer, int sourceIn, int sourceOut,
		float amplification, f_cnt_t offset, Track* track, bool metronome) noexcept
	{
		std::uint16_t index = SamplePlayHandle::NotPooled;
		const bool found = metronome ? metronomeReady.pop(&index) : frozenReady.pop(&index);
		if (!found)
		{
			engine->recordSampleVoiceOverflow();
			return nullptr;
		}
		SamplePlayHandle* const handle = &m_voiceSlots[index].handle;
		handle->armPooledVoice(buffer, sourceIn, sourceOut, amplification, offset, track);
		return handle;
	}

	void retire(std::uint16_t index, bool realtime) noexcept
	{
		const bool queued = realtime ? renderRetired.push(index) : controlRetired.push(index);
		if (!queued) { engine->m_sampleVoiceQueueInvariantFailureCount.fetch_add(1, std::memory_order_relaxed); }
	}

	bool owns(const SamplePlayHandle* handle) const noexcept
	{
		const std::uint16_t index = handle->poolSlot();
		return index < m_voiceSlots.size() && &m_voiceSlots[index].handle == handle;
	}

	SamplePlayHandle* find(PlayHandle* candidate) noexcept
	{
		for (Slot& slot : m_voiceSlots)
		{
			if (static_cast<PlayHandle*>(&slot.handle) == candidate) { return &slot.handle; }
		}
		return nullptr;
	}

	bool ownsBus(const AudioBusHandle* candidate) const noexcept { return &bus == candidate; }

	auto prepareConverterMode(AudioResampler::Mode mode) noexcept -> bool
	{
		std::size_t prepared = 0;
		for (; prepared < m_voiceSlots.size(); ++prepared)
		{
			if (m_voiceSlots[prepared].handle.prepareConverterMode(mode)) { continue; }
			for (std::size_t i = 0; i < prepared; ++i)
			{
				m_voiceSlots[i].handle.discardPreparedConverterMode();
			}
			return false;
		}
		return true;
	}

	void adoptPreparedConverterMode() noexcept
	{
		for (Slot& slot : m_voiceSlots) { slot.handle.adoptPreparedConverterMode(); }
	}

	void discardPreparedConverterMode() noexcept
	{
		for (Slot& slot : m_voiceSlots) { slot.handle.discardPreparedConverterMode(); }
	}

	void service() noexcept
	{
		std::uint16_t index = 0;
		while (renderRetired.pop(&index) || controlRetired.pop(&index))
		{
			m_voiceSlots[index].handle.resetPooledVoice();
			const bool queued = index < MetronomeVoicePoolCapacity
				? metronomeReady.push(index) : frozenReady.push(index);
			if (!queued) { engine->m_sampleVoiceQueueInvariantFailureCount.fetch_add(1, std::memory_order_relaxed); }
		}
	}
};

SamplePlayHandle* AudioEngine::acquirePooledSampleVoice(const std::shared_ptr<const SampleBuffer>& buffer,
	int sourceIn, int sourceOut, float amplification, f_cnt_t offset, Track* track, bool metronome) noexcept
{
	if (!m_sampleVoicePool || buffer == nullptr || sourceIn < 0 || sourceOut <= sourceIn)
	{
		recordSampleVoiceOverflow();
		return nullptr;
	}
	return m_sampleVoicePool->acquire(buffer, sourceIn, sourceOut, amplification, offset, track, metronome);
}


auto AudioEngine::applyConverterModeToRetainedStates(AudioResampler::Mode mode,
	SrcQuality quality) noexcept -> bool
{
	const auto changeGuard = requestChangesGuard();
	if (!prepareConverterModeForRetainedStates(mode))
	{
		discardPreparedConverterModeForRetainedStates();
		m_converterModePropagationFailures.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	// The publication and all pointer swaps happen while renderNextPeriod is
	// excluded by the same guard. No failure remains after this store.
	ExportRenderSettings::commitProposedSrcQuality(quality);
	adoptPreparedConverterModeForRetainedStates();
	return true;
}


bool AudioEngine::prepareConverterModeForRetainedStates(AudioResampler::Mode mode) noexcept
{
	if (!AudioResampler::prepareRealtimeVoiceConverterPool(mode)) { return false; }
	if (m_sampleVoicePool != nullptr && !m_sampleVoicePool->prepareConverterMode(mode)) { return false; }

	Song* const song = Engine::getSong();
	if (song != nullptr && !song->prepareConverterModeForInstruments(mode)) { return false; }

	for (PlayHandle* const handle : m_playHandles)
	{
		if (!applyConverterModeToPlayHandle(handle, mode, ConverterModePhase::Prepare)) { return false; }
	}
	for (LocklessListElement* element = m_newPlayHandles.first(); element; element = element->next)
	{
		if (!applyConverterModeToPlayHandle(element->value, mode, ConverterModePhase::Prepare)) { return false; }
	}
	for (std::size_t i = 0; i < m_capacityDeferredPlayHandleCount; ++i)
	{
		if (!applyConverterModeToPlayHandle(m_capacityDeferredPlayHandles[i], mode,
			ConverterModePhase::Prepare)) { return false; }
	}
	return true;
}


void AudioEngine::adoptPreparedConverterModeForRetainedStates() noexcept
{
	if (m_sampleVoicePool != nullptr) { m_sampleVoicePool->adoptPreparedConverterMode(); }
	if (Song* const song = Engine::getSong()) { song->adoptPreparedConverterModeForInstruments(); }
	for (PlayHandle* const handle : m_playHandles)
	{
		applyConverterModeToPlayHandle(handle, AudioResampler::Mode::Linear, ConverterModePhase::Adopt);
	}
	for (LocklessListElement* element = m_newPlayHandles.first(); element; element = element->next)
	{
		applyConverterModeToPlayHandle(element->value, AudioResampler::Mode::Linear, ConverterModePhase::Adopt);
	}
	for (std::size_t i = 0; i < m_capacityDeferredPlayHandleCount; ++i)
	{
		applyConverterModeToPlayHandle(m_capacityDeferredPlayHandles[i], AudioResampler::Mode::Linear,
			ConverterModePhase::Adopt);
	}
	AudioResampler::adoptRealtimeVoiceConverterPool();
}


void AudioEngine::discardPreparedConverterModeForRetainedStates() noexcept
{
	if (m_sampleVoicePool != nullptr) { m_sampleVoicePool->discardPreparedConverterMode(); }
	if (Song* const song = Engine::getSong()) { song->discardPreparedConverterModeForInstruments(); }
	for (PlayHandle* const handle : m_playHandles)
	{
		applyConverterModeToPlayHandle(handle, AudioResampler::Mode::Linear, ConverterModePhase::Discard);
	}
	for (LocklessListElement* element = m_newPlayHandles.first(); element; element = element->next)
	{
		applyConverterModeToPlayHandle(element->value, AudioResampler::Mode::Linear, ConverterModePhase::Discard);
	}
	for (std::size_t i = 0; i < m_capacityDeferredPlayHandleCount; ++i)
	{
		applyConverterModeToPlayHandle(m_capacityDeferredPlayHandles[i], AudioResampler::Mode::Linear,
			ConverterModePhase::Discard);
	}
	AudioResampler::discardRealtimeVoiceConverterPool();
}


auto AudioEngine::applyConverterModeToPlayHandle(PlayHandle* handle, AudioResampler::Mode mode,
	ConverterModePhase phase) noexcept -> bool
{
	if (handle == nullptr) { return true; }
	if (auto* const sampleHandle = dynamic_cast<SamplePlayHandle*>(handle))
	{
		if (sampleHandle->isPooledVoice()) { return true; }
		switch (phase)
		{
		case ConverterModePhase::Prepare: return sampleHandle->prepareConverterMode(mode);
		case ConverterModePhase::Adopt: sampleHandle->adoptPreparedConverterMode(); return true;
		case ConverterModePhase::Discard: sampleHandle->discardPreparedConverterMode(); return true;
		}
		return true;
	}
	if (auto* const note = dynamic_cast<NotePlayHandle*>(handle))
	{
		InstrumentTrack* const track = note->instrumentTrack();
		Instrument* const instrument = track != nullptr ? track->instrument() : nullptr;
		if (instrument == nullptr) { return true; }
		switch (phase)
		{
		case ConverterModePhase::Prepare:
			return instrument->prepareConverterModeForNote(note, mode);
		case ConverterModePhase::Adopt:
			instrument->adoptPreparedConverterModeForNote(note);
			return true;
		case ConverterModePhase::Discard:
			instrument->discardPreparedConverterModeForNote(note);
			return true;
		}
	}
	return true;
}


auto AudioEngine::onSrcQualityChanged(SrcQuality quality) noexcept -> bool
{
	AudioEngine* const engine = Engine::audioEngine();
	if (engine == nullptr)
	{
		ExportRenderSettings::commitProposedSrcQuality(quality);
		return true;
	}
	return engine->applyConverterModeToRetainedStates(AudioResampler::modeForSrcQuality(quality), quality);
}



void AudioEngine::recyclePooledSampleVoice(SamplePlayHandle* handle, bool realtime) noexcept
{
	if (m_sampleVoicePool == nullptr || handle == nullptr || !handle->isPooledVoice()
		|| !m_sampleVoicePool->owns(handle))
	{
		m_sampleVoiceQueueInvariantFailureCount.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	if (Track* const track = handle->pooledTrack()) { track->frozenVoiceFinished(); }
	m_sampleVoicePool->retire(handle->poolSlot(), realtime);
}



void AudioEngine::serviceSampleVoicePoolOnControlThread() noexcept
{
	if (!RealtimeScope::isActive() && QThread::currentThread() == thread() && m_sampleVoicePool != nullptr)
	{
		const auto changeGuard = requestChangesGuard();
		m_sampleVoicePool->service();
	}
}



bool AudioEngine::isSampleVoicePoolBus(const AudioBusHandle* handle) const noexcept
{
	return m_sampleVoicePool != nullptr && m_sampleVoicePool->ownsBus(handle);
}



void AudioEngine::shutdownSampleVoicePool() noexcept
{
	m_sampleVoicePool.reset();
}



void AudioEngine::disposePlayHandle(PlayHandle* handle, bool realtime) noexcept
{
	if (m_sampleVoicePool != nullptr)
	{
		if (SamplePlayHandle* const pooled = m_sampleVoicePool->find(handle))
		{
			recyclePooledSampleVoice(pooled, realtime);
			return;
		}
	}
	if (handle->type() == PlayHandle::Type::NotePlayHandle)
	{
		NotePlayHandleManager::release(static_cast<NotePlayHandle*>(handle));
	}
	else { delete handle; }
}

static thread_local bool s_renderingThread = false;

#ifdef LMMS_TESTING
thread_local int AudioEngine::s_changeInModelDepth = 0;
#endif

AudioEngine::AudioEngine(bool renderOnly)
	: m_renderOnly(renderOnly)
	, m_framesPerAudioBuffer(std::clamp(
		static_cast<f_cnt_t>(ConfigManager::inst()->value("audioengine", "framesperaudiobuffer",
		QString::number(DEFAULT_BUFFER_SIZE)).toUInt()),
		MINIMUM_BUFFER_SIZE, MAXIMUM_BUFFER_SIZE))
	, m_framesPerPeriod(std::min(m_framesPerAudioBuffer, DEFAULT_BUFFER_SIZE))
	, m_baseSampleRate(
		std::max(ConfigManager::inst()->value("audioengine", "samplerate").toInt(), SUPPORTED_SAMPLERATES.front()))
	, m_inputBufferRead(0)
	, m_inputBufferWrite(1)
	// The recorder's width follows the CONFIGURED input count (feature row 64):
	// every route can select any of the configured input channels - that is the
	// arbitrary input count - while the number of ROUTES is its own bound
	// (MultiTrackRecorder::MaxRoutes), because a route is a file and several
	// routes may record one channel. Read from the config file here because it
	// needs no device to be open: the routes exist whether or not an interface
	// is plugged in.
	, m_recorder(MultiTrackRecorder::MaxRoutes, AudioInputPath::configuredChannelCount())
	, m_outputBufferRead(nullptr)
	, m_outputBufferWrite(nullptr)
	, m_outputBufferReadIndex(0)
	, m_workers()
	, m_numWorkers(QThread::idealThreadCount() - 1)
	, m_newPlayHandles(PlayHandle::MaxNumber)
	, m_masterGain(1.0f)
	, m_audioDev(nullptr)
	, m_oldAudioDev(nullptr)
	, m_audioDevStartFailed(false)
	, m_profiler()
	, m_clearSignal(false)
	, m_sanitizationEnabled(ConfigManager::inst()->value("audioengine", "sanitizemix", "1").toInt())
{
	for( int i = 0; i < 2; ++i )
	{
		m_inputBufferFrames[i] = 0;
		m_inputBufferSize[i] = InputStageCapacityFrames;
		m_inputBuffer[i] = new SampleFrame[ InputStageCapacityFrames ];
		zeroSampleFrames(m_inputBuffer[i], m_inputBufferSize[i]);
	}
	// The first bus is already sufficient for a full bounded polyphony batch;
	// addAudioBusHandle() grows this list on the control thread as buses appear.
	m_playHandles.reserve(static_cast<qsizetype>(PlayHandle::MaxNumber));

	// Capture input staging (see pushInputFrames/drainInputStage). Allocated
	// exactly once, here, off the audio thread and never resized afterwards.
	m_inputStage = std::make_unique<SampleFrameRingBuffer>(InputStageCapacityFrames);

	// The live loudness tap (feature row 24): ONE construction, here, with the
	// engine's processing rate - the rate every period of the master mix is at,
	// whatever rate the audio device then resamples to. Nothing allocates it
	// later and nothing replaces it, so arming it can never race a render.
	m_masterLoudness = std::make_unique<MasterLoudnessTap>(m_baseSampleRate, DEFAULT_CHANNELS);
	// The N-CHANNEL capture staging stage (0.3.0, feature row 64). Same rule as
	// the stereo ring above: allocated once, here, and drained once per rendered
	// period. Its width is the widest block the path will accept, and its
	// capacity is the same bound the stereo stage uses, so the two input paths
	// hold the same amount of audio at most.
	m_wideInputStage = std::make_unique<AudioWideInputStage>(AudioInputPath::MaxChannels,
		InputStageCapacityFrames);

	BufferManager::init( m_framesPerPeriod );
	m_outputBufferRead = std::make_unique<SampleFrame[]>(m_framesPerPeriod);
	m_outputBufferWrite = std::make_unique<SampleFrame[]>(m_framesPerPeriod);
	m_hardwareOut.init(m_framesPerPeriod);  // 0.5.0 F1 (MX-040): allocated once, here, never on the render path


	for( int i = 0; i < m_numWorkers+1; ++i )
	{
		auto wt = new AudioEngineWorkerThread(this);
		wt->setRenderSlot(i);
		if( i < m_numWorkers )
		{
			wt->start( QThread::TimeCriticalPriority );
		}
		m_workers.push_back( wt );
	}
	AudioEngineWorkerThread::setInlineRenderSlot(m_numWorkers);
	connect(&m_playHandleCapacityTimer, &QTimer::timeout, this, [this] {
		m_recorder.reconcileInputChannelCapacity();
		serviceSampleVoicePoolOnControlThread();
		topUpPlayHandleCapacityOnControlThread();
		// Q050-016 round 3: free the expression-stream snapshots the render path
		// retired (a playing handle held the last reference) - here, off it.
		MpeStreamSnapshot::reclaimRetired();
	});
	m_playHandleCapacityTimer.start(100);
}




int AudioEngine::renderThreadCount() const noexcept
{
	return m_numWorkers + 1;
}


AudioEngine::~AudioEngine()
{
	ExportRenderSettings::setSrcQualityListener(nullptr);
	m_playHandleCapacityTimer.stop();
	for( int w = 0; w < m_numWorkers; ++w )
	{
		m_workers[w]->quit();
	}

	AudioEngineWorkerThread::startAndWaitForJobs();

	// Join every worker without a deadline. A deadline is what crashes here: with
	// wait(500) a worker that has not finished winding down when the budget expires
	// is left running, and the QThread objects are children of this QObject
	// (AudioEngineWorkerThread::AudioEngineWorkerThread passes the engine as the
	// QThread parent), so ~QObject deletes them while their threads run and Qt
	// answers with
	//   qFatal("QThread: Destroyed while thread is still running")
	// i.e. SIGABRT inside Engine::destroy(). Measured on this tree: 13 of 64
	// PdcMixerTest runs aborted in cleanupTestCase() at load average ~27, with
	// exactly one worker of QThread::idealThreadCount()-1 still parked in
	// QWaitCondition::wait(). wait() terminates because each worker re-checks
	// m_quit within AudioEngineWorkerThread's quit-recheck interval, so a lost
	// wake-up delays the join by that interval instead of stranding the thread.
	// The trade this makes explicit: if a job itself never returns, shutdown now
	// blocks until it does, where before it aborted the process.
	for( int w = 0; w < m_numWorkers; ++w )
	{
		m_workers[w]->wait();
	}

	// The pooled bus unregisters its device port in its destructor, so it must go before the device.
	m_sampleVoicePool.reset();
	delete m_midiClient;
	delete m_audioDev;


	for (const auto& input : m_inputBuffer)
	{
		delete[] input;
	}
	delete m_automatableModelRegistry.exchange(nullptr, std::memory_order_acq_rel);
	// Q050-016 round 3: the workers are joined, so nothing can copy a stream now.
	MpeStreamSnapshot::reclaimRetired();
}



bool AudioEngine::isRenderThread() const noexcept
{
	return s_renderingThread;
}



void AudioEngine::setAutomatableModelRegistrationFailureForTest(bool fail) noexcept
{
	m_failAutomatableModelRegistrationForTest.store(fail, std::memory_order_relaxed);
}


void AudioEngine::setAutomatableModelRegistrationAttemptStateForTest(std::atomic<int>* state) noexcept
{
	m_automatableModelRegistrationAttemptStateForTest.store(state, std::memory_order_release);
}


bool AudioEngine::registerAutomatableModel(AutomatableModel* model) noexcept
{
	if (model == nullptr) { return false; }
	if (m_failAutomatableModelRegistrationForTest.load(std::memory_order_relaxed))
	{
		m_automatableModelRegistrationFailures.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	if (RealtimeScope::isActive())
	{
		m_automatableModelRegistrationFailures.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	if (std::atomic<int>* const attemptState =
		m_automatableModelRegistrationAttemptStateForTest.load(std::memory_order_acquire))
	{
		int expected = 0;
		attemptState->compare_exchange_strong(expected, 1, std::memory_order_release,
			std::memory_order_relaxed);
		attemptState->notify_all();
	}

	const auto lock = std::lock_guard{m_changeMutex};
	if (model->m_registeredWithAudioEngine.load(std::memory_order_relaxed)) { return true; }

	AutomatableModelRegistry* registry = m_automatableModelRegistry.load(std::memory_order_acquire);
	std::size_t freeSlot = registry != nullptr ? registry->capacity : 0;
	if (registry != nullptr)
	{
		for (std::size_t i = 0; i < registry->capacity; ++i)
		{
			if (registry->modelSlots[i].load(std::memory_order_relaxed) == nullptr)
			{
				freeSlot = i;
				break;
			}
		}
	}

	if (freeSlot == (registry != nullptr ? registry->capacity : 0))
	{
		const std::size_t oldCapacity = registry != nullptr ? registry->capacity : 0;
		if (oldCapacity > std::numeric_limits<std::size_t>::max() / 2)
		{
			m_automatableModelRegistrationFailures.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		const std::size_t newCapacity = oldCapacity == 0 ? 64 : oldCapacity * 2;
		try
		{
			auto replacement = std::make_unique<AutomatableModelRegistry>(newCapacity);
			if (registry != nullptr)
			{
				for (std::size_t i = 0; i < registry->capacity; ++i)
				{
					replacement->modelSlots[i].store(
						registry->modelSlots[i].load(std::memory_order_relaxed), std::memory_order_relaxed);
				}
			}
			freeSlot = oldCapacity;
			replacement->modelSlots[freeSlot].store(model, std::memory_order_relaxed);
			model->m_audioEngineRegistrySlot = freeSlot;
			model->m_registeredWithAudioEngine.store(true, std::memory_order_release);
			AutomatableModelRegistry* const retired = m_automatableModelRegistry.exchange(
				replacement.release(), std::memory_order_acq_rel);
			delete retired; // m_changeMutex has waited for the previous render period to quiesce.
		}
		catch (...)
		{
			m_automatableModelRegistrationFailures.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
	}
	else
	{
		registry->modelSlots[freeSlot].store(model, std::memory_order_release);
		model->m_audioEngineRegistrySlot = freeSlot;
		model->m_registeredWithAudioEngine.store(true, std::memory_order_release);
	}
	m_registeredAutomatableModelCount.fetch_add(1, std::memory_order_relaxed);
	return true;
}



void AudioEngine::unregisterAutomatableModel(AutomatableModel* model) noexcept
{
	if (model == nullptr || !model->m_registeredWithAudioEngine.load(std::memory_order_acquire)) { return; }

	const auto remove = [this, model]() noexcept {
		AutomatableModelRegistry* const registry = m_automatableModelRegistry.load(std::memory_order_acquire);
		const std::size_t slot = model->m_audioEngineRegistrySlot;
		if (registry != nullptr && slot < registry->capacity)
		{
			AutomatableModel* expected = model;
			if (registry->modelSlots[slot].compare_exchange_strong(expected, nullptr,
				std::memory_order_acq_rel, std::memory_order_relaxed))
			{
				m_registeredAutomatableModelCount.fetch_sub(1, std::memory_order_relaxed);
			}
		}
		model->m_audioEngineRegistrySlot = AutomatableModel::NotInAudioEngineRegistry;
		model->m_registeredWithAudioEngine.store(false, std::memory_order_release);
	};

	if (RealtimeScope::isActive())
	{
		remove(); // the serial prepass is over; a tombstone keeps the slot array stable.
		return;
	}
	const auto lock = std::lock_guard{m_changeMutex};
	remove();
}



void AudioEngine::prepareAutomatableModelBuffers() noexcept
{
	AutomatableModelRegistry* const registry = m_automatableModelRegistry.load(std::memory_order_acquire);
	if (registry == nullptr) { return; }
	for (std::size_t i = 0; i < registry->capacity; ++i)
	{
		if (AutomatableModel* const model = registry->modelSlots[i].load(std::memory_order_acquire))
		{
			model->prepareValueBufferForRender();
		}
	}
}




void AudioEngine::initDevices()
{
	if (!AudioResampler::initializeRealtimeVoiceConverterPool(AudioResampler::modeForCurrentSrcQuality(),
		realtimeVoiceConverterCapacity(m_playHandles.capacity())))
	{
		throw std::runtime_error{"Unable to prepare realtime sample converters"};
	}
	bool success_ful = false;
	if( m_renderOnly ) {
		m_audioDev = new AudioDummy( success_ful, this );
		m_audioDevName = AudioDummy::name();
		m_midiClient = new MidiDummy;
		m_midiClientName = MidiDummy::name();
	} else {
		m_audioDev = tryAudioDevices();
		m_midiClient = tryMidiClients();
	}
	// Loading audio device may have changed the sample rate
	m_sampleVoicePool = std::make_unique<SampleVoicePool>(this);
	ExportRenderSettings::setSrcQualityListener(&AudioEngine::onSrcQualityChanged);
	emit sampleRateChanged();
}

bool AudioEngine::criticalXRuns() const
{
	return m_overloadShedding.load(std::memory_order_relaxed) && cpuLoad() >= 99
		&& Engine::getSong()->isExporting() == false;
}




void AudioEngine::pushInputFrames( const SampleFrame* _ab, const f_cnt_t _frames ) noexcept
{
	if( _ab == nullptr || _frames == 0 || m_inputStage == nullptr )
	{
		return;
	}

	// Realtime-safe by construction. This runs on the backend's capture thread
	// - the JACK process callback or the SDL capture callback - so it must not
	// lock, allocate or grow.
	//
	// It used to call requestChangeInModel(), which takes m_changeMutex and can
	// therefore block behind any GUI thread that holds the model lock, and it
	// grew m_inputBuffer by doubling (new SampleFrame[size]) whenever the
	// backlog did not fit. Both are gone: the frames go into one
	// fixed-capacity ring, allocated once in the constructor, which
	// drainInputStage() empties once per rendered period. A full ring drops
	// the newest frames and counts them (inputFramesDropped()).
	m_inputStage->writeBlock( _ab, static_cast<std::size_t>( _frames ) );
}



void AudioEngine::refreshRecordingLatency()
{
	// Four terms: the device's two latencies, one capture block of input staging, and one
	// engine period of OUTPUT staging - renderNextPeriod() hands the device the period it
	// rendered the time before (the output double buffer swapBuffers() flips). The fourth was
	// found by measurement: RecordingLatencyTest was one period late without it.
	m_recordingLatency.store(m_audioDev == nullptr ? 0
		: m_audioDev->inputLatencyFrames() + m_audioDev->outputLatencyFrames()
			+ m_audioDev->captureBlockFrames() + m_framesPerPeriod,
		std::memory_order_relaxed);
}



void AudioEngine::drainCaptureStages() noexcept
{
	if( m_inputStage == nullptr )
	{
		return;
	}

	// The N-CHANNEL path and the stereo path are TWO REPRESENTATIONS OF THE SAME
	// CAPTURE BLOCK (cluster 3705). The publisher pushes the wide block first and
	// the stereo pair second, so a render that landed between the two pushes used
	// to consume one representation now and the other in the next period: the
	// recorder took the wide block in one period and fell back to the same stereo
	// block in the next - the same audio twice, and the stereo bus a whole block
	// behind the recorders.
	//
	// So the two stages are drained AS ONE BLOCK: as many frames as BOTH of them
	// hold. A block whose stereo pair has not been published yet waits for it
	// instead of being consumed on its own. A capture path that does not use the
	// wide path at all (a device that only fills the stereo ring) drains the
	// stereo ring on its own, exactly as it always did.
	const auto stereoStaged = m_inputStage->available();
	const auto wideStaged = m_wideInputStage != nullptr
		? m_wideInputStage->stagedFrames() : std::size_t{0};
	const bool wideInUse = m_captureWideInUse.load(std::memory_order_relaxed);
	const auto paired = wideInUse ? std::min( wideStaged, stereoStaged ) : stereoStaged;

	// Render thread only, and bounded by both the ring and the destination
	// buffer, so this can neither allocate nor overrun m_inputBuffer.
	const auto frames = std::min( paired,
		static_cast<std::size_t>( m_inputBufferSize[ m_inputBufferRead ] ) );
	m_inputStage->read( m_inputBuffer[ m_inputBufferRead ], frames );
	m_inputBufferFrames[ m_inputBufferRead ] = static_cast<f_cnt_t>( frames );
	if( frames > 0 ) { m_inputStreamStarted.store( true, std::memory_order_relaxed ); }

	// The wide path takes the SAME number of frames, so the two stay one block.
	// Without the wide path the stage drains on its own, as it always did.
	if( m_wideInputStage != nullptr )
	{
		m_wideInputStage->drain( wideInUse ? static_cast<f_cnt_t>( frames )
			: static_cast<f_cnt_t>( wideStaged ) );
	}
}




PunchFrames AudioEngine::punchWindow(f_cnt_t frames) const noexcept
{
	const std::uint64_t punch = m_punch.load(std::memory_order_relaxed);
	const tick_t begin = punchBeginOf(punch);
	const tick_t end = punchEndOf(punch);
	return punchFramesForPeriod(end > begin, m_periodSongPlaying, m_periodStartFrame, Engine::framesPerTick(), frames, begin, end);
}

void AudioEngine::setPunchWindow(bool armed, tick_t begin, tick_t end) noexcept
{
	// One word: a render thread reads either the old or the new window whole, never
	// the armed flag or one bound of one publish with the rest of another.
	const bool on = armed && end > begin;
	const std::uint64_t punch = on ? (std::uint64_t{static_cast<std::uint32_t>(begin)} << 32) | static_cast<std::uint32_t>(end) : 0;
	m_punch.store(punch, std::memory_order_relaxed);
}




std::size_t AudioEngine::pushInputFramesWide( const float* _interleaved, int _channels,
	const f_cnt_t _frames ) noexcept
{
	// The N-CHANNEL sibling of pushInputFrames() (0.3.0, feature row 64). It
	// runs on the same backend capture thread and keeps the same contract: the
	// frames go into one fixed-capacity ring that swapBuffers() drains once per
	// rendered period, so it can neither lock, nor allocate, nor grow, and a
	// full ring drops the newest frames and counts them.
	if( m_wideInputStage == nullptr )
	{
		return 0;
	}
	// From here on the capture path publishes BOTH representations of every
	// block - this one and the stereo pair - so the engine drains the two
	// stages as one block (cluster 3705).
	m_captureWideInUse.store( true, std::memory_order_relaxed );
	return m_wideInputStage->push( _interleaved, _channels, _frames );
}




const float* AudioEngine::inputWideBuffer() const noexcept
{
	return m_wideInputStage != nullptr ? m_wideInputStage->data() : nullptr;
}




f_cnt_t AudioEngine::inputWideFrames() const noexcept
{
	return m_wideInputStage != nullptr ? m_wideInputStage->frames() : 0;
}




int AudioEngine::inputWideChannels() const noexcept
{
	return m_wideInputStage != nullptr ? m_wideInputStage->channels() : 0;
}




std::size_t AudioEngine::inputWideFramesStaged() const noexcept
{
	return m_wideInputStage != nullptr ? m_wideInputStage->stagedFrames() : 0u;
}




std::uint64_t AudioEngine::inputWideFramesDropped() const noexcept
{
	return m_wideInputStage != nullptr ? m_wideInputStage->droppedFrames() : 0u;
}



std::size_t AudioEngine::inputFramesStaged() const noexcept
{
	return m_inputStage != nullptr ? m_inputStage->available() : 0u;
}



std::uint64_t AudioEngine::inputFramesDropped() const noexcept
{
	return m_inputStage != nullptr ? m_inputStage->overflowCount() : 0u;
}



void AudioEngine::renderStageNoteSetup()
{
	AudioBusHandle::RenderStageScope ownsBusPlayHandleLists;
	AudioEngineProfiler::Probe profilerProbe(m_profiler, AudioEngineProfiler::DetailType::NoteSetup);

	if( m_clearSignal )
	{
		m_clearSignal = false;
		clearInternal();
	}

	// remove all play-handles that have to be deleted and delete
	// them if they still exist...
	// maybe this algorithm could be optimized...
	ConstPlayHandleList::Iterator it_rem = m_playHandlesToRemove.begin();
	while( it_rem != m_playHandlesToRemove.end() )
	{
		PlayHandle* const handle = const_cast<PlayHandle*>(*it_rem);
		PlayHandleList::Iterator it = std::find( m_playHandles.begin(), m_playHandles.end(), handle );

		if( it != m_playHandles.end() )
		{
			handle->audioBusHandle()->removePlayHandle(handle);
			disposePlayHandle(handle, true);
			m_playHandles.erase(it);
		}
		else if (removeCapacityDeferredPlayHandle(handle))
		{
			handle->audioBusHandle()->removePlayHandleUnderEngineChangeLock(handle);
			disposePlayHandle(handle, true);
		}

		it_rem = m_playHandlesToRemove.erase( it_rem );
	}

	swapBuffers();

	// prepare master mix (clear internal buffers etc.)
	Mixer * mixer = Engine::mixer();
	mixer->prepareMasterMix();

	// R2.4: where this period starts, for STAGE 4's punch gate - taken BEFORE
	// the song advances. Render thread; the song position is this thread's own.
	{
		const Song* song = Engine::getSong();
		m_periodSongPlaying = song->isPlaying() && song->playMode() == Song::PlayMode::Song;
		m_periodStartFrame = static_cast<double>(song->getFrames());
	}

	// Retry engine-list additions deferred by capacity in the previous period.
	std::size_t remainingDeferred = 0;
	for (std::size_t i = 0; i < m_capacityDeferredPlayHandleCount; ++i)
	{
		PlayHandle* const handle = m_capacityDeferredPlayHandles[i];
		if (!appendPlayHandleWithoutGrowth(handle))
		{
			m_capacityDeferredPlayHandles[remainingDeferred++] = handle;
		}
	}
	m_capacityDeferredPlayHandleCount = remainingDeferred;

	// Create play-handles for new notes, samples etc. The owner drains the staging list below.
	Engine::getSong()->processNextBuffer();
	// Song automation has now published this period's model values and ramps.
	// Prepare every registered model and reachable controller once, serially,
	// before instrument, effect or mixer jobs can read their fixed buffers.
	prepareAutomatableModelBuffers();

	// add all play-handles that have to be added
	for( LocklessListElement * e = m_newPlayHandles.popList(); e; )
	{
		PlayHandle* const handle = e->value;
		if (!appendPlayHandleWithoutGrowth(handle))
		{
			m_capacityDeferredPlayHandleCountTotal.fetch_add(1, std::memory_order_relaxed);
			if (m_capacityDeferredPlayHandleCount < m_capacityDeferredPlayHandles.size())
			{
				m_capacityDeferredPlayHandles[m_capacityDeferredPlayHandleCount++] = handle;
			}
			else
			{
				m_droppedPlayHandleCount.fetch_add(1, std::memory_order_relaxed);
				handle->audioBusHandle()->removePlayHandle(handle);
				disposePlayHandle(handle, true);
			}
		}
		LocklessListElement * next = e->next;
		m_newPlayHandles.free( e );
		e = next;
	}
}



void AudioEngine::renderStageInstruments()
{
	AudioEngineProfiler::Probe profilerProbe(m_profiler, AudioEngineProfiler::DetailType::Instruments);

	AudioEngineWorkerThread::fillJobQueue(m_playHandles);
	AudioEngineWorkerThread::startAndWaitForJobs();
}



void AudioEngine::renderStageEffects()
{
	AudioEngineProfiler::Probe profilerProbe(m_profiler, AudioEngineProfiler::DetailType::Effects);

	// STAGE 2: process effects of all instrument- and sampletracks
	AudioEngineWorkerThread::fillJobQueue(m_audioBusHandles);
	AudioEngineWorkerThread::startAndWaitForJobs();

	// removed all play handles which are done
	AudioBusHandle::RenderStageScope ownsBusPlayHandleLists;
	for( PlayHandleList::Iterator it = m_playHandles.begin();
						it != m_playHandles.end(); )
	{
		if( ( *it )->affinityMatters() &&
			( *it )->affinity() != QThread::currentThread() )
		{
			++it;
			continue;
		}
		if( ( *it )->isFinished() )
		{
			(*it)->audioBusHandle()->removePlayHandle(*it);
			disposePlayHandle(*it, true);
			it = m_playHandles.erase(it);
		}
		else
		{
			++it;
		}
	}
}



void AudioEngine::renderStageMix()
{
	AudioEngineProfiler::Probe profilerProbe(m_profiler, AudioEngineProfiler::DetailType::Mixing);

	Mixer *mixer = Engine::mixer();
	mixer->masterMix(m_outputBufferWrite.get());

	MixHelpers::multiply(m_outputBufferWrite.get(), m_masterGain, m_framesPerPeriod);

	// Q050-024: the Link audio stream taps the master (send), then adds the peer's stream (receive).
	// One acquire load while it is off; otherwise only preallocated rings and atomics (LinkAudioStream.h).
	link::LinkAudioStream::tap(m_outputBufferWrite.get(), m_framesPerPeriod, outputSampleRate());

	// STAGE 3b (feature row 24 of docs/FEATURE-LIST-0.3.0.md): the PASSIVE
	// loudness tap. The period that was just mixed is handed to the meter -
	// read only, in place, with no copy. A disarmed tap (the default, and the
	// state of every engine that never asked for a live loudness reading)
	// returns on its first load and the mixed buffer is not even inspected, so
	// this is one relaxed atomic load per period for a render that does not use
	// it. Nothing here allocates, locks or grows anything (AGENTS.md rule 4);
	// the meter itself was constructed once, with this engine.
	if( m_masterLoudness != nullptr )
	{
		m_masterLoudness->feed( m_outputBufferWrite.get(), m_framesPerPeriod );
	}
	// 0.5.0 W3 (WF-147): the tuner's tap on the master - one relaxed load unless it listens here (TunerTap.h).
	TunerTap::instance().push( TunerTap::Source::Master, m_outputBufferWrite.get(), m_framesPerPeriod );

	emit nextAudioBuffer(m_outputBufferRead.get());

	// and trigger LFOs
	EnvelopeAndLfoParameters::instances()->trigger();
	Controller::triggerFrameCounter();
	AutomatableModel::incrementPeriodCounter();
}



std::span<const SampleFrame> AudioEngine::renderNextPeriod()
{
	crashreporter::installAlternateStackForCurrentThread();
	const auto lock = std::lock_guard{m_changeMutex};
	// R7.2: under WANT_DEBUG_RTSAN everything from here to the return is checked as realtime code.
	const RealtimeScope realtime;

	RenderPeriodClock::markPeriodStart();
	// Ends the clock's period on every return path of this function.
	struct PeriodEnd
	{
		~PeriodEnd() { RenderPeriodClock::markPeriodEnd(); }
	} periodEnd;
	m_profiler.startPeriod();
	s_renderingThread = true;

	renderStageNoteSetup();     // STAGE 0: clear old play handles and buffers, setup new play handles
	renderStageInstruments();   // STAGE 1: run and render all play handles
	renderStageEffects();       // STAGE 2: process effects of all instrument- and sampletracks
	renderStageMix();           // STAGE 3: do master mix in mixer

	// STAGE 4 (prototype, task #556; N-channel since 0.3.0): demux the engine
	// input buffer into the armed per-route recorders. Realtime-safe by
	// contract: the recorders only touch pre-allocated SPSC ring buffers here.
	//
	// WHICH BUFFER. When the backend delivers an N-CHANNEL block (feature row
	// 64 - AudioAlsa's capture path, or the JACK and SDL backends, each of which
	// only has the wide path when the configuration asks for more than the
	// stereo bus), the recorders read that: it is what lets a route select an
	// input channel above 1. Otherwise the stereo bus is what they read, which
	// is the shape every route had before the wide path existed. Under a backend
	// with NO capture path at all the stereo bus is empty
	// (inputBufferFrames() == 0) and every armed route records nothing, which is
	// exactly the state docs/RECORDING-REALTIME-FIXES.md describes.
	//
	// THE PUNCH GATE (R2.4): with a punch region armed, only the frames of this
	// period whose song position is inside [punch-in, punch-out) reach the
	// recorders - frame-accurately, so a region that starts mid-period starts
	// mid-period. No armed region: every frame, the pre-gate behaviour. Pure
	// arithmetic on atomics (include/PunchWindow.h): no lock, no allocation.
	const auto gate = [this](f_cnt_t frames) { return punchWindow(frames); };
	if (m_wideInputStage != nullptr && m_wideInputStage->frames() > 0
		&& m_wideInputStage->channels() > 0)
	{
		const int channels = m_wideInputStage->channels();
		const PunchFrames window = gate(m_wideInputStage->frames());
		if (window.count() > 0)
		{
			m_recorder.processInputInterleaved(m_wideInputStage->data() + window.begin * channels,
				channels, window.count());
		}
	}
	else
	{
		const PunchFrames window = gate(m_inputBufferFrames[m_inputBufferRead]);
		if (window.count() > 0)
		{
			m_recorder.processInput(m_inputBuffer[m_inputBufferRead] + window.begin, window.count());
		}
	}

	// STAGE 4b: the retrospective AUDIO window (feature row 16, the audio
	// counterpart of the MIDI capture in include/RetroMidiCapture.h). It records
	// the ENGINE'S STEREO INPUT BUS - the same frames STAGE 1's play handles
	// read - and while it is disarmed this is one relaxed atomic load. Realtime-safe.
	m_retroCapture.push(m_inputBuffer[m_inputBufferRead], m_inputBufferFrames[m_inputBufferRead],
		static_cast<int>(inputSampleRate()));
	// 0.5.0 L10 (Q050-156): the input a sampler (AudioFileProcessor / SlicerT) is recording
	// straight into. While idle this is one relaxed atomic load; the accumulator's ring is
	// pre-allocated by the control-thread arm(), never here. Realtime-safe.
	m_samplerTakeRecorder.push(m_inputBuffer[m_inputBufferRead], m_inputBufferFrames[m_inputBufferRead]);
	inputmeter::measure(m_inputBuffer[m_inputBufferRead], m_inputBufferFrames[m_inputBufferRead]);  // 0.5.0 L08 (MX-134)
	// Q050-340: each capture channel's own peak - the wide capture when the backend fills it, else the stereo bus.
	if (m_wideInputStage != nullptr && m_wideInputStage->frames() > 0 && m_wideInputStage->channels() > 0)
	{
		inputmeter::measureChannels(m_wideInputStage->data(), m_wideInputStage->channels(), m_wideInputStage->frames());
	}
	else { inputmeter::measureStereoChannels(m_inputBuffer[m_inputBufferRead], m_inputBufferFrames[m_inputBufferRead]); }
	// 0.5.0 L09 (MX-038): the control room on what is heard - skipped while exporting and while neutral.
	controlroom::apply(m_outputBufferRead.get(), m_framesPerPeriod);
	// 0.5.0 W3 (WF-147): the tuner's tap on the input bus - one relaxed load unless it listens here.
	TunerTap::instance().push(TunerTap::Source::Input, m_inputBuffer[m_inputBufferRead], m_inputBufferFrames[m_inputBufferRead]);

	s_renderingThread = false;
	m_profiler.finishPeriod(outputSampleRate(), m_framesPerPeriod);
	m_outputBufferReadIndex = 0;

	return {m_outputBufferRead.get(), m_framesPerPeriod};
}

void AudioEngine::swapBuffers()
{
	m_inputBufferWrite = (m_inputBufferWrite + 1) % 2;
	m_inputBufferRead = (m_inputBufferRead + 1) % 2;
	m_inputBufferFrames[m_inputBufferWrite] = 0;

	// Move whatever the capture thread staged into this period's read side.
	// Done here - once per rendered period, before any play handle reads
	// inputBuffer() (STAGE 1) or the recorder demuxes it (STAGE 4) - so the
	// capture thread never touches m_inputBuffer and needs no lock for it.
	//
	// And the N-CHANNEL path (0.3.0, feature row 64) with it: the wide block
	// and the stereo pair are ONE capture block, so the two stages are drained
	// together, as one block (cluster 3705).
	drainCaptureStages();

	std::swap(m_outputBufferRead, m_outputBufferWrite);
	zeroSampleFrames(m_outputBufferWrite.get(), m_framesPerPeriod);
	m_hardwareOut.swap();  // 0.5.0 F1: the hardware outputs follow the master's double buffer
}

void AudioEngine::clear()
{
	m_clearSignal = true;
}




void AudioEngine::clearNewPlayHandles()
{
	requestChangeInModel();
	for( LocklessListElement * e = m_newPlayHandles.popList(); e; )
	{
		PlayHandle* const handle = e->value;
		if (m_sampleVoicePool != nullptr && m_sampleVoicePool->find(handle) != nullptr)
		{
			handle->audioBusHandle()->removePlayHandleUnderEngineChangeLock(handle);
			disposePlayHandle(handle, false);
		}
		LocklessListElement * next = e->next;
		m_newPlayHandles.free( e );
		e = next;
	}
	doneChangeInModel();
}



// removes all play-handles. this is necessary, when the song is stopped ->
// all remaining notes etc. would be played until their end
void AudioEngine::clearInternal()
{
	// TODO: m_midiClient->noteOffAll();
	for (auto ph : m_playHandles)
	{
		if (ph->type() != PlayHandle::Type::InstrumentPlayHandle
			&& ph->type() != PlayHandle::Type::InputMonitorHandle
			&& ph->type() != PlayHandle::Type::SessionCellHandle)
		{
			m_playHandlesToRemove.push_back(ph);
		}
	}

	// Capacity-deferred handles have the same stop semantics as live handles,
	// but use fixed storage so cancellation cannot grow a container on render.
	std::size_t remainingDeferred = 0;
	for (std::size_t i = 0; i < m_capacityDeferredPlayHandleCount; ++i)
	{
		PlayHandle* const handle = m_capacityDeferredPlayHandles[i];
		if (handle->type() == PlayHandle::Type::InstrumentPlayHandle
			|| handle->type() == PlayHandle::Type::InputMonitorHandle
			|| handle->type() == PlayHandle::Type::SessionCellHandle)
		{
			m_capacityDeferredPlayHandles[remainingDeferred++] = handle;
			continue;
		}

		handle->audioBusHandle()->removePlayHandleUnderEngineChangeLock(handle);
		disposePlayHandle(handle, true);
	}
	m_capacityDeferredPlayHandleCount = remainingDeferred;
}

void AudioEngine::doSetAudioDevice( AudioDevice * _dev )
{
	// TODO: Use shared_ptr here in the future.
	// Currently, this is safe, because this is only called by
	// ProjectRenderer, and after ProjectRenderer calls this function,
	// it does not access the old device anymore.
	if( m_audioDev != m_oldAudioDev ) {delete m_audioDev;}

	if( _dev )
	{
		m_audioDev = _dev;
	}
	else
	{
		printf( "param _dev == NULL in AudioEngine::setAudioDevice(...). "
					"Trying any working audio-device\n" );
		m_audioDev = tryAudioDevices();
	}
}

void AudioEngine::setAudioDevice(AudioDevice* _dev, bool startNow)
{
	stopProcessing();

	doSetAudioDevice( _dev );

	emit qualitySettingsChanged();
	emit sampleRateChanged();

	if (startNow) { startProcessing(); }
}




void AudioEngine::storeAudioDevice()
{
	if( !m_oldAudioDev )
	{
		m_oldAudioDev = m_audioDev;
	}
}




void AudioEngine::restoreAudioDevice()
{
	if( m_oldAudioDev && m_audioDev != m_oldAudioDev )
	{
		stopProcessing();
		delete m_audioDev;

		m_audioDev = m_oldAudioDev;
		emit sampleRateChanged();

		startProcessing();
	}
	m_oldAudioDev = nullptr;
}




void AudioEngine::addAudioBusHandle(AudioBusHandle* busHandle)
{
	requestChangeInModel();
	m_audioBusHandles.push_back(busHandle);
	if (!RealtimeScope::isActive() && QThread::currentThread() == thread())
	{
		const qsizetype capacity = m_playHandles.capacity();
		const qsizetype required = static_cast<qsizetype>(m_audioBusHandles.size()
			* PlayHandle::MaxNumber);
		if (capacity < required || capacity - m_playHandles.size() <= PlayHandleListLowWater)
		{
			const qsizetype requested = std::max(required,
				capacity + static_cast<qsizetype>(PlayHandle::MaxNumber));
			if (AudioResampler::ensureRealtimeVoiceConverterPoolCapacity(
				realtimeVoiceConverterCapacity(requested)))
			{
				m_playHandles.reserve(requested);
			}
		}
	}
	doneChangeInModel();
}


void AudioEngine::removeAudioBusHandle(AudioBusHandle* busHandle)
{
	requestChangeInModel();

	auto it = std::find(m_audioBusHandles.begin(), m_audioBusHandles.end(), busHandle);
	if (it != m_audioBusHandles.end())
	{
		m_audioBusHandles.erase(it);
	}
	doneChangeInModel();
}


bool AudioEngine::addPlayHandle( PlayHandle* handle )
{
	topUpPlayHandleCapacityOnControlThread();
	// Only add play handles if we have the CPU capacity to process them.
	// Instrument play handles are not added during playback, but when the
	// associated instrument is created, so add those unconditionally.
	if (handle->type() == PlayHandle::Type::InstrumentPlayHandle
		|| handle->type() == PlayHandle::Type::InputMonitorHandle
		|| handle->type() == PlayHandle::Type::SessionCellHandle || !criticalXRuns())
	{
		if (m_newPlayHandles.push(handle))
		{
			handle->audioBusHandle()->addPlayHandle(handle);
			return true;
		}

		if (handle->type() == PlayHandle::Type::InputMonitorHandle)
		{
			return false;
		}
	}

	disposePlayHandle(handle, RealtimeScope::isActive());

	return false;
}

bool AudioEngine::appendPlayHandleWithoutGrowth(PlayHandle* handle) noexcept
{
	if (m_playHandles.size() >= m_playHandles.capacity())
	{
		m_playHandleCapacityLow.store(true, std::memory_order_relaxed);
		requestPlayHandleCapacityRefresh();
		return false;
	}
	m_playHandles.append(handle);
	if (m_playHandles.capacity() - m_playHandles.size() <= PlayHandleListLowWater)
	{
		m_playHandleCapacityLow.store(true, std::memory_order_relaxed);
		requestPlayHandleCapacityRefresh();
	}
	return true;
}

bool AudioEngine::removeCapacityDeferredPlayHandle(PlayHandle* handle) noexcept
{
	for (std::size_t i = 0; i < m_capacityDeferredPlayHandleCount; ++i)
	{
		if (m_capacityDeferredPlayHandles[i] != handle) { continue; }
		for (std::size_t move = i + 1; move < m_capacityDeferredPlayHandleCount; ++move)
		{
			m_capacityDeferredPlayHandles[move - 1] = m_capacityDeferredPlayHandles[move];
		}
		--m_capacityDeferredPlayHandleCount;
		return true;
	}
	return false;
}

void AudioEngine::topUpPlayHandleCapacityOnControlThread()
{
	if (RealtimeScope::isActive() || QThread::currentThread() != thread()
		|| !m_playHandleCapacityRefreshRequested.exchange(false, std::memory_order_relaxed))
	{
		return;
	}

	const auto renderLock = requestChangesGuard();
	if (m_playHandleCapacityLow.exchange(false, std::memory_order_relaxed))
	{
		const qsizetype capacity = m_playHandles.capacity();
		if (capacity - m_playHandles.size() <= PlayHandleListLowWater)
		{
			const qsizetype requested = capacity + static_cast<qsizetype>(PlayHandle::MaxNumber);
			if (AudioResampler::ensureRealtimeVoiceConverterPoolCapacity(
				realtimeVoiceConverterCapacity(requested)))
			{
				m_playHandles.reserve(requested);
			}
		}
	}
	for (AudioBusHandle* bus : m_audioBusHandles) { bus->topUpPlayHandleCapacityUnderEngineChangeLock(); }
}


void AudioEngine::removePlayHandle(PlayHandle * ph)
{
	requestChangeInModel();
	// check thread affinity as we must not delete play-handles
	// which were created in a thread different than the audio engine thread
	if (ph->affinityMatters() && ph->affinity() == QThread::currentThread())
	{
		ph->audioBusHandle()->removePlayHandleUnderEngineChangeLock(ph);
		bool removedFromList = false;
		// Check m_newPlayHandles first because doing it the other way around
		// creates a race condition
		for( LocklessListElement * e = m_newPlayHandles.first(),
				* ePrev = nullptr; e; ePrev = e, e = e->next )
		{
			if (e->value == ph)
			{
				if( ePrev )
				{
					ePrev->next = e->next;
				}
				else
				{
					m_newPlayHandles.setFirst( e->next );
				}
				m_newPlayHandles.free( e );
				removedFromList = true;
				break;
			}
		}
		// Now check m_playHandles
		PlayHandleList::Iterator it = std::find(m_playHandles.begin(), m_playHandles.end(), ph);
		if (it != m_playHandles.end())
		{
			m_playHandles.erase(it);
			removedFromList = true;
		}
		removedFromList = removeCapacityDeferredPlayHandle(ph) || removedFromList;
		// Only deleting PlayHandles that were actually found in the list
		// "fixes crash when previewing a preset under high load"
		// (See tobydox's 2008 commit 4583e48)
		if ( removedFromList )
		{
			disposePlayHandle(ph, RealtimeScope::isActive());
		}
	}
	else
	{
		m_playHandlesToRemove.push_back(ph);
	}
	doneChangeInModel();
}




void AudioEngine::removePlayHandlesOfTypes(Track * track, PlayHandle::Types types)
{
	requestChangeInModel();
	PlayHandleList::Iterator it = m_playHandles.begin();
	while( it != m_playHandles.end() )
	{
		if ((*it)->isFromTrack(track) && ((*it)->type() & types))
		{
			(*it)->audioBusHandle()->removePlayHandleUnderEngineChangeLock(*it);
			disposePlayHandle(*it, RealtimeScope::isActive());
			it = m_playHandles.erase(it);
		}
		else
		{
			++it;
		}
	}

	// Handles created since the last render wait in m_newPlayHandles and merge
	// into m_playHandles only at the next render - a removal that walks just
	// m_playHandles misses them, and the survivor later renders through its
	// FREED track (the use-after-free SessionTuningTest caught; the same class
	// is behind old-project load crashes). Sweep the staging list IN PLACE,
	// the splice pattern the singular removePlayHandle() uses for this list.
	{
		LocklessList<PlayHandle *>::Element * e = m_newPlayHandles.first();
		LocklessList<PlayHandle *>::Element * ePrev = nullptr;
		while( e )
		{
			LocklessList<PlayHandle *>::Element * next = e->next;
			PlayHandle * handle = e->value;
			// Every type the caller asked for, as the active sweep above. A staged
			// InstrumentPlayHandle (an instrument such as LB302 queues one when it is
			// created) or InputMonitorHandle that this sweep skipped merged into the
			// render list at the next period and played through its deleted
			// instrument or track - the ArrangingCatchUpTest audio-worker crash in
			// InstrumentPlayHandle::play. Matching an instrument handle no longer
			// reads its instrument (InstrumentPlayHandle::isFromTrack), so a caller
			// that deleted the instrument before its track is safe here too.
			if (handle->isFromTrack(track) && (handle->type() & types))
			{
				handle->audioBusHandle()->removePlayHandleUnderEngineChangeLock(handle);
				disposePlayHandle(handle, RealtimeScope::isActive());
				if( ePrev ) { ePrev->next = next; }
				else { m_newPlayHandles.setFirst( next ); }
				m_newPlayHandles.free( e );
			}
			else
			{
				ePrev = e;
			}
			e = next;
		}
	}

	// Handles waiting for an engine-list slot are still owned by the engine.
	// Track teardown must remove matching deferred entries before the track or
	// its audio bus can be destroyed.
	{
		std::size_t remainingDeferred = 0;
		for (std::size_t i = 0; i < m_capacityDeferredPlayHandleCount; ++i)
		{
			PlayHandle* const handle = m_capacityDeferredPlayHandles[i];
			if (handle->isFromTrack(track) && (handle->type() & types))
			{
				handle->audioBusHandle()->removePlayHandleUnderEngineChangeLock(handle);
				disposePlayHandle(handle, RealtimeScope::isActive());
			}
			else
			{
				m_capacityDeferredPlayHandles[remainingDeferred++] = handle;
			}
		}
		m_capacityDeferredPlayHandleCount = remainingDeferred;
	}
	doneChangeInModel();
}




void AudioEngine::requestChangeInModel()
{
	if (s_renderingThread) { return; }
	// Counted around the acquisition so that modelChangeWaiters() reports the
	// control threads that are waiting for (or just got) the render period's
	// model ownership; the rendering thread short-circuits above and never
	// touches the counter.
	m_modelChangeWaiters.fetch_add(1, std::memory_order_relaxed);
	m_changeMutex.lock();
	m_modelChangeWaiters.fetch_sub(1, std::memory_order_relaxed);
#ifdef LMMS_TESTING
	++s_changeInModelDepth;
#endif
}

void AudioEngine::doneChangeInModel()
{
	if (s_renderingThread) { return; }
#ifdef LMMS_TESTING
	--s_changeInModelDepth;
#endif
	m_changeMutex.unlock();
}

#ifdef LMMS_TESTING
void AudioEngine::requestChangeInModelForTest(void (*contendedProbe)(void*), void* context)
{
	if (s_renderingThread) { return; }
	if (!m_changeMutex.try_lock())
	{
		// The acquisition was ATTEMPTED and the lock was held by another thread
		// (the render thread): this is the observation a test needs, and it is
		// made at the attempt - a report sent before the attempt could not tell
		// a blocked thread from one that has not run yet.
		if (contendedProbe != nullptr) { contendedProbe(context); }
		m_changeMutex.lock();
	}
	++s_changeInModelDepth;
}
#endif

bool AudioEngine::isAudioDevNameValid(QString name)
{
#ifdef LMMS_HAVE_SDL
	if (name == AudioSdl::name())
	{
		return true;
	}
#endif


#ifdef LMMS_HAVE_ALSA
	if (name == AudioAlsa::name())
	{
		return true;
	}
#endif


#ifdef LMMS_HAVE_PULSEAUDIO
	if (name == AudioPulseAudio::name())
	{
		return true;
	}
#endif


#ifdef LMMS_HAVE_OSS
	if (name == AudioOss::name())
	{
		return true;
	}
#endif

#ifdef LMMS_HAVE_SNDIO
	if (name == AudioSndio::name())
	{
		return true;
	}
#endif

#ifdef LMMS_HAVE_JACK
	if (name == AudioJack::name())
	{
		return true;
	}
#endif


#ifdef LMMS_HAVE_PORTAUDIO
	if (name == AudioPortAudio::name())
	{
		return true;
	}
#endif


#ifdef LMMS_HAVE_SOUNDIO
	if (name == AudioSoundIo::name())
	{
		return true;
	}
#endif

	if (name == AudioDummy::name())
	{
		return true;
	}

	return false;
}

bool AudioEngine::isMidiDevNameValid(QString name)
{
#ifdef LMMS_HAVE_ALSA
	if (name == MidiAlsaSeq::name() || name == MidiAlsaRaw::name())
	{
		return true;
	}
#endif

#ifdef LMMS_HAVE_JACK
	if (name == MidiJack::name())
	{
		return true;
	}
#endif

#ifdef LMMS_HAVE_OSS
	if (name == MidiOss::name())
	{
		return true;
	}
#endif

#ifdef LMMS_HAVE_SNDIO
	if (name == MidiSndio::name())
	{
		return true;
	}
#endif

#ifdef LMMS_HAVE_WINMM
	if (name == MidiWinMM::name())
	{
		return true;
	}
#endif

#ifdef LMMS_BUILD_APPLE
    if (name == MidiApple::name())
    {
		return true;
    }
#endif

    if (name == MidiDummy::name())
    {
		return true;
    }

	return false;
}

AudioDevice * AudioEngine::tryAudioDevices()
{
	bool success_ful = false;
	AudioDevice * dev = nullptr;
	// What the configuration asked for, verbatim, so a failure report can name
	// the backend that did not open (task #626).
	m_audioDevRequestName = ConfigManager::inst()->value( "audioengine", "audiodev" );
	m_audioDevStartReason.clear();
	QString dev_name = m_audioDevRequestName;
	if( !isAudioDevNameValid( dev_name ) )
	{
		dev_name = "";
	}

	m_audioDevStartFailed = false;

#ifdef LMMS_HAVE_SDL
	if( dev_name == AudioSdl::name() || dev_name == "" )
	{
		dev = new AudioSdl( success_ful, this );
		if( success_ful )
		{
			m_audioDevName = AudioSdl::name();
			return dev;
		}
		delete dev;
	}
#endif


#ifdef LMMS_HAVE_ALSA
	if( dev_name == AudioAlsa::name() || dev_name == "" )
	{
		dev = new AudioAlsa( success_ful, this );
		if( success_ful )
		{
			m_audioDevName = AudioAlsa::name();
			return dev;
		}
		delete dev;
	}
#endif


#ifdef LMMS_HAVE_PULSEAUDIO
	if( dev_name == AudioPulseAudio::name() || dev_name == "" )
	{
		dev = new AudioPulseAudio( success_ful, this );
		if( success_ful )
		{
			m_audioDevName = AudioPulseAudio::name();
			return dev;
		}
		delete dev;
	}
#endif


#ifdef LMMS_HAVE_OSS
	if( dev_name == AudioOss::name() || dev_name == "" )
	{
		dev = new AudioOss( success_ful, this );
		if( success_ful )
		{
			m_audioDevName = AudioOss::name();
			return dev;
		}
		delete dev;
	}
#endif

#ifdef LMMS_HAVE_SNDIO
	if( dev_name == AudioSndio::name() || dev_name == "" )
	{
		dev = new AudioSndio( success_ful, this );
		if( success_ful )
		{
			m_audioDevName = AudioSndio::name();
			return dev;
		}
		delete dev;
	}
#endif


#ifdef LMMS_HAVE_JACK
	if( dev_name == AudioJack::name() || dev_name == "" )
	{
		dev = new AudioJack( success_ful, this );
		if( success_ful )
		{
			m_audioDevName = AudioJack::name();
			return dev;
		}
		delete dev;
	}
#endif


#ifdef LMMS_HAVE_PORTAUDIO
	if (dev_name == AudioPortAudio::name() || dev_name.isEmpty())
	{
		dev = new AudioPortAudio( success_ful, this );
		if( success_ful )
		{
			m_audioDevName = AudioPortAudio::name();
			return dev;
		}
		delete dev;
	}
#endif


#ifdef LMMS_HAVE_SOUNDIO
	if( dev_name == AudioSoundIo::name() || dev_name == "" )
	{
		dev = new AudioSoundIo( success_ful, this );
		if( success_ful )
		{
			m_audioDevName = AudioSoundIo::name();
			return dev;
		}
		delete dev;
	}
#endif


	// add more device-classes here...
	//dev = new audioXXXX( SAMPLE_RATES[m_qualityLevel], success_ful, this );
	//if( sucess_ful )
	//{
	//	return dev;
	//}
	//delete dev

	if( dev_name != AudioDummy::name() )
	{
		printf( "No audio-driver working - falling back to dummy-audio-"
			"driver\nYou can render your songs and listen to the output "
			"files...\n" );

		m_audioDevStartFailed = true;
		// Say WHAT failed to open and what is being used instead, so control.ping
		// and the typed 'requires' refusal can hand an agent an actionable
		// sentence instead of a bare false (task #626). The backend library's own
		// error text (SDL_GetError / snd_strerror) is printed by the backend
		// above and stays on stderr.
		m_audioDevStartReason = m_audioDevRequestName.isEmpty()
			? tr( "no configured audio device could be opened; the engine is running with '%1' (no sound output)" )
				.arg( AudioDummy::name() )
			: tr( "the configured audio device '%1' could not be opened; the engine is running with '%2' (no sound output)" )
				.arg( m_audioDevRequestName, AudioDummy::name() );
	}

	m_audioDevName = AudioDummy::name();

	return new AudioDummy( success_ful, this );
}




MidiClient * AudioEngine::tryMidiClients()
{
	QString client_name = ConfigManager::inst()->value( "audioengine", "mididev" );
	if( !isMidiDevNameValid( client_name ) )
	{
		client_name = "";
	}

#ifdef LMMS_HAVE_ALSA
	if( client_name == MidiAlsaSeq::name() || client_name == "" )
	{
		auto malsas = new MidiAlsaSeq;
		if( malsas->isRunning() )
		{
			m_midiClientName = MidiAlsaSeq::name();
			return malsas;
		}
		delete malsas;
	}

	if( client_name == MidiAlsaRaw::name() || client_name == "" )
	{
		auto malsar = new MidiAlsaRaw;
		if( malsar->isRunning() )
		{
			m_midiClientName = MidiAlsaRaw::name();
			return malsar;
		}
		delete malsar;
	}
#endif

#ifdef LMMS_HAVE_JACK
	if( client_name == MidiJack::name() || client_name == "" )
	{
		auto mjack = new MidiJack;
		if( mjack->isRunning() )
		{
			m_midiClientName = MidiJack::name();
			return mjack;
		}
		delete mjack;
	}
#endif

#ifdef LMMS_HAVE_OSS
	if( client_name == MidiOss::name() || client_name == "" )
	{
		auto moss = new MidiOss;
		if( moss->isRunning() )
		{
			m_midiClientName = MidiOss::name();
			return moss;
		}
		delete moss;
	}
#endif

#ifdef LMMS_HAVE_SNDIO
	if( client_name == MidiSndio::name() || client_name == "" )
	{
		MidiSndio * msndio = new MidiSndio;
		if( msndio->isRunning() )
		{
			m_midiClientName = MidiSndio::name();
			return msndio;
		}
		delete msndio;
	}
#endif

#ifdef LMMS_HAVE_WINMM
	if( client_name == MidiWinMM::name() || client_name == "" )
	{
		MidiWinMM * mwmm = new MidiWinMM;
//		if( moss->isRunning() )
		{
			m_midiClientName = MidiWinMM::name();
			return mwmm;
		}
		delete mwmm;
	}
#endif

#ifdef LMMS_BUILD_APPLE
    printf( "trying midi apple...\n" );
    if( client_name == MidiApple::name() || client_name == "" )
    {
        MidiApple * mapple = new MidiApple;
        m_midiClientName = MidiApple::name();
        printf( "Returning midi apple\n" );
        return mapple;
    }
    printf( "midi apple didn't work: client_name=%s\n", client_name.toUtf8().constData());
#endif

	if(client_name != MidiDummy::name())
	{
		if (client_name.isEmpty())
		{
			printf("Unknown MIDI-client. ");
		}
		else
		{
			printf("Couldn't create %s MIDI-client. ", client_name.toUtf8().constData());
		}
		printf("Will use dummy-MIDI-client.\n");
	}

	m_midiClientName = MidiDummy::name();

	return new MidiDummy;
}

// ---------------------------------------------------------------------------
// R2.2: the Dummy device's loopback fixture (declared in include/AudioDummy.h)
// ---------------------------------------------------------------------------

namespace
{

/*! R2.2: the ceiling on a loopback latency read from the environment. The pair sums into the
 *  delay line configureLoopback() allocates (AudioDummy.h), and the raw figures came straight
 *  from qEnvironmentVariableIntValue() up to INT_MAX - so two values near the limit asked for
 *  more than 30 GB of SampleFrame and the allocation could only throw. A cable of more than a
 *  million frames (over 20 s at 48 kHz) is not a figure any device reports, so both latencies
 *  are clamped here, before the sum that sizes the line. */
constexpr f_cnt_t MaxLoopbackLatencyFrames = 1 << 20;

f_cnt_t latencyFromEnv(const char* name)
{
	bool ok = false;
	const int value = qEnvironmentVariableIntValue(name, &ok);
	return ok && value > 0 ? std::min<f_cnt_t>(static_cast<f_cnt_t>(value), MaxLoopbackLatencyFrames) : 0;
}

//! 0.5.0 F1: the first device output (0-based) of the pair the loopback cable carries back to the input.
int g_dummyLoopOutput = 0;

} // namespace


void AudioDummy::configureLoopback()
{
	// 0.5.0 F1 (MX-040): LMMS_DUMMY_OUTPUT_CHANNELS gives the Dummy that many outputs (2..128) - an
	// interface with more than a stereo pair, for the hardware-output tests - and
	// LMMS_DUMMY_LOOPBACK_OUTPUT (1-based) picks the pair the loopback cable plugs into (1/2 unless set).
	const f_cnt_t outputs = latencyFromEnv("LMMS_DUMMY_OUTPUT_CHANNELS");
	setChannels(static_cast<ch_cnt_t>(std::clamp<f_cnt_t>(outputs, DEFAULT_CHANNELS, HardwareOutputBus::MaxChannels)));
	g_dummyLoopOutput = std::clamp<int>(static_cast<int>(latencyFromEnv("LMMS_DUMMY_LOOPBACK_OUTPUT")) - 1, 0, channels() - 2);
	m_loopback = !qEnvironmentVariableIsEmpty("LMMS_DUMMY_LOOPBACK");
	if (!m_loopback) { return; }
	m_loopInLatency = latencyFromEnv("LMMS_DUMMY_INPUT_LATENCY");
	m_loopOutLatency = latencyFromEnv("LMMS_DUMMY_OUTPUT_LATENCY");
	// The line holds the whole delay plus the largest period a render can hand back.
	m_loopLine.assign(static_cast<std::size_t>(m_loopInLatency + m_loopOutLatency) + MAXIMUM_BUFFER_SIZE + 1,
		SampleFrame(0.0f, 0.0f));
	m_loopBlock.assign(MAXIMUM_BUFFER_SIZE, SampleFrame(0.0f, 0.0f));
}


f_cnt_t AudioDummy::captureBlockFrames() const
{
	return audioEngine() != nullptr ? audioEngine()->framesPerPeriod() : 0;
}


void AudioDummy::feedBack(std::span<const SampleFrame> period)
{
	// Write the period, then read the same span back delayed: in[t] = out[t - delay].
	const std::size_t size = m_loopLine.size();
	const std::size_t delay = static_cast<std::size_t>(m_loopInLatency + m_loopOutLatency);
	const std::size_t frames = std::min(period.size(), m_loopBlock.size());
	// 0.5.0 F1: the cable carries what the device's looped pair plays - the master on 1/2 unless the
	// mixer routed the hardware outputs (HardwareOutputBus), and then exactly what that pair is fed.
	const HardwareOutputBus& bus = audioEngine()->hardwareOutputs();
	const int pair = g_dummyLoopOutput;
	for (std::size_t f = 0; f < frames; ++f)
	{
		const auto at = static_cast<f_cnt_t>(f);
		m_loopLine[(m_loopWrite + f) % size] = !bus.active() ? (pair == 0 ? period[f] : SampleFrame(0.f, 0.f))
			: SampleFrame(bus.sample(pair, at, period[f]), bus.sample(pair + 1, at, period[f]));
	}
	for (std::size_t f = 0; f < frames; ++f) { m_loopBlock[f] = m_loopLine[(m_loopWrite + f + size - delay) % size]; }
	m_loopWrite = (m_loopWrite + frames) % size;
	audioEngine()->pushInputFrames(m_loopBlock.data(), static_cast<f_cnt_t>(frames));
}


} // namespace lmms
