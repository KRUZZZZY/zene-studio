/*
 * PluginInstantiationFailure.cpp - why a plug-in module's entry point produced no plug-in
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

#include "PluginInstantiationFailure.h"

#include <utility>

namespace lmms::pluginload
{

namespace
{

//! Per thread: a plug-in is instantiated, and its reason taken, on the thread that asked for it.
auto noted() -> QString&
{
	static thread_local QString reason;
	return reason;
}

} // namespace

void noteInstantiationFailure(const QString& reason)
{
	noted() = reason;
}

auto takeInstantiationFailure() -> QString
{
	return std::exchange(noted(), QString{});
}

} // namespace lmms::pluginload
