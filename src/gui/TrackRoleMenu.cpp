/*
 * TrackRoleMenu.cpp - the Actions-menu items that switch a row between neutral and instrument (Q050-283.2)
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

#include "TrackRoleMenu.h"

#include <algorithm>

#include <QCoreApplication>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "NeutralTrack.h"
#include "Plugin.h"
#include "PluginFactory.h"
#include "Track.h"
#include "UnattendedRun.h"

namespace lmms::gui
{

namespace
{

const QString SetRole = QStringLiteral("track.set_role");

void runSetRole(const QJsonObject& args)
{
	const ControlResult result = ControlRegistry::instance()->invoke(SetRole, args);
	if (result.ok) { return; }
	if (isUnattendedRun())
	{
		qWarning("track role menu: %s refused: %s", qPrintable(SetRole), qPrintable(result.errorMessage));
		return;
	}
	QMessageBox::warning(nullptr, QObject::tr("Track role"), result.errorMessage);
}

//! One item running track.set_role with @a args. Queued on the application object: the switch deletes
//! the row's view, and with it this menu and this action, so it must not run inside their signal.
QAction* addRoleItem(QMenu* menu, const QString& label, const QString& objectName, const QJsonObject& args)
{
	QAction* action = menu->addAction(label);
	action->setObjectName(objectName);
	action->setData(SetRole);
	action->setProperty("controlCommand", SetRole);
	QObject::connect(action, &QAction::triggered, QCoreApplication::instance(),
		[args] { runSetRole(args); }, Qt::QueuedConnection);
	return action;
}

//! The instrument plug-ins that load by name alone (no sub-plug-in key), by display name.
Plugin::DescriptorList directInstruments()
{
	Plugin::DescriptorList instruments;
	for (Plugin::Descriptor* descriptor : getPluginFactory()->descriptors(Plugin::Type::Instrument))
	{
		if (descriptor != nullptr && descriptor->subPluginFeatures == nullptr) { instruments.append(descriptor); }
	}
	std::sort(instruments.begin(), instruments.end(), [](const Plugin::Descriptor* a, const Plugin::Descriptor* b) {
		return QString::fromUtf8(a->displayName).compare(QString::fromUtf8(b->displayName), Qt::CaseInsensitive) < 0;
	});
	return instruments;
}

void addAssignInstrumentMenu(QMenu* menu, const NeutralTrack* neutral, const QString& trackId)
{
	QMenu* assign = menu->addMenu(QObject::tr("Assign instrument"));
	assign->setObjectName(QStringLiteral("assignInstrumentMenu"));
	if (!neutral->keptInstrumentRole().isEmpty())
	{
		addRoleItem(assign, QObject::tr("Restore %1").arg(neutral->keptInstrumentPlugin()),
			QStringLiteral("restoreInstrumentRoleAction"),
			{{QStringLiteral("track"), trackId}, {QStringLiteral("role"), QStringLiteral("instrument")}});
		// A kept role comes back as it was; a different plug-in is plugin.load's job afterwards.
		return;
	}
	for (const Plugin::Descriptor* descriptor : directInstruments())
	{
		const QString plugin = QString::fromUtf8(descriptor->name);
		addRoleItem(assign, QString::fromUtf8(descriptor->displayName), QStringLiteral("assignInstrument_") + plugin,
			{{QStringLiteral("track"), trackId}, {QStringLiteral("role"), QStringLiteral("instrument")},
				{QStringLiteral("instrument"), plugin}});
	}
}

} // namespace

void addTrackRoleMenu(QMenu* menu, Track* track)
{
	if (menu == nullptr || track == nullptr) { return; }
	const QString trackId = control::trackIdOf(track);
	if (const auto* neutral = dynamic_cast<const NeutralTrack*>(track))
	{
		addAssignInstrumentMenu(menu, neutral, trackId);
	}
	else if (track->type() == Track::Type::Instrument)
	{
		addRoleItem(menu, QObject::tr("Make neutral (keep instrument)"), QStringLiteral("makeNeutralAction"),
			{{QStringLiteral("track"), trackId}, {QStringLiteral("role"), QStringLiteral("neutral")}});
	}
}

} // namespace lmms::gui
