/*
 * MenuTestSupport.h - reading and triggering a built menu in a widget test
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

#ifndef LMMS_TESTS_MENU_TEST_SUPPORT_H
#define LMMS_TESTS_MENU_TEST_SUPPORT_H

#include <QAction>
#include <QList>
#include <QMenu>
#include <QStringList>

namespace lmms::test
{

//! The action at @a path under @a menu - every element but the last names a submenu by its
//! text, the last the action - or nullptr. Text is compared without mnemonic ampersands.
inline QAction* menuAction(QMenu* menu, const QStringList& path)
{
	for (int depth = 0; menu != nullptr && depth < path.size(); ++depth)
	{
		QMenu* next = nullptr;
		for (QAction* action : menu->actions())
		{
			if (action->text().remove(QLatin1Char('&')) != path.at(depth)) { continue; }
			if (depth == path.size() - 1) { return action; }
			next = action->menu();
			break;
		}
		menu = next;
	}
	return nullptr;
}

//! Every leaf action under @a menu (separators and submenu entries left out).
inline QList<QAction*> leafActions(QMenu* menu)
{
	QList<QAction*> out;
	for (QAction* action : menu->actions())
	{
		if (action->isSeparator()) { continue; }
		if (action->menu() != nullptr) { out.append(leafActions(action->menu())); }
		else { out.append(action); }
	}
	return out;
}

} // namespace lmms::test

#endif // LMMS_TESTS_MENU_TEST_SUPPORT_H
