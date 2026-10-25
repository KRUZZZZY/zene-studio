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
#include <functional>

#include <QAction>
#include <QCoreApplication>
#include <QEvent>
#include <QJsonArray>
#include <QKeyEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "CommandForm.h"
#include "CommandFormFields.h"
#include "CommandMetadata.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"

namespace lmms::gui
{

namespace
{

//! A menu's title as a person reads it: no mnemonic ampersand, no tab-shortcut.
QString clean(QString text)
{
	// '&&' is how Qt writes a literal ampersand: it must survive as one '&', while a single '&'
	// is the mnemonic marker and goes. A trailing '&' (mnemonic on the next character) goes too.
	QString out;
	out.reserve(text.size());
	for (int i = 0; i < text.size(); ++i)
	{
		if (text.at(i) != QLatin1Char('&')) { out.append(text.at(i)); continue; }
		if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('&')) { out.append(QLatin1Char('&')); ++i; }
	}
	const int tab = out.indexOf(QLatin1Char('\t'));
	if (tab >= 0) { out.truncate(tab); }
	return out.trimmed();
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
		entry.irreversible = commandIsIrreversible(entry.command);  // false for an action that declares none
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

namespace
{

//! The registry entries for which @a wanted holds: no `requires`, not carried by a menu. Each reads by its
//! name under its group (theme 4); a command with any argument to give is a form entry.
QList<PaletteEntry> registryEntries(const QList<PaletteEntry>& menuEntries,
	const std::function<bool(const ControlCommand&)>& wanted)
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
		if (!wanted(*command)) { continue; }
		PaletteEntry entry;
		entry.label = commandName(id);
		entry.path = commandCategory(id);
		entry.command = id;
		entry.form = !command->argsSchema.value(QStringLiteral("properties")).toObject().isEmpty();
		entry.irreversible = commandIsIrreversible(id);
		entry.detail = command->description;
		out.append(entry);
	}
	return out;
}

bool takesRequired(const ControlCommand& command)
{
	return !command.argsSchema.value(QStringLiteral("required")).toArray().isEmpty();
}

} // namespace

QList<PaletteEntry> paletteRegistryEntries(const QList<PaletteEntry>& menuEntries)
{
	return registryEntries(menuEntries, [](const ControlCommand& c) { return !takesRequired(c); });
}

