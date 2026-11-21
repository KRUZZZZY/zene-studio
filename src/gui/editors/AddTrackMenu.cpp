/*
 * AddTrackMenu.cpp - the arranger's menu for adding tracks with a destination (Q050-212)
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

#include "AddTrackMenu.h"

#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QObject>
#include <QSpinBox>
#include <QToolButton>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "PluginFactory.h"
#include "embed.h"

namespace lmms::gui
{

namespace
{

const char* const DefaultInstrument = "tripleoscillator";

QString tr(const char* text) { return QObject::tr(text); }

//! The plug-in a new instrument track opens with, as the file lists do; none in a build without it.
void addDefaultInstrument(QJsonObject* args, const QString& type)
{
	if (type == QLatin1String("instrument") && !getPluginFactory()->pluginInfo(DefaultInstrument).isNull())
	{
		args->insert(QStringLiteral("instrument"), QString::fromLatin1(DefaultInstrument));
	}
}

void dispatch(const QString& command, const QJsonObject& args, QWidget* parent)
{
	const ControlResult result = ControlRegistry::instance()->invoke(command, args);
	if (!result.ok) { QMessageBox::warning(parent, tr("Add track"), result.errorMessage); }
}

void addTrackOfType(const QString& type, const QString& output, QWidget* parent)
{
	QJsonObject args{{QStringLiteral("type"), type}};
	if (!output.isEmpty()) { args.insert(QStringLiteral("output"), output); }
	addDefaultInstrument(&args, type);
	dispatch(QStringLiteral("track.add"), args, parent);
}

//! One entry that declares the command it dispatches, as the song editor's folder action does.
template<typename Run>
void addEntry(QMenu* menu, const QString& text, const QString& command, Run run)
{
	QAction* action = menu->addAction(text);
	action->setData(command);
	QObject::connect(action, &QAction::triggered, menu, run);
}

void populateDestinations(QMenu* menu, const QString& type)
{
	addEntry(menu, tr("Default destination"), QStringLiteral("track.add"), [menu, type] { addTrackOfType(type, QString(), menu); });
	menu->addSeparator();
	Mixer* mixer = Engine::mixer();
	for (int i = 0; i < static_cast<int>(mixer->numChannels()); ++i)
	{
		const MixerChannel* channel = mixer->mixerChannel(i);
		if (channel->isBus()) { continue; }  // a parallel bus takes no track input
		const QString id = control::channelIdOf(channel);
		addEntry(menu, channel->m_name, QStringLiteral("track.add"), [menu, type, id] { addTrackOfType(type, id, menu); });
	}
}

void askAndAddToNewBus(QWidget* parent)
{
	QDialog dialog(parent);
	dialog.setObjectName(QStringLiteral("addTracksToNewBus"));
	dialog.setWindowTitle(tr("Add tracks to new bus"));
	auto* form = new QFormLayout(&dialog);
	auto* count = new QSpinBox(&dialog);
	count->setObjectName(QStringLiteral("count"));
	count->setRange(1, 64);
	count->setValue(2);
	form->addRow(tr("Number of tracks"), count);
	auto* type = new QComboBox(&dialog);
	type->setObjectName(QStringLiteral("type"));
	type->addItem(tr("Instrument"), QStringLiteral("instrument"));
	type->addItem(tr("Sample"), QStringLiteral("sample"));
	form->addRow(tr("Track type"), type);
	auto* name = new QLineEdit(&dialog);
	name->setObjectName(QStringLiteral("busName"));
	name->setMaxLength(64);
	name->setPlaceholderText(tr("Named automatically"));
	form->addRow(tr("Bus name"), name);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	form->addRow(buttons);
	if (dialog.exec() != QDialog::Accepted) { return; }

	const QString typeName = type->currentData().toString();
	QJsonObject args{{QStringLiteral("count"), count->value()}, {QStringLiteral("type"), typeName}};
	if (!name->text().trimmed().isEmpty()) { args.insert(QStringLiteral("bus_name"), name->text()); }
	addDefaultInstrument(&args, typeName);
	dispatch(QStringLiteral("track.add_to_new_bus"), args, parent);
}

} // namespace

void populateAddTrackMenu(QMenu* menu)
{
	menu->clear();
	populateDestinations(menu->addMenu(tr("Instrument track to")), QStringLiteral("instrument"));
	populateDestinations(menu->addMenu(tr("Sample track to")), QStringLiteral("sample"));
	menu->addSeparator();
	addEntry(menu, tr("Add tracks to new bus..."), QStringLiteral("track.add_to_new_bus"), [menu] { askAndAddToNewBus(menu); });
}

QToolButton* makeAddTrackButton(QWidget* parent)
{
	auto* button = new QToolButton(parent);
	button->setObjectName(QStringLiteral("addTrackButton"));
	button->setIcon(embed::getIconPixmap("add_sample_track"));
	button->setAccessibleName(tr("Add track to"));
	button->setToolTip(tr("Add a track and choose where it plays, or several tracks on a new bus"));
	button->setPopupMode(QToolButton::InstantPopup);
	auto* menu = new QMenu(button);
	QObject::connect(menu, &QMenu::aboutToShow, menu, [menu] { populateAddTrackMenu(menu); });
	button->setMenu(menu);
	return button;
}

} // namespace lmms::gui
