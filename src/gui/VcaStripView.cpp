/*
 * VcaStripView.cpp - M3.7: a VCA group's own strip in the mixer, and the channel's VCA menu
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

#include "VcaStripView.h"

#include <QAction>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QVBoxLayout>

#include "AutomatableButton.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Fader.h"
#include "Mixer.h"
#include "VcaGroup.h"

namespace lmms::gui
{

VcaStripView::VcaStripView(VcaGroup* group, QWidget* parent) :
	QWidget(parent),
	m_groupId(group->id())
{
	setObjectName(QStringLiteral("vcaStrip"));
	setAccessibleName(tr("VCA group %1").arg(group->name()));
	setAccessibleDescription(tr("Scales every member channel's level together; the members' own "
		"faders do not move."));
	setToolTip(tr("VCA %1 - %n member channel(s)", nullptr, static_cast<int>(group->members().size()))
		.arg(group->name()));

	m_name = new QLabel(group->name(), this);
	m_name->setAlignment(Qt::AlignHCenter);
	m_name->setWordWrap(true);
	m_name->setFixedWidth(64);
	m_name->setAccessibleName(tr("VCA name"));

	auto* members = new QLabel(tr("%n ch", nullptr, static_cast<int>(group->members().size())), this);
	members->setAlignment(Qt::AlignHCenter);
	members->setAccessibleName(tr("Member count"));

	m_mute = new AutomatableButton(this, tr("Mute"));
	m_mute->setObjectName(QStringLiteral("btn-mute"));
	m_mute->setToolTip(tr("Mute every member of this VCA group"));
	m_mute->setCheckable(true);
	m_mute->setModel(group->muteModel());

	m_solo = new AutomatableButton(this, tr("Solo"));
	m_solo->setObjectName(QStringLiteral("btn-solo"));
	m_solo->setToolTip(tr("Solo this VCA group's members"));
	m_solo->setCheckable(true);
	m_solo->setModel(group->soloModel());

	m_fader = new Fader(group->vcaModel(), tr("VCA %1").arg(group->name()), this);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(2);
	layout->addWidget(m_name, 0, Qt::AlignHCenter);
	layout->addWidget(members, 0, Qt::AlignHCenter);
	layout->addWidget(m_solo, 0, Qt::AlignHCenter);
	layout->addWidget(m_mute, 0, Qt::AlignHCenter);
	layout->addWidget(m_fader, 1, Qt::AlignHCenter);

	// A removed group (vca.remove, an undo) frees these models before the mixer's next sync
	// rebuilds the strips - MixerView polls every 500 ms - and a repaint in between read the
	// freed fader model: a SIGSEGV in Fader::calculateKnobPosYFromModel, ~1 run in 12 of
	// control-vca-commands.py (BUGS_FOUND 11.12). The strip retires the moment the fader goes.
	connect(group->vcaModel(), &QObject::destroyed, this, [this] {
		hide();
		deleteLater();
	});
}

QString VcaStripView::caption() const
{
	return m_name->text();
}




void populateVcaMenu(QMenu* menu, mix_ch_t channelIndex, std::function<void()> changed)
{
	Mixer* mixer = Engine::mixer();
	if (mixer == nullptr || channelIndex >= static_cast<mix_ch_t>(mixer->numChannels())) { return; }
	MixerChannel* channel = mixer->mixerChannel(channelIndex);
	const QString channelId = QStringLiteral("ch-%1").arg(channel->id());
	const VcaGroup* current = mixer->vcaGroupForChannel(channelIndex);

	auto run = [changed](const QString& id, const QJsonObject& args) {
		const ControlResult result = ControlRegistry::instance()->invoke(id, args);
		if (changed) { changed(); }
		return result;
	};

	for (VcaGroup* group : mixer->vcaGroups())
	{
		QAction* entry = menu->addAction(group->name());
		entry->setCheckable(true);
		const bool member = group == current;
		entry->setChecked(member);
		const QString groupId = QStringLiteral("vca-%1").arg(group->id());
		QObject::connect(entry, &QAction::triggered, menu, [run, groupId, channelId, member]() {
			run(member ? QStringLiteral("vca.unassign") : QStringLiteral("vca.assign"),
				QJsonObject{{QStringLiteral("group"), groupId}, {QStringLiteral("channel"), channelId}});
		});
	}
	if (!mixer->vcaGroups().empty()) { menu->addSeparator(); }
	const QString name = channel->m_name;
	QAction* create = menu->addAction(QObject::tr("New VCA group"));
	QObject::connect(create, &QAction::triggered, menu, [run, name, channelId]() {
		const ControlResult made = ControlRegistry::instance()->invoke(QStringLiteral("vca.create"),
			QJsonObject{{QStringLiteral("name"), QObject::tr("VCA %1").arg(name)}});
		if (!made.ok) { return; }
		run(QStringLiteral("vca.assign"), QJsonObject{{QStringLiteral("group"),
			made.result.value(QStringLiteral("group"))}, {QStringLiteral("channel"), channelId}});
	});
}

} // namespace lmms::gui
