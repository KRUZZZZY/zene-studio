/*
 * ControlRegistryShutdown.cpp - process-lifetime shutdown hooks for the command registry
 *
 * Copyright (c) 2026 Zene Studio contributors
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 */

#include "ControlRegistry.h"

#include <utility>

namespace lmms
{

namespace
{
/*! \brief The process's shutdown hooks, and the serial that identifies one
 *  (CODE-8).
 *
 * NOT a member of the registry instance, and that is the whole point. The
 * registry is a singleton that can be destroyed and re-created - main() takes
 * `ControlRegistry::instance()` and ControlSession.cpp's last-resort guard calls
 * `ControlRegistry::instance()->runShutdownHooks()` on the way out - so if the
 * instance that existed at registration time is gone, instance() builds a NEW,
 * EMPTY one and a hook held in a member would die with the first instance. The
 * guard would then run nothing at all and leave the control socket behind on
 * exactly the route the hook exists for.
 *
 * A function-local static gives the right lifetime: one store per process,
 * constructed on first use and destroyed at exit - after every path that can
 * call runShutdownHooks(), and never before a hook that is still registered.
 */
struct ShutdownHookEntry
{
	ControlRegistry::ShutdownHookId id = 0;
	std::function<void()> hook;
};

QVector<ShutdownHookEntry>& shutdownHooks()
{
	static QVector<ShutdownHookEntry> hooks;
	return hooks;
}

//! Serial source for ShutdownHookId. 0 is reserved for "no hook", so the first
//! registered hook is 1 and a default-initialised id can never match one.
ControlRegistry::ShutdownHookId nextShutdownHookId()
{
	static ControlRegistry::ShutdownHookId next = 0;
	return ++next;
}

} // namespace

ControlRegistry::ShutdownHookId ControlRegistry::addShutdownHook(std::function<void()> hook)
{
	const ShutdownHookId id = nextShutdownHookId();
	shutdownHooks().append(ShutdownHookEntry{id, std::move(hook)});
	return id;
}


bool ControlRegistry::removeShutdownHook(ShutdownHookId id)
{
	if (id == 0)
	{
		return false;
	}
	QVector<ShutdownHookEntry>& hooks = shutdownHooks();
	for (int i = 0; i < hooks.size(); ++i)
	{
		if (hooks.at(i).id == id)
		{
			hooks.remove(i);
			return true;
		}
	}
	return false;
}


int ControlRegistry::shutdownHookCount() const
{
	return shutdownHooks().size();
}


void ControlRegistry::runShutdownHooks()
{
	// Take the list and clear it BEFORE running anything: a hook that
	// re-registers (a re-listen) must not extend the run it is inside, and a
	// hook that quits or aborts must not leave the store half-consumed for the
	// next caller on the way out.
	const QVector<ShutdownHookEntry> hooks = shutdownHooks();
	shutdownHooks().clear();
	for (const ShutdownHookEntry& entry : hooks)
	{
		if (entry.hook) { entry.hook(); }
	}
}


} // namespace lmms
