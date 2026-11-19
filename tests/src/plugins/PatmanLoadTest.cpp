/*
 * PatmanLoadTest.cpp - malformed patch input regression coverage
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>
#include <thread>
#include <vector>

#include "AudioEngine.h"
#include "AudioResampler.h"
#include "Engine.h"
#include "ExportRenderSettings.h"
#include "InstrumentTrack.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "Patman.h"
#include "PathUtil.h"
#include "RealtimeScope.h"
#include "SampleFrame.h"
#include "Song.h"

namespace
{

constexpr std::size_t MaxArrayAllocationForTest = 64U * 1024U * 1024U;
std::atomic<bool> rejectLargeArrayAllocations{false};

class LargeArrayAllocationGuard
{
public:
	LargeArrayAllocationGuard() { rejectLargeArrayAllocations.store(true); }
	~LargeArrayAllocationGuard() { rejectLargeArrayAllocations.store(false); }
};

constexpr qsizetype PatchHeaderSize = 239;
constexpr qsizetype PatchSampleHeaderSize = 96;

void writeLittleEndian16(QByteArray& data, qsizetype offset, std::uint16_t value)
{
	const int at = static_cast<int>(offset);
	for (int i = 0; i < 2; ++i)
	{
		data[at + i] = static_cast<char>((value >> (8 * i)) & 0xff);
	}
}

void writeLittleEndian32(QByteArray& data, qsizetype offset, std::uint32_t value)
{
	const int at = static_cast<int>(offset);
	for (int i = 0; i < 4; ++i)
	{
		data[at + i] = static_cast<char>((value >> (8 * i)) & 0xff);
	}
}

//! A valid GF1 patch. The default four-frame sample keeps the load tests
//! small; the render tests ask for a longer square wave so two consecutive
//! render blocks are both audible.
auto makePatch(qsizetype sampleFrames = 4) -> QByteArray
{
	auto patch = QByteArray(PatchHeaderSize + PatchSampleHeaderSize + sampleFrames, '\0');
	std::memcpy(patch.data(), "GF1PATCH110\0ID#000002", 22);
	patch[198] = 1;

	const qsizetype sample = PatchHeaderSize;
	writeLittleEndian32(patch, sample + 8, static_cast<std::uint32_t>(sampleFrames));
	writeLittleEndian32(patch, sample + 16, static_cast<std::uint32_t>(sampleFrames));
	writeLittleEndian16(patch, sample + 20, 22050);
	writeLittleEndian32(patch, sample + 30, 440000);
	const int sampleAt = static_cast<int>(PatchHeaderSize + PatchSampleHeaderSize);
	for (qsizetype i = 0; i < sampleFrames; ++i)
	{
		patch[sampleAt + static_cast<int>(i)] = static_cast<char>((i % 8) < 4 ? 64 : -64);
	}
	return patch;
}

bool writePatch(const QString& filename, qsizetype sampleFrames = 4)
{
	QFile file(filename);
	if (!file.open(QIODevice::WriteOnly)) { return false; }
	const auto patch = makePatch(sampleFrames);
	if (file.write(patch) != static_cast<qint64>(patch.size())) { return false; }
	file.close();
	return true;
}

//! The patch path the instrument publishes: the part of the patch state the
//! audio thread reads (playNoteImpl checks it before it renders).
auto patmanSource(lmms::PatmanInstrument& instrument) -> QString
{
	QDomDocument doc;
	QDomElement element = doc.createElement("instrument");
	doc.appendChild(element);
	instrument.saveSettings(doc, element);
	return element.attribute("src");
}

} // namespace

void* operator new[](std::size_t size)
{
	if (rejectLargeArrayAllocations.load() && size > MaxArrayAllocationForTest)
	{
		throw std::bad_alloc{};
	}
	if (auto* memory = std::malloc(size == 0 ? 1 : size)) { return memory; }
	throw std::bad_alloc{};
}

void operator delete[](void* memory) noexcept
{
	std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
	std::free(memory);
}

namespace lmms
{

namespace
{

//! The engine entry point the SRC setting uses: it prepares every retained
//! converter (pool, sample voices, instrument notes, play handles) and only
//! then publishes and adopts the new quality.
auto switchQuality(SrcQuality quality) -> bool
{
	AudioEngine* const engine = Engine::audioEngine();
	return engine != nullptr
		&& engine->applyConverterModeToRetainedStates(AudioResampler::modeForSrcQuality(quality), quality);
}

auto acquireNote(InstrumentTrack& track, f_cnt_t frames) -> NotePlayHandle*
{
	return NotePlayHandleManager::acquire(&track, 0, frames, Note(TimePos(), TimePos(), 60, 100));
}

//! Render exactly one block. Clear first so a short or failed render is
//! reported as silence instead of stale samples from the previous block.
void renderBlock(PatmanInstrument& instrument, NotePlayHandle* note, std::vector<SampleFrame>& block)
{
	std::fill(block.begin(), block.end(), SampleFrame{});
	instrument.playNote(note, std::span<SampleFrame>(block.data(), block.size()));
}

auto hasAudio(const std::vector<SampleFrame>& block) -> bool
{
	for (const SampleFrame& frame : block)
	{
		if (frame.left() != 0.0f || frame.right() != 0.0f) { return true; }
	}
	return false;
}

auto blocksMatch(const std::vector<SampleFrame>& left, const std::vector<SampleFrame>& right) -> bool
{
	return left.size() == right.size()
		&& std::memcmp(left.data(), right.data(), left.size() * sizeof(SampleFrame)) == 0;
}

//! One real Patman note, primed on block 0, with block 1 rendered outside the
//! realtime scope. Every note of the same pitch and patch renders block 1 bit
//! for bit, so this is the deterministic oracle for one quality.
auto referenceBlock(InstrumentTrack& track, PatmanInstrument& instrument, f_cnt_t frames)
	-> std::vector<SampleFrame>
{
	std::vector<SampleFrame> block(static_cast<std::size_t>(frames));
	NotePlayHandle* const note = acquireNote(track, frames * 4);
	renderBlock(instrument, note, block); // block 0 primes the converter
	renderBlock(instrument, note, block); // block 1 is the oracle
	instrument.deleteNotePluginData(note);
	note->m_pluginData = nullptr;
	NotePlayHandleManager::release(note);
	return block;
}

} // namespace

class PatmanLoadTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void patmanPatchLoadWaitsForTheRenderPeriodToReleaseTheModel();

	void qualityChangeKeepsHeldPatmanSlotsAtTheNewMode();

	void lazyRenderPathConversionIsDetected();

	void rejectsHugeTruncatedSampleWithoutAllocating()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		QByteArray patch(239 + 96, '\0');
		patch.replace(0, 22, QByteArray{"GF1PATCH110\0ID#000002", 22});
		patch[198] = 1;
		for (int i = 0; i < 4; ++i) { patch[239 + 8 + i] = static_cast<char>(0xff); }
		patch[239 + 55] = static_cast<char>(MODES_16BIT);
		const auto filename = directory.filePath("oversized-truncated.pat");
		QFile file(filename);
		QVERIFY(file.open(QIODevice::WriteOnly));
		QCOMPARE(file.write(patch), static_cast<qint64>(patch.size()));
		file.close();

		InstrumentTrack track{Engine::getSong()};
		PatmanInstrument instrument{&track};
		bool allocationThrew = false;
		{
			LargeArrayAllocationGuard allocationGuard;
			try
			{
				instrument.loadFile(filename);
			}
			catch (const std::bad_alloc&)
			{
				allocationThrew = true;
			}
		}
		QVERIFY2(!allocationThrew, "truncated patch length reached the sample allocation");
	}

	void patmanVoiceUsesPreparedConverterOnRenderPath()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString patchPath = directory.filePath(QStringLiteral("voice.pat"));
		QVERIFY(writePatch(patchPath));

		InstrumentTrack track{Engine::getSong()};
		PatmanInstrument instrument{&track};
		instrument.loadFile(patchPath);
		QVERIFY(!instrument.nodeName().isEmpty());
		QVERIFY(ExportRenderSettings::setSrcQuality(SrcQuality::SincBest));

		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		NotePlayHandle* const note = NotePlayHandleManager::acquire(&track, 0, frames * 2,
			Note(TimePos(), TimePos(), 60, 100));
		std::vector<SampleFrame> output(static_cast<std::size_t>(frames));
		AudioResampler::resetRealtimeConverterAllocationsForTest();
		AudioResampler::resetRealtimeConverterDeletionsForTest();
		{
			const RealtimeScope realtime;
			instrument.playNote(note, output);
		}
		const auto realtimeAllocations = AudioResampler::realtimeConverterAllocationsForTest();
		const auto realtimeDeletions = AudioResampler::realtimeConverterDeletionsForTest();
		instrument.deleteNotePluginData(note);
		note->m_pluginData = nullptr;
		NotePlayHandleManager::release(note);
		QVERIFY(ExportRenderSettings::setSrcQuality(SrcQuality::Linear));
		QCOMPARE(realtimeAllocations, std::uint64_t{0});
		QCOMPARE(realtimeDeletions, std::uint64_t{0});
	}

	void overPolyphonyKeepsEverySincBestBlockAudible();

	void sincBestReferenceDiffersFromLinearReference();
};

//! The audio thread picks its sample out of the patch bank while it renders
//! (playNoteImpl -> selectSample), and the render thread owns the engine's
//! model state for a whole render period. Replacing the patch must therefore
//! not land in the middle of a render period: the bank and the patch path are
//! read by the audio thread.
void PatmanLoadTest::patmanPatchLoadWaitsForTheRenderPeriodToReleaseTheModel()
{
	QTemporaryDir directory;
	QVERIFY(directory.isValid());

	const QString firstPatch = directory.filePath(QStringLiteral("first.pat"));
	const QString secondPatch = directory.filePath(QStringLiteral("second.pat"));
	QVERIFY(writePatch(firstPatch));
	QVERIFY(writePatch(secondPatch));

	InstrumentTrack track{Engine::getSong()};
	PatmanInstrument instrument{&track};
	instrument.loadFile(firstPatch);
	QCOMPARE(patmanSource(instrument), PathUtil::toShortestRelative(firstPatch));

	AudioEngine* const engine = Engine::audioEngine();
	QVERIFY(engine != nullptr);

	// The render thread holds the engine's model-change mutex for a whole
	// render period (AudioEngine::renderNextPeriod): the patch state the
	// audio thread reads must not change while it owns it.
	engine->requestChangeInModel();

	std::atomic<bool> finished{false};
	std::thread loader([&instrument, &secondPatch, &finished] {
		instrument.setFile(secondPatch, false);
		finished.store(true, std::memory_order_release);
	});

	// The outcome is an ordering of events, not a duration. While the render
	// period owns the model state, the load either reaches the model-change
	// guard and waits there (a control thread parked inside
	// AudioEngine::requestChangeInModel, counted by modelChangeWaiters()), or
	// it completes without ever waiting. Whichever happens first decides; a
	// load that reaches neither state leaves the loop - and the test - hung,
	// which a timeout reports as a failure, never as a pass.
	while (!finished.load(std::memory_order_acquire) &&
			engine->modelChangeWaiters() == 0)
	{
		std::this_thread::yield();
	}
	const bool completedWhileRenderPeriodOwned = finished.load(std::memory_order_acquire);

	engine->doneChangeInModel();
	loader.join();

	QVERIFY2(!completedWhileRenderPeriodOwned,
		"the patch was replaced while the render period owned the model state");
	// The deferred load still lands once the render period is over.
	QCOMPARE(patmanSource(instrument), PathUtil::toShortestRelative(secondPatch));
}

//! A quality change runs through AudioEngine::applyConverterModeToRetainedStates,
//! not through the settings store alone. Hold real Patman notes (and fill every
//! other slot so the released note slots are the next ones acquire() hands out),
//! switch quality, render the held notes under RealtimeScope, then release and
//! reacquire. A slot skipped by prepareAll() and never reached by the note hook
//! would still say "old quality" here, and the next acquire() would hand it out.
void PatmanLoadTest::qualityChangeKeepsHeldPatmanSlotsAtTheNewMode()
{
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString patchPath = directory.filePath(QStringLiteral("held.pat"));
	QVERIFY(writePatch(patchPath, 4096));

	InstrumentTrack track{Engine::getSong()};
	auto* const instrument = new PatmanInstrument(&track);
	track.setInstrumentForTest(instrument); // the engine routes note hooks through track->instrument()
	instrument->loadFile(patchPath);
	QVERIFY(!instrument->nodeName().isEmpty());

	AudioEngine* const engine = Engine::audioEngine();
	QVERIFY(engine != nullptr);
	QVERIFY(switchQuality(SrcQuality::Linear));

	const f_cnt_t frames = engine->framesPerPeriod();
	const std::size_t capacity = AudioResampler::realtimeVoiceConverterCapacityForTest();
	const std::size_t noteCount = 3;
	QVERIFY(capacity > noteCount + 1);
	const std::size_t fillerCount = capacity - noteCount;

	for (const SrcQuality target : {SrcQuality::SincBest, SrcQuality::Linear})
	{
		const std::size_t baseline = AudioResampler::freeRealtimeVoiceConvertersForTest();
		QCOMPARE(baseline, capacity);
		const auto expectedMode = AudioResampler::modeForSrcQuality(target);

		// Occupy every slot except the ones the notes will take. These voices are
		// deliberately not retained states the engine can walk: prepareAll must
		// skip them, and the note hook is the only path that reaches held slots.
		std::vector<AudioResampler> fillers;
		fillers.reserve(fillerCount);
		for (std::size_t i = 0; i < fillerCount; ++i)
		{
			fillers.emplace_back(AudioResampler::RealtimeVoiceTag{});
		}
		QCOMPARE(AudioResampler::freeRealtimeVoiceConvertersForTest(), baseline - fillerCount);

		std::vector<NotePlayHandle*> notes;
		notes.reserve(noteCount);
		std::vector<SampleFrame> block(static_cast<std::size_t>(frames));
		for (std::size_t i = 0; i < noteCount; ++i)
		{
			NotePlayHandle* const note = acquireNote(track, frames * 8);
			QVERIFY(note->m_pluginData == nullptr);
			renderBlock(*instrument, note, block); // creates the note's pool slot
			QVERIFY(note->m_pluginData != nullptr);
			QVERIFY(engine->addPlayHandle(note)); // makes the note reachable by the hook
			notes.push_back(note);
		}
		QCOMPARE(AudioResampler::freeRealtimeVoiceConvertersForTest(), baseline - capacity);

		QVERIFY(switchQuality(target));
		QCOMPARE(ExportRenderSettings::srcQuality(), target);

		AudioResampler::resetRealtimeConverterAllocationsForTest();
		AudioResampler::resetRealtimeConverterDeletionsForTest();
		{
			const RealtimeScope realtime;
			for (NotePlayHandle* const note : notes) { renderBlock(*instrument, note, block); }
		}
		QCOMPARE(AudioResampler::realtimeConverterAllocationsForTest(), std::uint64_t{0});
		QCOMPARE(AudioResampler::realtimeConverterDeletionsForTest(), std::uint64_t{0});

		for (NotePlayHandle* const note : notes)
		{
			instrument->deleteNotePluginData(note);
			note->m_pluginData = nullptr;
		}
		for (NotePlayHandle* const note : notes)
		{
			// removePlayHandle() disposes the handle, which releases a
			// NotePlayHandle back to NotePlayHandleManager itself.
			engine->removePlayHandle(note);
		}
		QCOMPARE(AudioResampler::freeRealtimeVoiceConvertersForTest(), baseline - fillerCount);

		// The only free slots are the three the notes just released.
		std::vector<AudioResampler> reacquired;
		reacquired.reserve(noteCount);
		for (std::size_t i = 0; i < noteCount; ++i)
		{
			reacquired.emplace_back(AudioResampler::RealtimeVoiceTag{});
			QCOMPARE(reacquired.back().mode(), expectedMode);
		}
		QCOMPARE(AudioResampler::freeRealtimeVoiceConvertersForTest(), baseline - capacity);
		reacquired.clear();
		QCOMPARE(AudioResampler::freeRealtimeVoiceConvertersForTest(), baseline - fillerCount);

		fillers.clear();
		QCOMPARE(AudioResampler::freeRealtimeVoiceConvertersForTest(), baseline);
	}

	QVERIFY(switchQuality(SrcQuality::Linear));
}

//! T4: the review's lazy-conversion scenario. Three real notes hold their pool
//! slots across a quality switch; every note's own PlaybackState already says
//! the new quality, and the render that follows creates and deletes nothing.
void PatmanLoadTest::lazyRenderPathConversionIsDetected()
{
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString patchPath = directory.filePath(QStringLiteral("lazy.pat"));
	QVERIFY(writePatch(patchPath, 4096));

	InstrumentTrack track{Engine::getSong()};
	auto* const instrument = new PatmanInstrument(&track);
	track.setInstrumentForTest(instrument); // the engine routes note hooks through track->instrument()
	instrument->loadFile(patchPath);
	QVERIFY(!instrument->nodeName().isEmpty());

	AudioEngine* const engine = Engine::audioEngine();
	QVERIFY(engine != nullptr);
	QVERIFY(switchQuality(SrcQuality::Linear));
	const f_cnt_t frames = engine->framesPerPeriod();
	std::vector<NotePlayHandle*> notes;
	std::vector<SampleFrame> block(static_cast<std::size_t>(frames));
	for (std::size_t i = 0; i < 3; ++i)
	{
		NotePlayHandle* const note = acquireNote(track, frames * 8);
		renderBlock(*instrument, note, block); QVERIFY(engine->addPlayHandle(note)); // creates the slot, hook-reachable
		notes.push_back(note);
	}
	QVERIFY(switchQuality(SrcQuality::SincBest)); const auto expectedMode = AudioResampler::modeForSrcQuality(SrcQuality::SincBest);

	// The switch reached the states the notes already hold: this render must
	// not discover an old converter and replace it. The mode is checked after
	// the counted render so a lazy conversion that fixes the mode is still
	// caught by the creation/deletion counters.
	const auto countedRender = [&](NotePlayHandle* const note) {
		AudioResampler::resetRealtimeConverterAllocationsForTest(); AudioResampler::resetRealtimeConverterDeletionsForTest();
		const RealtimeScope realtime;
		renderBlock(*instrument, note, block);
	};
	AudioResampler::resetRealtimeConverterAllocationsForTest(); AudioResampler::resetRealtimeConverterDeletionsForTest();
	{
		const RealtimeScope realtime;
		for (NotePlayHandle* const note : notes) { renderBlock(*instrument, note, block); }
	}
	QCOMPARE(AudioResampler::realtimeConverterAllocationsForTest(), std::uint64_t{0});
	QCOMPARE(AudioResampler::realtimeConverterDeletionsForTest(), std::uint64_t{0});
	for (NotePlayHandle* const note : notes)
	{
		auto* const handle = static_cast<PatmanInstrument::handle_data*>(note->m_pluginData); QVERIFY(handle != nullptr && handle->state != nullptr); QCOMPARE(handle->state->converterMode(), expectedMode);
	}

	// A state the engine has prepared but not adopted yet must stay pending
	// through the render: the render path itself must never swap converters.
	const auto pendingMode = AudioResampler::modeForSrcQuality(SrcQuality::SincMedium); QVERIFY(instrument->prepareConverterModeForNote(notes.front(), pendingMode));
	countedRender(notes.front());
	QCOMPARE(AudioResampler::realtimeConverterDeletionsForTest(), std::uint64_t{0});
	QCOMPARE(AudioResampler::realtimeConverterAllocationsForTest(), std::uint64_t{0});

	for (NotePlayHandle* const note : notes)
	{
		instrument->deleteNotePluginData(note); note->m_pluginData = nullptr; engine->removePlayHandle(note);
	}
	QVERIFY(switchQuality(SrcQuality::Linear));
}

//! More simultaneous Patman voices than the claimed 128/129 limit, all at
//! SincBest. Every block must be audible, bit-for-bit equal to a SincBest
//! reference, and rendered without creating or deleting a converter. The
//! global pool is far larger than this, so "capacity + k" is bounded at the
//! audited 200-voice case; the hook capacity is the whole global pool (thousands
//! of slots) and exceeding it is not a polyphony scenario.
void PatmanLoadTest::overPolyphonyKeepsEverySincBestBlockAudible()
{
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString patchPath = directory.filePath(QStringLiteral("poly.pat"));
	QVERIFY(writePatch(patchPath, 4096));

	InstrumentTrack track{Engine::getSong()};
	PatmanInstrument instrument{&track};
	instrument.loadFile(patchPath);
	QVERIFY(!instrument.nodeName().isEmpty());

	AudioEngine* const engine = Engine::audioEngine();
	QVERIFY(engine != nullptr);
	QVERIFY(switchQuality(SrcQuality::SincBest));

	const f_cnt_t frames = engine->framesPerPeriod();
	constexpr std::size_t Polyphony = 200;
	constexpr std::size_t ExtraOverPolyphony = 32;
	constexpr std::size_t NoteCount = Polyphony + ExtraOverPolyphony;
	const std::size_t capacity = AudioResampler::realtimeVoiceConverterCapacityForTest();
	QVERIFY(capacity > NoteCount);
	const std::size_t freeBefore = AudioResampler::freeRealtimeVoiceConvertersForTest();

	const std::vector<SampleFrame> sincBestReference = referenceBlock(track, instrument, frames);

	std::vector<NotePlayHandle*> notes;
	notes.reserve(NoteCount);
	std::vector<SampleFrame> block(static_cast<std::size_t>(frames));
	for (std::size_t i = 0; i < NoteCount; ++i)
	{
		NotePlayHandle* const note = acquireNote(track, frames * 4);
		renderBlock(instrument, note, block); // prime block 0
		notes.push_back(note);
	}
	QCOMPARE(AudioResampler::freeRealtimeVoiceConvertersForTest(), freeBefore - NoteCount);

	std::size_t silent = 0;
	std::size_t mismatched = 0;
	AudioResampler::resetRealtimeConverterAllocationsForTest();
	{
		const RealtimeScope realtime;
		for (NotePlayHandle* const note : notes)
		{
			renderBlock(instrument, note, block);
			silent += !hasAudio(block);
			mismatched += !blocksMatch(block, sincBestReference);
		}
	}
	const std::uint64_t realtimeAllocations = AudioResampler::realtimeConverterAllocationsForTest();
	QCOMPARE(silent, std::size_t{0});
	QCOMPARE(mismatched, std::size_t{0});
	QCOMPARE(realtimeAllocations, std::uint64_t{0});
	QCOMPARE(AudioResampler::realtimeVoiceConverterCapacityForTest(), capacity);
	QCOMPARE(AudioResampler::freeRealtimeVoiceConvertersForTest(), freeBefore - NoteCount);

	// The helper must actually distinguish the qualities: a Linear oracle must
	// not be accepted as a SincBest reference.
	QVERIFY(switchQuality(SrcQuality::Linear));
	const std::vector<SampleFrame> linearReference = referenceBlock(track, instrument, frames);
	QVERIFY(hasAudio(linearReference));
	QVERIFY(!blocksMatch(sincBestReference, linearReference));

	// A held note must follow the switch without converting during render. These
	// notes are reachable by the engine hook, so the switch itself has to move
	// their converter; a render that lazily converts would allocate and free.
	{
		const std::size_t heldBaseline = AudioResampler::freeRealtimeVoiceConvertersForTest();
		std::vector<NotePlayHandle*> held;
		for (std::size_t i = 0; i < 3; ++i)
		{
			NotePlayHandle* const note = acquireNote(track, frames * 8);
			renderBlock(instrument, note, block); QVERIFY(engine->addPlayHandle(note));
			held.push_back(note);
		}
		QVERIFY(switchQuality(SrcQuality::SincBest));
		AudioResampler::resetRealtimeConverterAllocationsForTest();
		AudioResampler::resetRealtimeConverterDeletionsForTest();
		{
			const RealtimeScope realtime;
			for (NotePlayHandle* const note : held) { renderBlock(instrument, note, block); }
		}
		QCOMPARE(AudioResampler::realtimeConverterAllocationsForTest(), std::uint64_t{0});
		QCOMPARE(AudioResampler::realtimeConverterDeletionsForTest(), std::uint64_t{0});
		for (NotePlayHandle* const note : held)
		{
			instrument.deleteNotePluginData(note); note->m_pluginData = nullptr; engine->removePlayHandle(note);
		}
		QVERIFY(switchQuality(SrcQuality::Linear)); QCOMPARE(AudioResampler::freeRealtimeVoiceConvertersForTest(), heldBaseline);
	}

	for (NotePlayHandle* const note : notes)
	{
		instrument.deleteNotePluginData(note); note->m_pluginData = nullptr; NotePlayHandleManager::release(note);
	}
	QCOMPARE(AudioResampler::freeRealtimeVoiceConvertersForTest(), freeBefore);
	QVERIFY(switchQuality(SrcQuality::Linear));
}

//! T3: the comparison helper reports a mismatch across qualities. If it did
//! not, T2's "matches SincBest, differs from Linear" would be vacuous.
void PatmanLoadTest::sincBestReferenceDiffersFromLinearReference()
{
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString patchPath = directory.filePath(QStringLiteral("oracle.pat"));
	QVERIFY(writePatch(patchPath, 4096));

	InstrumentTrack track{Engine::getSong()};
	PatmanInstrument instrument{&track};
	instrument.loadFile(patchPath);
	QVERIFY(!instrument.nodeName().isEmpty());

	AudioEngine* const engine = Engine::audioEngine();
	QVERIFY(engine != nullptr);
	const f_cnt_t frames = engine->framesPerPeriod();

	QVERIFY(switchQuality(SrcQuality::SincBest));
	const std::vector<SampleFrame> sincBest = referenceBlock(track, instrument, frames);
	QVERIFY(switchQuality(SrcQuality::Linear));
	const std::vector<SampleFrame> linear = referenceBlock(track, instrument, frames);

	QVERIFY(hasAudio(sincBest));
	QVERIFY(hasAudio(linear));
	QVERIFY2(!blocksMatch(sincBest, linear), "the SincBest oracle is indistinguishable from Linear");
}

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::PatmanLoadTest)

#include "PatmanLoadTest.moc"
