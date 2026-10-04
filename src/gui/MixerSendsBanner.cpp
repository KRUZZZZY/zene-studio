/*
 * MixerSendsBanner.cpp - the mixer's mode banner while every fader drives one return's sends
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

#include "MixerSendsBanner.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>

#include "CommandForm.h"
#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerOverlays.h"

namespace lmms::gui
{

MixerSendsBanner::MixerSendsBanner(QWidget* parent) :
	QWidget(parent),
	m_message(new QLabel(this)),
	m_picker(new QComboBox(this)),
	m_exit(new QPushButton(tr("Back to Volume Faders"), this))
{
	setObjectName(QStringLiteral("mixerSendsBanner"));
	setAccessibleName(tr("Sends on faders"));
	const tokens::Palette& pal = tokens::palette();
	// The accent fill is the mode cue: on the channel faders there is no bar at all.
	setAttribute(Qt::WA_StyledBackground, true);
	setStyleSheet(QStringLiteral("#mixerSendsBanner { background-color: %1; } #mixerSendsBanner > QLabel { color: %2; }")
		.arg(pal.accent.name(), pal.textOnAccent.name()));
	auto* layout = new QHBoxLayout(this);
	layout->setContentsMargins(tokens::space::sm, tokens::space::xs, tokens::space::sm, tokens::space::xs);
	layout->setSpacing(tokens::space::sm);
	m_message->setObjectName(QStringLiteral("mixerSendsBannerText"));
	m_message->setTextFormat(Qt::RichText);
	layout->addWidget(m_message, 1);
	auto* pickerLabel = new QLabel(tr("Show sends to:"), this);
	pickerLabel->setBuddy(m_picker);
	layout->addWidget(pickerLabel);
	m_picker->setObjectName(QStringLiteral("mixerSendsBannerPicker"));
	m_picker->setAccessibleName(tr("The return whose sends the faders show"));
	m_picker->setProperty("controlCommand", QStringLiteral("window.mixer_view"));
	layout->addWidget(m_picker);
	m_exit->setObjectName(QStringLiteral("mixerSendsBannerExit"));
	m_exit->setToolTip(tr("Every fader back on its channel's volume (window.mixer_view)"));
	m_exit->setProperty("controlCommand", QStringLiteral("window.mixer_view"));
	layout->addWidget(m_exit);
	connect(m_picker, &QComboBox::activated, this, [this](int row) { choose(m_picker->itemData(row).toString()); });
	connect(m_exit, &QPushButton::clicked, this, [this] { choose(QString()); });
	hide();
}

void MixerSendsBanner::refresh()
{
	const int target = mixeroverlays::sendsOnFaders();
	setVisible(target >= 0);
	if (target < 0) { return; }
	const QString name = Engine::mixer()->mixerChannel(target)->m_name.toHtmlEscaped();
	const QString text = tr("<b>Sends on faders: %1</b> &mdash; every fader sets its channel's send to %1; "
		"\"No send\" marks a channel without one").arg(name);
	if (m_message->text() != text) { m_message->setText(text); }  // set only on a change: each set is announced
	fillPicker(target);
}

void MixerSendsBanner::choose(const QString& channel)
{
	// The command refuses a channel gone since the picker was filled; the refusal is shown, nothing changes.
	runCommand(this, CommandCall{QStringLiteral("window.mixer_view"), {{QStringLiteral("sends_on_faders"), channel}},
		{}, QString(), false});
}

void MixerSendsBanner::fillPicker(int target)
{
	// The returns, and the channel shown if it is not one (an agent may pick any channel).
	Mixer* mixer = Engine::mixer();
	QList<int> offered;
	QString signature;
	for (int i = 1; i < mixer->numChannels(); ++i)
	{
		if (!mixer->mixerChannel(i)->isBus() && i != target) { continue; }
		offered << i;
		signature += control::channelIdOf(mixer->mixerChannel(i)) + QLatin1Char('=') + mixer->mixerChannel(i)->m_name + QLatin1Char(';');
	}
	const QSignalBlocker blocker(m_picker);
	if (signature != m_pickerSignature)
	{
		m_pickerSignature = signature;
		m_picker->clear();
		for (const int i : offered) { m_picker->addItem(mixer->mixerChannel(i)->m_name, control::channelIdOf(mixer->mixerChannel(i))); }
	}
	m_picker->setCurrentIndex(m_picker->findData(control::channelIdOf(mixer->mixerChannel(target))));
}

} // namespace lmms::gui
