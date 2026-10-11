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

#include <QFile>
#include <QTemporaryDir>

#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <new>

#include "Engine.h"
#include "InstrumentTrack.h"
#include "Patman.h"
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

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::PatmanLoadTest)

#include "PatmanLoadTest.moc"
