/*
 * RenderRangeMenu.cpp - Navigate > Range: Render Selection and Export Sections
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

#include "RenderRangeMenu.h"

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>

#include "ControlRegistry.h"
#include "UnattendedRun.h"

namespace lmms::gui
{

namespace
{

std::function<QString(const QString& title)> s_filePicker;
std::function<QString(const QString& title)> s_dirPicker;
std::function<ControlResult(const QString& command, const QJsonObject& args)> s_invoker;

//! The registered command behind this module: the injected invoker when a test set one, the
//! registry otherwise, so the GUI and the socket reach ONE implementation.
ControlResult invoke(const QString& command, const QJsonObject& args = QJsonObject{})
{
	if (s_invoker) { return s_invoker(command, args); }
	return ControlRegistry::instance()->invoke(command, args);
}

//! Shows a refusal (or logs it when nobody is there to answer a dialog).
void report(const QString& title, const ControlResult& result)
{
	if (result.ok) { return; }
	if (isUnattendedRun()) { qWarning("%s refused: %s", qPrintable(title), qPrintable(result.errorMessage)); }
	else { QMessageBox::warning(QApplication::activeWindow(), title, result.errorMessage); }
}

void say(const QString& title, const QString& text)
{
	if (isUnattendedRun()) { qWarning("%s: %s", qPrintable(title), qPrintable(text)); }
	else { QMessageBox::information(QApplication::activeWindow(), title, text); }
}

QString pickFile(const QString& title)
{
	if (s_filePicker) { return s_filePicker(title); }
	return QFileDialog::getSaveFileName(QApplication::activeWindow(), title, QString(),
		QMenu::tr("WAV file (*.wav)"));
}

QString pickDirectory(const QString& title)
{
	if (s_dirPicker) { return s_dirPicker(title); }
	return QFileDialog::getExistingDirectory(QApplication::activeWindow(), title);
}

/*! Render Selection: the active arranger range goes to render.render with its EXACT ticks, so
 *  the file is the span the user selected. No range is a message, not a refusal of a command the
 *  user did not issue.
 */
void renderSelection()
{
	const QString title = QMenu::tr("Render Selection");
	const ControlResult range = invoke(QStringLiteral("range.get"));
	if (!range.ok)
	{
		report(title, range);
		return;
	}
	const QJsonObject state = range.result;
	if (!state.value(QStringLiteral("active")).toBool())
	{
		say(title, QMenu::tr("Select a range in the arranger first (Navigate > Range or drag on the "
			"timeline ruler)."));
		return;
	}
	const QString out = pickFile(title);
	if (out.isEmpty()) { return; }
	QApplication::setOverrideCursor(Qt::WaitCursor);
	const ControlResult rendered = invoke(QStringLiteral("render.render"),
		{{QStringLiteral("out"), out},
			{QStringLiteral("start_ticks"), state.value(QStringLiteral("start")).toInt()},
			{QStringLiteral("end_ticks"), state.value(QStringLiteral("end")).toInt()}});
	QApplication::restoreOverrideCursor();
	report(title, rendered);
}

/*! Export Sections: every named section to its own file through render.sections, in a picked
 *  directory. A successful run says how many files were written.
 */
void exportSections()
{
	const QString title = QMenu::tr("Export Sections");
	const QString directory = pickDirectory(title);
	if (directory.isEmpty()) { return; }
	QApplication::setOverrideCursor(Qt::WaitCursor);
	const ControlResult rendered = invoke(QStringLiteral("render.sections"),
		{{QStringLiteral("out"), directory}});
	QApplication::restoreOverrideCursor();
	report(title, rendered);
	if (rendered.ok)
	{
		say(title, QMenu::tr("Wrote %n section file(s) to %1.", nullptr,
			rendered.result.value(QStringLiteral("count")).toInt()).arg(directory));
	}
}

QAction* addRangeRenderAction(QMenu* menu, const QString& label, const QString& command,
	const QString& tooltip, void (*handler)())
{
	QAction* action = menu->addAction(label);
	action->setProperty("controlCommand", command);
	action->setToolTip(tooltip);
	QObject::connect(action, &QAction::triggered, menu, [handler] { handler(); });
	return action;
}

} // namespace

QMenu* addRenderRangeActions(QMenu* rangeMenu)
{
	addRangeRenderAction(rangeMenu, QMenu::tr("Render Selection..."), QStringLiteral("render.render"),
		QMenu::tr("Render the selected range to a file with the export settings in force"), &renderSelection);
	addRangeRenderAction(rangeMenu, QMenu::tr("Export Sections..."), QStringLiteral("render.sections"),
		QMenu::tr("Write one file per named section (section.add), each bounded to its own ticks"),
		&exportSections);
	return rangeMenu;
}

void setRenderRangeOutputPicker(std::function<QString(const QString& title)> picker) { s_filePicker = std::move(picker); }
void setRenderSectionsOutputPicker(std::function<QString(const QString& title)> picker) { s_dirPicker = std::move(picker); }
void setRenderRangeInvoker(
	std::function<ControlResult(const QString& command, const QJsonObject& args)> invoker)
{
	s_invoker = std::move(invoker);
}

} // namespace lmms::gui
