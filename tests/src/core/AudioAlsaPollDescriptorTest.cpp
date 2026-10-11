/*
 * AudioAlsaPollDescriptorTest.cpp - nonpositive ALSA poll counts are safe
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 */

#include "AudioAlsa.h"
#include "AudioEngine.h"
#include "ConfigManager.h"
#include "Engine.h"

#include <QtTest>

using namespace lmms;

namespace
{

int pollDescriptorCount = -1;
int pollDescriptorCountCalls = 0;

} // namespace

extern "C" int snd_pcm_poll_descriptors_count(snd_pcm_t*)
{
	++pollDescriptorCountCalls;
	return pollDescriptorCount;
}

class AudioAlsaPollDescriptorTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		ConfigManager::inst()->setValue(QStringLiteral("audioalsa"), QStringLiteral("device"), QStringLiteral("null"));
		Engine::init(true);
		QVERIFY(Engine::audioEngine() != nullptr);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void negativeDescriptorCountDoesNotBreakConstruction()
	{
		pollDescriptorCountCalls = 0;
		bool successful = false;
		bool constructionCompleted = false;
		try
		{
			AudioAlsa device(successful, Engine::audioEngine());
			constructionCompleted = true;
		}
		catch (...)
		{
			// The regression is that a negative ALSA count becomes an invalid
			// allocation size before the device can report successful setup.
		}

		QCOMPARE(pollDescriptorCountCalls, 1);
		QVERIFY2(constructionCompleted, "a negative ALSA descriptor count must skip CLOEXEC setup");
		QVERIFY(successful);
	}
};

QTEST_GUILESS_MAIN(AudioAlsaPollDescriptorTest)

#include "AudioAlsaPollDescriptorTest.moc"
