/*
 * DeviceCardState.cpp - what a device card says about its device: bypass, A/B, latency, hosting, preset
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

#include "DeviceCardState.h"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QToolButton>

#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "DeviceAbState.h"
#include "DevicePanel.h"
#include "CommandForm.h"
#include "DesignTokens.h"
#include "Effect.h"
#include "Engine.h"
#include "InstrumentTrack.h"

namespace lmms::gui::devicestate
{

namespace
{

//! What one oop.get_state device entry says is wrong with its host, or empty.
QString problemOf(const QJsonObject& hosting)
{
	if (hosting.value(QStringLiteral("refused")).toBool()
		|| hosting.value(QStringLiteral("state")).toString() == QLatin1String("refused-crash-loop")) { return QStringLiteral("quarantined"); }
	return hosting.value(QStringLiteral("state")).toString() == QLatin1String("client-exited") ? QStringLiteral("crashed") : QString();
}

void collect(const QJsonValue& device, QHash<QString, QString>* out)
{
	const QJsonObject entry = device.toObject();
	const QString problem = problemOf(entry.value(QStringLiteral("hosting")).toObject());
	if (problem.isEmpty()) { return; }
	out->insert(hostingKey(entry.value(QStringLiteral("target")).toString(), entry.value(QStringLiteral("device")).toString()), problem);
}

QString abText(const State& state)
{
	const QString heard = state.onB ? QStringLiteral("B") : QStringLiteral("A");
	if (state.hasA && state.hasB) { return QObject::tr("Hearing %1 (A and B stored)").arg(heard); }
	return QObject::tr("Hearing %1 (only %2 stored)").arg(heard, state.hasA ? QStringLiteral("A") : QStringLiteral("B"));
}

QString hostingText(const QString& hosting)
{
	return hosting == QLatin1String("quarantined") ? QObject::tr("Quarantined after repeated crashes") : QObject::tr("Crashed - restart it from Hosting");
}

} // namespace

QString hostingKey(const QString& target, const QString& plugin) { return target + QLatin1Char('|') + plugin; }

QHash<QString, QString> hostingProblems()
{
	// A report (not_mutating): no transaction, nothing forwarded to a collaboration host.
	QHash<QString, QString> out;
	const QJsonArray chains = ControlRegistry::instance()->invoke(QStringLiteral("oop.get_state"), QJsonObject{})
		.result.value(QStringLiteral("chains")).toArray();
	for (const QJsonValue& chain : chains)
	{
		const QJsonObject object = chain.toObject();
		for (const QJsonValue& device : object.value(QStringLiteral("devices")).toArray()) { collect(device, &out); }
		if (object.contains(QStringLiteral("instrument"))) { collect(object.value(QStringLiteral("instrument")), &out); }
	}
	return out;
}

State ofEffect(Effect* effect, const QString& hosting)
{
	State state;
	state.effect = true;
	state.bypassed = !effect->isEnabled();
	state.processing = effect->isProcessingAudio();
	control::AbState ab;
	state.abDefined = control::abSlotState(effect, &ab);
	state.onB = ab.onB;
	state.hasA = ab.hasA;
	state.hasB = ab.hasB;
	state.latencyFrames = effect->compensatedLatencyFrames();
	state.hosting = hosting;
	state.preset = effect->property(control::presetNameProperty()).toString();
	return state;
}

State ofInstrument(const InstrumentTrack* track, const QString& hosting)
{
	State state;
	state.hosting = hosting;
	state.preset = track != nullptr ? track->property(control::presetNameProperty()).toString() : QString();
	return state;
}

QString latencyText(int frames)
{
	const double rate = Engine::audioEngine() != nullptr ? Engine::audioEngine()->outputSampleRate() : 0.0;
	const QString ms = rate > 0 ? QObject::tr(" (%1 ms)").arg(1000.0 * frames / rate, 0, 'f', 1) : QString();
	return QObject::tr("%n sample(s)", "", frames) + ms;
}

QStringList parts(const State& state)
{
	QStringList out;
	if (state.bypassed) { out << QObject::tr("Bypassed"); }
	if (state.abDefined) { out << abText(state); }
	if (state.latencyFrames > 0) { out << QObject::tr("Latency %1").arg(latencyText(state.latencyFrames)); }
	if (!state.hosting.isEmpty()) { out << hostingText(state.hosting); }
	if (!state.preset.isEmpty()) { out << QObject::tr("Preset: %1").arg(state.preset); }
	if (out.isEmpty() && state.effect) { out << QObject::tr("On"); }
	return out;
}

QString text(const State& state) { return parts(state).join(QStringLiteral(" · ")); }

QJsonObject json(const State& state)
{
	const QString ab = !state.abDefined ? QString() : state.onB ? QStringLiteral("b") : QStringLiteral("a");
	return {{QStringLiteral("bypassed"), state.bypassed}, {QStringLiteral("ab"), ab}, {QStringLiteral("latency_frames"), state.latencyFrames},
		{QStringLiteral("hosting"), state.hosting}, {QStringLiteral("preset"), state.preset}, {QStringLiteral("text"), text(state)}};
}

} // namespace lmms::gui::devicestate

namespace lmms::gui
{

// Theme 12: the DeviceCard members that show this state (DevicePanel.h), kept beside it.

void DeviceCard::addStateButtons(QHBoxLayout* row, QWidget* header)
{
	if (m_spec.effect == nullptr) { return; }
	// Shown once plugin.ab has a slot stored: the letter heard, and a click compares (plugin.ab toggle).
	m_ab = new QToolButton(header);
	m_ab->setObjectName(QStringLiteral("deviceAb"));
	m_ab->setAutoRaise(true);
	m_ab->setMinimumSize(tokens::targetSize(), tokens::targetSize());
	m_ab->setVisible(false);
	connect(m_ab, &QToolButton::clicked, this, [this] {
		const QJsonObject args{{QStringLiteral("target"), m_spec.target}, {QStringLiteral("plugin"), m_spec.plugin},
			{QStringLiteral("action"), QStringLiteral("toggle")}};
		runCommand(this, CommandCall{QStringLiteral("plugin.ab"), args, {}, QString(), false});
		refreshStateLine();
	});
	row->addWidget(m_ab);
}

void DeviceCard::refreshState(const QHash<QString, QString>& hostingProblems)
{
	m_hosting = hostingProblems.value(devicestate::hostingKey(m_spec.target, m_spec.plugin));
	refreshActivity();
}

namespace
{

devicestate::State stateOf(const DeviceCard::Spec& spec, const QString& hosting)
{
	return spec.effect != nullptr ? devicestate::ofEffect(spec.effect, hosting) : devicestate::ofInstrument(spec.instrumentTrack, hosting);
}

} // namespace

QJsonObject DeviceCard::stateJson() const { return devicestate::json(stateOf(m_spec, m_hosting)); }

void DeviceCard::refreshStateLine()
{
	if (m_state == nullptr) { return; }  // the header's first refresh runs before the line is made
	const devicestate::State state = stateOf(m_spec, m_hosting);
	const QString text = devicestate::text(state);
	if (m_state->text() != text)  // set only on a change: each set is announced
	{
		m_state->setText(text);
		m_state->setVisible(!text.isEmpty());
		m_state->setAccessibleName(tr("%1 state: %2").arg(m_spec.title, text));
		setAccessibleDescription(text);  // the card itself carries its state for a screen reader
	}
	if (m_ab == nullptr) { return; }
	const QString heard = state.onB ? QStringLiteral("B") : QStringLiteral("A");
	m_ab->setVisible(state.abDefined);
	m_ab->setText(heard);
	m_ab->setToolTip(tr("Hearing %1 - click to compare A and B").arg(heard));
	m_ab->setAccessibleName(tr("Compare A and B on %1, hearing %2").arg(m_spec.title, heard));
}

} // namespace lmms::gui
