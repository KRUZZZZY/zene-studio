/*
 * OfflineRenderBarrier.cpp - lets an export wait for out-of-thread device
 *                            renderers BETWEEN periods (Q050-481.2).
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

#include "OfflineRenderBarrier.h"

#include <algorithm>
#include <mutex>
#include <vector>

namespace lmms::offlinerender
{

namespace
{

//! The participants, and the lock that keeps one alive while the exporter waits on it. Never taken by a
//! render period: only by the exporter between periods and by the control thread adding or removing one.
std::mutex& registryMutex()
{
	static std::mutex mutex;
	return mutex;
}

std::vector<Participant*>& registry()
{
	static std::vector<Participant*> participants;
	return participants;
}

} // namespace

void add(Participant* participant)
{
	if (participant == nullptr) { return; }
	const std::lock_guard lock{registryMutex()};
	registry().push_back(participant);
}

void remove(Participant* participant)
{
	const std::lock_guard lock{registryMutex()};
	auto& participants = registry();
	participants.erase(std::remove(participants.begin(), participants.end(), participant), participants.end());
}

void waitForAll(int limitMs)
{
	const std::lock_guard lock{registryMutex()};
	for (Participant* participant : registry()) { participant->waitUntilCaughtUp(limitMs); }
}

} // namespace lmms::offlinerender
