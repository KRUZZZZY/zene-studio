/*
 * ShortcutPresets.cpp - the shipped keyboard-map presets (0.5.0 Q050-202)
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "ShortcutPresets.h"

#include <utility>

namespace lmms::gui
{

namespace
{

QJsonObject keys(std::initializer_list<std::pair<const char*, const char*>> entries)
{
	QJsonObject out;
	for (const auto& [command, key] : entries) { out.insert(QLatin1String(command), QLatin1String(key)); }
	return out;
}

} // namespace

QList<ShortcutPreset> shortcutPresets()
{
	return {
		{ QStringLiteral("accessibility"), QStringLiteral("Accessibility"),
			QStringLiteral("A keyboard-first map: the command palette and the everyday record, undo, redo and save/open "
				"actions stay on easy keys. Every menu action this map does not name keeps its own default."),
			keys({{"window.command_palette", "Ctrl+Shift+P"}, {"transport.record", "Ctrl+R"},
				{"control.undo", "Ctrl+Z"}, {"control.redo", "Ctrl+Shift+Z"},
				{"project.save", "Ctrl+S"}, {"project.open", "Ctrl+O"}, {"app.help", "F1"}}) },
		{ QStringLiteral("ableton-live"), QStringLiteral("Ableton Live"),
			QStringLiteral("Zene's corresponding menu actions on Live's familiar keys: F9 records, Ctrl+Z undoes, "
				"Ctrl+S saves and Ctrl+O opens. It maps Zene actions; it does not import Live sets."),
			keys({{"transport.record", "F9"}, {"control.undo", "Ctrl+Z"}, {"control.redo", "Ctrl+Shift+Z"},
				{"project.save", "Ctrl+S"}, {"project.open", "Ctrl+O"}}) },
		{ QStringLiteral("fl-studio"), QStringLiteral("FL Studio"),
			QStringLiteral("Zene's corresponding menu actions on FL Studio's familiar keys: R records, Ctrl+Z undoes, "
				"Ctrl+S saves and Ctrl+O opens. It maps Zene actions; it does not import FL projects."),
			keys({{"transport.record", "R"}, {"control.undo", "Ctrl+Z"}, {"control.redo", "Ctrl+Shift+Z"},
				{"project.save", "Ctrl+S"}, {"project.open", "Ctrl+O"}}) },
		{ QStringLiteral("logic-pro"), QStringLiteral("Logic Pro"),
			QStringLiteral("Zene's corresponding menu actions on Logic's familiar keys: R records, Ctrl+Z undoes, "
				"Ctrl+S saves and Ctrl+O opens. It maps Zene actions; it does not import Logic projects."),
			keys({{"transport.record", "R"}, {"control.undo", "Ctrl+Z"}, {"control.redo", "Ctrl+Shift+Z"},
				{"project.save", "Ctrl+S"}, {"project.open", "Ctrl+O"}}) },
	};
}

const ShortcutPreset* shortcutPreset(const QString& id)
{
	static const QList<ShortcutPreset> presets = shortcutPresets();
	for (const ShortcutPreset& preset : presets)
	{
		if (preset.id == id) { return &preset; }
	}
	return nullptr;
}

} // namespace lmms::gui
