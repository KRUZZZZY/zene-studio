/*
 * VcaGroup.h - VCA / mix-and-edit groups for the mixer (task #622)
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

#ifndef LMMS_VCA_GROUP_H
#define LMMS_VCA_GROUP_H

#include <vector>

#include <QObject>
#include <QString>

#include "AutomatableModel.h"
#include "LmmsTypes.h"

namespace lmms
{

class Mixer;

//! A VCA / mix-and-edit group (task #622).
//!
//! A group owns one fader (`vcaModel()`), a mute and a solo flag, and a set of
//! member mixer channels. The group's gain is published to each member as a
//! *separate* factor (`MixerChannel::m_vcaGain`) and applied as one extra
//! multiply on the audio path. A member's own volume model is never written,
//! which is what makes the operation exactly reversible: moving the group
//! fader and moving it back leaves every member model bit-identical, and a
//! member's fader keeps the value the user set for it (the group is a relative,
//! not absolute, offset -- the semantics a VCA has and the one that is easy to
//! get wrong).
//!
//! Threading. Everything in this class is control-thread state: the models,
//! the member list, and every mutator. The single piece of state the audio
//! thread reads is the gain `Mixer::refreshGroups()` publishes into each member
//! channel's relaxed atomic -- no allocation, no locking, no growth on the
//! audio path. Nothing here is called from `MixerChannel::doProcessing()`.
//!
//! Persistence. `Mixer::saveSettings()` writes one `<vcagroup>` element per
//! group inside `<mixer>` (id, name, `vca`/`muted`/`soloed` attributes and one
//! `<member channel="N"/>` child each); `Mixer::loadSettings()` recreates them,
//! so grouping and the fader value survive a round trip through the project
//! file. A project without `<vcagroup>` elements loads with no groups at all,
//! in which case every channel publishes unity gain.
class LMMS_EXPORT VcaGroup : public QObject
{
	Q_OBJECT
public:
	VcaGroup(Mixer* mixer, int id, const QString& name);

	//! Stable identifier, unique within a Mixer and preserved across save/load.
	int id() const { return m_id; }

	const QString& name() const { return m_name; }
	void setName(const QString& name);

	FloatModel* vcaModel() { return &m_vcaModel; }
	BoolModel* muteModel() { return &m_muteModel; }
	BoolModel* soloModel() { return &m_soloModel; }

	//! Member channel indices, ascending. Control thread only.
	const std::vector<mix_ch_t>& members() const { return m_members; }

	bool contains(mix_ch_t channel) const;

	//! Add a channel. Refuses master (index 0), a duplicate, and a channel
	//! that already belongs to another group -- a channel is in at most one
	//! group, so "soloing a member" has exactly one meaning. Returns true when
	//! the membership changed.
	bool addMember(mix_ch_t channel);

	//! Remove a channel; a no-op for a channel that is not a member. The
	//! channel plays at unity again afterwards.
	bool removeMember(mix_ch_t channel);

	//! Mixer::deleteChannel() bookkeeping: drop `channel` and shift every
	//! higher index down by one, so the surviving members keep pointing at the
	//! same channels.
	void channelDeleted(mix_ch_t channel);

	//! Mixer::moveChannelLeft() bookkeeping: two channels swapped places.
	void channelsSwapped(mix_ch_t a, mix_ch_t b);

	//! The gain this group publishes to its members right now: the fader value,
	//! or 0 when the group is muted. Mute is folded into the same factor as the
	//! fader (a VCA at zero gain *is* a mute), so muting never writes a member's
	//! own mute model and is exactly reversible too.
	float gain() const;

	//! Drop the link back to the Mixer. Called by Mixer before it deletes
	//! groups (and by its destructor) so the model change hooks cannot reach a
	//! half-destroyed Mixer.
	void detach() { m_mixer = nullptr; }

private:
	Mixer* m_mixer;
	int m_id;
	QString m_name;
	FloatModel m_vcaModel;
	BoolModel m_muteModel;
	BoolModel m_soloModel;
	//! Member channel indices, ascending; control thread only.
	std::vector<mix_ch_t> m_members;
};

} // namespace lmms

#endif // LMMS_VCA_GROUP_H
