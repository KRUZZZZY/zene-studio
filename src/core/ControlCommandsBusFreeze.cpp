/*
 * ControlCommandsBusFreeze.cpp - freeze.bus / freeze.unfreeze_bus (Q050-124): render a mixer bus's output and
 *                                play the render in place of its live inputs until it is unfrozen
 *
 * The engine half is include/MixerBusFreeze.h (the take on the MixerChannel, its playback at the bus's own place
 * in the routing graph, its `frozenAudio` attribute on the <mixerchannel> element) and BounceInPlace::renderBus
 * (the child render that captures the bus instead of the master). The mixer strip's context menu runs these same
 * registered commands (gui/MixerBusFreezeMenu.cpp).
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
 */

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QString>
#include <QUuid>

#include "BounceInPlace.h"
#include "ControlBounceSupport.h"
#include "ControlEdit.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerBusFreeze.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

const QString ClauseBusTake = QStringLiteral("action checkpoint: the recorded step installs or clears the bus's "
	"take by its stable channel id (busfreeze::install / busfreeze::clear, re-reading the take's file), which is "
	"the whole of the change - the bus's inputs, devices, fader and sends are never written");

//! The live channel with stable id @a id, or nullptr (it was removed since the step was recorded).
MixerChannel* channelById(int id)
{
	Mixer* mixer = Engine::mixer();
	for (int i = 0; mixer != nullptr && i < static_cast<int>(mixer->numChannels()); ++i)
	{
		if (mixer->mixerChannel(i)->id() == id) { return mixer->mixerChannel(i); }
	}
	return nullptr;
}

void reinstall(int id, const QString& path)
{
	if (MixerChannel* channel = channelById(id)) { busfreeze::install(*channel, path, busfreeze::loadTake(path, nullptr)); }
}

void unfreezeById(int id)
{
	if (MixerChannel* channel = channelById(id)) { busfreeze::clear(*channel); }
}

//! Beside the project in "<project>-bounces" (the bounce.in_place convention), one new file per freeze, so a take
//! an undo step still names is never overwritten by a later freeze of the same bus.
QString defaultTakePath(const MixerChannel* bus)
{
	const Song* song = Engine::getSong();
	const QString project = song->projectFileName();
	const QDir base = project.isEmpty() ? QDir(QDir::tempPath()) : QFileInfo(project).dir();
	const QString name = project.isEmpty()
		? QStringLiteral("untitled-%1").arg(song->property("_zeneUnsavedProjectId").toString())
		: QFileInfo(project).completeBaseName();
	const QString unique = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
	return QDir(base.filePath(name + QStringLiteral("-bounces")))
		.filePath(QStringLiteral("%1-frozen-bus-%2.wav").arg(channelIdOf(bus), unique));
}

QJsonObject busStateJson(const MixerChannel* bus)
{
	QJsonObject result;
	result.insert(QStringLiteral("channel"), channelIdOf(bus));
	result.insert(QStringLiteral("name"), bus->m_name);
	result.insert(QStringLiteral("frozen"), bus->m_freeze.isFrozen());
	result.insert(QStringLiteral("audio"), bus->m_freeze.path);
	// False when the take's file has moved or cannot be decoded: still frozen, but the live inputs play.
	result.insert(QStringLiteral("audio_ready"), bus->m_freeze.audioReady());
	if (busfreeze::fileMissing(*bus))
	{
		result.insert(QStringLiteral("audio_missing"), true);
		result.insert(QStringLiteral("recovery"), QStringLiteral("project.relink points the take (mixerchannel/"
			"frozenAudio) at the moved file, then reopen the project; freeze.unfreeze_bus clears it"));
	}
	return result;
}

