/*
 * CommandPalette.cpp - M3.4: every menu action and every argument-free command, searchable
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

#include "CommandPalette.h"

#include <algorithm>

#include <QAction>
#include <QEvent>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QSet>
#include <QVBoxLayout>

#include "ControlRegistry.h"

namespace lmms::gui
{

namespace
{

//! A menu's title as a person reads it: no mnemonic ampersand, no tab-shortcut.
QString clean(QString text)
{
	text.remove(QLatin1Char('&'));
	const int tab = text.indexOf(QLatin1Char('\t'));
	if (tab >= 0) { text.truncate(tab); }
	return text.trimmed();
}

void walk(QMenu* menu, const QString& path, QList<PaletteEntry>& out, QSet<QMenu*>& seen)
{
	if (menu == nullptr || seen.contains(menu)) { return; }
	seen.insert(menu);
	// A menu that builds its items on show (View rebuilds from updateViewMenu)
	// is asked to, so the palette lists what the menu would show now.
	QMetaObject::invokeMethod(menu, "aboutToShow");
	for (QAction* action : menu->actions())
	{
		if (action->isSeparator() || !action->isVisible()) { continue; }
		const QString label = clean(action->text());
		if (QMenu* sub = action->menu())
		{
			walk(sub, path.isEmpty() ? label : path + QStringLiteral(" ▸ ") + label, out, seen);
			continue;
		}
		if (label.isEmpty() || !action->isEnabled()) { continue; }
		PaletteEntry entry;
		entry.label = label;
		entry.path = path;
		entry.shortcut = action->shortcut().toString(QKeySequence::NativeText);
		entry.command = action->property("controlCommand").toString();
		entry.action = action;
		out.append(entry);
	}
}

//! How well @a query matches @a text: -1 none; higher is better.
int score(const QString& text, const QString& query)
{
	if (query.isEmpty()) { return 0; }
	const QString haystack = text.toLower();
	const QString needle = query.toLower();
	if (haystack.startsWith(needle)) { return 400; }
	const int at = haystack.indexOf(needle);
	if (at > 0 && !haystack.at(at - 1).isLetterOrNumber()) { return 300; }
	if (at > 0) { return 200; }
	int from = 0;
	for (const QChar c : needle)
	{
		from = haystack.indexOf(c, from);
		if (from < 0) { return -1; }
		++from;
	}
	return 100;
}

} // namespace

QList<PaletteEntry> paletteMenuEntries(QMenuBar* bar)
{
	QList<PaletteEntry> out;
	if (bar == nullptr) { return out; }
	QSet<QMenu*> seen;
	for (QAction* top : bar->actions())
	{
		if (top->menu() != nullptr) { walk(top->menu(), clean(top->text()), out, seen); }
	}
	return out;
}

QList<PaletteEntry> paletteRegistryEntries(const QList<PaletteEntry>& menuEntries)
{
	QSet<QString> covered;
	for (const PaletteEntry& entry : menuEntries)
	{
		if (!entry.command.isEmpty()) { covered.insert(entry.command); }
	}
	QList<PaletteEntry> out;
	const ControlRegistry* registry = ControlRegistry::instance();
	for (const QString& id : registry->commandIds())
	{
		const ControlCommand* command = registry->command(id);
		if (command == nullptr || covered.contains(id) || !command->requiresDecl.isEmpty()) { continue; }
		if (!command->argsSchema.value(QStringLiteral("required")).toArray().isEmpty()) { continue; }
		PaletteEntry entry;
		entry.label = id;
		entry.path = QStringLiteral("Command");
		entry.command = id;
		const int stop = command->description.indexOf(QStringLiteral(". "));
		entry.detail = stop > 0 ? command->description.left(stop + 1) : command->description;
		out.append(entry);
	}
	return out;
}

QList<PaletteEntry> paletteMatches(const QList<PaletteEntry>& entries, const QString& query)
{
	const QString trimmed = query.trimmed();
	if (trimmed.isEmpty()) { return entries; }
	QList<QPair<int, int>> ranked; // (score, index), stable for equal scores
	for (int i = 0; i < entries.size(); ++i)
	{
		const PaletteEntry& entry = entries.at(i);
		int best = score(entry.label, trimmed);
		if (best >= 0) { best += 1000; } // the label beats the path
		else { best = score(entry.path + QLatin1Char(' ') + entry.label, trimmed); }
		if (best < 0 && !entry.command.isEmpty()) { best = score(entry.command, trimmed); }
		if (best >= 0) { ranked.append({best, i}); }
	}
	std::stable_sort(ranked.begin(), ranked.end(),
		[](const QPair<int, int>& a, const QPair<int, int>& b) { return a.first > b.first; });
	QList<PaletteEntry> out;
	for (const auto& [unused, index] : ranked) { Q_UNUSED(unused) out.append(entries.at(index)); }
	return out;
}

QString paletteActivate(const PaletteEntry& entry)
{
	if (!entry.action.isNull())
	{
		entry.action->trigger();
		return QString();
	}
	if (entry.command.isEmpty()) { return QString(); }
	const ControlResult result = ControlRegistry::instance()->invoke(entry.command);
	return result.ok ? QObject::tr("%1: done").arg(entry.command)
		: QObject::tr("%1: %2").arg(entry.command, result.errorMessage);
}




CommandPalette::CommandPalette(QMenuBar* bar, QWidget* parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Command Palette"));
	setAccessibleName(tr("Command Palette"));
	setAccessibleDescription(tr("Type to find any menu item or command; Enter runs the highlighted one."));
	m_entries = paletteMenuEntries(bar);
	m_entries.append(paletteRegistryEntries(m_entries));

	m_filter = new QLineEdit(this);
	m_filter->setPlaceholderText(tr("Type a command or a menu item..."));
	m_filter->setAccessibleName(tr("Filter"));
	m_filter->installEventFilter(this);
	m_list = new QListWidget(this);
	m_list->setAccessibleName(tr("Matching commands"));
	m_status = new QLabel(this);
	m_status->setAccessibleName(tr("Last result"));

	auto* layout = new QVBoxLayout(this);
	layout->addWidget(m_filter);
	layout->addWidget(m_list, 1);
	layout->addWidget(m_status);
	resize(560, 420);

	connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& text) { setQuery(text); });
	connect(m_list, &QListWidget::itemActivated, this,
		[this](QListWidgetItem* item) { activateRow(m_list->row(item)); });
	refill();
}

void CommandPalette::setQuery(const QString& query)
{
	if (m_filter->text() != query) { m_filter->setText(query); }
	m_shown = paletteMatches(m_entries, query);
	refill();
}

void CommandPalette::refill()
{
	if (m_shown.isEmpty() && m_filter->text().trimmed().isEmpty()) { m_shown = m_entries; }
	m_list->clear();
	for (const PaletteEntry& entry : m_shown)
	{
		QString text = entry.path.isEmpty() ? entry.label : entry.path + QStringLiteral(" ▸ ") + entry.label;
		if (!entry.shortcut.isEmpty()) { text += QStringLiteral("    ") + entry.shortcut; }
		auto* item = new QListWidgetItem(text, m_list);
		item->setToolTip(entry.detail.isEmpty() ? entry.command : entry.detail);
	}
	if (m_list->count() > 0) { m_list->setCurrentRow(0); }
}

bool CommandPalette::activateRow(int row)
{
	if (row < 0 || row >= m_shown.size()) { return false; }
	const PaletteEntry entry = m_shown.at(row);
	// A menu action closes the palette first, so a dialog it opens is not
	// stacked under it; a command stays open to report its result.
	if (!entry.action.isNull()) { hide(); }
	const QString outcome = paletteActivate(entry);
	m_status->setText(outcome);
	if (!entry.action.isNull()) { close(); }
	return true;
}

bool CommandPalette::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == m_filter && event->type() == QEvent::KeyPress)
	{
		auto* key = static_cast<QKeyEvent*>(event);
		const int row = m_list->currentRow();
		switch (key->key())
		{
		case Qt::Key_Down: m_list->setCurrentRow(std::min(row + 1, m_list->count() - 1)); return true;
		case Qt::Key_Up: m_list->setCurrentRow(std::max(row - 1, 0)); return true;
		case Qt::Key_Return:
		case Qt::Key_Enter: activateRow(row); return true;
		default: break;
		}
	}
	return QDialog::eventFilter(watched, event);
}

} // namespace lmms::gui
