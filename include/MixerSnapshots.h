/*
 * MixerSnapshots.h - named snapshots of the whole mixer, stored with the project
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

#ifndef LMMS_MIXER_SNAPSHOTS_H
#define LMMS_MIXER_SNAPSHOTS_H

#include <vector>

#include <QString>

#include "ProjectSection.h"
#include "lmms_export.h"

namespace lmms
{

//! One send of a channel as a snapshot holds it: the receiving channel's persistent id and the send's settings.
struct MixerSendState
{
	int to = 0;
	float amount = 1.f;
	float pan = 0.f;
	bool enabled = true;
};

//! One channel as a snapshot holds it, by its persistent id (ch-<n>), so a moved channel is still found.
struct MixerChannelState
{
	int id = 0;
	float volume = 1.f;
	float pan = 0.f;
	bool muted = false;
	bool soloed = false;
	std::vector<MixerSendState> sends;
};

struct MixerSnapshot
{
	QString name;
	std::vector<MixerChannelState> channels;
};

/*! 0.5.0 L12 (MX-170): named snapshots of the mixer - every channel's fader, pan, mute and solo and
 *  every send's level, pan and switch - stored with the project as <mixer-snapshots> (written only
 *  when there is one, so a project that never stored one re-saves byte for byte; an absent element
 *  loads as none). Recall writes the parts it is asked for onto the channels and sends that still
 *  exist and counts the ones that do not; it never creates or deletes a channel or a send, so it
 *  cannot change the mixer's graph. UI-thread state: the audio thread reads only the models recall
 *  writes, as it reads them when a fader is moved by hand. */
class LMMS_EXPORT MixerSnapshots : public ProjectSection
{
public:
	static constexpr int MaxSnapshots = 64;

	//! Which parts of a snapshot recall writes.
	struct Parts
	{
		bool volume = true;
		bool pan = true;
		bool mute = true;
		bool solo = true;
		bool sends = true;
	};

	//! What a recall wrote and what it could not find.
	struct Applied
	{
		int channels = 0;
		int missingChannels = 0;
		int missingSends = 0;
	};

	static MixerSnapshots& instance();

	const std::vector<MixerSnapshot>& snapshots() const { return m_snapshots; }
	const MixerSnapshot* find(const QString& name) const;
	//! Store @a snapshot, replacing one of the same name; false when the store is full.
	bool store(const MixerSnapshot& snapshot);
	bool remove(const QString& name);
	void restore(const std::vector<MixerSnapshot>& snapshots) { m_snapshots = snapshots; }

	//! The mixer as it is now, under @a name.
	static MixerSnapshot capture(const QString& name);
	//! Write @a parts of @a snapshot onto the live mixer.
	static Applied apply(const MixerSnapshot& snapshot, const Parts& parts);

	QString elementName() const override { return QStringLiteral("mixer-snapshots"); }
	bool shouldPersist() const override { return !m_snapshots.empty(); }
	void save(QDomDocument& doc, QDomElement& parent) const override;
	void load(const QDomElement& element) override;
	void clear() override { m_snapshots.clear(); }

private:
	MixerSnapshots() = default;
	std::vector<MixerSnapshot> m_snapshots;
};

} // namespace lmms

#endif // LMMS_MIXER_SNAPSHOTS_H
