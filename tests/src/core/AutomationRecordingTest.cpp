/*
 * AutomationRecordingTest.cpp - lock-free recorded automation handoff
 *
 * This file is part of LMMS - https://lmms.io
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <thread>
#include <vector>

#include <QMutexLocker>

#include "AllocationProbe.h"
#include "AutomationClip.h"
#include "AutomationRamp.h"
#include "AutomationRecordingQueue.h"
#include "AutomationSwitches.h"
#include "AudioEngine.h"
#include "AutomationTrack.h"
#include "DetuningHelper.h"
#include "Engine.h"
#include "AutomatableModel.h"
#include "InstrumentTrack.h"
#include "JournallingObject.h"
#include "NotePlayHandle.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

std::uint32_t bits(float value)
{
	return std::bit_cast<std::uint32_t>(value);
}

bool sameRamp(const AutomationRamp& first, const AutomationRamp& second)
{
	if (first.knotCount() != second.knotCount() || first.frames() != second.frames()) { return false; }
	for (int i = 0; i < first.knotCount(); ++i)
	{
		if (first.knot(i).frame != second.knot(i).frame || bits(first.knot(i).value) != bits(second.knot(i).value))
		{
			return false;
		}
	}
	for (f_cnt_t frame = 0; frame <= first.frames(); ++frame)
	{
		if (bits(first.valueAt(frame)) != bits(second.valueAt(frame))) { return false; }
	}
	return true;
}

struct RecordingSweepEvent
{
	tick_t tick = 0;
	float value = 0.0f;
};

using RecordingSweepTrace = std::vector<std::vector<RecordingSweepEvent>>;

struct RecordingSweepRender
{
	std::vector<SampleFrame> frames;
	std::vector<float> controlValues;
	RecordingSweepTrace events;
	bool queueMatchesTickSchedule = true;
};

struct RecordingRenderRig
{
	SampleTrack* sourceTrack = nullptr;
	AutomationClip* automation = nullptr;
};

RecordingRenderRig makeRecordingRenderRig(Song* song, const std::shared_ptr<const SampleBuffer>& buffer,
	bool recording, bool sampleAccurate)
{
	auto* source = new SampleTrack(song);
	auto* sample = dynamic_cast<SampleClip*>(source->createClip(TimePos(0)));
	sample->setSampleBuffer(buffer);
	auto* automationTrack = new AutomationTrack(song);
	auto* automation = dynamic_cast<AutomationClip*>(automationTrack->createClip(TimePos(0)));
	automation->addObject(source->volumeModel());
	automation->setProgressionType(sampleAccurate
		? AutomationClip::ProgressionType::Linear : AutomationClip::ProgressionType::Discrete);
	automation->setRampSetting(sampleAccurate
		? AutomationClip::RampSetting::On : AutomationClip::RampSetting::Off);
	automation->changeLength(TimePos(64));
	automation->putValue(TimePos(0), 100.0f, false, true);
	automation->setRecording(recording);
	song->updateLength();
	return {source, automation};
}

std::vector<tick_t> recordingTicksInPeriod(Song* song, double framesPerTick, f_cnt_t frames)
{
	std::vector<tick_t> ticks;
	auto tick = song->getPlayPos().getTicks();
	double offset = song->getTimeline().frameOffset();
	if (offset == 0.0) { ticks.push_back(tick); }
	offset += frames;
	while (offset > framesPerTick)
	{
		offset -= framesPerTick;
		ticks.push_back(++tick);
	}
	return ticks;
}

std::size_t firstDifferentRenderedFrame(const std::vector<SampleFrame>& first,
	const std::vector<SampleFrame>& second)
{
	const std::size_t frames = std::min(first.size(), second.size());
	for (std::size_t i = 0; i < frames; ++i)
	{
		for (int channel = 0; channel < 2; ++channel)
		{
			if (bits(first[i][channel]) != bits(second[i][channel])) { return i; }
		}
	}
	return frames;
}

} // namespace

class AutomationRecordingTest : public QObject
{
	Q_OBJECT

private:
	RecordingSweepRender renderRecordingSweep(Song* song,
		const std::shared_ptr<const SampleBuffer>& buffer, bool recording, bool sampleAccurate,
		const RecordingSweepTrace* referenceEvents)
	{
		constexpr int Periods = 24;
		constexpr std::array<float, Periods> Inputs = {
			35.0f, 58.0f, 82.0f, 109.0f, 137.0f, 161.0f, 124.0f, 91.0f,
			63.0f, 42.0f, 76.0f, 118.0f, 149.0f, 103.0f, 69.0f, 47.0f,
			88.0f, 132.0f, 157.0f, 111.0f, 73.0f, 51.0f, 97.0f, 143.0f};
		RecordingSweepRender result;
		result.frames.reserve(static_cast<std::size_t>(Periods) * Engine::audioEngine()->framesPerPeriod());
		result.controlValues.reserve(Periods);
		result.events.resize(Periods);
		const auto rig = makeRecordingRenderRig(song, buffer, recording, sampleAccurate);
		song->tempoModel().setValue(90);
		song->setPlayPos(0, Song::PlayMode::Song);
		// The engine returns a double-buffered master bus; drain the prior pass
		// while stopped so each comparison starts from the same silent frame.
		Engine::audioEngine()->renderNextPeriod();
		Engine::audioEngine()->renderNextPeriod();
		song->playSong();
		for (int period = 0; period < Periods; ++period)
		{
			auto* model = rig.sourceTrack->volumeModel();
			model->setValue(Inputs[period]);
			if (referenceEvents != nullptr)
			{
				// Legacy recording keeps the control manual for this tick. For an
				// opted-in ramp, publish the synchronous map's block ramp exactly
				// where the engine would build it, then keep per-tick writes off.
				model->setAutomationMode(AutomatableModel::AutomationMode::Off);
				if (sampleAccurate)
				{
					model->setAutomationMode(AutomatableModel::AutomationMode::Read);
					song->buildAutomationRamps(song->tracks(), song->getPlayPos(),
						Engine::audioEngine()->framesPerPeriod(),
						static_cast<int>(song->getTimeline().frameOffset()));
					model->setAutomationMode(AutomatableModel::AutomationMode::Off);
				}
			}
			const auto ticks = recordingTicksInPeriod(song, Engine::framesPerTick(),
				Engine::audioEngine()->framesPerPeriod());
			const auto queueBefore = song->m_automationRecordingQueues[0].writeCursor();
			for (const auto& frame : Engine::audioEngine()->renderNextPeriod()) { result.frames.push_back(frame); }
			result.controlValues.push_back(rig.sourceTrack->volumeModel()->value());
			if (recording && referenceEvents == nullptr)
			{
				const auto queueAfter = song->m_automationRecordingQueues[0].writeCursor();
				result.queueMatchesTickSchedule &= queueAfter - queueBefore == ticks.size();
				for (auto sequence = queueBefore + 1; sequence <= queueAfter; ++sequence)
				{
					AutomationRecordingQueue::Event event;
					result.queueMatchesTickSchedule &=
						song->m_automationRecordingQueues[0].copyEventAtSequence(sequence, event);
					result.events[period].push_back({event.time.getTicks(), event.value});
				}
			}
			else if (referenceEvents != nullptr)
			{
				for (const auto& event : referenceEvents->at(period))
				{
					rig.automation->recordValue(TimePos(event.tick), event.value);
				}
			}
		}
		song->stop();
		return result;
	}

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void queuedRecordingIsVisibleBeforeDrain()
	{
		auto* song = Engine::getSong();
		song->drainAutomationRecordingEvents();
		AutomationClip clip(nullptr);
		clip.setProgressionType(AutomationClip::ProgressionType::Discrete);
		const auto id = static_cast<const JournallingObject&>(clip).id();

		QVERIFY(song->enqueueAutomationRecord(id, clip.recordingMapTime(TimePos(4)), 0.875f));
		QVERIFY(clip.hasAutomation());
		QCOMPARE(bits(clip.valueAt(TimePos(4))), bits(0.875f));
		QVERIFY(!clip.getTimeMap().contains(4)); // the audio thread did not edit the map

		song->drainAutomationRecordingEvents();
		QVERIFY(clip.getTimeMap().contains(4));
		QCOMPARE(bits(clip.valueAt(TimePos(4))), bits(0.875f));

		const int previousQuantization = AutomationClip::quantization();
		struct RestoreQuantization
		{
			int value;
			~RestoreQuantization() { AutomationClip::setQuantization(value); }
		} restoreQuantization{previousQuantization};
		AutomationClip::setQuantization(4);
		clip.setStartTimeOffset(TimePos(2));
		QVERIFY(song->enqueueAutomationRecord(id, clip.recordingMapTime(TimePos(5)), 0.625f));
		QCOMPARE(bits(clip.valueAt(TimePos(4))), bits(0.625f));
		song->drainAutomationRecordingEvents();
		QVERIFY(clip.getTimeMap().contains(4)); // 5 relative ticks minus offset 2 quantizes to tick 4
		QCOMPARE(bits(clip.getTimeMap().value(4).getInValue()), bits(0.625f));
	}

	void recordedSweepMatchesThePreChangeEvaluatorForNormalAndSampleAccurateClips()
	{
		auto* song = Engine::getSong();
		song->drainAutomationRecordingEvents();
		const std::pair<int, float> sweep[] = {
			{2, 0.25f}, {4, 0.5f}, {6, 0.75f}, {8, 0.875f}};

		for (const bool sampleAccurate : {false, true})
		{
			AutomationClip actual(nullptr);
			AutomationClip reference(nullptr);
			const auto progression = sampleAccurate ? AutomationClip::ProgressionType::Linear
				: AutomationClip::ProgressionType::Discrete;
			actual.setProgressionType(progression);
			reference.setProgressionType(progression);
			actual.putValue(TimePos(0), 0.125f, false, true);
			reference.putValue(TimePos(0), 0.125f, false, true);
			// Recording at an existing tick must supersede that point in the
			// audio-visible overlay before the main-thread snapshot is published.
			actual.putValue(TimePos(8), 0.25f, false, true);
			reference.putValue(TimePos(8), 0.25f, false, true);
			actual.changeLength(TimePos(64));
			reference.changeLength(TimePos(64));
			actual.setRampSetting(sampleAccurate ? AutomationClip::RampSetting::On : AutomationClip::RampSetting::Off);
			reference.setRampSetting(sampleAccurate ? AutomationClip::RampSetting::On : AutomationClip::RampSetting::Off);
			const auto id = static_cast<const JournallingObject&>(actual).id();

			for (const auto& [tick, value] : sweep)
			{
				reference.recordValue(TimePos(tick), value);
				QVERIFY(song->enqueueAutomationRecord(id, actual.recordingMapTime(TimePos(tick)), value));

				// Compare each event before the next one is recorded: this checks
				// the audio-visible handoff during the sweep, not only after it ends.
				for (int checkTick = std::max(0, tick - 1); checkTick <= tick + 1; ++checkTick)
				{
					QVERIFY2(bits(actual.valueAt(TimePos(checkTick))) == bits(reference.valueAt(TimePos(checkTick))),
						qPrintable(QStringLiteral("recorded value differs at tick %1, sample-accurate=%2")
							.arg(checkTick).arg(sampleAccurate)));
				}
				if (sampleAccurate)
				{
					AutomationRamp actualRamp, referenceRamp;
					actual.writeBlockRamp(actualRamp, TimePos(tick), 8, 4.0, 0);
					reference.writeBlockRamp(referenceRamp, TimePos(tick), 8, 4.0, 0);
					QVERIFY2(sameRamp(actualRamp, referenceRamp),
						"sample-accurate rendered knots or samples differ during the recording sweep");
				}
			}
			for (int tick = 0; tick <= 10; ++tick)
			{
				QCOMPARE(bits(actual.valueAt(TimePos(tick))), bits(reference.valueAt(TimePos(tick))));
			}
			song->drainAutomationRecordingEvents();
			if (sampleAccurate)
			{
				AutomationRamp drainedRamp;
				AutomationRamp referenceRamp;
				actual.writeBlockRamp(drainedRamp, TimePos(0), 40, 4.0, 0);
				reference.writeBlockRamp(referenceRamp, TimePos(0), 40, 4.0, 0);
				QVERIFY2(sameRamp(drainedRamp, referenceRamp), "drained render differs from the synchronous pre-change result");
			}
		}
	}

	void recordingSweepProducesThePreChangeAudioRender()
	{
		auto* song = Engine::getSong();
		auto& switches = AutomationSwitches::instance();
		struct RestoreSwitches
		{
			bool reads;
			bool writes;
			bool punched;
			~RestoreSwitches() { AutomationSwitches::instance().set(reads, writes, punched); }
		} restoreSwitches{switches.reads(), switches.writes(), switches.punchedWrites()};
		song->drainAutomationRecordingEvents();
		song->stop();
		song->clearProject();
		const auto rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		std::vector<SampleFrame> sourceFrames(static_cast<std::size_t>(rate));
		for (std::size_t i = 0; i < sourceFrames.size(); ++i)
		{
			const float value = static_cast<float>(static_cast<int>(i % 97) - 48) / 128.0f;
			sourceFrames[i] = SampleFrame(value, -value * 0.75f);
		}
		const auto buffer = std::make_shared<const SampleBuffer>(std::move(sourceFrames), rate);

		for (const bool sampleAccurate : {false, true})
		{
			switches.set(true, true, false);
			song->clearProject();
			song->drainAutomationRecordingEvents();
			auto recorded = renderRecordingSweep(song, buffer, true, sampleAccurate, nullptr);
			QVERIFY(recorded.queueMatchesTickSchedule);
			song->drainAutomationRecordingEvents();
			song->clearProject();
			switches.set(true, false, false);
			auto synchronous = renderRecordingSweep(song, buffer, true, sampleAccurate, &recorded.events);
			const auto mismatch = firstDifferentRenderedFrame(recorded.frames, synchronous.frames);
			const auto mismatchPeriod = mismatch / Engine::audioEngine()->framesPerPeriod();
			const float recordedControl = mismatchPeriod < recorded.controlValues.size()
				? recorded.controlValues[mismatchPeriod] : 0.0f;
			const float synchronousControl = mismatchPeriod < synchronous.controlValues.size()
				? synchronous.controlValues[mismatchPeriod] : 0.0f;
			QVERIFY2(mismatch == recorded.frames.size() && recorded.frames.size() == synchronous.frames.size(),
				qPrintable(QStringLiteral("audio render changed (sample-accurate=%1, frame=%2/%3, control=%4/%5, left=%6/%7)")
					.arg(sampleAccurate).arg(mismatch).arg(synchronous.frames.size())
					.arg(recordedControl).arg(synchronousControl)
					.arg(mismatch < recorded.frames.size() ? bits(recorded.frames[mismatch][0]) : 0, 0, 16)
					.arg(mismatch < synchronous.frames.size() ? bits(synchronous.frames[mismatch][0]) : 0, 0, 16)));
			const auto peak = std::max_element(recorded.frames.begin(), recorded.frames.end(),
				[](const SampleFrame& a, const SampleFrame& b) {
					return std::abs(a[0]) < std::abs(b[0]);
				});
			QVERIFY(peak != recorded.frames.end());
			QVERIFY(std::abs((*peak)[0]) > 0.01f);
			song->clearProject();
		}
		song->tempoModel().setValue(DefaultTempo);
	}

	void songProcessAutomationRecordingUsesTheQueue()
	{
		auto* song = Engine::getSong();
		song->drainAutomationRecordingEvents();
		AutomationTrack track(song);
		AutomationClip clip(&track);
		FloatModel model(0.75f, 0.0f, 1.0f, 0.01f);
		clip.putValue(TimePos(0), 0.125f, false, true);
		clip.addObject(&model);
		clip.changeLength(TimePos(64));
		clip.setRecording(true);

		const auto previousMode = song->m_playMode;
		song->m_playMode = Song::PlayMode::Song;
		std::atomic<bool> finished{false};
		QMutexLocker held(&clip.m_clipMutex);
		std::thread renderer([&] {
			song->processAutomations(song->tracks(), TimePos(1), 0);
			finished.store(true, std::memory_order_release);
		});
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
		while (!finished.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		const bool completedWhileMutexHeld = finished.load(std::memory_order_acquire);
		held.unlock();
		renderer.join();
		song->m_playMode = previousMode;
		QVERIFY2(completedWhileMutexHeld, "Song::processAutomations waited for the automation clip mutex");

		QCOMPARE(bits(clip.valueAt(TimePos(1))), bits(0.75f));
		QVERIFY(!clip.getTimeMap().contains(1));
		song->drainAutomationRecordingEvents();
		QVERIFY(clip.getTimeMap().contains(1));
		QCOMPARE(bits(clip.getTimeMap().value(1).getInValue()), bits(0.75f));
	}

	void midiInputDetuningRecordingUsesTheQueue()
	{
		auto* song = Engine::getSong();
		song->drainAutomationRecordingEvents();
		InstrumentTrack instrumentTrack(song);
		auto detuning = std::make_shared<DetuningHelper>();
		Note note(TimePos(32), TimePos(0), DefaultKey, DefaultVolume, DefaultPanning, detuning);
		auto* clip = detuning->automationClip();
		const auto id = static_cast<const JournallingObject&>(*clip).id();
		NotePlayHandle handle(&instrumentTrack, 0, 64, note, nullptr, -1, NotePlayHandle::Origin::MidiInput);

		handle.processTimePos(TimePos(1), 25.0f, true);
		QCOMPARE(bits(clip->valueAt(TimePos(1))), bits(0.25f));
		QVERIFY(!clip->getTimeMap().contains(1));
		song->drainAutomationRecordingEvents();
		QVERIFY(clip->getTimeMap().contains(1));
		QCOMPARE(bits(clip->getTimeMap().value(1).getInValue()), bits(0.25f));
		QVERIFY(id != 0);
	}

	void recordingFifoCollapseStaleIdsAndOverflowAreCounted()
	{
		auto* song = Engine::getSong();
		song->drainAutomationRecordingEvents();
		AutomationClip clip(nullptr);
		clip.setProgressionType(AutomationClip::ProgressionType::Discrete);
		const auto id = static_cast<const JournallingObject&>(clip).id();
		QVERIFY(song->enqueueAutomationRecord(id, clip.recordingMapTime(TimePos(2)), 0.5f));
		QVERIFY(song->enqueueAutomationRecord(id, clip.recordingMapTime(TimePos(3)), 0.5f));
		QVERIFY(song->enqueueAutomationRecord(id, clip.recordingMapTime(TimePos(4)), 0.875f));
		song->drainAutomationRecordingEvents();
		QVERIFY(clip.getTimeMap().contains(2));
		QVERIFY(!clip.getTimeMap().contains(3)); // equal successive samples collapse to one point
		QVERIFY(clip.getTimeMap().contains(4));
		QCOMPARE(bits(clip.valueAt(TimePos(4))), bits(0.875f));

		const auto before = song->automationRecordingStatus();
		const auto staleId = std::numeric_limits<jo_id_t>::max();
		for (std::size_t i = 0; i < AutomationRecordingQueue::Capacity; ++i)
		{
			QVERIFY(song->enqueueAutomationRecordForProducerForTest(0, staleId, TimePos(1), 0.5f));
		}
		QVERIFY(!song->enqueueAutomationRecordForProducerForTest(0, staleId, TimePos(1), 0.5f));
		QCOMPARE(song->automationRecordingStatus().droppedEvents, before.droppedEvents + 1);
		song->drainAutomationRecordingEvents();
		QCOMPARE(song->automationRecordingStatus().staleClipIds, before.staleClipIds + AutomationRecordingQueue::Capacity);

		song->m_recordedModelMembershipGeneration = 1;
		song->m_recordedModelMembershipSize = 0;
		for (auto& entry : song->m_recordedModelMembership) { entry.generation = 0; }
		for (std::size_t i = 0; i < Song::AutomationRecordingMembershipCapacity; ++i)
		{
			QVERIFY(song->markRecordedModelForCurrentTick(static_cast<jo_id_t>(100000 + i)));
		}
		QVERIFY(!song->markRecordedModelForCurrentTick(9999999));
		QCOMPARE(song->automationRecordingStatus().membershipOverflows, before.membershipOverflows + 1);
	}

	void audioRecordingQueueDoesNotAllocateOrWaitForClipMutex()
	{
		auto* song = Engine::getSong();
		song->drainAutomationRecordingEvents();
		AutomationClip clip(nullptr);
		const auto id = static_cast<const JournallingObject&>(clip).id();
		std::atomic<bool> finished{false};
		std::atomic<bool> accepted{false};
		std::atomic<std::uint64_t> allocations{0};
		QMutexLocker held(&clip.m_clipMutex);
		std::thread producer([&] {
			test::resetAllocationCount();
			test::tlCountAllocations = true;
			accepted.store(song->enqueueAutomationRecord(id, clip.recordingMapTime(TimePos(1)), 0.5f), std::memory_order_relaxed);
			allocations.store(test::tlAllocationCount, std::memory_order_relaxed);
			test::tlCountAllocations = false;
			finished.store(true, std::memory_order_release);
		});
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
		while (!finished.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		const bool completedWhileMutexHeld = finished.load(std::memory_order_acquire);
		held.unlock();
		producer.join();
		QVERIFY2(completedWhileMutexHeld, "audio recording enqueue waited for the clip edit mutex");
		QVERIFY(accepted.load(std::memory_order_relaxed));
		QCOMPARE(allocations.load(std::memory_order_relaxed), std::uint64_t{0});
		song->drainAutomationRecordingEvents();
	}

	void pendingRecordingOverlayDoesNotAllocateOrWaitForClipMutex()
	{
		auto* song = Engine::getSong();
		song->drainAutomationRecordingEvents();
		AutomationClip clip(nullptr);
		clip.setProgressionType(AutomationClip::ProgressionType::Discrete);
		clip.putValue(TimePos(0), 0.125f, false, true);
		const auto id = static_cast<const JournallingObject&>(clip).id();
		QVERIFY(song->enqueueAutomationRecord(id, clip.recordingMapTime(TimePos(1)), 0.5f));

		std::atomic<bool> finished{false};
		std::atomic<std::uint64_t> allocations{0};
		std::atomic<bool> observedHasAutomation{false};
		std::atomic<std::uint32_t> observedValue{0};
		std::atomic<std::uint32_t> observedRampValue{0};
		QMutexLocker held(&clip.m_clipMutex);
		std::thread reader([&] {
			test::resetAllocationCount();
			test::tlCountAllocations = true;
			observedHasAutomation.store(clip.hasAutomation(), std::memory_order_relaxed);
			observedValue.store(bits(clip.valueAt(TimePos(1))), std::memory_order_relaxed);
			AutomationRamp ramp;
			clip.writeBlockRamp(ramp, TimePos(1), 4, 4.0, 0);
			observedRampValue.store(bits(ramp.valueAt(0)), std::memory_order_relaxed);
			allocations.store(test::tlAllocationCount, std::memory_order_relaxed);
			test::tlCountAllocations = false;
			finished.store(true, std::memory_order_release);
		});
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
		while (!finished.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		const bool completedWhileMutexHeld = finished.load(std::memory_order_acquire);
		held.unlock();
		reader.join();
		QVERIFY2(completedWhileMutexHeld, "pending recording overlay waited for the clip edit mutex");
		QVERIFY(observedHasAutomation.load(std::memory_order_relaxed));
		QCOMPARE(observedValue.load(std::memory_order_relaxed), bits(0.5f));
		QCOMPARE(observedRampValue.load(std::memory_order_relaxed), bits(0.5f));
		QCOMPARE(allocations.load(std::memory_order_relaxed), std::uint64_t{0});
		song->drainAutomationRecordingEvents();
	}

};

QTEST_GUILESS_MAIN(AutomationRecordingTest)

#include "AutomationRecordingTest.moc"
