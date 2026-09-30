/*
 * BrowserTagMenu.h - the file browser's Tags menu: a file's tags, added and removed
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

#ifndef LMMS_GUI_BROWSER_TAG_MENU_H
#define LMMS_GUI_BROWSER_TAG_MENU_H

#include <functional>
#include <optional>

#include <QString>

#include "lmms_export.h"

class QMenu;

namespace lmms::gui
{

//! Asks for a new tag's text; nothing when cancelled. Replaceable for tests.
using BrowserTagPrompt = std::function<std::optional<QString>()>;
LMMS_EXPORT void setBrowserTagPrompt(BrowserTagPrompt prompt);

/*! KNOWN-LIMITATIONS "browser tag/metadata search ... has no interface": a "Tags" submenu for a
 *  file's context menu. It lists every tag the library knows (browser.tags), the file's own ones
 *  checked - unchecking removes (browser.tag.remove), checking adds (browser.tag.add) - and "New
 *  tag..." adds one by name. @a path is the file's absolute path, the store's key. */
LMMS_EXPORT void addBrowserTagMenu(QMenu* menu, const QString& path);

} // namespace lmms::gui

#endif // LMMS_GUI_BROWSER_TAG_MENU_H
