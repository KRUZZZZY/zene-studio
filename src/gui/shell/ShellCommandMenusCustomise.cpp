/*
 * ShellCommandMenusCustomise.cpp - the Customise submenu of the project menu
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

#include <QJsonObject>
#include <QMenu>
#include <QString>

#include "CommandForm.h"
#include "CommandLists.h"
#include "LayoutMenu.h"
#include "ShellCommandMenus.h"
#include "ShellLayout.h"
#include "ShellMenuParts.h"
#include "WorkspaceMenus.h"

namespace lmms::gui::ShellCommandMenus
{

using namespace menuparts;

void populateCustomise(QMenu* menu)
{
	list(menu, "Keyboard Shortcuts...", [] { return CommandLists::shortcuts(); });
	list(menu, "Workflows...", [] { return CommandLists::macros(); });
	list(menu, "MIDI Mappings...", [] { return CommandLists::controllerMappings(); });
	list(menu, "Mapping Templates...", [] { return CommandLists::mappingTemplates(); });
	LayoutMenu::populate(menu->addMenu(tr("Window Layouts")));
	QMenu* surface = menu->addMenu(tr("Control Surface (Mackie Control)"));
	add(surface, "Connect...", "surface.mcu_connect", {}, true);
	add(surface, "Ports", "surface.mcu_ports", {}, false, {}, true);
	add(surface, "State", "surface.mcu_state", {}, false, {}, true);
	add(surface, "Disconnect", "surface.mcu_disconnect", {}, false);
	// Q050-081: Lua control-surface scripts on MIDI ports (the bindings themselves stay in MIDI Mappings above).
	QMenu* scripts = menu->addMenu(tr("Controller Scripts"));
	add(scripts, "Start Script...", "controller.script_start", {}, true, {}, true);
	add(scripts, "Script State", "controller.script_state", {}, false, {}, true);
	add(scripts, "Stop Script...", "controller.script_stop", {}, true);
	// Q050-205: the opt-in OSC server - off until Start runs osc.start; the form shows where it listens.
	QMenu* osc = menu->addMenu(tr("OSC Control Server"));
	add(osc, "Start...", "osc.start", {}, true, {{QStringLiteral("address"), QStringLiteral("127.0.0.1")},
		{QStringLiteral("port"), 9000}, {QStringLiteral("scope"), QStringLiteral("remote")}}, true);
	add(osc, "State", "osc.get_state", {}, false, {}, true);
	add(osc, "Stop", "osc.stop", {}, false, {}, true);
	QMenu* pop = menu->addMenu(tr("Pop Out Panel"));
	for (const ShellPanelSpec& spec : ShellLayout::panels())
	{
		const QJsonObject panel{{QStringLiteral("panel"), spec.id}};
		addCommandAction(pop, spec.title, QStringLiteral("window.detach"),
			[panel] { return CommandCall{QStringLiteral("window.detach"), panel, {}, QString(), false}; });
	}
	populateCustomiseExtras(menu);  // 0.5.0 W3: theme, density, settings (WorkspaceMenus.cpp)
}

} // namespace lmms::gui::ShellCommandMenus
