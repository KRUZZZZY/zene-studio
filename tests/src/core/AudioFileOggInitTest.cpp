/*
 * AudioFileOggInitTest.cpp - destroying an Ogg writer after rejected setup
 *
 * Copyright (c) 2026 Zene Studio contributors
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

#include <QtTest>

#include <QTemporaryDir>

#include <array>
#include <cstddef>
#include <cstring>
#include <new>

#include "AudioFileOgg.h"
#include "Engine.h"

using namespace lmms;

class AudioFileOggInitTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		QVERIFY(Engine::audioEngine() != nullptr);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void failedEncoderInitializationCanBeDestroyed()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());

		OutputSettings settings(44100, 192, OutputSettings::BitDepth::Depth16Bit);
		alignas(AudioFileOgg) std::array<std::byte, sizeof(AudioFileOgg)> storage;
		std::memset(storage.data(), 0xa5, storage.size());

		bool successful = true;
		auto* device = new (storage.data()) AudioFileOgg(settings, 0, successful,
			directory.filePath("failed.ogg"), Engine::audioEngine());
		const bool rejected = !successful;

		// Poisoning the untouched codec objects makes the old destructor's reads
		// of state after the constructor's early return deterministic.
		device->~AudioFileOgg();
		QVERIFY(rejected);
	}
};

QTEST_GUILESS_MAIN(AudioFileOggInitTest)

#include "AudioFileOggInitTest.moc"
