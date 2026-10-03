/*
 * MixerSnapshots.cpp - named snapshots of the whole mixer, stored with the project
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

#include "MixerSnapshots.h"

#include <algorithm>

#include <QDomElement>

#include "Engine.h"
#include "Mixer.h"

namespace lmms
{

namespace
{

MixerChannel* channelWithId(int id)
{
	Mixer* mixer = Engine::mixer();
	for (int i = 0; i < mixer->numChannels(); ++i)
	{
		if (mixer->mixerChannel(i)->id() == id) { return mixer->mixerChannel(i); }
	}
	return nullptr;
}

MixerChannelState stateOf(MixerChannel* channel)
{
	MixerChannelState state;
	state.id = channel->id();
	state.volume = channel->m_volumeModel.value();
	state.pan = channel->m_panModel.value();
	state.muted = channel->m_muteModel.value();
	state.soloed = channel->m_soloModel.value();
	for (MixerRoute* route : channel->m_sends)
	{
		state.sends.push_back(MixerSendState{route->receiver()->id(), route->amount()->value(), route->pan()->value(),
			route->enabledModel()->value()});
	}
	return state;
}

void applyChannel(MixerChannel* channel, const MixerChannelState& state, const MixerSnapshots::Parts& parts)
{
	if (parts.volume) { channel->m_volumeModel.setValue(state.volume); }
	if (parts.pan) { channel->m_panModel.setValue(state.pan); }
	if (parts.mute) { channel->m_muteModel.setValue(state.muted); }
	if (parts.solo) { channel->m_soloModel.setValue(state.soloed); }
}

//! The sends of @a channel set from @a sends; how many of them the channel no longer has.
int applySends(MixerChannel* channel, const std::vector<MixerSendState>& sends)
{
	int missing = 0;
	for (const MixerSendState& send : sends)
	{
		const auto route = std::find_if(channel->m_sends.begin(), channel->m_sends.end(),
			[&send](MixerRoute* each) { return each->receiver()->id() == send.to; });
		if (route == channel->m_sends.end()) { ++missing; continue; }
		(*route)->amount()->setValue(send.amount);
		(*route)->pan()->setValue(send.pan);
		(*route)->enabledModel()->setValue(send.enabled);
	}
	return missing;
}

QDomElement channelElement(QDomDocument& doc, const MixerChannelState& state)
{
	QDomElement element = doc.createElement(QStringLiteral("channel"));
	element.setAttribute(QStringLiteral("id"), state.id);
	element.setAttribute(QStringLiteral("volume"), state.volume);
	element.setAttribute(QStringLiteral("pan"), state.pan);
	element.setAttribute(QStringLiteral("muted"), state.muted ? 1 : 0);
	element.setAttribute(QStringLiteral("soloed"), state.soloed ? 1 : 0);
	for (const MixerSendState& send : state.sends)
	{
		QDomElement sendElement = doc.createElement(QStringLiteral("send"));
		sendElement.setAttribute(QStringLiteral("to"), send.to);
		sendElement.setAttribute(QStringLiteral("amount"), send.amount);
		sendElement.setAttribute(QStringLiteral("pan"), send.pan);
		sendElement.setAttribute(QStringLiteral("enabled"), send.enabled ? 1 : 0);
		element.appendChild(sendElement);
	}
	return element;
}

MixerChannelState channelFrom(const QDomElement& element)
{
	MixerChannelState state;
	state.id = element.attribute(QStringLiteral("id")).toInt();
	state.volume = element.attribute(QStringLiteral("volume"), QStringLiteral("1")).toFloat();
	state.pan = element.attribute(QStringLiteral("pan")).toFloat();
	state.muted = element.attribute(QStringLiteral("muted")).toInt() != 0;
	state.soloed = element.attribute(QStringLiteral("soloed")).toInt() != 0;
	for (QDomElement send = element.firstChildElement(QStringLiteral("send")); !send.isNull();
		send = send.nextSiblingElement(QStringLiteral("send")))
	{
		state.sends.push_back(MixerSendState{send.attribute(QStringLiteral("to")).toInt(), send.attribute(QStringLiteral("amount"),
			QStringLiteral("1")).toFloat(), send.attribute(QStringLiteral("pan")).toFloat(),
			send.attribute(QStringLiteral("enabled"), QStringLiteral("1")).toInt() != 0});
	}
	return state;
}

} // namespace

MixerSnapshots& MixerSnapshots::instance()
{
	static MixerSnapshots* snapshots = [] {
		auto* made = new MixerSnapshots();
		ProjectSection::registerSection(made);
		return made;
	}();
	return *snapshots;
}

const MixerSnapshot* MixerSnapshots::find(const QString& name) const
{
	const auto it = std::find_if(m_snapshots.begin(), m_snapshots.end(), [&name](const MixerSnapshot& s) { return s.name == name; });
	return it != m_snapshots.end() ? &*it : nullptr;
}

bool MixerSnapshots::store(const MixerSnapshot& snapshot)
{
	for (MixerSnapshot& each : m_snapshots)
	{
		if (each.name == snapshot.name)
		{
			each = snapshot;
			return true;
		}
	}
	if (static_cast<int>(m_snapshots.size()) >= MaxSnapshots) { return false; }
	m_snapshots.push_back(snapshot);
	return true;
}

bool MixerSnapshots::remove(const QString& name)
{
	const auto before = m_snapshots.size();
	m_snapshots.erase(std::remove_if(m_snapshots.begin(), m_snapshots.end(), [&name](const MixerSnapshot& s) { return s.name == name; }),
		m_snapshots.end());
	return m_snapshots.size() != before;
}

MixerSnapshot MixerSnapshots::capture(const QString& name)
{
	MixerSnapshot snapshot;
	snapshot.name = name;
	Mixer* mixer = Engine::mixer();
	for (int i = 0; i < mixer->numChannels(); ++i) { snapshot.channels.push_back(stateOf(mixer->mixerChannel(i))); }
	return snapshot;
}

MixerSnapshots::Applied MixerSnapshots::apply(const MixerSnapshot& snapshot, const Parts& parts)
{
	Applied applied;
	for (const MixerChannelState& state : snapshot.channels)
	{
		MixerChannel* channel = channelWithId(state.id);
		if (channel == nullptr)
		{
			++applied.missingChannels;
			continue;
		}
		applyChannel(channel, state, parts);
		if (parts.sends) { applied.missingSends += applySends(channel, state.sends); }
		++applied.channels;
	}
	return applied;
}

void MixerSnapshots::save(QDomDocument& doc, QDomElement& parent) const
{
	QDomElement list = doc.createElement(elementName());
	for (const MixerSnapshot& snapshot : m_snapshots)
	{
		QDomElement element = doc.createElement(QStringLiteral("snapshot"));
		element.setAttribute(QStringLiteral("name"), snapshot.name);
		for (const MixerChannelState& state : snapshot.channels) { element.appendChild(channelElement(doc, state)); }
		list.appendChild(element);
	}
	parent.appendChild(list);
}

void MixerSnapshots::load(const QDomElement& element)
{
	m_snapshots.clear();
	for (QDomElement child = element.firstChildElement(QStringLiteral("snapshot"));
		!child.isNull() && static_cast<int>(m_snapshots.size()) < MaxSnapshots; child = child.nextSiblingElement(QStringLiteral("snapshot")))
	{
		MixerSnapshot snapshot;
		snapshot.name = child.attribute(QStringLiteral("name"));
		for (QDomElement channel = child.firstChildElement(QStringLiteral("channel")); !channel.isNull();
			channel = channel.nextSiblingElement(QStringLiteral("channel")))
		{
			snapshot.channels.push_back(channelFrom(channel));
		}
		m_snapshots.push_back(snapshot);
	}
}

} // namespace lmms
