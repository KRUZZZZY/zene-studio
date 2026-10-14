/*
 * ControlCommandsMixerRename.cpp - mixer.rename_channel: a mixer channel's name, as one undo step
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

/*! 0.5.0 theme 13: a channel's name was written straight into MixerChannel::m_name by the strip's own rename field,
 *  so an agent could not rename a channel and the Inspector could not offer the name in place. mixer.rename_channel is
 *  that one path: the strip, the Inspector, a script and MCP all take it, and one undo puts the old name back. */

#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

//! The longest name a channel takes (a strip shows a few characters; the rest is for the tooltip and the Inspector).
constexpr int MaxChannelNameLength = 64;

//! Names the channel @a id names now (found by its persistent ch-<n> on every run: an undo can outlive a renumbering)
//! @a name, and tells the mixer's views.
void nameChannel(const QString& id, const QString& name)
{
	ControlResult ignored;
	MixerChannel* channel = resolveMixerChannel(id, &ignored);
	if (channel == nullptr) { return; }
	channel->m_name = name;
	emit Engine::mixer()->dataChanged();
}

ControlResult renameChannel(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
	if (channel == nullptr) { return error; }
	const QString name = args.value(QStringLiteral("name")).toString().trimmed();
	if (name.isEmpty() || name.size() > MaxChannelNameLength)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'name' is 1..%1 characters, not only spaces").arg(MaxChannelNameLength));
	}
	const QString id = channelIdOf(channel);
	const QString before = channel->m_name;
	QJsonObject result{{QStringLiteral("channel"), id}, {QStringLiteral("name"), name}, {QStringLiteral("previous"), before}};
	if (before == name) { return ControlResult::success(result); }  // the name it has: nothing changed, no step
	addUndoStep([id, before]() { nameChannel(id, before); }, [id, name]() { nameChannel(id, name); });
	nameChannel(id, name);
	Engine::getSong()->setModified();
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("channel"), id},
		{QStringLiteral("name"), before}}, QStringLiteral("control.undo"), {}, true,
		QStringLiteral("action checkpoint: the recorded undo step puts the previous channel name back")));
	return ControlResult::success(result);
}

} // namespace

void registerMixerRenameCommands(ControlRegistry& registry)
{
	defineCommand(registry, QStringLiteral("mixer.rename_channel"),
		QStringLiteral("Name a mixer channel ('channel', ch-<n>): 'name', 1..64 characters (trimmed). 'previous' answers the "
			"name it had. One undo step; the name it already has changes nothing."),
		{{QStringLiteral("channel"), stringProperty()}, {QStringLiteral("name"), stringProperty()}},
		{QStringLiteral("channel"), QStringLiteral("name")}, true, renameChannel);
}

} // namespace lmms
