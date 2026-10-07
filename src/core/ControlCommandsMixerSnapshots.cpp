/*
 * ControlCommandsMixerSnapshots.cpp - store, recall, list and remove named mixer snapshots
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
/*! 0.5.0 L12 (MX-170): mixer snapshots - the whole mix (faders, pans, mutes, solos, send levels, send
 *  pans and send switches) stored under a name with the project and recalled in one undo step, all of
 *  it or only some parts (A/B two mixes, or bring back only the faders of an earlier pass). */

#include <QJsonArray>

#include "ControlCommandDef.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "MixerSnapshots.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

MixerSnapshots& snapshots() { return MixerSnapshots::instance(); }

const QStringList& partNames()
{
	static const QStringList names{QStringLiteral("volume"), QStringLiteral("pan"), QStringLiteral("mute"), QStringLiteral("solo"),
		QStringLiteral("sends")};
	return names;
}

//! The parts @a args names ('parts'; every part when left out), or a failure in @a error.
bool partsOf(const QJsonObject& args, MixerSnapshots::Parts* parts, ControlResult* error)
{
	if (!args.contains(QStringLiteral("parts"))) { return true; }
	QStringList named;
	for (const QJsonValue& value : args.value(QStringLiteral("parts")).toArray())
	{
		if (!partNames().contains(value.toString()))
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'parts' takes %1").arg(partNames().join(QStringLiteral(", "))));
			return false;
		}
		named << value.toString();
	}
	if (named.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'parts' must name at least one part"));
		return false;
	}
	*parts = MixerSnapshots::Parts{named.contains(partNames()[0]), named.contains(partNames()[1]), named.contains(partNames()[2]),
		named.contains(partNames()[3]), named.contains(partNames()[4])};
	return true;
}

QJsonObject channelJson(const MixerChannelState& state)
{
	QJsonArray sends;
	for (const MixerSendState& send : state.sends)
	{
		sends.append(QJsonObject{{QStringLiteral("to"), channelId(send.to)}, {QStringLiteral("amount"), send.amount},
			{QStringLiteral("pan"), send.pan}, {QStringLiteral("enabled"), send.enabled}});
	}
	return {{QStringLiteral("channel"), channelId(state.id)}, {QStringLiteral("volume"), state.volume}, {QStringLiteral("pan"), state.pan},
		{QStringLiteral("muted"), state.muted}, {QStringLiteral("soloed"), state.soloed}, {QStringLiteral("sends"), sends}};
}

QJsonArray listJson()
{
	QJsonArray out;
	for (const MixerSnapshot& snapshot : snapshots().snapshots())
	{
		out.append(QJsonObject{{QStringLiteral("name"), snapshot.name}, {QStringLiteral("channels"), static_cast<int>(snapshot.channels.size())}});
	}
	return out;
}

//! Replace the snapshot list with what @a edit leaves, as one undo step.
void recordList(const std::vector<MixerSnapshot>& before)
{
	const std::vector<MixerSnapshot> after = snapshots().snapshots();
	addUndoStep([before]() { MixerSnapshots::instance().restore(before); }, [after]() { MixerSnapshots::instance().restore(after); });
	Engine::getSong()->setModified();
}

ControlResult storeSnapshot(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString().trimmed();
	if (name.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("a snapshot needs a 'name'")); }
	const std::vector<MixerSnapshot> before = snapshots().snapshots();
	const MixerSnapshot made = MixerSnapshots::capture(name);
	if (!snapshots().store(made))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the project holds %1 snapshots, the most it keeps; remove one first")
			.arg(MixerSnapshots::MaxSnapshots));
	}
	recordList(before);
	return ControlResult::success({{QStringLiteral("name"), name}, {QStringLiteral("channels"), static_cast<int>(made.channels.size())},
		{QStringLiteral("replaced"), before.size() == snapshots().snapshots().size()}, {QStringLiteral("snapshots"), listJson()}});
}

ControlResult recallSnapshot(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString();
	const MixerSnapshot* found = snapshots().find(name);
	if (found == nullptr) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no mixer snapshot '%1'").arg(name)); }
	MixerSnapshots::Parts parts;
	ControlResult error;
	if (!partsOf(args, &parts, &error)) { return error; }
	const MixerSnapshot target = *found;
	const MixerSnapshot before = MixerSnapshots::capture(QString());
	const MixerSnapshots::Applied applied = MixerSnapshots::apply(target, parts);
	addUndoStep([before]() { MixerSnapshots::apply(before, MixerSnapshots::Parts{}); },
		[target, parts]() { MixerSnapshots::apply(target, parts); });
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("name"), name}, {QStringLiteral("channels"), applied.channels},
		{QStringLiteral("missing_channels"), applied.missingChannels}, {QStringLiteral("missing_sends"), applied.missingSends}});
}

ControlResult listSnapshots(const QJsonObject& args)
{
	QJsonObject result{{QStringLiteral("snapshots"), listJson()}, {QStringLiteral("count"), static_cast<int>(snapshots().snapshots().size())},
		{QStringLiteral("limit"), MixerSnapshots::MaxSnapshots}};
	if (!args.contains(QStringLiteral("name"))) { return ControlResult::success(result); }
	const MixerSnapshot* found = snapshots().find(args.value(QStringLiteral("name")).toString());
	if (found == nullptr) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no such mixer snapshot")); }
	QJsonArray channels;
	for (const MixerChannelState& state : found->channels) { channels.append(channelJson(state)); }
	result.insert(QStringLiteral("channels"), channels);
	return ControlResult::success(result);
}

ControlResult removeSnapshot(const QJsonObject& args)
{
	const QString name = args.value(QStringLiteral("name")).toString();
	const std::vector<MixerSnapshot> before = snapshots().snapshots();
	if (!snapshots().remove(name)) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no mixer snapshot '%1'").arg(name)); }
	recordList(before);
	return ControlResult::success({{QStringLiteral("removed"), name}, {QStringLiteral("snapshots"), listJson()}});
}

void add(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args, const QStringList& required,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QLatin1String(id), description, objectSchema(args, QJsonArray::fromStringList(required)), mutating, handler);
}

} // namespace

void registerMixerSnapshotCommands(ControlRegistry& registry)
{
	MixerSnapshots::instance();  // registers its project section before a project loads
	const QJsonObject named{{QStringLiteral("name"), stringProperty()}};
	const QStringList name{QStringLiteral("name")};
	add(registry, "mixer.snapshot_store", QStringLiteral("Store the whole mixer under 'name': every channel's fader, pan, mute "
		"and solo and every send's level, pan and switch. A snapshot of the same name is replaced. Saved with the project; "
		"one undo step."), named, name, true, storeSnapshot);
	add(registry, "mixer.snapshot_recall", QStringLiteral("Put the mixer back as snapshot 'name' had it - every part, or only "
		"the 'parts' named (volume, pan, mute, solo, sends). Channels and sends that no longer exist are skipped and counted; "
		"nothing is created or deleted. One undo step."),
		{{QStringLiteral("name"), stringProperty()}, {QStringLiteral("parts"), arrayProperty()}}, name, true, recallSnapshot);
	add(registry, "mixer.snapshot_list", QStringLiteral("The project's mixer snapshots (name, channel count); with 'name', that "
		"snapshot's channels and sends as it holds them."), named, {}, false, listSnapshots);
	add(registry, "mixer.snapshot_remove", QStringLiteral("Remove mixer snapshot 'name'. One undo step."), named, name, true, removeSnapshot);
}

} // namespace lmms
