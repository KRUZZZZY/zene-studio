/*
 * ReferenceProjectPanel.cpp - browse the reference project and copy from it (WF-042)
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

#include "ReferenceProjectPanel.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "CommandForm.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "Song.h"
#include "Track.h"

namespace lmms::gui
{

namespace
{

enum Role
{
	KindRole = Qt::UserRole,  //!< "track", "clip", "device", "channel" (or empty for a heading)
	OwnerRole,                //!< the reference track's or channel's index
	IndexRole,                //!< the clip's or device's index under its owner
	OwnerKindRole             //!< "track" or "channel"
};

QPointer<ReferenceProjectPanel> s_panel;

QTreeWidgetItem* item(const QString& name, const QString& detail, const QString& kind, int owner, int index, const QString& ownerKind)
{
	auto* made = new QTreeWidgetItem(QStringList{name, detail});
	made->setData(0, KindRole, kind);
	made->setData(0, OwnerRole, owner);
	made->setData(0, IndexRole, index);
	made->setData(0, OwnerKindRole, ownerKind);
	return made;
}

void addDevices(QTreeWidgetItem* parent, const QJsonArray& devices, int owner, const QString& ownerKind)
{
	for (const QJsonValue& value : devices)
	{
		const QJsonObject device = value.toObject();
		parent->addChild(item(device.value(QStringLiteral("plugin")).toString(), QObject::tr("device"), QStringLiteral("device"), owner,
			device.value(QStringLiteral("index")).toInt(), ownerKind));
	}
}

QTreeWidgetItem* trackItem(const QJsonObject& track)
{
	const int index = track.value(QStringLiteral("index")).toInt();
	QTreeWidgetItem* made = item(track.value(QStringLiteral("name")).toString(), track.value(QStringLiteral("type")).toString(),
		QStringLiteral("track"), index, index, QStringLiteral("track"));
	for (const QJsonValue& value : track.value(QStringLiteral("clips")).toArray())
	{
		const QJsonObject clip = value.toObject();
		const QString at = QObject::tr("%1 at bar %2").arg(clip.value(QStringLiteral("kind")).toString())
			.arg(clip.value(QStringLiteral("position")).toInt() / TimePos::ticksPerBar() + 1);
		made->addChild(item(clip.value(QStringLiteral("name")).toString(), at, QStringLiteral("clip"), index,
			clip.value(QStringLiteral("index")).toInt(), QStringLiteral("track")));
	}
	addDevices(made, track.value(QStringLiteral("devices")).toArray(), index, QStringLiteral("track"));
	return made;
}

} // namespace

ReferenceProjectPanel::ReferenceProjectPanel(QWidget* parent) :
	QDialog(parent),
	m_path(new QLabel(this)),
	m_tree(new QTreeWidget(this)),
	m_targets(new QComboBox(this)),
	m_atPlayhead(new QCheckBox(tr("At the play position"), this))
{
	setWindowTitle(tr("Reference Project"));
	setAccessibleName(tr("Reference Project"));
	m_tree->setHeaderLabels({tr("Name"), tr("Kind")});
	m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_targets->setAccessibleName(tr("Copy to"));
	auto* openButton = new QPushButton(tr("Open..."), this);
	auto* closeButton = new QPushButton(tr("Close Reference"), this);
	auto* tracks = new QPushButton(tr("Copy Tracks"), this);
	auto* clips = new QPushButton(tr("Copy Clips"), this);
	auto* devices = new QPushButton(tr("Copy Devices"), this);
	tracks->setProperty("controlCommand", QStringLiteral("reference.copy_tracks"));
	clips->setProperty("controlCommand", QStringLiteral("reference.copy_clips"));
	devices->setProperty("controlCommand", QStringLiteral("reference.copy_devices"));
	connect(openButton, &QPushButton::clicked, this, [this] { openWithDialog(this); });
	connect(closeButton, &QPushButton::clicked, this, [this] { run(QStringLiteral("reference.close"), {}); });
	connect(tracks, &QPushButton::clicked, this, &ReferenceProjectPanel::copyTracks);
	connect(clips, &QPushButton::clicked, this, &ReferenceProjectPanel::copyClips);
	connect(devices, &QPushButton::clicked, this, &ReferenceProjectPanel::copyDevices);
	auto* top = new QHBoxLayout;
	top->addWidget(m_path, 1);
	top->addWidget(openButton);
	top->addWidget(closeButton);
	auto* target = new QHBoxLayout;
	target->addWidget(new QLabel(tr("Copy to:"), this));
	target->addWidget(m_targets, 1);
	target->addWidget(m_atPlayhead);
	auto* buttons = new QHBoxLayout;
	buttons->addWidget(tracks);
	buttons->addWidget(clips);
	buttons->addWidget(devices);
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(top);
	layout->addWidget(m_tree, 1);
	layout->addLayout(target);
	layout->addLayout(buttons);
	resize(520, 560);
}

ReferenceProjectPanel* ReferenceProjectPanel::showPanel(QWidget* parent)
{
	if (s_panel.isNull()) { s_panel = new ReferenceProjectPanel(parent); }
	s_panel->refresh();
	s_panel->show();
	s_panel->raise();
	return s_panel;
}

bool ReferenceProjectPanel::openWithDialog(QWidget* parent)
{
	const QString path = QFileDialog::getOpenFileName(parent, tr("Open as Reference"), QString(), tr("Projects (*.mmp *.mmpz)"));
	if (path.isEmpty()) { return false; }
	const ControlResult opened = runCommand(parent, CommandCall{QStringLiteral("reference.open"), {{QStringLiteral("path"), path}}, {},
		tr("Open as Reference"), false});
	if (opened.ok) { showPanel(parent); }
	return opened.ok;
}

void ReferenceProjectPanel::refreshIfOpen()
{
	if (!s_panel.isNull()) { s_panel->refresh(); }
}

void ReferenceProjectPanel::refresh()
{
	const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("reference.get_state"), {});
	if (!state.ok)
	{
		// A refused or failed query is not a closed reference: say what failed and keep the last tree read
		// (as MixerRoutingMatrix does), rather than presenting an empty tree as "no reference is open".
		m_path->setText(tr("The reference project could not be read: %1").arg(state.errorMessage));
		fillTargets();
		return;
	}
	const QJsonObject& listing = state.result;
	const bool open = listing.value(QStringLiteral("open")).toBool();
	m_path->setText(open ? listing.value(QStringLiteral("path")).toString() + (listing.value(QStringLiteral("stale")).toBool()
		? tr(" (changed on disk since it was opened)") : QString()) : tr("No reference project is open."));
	buildTree(listing);
	fillTargets();
}

void ReferenceProjectPanel::buildTree(const QJsonObject& state)
{
	m_tree->clear();
	for (const QJsonValue& value : state.value(QStringLiteral("tracks")).toArray()) { m_tree->addTopLevelItem(trackItem(value.toObject())); }
	const QJsonArray channels = state.value(QStringLiteral("channels")).toArray();
	if (channels.isEmpty()) { return; }
	auto* mixer = new QTreeWidgetItem(QStringList{tr("Mixer"), QString()});
	for (const QJsonValue& value : channels)
	{
		const QJsonObject channel = value.toObject();
		const int index = channel.value(QStringLiteral("index")).toInt();
		QTreeWidgetItem* row = item(channel.value(QStringLiteral("name")).toString(), tr("channel %1").arg(index), QStringLiteral("channel"),
			index, index, QStringLiteral("channel"));
		addDevices(row, channel.value(QStringLiteral("devices")).toArray(), index, QStringLiteral("channel"));
		mixer->addChild(row);
	}
	m_tree->addTopLevelItem(mixer);
}

void ReferenceProjectPanel::fillTargets()
{
	const QString kept = m_targets->currentData().toString();
	m_targets->clear();
	for (Track* track : Engine::getSong()->tracks())
	{
		m_targets->addItem(tr("Track: %1").arg(track->name()), control::trackIdOf(track));
	}
	for (int i = 0; i < static_cast<int>(Engine::mixer()->numChannels()); ++i)
	{
		MixerChannel* channel = Engine::mixer()->mixerChannel(i);
		m_targets->addItem(i == 0 ? tr("Master channel") : tr("Channel %1: %2").arg(i).arg(channel->m_name), control::channelIdOf(channel));
	}
	m_targets->setCurrentIndex(std::max(0, m_targets->findData(kept)));
}

bool ReferenceProjectPanel::selectedOf(const QString& kind, QList<QTreeWidgetItem*>* items, bool oneOwner)
{
	for (QTreeWidgetItem* selected : m_tree->selectedItems())
	{
		if (selected->data(0, KindRole).toString() == kind) { items->append(selected); }
	}
	const auto sameOwner = [items](QTreeWidgetItem* other) {
		return other->data(0, OwnerRole) == items->first()->data(0, OwnerRole)
			&& other->data(0, OwnerKindRole) == items->first()->data(0, OwnerKindRole);
	};
	if (!items->isEmpty() && (!oneOwner || std::all_of(items->begin(), items->end(), sameOwner))) { return true; }
	m_path->setText(items->isEmpty() ? tr("Select the %1s to copy first.").arg(kind) : tr("Select %1s of one owner at a time.").arg(kind));
	return false;
}

void ReferenceProjectPanel::run(const QString& id, const QJsonObject& args)
{
	runCommand(this, CommandCall{id, args, {}, QString(), false});
	refresh();
}

void ReferenceProjectPanel::copyTracks()
{
	QList<QTreeWidgetItem*> items;
	if (!selectedOf(QStringLiteral("track"), &items, false)) { return; }
	QJsonArray indices;
	for (QTreeWidgetItem* selected : items) { indices.append(selected->data(0, IndexRole).toInt()); }
	run(QStringLiteral("reference.copy_tracks"), {{QStringLiteral("tracks"), indices}});
}

void ReferenceProjectPanel::copyClips()
{
	QList<QTreeWidgetItem*> items;
	if (!selectedOf(QStringLiteral("clip"), &items)) { return; }
	QJsonArray indices;
	for (QTreeWidgetItem* selected : items) { indices.append(selected->data(0, IndexRole).toInt()); }
	QJsonObject args{{QStringLiteral("track"), items.first()->data(0, OwnerRole).toInt()}, {QStringLiteral("clips"), indices},
		{QStringLiteral("to"), m_targets->currentData().toString()}};
	if (m_atPlayhead->isChecked()) { args.insert(QStringLiteral("position"), Engine::getSong()->getPlayPos().getTicks()); }
	run(QStringLiteral("reference.copy_clips"), args);
}

void ReferenceProjectPanel::copyDevices()
{
	QList<QTreeWidgetItem*> items;
	if (!selectedOf(QStringLiteral("device"), &items)) { return; }
	QJsonArray indices;
	for (QTreeWidgetItem* selected : items) { indices.append(selected->data(0, IndexRole).toInt()); }
	const QString ownerKind = items.first()->data(0, OwnerKindRole).toString();
	run(QStringLiteral("reference.copy_devices"), {{ownerKind, items.first()->data(0, OwnerRole).toInt()},
		{QStringLiteral("devices"), indices}, {QStringLiteral("to"), m_targets->currentData().toString()}});
}

} // namespace lmms::gui
