/*
 * UnattendedRun.cpp - "can a human answer a dialog in this process?"
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
 */

#include "UnattendedRun.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QString>

namespace lmms
{

namespace
{

bool isHeadlessPlatformName(const QString& name)
{
	return name == QLatin1String("offscreen") || name == QLatin1String("minimal") ||
		name == QLatin1String("vnc");
}

//! True when Qt is on a platform with no display a human could click on.
//! Both sources are consulted: QT_QPA_PLATFORM is set before an application
//! object exists (main() asks that early) and platformName() is authoritative
//! once it does.
bool platformHasNoDisplay()
{
	const QByteArray requested = qgetenv("QT_QPA_PLATFORM");
	if (requested.contains("offscreen") || requested.contains("minimal") ||
		requested.contains("vnc"))
	{
		return true;
	}
	if (auto* guiApp = qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
	{
		return isHeadlessPlatformName(guiApp->platformName());
	}
	return false;
}

bool s_agentInstance = false;

} // namespace

bool isAgentInstance()
{
	return s_agentInstance;
}

void setAgentInstance(bool agent)
{
	s_agentInstance = agent;
}

bool isUnattendedRun()
{
	return s_agentInstance || platformHasNoDisplay();
}

} // namespace lmms
