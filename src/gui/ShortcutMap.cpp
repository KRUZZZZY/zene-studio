/*
 * ShortcutMap.cpp - rebindable keyboard shortcuts over the live menus
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

#include "ShortcutMap.h"

#include <QAction>
#include <QKeySequence>
#include <QVariant>

#include "CommandPalette.h"
#include "UiConfigStore.h"

namespace lmms::gui::ShortcutMap
{

const char* const StoreKey = "shortcuts";

namespace
{

constexpr const char* DefaultProperty = "zeneDefaultShortcut";

//! The action's own shortcut, remembered the first time the map sees it.
QString defaultOf(QAction* action)
{
	const QVariant known = action->property(DefaultProperty);
	if (known.isValid()) { return known.toString(); }
	const QString keys = action->shortcut().toString(QKeySequence::PortableText);
	action->setProperty(DefaultProperty, keys);
	return keys;
}

QString idOf(const PaletteEntry& entry)
{
	QString label = entry.label;
	label.remove(QLatin1Char('&'));
	return entry.path.isEmpty() ? label : entry.path + QStringLiteral(" ▸ ") + label;
}

//! @a entry's action gets its override from @a chosen, or its default back.
void applyTo(const PaletteEntry& entry, const QJsonObject& chosen)
{
	const QString id = idOf(entry);
	const QString keys = chosen.contains(id) ? chosen.value(id).toString() : defaultOf(entry.action);
	entry.action->setShortcut(QKeySequence::fromString(keys, QKeySequence::PortableText));
}

} // namespace

QJsonObject overrides()
{
	return uistore::read(QLatin1String(StoreKey)).value(QStringLiteral("overrides")).toObject();
}

QList<Binding> bindings(QMenuBar* bar)
{
	const QJsonObject chosen = overrides();
	QList<Binding> out;
	for (const PaletteEntry& entry : paletteMenuEntries(bar))
	{
		if (entry.action.isNull()) { continue; }
		Binding binding;
		binding.id = idOf(entry);
		binding.command = entry.command;
		binding.action = entry.action;
		binding.defaults = defaultOf(entry.action);
		binding.overridden = chosen.contains(binding.id);
		// The walk asked each menu to fill itself, and one that builds its items on show (New from template) made
		// them anew without their keys: give them back before reading (a listed binding is the live one).
		applyTo(entry, chosen);
		binding.keys = entry.action->shortcut().toString(QKeySequence::PortableText);
		out.append(binding);
	}
	return out;
}

void apply(QMenuBar* bar)
{
	const QJsonObject chosen = overrides();
	for (const PaletteEntry& entry : paletteMenuEntries(bar))
	{
		if (!entry.action.isNull()) { applyTo(entry, chosen); }
	}
}

QString portable(const QString& keys, bool* ok)
{
	const QString trimmed = keys.trimmed();
	const QKeySequence sequence = QKeySequence::fromString(trimmed, QKeySequence::PortableText);
	const bool parsed = trimmed.isEmpty() || !sequence.isEmpty();
	if (ok != nullptr) { *ok = parsed; }
	return parsed ? sequence.toString(QKeySequence::PortableText) : QString();
}

QStringList conflicts(const QList<Binding>& all, const QString& keys, const QString& except)
{
	QStringList out;
	if (keys.isEmpty()) { return out; }
	for (const Binding& binding : all)
	{
		if (binding.id != except && binding.keys == keys) { out.append(binding.id); }
	}
	return out;
}

} // namespace lmms::gui::ShortcutMap
