/*
 * RenderPresetMenu.cpp - File > Render Presets: save, apply and delete a render preset, render with it
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

#include "RenderPresetMenu.h"

#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>

#include "ControlRegistry.h"
#include "UnattendedRun.h"

namespace lmms::gui
{

namespace
{

std::function<std::optional<RenderPresetRequest>()> s_form;
std::function<bool(const QString&)> s_ask;
std::function<QString()> s_picker;

std::optional<RenderPresetRequest> askForPreset()
{
	if (s_form) { return s_form(); }
	QDialog dialog(QApplication::activeWindow());
	dialog.setWindowTitle(QMenu::tr("Save Render Preset"));
	auto* layout = new QFormLayout(&dialog);
	auto* name = new QLineEdit(&dialog);
	auto* rate = new QComboBox(&dialog);
	for (const int hz : {44100, 48000, 88200, 96000, 192000}) { rate->addItem(QString::number(hz), hz); }
	auto* depth = new QComboBox(&dialog);
	depth->addItem(QMenu::tr("16-bit"), QStringLiteral("16"));
	depth->addItem(QMenu::tr("24-bit"), QStringLiteral("24"));
	depth->addItem(QMenu::tr("32-bit float"), QStringLiteral("32"));
	auto* mode = new QComboBox(&dialog);
	mode->addItem(QMenu::tr("Joint stereo"), QStringLiteral("jointstereo"));
	mode->addItem(QMenu::tr("Stereo"), QStringLiteral("stereo"));
	mode->addItem(QMenu::tr("Mono"), QStringLiteral("mono"));
	layout->addRow(QMenu::tr("Name:"), name);
	layout->addRow(QMenu::tr("Sample rate:"), rate);
	layout->addRow(QMenu::tr("Bit depth:"), depth);
	layout->addRow(QMenu::tr("Stereo mode (MP3 only):"), mode);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addRow(buttons);
	if (dialog.exec() != QDialog::Accepted || name->text().trimmed().isEmpty()) { return std::nullopt; }
	return RenderPresetRequest{name->text().trimmed(), rate->currentData().toInt(),
		depth->currentData().toString(), mode->currentData().toString()};
}

bool ask(const QString& question)
{
	if (s_ask) { return s_ask(question); }
	return QMessageBox::question(QApplication::activeWindow(), QMenu::tr("Render Presets"), question,
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

QString pickOutput()
{
	if (s_picker) { return s_picker(); }
	return QFileDialog::getSaveFileName(QApplication::activeWindow(), QMenu::tr("Render Song with Preset"),
		QString(), QMenu::tr("WAV file (*.wav)"));
}

//! Shows a refusal of @a command (or logs it when nobody is there).
void report(const QString& command, const ControlResult& result)
{
	if (result.ok) { return; }
	if (isUnattendedRun()) { qWarning("%s refused: %s", qPrintable(command), qPrintable(result.errorMessage)); }
	else { QMessageBox::warning(QApplication::activeWindow(), QMenu::tr("Render Presets"), result.errorMessage); }
}

ControlResult run(const QString& command, const QJsonObject& args = QJsonObject{})
{
	const ControlResult result = ControlRegistry::instance()->invoke(command, args);
	report(command, result);
	return result;
}

void savePreset()
{
	const auto request = askForPreset();
	if (!request || request->name.isEmpty()) { return; }
	const bool exists = ControlRegistry::instance()->invoke(QStringLiteral("export.preset_list"),
		{{QStringLiteral("name"), request->name}}).ok;
	if (exists && !ask(QMenu::tr("A render preset named \"%1\" exists. Replace it?").arg(request->name))) { return; }
	run(QStringLiteral("export.preset_add"), {{QStringLiteral("name"), request->name},
		{QStringLiteral("sample_rate"), request->sampleRate}, {QStringLiteral("bit_depth"), request->bitDepth},
		{QStringLiteral("stereo_mode"), request->stereoMode}, {QStringLiteral("overwrite"), exists}});
}

void renderWithPreset()
{
	const QString out = pickOutput();
	if (out.isEmpty()) { return; }
	QApplication::setOverrideCursor(Qt::WaitCursor);
	const ControlResult rendered = ControlRegistry::instance()->invoke(QStringLiteral("render.render"),
		{{QStringLiteral("out"), out}});
	QApplication::restoreOverrideCursor();
	report(QStringLiteral("render.render"), rendered);
}

void rebuild(QMenu* menu)
{
	for (QAction* action : menu->actions())
	{
		menu->removeAction(action);
		if (action->menu() != nullptr && action->menu()->parent() == menu) { action->menu()->deleteLater(); }
		else if (action->parent() == menu) { action->deleteLater(); }
	}
	for (QObject* child : menu->children())
	{
		if (qobject_cast<QActionGroup*>(child) != nullptr) { child->deleteLater(); }
	}
	const QJsonObject listed = ControlRegistry::instance()->invoke(QStringLiteral("export.preset_list"), QJsonObject{}).result;
	const QString applied = listed.value(QStringLiteral("applied_preset")).toString();
	QStringList names;
	for (const QJsonValue& preset : listed.value(QStringLiteral("presets")).toArray())
	{
		names << preset.toObject().value(QStringLiteral("name")).toString();
	}

	auto* group = new QActionGroup(menu);
	QAction* defaults = menu->addAction(QMenu::tr("Default Settings"));
	defaults->setCheckable(true);
	defaults->setChecked(applied.isEmpty());
	defaults->setData(QStringLiteral("export.preset_apply"));
	defaults->setToolTip(QMenu::tr("44100 Hz, 16-bit, joint stereo"));
	group->addAction(defaults);
	QObject::connect(defaults, &QAction::triggered, menu, [] { run(QStringLiteral("export.preset_apply")); });
	for (const QString& name : names)
	{
		QAction* apply = menu->addAction(name);
		apply->setCheckable(true);
		apply->setChecked(name == applied);
		apply->setData(QStringLiteral("export.preset_apply"));
		group->addAction(apply);
		QObject::connect(apply, &QAction::triggered, menu, [name] {
			run(QStringLiteral("export.preset_apply"), {{QStringLiteral("name"), name}});
		});
	}
	menu->addSeparator();
	QAction* save = menu->addAction(QMenu::tr("Save Preset..."));
	save->setData(QStringLiteral("export.preset_add"));
	QObject::connect(save, &QAction::triggered, menu, [] { savePreset(); });
	if (!names.isEmpty())
	{
		QMenu* remove = new QMenu(QMenu::tr("Delete"), menu);
		menu->addMenu(remove);
		for (const QString& name : names)
		{
			QAction* item = remove->addAction(name);
			item->setData(QStringLiteral("export.preset_remove"));
			QObject::connect(item, &QAction::triggered, menu, [name] {
				if (!ask(QMenu::tr("Delete the render preset \"%1\"?").arg(name))) { return; }
				run(QStringLiteral("export.preset_remove"), {{QStringLiteral("name"), name}});
			});
		}
	}
	menu->addSeparator();
	QAction* render = menu->addAction(QMenu::tr("Render Song with Preset..."));
	render->setData(QStringLiteral("render.render"));
	render->setToolTip(QMenu::tr("Render the whole song to a WAV file with the checked settings"));
	QObject::connect(render, &QAction::triggered, menu, [] { renderWithPreset(); });
}

} // namespace

void setRenderPresetForm(std::function<std::optional<RenderPresetRequest>()> form) { s_form = std::move(form); }
void setRenderPresetQuestion(std::function<bool(const QString&)> askFn) { s_ask = std::move(askFn); }
void setRenderPresetOutputPicker(std::function<QString()> picker) { s_picker = std::move(picker); }

QMenu* addRenderPresetMenu(QMenu* menu)
{
	QMenu* presets = menu->addMenu(QMenu::tr("Render Presets"));
	QObject::connect(presets, &QMenu::aboutToShow, presets, [presets] { rebuild(presets); });
	rebuild(presets);
	return presets;
}

} // namespace lmms::gui
