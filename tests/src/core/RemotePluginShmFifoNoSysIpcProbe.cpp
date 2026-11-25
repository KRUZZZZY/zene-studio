/*
 * RemotePluginShmFifoNoSysIpcProbe.cpp - RemotePluginBase.h on a host without <sys/ipc.h>
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

// Windows has neither <sys/ipc.h> nor <semaphore.h>, so RemotePluginBase.h
// selects the shared-memory FIFO itself there, while the test targets also pass
// SYNC_WITH_SHM_FIFO (value 1) on the command line; mingw -Werror reported the
// header's second definition as "SYNC_WITH_SHM_FIFO redefined" (cluster
// ci-v041-5). tests/CMakeLists.txt compiles this file against a lmmsconfig.h
// without LMMS_HAVE_SYS_IPC_H, so it takes that header branch on any host.
//
// Deliberately no <unistd.h> here (cluster ci-4e020460347493c3): the header's
// shm-FIFO branch calls getpid() and usleep(), so it must include what declares
// them itself. This TU used to add <unistd.h> under #ifdef __unix__, which hid
// the gap on Linux but not on macOS - Apple clang defines __APPLE__/__MACH__,
// never __unix__, so the macOS CI job saw the header's own error:
// "use of undeclared identifier 'getpid'" (macos-arm64, run 43076107122).
#include "RemotePluginBase.h"

#include <type_traits>

#ifdef LMMS_HAVE_UNISTD_H
// The probe's contract, stated rather than left to the header's inline calls:
// on a host with <unistd.h>, RemotePluginBase.h alone declares getpid() and
// usleep(), with __unix__ undefined as on Apple clang (target
// RemotePluginShmFifoNoSysIpcAppleMacroProbe, ctest
// RemotePluginShmFifoAppleMacroProbeCompile).
static_assert(std::is_pointer_v<decltype(&::getpid)>, "RemotePluginBase.h must declare getpid()");
static_assert(std::is_pointer_v<decltype(&::usleep)>, "RemotePluginBase.h must declare usleep()");
#endif

int remotePluginShmFifoValueWithoutSysIpc()
{
	return SYNC_WITH_SHM_FIFO + 0;
}