ControlResult freezeBus(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* bus = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
	if (bus == nullptr) { return error; }
	const QString requested = args.value(QStringLiteral("out")).toString();

	// The render runs BEFORE anything is recorded: a refused or failed render changes nothing (SPEC A16).
	const BounceInPlace::Result rendered =
		BounceInPlace::renderBus(bus, requested.isEmpty() ? defaultTakePath(bus) : requested);
	if (!rendered.ok) { return ControlResult::failure(ControlErrorKind::Refused, rendered.error); }
	QString loadError;
	std::shared_ptr<const SampleBuffer> take = busfreeze::loadTake(rendered.path, &loadError);
	if (take == nullptr) { return ControlResult::failure(ControlErrorKind::Refused, loadError); }

	const int id = bus->id();
	const QString path = rendered.path;
	addUndoStep([id] { unfreezeById(id); }, [id, path] { reinstall(id, path); });
	busfreeze::install(*bus, path, std::move(take));

	QJsonObject result = renderJson(rendered);
	const QJsonObject state = busStateJson(bus);
	for (auto entry = state.constBegin(); entry != state.constEnd(); ++entry) { result.insert(entry.key(), entry.value()); }
	QJsonObject before{{QStringLiteral("channel"), channelIdOf(bus)}, {QStringLiteral("frozen"), false}};
	result.insert(QStringLiteral("__transaction"), transactionPayload(before, QStringLiteral("freeze.unfreeze_bus"),
		QJsonObject{{QStringLiteral("channel"), channelIdOf(bus)}}, true, ClauseBusTake));
	return ControlResult::success(result);
}

ControlResult unfreezeBus(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* bus = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
	if (bus == nullptr) { return error; }
	if (!bus->m_freeze.isFrozen())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is not frozen").arg(channelIdOf(bus)));
	}
	const QJsonObject before = busStateJson(bus);
	const int id = bus->id();
	const QString path = bus->m_freeze.path;
	addUndoStep([id, path] { reinstall(id, path); }, [id] { unfreezeById(id); });
	busfreeze::clear(*bus);

	QJsonObject result = busStateJson(bus);
	// The inverse NAMED is control.undo: the step reinstalls this exact take, which freeze.bus (a new render) is not.
	result.insert(QStringLiteral("__transaction"),
		transactionPayload(before, QStringLiteral("control.undo"), {}, true, ClauseBusTake));
	return ControlResult::success(result);
}

QJsonObject stateSchema()
{
	return objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("frozen"), booleanProperty()},
		{QStringLiteral("audio"), stringProperty()},
		{QStringLiteral("audio_ready"), booleanProperty()},
		{QStringLiteral("audio_missing"), booleanProperty()},
		{QStringLiteral("recovery"), stringProperty()},
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("frames"), integerProperty()},
		{QStringLiteral("bytes"), integerProperty()},
		{QStringLiteral("sha256"), stringProperty()},
		{QStringLiteral("start_ticks"), integerProperty()},
		{QStringLiteral("end_ticks"), integerProperty()},
	});
}

void registerFreezeBus(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("freeze.bus");
	cmd.group = QStringLiteral("freeze");
	cmd.verb = QStringLiteral("bus");
	cmd.description = QStringLiteral("Freeze a mixer bus (channel, ch-<n>, must be a bus): its output - every input "
		"routed to it, through its trim, devices, VCA and pan, i.e. the signal just before its fader - is rendered to a "
		"32-bit float WAV ('out', or a new file beside the project) and, while the transport plays, the bus plays "
		"that take in place of its live inputs and devices. Its fader, its sends to other buses and the master, its "
		"taps and its meters keep working on the take. Refused for a channel that is not a bus, a frozen bus, a bus "
		"nothing is routed to and a project whose tempo map is on. Saved with the project; one control.undo "
		"unfreezes. Blocking: the render runs in a child process against a copy of the session.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("out"), stringProperty()},
	}, {QStringLiteral("channel")});
	cmd.resultSchema = stateSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return freezeBus(args); };
	registry.registerCommand(cmd);
}

void registerUnfreezeBus(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("freeze.unfreeze_bus");
	cmd.group = QStringLiteral("freeze");
	cmd.verb = QStringLiteral("unfreeze_bus");
	cmd.description = QStringLiteral("Drop a frozen bus's take: the bus plays its live inputs through its own "
		"devices again. Its routing, devices and fader were never changed by the freeze. The take's file stays on "
		"disk (not a delete), and one control.undo puts the same take back.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
	}, {QStringLiteral("channel")});
	cmd.resultSchema = stateSchema();
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) { return unfreezeBus(args); };
	registry.registerCommand(cmd);
}

} // namespace

void registerBusFreezeCommands(ControlRegistry& registry)
{
	registerFreezeBus(registry);
	registerUnfreezeBus(registry);
}

} // namespace lmms
