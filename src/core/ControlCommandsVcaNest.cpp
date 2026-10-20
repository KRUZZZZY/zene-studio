/*
 * ControlCommandsVcaNest.cpp - vca.set_parent / set_links: nested groups and a group's chosen links
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

/*! MX-070 / MX-071 (0.5.0 L09): a VCA group inside another (its gain scaled by the one above - and by
 *  every group above that, MixerGroups.h) and a group whose links are chosen: level, mute and solo each
 *  on or off, and a suspend switch that turns them all off without losing them. Each change is one undo
 *  step (an action step holding the previous values); the members' gains are republished at once. */

#include "ControlCommandsVcaShared.h"
#include "ControlCommandDef.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerGroups.h"
#include "VcaGroup.h"

namespace lmms
{

using namespace vcacontrol;

namespace
{

//! A group's nesting and links as they stand, to put back.
struct GroupLinks
{
	int parent = VcaGroup::NoParent;
	bool gain = true;
	bool mute = true;
	bool solo = true;
	bool suspended = false;

	bool operator==(const GroupLinks& other) const = default;
};

GroupLinks linksOf(const VcaGroup* group)
{
	return {group->parentId(), group->linksGain(), group->linksMute(), group->linksSolo(), group->isSuspended()};
}

void apply(int id, const GroupLinks& links)
{
	Mixer* mixer = Engine::mixer();
	if (VcaGroup* group = mixer->vcaGroup(id))
	{
		group->setParentId(links.parent);
		group->setLinks(links.gain, links.mute, links.solo);
		group->setSuspended(links.suspended);
		mixer->refreshGroups();
	}
}

QString parentIdOf(const GroupLinks& links)
{
	return links.parent != VcaGroup::NoParent ? vcaGroupId(links.parent) : QString();
}

QJsonObject linksJson(const GroupLinks& links)
{
	return {{QStringLiteral("gain"), links.gain}, {QStringLiteral("mute"), links.mute},
		{QStringLiteral("solo"), links.solo}, {QStringLiteral("suspended"), links.suspended}};
}

//! The arguments that make `command` put `links` back: the parent for set_parent, the four links for set_links.
QJsonObject inverseArgs(int id, const GroupLinks& links, const QString& command)
{
	QJsonObject args = command == QLatin1String("vca.set_parent")
		? QJsonObject{{QStringLiteral("parent"), parentIdOf(links)}} : linksJson(links);
	args.insert(QStringLiteral("group"), vcaGroupId(id));
	return args;
}

ControlResult change(VcaGroup* group, const GroupLinks& next, const QString& command)
{
	const int id = group->id();
	const GroupLinks before = linksOf(group);
	// A request for what the group already has changes nothing: it records no undo step, marks nothing modified and
	// carries no transaction, so the registry records nothing for the call either.
	const bool changed = next != before;
	if (changed)
	{
		control::addUndoStep([id, before]() { apply(id, before); }, [id, next]() { apply(id, next); });
		apply(id, next);
		Engine::getSong()->setModified();
	}
	QJsonObject result = groupState(group);
	result.insert(QStringLiteral("parent"), parentIdOf(next));
	result.insert(QStringLiteral("links"), linksJson(next));
	if (changed)
	{
		QJsonObject previous{{QStringLiteral("group"), vcaGroupId(id)}, {QStringLiteral("parent"), parentIdOf(before)},
			{QStringLiteral("links"), linksJson(before)}};
		result.insert(QStringLiteral("__transaction"), control::transactionPayload(previous, command,
			inverseArgs(id, before, command), true,
			QStringLiteral("action checkpoint: the recorded undo step puts the group's previous nesting and links back")));
	}
	return ControlResult::success(result);
}

ControlResult setParent(const QJsonObject& args)
{
	ControlResult error;
	VcaGroup* group = resolveGroup(args, &error);
	if (group == nullptr) { return error; }
	GroupLinks next = linksOf(group);
	next.parent = VcaGroup::NoParent;
	const QString parentId = args.value(QStringLiteral("parent")).toString();
	if (!parentId.isEmpty())
	{
		VcaGroup* parent = resolveGroup({{QStringLiteral("group"), parentId}}, &error);
		if (parent == nullptr) { return error; }
		if (wouldLoop(*Engine::mixer(), group->id(), parent->id()))
		{
			return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("%1 inside %2 would close a loop (or nest past %3 levels)")
				.arg(vcaGroupId(group->id()), parentId).arg(MaxGroupDepth));
		}
		next.parent = parent->id();
	}
	return change(group, next, QStringLiteral("vca.set_parent"));
}

ControlResult setLinks(const QJsonObject& args)
{
	ControlResult error;
	VcaGroup* group = resolveGroup(args, &error);
	if (group == nullptr) { return error; }
	GroupLinks next = linksOf(group);
	next.gain = args.value(QStringLiteral("gain")).toBool(next.gain);
	next.mute = args.value(QStringLiteral("mute")).toBool(next.mute);
	next.solo = args.value(QStringLiteral("solo")).toBool(next.solo);
	next.suspended = args.value(QStringLiteral("suspended")).toBool(next.suspended);
	return change(group, next, QStringLiteral("vca.set_links"));
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QStringLiteral("vca.") + QLatin1String(verb), description, args, true, handler);
}

} // namespace

void registerVcaNestCommands(ControlRegistry& registry)
{
	add(registry, "set_parent", QStringLiteral("Put a group inside another ('parent', a vca-<n>; empty or left out takes it out): the "
		"parent's gain - and every group's above it - scales this group's members too. A loop, or nesting deeper than "
		"8, is refused. One undo step."), control::objectSchema({{QStringLiteral("group"), control::stringProperty()},
		{QStringLiteral("parent"), control::stringProperty()}}, {QStringLiteral("group")}), setParent);
	add(registry, "set_links", QStringLiteral("Choose what a group links on its members: 'gain' (its fader), 'mute' and "
		"'solo', each on by default; 'suspended' turns every link off while keeping the choice. A field left out keeps "
		"its value. One undo step."), control::objectSchema({{QStringLiteral("group"), control::stringProperty()},
		{QStringLiteral("gain"), control::booleanProperty()}, {QStringLiteral("mute"), control::booleanProperty()},
		{QStringLiteral("solo"), control::booleanProperty()}, {QStringLiteral("suspended"), control::booleanProperty()}},
		{QStringLiteral("group")}), setLinks);
}

} // namespace lmms
