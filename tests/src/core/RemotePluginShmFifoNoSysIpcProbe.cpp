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
#ifdef __unix__
#include <unistd.h> // getpid()/usleep(), which <process.h> and mingw provide on Windows
#endif

#include "RemotePluginBase.h"

int remotePluginShmFifoValueWithoutSysIpc()
{
	return SYNC_WITH_SHM_FIFO + 0;
}
