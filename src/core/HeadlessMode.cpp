/*
 * HeadlessMode.cpp - "can a human answer a dialog in this instance?"
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

#include "HeadlessMode.h"
#include "UnattendedRun.h"

#include <QCoreApplication>
#include <QGuiApplication>

namespace lmms
{

namespace
{

//! The platform name Qt was asked for, or is actually using. Either source is
//! enough on its own: QT_QPA_PLATFORM is set before the application object
//! exists (the control socket is created that early), and platformName() is
//! authoritative once it does.
QString platformName()
{
	const QByteArray requested = qgetenv("QT_QPA_PLATFORM");
	if (auto* guiApp = qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
	{
		const QString actual = guiApp->platformName();
		if (!actual.isEmpty())
		{
			return requested.isEmpty() ? actual : actual + QLatin1Char(' ') + QString::fromLocal8Bit(requested);
		}
	}
	return QString::fromLocal8Bit(requested);
}

//! The platform plugins with no display behind them.
bool isHeadlessName(const QString& name)
{
	return name.contains(QLatin1String("offscreen")) || name.contains(QLatin1String("minimal")) ||
		name.contains(QLatin1String("vnc"));
}

} // namespace

bool isHeadlessRun()
{
	// DEPRECATED ALIAS (merge 2026-09-12): isUnattendedRun() is the single predicate -
	// it is true for an agent instance (--control-socket) OR a displayless platform,
	// which is a superset of this function's old meaning. Delete this file once no
	// caller remains.
	return isUnattendedRun();
}

QString headlessPlatformName()
{
	const QString name = platformName();
	return isHeadlessName(name) ? name : QString();
}

} // namespace lmms
