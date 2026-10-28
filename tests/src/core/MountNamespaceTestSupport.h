/*
 * MountNamespaceTestSupport.h - what a test needs to reach one filesystem under
 *                                two mount points.
 *
 * ONE copy, in the repo's own convention for a shared test helper
 * (ChordTestSupport.h, GrooveTestSupport.h, ReversibilityTestSupport.h): a
 * budget of free disk space belongs to a FILESYSTEM, and a filesystem can be
 * reached under more than one name - a bind mount, a filesystem mounted twice.
 * A test that has to tell those apart needs such a pair, and making one takes a
 * mount namespace of the test's own.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef LMMS_TESTS_MOUNT_NAMESPACE_TEST_SUPPORT_H
#define LMMS_TESTS_MOUNT_NAMESPACE_TEST_SUPPORT_H

#include <QString>
#include <sys/stat.h>

#ifdef __linux__
#include <fcntl.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace lmms
{
namespace testsupport
{

//! The device the kernel mounts the filesystem of @a path from (st_dev), 0 when it cannot be read.
inline quint64 filesystemDevice(const QString& path)
{
	struct stat st;
	if (::stat(path.toUtf8().constData(), &st) != 0) { return 0; }
	return static_cast<quint64>(st.st_dev);
}

#ifdef __linux__

//! The child's half of the namespace handshake: wait until the parent has a namespace of its own,
//! then map the parent's own uid into it. The kernel only takes the map from a process that still
//! holds the old identity, so a child of the unsharing process writes it. The child touches no Qt
//! state, so it leaves with _exit.
inline bool mapOwnUidIntoNamespace(pid_t parentPid, int readEnd, int writeEnd)
{
	char go = 0;
	if (::read(readEnd, &go, 1) != 1) { return false; }
	char path[64];
	if (::snprintf(path, sizeof(path), "/proc/%ld/uid_map", static_cast<long>(parentPid)) <= 0) { return false; }
	const int fd = ::open(path, O_WRONLY | O_TRUNC);
	if (fd < 0) { return false; }
	char line[64];
	const long uid = static_cast<long>(::geteuid());
	const int len = ::snprintf(line, sizeof(line), "%ld %ld 1\n", uid, uid);
	bool written = len > 0 && ::write(fd, line, static_cast<size_t>(len)) == len;
	::close(fd);
	char done = written ? 1 : 0;
	if (::write(writeEnd, &done, 1) != 1) { written = false; } // the parent sees a failed handshake either way
	return written;
}

//! Puts this process in a mount namespace of its own, with its own uid mapped in it, so a
//! directory can be bound onto another: one filesystem under two mount points. False when the
//! sandbox forbids the namespace. The kernel refuses a user namespace for a process that has
//! threads already, so this has to run before the test starts any.
inline bool enterOwnMountNamespace()
{
	int toChild[2];
	int toParent[2];
	if (::pipe(toChild) != 0) { return false; }
	if (::pipe(toParent) != 0) { ::close(toChild[0]); ::close(toChild[1]); return false; }
	const pid_t parent = ::getpid();
	const pid_t child = ::fork();
	if (child < 0)
	{
		::close(toChild[0]); ::close(toChild[1]); ::close(toParent[0]); ::close(toParent[1]);
		return false;
	}
	if (child == 0)
	{
		::close(toChild[1]);
		::close(toParent[0]);
		const bool mapped = mapOwnUidIntoNamespace(parent, toChild[0], toParent[1]);
		::_exit(mapped ? 0 : 1);
	}
	::close(toChild[0]);
	::close(toParent[1]);
	const bool unshared = ::unshare(CLONE_NEWUSER | CLONE_NEWNS) == 0;
	char go = 1;
	const bool signalled = ::write(toChild[1], &go, 1) == 1;
	char done = 0;
	const bool answered = ::read(toParent[0], &done, 1) == 1;
	::close(toChild[1]);
	::close(toParent[0]);
	int status = 0;
	::waitpid(child, &status, 0);
	return unshared && signalled && answered && done == 1;
}

//! Binds @a source onto @a target in this process's mount namespace: one filesystem under two
//! mount points, which is what a bind mount of a directory is in the world.
inline bool bindMountForTest(const QString& source, const QString& target)
{
	// The namespace's mounts are copies: the bind mount cannot reach the host.
	::mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr);
	return ::mount(source.toUtf8().constData(), target.toUtf8().constData(), nullptr, MS_BIND, nullptr) == 0;
}

//! Takes the test's bind mount down again, so the directory under it can be removed.
inline void unbindMountForTest(const QString& target)
{
	::umount2(target.toUtf8().constData(), MNT_DETACH);
}

#else

//! No mount namespaces here: the case that needs one filesystem under two mount points skips.
inline bool enterOwnMountNamespace() { return false; }

#endif

} // namespace testsupport
} // namespace lmms

#endif // LMMS_TESTS_MOUNT_NAMESPACE_TEST_SUPPORT_H
