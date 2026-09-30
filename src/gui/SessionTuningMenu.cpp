/*
 * SessionTuningMenu.cpp - Edit > Session Tuning: load a Scala scale or keymap, reset, publish over MTS-ESP
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

#include "SessionTuningMenu.h"

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QSignalBlocker>

#include "ControlRegistry.h"
#include "Keymap.h"
#include "UnattendedRun.h"

namespace lmms::gui
{

namespace
{

std::function<QString(const QString&)> s_picker;

QString pick(const QString& filter)
{
	if (s_picker) { return s_picker(filter); }
	return QFileDialog::getOpenFileName(QApplication::activeWindow(), QMenu::tr("Session Tuning"), QString(), filter);
}

void run(const QString& command, const QJsonObject& args = QJsonObject{})
{
	const ControlResult result = ControlRegistry::instance()->invoke(command, args);
	if (result.ok) { return; }
	if (isUnattendedRun()) { qWarning("%s refused: %s", qPrintable(command), qPrintable(result.errorMessage)); }
	else { QMessageBox::warning(QApplication::activeWindow(), QMenu::tr("Session Tuning"), result.errorMessage); }
}

struct TuningItems
{
	QAction* line;
	QAction* master;
};

void showState(const TuningItems& items)
{
	const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("mts.get_state"), QJsonObject{});
	items.line->setText(state.ok ? sessionTuningStatusText(state.result) : state.errorMessage);
	const QSignalBlocker block(items.master);
	items.master->setChecked(state.result.value(QStringLiteral("mts_master")).toBool());
	const QString library = state.result.value(QStringLiteral("mts_library")).toString();
	items.master->setEnabled(state.ok && !library.isEmpty() && library != QStringLiteral("absent"));
}

void addLoad(QMenu* menu, const QString& text, const char* command, const QString& filter, const TuningItems& items)
{
	QAction* load = menu->addAction(text);
	load->setData(QString::fromLatin1(command));
	QObject::connect(load, &QAction::triggered, menu, [command, filter, items] {
		const QString path = pick(filter);
		if (path.isEmpty()) { return; }
		run(QString::fromLatin1(command), {{QStringLiteral("path"), path}});
		showState(items);
	});
}

} // namespace

void setSessionTuningPicker(std::function<QString(const QString&)> picker) { s_picker = std::move(picker); }

QString sessionTuningStatusText(const QJsonObject& state)
{
	if (!state.value(QStringLiteral("active")).toBool()) { return QMenu::tr("12-TET (no session table)"); }
	// mts.get_state reports the Scala files' own description lines, not their paths.
	const QString scale = state.value(QStringLiteral("scale")).toString();
	const QString keymap = state.value(QStringLiteral("keymap")).toString();
	QString text = scale.isEmpty() ? QMenu::tr("Session table active (%1)").arg(state.value(QStringLiteral("source")).toString())
		: QMenu::tr("Scale: %1").arg(scale);
	// A default Keymap (the identity mapping) describes itself as "empty" (translated): not worth a word.
	if (!keymap.isEmpty() && keymap != Keymap().getDescription()) { text += QMenu::tr(", keymap: %1").arg(keymap); }
	if (state.value(QStringLiteral("mts_master")).toBool()) { text += QMenu::tr(", published over MTS-ESP"); }
	return text;
}

QMenu* addSessionTuningMenu(QMenu* menu)
{
	QMenu* tuning = menu->addMenu(QMenu::tr("Session Tuning"));
	QAction* line = tuning->addAction(QString());
	line->setEnabled(false);
	line->setData(QStringLiteral("mts.get_state"));
	tuning->addSeparator();
	QAction* master = new QAction(QMenu::tr("Publish as MTS-ESP Master"), tuning);
	const TuningItems items{line, master};
	addLoad(tuning, QMenu::tr("Load Scala Scale..."), "mts.load_scale", QMenu::tr("Scala scale (*.scl)"), items);
	addLoad(tuning, QMenu::tr("Load Keymap..."), "mts.load_keymap", QMenu::tr("Scala keymap (*.kbm)"), items);
	QAction* reset = tuning->addAction(QMenu::tr("Reset to 12-TET"));
	reset->setData(QStringLiteral("mts.reset"));
	QObject::connect(reset, &QAction::triggered, tuning, [items] { run(QStringLiteral("mts.reset")); showState(items); });
	tuning->addSeparator();
	master->setCheckable(true);
	master->setData(QStringLiteral("mts.master_set"));
	tuning->addAction(master);
	QObject::connect(master, &QAction::triggered, tuning, [items](bool on) {
		run(QStringLiteral("mts.master_set"), {{QStringLiteral("enabled"), on}});
		showState(items);
	});
	QObject::connect(tuning, &QMenu::aboutToShow, tuning, [items] { showState(items); });
	showState(items);
	return tuning;
}

} // namespace lmms::gui
