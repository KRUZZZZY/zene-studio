/*
 * AutomationRecordingOverlayTest.cpp - exact pending-record replay
 *
 * This file is part of LMMS.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QtTest>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "AllocationProbe.h"
#include "AutomationClip.h"
#include "AutomationRamp.h"
#include "AutomationSwitches.h"
#include "AudioEngine.h"
#include "AutomationTrack.h"
#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"

using namespace lmms;

namespace
{

std::uint32_t bits(float value)
{
	return std::bit_cast<std::uint32_t>(value);
}

struct SweepEvent
{
	int tick = 0;
	float value = 0.0f;
};

using SweepTrace = std::vector<std::vector<SweepEvent>>;

struct RenderResult
{
	std::vector<SampleFrame> frames;
	SweepTrace events;
};

struct RenderRig
{
	SampleTrack* source = nullptr;
	AutomationClip* automation = nullptr;
};

RenderRig makeRig(Song* song, const std::shared_ptr<const SampleBuffer>& buffer,
	AutomationClip::ProgressionType progression, bool seedRepeatedValueNode)
{
	auto* source = new SampleTrack(song);
	auto* sample = dynamic_cast<SampleClip*>(source->createClip(TimePos(0)));
	sample->setSampleBuffer(buffer);
	auto* automationTrack = new AutomationTrack(song);
	auto* automation = dynamic_cast<AutomationClip*>(automationTrack->createClip(TimePos(0)));
	automation->addObject(source->volumeModel());
	automation->setProgressionType(progression);
	automation->setRampSetting(AutomationClip::RampSetting::On);
	automation->changeLength(TimePos(64));
	automation->putValue(TimePos(0), 100.0f, false, true);
	automation->putValue(TimePos(12), seedRepeatedValueNode ? 165.0f : 35.0f, false, true);
	automation->putValue(TimePos(24), 165.0f, false, true);
	if (seedRepeatedValueNode) { automation->putValue(TimePos(1), 75.0f, false, true); }
	song->updateLength();
	return {source, automation};
}

std::vector<int> ticksInPeriod(Song* song)
{
	std::vector<int> ticks;
	int tick = song->getPlayPos().getTicks();
	double offset = song->getTimeline().frameOffset();
	if (offset == 0.0) { ticks.push_back(tick); }
	offset += Engine::audioEngine()->framesPerPeriod();
	while (offset > Engine::framesPerTick())
	{
		offset -= Engine::framesPerTick();
		ticks.push_back(++tick);
	}
	return ticks;
}

RenderResult renderSweep(Song* song, const std::shared_ptr<const SampleBuffer>& buffer,
	AutomationClip::ProgressionType progression, bool seedRepeatedValueNode,
	const SweepTrace* referenceEvents)
{
	constexpr int Periods = 12;
	constexpr std::array<float, Periods> Inputs = {
		35.0f, 58.0f, 82.0f, 109.0f, 137.0f, 161.0f, 124.0f, 91.0f,
		63.0f, 42.0f, 76.0f, 118.0f};
	RenderResult result;
	result.frames.reserve(static_cast<std::size_t>(Periods) * Engine::audioEngine()->framesPerPeriod());
	result.events.resize(Periods);
	const auto rig = makeRig(song, buffer, progression, seedRepeatedValueNode);
	song->tempoModel().setValue(90);
	song->setPlayPos(0, Song::PlayMode::Song);
	Engine::audioEngine()->renderNextPeriod();
	Engine::audioEngine()->renderNextPeriod();
	song->playSong();
	for (int period = 0; period < Periods; ++period)
	{
		auto* model = rig.source->volumeModel();
		const float input = seedRepeatedValueNode ? Inputs[0] : Inputs[period];
		model->setValue(input);
		if (referenceEvents == nullptr)
		{
			for (const int tick : ticksInPeriod(song))
			{
				const float value = model->inverseScaledValue(model->value());
				const TimePos mapTime = rig.automation->recordingMapTime(TimePos(tick));
				if (!song->enqueueAutomationRecord(
					static_cast<const JournallingObject&>(*rig.automation).id(), mapTime, value))
				{
					qFatal("recording-overlay render fixture exceeded its preallocated queue");
				}
				result.events[period].push_back({mapTime.getTicks(), value});
			}
		}
		else
		{
			for (const auto& event : referenceEvents->at(period))
			{
				rig.automation->recordValue(TimePos(event.tick), event.value);
			}
		}
		for (const auto& frame : Engine::audioEngine()->renderNextPeriod()) { result.frames.push_back(frame); }
	}
	song->stop();
	if (referenceEvents == nullptr) { song->drainAutomationRecordingEvents(); }
	return result;
}

std::size_t firstDifferentFrame(const std::vector<SampleFrame>& left, const std::vector<SampleFrame>& right)
{
	for (std::size_t frame = 0; frame < std::min(left.size(), right.size()); ++frame)
	{
		for (int channel = 0; channel < 2; ++channel)
		{
			if (bits(left[frame][channel]) != bits(right[frame][channel])) { return frame; }
		}
	}
	return std::min(left.size(), right.size());
}

std::shared_ptr<const SampleBuffer> makeRenderBuffer(sample_rate_t rate)
{
	std::vector<SampleFrame> frames(static_cast<std::size_t>(rate));
	for (std::size_t i = 0; i < frames.size(); ++i)
	{
		const float value = static_cast<float>(static_cast<int>(i % 97) - 48) / 128.0f;
		frames[i] = SampleFrame(value, -value * 0.75f);
	}
	return std::make_shared<const SampleBuffer>(std::move(frames), rate);
}

} // namespace

class AutomationRecordingOverlayTest : public QObject
{
	Q_OBJECT

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

	void pendingCubicInsertionMatchesSynchronousTangentRecalculation()
	{
		auto* song = Engine::getSong();
		song->drainAutomationRecordingEvents();
		song->clearProject();
		AutomationClip actual(nullptr), reference(nullptr);
		for (AutomationClip* clip : {&actual, &reference})
		{
			clip->setProgressionType(AutomationClip::ProgressionType::CubicHermite);
			clip->putValue(TimePos(0), 0.125f, false, true);
			clip->putValue(TimePos(12), 0.875f, false, true);
			clip->putValue(TimePos(24), 0.25f, false, true);
			clip->changeLength(TimePos(32));
		}
		reference.recordValue(TimePos(5), 0.75f);
		QVERIFY(song->enqueueAutomationRecord(static_cast<const JournallingObject&>(actual).id(),
			actual.recordingMapTime(TimePos(5)), 0.75f));
		for (int tick = 0; tick <= 12; ++tick)
		{
			const float actualValue = actual.valueAt(TimePos(tick));
			const float referenceValue = reference.valueAt(TimePos(tick));
			QVERIFY2(bits(actualValue) == bits(referenceValue),
				qPrintable(QStringLiteral("cubic recording differs at tick %1 (%2 vs %3)")
					.arg(tick).arg(actualValue, 0, 'g', 9).arg(referenceValue, 0, 'g', 9)));
		}
		song->drainAutomationRecordingEvents();
		song->clearProject();
	}

	void repeatedEqualRecordingRemovesTheExistingNodeLikeRecordValue()
	{
		auto* song = Engine::getSong();
		song->drainAutomationRecordingEvents();
		song->clearProject();
		AutomationClip actual(nullptr), reference(nullptr);
		for (AutomationClip* clip : {&actual, &reference})
		{
			clip->setProgressionType(AutomationClip::ProgressionType::Linear);
			clip->putValue(TimePos(0), 0.125f, false, true);
			clip->putValue(TimePos(4), 0.75f, false, true);
			clip->putValue(TimePos(8), 0.875f, false, true);
			clip->changeLength(TimePos(16));
		}
		const auto id = static_cast<const JournallingObject&>(actual).id();
		reference.recordValue(TimePos(2), 0.0f);
		QVERIFY(song->enqueueAutomationRecord(id, actual.recordingMapTime(TimePos(2)), 0.0f));
		QVERIFY(!reference.getTimeMap().contains(2));
		for (const auto& event : {SweepEvent{2, 0.5f}, SweepEvent{4, 0.5f}})
		{
			reference.recordValue(TimePos(event.tick), event.value);
			QVERIFY(song->enqueueAutomationRecord(id, actual.recordingMapTime(TimePos(event.tick)), event.value));
		}
		QVERIFY(reference.getTimeMap().contains(2));
		QVERIFY(!reference.getTimeMap().contains(4));
		for (int tick = 0; tick <= 8; ++tick)
		{
			const float actualValue = actual.valueAt(TimePos(tick));
			const float referenceValue = reference.valueAt(TimePos(tick));
			QVERIFY2(bits(actualValue) == bits(referenceValue),
				qPrintable(QStringLiteral("repeated equal recording differs at tick %1 (%2 vs %3)")
					.arg(tick).arg(actualValue, 0, 'g', 9).arg(referenceValue, 0, 'g', 9)));
		}
		song->drainAutomationRecordingEvents();
		song->clearProject();
	}

	void renderedCubicAndRepeatedEqualRecordingMatchSynchronousReplay()
	{
		auto* song = Engine::getSong();
		auto& switches = AutomationSwitches::instance();
		struct RestoreSwitches
		{
			bool reads;
			bool writes;
			bool punched;
			~RestoreSwitches() { AutomationSwitches::instance().set(reads, writes, punched); }
		} restore{switches.reads(), switches.writes(), switches.punchedWrites()};
		const auto buffer = makeRenderBuffer(Engine::audioEngine()->outputSampleRate());
		bool cubicMatches = true;
		bool repeatedEqualMatches = true;
		for (const auto& scenario : {
			std::pair{AutomationClip::ProgressionType::CubicHermite, false},
			std::pair{AutomationClip::ProgressionType::Linear, true}})
		{
			song->stop();
			song->clearProject();
			song->drainAutomationRecordingEvents();
			switches.set(true, false, false);
			const auto queued = renderSweep(song, buffer, scenario.first, scenario.second, nullptr);
			song->clearProject();
			const auto synchronous = renderSweep(song, buffer, scenario.first, scenario.second, &queued.events);
			const auto mismatch = firstDifferentFrame(queued.frames, synchronous.frames);
			const bool matches = mismatch == queued.frames.size() && queued.frames.size() == synchronous.frames.size();
			if (scenario.second) { repeatedEqualMatches = matches; }
			else { cubicMatches = matches; }
			song->clearProject();
		}
		QVERIFY2(cubicMatches && repeatedEqualMatches,
			qPrintable(QStringLiteral("render equivalence: cubic=%1, repeated-equal=%2")
				.arg(cubicMatches).arg(repeatedEqualMatches)));
	}

	void renderPeriodWithAutomationPlayingAndRecordingDoesNotAllocate()
	{
		auto* song = Engine::getSong();
		auto& switches = AutomationSwitches::instance();
		struct RestoreSwitches
		{
			bool reads;
			bool writes;
			bool punched;
			~RestoreSwitches() { AutomationSwitches::instance().set(reads, writes, punched); }
		} restore{switches.reads(), switches.writes(), switches.punchedWrites()};
		song->drainAutomationRecordingEvents();
		song->stop();
		song->clearProject();
		switches.set(true, true, false);
		const auto buffer = makeRenderBuffer(Engine::audioEngine()->outputSampleRate());
		const auto rig = makeRig(song, buffer, AutomationClip::ProgressionType::Linear, false);
		rig.automation->setRecording(true);
		song->tempoModel().setValue(90);
		song->setPlayPos(0, Song::PlayMode::Song);
		Engine::audioEngine()->renderNextPeriod();
		Engine::audioEngine()->renderNextPeriod();
		song->playSong();
		for (int period = 0; period < 3; ++period)
		{
			rig.source->volumeModel()->setValue(35.0f + period * 11.0f);
			Engine::audioEngine()->renderNextPeriod();
		}

		test::resetAllocationCount();
		std::uint64_t allocations = 0;
		for (int period = 0; period < 8; ++period)
		{
			rig.source->volumeModel()->setValue(70.0f + period * 3.0f);
			test::tlCountAllocations = true;
			Engine::audioEngine()->renderNextPeriod();
			test::tlCountAllocations = false;
			allocations += test::tlAllocationCount;
		}
		QCOMPARE(allocations, std::uint64_t{0});
		const auto status = song->automationRecordingStatus();
		QCOMPARE(status.overlayOverflows, std::uint64_t{0});
		QCOMPARE(status.renderScratchOverflows, std::uint64_t{0});
		song->stop();
		song->drainAutomationRecordingEvents();
		QVERIFY(rig.automation->getTimeMap().size() > 3);
		song->clearProject();
	}
};

QTEST_MAIN(AutomationRecordingOverlayTest)

#include "AutomationRecordingOverlayTest.moc"
