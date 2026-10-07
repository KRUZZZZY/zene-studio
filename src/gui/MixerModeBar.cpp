/*
 * MixerModeBar.cpp - the bar across the mixer that shows and sets its view, density, filter and modes
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
#include "MixerModeBar.h"

#include <algorithm>

#include <QButtonGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>

#include "ChannelPickDialog.h"
#include "CommandForm.h"
#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerHardwareMenu.h"
#include "MixerListen.h"
#include "MixerOverlays.h"
#include "MixerRoutingMatrix.h"
#include "MixerStrip.h"
#include "MixerViewOptions.h"

namespace lmms::gui
{

namespace
{

const QString kViewCommand = QStringLiteral("window.mixer_view");

//! A checkable entry running @a id with @a args, checked when it is the state in force.
void commandChoice(QMenu* menu, const QString& label, const QString& id, const QJsonObject& args, bool checked)
{
	QAction* action = addCommandAction(menu, label, id, [id, args] { return CommandCall{id, args, {}, QString(), false}; });
	action->setCheckable(true);
	action->setChecked(checked);
}

//! Name @a chip's state; @a active (a mode that changes what is heard) fills it with the accent as well.
void setChip(QToolButton* chip, const QString& text, bool active)
{
	if (chip->property("active").toBool() != active)
	{
		chip->setProperty("active", active);
		const tokens::Palette& pal = tokens::palette();
		chip->setStyleSheet(active ? QStringLiteral("QToolButton { background-color: %1; color: %2; }")
			.arg(pal.accent.name(), pal.textOnAccent.name()) : QString());
	}
	if (chip->text() == text) { return; }  // each set is announced
	chip->setText(text);
	chip->setAccessibleName(text);
}

//! The names of the channels listening, in mixer order.
QStringList listeningNames(const mixerstripinfo::Context& context)
{
	QList<int> indices = context.listening.values();
	std::sort(indices.begin(), indices.end());
	QStringList names;
	for (const int index : indices)
	{
		if (index < Engine::mixer()->numChannels()) { names << Engine::mixer()->mixerChannel(index)->m_name; }
	}
	return names;
}

} // namespace

MixerModeBar::MixerModeBar(QWidget* parent) :
	QWidget(parent),
	m_filter(new QComboBox(this))
{
	setObjectName(QStringLiteral("mixerModeBar"));
	setAccessibleName(tr("Mixer modes"));
	auto* layout = new QHBoxLayout(this);
	layout->setContentsMargins(tokens::space::sm, tokens::space::xs, tokens::space::sm, tokens::space::xs);
	layout->setSpacing(tokens::space::sm);
	m_viewButtons = addSegment(layout, tr("View"), "view", mixerviewoptions::viewNames(), {tr("Mix"), tr("Routing")});
	m_densityButtons = addSegment(layout, tr("Density"), "density", mixerviewoptions::densityNames(), {tr("Compact"), tr("Expanded")});
	auto* show = new QLabel(tr("Show:"), this);
	show->setBuddy(m_filter);
	layout->addWidget(show);
	const QStringList captions{tr("All Channels"), tr("Instruments"), tr("Audio"), tr("Returns"), tr("Used"), tr("Selected")};
	for (int i = 0; i < captions.size(); ++i) { m_filter->addItem(captions.at(i), mixerviewoptions::filterNames().at(i)); }
	m_filter->setObjectName(QStringLiteral("mixerModeFilter"));
	m_filter->setAccessibleName(tr("Show channels"));
	m_filter->setProperty("controlCommand", kViewCommand);
	connect(m_filter, &QComboBox::activated, this, [this](int row) { setOption(QStringLiteral("filter"), m_filter->itemData(row).toString()); });
	layout->addWidget(m_filter);
	auto* pick = new QPushButton(tr("Pick..."), this);  // MX-005: the channels the Selected filter shows
	pick->setObjectName(QStringLiteral("mixerModePickSelected"));
	pick->setMinimumHeight(tokens::targetSize());
	pick->setAccessibleName(tr("Pick the channels to show"));
	pick->setToolTip(tr("Pick the channels the Selected filter shows (Ctrl+click a strip to add or remove one)"));
	pick->setProperty("controlCommand", kViewCommand);
	connect(pick, &QPushButton::clicked, this, &MixerModeBar::pickSelected);
	layout->addWidget(pick);
	layout->addStretch(1);
	m_faders = addChip(layout, QStringLiteral("mixerModeFaders"), tr("What every fader drives: its channel's volume, or its send to one return"), &MixerModeBar::fillFaders);
	m_solo = addChip(layout, QStringLiteral("mixerModeSolo"), tr("The solo mode: whether a solo unsolos the others"), &MixerModeBar::fillSolo);
	m_listen = addChip(layout, QStringLiteral("mixerModeListen"), tr("The listen bus: the channels heard in place of the mix, after or before their faders"), &MixerModeBar::fillListen);
	m_master = addChip(layout, QStringLiteral("mixerModeMaster"), tr("Where the master plays (its hardware output) and its speaker format"), &MixerModeBar::fillMaster);
	auto* matrix = new QPushButton(tr("Routing Matrix..."), this);
	matrix->setObjectName(QStringLiteral("mixerModeMatrix"));
	matrix->setMinimumHeight(tokens::targetSize());
	matrix->setProperty("controlCommand", QStringLiteral("routing.matrix"));
	connect(matrix, &QPushButton::clicked, this, [] { showRoutingMatrix(); });
	layout->addWidget(matrix);
	refresh(mixerstripinfo::buildContext());
}

QHash<QString, QPushButton*> MixerModeBar::addSegment(QHBoxLayout* layout, const QString& label, const char* key,
	const QStringList& values, const QStringList& captions)
{
	auto* caption = new QLabel(label + QLatin1Char(':'), this);
	layout->addWidget(caption);
	auto* group = new QButtonGroup(this);
	QHash<QString, QPushButton*> buttons;
	for (int i = 0; i < values.size(); ++i)
	{
		auto* button = new QPushButton(captions.at(i), this);
		button->setObjectName(QStringLiteral("mixerMode_%1").arg(values.at(i)));
		button->setCheckable(true);
		button->setMinimumHeight(tokens::targetSize());
		button->setAccessibleName(QStringLiteral("%1: %2").arg(label, captions.at(i)));
		button->setProperty("controlCommand", kViewCommand);
		group->addButton(button);
		layout->addWidget(button);
		const QString name = QLatin1String(key);
		const QString value = values.at(i);
		connect(button, &QPushButton::clicked, this, [this, name, value] { setOption(name, value); });
		buttons.insert(value, button);
	}
	caption->setBuddy(buttons.value(values.value(0)));
	return buttons;
}

QToolButton* MixerModeBar::addChip(QHBoxLayout* layout, const QString& objectName, const QString& hint,
	void (MixerModeBar::*fill)(QMenu*))
{
	auto* chip = new QToolButton(this);
	chip->setObjectName(objectName);
	chip->setPopupMode(QToolButton::InstantPopup);
	chip->setToolButtonStyle(Qt::ToolButtonTextOnly);
	chip->setMinimumHeight(tokens::targetSize());
	chip->setFocusPolicy(Qt::StrongFocus);
	chip->setToolTip(hint);
	chip->setAccessibleDescription(hint);
	auto* menu = new QMenu(chip);
	// Built when it opens, so it shows the state now; the submenus of the last opening go with it.
	connect(menu, &QMenu::aboutToShow, this, [this, menu, fill] {
		for (QMenu* old : menu->findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly)) { old->deleteLater(); }
		menu->clear();
		(this->*fill)(menu);
	});
	connect(menu, &QMenu::triggered, this, &MixerModeBar::changed);
	chip->setMenu(menu);
	layout->addWidget(chip);
	return chip;
}

void MixerModeBar::setOption(const QString& key, const QJsonValue& value)
{
	// A refusal is shown by runCommand; the bar follows the state either way at the view's next pass.
	runCommand(this, CommandCall{kViewCommand, {{key, value}}, {}, QString(), false});
	emit changed();
}

void MixerModeBar::pickSelected()
{
	const QStringList picked = pickChannels(this, tr("Channels to Show"), QString());
	if (picked.isEmpty()) { return; }  // cancelled: the selection and the filter stay as they were
	runCommand(this, CommandCall{kViewCommand, {{QStringLiteral("selected_channels"), QJsonArray::fromStringList(picked)},
		{QStringLiteral("filter"), QStringLiteral("selected")}}, {}, QString(), false});
	emit changed();
}

void MixerModeBar::fillFaders(QMenu* menu)
{
	const int target = mixeroverlays::sendsOnFaders();
	commandChoice(menu, tr("Volume Faders"), kViewCommand, {{QStringLiteral("sends_on_faders"), QString()}}, target < 0);
	menu->addSeparator();
	Mixer* mixer = Engine::mixer();
	for (int i = 1; i < mixer->numChannels(); ++i)
	{
		MixerChannel* channel = mixer->mixerChannel(i);
		if (!channel->isBus() && i != target) { continue; }
		commandChoice(menu, tr("Sends to %1").arg(channel->m_name), kViewCommand,
			{{QStringLiteral("sends_on_faders"), control::channelIdOf(channel)}}, i == target);
	}
}

void MixerModeBar::fillSolo(QMenu* menu)
{
	const bool additive = mixerstrip::additiveSolo();
	const QString id = QStringLiteral("mixer.solo_mode");
	commandChoice(menu, tr("Exclusive (a solo unsolos the others)"), id, {{QStringLiteral("mode"), QStringLiteral("exclusive")}}, !additive);
	commandChoice(menu, tr("Additive (each solo adds to the others)"), id, {{QStringLiteral("mode"), QStringLiteral("additive")}}, additive);
}

void MixerModeBar::fillListen(QMenu* menu)
{
	const bool pfl = mixerlisten::mode() == mixerlisten::Mode::PreFader;
	const QString id = QStringLiteral("mixer.listen_mode");
	commandChoice(menu, tr("After the Fader (AFL)"), id, {{QStringLiteral("mode"), QStringLiteral("afl")}}, !pfl);
	commandChoice(menu, tr("Before the Fader (PFL)"), id, {{QStringLiteral("mode"), QStringLiteral("pfl")}}, pfl);
	menu->addSeparator();
	QAction* clear = addCommandAction(menu, tr("Clear the Listen Bus"), id,
		[id] { return CommandCall{id, {{QStringLiteral("clear"), true}}, {}, QString(), false}; });
	clear->setEnabled(clear->isEnabled() && mixerlisten::anyListening());
}

void MixerModeBar::fillMaster(QMenu* menu)
{
	const QString master = control::channelIdOf(Engine::mixer()->mixerChannel(0));
	addHardwareMenu(menu, master);
	addSurroundMenu(menu, master);
}

void MixerModeBar::refresh(const mixerstripinfo::Context& context)
{
	const mixerviewoptions::Options& options = mixerviewoptions::options();
	if (QPushButton* view = m_viewButtons.value(options.view); view != nullptr && !view->isChecked()) { view->setChecked(true); }
	if (QPushButton* density = m_densityButtons.value(options.density); density != nullptr && !density->isChecked()) { density->setChecked(true); }
	const int row = m_filter->findData(options.filter);
	if (row != m_filter->currentIndex())
	{
		const QSignalBlocker quiet(m_filter);
		m_filter->setCurrentIndex(row);
	}
	refreshChips(context);
}

void MixerModeBar::refreshChips(const mixerstripinfo::Context& context)
{
	Mixer* mixer = Engine::mixer();
	const int target = mixeroverlays::sendsOnFaders();
	setChip(m_faders, target < 0 ? tr("Faders: Volume") : tr("Faders: Sends to %1").arg(mixer->mixerChannel(target)->m_name), target >= 0);
	setChip(m_solo, mixerstrip::additiveSolo() ? tr("Solo: Additive") : tr("Solo: Exclusive"), false);
	const QStringList names = listeningNames(context);
	const QString point = context.preFaderListen ? tr("PFL") : tr("AFL");
	setChip(m_listen, names.isEmpty() ? tr("Listen: Off")
		: tr("Listen %1: %2").arg(point, names.size() == 1 ? names.first() : tr("%1 channels").arg(names.size())), !names.isEmpty());
	const mixerstripinfo::StripInfo master = mixerstripinfo::describe(*mixer, 0, context);
	setChip(m_master, master.format.isEmpty() ? tr("Master: %1").arg(master.hardware)
		: tr("Master: %1, %2").arg(master.hardware, master.format), false);
}

} // namespace lmms::gui
