/*
 * AutomationPatternTimeTest.cpp - selected-pattern automation time equivalence
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
#include "AutomationSwitches.h"
#include "AudioEngine.h"
#include "AutomationTrack.h"
#include "Engine.h"
#include "PatternClip.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

std::uint32_t bits(float value)
{
	return std::bit_cast<std::uint32_t>(value);
}

struct PatternAutomationRig
{
	PatternStore* patternStore = nullptr;
	PatternTrack* referencePatternTrack = nullptr;
	PatternTrack* patternTrack = nullptr;
	SampleTrack* source = nullptr;
	SampleClip* referenceSample = nullptr;
	SampleClip* sample = nullptr;
	AutomationTrack* automationTrack = nullptr;
	int patternIndex = -1;
};

struct PatternRenderTrace
{
	std::vector<SampleFrame> frames;
	std::vector<std::uint32_t> valueBits;
};

struct LegacyTrackContainerAccess : TrackContainer
{
	using TrackContainer::automatedValuesFromTracks;
};

enum class PatternPlaybackMode
{
	Editor,
	Arrangement
};

constexpr std::array<int, 4> PatternProbeTicks = {0, 24, 48, 72};

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

PatternAutomationRig makePatternAutomationRig(Song* song,
	const std::shared_ptr<const SampleBuffer>& buffer)
{
	auto* patternStore = Engine::patternStore();
	while (patternStore->numOfPatterns() <= 1) { new PatternTrack(song); }
	const int patternIndex = patternStore->numOfPatterns() - 1;
	auto* referencePatternTrack = PatternTrack::findPatternTrack(0);
	(void)referencePatternTrack->createClip(TimePos(0));
	auto* patternTrack = PatternTrack::findPatternTrack(patternIndex);
	(void)patternTrack->createClip(TimePos(0));
	auto* source = dynamic_cast<SampleTrack*>(Track::create(Track::Type::Sample, patternStore));
	auto* referenceSample = dynamic_cast<SampleClip*>(source->getClip(0));
	referenceSample->setSampleBuffer(buffer);
	auto* sample = dynamic_cast<SampleClip*>(source->getClip(patternIndex));
	sample->setSampleBuffer(buffer);
	auto* automationTrack = dynamic_cast<AutomationTrack*>(
		Track::create(Track::Type::Automation, patternStore));
	for (int index : {0, patternIndex})
	{
		auto* automation = dynamic_cast<AutomationClip*>(automationTrack->getClip(index));
		automation->addObject(source->volumeModel());
		automation->setProgressionType(AutomationClip::ProgressionType::Linear);
		automation->changeLength(TimePos(TimePos::ticksPerBar()));
		automation->putValue(TimePos(0), 0.0f, false, true);
		automation->putValue(TimePos(96), 200.0f, false, true);
	}
	patternStore->setCurrentPattern(patternIndex);
	return {patternStore, referencePatternTrack, patternTrack, source, referenceSample,
		sample, automationTrack, patternIndex};
}

std::array<float, PatternProbeTicks.size()> legacyPatternValues(const PatternAutomationRig& rig,
	int patternIndex)
{
	std::array<float, PatternProbeTicks.size()> values{};
	for (std::size_t index = 0; index < PatternProbeTicks.size(); ++index)
	{
		const auto map = rig.patternStore->automatedValuesAt(TimePos(PatternProbeTicks[index]), patternIndex);
		if (!map.contains(rig.source->volumeModel()))
		{
			qFatal("legacy pattern automation fixture did not resolve its model");
		}
		values[index] = map.value(rig.source->volumeModel());
	}
	return values;
}

PatternRenderTrace renderPatternAutomation(PatternAutomationRig& rig, Song* song,
	PatternPlaybackMode mode, int patternIndex, bool useLegacyValues)
{
	song->stop();
	Engine::audioEngine()->clear();
	// Clearing output buffers does not rewind a live SamplePlayHandle. Remove
	// handles from the preceding comparison so each trace starts at frame zero.
	rig.source->updateClips();
	rig.sample->setIsPlaying(false);
	rig.referenceSample->setIsPlaying(false);
	Engine::audioEngine()->renderNextPeriod();
	Engine::audioEngine()->renderNextPeriod(); // Flush output left by the preceding test/render.
	rig.patternTrack->setMuted(patternIndex != rig.patternIndex);
	rig.referencePatternTrack->setMuted(patternIndex != 0);
	rig.automationTrack->setMuted(false);
	song->tempoModel().setValue(10); // Keep a period within one tick for the legacy value driver.
	rig.source->volumeModel()->setValue(100.0f);
	rig.patternStore->setCurrentPattern(patternIndex);
	const auto playMode = mode == PatternPlaybackMode::Editor ? Song::PlayMode::Pattern : Song::PlayMode::Song;
	song->setPlayPos(0, playMode);
	song->getTimeline(playMode).setFrameOffset(0);
	if (mode == PatternPlaybackMode::Editor) { song->playPattern(); }
	else { song->playSong(); }

	PatternRenderTrace trace;
	trace.frames.reserve(PatternProbeTicks.size() * Engine::audioEngine()->framesPerPeriod());
	trace.valueBits.reserve(PatternProbeTicks.size());
	for (std::size_t index = 0; index < PatternProbeTicks.size(); ++index)
	{
		if (useLegacyValues)
		{
			const TimePos queryTime(PatternProbeTicks[index]);
			rig.automationTrack->setMuted(false);
			const auto values = mode == PatternPlaybackMode::Editor
				? rig.patternStore->automatedValuesAt(queryTime, patternIndex)
				: LegacyTrackContainerAccess::automatedValuesFromTracks(song->tracks(), queryTime);
			// Hold the mapped legacy value manually for this period while keeping
			// the production collector from also applying its value.
			rig.automationTrack->setMuted(true);
			auto* model = rig.source->volumeModel();
			if (!values.contains(model)) { qFatal("legacy pattern render did not resolve its model"); }
			model->setValue(model->effectiveAutomationValue(model->scaledValue(values.value(model))), true);
		}
		song->setPlayPos(PatternProbeTicks[index], playMode);
		song->getTimeline(playMode).setFrameOffset(0);
		const auto output = Engine::audioEngine()->renderNextPeriod();
		trace.frames.insert(trace.frames.end(), output.begin(), output.end());
		trace.valueBits.push_back(bits(rig.source->volumeModel()->value()));
	}
	song->stop();
	rig.source->updateClips();
	rig.sample->setIsPlaying(false);
	rig.referenceSample->setIsPlaying(false);
	rig.automationTrack->setMuted(false);
	return trace;
}

std::size_t firstDifferentFrame(const std::vector<SampleFrame>& left,
	const std::vector<SampleFrame>& right)
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

template<std::size_t N>
bool patternRenderMatches(const PatternRenderTrace& actual, const PatternRenderTrace& legacy,
	const std::array<float, N>& expectedValues)
{
	if (actual.valueBits.size() != N || legacy.valueBits.size() != N) { return false; }
	for (std::size_t index = 0; index < N; ++index)
	{
		if (actual.valueBits[index] != bits(expectedValues[index])
			|| legacy.valueBits[index] != bits(expectedValues[index])) { return false; }
	}
	return actual.frames.size() == legacy.frames.size()
		&& firstDifferentFrame(actual.frames, legacy.frames) == actual.frames.size();
}

QString firstStereo(const PatternRenderTrace& trace)
{
	return trace.frames.empty() ? QStringLiteral("empty")
		: QStringLiteral("%1,%2").arg(trace.frames.front()[0], 0, 'g', 9)
			.arg(trace.frames.front()[1], 0, 'g', 9);
}

} // namespace

class AutomationPatternTimeTest : public QObject
{
	Q_OBJECT

private:
	static std::pair<bool, float> collectModelValue(Song* song, PatternAutomationRig& rig,
		TrackContainer* container, TimePos time, int clipNum, std::uint64_t& allocations)
	{
		song->collectAutomatedValuesForRender(container, time, clipNum);
		test::resetAllocationCount();
		test::tlCountAllocations = true;
		song->collectAutomatedValuesForRender(container, time, clipNum);
		test::tlCountAllocations = false;
		allocations += test::tlAllocationCount;
		const auto& scratch = song->m_automationRenderScratch;
		for (std::size_t index = 0; index < scratch.valueCount; ++index)
		{
			if (scratch.values[index].model == rig.source->volumeModel())
			{
				return {true, scratch.values[index].value};
			}
		}
		return {false, 0.0f};
	}

	static bool collectorMatches(Song* song, PatternAutomationRig& rig,
		TrackContainer* container, int clipNum,
		const std::array<float, PatternProbeTicks.size()>& expected, std::uint64_t& allocations)
	{
		for (std::size_t index = 0; index < PatternProbeTicks.size(); ++index)
		{
			const auto actual = collectModelValue(song, rig, container,
				TimePos(PatternProbeTicks[index]), clipNum, allocations);
			if (!actual.first || bits(actual.second) != bits(expected[index])) { return false; }
		}
		return true;
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

	void nonzeroPatternAutomationMatchesLegacyValuesAndRenders()
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
		song->stop();
		song->drainAutomationRecordingEvents();
		song->clearProject();
		switches.set(true, false, false);
		const auto buffer = makeRenderBuffer(Engine::audioEngine()->outputSampleRate());
		auto rig = makePatternAutomationRig(song, buffer);
		QVERIFY(rig.patternIndex > 0);
		const auto expectedValues = legacyPatternValues(rig, rig.patternIndex);
		std::array<float, PatternProbeTicks.size()> expectedAppliedValues{};
		for (std::size_t index = 0; index < expectedValues.size(); ++index)
		{
			expectedAppliedValues[index] = rig.source->volumeModel()->effectiveAutomationValue(
				rig.source->volumeModel()->scaledValue(expectedValues[index]));
		}
		std::uint64_t collectorAllocations = 0;
		rig.patternTrack->setMuted(false);
		rig.referencePatternTrack->setMuted(true);
		const bool editorCollectorMatches = collectorMatches(song, rig, rig.patternStore,
			rig.patternIndex, expectedValues, collectorAllocations);
		const bool arrangementCollectorMatches = collectorMatches(song, rig, song,
			-1, expectedValues, collectorAllocations);

		const auto editorRender = renderPatternAutomation(rig, song, PatternPlaybackMode::Editor,
			rig.patternIndex, false);
		const auto editorReference = renderPatternAutomation(rig, song, PatternPlaybackMode::Editor,
			rig.patternIndex, true);
		const bool editorRenderMatches = patternRenderMatches(editorRender, editorReference,
			expectedAppliedValues);
		const auto arrangementRender = renderPatternAutomation(rig, song,
			PatternPlaybackMode::Arrangement, rig.patternIndex, false);
		const auto arrangementReference = renderPatternAutomation(rig, song,
			PatternPlaybackMode::Arrangement, rig.patternIndex, true);
		const bool arrangementRenderMatches = patternRenderMatches(arrangementRender,
			arrangementReference, expectedAppliedValues);
		QVERIFY2(editorCollectorMatches && arrangementCollectorMatches
			&& editorRenderMatches && arrangementRenderMatches
			&& collectorAllocations == 0,
			qPrintable(QStringLiteral("editor collector=%1 render=%2 (%3/%4); "
				"arrangement collector=%5 render=%6 (%7/%8); allocations=%9")
				.arg(editorCollectorMatches).arg(editorRenderMatches)
				.arg(firstStereo(editorRender), firstStereo(editorReference))
				.arg(arrangementCollectorMatches).arg(arrangementRenderMatches)
				.arg(firstStereo(arrangementRender), firstStereo(arrangementReference))
				.arg(collectorAllocations)));
		song->stop();
		song->clearProject();
	}
};

QTEST_MAIN(AutomationPatternTimeTest)

#include "AutomationPatternTimeTest.moc"
