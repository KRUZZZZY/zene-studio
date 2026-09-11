/*
 * VcaGroup.cpp - VCA / mix-and-edit groups for the mixer (task #622)
 *
 * Copyright (c) 2026 LMMS developers
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

#include "VcaGroup.h"

#include <algorithm>

#include "Mixer.h"

namespace lmms
{

VcaGroup::VcaGroup(Mixer* mixer, int id, const QString& name) :
	QObject(mixer),
	m_mixer(mixer),
	m_id(id),
	m_name(name),
	// Same range/step as a mixer channel fader so the group fader and a
	// channel fader mean the same thing (1.0 = unity, 2.0 = +6 dB).
	m_vcaModel(1.0f, 0.0f, 2.0f, 0.001f, mixer),
	m_muteModel(false, mixer),
	m_soloModel(false, mixer),
	m_members()
{
	m_vcaModel.setDisplayName(name + ">" + tr("VCA gain"));
	m_muteModel.setDisplayName(name + ">" + tr("Mute"));
	m_soloModel.setDisplayName(name + ">" + tr("Solo"));

	// The published member gain must follow the fader and the mute flag the
	// moment either changes -- the audio thread reads nothing else. Direct
	// connections, so a caller never observes a published gain that disagrees
	// with the model it just wrote.
	if (m_mixer != nullptr)
	{
		connect(&m_vcaModel, &FloatModel::dataChanged, this,
			[this] { if (m_mixer != nullptr) { m_mixer->refreshGroups(); } },
			Qt::DirectConnection);
		connect(&m_muteModel, &BoolModel::dataChanged, this,
			[this] { if (m_mixer != nullptr) { m_mixer->refreshGroups(); } },
			Qt::DirectConnection);
		// Soloing a group makes exactly its members audible (see
		// Mixer::applyGroupSolo).
		connect(&m_soloModel, &BoolModel::dataChanged, this,
			[this]
			{
				if (m_mixer != nullptr)
				{
					m_mixer->applyGroupSolo(this, m_soloModel.value());
				}
			},
			Qt::DirectConnection);
	}
}



void VcaGroup::setName(const QString& name)
{
	m_name = name;
	m_vcaModel.setDisplayName(name + ">" + tr("VCA gain"));
	m_muteModel.setDisplayName(name + ">" + tr("Mute"));
	m_soloModel.setDisplayName(name + ">" + tr("Solo"));
}



float VcaGroup::gain() const
{
	return m_muteModel.value() ? 0.0f : m_vcaModel.value();
}



bool VcaGroup::contains(mix_ch_t channel) const
{
	return std::find(m_members.begin(), m_members.end(), channel) != m_members.end();
}



bool VcaGroup::addMember(mix_ch_t channel)
{
	// Master is the mix bus itself, never a member; a channel already in this
	// group is a no-op; a channel in another group is refused so that solo
	// linking stays single-valued.
	if (m_mixer == nullptr || channel == 0 || contains(channel)
		|| m_mixer->vcaGroupForChannel(channel) != nullptr)
	{
		return false;
	}

	m_members.push_back(channel);
	std::sort(m_members.begin(), m_members.end());
	m_mixer->refreshGroups();
	return true;
}



bool VcaGroup::removeMember(mix_ch_t channel)
{
	const auto it = std::find(m_members.begin(), m_members.end(), channel);
	if (it == m_members.end())
	{
		return false;
	}

	m_members.erase(it);
	if (m_mixer != nullptr)
	{
		// A channel that left the group plays at unity again; refreshGroups
		// resets every channel before applying the groups that still exist.
		m_mixer->refreshGroups();
	}
	return true;
}



void VcaGroup::channelDeleted(mix_ch_t channel)
{
	auto it = std::find(m_members.begin(), m_members.end(), channel);
	if (it != m_members.end())
	{
		m_members.erase(it);
	}
	for (mix_ch_t& member : m_members)
	{
		if (member > channel)
		{
			--member;
		}
	}
}



void VcaGroup::channelsSwapped(mix_ch_t a, mix_ch_t b)
{
	for (mix_ch_t& member : m_members)
	{
		if (member == a)
		{
			member = b;
		}
		else if (member == b)
		{
			member = a;
		}
	}
	std::sort(m_members.begin(), m_members.end());
}

} // namespace lmms
