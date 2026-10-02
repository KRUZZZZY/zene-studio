/*
 * ControlGuiSupport.cpp - the interface commands' shared scaffolding
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

#include "ControlWindowCommands.h"

#include "ControlRegistry.h"
#include "GuiApplication.h"
#include "MainWindow.h"

namespace lmms
{

ControlCommand guiCommand(const char* group, const char* verb)
{
	ControlCommand cmd;
	cmd.group = QLatin1String(group);
	cmd.verb = QLatin1String(verb);
	cmd.id = cmd.group + QLatin1Char('.') + cmd.verb;
	cmd.mutating = false;
	cmd.requiresEngine = false;
	return cmd;
}

ControlResult noInterface(const QString& id)
{
	return ControlResult::failure(ControlErrorKind::Requires,
		QStringLiteral("command '%1' requires an interface: this instance has no main window").arg(id));
}

gui::MainWindow* controlMainWindow()
{
	gui::GuiApplication* g = gui::getGUI();
	return g != nullptr ? g->mainWindow() : nullptr;
}

} // namespace lmms
