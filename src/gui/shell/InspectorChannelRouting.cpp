/*
 * InspectorChannelRouting.cpp - a mixer channel's inserts, sends and strip settings in the Inspector
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
#include "InspectorChannelRouting.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

#include "CommandForm.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "Effect.h"
#include "Knob.h"
#include "LedCheckBox.h"
#include "Mixer.h"

namespace lmms::gui::inspectorrouting
{

namespace
{

QLabel* heading(const QString& text, QWidget* parent)
{
	auto* label = new QLabel(text.toUpper(), parent);
	label->setObjectName(QStringLiteral("deviceSection"));
	return label;
}

Knob* smallKnob(FloatModel* model, const QString& label, QWidget* parent)
{
	auto* knob = new Knob(KnobType::Small17, label, parent);
	knob->setFont(tokens::font(tokens::TextRole::Small));
	knob->setModel(model);
	knob->setHintText(label + QLatin1Char(':'), QString());
	knob->setAccessibleName(label);
	return knob;
}

QString channelName(const MixerChannel* channel)
{
	if (!channel->m_name.isEmpty()) { return channel->m_name; }
	return channel->index() == 0 ? QObject::tr("Master") : QString::number(channel->index());
}

//! A button that runs @a call (a form when it asks) and then rebuilds the section.
QPushButton* commandButton(const QString& text, const CommandCall& call, QWidget* parent, const std::function<void()>& rebuild)
{
	auto* button = new QPushButton(text, parent);
	button->setProperty("controlCommand", call.id);
	QObject::connect(button, &QPushButton::clicked, parent, [button, call, rebuild] {
		if (runCommand(button->window(), call).ok) { rebuild(); }
	});
	return button;
}

void addInserts(QVBoxLayout* column, MixerChannel* channel, const QString& id, QWidget* parent, const std::function<void()>& rebuild)
{
	column->addWidget(heading(QObject::tr("Inserts"), parent));
	for (Effect* effect : channel->m_fxChain.effects())
	{
		auto* on = new QCheckBox(effect->displayName(), parent);
		on->setChecked(effect->isEnabled());
		on->setProperty("controlCommand", QStringLiteral("plugin.bypass"));
		const QString plugin = control::effectIdOf(effect);
		QObject::connect(on, &QCheckBox::toggled, parent, [id, plugin](bool enabled) {
			ControlRegistry::instance()->invoke(QStringLiteral("plugin.bypass"),
				{{QStringLiteral("target"), id}, {QStringLiteral("plugin"), plugin}, {QStringLiteral("bypass"), !enabled}});
		});
		column->addWidget(on);
	}
	column->addWidget(commandButton(QObject::tr("Add Insert..."), CommandCall{QStringLiteral("plugin.load"),
		{{QStringLiteral("target"), id}}, {}, QObject::tr("Add an insert"), true}, parent, rebuild));
}

void addSends(QVBoxLayout* column, MixerChannel* channel, const QString& id, QWidget* parent, const std::function<void()>& rebuild)
{
	column->addWidget(heading(QObject::tr("Sends"), parent));
	auto* grid = new QGridLayout;
	grid->setHorizontalSpacing(tokens::space::md);
	for (MixerRoute* route : channel->m_sends)
	{
		const int row = grid->rowCount();
		const QString to = control::channelIdOf(route->receiver());
		auto* name = new QLabel(channelName(route->receiver()) + (route->preFader() ? QObject::tr(" (pre)") : QString()), parent);
		Knob* amount = smallKnob(route->amount(), QObject::tr("Send"), parent);
		Knob* pan = smallKnob(route->pan(), QObject::tr("Send pan"), parent);
		auto* remove = new QToolButton(parent);
		remove->setText(QStringLiteral("×"));
		remove->setToolTip(QObject::tr("Remove the send to %1").arg(channelName(route->receiver())));
		remove->setProperty("controlCommand", QStringLiteral("mixer.route_remove"));
		QObject::connect(remove, &QToolButton::clicked, parent, [id, to, rebuild] {
			ControlRegistry::instance()->invoke(QStringLiteral("mixer.route_remove"), {{QStringLiteral("channel"), id}, {QStringLiteral("to"), to}});
			rebuild();
		});
		// The route's models go with it (an undo, a script): hide the row at once, then rebuild.
		QObject::connect(route->amount(), &QObject::destroyed, parent, [amount, pan, rebuild] {
			amount->hide();
			pan->hide();
			rebuild();
		});
		grid->addWidget(name, row, 0);
		grid->addWidget(amount, row, 1);
		grid->addWidget(pan, row, 2);
		grid->addWidget(remove, row, 3);
	}
	grid->setColumnStretch(0, 1);
	column->addLayout(grid);
	column->addWidget(commandButton(QObject::tr("Add Send..."), CommandCall{QStringLiteral("mixer.send_to"),
		{{QStringLiteral("channel"), id}}, {{QStringLiteral("pre_fader"), false}}, QObject::tr("Add a send"), true}, parent, rebuild));
}

void addStrip(QVBoxLayout* column, MixerChannel* channel, QWidget* parent)
{
	column->addWidget(heading(QObject::tr("Strip"), parent));
	auto* knobs = new QHBoxLayout;
	knobs->addWidget(smallKnob(&channel->m_trimModel, QObject::tr("Trim (dB)"), parent));
	knobs->addWidget(smallKnob(&channel->m_widthModel, QObject::tr("Width"), parent));
	knobs->addStretch(1);
	column->addLayout(knobs);
	auto* eq = new QHBoxLayout;  // MX-085
	eq->addWidget(smallKnob(&channel->m_eqLowModel, QObject::tr("Low (dB)"), parent));
	eq->addWidget(smallKnob(&channel->m_eqMidModel, QObject::tr("Mid (dB)"), parent));
	eq->addWidget(smallKnob(&channel->m_eqMidFreqModel, QObject::tr("Mid (Hz)"), parent));
	eq->addWidget(smallKnob(&channel->m_eqHighModel, QObject::tr("High (dB)"), parent));
	eq->addStretch(1);
	column->addLayout(eq);
	auto* flags = new QGridLayout;
	const std::pair<BoolModel*, QString> boxes[] = {{&channel->m_polarityLeftModel, QObject::tr("Invert L")},
		{&channel->m_polarityRightModel, QObject::tr("Invert R")}, {&channel->m_monoModel, QObject::tr("Mono")},
		{&channel->m_soloSafeModel, QObject::tr("Solo safe")}, {&channel->m_swapModel, QObject::tr("Swap L/R")},
		{&channel->m_inputMonoModel, QObject::tr("Mono input")}};
	int i = 0;
	for (const auto& [model, text] : boxes)
	{
		auto* box = new LedCheckBox(text, parent);
		box->setModel(model);
		flags->addWidget(box, i / 2, i % 2);
		++i;
	}
	column->addLayout(flags);
}

//! MX-064: where the channel's output goes - any channel after it, through mixer.route_to.
void addOutput(QVBoxLayout* column, MixerChannel* channel, const QString& id, QWidget* parent, const std::function<void()>& rebuild)
{
	if (channel->index() == 0) { return; }  // the master's output is the audio device
	auto* output = new QComboBox(parent);
	output->setProperty("controlCommand", QStringLiteral("mixer.route_to"));
	output->setAccessibleName(QObject::tr("Output"));
	output->addItem(QObject::tr("Route the output to..."), QString());
	for (int i = 0; i < Engine::mixer()->numChannels(); ++i)
	{
		const MixerChannel* to = Engine::mixer()->mixerChannel(i);
		if (to == channel) { continue; }
		output->addItem(i == 0 ? QObject::tr("Master") : to->m_name, control::channelIdOf(to));
	}
	QObject::connect(output, qOverload<int>(&QComboBox::activated), parent, [output, id, parent, rebuild](int index) {
		const QString to = output->itemData(index).toString();
		if (to.isEmpty()) { return; }
		runCommand(parent, CommandCall{QStringLiteral("mixer.route_to"), {{QStringLiteral("channel"), id}, {QStringLiteral("to"), to}}, {}, {}, false});
		rebuild();
	});
	column->addWidget(heading(QObject::tr("Output"), parent));
	column->addWidget(output);
}

} // namespace

QWidget* routingSection(MixerChannel* channel, const QString& id, QWidget* parent, const std::function<void()>& rebuild)
{
	auto* section = new QWidget(parent);
	auto* column = new QVBoxLayout(section);
	column->setContentsMargins(0, 0, 0, 0);
	column->setSpacing(tokens::space::sm);
	addInserts(column, channel, id, section, rebuild);
	addSends(column, channel, id, section, rebuild);
	addStrip(column, channel, section);
	addOutput(column, channel, id, section, rebuild);
	return section;
}

} // namespace lmms::gui::inspectorrouting
