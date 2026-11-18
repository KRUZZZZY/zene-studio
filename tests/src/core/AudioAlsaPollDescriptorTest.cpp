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

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

using namespace lmms;

namespace
{

int pollDescriptorCount = -1;
int pollDescriptorCountCalls = 0;
//! The descriptor the fake hands out when the retrieval FAILS. A real,
//! open descriptor, so the garbage the unfilled array carries is observable
//! through its FD_CLOEXEC flag instead of being an unusable number.
int failedRetrievalFd = -1;

} // namespace

extern "C" int snd_pcm_poll_descriptors_count(snd_pcm_t*)
{
	++pollDescriptorCountCalls;
	return pollDescriptorCount;
}

//! The failure the finding is about: ALSA could not fill the array, so it
//! reports an error and leaves the caller's entries as they were. The fake
//! hands out a valid descriptor instead, so a caller that trusts the array
//! marks THAT descriptor close-on-exec - visible in the test.
extern "C" int snd_pcm_poll_descriptors(snd_pcm_t*, struct pollfd* pfds, unsigned int nfds)
{
	for (unsigned int i = 0; i < nfds; ++i)
	{
		pfds[i].fd = failedRetrievalFd;
		pfds[i].events = 0;
		pfds[i].revents = 0;
	}
	return -1;
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

	void failedDescriptorRetrievalSkipsCloexecSetup()
	{
		// A descriptor that is open and has NO close-on-exec flag yet: the
		// one an unfilled pollfd array would carry into fcntl().
		FILE* const temp = tmpfile();
		QVERIFY(temp != nullptr);
		const int fd = fileno(temp);
		QVERIFY(fd >= 0);
		const int initialFlags = fcntl(fd, F_GETFD, 0);
		QVERIFY(initialFlags >= 0);
		QVERIFY(fcntl(fd, F_SETFD, initialFlags & ~FD_CLOEXEC) >= 0);

		pollDescriptorCount = 3;
		failedRetrievalFd = fd;
		pollDescriptorCountCalls = 0;

		bool successful = false;
		{
			AudioAlsa device(successful, Engine::audioEngine());
		}

		pollDescriptorCount = -1;
		failedRetrievalFd = -1;

		QCOMPARE(pollDescriptorCountCalls, 1);
		QVERIFY(successful);

		const int flags = fcntl(fd, F_GETFD, 0);
		QVERIFY(flags >= 0);
		QVERIFY2((flags & FD_CLOEXEC) == 0,
			"a failed snd_pcm_poll_descriptors must not mark an unrelated descriptor close-on-exec");

		fclose(temp);
	}
};

QTEST_GUILESS_MAIN(AudioAlsaPollDescriptorTest)

#include "AudioAlsaPollDescriptorTest.moc"
