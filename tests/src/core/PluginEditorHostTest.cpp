/*
 * PluginEditorHostTest.cpp - R4.1: a plugin's descriptors fire and its timers run
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

/*! The plan's acceptance for R4.1: "a fake plugin's fd fires its handler, and a timer fires at
 *  its interval". The fake plugin is a pipe and a counter - exactly what an editor does with
 *  its X11 connection and its repaint timer - driven through the real Qt event loop.
 */

#include <QtTest>

#include <unistd.h>

#include <QElapsedTimer>

#include "PluginEditorHost.h"

using namespace lmms;

class PluginEditorHostTest : public QObject
{
	Q_OBJECT

private slots:
	void aReadableDescriptorFiresItsHandler()
	{
		int fds[2] = {-1, -1};
		QVERIFY(::pipe(fds) == 0);
		PluginEditorHost host;
		int fired = 0;
		unsigned seen = 0;
		QVERIFY(host.registerFd(fds[0], PluginEditorHost::Read, [&](int fd, unsigned flags) {
			char byte = 0;
			QCOMPARE(::read(fd, &byte, 1), ssize_t{1});
			++fired;
			seen = flags;
		}));
		QVERIFY2(!host.registerFd(fds[0], PluginEditorHost::Read, [](int, unsigned) {}), "a second registration was merged");
		QTest::qWait(30);
		QCOMPARE(fired, 0);  // nothing to read yet
		QCOMPARE(::write(fds[1], "x", 1), ssize_t{1});
		QTRY_COMPARE(fired, 1);
		QCOMPARE(seen, unsigned{PluginEditorHost::Read});

		// Unregistered, the descriptor fires nothing more.
		QVERIFY(host.unregisterFd(fds[0]));
		QCOMPARE(::write(fds[1], "y", 1), ssize_t{1});
		QTest::qWait(50);
		QCOMPARE(fired, 1);
		QVERIFY(!host.unregisterFd(fds[0]));
		::close(fds[0]);
		::close(fds[1]);
	}

	void aCallbackMayUnregisterItsOwnDescriptor()
	{
		int fds[2] = {-1, -1};
		QVERIFY(::pipe(fds) == 0);
		PluginEditorHost host;
		int fired = 0;
		QVERIFY(host.registerFd(fds[0], PluginEditorHost::Read, [&](int fd, unsigned) {
			++fired;
			host.unregisterFd(fd);
		}));
		QCOMPARE(::write(fds[1], "x", 1), ssize_t{1});
		QTRY_COMPARE(fired, 1);
		QCOMPARE(host.fdCount(), 0);
		QTest::qWait(50);  // the unread byte must not fire a destroyed entry
		QCOMPARE(fired, 1);
		::close(fds[0]);
		::close(fds[1]);
	}

	void aTimerFiresAtItsIntervalAndStopsWhenUnregistered()
	{
		PluginEditorHost host;
		std::uint32_t id = 0;
		int ticks = 0;
		QElapsedTimer clock;
		clock.start();
		QVERIFY(host.registerTimer(20, [&](std::uint32_t timer) { QCOMPARE(timer, id); ++ticks; }, &id));
		QVERIFY(id != 0);
		QTest::qWait(210);
		const qint64 elapsed = clock.elapsed();
		std::printf("EDITOR_HOST_EVIDENCE 20 ms timer: %d ticks in %lld ms\n", ticks, static_cast<long long>(elapsed));
		// About elapsed/20; a loaded CI machine runs late, never early.
		QVERIFY2(ticks >= 5 && ticks <= elapsed / 20 + 1, qPrintable(QStringLiteral("%1 ticks").arg(ticks)));
		QVERIFY(host.unregisterTimer(id));
		const int stopped = ticks;
		QTest::qWait(80);
		QCOMPARE(ticks, stopped);
		std::uint32_t second = 0;
		QVERIFY(host.registerTimer(20, [](std::uint32_t) {}, &second));
		QVERIFY2(second != id, "a timer id was reused");
		QCOMPARE(host.timerCount(), 1);
	}
};

QTEST_GUILESS_MAIN(PluginEditorHostTest)
#include "PluginEditorHostTest.moc"
