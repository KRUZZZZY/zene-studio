/*
 * PluginEditorHost.h - R4.1: the run loop a plugin editor asks its host for
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

#ifndef LMMS_PLUGIN_EDITOR_HOST_H
#define LMMS_PLUGIN_EDITOR_HOST_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>

#include <QObject>

#include "lmms_export.h"

class QSocketNotifier;
class QTimer;

namespace lmms
{

/*! What a plugin's EDITOR needs from the host on Linux: its file descriptors watched and its
 *  timers run, on the host's main thread (relief plan R4.1). A VST3 editor asks through
 *  Steinberg::Linux::IRunLoop (registerEventHandler / registerTimer), a CLAP plugin through
 *  the posix-fd-support and timer-support host extensions; both adapters sit on this one
 *  implementation, so the two formats cannot disagree about how a descriptor or a timer
 *  behaves. Built on the Qt event loop (QSocketNotifier, QTimer) - no second loop, no
 *  thread. Main thread only, like every GUI call a plugin makes.
 *
 *  The rules both formats specify: a descriptor is registered once (a second registration
 *  of the same one is refused, not merged), its flags can be modified in place, and a
 *  callback may unregister its own descriptor or timer - the entry is removed after the
 *  callback returns, never under it.
 */
class LMMS_EXPORT PluginEditorHost : public QObject
{
public:
	enum FdFlag : unsigned
	{
		Read = 1u << 0,
		Write = 1u << 1,
		Error = 1u << 2
	};
	using FdCallback = std::function<void(int fd, unsigned flags)>;
	using TimerCallback = std::function<void(std::uint32_t id)>;

	explicit PluginEditorHost(QObject* parent = nullptr);
	~PluginEditorHost() override;

	//! Watch @a fd for @a flags (FdFlag bits). False for a descriptor already registered,
	//! a negative one, or no flags.
	bool registerFd(int fd, unsigned flags, FdCallback callback);
	//! Change the flags a registered descriptor is watched for. False when it is not.
	bool modifyFd(int fd, unsigned flags);
	bool unregisterFd(int fd);

	//! Run @a callback every @a periodMs (at least 1). The id goes to @a id; ids are never
	//! reused while this host lives.
	bool registerTimer(std::uint32_t periodMs, TimerCallback callback, std::uint32_t* id);
	bool unregisterTimer(std::uint32_t id);

	int fdCount() const { return static_cast<int>(m_fds.size()); }
	int timerCount() const { return static_cast<int>(m_timers.size()); }

private:
	struct FdEntry;
	struct TimerEntry;
	void applyFlags(int fd, FdEntry& entry);
	void fire(int fd, unsigned flag);

	std::map<int, std::unique_ptr<FdEntry>> m_fds;
	std::map<std::uint32_t, std::unique_ptr<TimerEntry>> m_timers;
	std::uint32_t m_nextTimerId = 1;
};

} // namespace lmms

#endif // LMMS_PLUGIN_EDITOR_HOST_H
