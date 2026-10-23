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

#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <new>
#include <thread>

#include "AudioEngine.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Patman.h"
#include "PathUtil.h"
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

//! A valid one-sample GF1 patch.
auto makePatch() -> QByteArray
{
	constexpr qsizetype SampleFrames = 4;
	auto patch = QByteArray(PatchHeaderSize + PatchSampleHeaderSize + SampleFrames, '\0');
	std::memcpy(patch.data(), "GF1PATCH110\0ID#000002", 22);
	patch[198] = 1;

	const qsizetype sample = PatchHeaderSize;
	writeLittleEndian32(patch, sample + 8, SampleFrames);
	writeLittleEndian32(patch, sample + 16, SampleFrames);
	writeLittleEndian16(patch, sample + 20, 22050);
	writeLittleEndian32(patch, sample + 30, 440000);
	const int sampleAt = static_cast<int>(PatchHeaderSize + PatchSampleHeaderSize);
	patch[sampleAt] = 32;
	patch[sampleAt + 1] = -32;
	patch[sampleAt + 2] = 64;
	patch[sampleAt + 3] = -64;
	return patch;
}

bool writePatch(const QString& filename)
{
	QFile file(filename);
	if (!file.open(QIODevice::WriteOnly)) { return false; }
	const auto patch = makePatch();
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

class PatmanLoadTest : public QObject
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

	void patmanPatchLoadWaitsForTheRenderPeriodToReleaseTheModel();

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

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::PatmanLoadTest)

#include "PatmanLoadTest.moc"