QList<PaletteEntry> paletteFormEntries(const QList<PaletteEntry>& menuEntries)
{
	return registryEntries(menuEntries, takesRequired);
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

namespace
{

QString outcomeOf(const QString& name, const ControlResult& result)
{
	return result.ok ? QObject::tr("%1: done").arg(name) : QObject::tr("%1: %2").arg(name, result.errorMessage);
}

//! @a entry dispatched with @a args, after asking when it cannot be undone (theme 4: confirm the verified
//! non-undoable; Undo covers the rest).
QString runNow(const PaletteEntry& entry, const QJsonObject& args, QWidget* parent)
{
	if (entry.irreversible && !confirmAction(parent, entry.label, QString(), commandUndoWarning(entry.command)))
	{
		return QObject::tr("%1: cancelled").arg(entry.label);
	}
	return outcomeOf(entry.label, ControlRegistry::instance()->invoke(entry.command, args));
}

//! The required arguments of @a id the selection fills, and in @a missing the labels of those it does not.
QJsonObject requiredFromSelection(const QString& id, QStringList* missing)
{
	const QJsonObject selection = selectionContext(id);
	const QJsonObject properties = commandProperties(id);
	QJsonObject args;
	for (const QString& key : commandRequired(id))
	{
		if (selection.contains(key)) { args.insert(key, selection.value(key)); }
		else { missing->append(fieldLabel(key, properties.value(key).toObject())); }
	}
	return args;
}

} // namespace

QString paletteActivate(const PaletteEntry& entry, QWidget* formParent)
{
	if (!entry.action.isNull())
	{
		if (entry.irreversible && !confirmAction(formParent, entry.label, QString(), commandUndoWarning(entry.command)))
		{
			return QObject::tr("%1: cancelled").arg(entry.label);
		}
		entry.action->trigger();
		return QString();
	}
	if (entry.command.isEmpty()) { return QString(); }
	if (!entry.form) { return runNow(entry, {}, formParent); }
	// Configure is the primary action whenever arguments remain to be given (theme 4).
	const CommandCall call{entry.command, {}, selectionContext(entry.command), entry.label, true};
	const ControlResult opened = runCommand(formParent, call);
	if (opened.errorKind == ControlErrorKind::Busy) { return QObject::tr("%1: fill in its form").arg(entry.label); }
	return outcomeOf(entry.label, opened);
}

bool paletteCanRunWithDefaults(const PaletteEntry& entry)
{
	if (!entry.form || !entry.action.isNull() || entry.command.isEmpty()) { return false; }
	QStringList missing;
	requiredFromSelection(entry.command, &missing);
	return missing.isEmpty();
}

QString paletteRunWithDefaults(const PaletteEntry& entry, QWidget* parent)
{
	if (!entry.form || !entry.action.isNull()) { return paletteActivate(entry, parent); }
	QStringList missing;
	const QJsonObject args = requiredFromSelection(entry.command, &missing);
	if (!missing.isEmpty())
	{
		return QObject::tr("%1 needs %2: use Configure").arg(entry.label, missing.join(QStringLiteral(", ")));
	}
	return runNow(entry, args, parent);
}

namespace
{

//! A list row: the name (an ellipsis when it opens a form, a flag when it cannot be undone), where it lives,
//! its shortcut, and the command id - dimmed, there for the script writer, never the headline.
QTreeWidgetItem* itemFor(const PaletteEntry& entry)
{
	QString name = entry.form ? entry.label + QStringLiteral("…") : entry.label;
	if (entry.irreversible)
	{
		const QString warning = commandUndoWarning(entry.command);
		name += warning == QCoreApplication::translate("CommandMetadata", "This cannot be undone.")
			? QObject::tr("  (cannot be undone)") : QObject::tr("  (undo may not fully reverse this)");
	}
	auto* item = new QTreeWidgetItem(QStringList{name, entry.path, entry.shortcut, entry.command});
	item->setForeground(3, tokens::palette().textMuted);
	if (entry.irreversible) { item->setForeground(0, tokens::palette().warning); }
	item->setToolTip(0, entry.detail.isEmpty() ? entry.command : entry.detail);
	return item;
}

} // namespace

CommandPalette::CommandPalette(QMenuBar* bar, QWidget* parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Command Palette"));
	setAccessibleName(tr("Command Palette"));
	setAccessibleDescription(tr("Type to find any menu item or command. Enter runs the highlighted one, or opens its "
		"settings when it has any; Shift+Enter runs it with its defaults."));
	m_entries = paletteMenuEntries(bar);
	const QList<PaletteEntry> menus = m_entries;
	m_entries.append(paletteRegistryEntries(menus));
	m_entries.append(paletteFormEntries(menus));

	m_filter = new QLineEdit(this);
	m_filter->setPlaceholderText(tr("Type a command or a menu item..."));
	m_filter->setAccessibleName(tr("Filter"));
	m_filter->installEventFilter(this);
	m_list = new QTreeWidget(this);
	m_list->installEventFilter(this);
	m_list->setAccessibleName(tr("Matching commands"));
	m_list->setRootIsDecorated(false);
	m_list->setUniformRowHeights(true);
	m_list->setHeaderLabels({tr("Command"), tr("Where"), tr("Shortcut"), tr("ID")});
	m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_list->header()->setStretchLastSection(false);
	m_detail = formfield::captionLabel(QString(), this);
	m_detail->setObjectName(QStringLiteral("paletteDetail"));
	m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
	m_primary = new QPushButton(this);
	m_primary->setAutoDefault(false);
	m_secondary = new QPushButton(tr("Run with Defaults"), this);
	m_secondary->setAutoDefault(false);
	m_secondary->setToolTip(tr("Run it now without opening its settings (Shift+Enter)"));
	m_status = new QLabel(this);
	m_status->setAccessibleName(tr("Last result"));

	auto* actions = new QHBoxLayout;
	actions->addWidget(m_status, 1);
	actions->addWidget(m_secondary);
	actions->addWidget(m_primary);
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(m_filter);
	layout->addWidget(m_list, 1);
	layout->addWidget(m_detail);
	layout->addLayout(actions);
	resize(720, 460);

	connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& text) { setQuery(text); });
	connect(m_list, &QTreeWidget::itemActivated, this,
		[this](QTreeWidgetItem* item) { activateRow(m_list->indexOfTopLevelItem(item)); });
	connect(m_list, &QTreeWidget::currentItemChanged, this, [this] { showDetail(); });
	connect(m_primary, &QPushButton::clicked, this, [this] { activateRow(currentRow()); });
	connect(m_secondary, &QPushButton::clicked, this, [this] { runRowWithDefaults(currentRow()); });
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
	for (const PaletteEntry& entry : m_shown) { m_list->addTopLevelItem(itemFor(entry)); }
	if (m_list->topLevelItemCount() > 0) { m_list->setCurrentItem(m_list->topLevelItem(0)); }
	showDetail();
}

