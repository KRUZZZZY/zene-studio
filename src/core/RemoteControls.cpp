/*
 * RemoteControls.cpp - eight MIDI controls driving the selected device's parameters, page by page
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
#include "RemoteControls.h"

#include <algorithm>
#include <cmath>

#include <QJsonArray>

#include "AutomatableModel.h"
#include "ControlDeviceSupport.h"
#include "MidiEvent.h"

namespace lmms
{

namespace
{

std::unique_ptr<RemoteControls> s_remote;

//! The parameters of the device the knobs drive, or none when it does not resolve.
QList<AutomatableModel*> parametersOf(const QString& target, const QString& plugin)
{
	ControlTarget resolved;
	ControlResult ignored;
	QList<AutomatableModel*> models;
	QString name;
	if (target.isEmpty() || !resolveControlTarget(target, &resolved, &ignored)) { return models; }
	if (!controlDeviceParameters(resolved, plugin, &models, &name, &ignored)) { models.clear(); }
	return models;
}

int ccValueOf(const AutomatableModel* model)
{
	const float span = model->maxValue<float>() - model->minValue<float>();
	if (span <= 0.f) { return 0; }
	return std::clamp(static_cast<int>(std::lround((model->value<float>() - model->minValue<float>()) / span * 127.f)), 0, 127);
}

} // namespace

RemoteControls::RemoteControls() = default;

RemoteControls::~RemoteControls()
{
	close();
}

RemoteControls* RemoteControls::instance() { return s_remote.get(); }

RemoteControls* RemoteControls::connect(const QString& input, const QString& output, int channel, int firstCc)
{
	auto remote = std::make_unique<RemoteControls>();
	remote->m_channel = channel;
	remote->m_firstCc = std::clamp(firstCc, 0, 127 - Knobs + 1);
	remote->open(QStringLiteral("Remote Controls"), input, output);
	if (s_remote != nullptr) { remote->setTarget(s_remote->m_target, s_remote->m_plugin); }
	s_remote = std::move(remote);
	return s_remote.get();
}

void RemoteControls::disconnect() { s_remote.reset(); }

void RemoteControls::setTarget(const QString& target, const QString& plugin)
{
	if (target == m_target && plugin == m_plugin) { return; }
	m_target = target;
	m_plugin = plugin;
	m_page = 0;
	sendPage();
}

void RemoteControls::followSelection(const QString& target, const QString& plugin)
{
	if (m_follow && !target.isEmpty()) { setTarget(target, plugin); }
}

int RemoteControls::pageCount() const
{
	return std::max(1, (static_cast<int>(parametersOf(m_target, m_plugin).size()) + Knobs - 1) / Knobs);
}

void RemoteControls::setPage(int page)
{
	m_page = std::clamp(page, 0, pageCount() - 1);
	sendPage();
}

bool RemoteControls::applyKnob(int knob, int value)
{
	const QList<AutomatableModel*> models = parametersOf(m_target, m_plugin);
	const int index = m_page * Knobs + knob;
	if (knob < 0 || knob >= Knobs || index >= models.size()) { return false; }
	AutomatableModel* model = models.at(index);
	const float lo = model->minValue<float>();
	const float hi = model->maxValue<float>();
	model->setValue(lo + (hi - lo) * static_cast<float>(std::clamp(value, 0, 127)) / 127.f);
	return true;
}

bool RemoteControls::decode(const MidiEvent& event, RemoteTurn* out)
{
	// MIDI client thread: nothing allocated or locked.
	if (event.type() != MidiControlChange) { return false; }
	if (m_channel >= 0 && event.channel() != m_channel) { return false; }
	const int knob = event.controllerNumber() - m_firstCc;
	if (knob < 0 || knob >= Knobs) { return false; }
	*out = RemoteTurn{knob, event.controllerValue()};
	return true;
}

void RemoteControls::sendPage()
{
	if (!isOpen() || output().isEmpty()) { return; }
	const QList<AutomatableModel*> models = parametersOf(m_target, m_plugin);
	for (int knob = 0; knob < Knobs; ++knob)
	{
		const int index = m_page * Knobs + knob;
		const int value = index < models.size() ? ccValueOf(models.at(index)) : 0;
		send(MidiEvent(MidiControlChange, static_cast<int8_t>(std::max(0, m_channel)),
			static_cast<int16_t>(m_firstCc + knob), static_cast<int16_t>(value)));
	}
}

QJsonObject RemoteControls::state() const
{
	const QList<AutomatableModel*> models = parametersOf(m_target, m_plugin);
	QJsonArray page;
	for (int knob = 0; knob < Knobs; ++knob)
	{
		const int index = m_page * Knobs + knob;
		if (index >= models.size()) { break; }
		page.append(QJsonObject{{QStringLiteral("knob"), knob}, {QStringLiteral("name"), models.at(index)->displayName()},
			{QStringLiteral("value"), static_cast<double>(models.at(index)->value<float>())}});
	}
	return {{QStringLiteral("connected"), isOpen()}, {QStringLiteral("subscribed"), subscribed()}, {QStringLiteral("input"), input()},
		{QStringLiteral("output"), output()},
		{QStringLiteral("channel"), m_channel}, {QStringLiteral("first_cc"), m_firstCc}, {QStringLiteral("target"), m_target},
		{QStringLiteral("plugin"), m_plugin}, {QStringLiteral("follow"), m_follow}, {QStringLiteral("page"), m_page},
		{QStringLiteral("pages"), pageCount()}, {QStringLiteral("parameters"), page},
		{QStringLiteral("events_in"), eventsIn()}};
}

} // namespace lmms
