/*
 * RecordToolsTest.cpp - comp edges and comp groups, metronome options, punch here, remaining record time
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

// Complete fixture method definitions; included once after the fixture declaration.

void RecordToolsTest::armedRoutesOnOneFilesystemUnderTwoMountPointsShareItsFreeSpace() {
#ifdef __linux__
	QTemporaryDir pool;
	QVERIFY2(pool.isValid(), "could not make a pool directory");
	QVERIFY(QDir().mkpath(pool.filePath(QStringLiteral("a"))));
	QVERIFY(QDir().mkpath(pool.filePath(QStringLiteral("b"))));
	const QString first = pool.filePath(QStringLiteral("a"));
	const QString second = pool.filePath(QStringLiteral("b"));
	// One filesystem under two mount points: the bind mount is made in the mount namespace
	// this process entered before it started any thread.
	if (!testsupport::bindMountForTest(first, second))
	{
		QSKIP("this host cannot give one filesystem two mount points (no unprivileged mount namespace)");
	}
	// The bind mount comes down again when the slot ends, so the pool can be removed.
	const auto unbind = qScopeGuard([&second]() { testsupport::unbindMountForTest(second); });
	const QString takeFirst = first + QStringLiteral("/take0");
	const QString takeSecond = second + QStringLiteral("/take1");
	QVERIFY(QDir().mkpath(takeFirst));
	QVERIFY(QDir().mkpath(takeSecond));

	// The case's premises, checked at the take directories themselves: one filesystem (the
	// kernel's st_dev) and one pool of free bytes, under two mount points.
	QCOMPARE(testsupport::filesystemDevice(takeFirst), testsupport::filesystemDevice(takeSecond));
	const QStorageInfo firstStorage(takeFirst);
	const QStorageInfo secondStorage(takeSecond);
	QVERIFY2(firstStorage.rootPath() != secondStorage.rootPath(),
		qPrintable(QStringLiteral("both take directories are under the mount point %1").arg(firstStorage.rootPath())));

	QVERIFY2(run(QStringLiteral("record.arm_track"), {{QStringLiteral("route"), 0},
		{QStringLiteral("file"), takeFirst + QStringLiteral("/route0.wav")}}).ok, "arm 0 refused");
	QVERIFY2(run(QStringLiteral("record.arm_track"), {{QStringLiteral("route"), 1},
		{QStringLiteral("file"), takeSecond + QStringLiteral("/route1.wav")}}).ok, "arm 1 refused");

	const ControlResult left = run(QStringLiteral("record.remaining"));
	QVERIFY2(left.ok, qPrintable(left.errorMessage));
	QCOMPARE(left.result.value(QStringLiteral("armed_routes")).toInt(), 2);
	// The free bytes the answer quotes are the ones of the pool the take directories are on.
	const double free = left.result.value(QStringLiteral("free_bytes")).toDouble();
	const double measured = static_cast<double>(firstStorage.bytesAvailable());
	QVERIFY2(qAbs(free - measured) <= qMax(1048576.0, measured * 0.05),
		qPrintable(QStringLiteral("free_bytes %1, measured %2").arg(free).arg(measured)));
	// One pool of free bytes for two routes: it lasts two routes' writing rate.
	const double rate = static_cast<double>(Engine::audioEngine()->outputSampleRate());
	const double expected = free / (rate * 3.0 * 2.0);
	const double seconds = left.result.value(QStringLiteral("seconds")).toDouble();
	QVERIFY2(qAbs(seconds - expected) <= 1e-9 * qMax(1.0, expected),
		qPrintable(QStringLiteral("seconds %1, expected %2 for two routes on one filesystem under two mount points")
			.arg(seconds).arg(expected)));

	QVERIFY(run(QStringLiteral("record.disarm_track"), {{QStringLiteral("route"), 0}}).ok);
	QVERIFY(run(QStringLiteral("record.disarm_track"), {{QStringLiteral("route"), 1}}).ok);
#else
	QSKIP("one filesystem under two mount points needs a mount namespace, which this platform has not");
#endif
}
void RecordToolsTest::theBudgetKeyIsTheFilesystemIdentity() {
	const QString volumeId = QStringLiteral("volume-one");
	const QString firstMount = QStringLiteral("/mnt/first");
	const QString secondMount = QStringLiteral("/mnt/second");
	const control::VolumeNaming windows = control::VolumeNaming::VolumeId;
	// One volume under two mount points: ONE key, so both routes share the pool of free bytes.
	QCOMPARE(control::filesystemIdentityKey(firstMount, windows, volumeId, firstMount),
		control::filesystemIdentityKey(secondMount, windows, volumeId, secondMount));
	// The mount point is the fallback only: it names no volume, so it names neither of these.
	QVERIFY(control::filesystemIdentityKey(firstMount, windows, QString(), firstMount)
		!= control::filesystemIdentityKey(firstMount, windows, volumeId, firstMount));
	QVERIFY(control::filesystemIdentityKey(firstMount, windows, QString(), firstMount)
		!= control::filesystemIdentityKey(secondMount, windows, QString(), secondMount));
	// POSIX: the device the kernel mounts the filesystem from is the identity - two paths onto
	// one filesystem share it, whatever the device label says.
	QTemporaryDir takes;
	QVERIFY2(takes.isValid(), "could not make a take directory");
	QVERIFY(QDir().mkpath(takes.filePath(QStringLiteral("a"))));
	QVERIFY(QDir().mkpath(takes.filePath(QStringLiteral("b"))));
	const control::VolumeNaming posix = control::VolumeNaming::KernelDevice;
	QCOMPARE(control::filesystemIdentityKey(takes.filePath(QStringLiteral("a")), posix,
		QStringLiteral("tmpfs"), takes.path()),
		control::filesystemIdentityKey(takes.filePath(QStringLiteral("b")), posix,
			QStringLiteral("tmpfs"), takes.path()));
	// The naming this platform uses for itself.
#ifdef _WIN32
	QCOMPARE(control::platformVolumeNaming(), control::VolumeNaming::VolumeId);
#else
	QCOMPARE(control::platformVolumeNaming(), control::VolumeNaming::KernelDevice);
#endif
}