int CommandPalette::currentRow() const
{
	return m_list->indexOfTopLevelItem(m_list->currentItem());
}

void CommandPalette::showDetail()
{
	const int row = currentRow();
	const bool any = row >= 0 && row < m_shown.size();
	const PaletteEntry entry = any ? m_shown.at(row) : PaletteEntry();
	QStringList lines;
	if (!entry.detail.isEmpty()) { lines.append(entry.detail); }
	if (entry.irreversible) { lines.append(commandUndoWarning(entry.command)); }
	if (!entry.command.isEmpty()) { lines.append(tr("Command ID: %1").arg(entry.command)); }
	m_detail->setText(lines.join(QLatin1Char('\n')));
	m_detail->setVisible(!lines.isEmpty());
	// Configure is the primary action whenever arguments remain to be given; Run otherwise (theme 4).
	m_primary->setText(entry.form ? tr("Configure…") : tr("Run"));
	m_primary->setEnabled(any);
	m_secondary->setVisible(any && paletteCanRunWithDefaults(entry));
}

QString CommandPalette::detailText() const
{
	return m_detail->text();
}

bool CommandPalette::activateRow(int row)
{
	return runRow(row, false);
}

bool CommandPalette::runRowWithDefaults(int row)
{
	return runRow(row, true);
}

bool CommandPalette::runRow(int row, bool withDefaults)
{
	if (row < 0 || row >= m_shown.size()) { return false; }
	const PaletteEntry entry = m_shown.at(row);
	// A menu action closes the palette first, so a dialog it opens is not stacked under it;
	// a form closes it too - the form, not the palette, is where the work is. A command run
	// here stays open to report its result.
	const bool leaves = !entry.action.isNull() || (entry.form && !withDefaults);
	if (leaves) { hide(); }
	const QString outcome = withDefaults ? paletteRunWithDefaults(entry, parentWidget()) : paletteActivate(entry, parentWidget());
	m_status->setText(outcome);
	if (leaves) { close(); }
	return true;
}

bool CommandPalette::eventFilter(QObject* watched, QEvent* event)
{
	if ((watched == m_filter || watched == m_list) && event->type() == QEvent::KeyPress)
	{
		auto* key = static_cast<QKeyEvent*>(event);
		const int row = currentRow();
		const int last = m_list->topLevelItemCount() - 1;
		switch (key->key())
		{
		case Qt::Key_Down:
			if (watched == m_filter) { m_list->setCurrentItem(m_list->topLevelItem(std::min(row + 1, last))); return true; }
			break;
		case Qt::Key_Up:
			if (watched == m_filter) { m_list->setCurrentItem(m_list->topLevelItem(std::max(row - 1, 0))); return true; }
			break;
		case Qt::Key_Return:
		case Qt::Key_Enter: runRow(row, key->modifiers().testFlag(Qt::ShiftModifier)); return true;
		default: break;
		}
	}
	return QDialog::eventFilter(watched, event);
}

} // namespace lmms::gui
