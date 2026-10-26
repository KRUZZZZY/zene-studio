/*
 * LaunchBindings.cpp - the controller's bindings: launcher actions and built-in commands
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
#include "LaunchBindings.h"

#include <cmath>
#include <memory>

#include <QJsonDocument>

#include "ConfigManager.h"
#include "ControlReversibility.h"
#include "ControlRegistry.h"
#include "MidiEvent.h"
#include "SessionModel.h"
#include "Song.h"

namespace lmms
{

namespace
{

std::unique_ptr<LaunchBindings> s_bindings;
const QString kSection = QStringLiteral("launcher");
const QString kKey = QStringLiteral("bindings");
//! Q050-217.1: the command bindings' OWN section. The launcher's store keeps its format and meaning.
const QString kCommandSection = QStringLiteral("midi");
const QString kCommandKey = QStringLiteral("commandbindings");

//! A stored binding's channel: -1 (any channel) or a MIDI channel 0..15. False for anything else (a fraction, a word,
//! -2), which is no channel at all - the binding is skipped rather than read as any channel. A missing channel is invalid.
bool storedChannel(const QJsonObject& binding, int* out)
{
	const QJsonValue value = binding.value(QStringLiteral("channel"));
	if (!value.isDouble()) { return false; }
	const double channel = value.toDouble();
	if (channel != std::floor(channel) || channel < -1 || channel > 15) { return false; }
	*out = static_cast<int>(channel);
	return true;
}

//! How closely the launcher binds @a note: 0 for an exact channel, 1 for any channel, 2 for none.
QJsonObject bindingFor(const LaunchNote& note, int* rank)
{
	QJsonObject any;
	*rank = 2;
	for (const QJsonValue& value : LaunchBindings::bindings())
	{
		const QJsonObject binding = value.toObject();
		if (binding.value(QStringLiteral("key")).toInt(-1) != note.key) { continue; }
		int channel = -1;
		if (!storedChannel(binding, &channel)) { continue; }
		if (channel == note.channel)
		{
			*rank = 0;
			return binding;
		}
		if (channel < 0)
		{
			any = binding;
			*rank = 1;
		}
	}
	return any;
}

//! The launcher binding for @a note: an exact channel first, then one for any channel.
QJsonObject bindingFor(const LaunchNote& note)
{
	int rank = 2;
	return bindingFor(note, &rank);
}

/*! Q050-217.1: whether @a binding is the command binding @a trigger activates, and how closely.
 *  @a rank is 0 for an exact channel and 1 for an any-channel binding, so the closer one wins. */
bool commandMatch(const QJsonObject& binding, const LaunchTrigger& trigger, int* rank)
{
	const bool control = binding.value(QStringLiteral("trigger")).toString() == QLatin1String("cc");
	if (control != (trigger.kind == LaunchTrigger::Kind::Control)) { return false; }
	if (control)
	{
		if (binding.value(QStringLiteral("controller")).toInt(-1) != trigger.controller) { return false; }
	}
	else if (binding.value(QStringLiteral("key")).toInt(-1) != trigger.key)
	{
		return false;
	}
	int channel = -1;
	if (!storedChannel(binding, &channel)) { return false; }
	if (channel == trigger.channel) { *rank = 0; return true; }
	if (channel < 0) { *rank = 1; return true; }
	return false;
}

//! The built-in command @a trigger activates: the closest command binding (an exact channel first), or empty.
QString commandFor(const LaunchTrigger& trigger, int* rank)
{
	QString best;
	int bestRank = 2;
	for (const QJsonValue& value : LaunchBindings::commandBindings())
	{
		const QJsonObject binding = value.toObject();
		int found = 2;
		if (!commandMatch(binding, trigger, &found) || found >= bestRank) { continue; }
		const QString command = binding.value(QStringLiteral("command")).toString();
		// Not checked against the registry here: a binding naming a command this build lacks matches nothing to run,
		// and the activation is a no-op rather than a failed dispatch nobody asked for. The command is validated
		// when the binding is made (session.bind_command).
		if (command.isEmpty()) { continue; }
		best = command;
		bestRank = found;
	}
	*rank = bestRank;
	return best;
}

//! Commands win ties, but a note's exact-channel launcher takes precedence over an any-channel command.
bool noteCommandWins(const LaunchTrigger& trigger, int commandRank)
{
	int launcherRank = 2;
	(void)bindingFor(LaunchNote{trigger.channel, trigger.key, true}, &launcherRank);
	return commandRank <= launcherRank;
}

void release(const QJsonObject& binding)
{
	Song* song = Engine::getSong();
	// A binding without its cell (a hand-edited config) releases nothing - not cell 0, 0.
	const int track = binding.value(QStringLiteral("track")).toInt(-1);
	const int scene = binding.value(QStringLiteral("scene")).toInt(-1);
	const SessionModel& model = song->sessionModel();
	if (track < 0 || scene < 0 || track >= model.trackCount() || scene >= model.sceneCount()) { return; }
	const ClipSlot& slot = model.slot(track, scene);
	song->sessionScheduler().requestRelease(track, scene, slot.launchMode(), slot.launchQuantisation());
}

//! Bind @a note to @a action, in place of whatever the same note was bound to.
void learnNote(const QJsonObject& action, const LaunchNote& note)
{
	QJsonObject binding = action;
	binding.insert(QStringLiteral("key"), note.key);
	binding.insert(QStringLiteral("channel"), note.channel);
	QJsonArray list;
	for (const QJsonValue& value : LaunchBindings::bindings())
	{
		const QJsonObject old = value.toObject();
		const bool same = old.value(QStringLiteral("key")).toInt() == note.key && old.value(QStringLiteral("channel")).toInt(-1) == note.channel;
		if (!same) { list.append(old); }
	}
	list.append(binding);
	// Q050-217.1: one control has one meaning. A note just bound to launch keeps no command binding - on its
	// own channel or on any channel, since either would answer the same press - and that removal is part of
	// THIS edit: one undo step puts both stores back, one redo writes both.
	QJsonArray commands;
	for (const QJsonValue& value : LaunchBindings::commandBindings())
	{
		const QJsonObject old = value.toObject();
		const bool control = old.value(QStringLiteral("trigger")).toString() == QLatin1String("cc");
		const int channel = old.value(QStringLiteral("channel")).toInt(-1);
		const bool same = !control && old.value(QStringLiteral("key")).toInt() == note.key
			&& (channel < 0 || channel == note.channel);
		if (!same) { commands.append(old); }
	}
	const QJsonArray before = LaunchBindings::bindings();
	const QJsonArray commandsBefore = LaunchBindings::commandBindings();
	LaunchBindings::setBindings(list);
	LaunchBindings::setCommandBindings(commands);
	// One undo step, as session.bind's (review L10-L03-L02-1 #13): the binding it made comes off again.
	control::addUndoStep([before, commandsBefore]() { LaunchBindings::setBindings(before); LaunchBindings::setCommandBindings(commandsBefore); },
		[list, commands]() { LaunchBindings::setBindings(list); LaunchBindings::setCommandBindings(commands); });
}

//! A pressed note's action, through the registry.
void launch(const QJsonObject& binding)
{
	const QString action = binding.value(QStringLiteral("action")).toString();
	ControlRegistry* registry = ControlRegistry::instance();
	if (action == QLatin1String("slot"))
	{
		registry->invoke(QStringLiteral("session.launch_slot"), {{QStringLiteral("track"), binding.value(QStringLiteral("track"))},
			{QStringLiteral("scene"), binding.value(QStringLiteral("scene"))}});
	}
	else if (action == QLatin1String("scene"))
	{
		registry->invoke(QStringLiteral("session.launch_scene"), {{QStringLiteral("scene"), binding.value(QStringLiteral("scene"))}});
	}
	else if (action == QLatin1String("stop_all")) { registry->invoke(QStringLiteral("session.stop_all"), {}); }
}

} // namespace

LaunchBindings::~LaunchBindings()
{
	close();
}

LaunchBindings* LaunchBindings::instance() { return s_bindings.get(); }

LaunchBindings* LaunchBindings::connect(const QString& input, const QString& output)
{
	auto bindings = std::make_unique<LaunchBindings>();
	bindings->open(QStringLiteral("Launcher"), input, output);
	s_bindings = std::move(bindings);
	return s_bindings.get();
}

void LaunchBindings::disconnect() { s_bindings.reset(); }

QJsonArray LaunchBindings::bindings()
{
	return QJsonDocument::fromJson(ConfigManager::inst()->value(kSection, kKey).toUtf8()).array();
}

void LaunchBindings::setBindings(const QJsonArray& list)
{
	ConfigManager::inst()->setValue(kSection, kKey, QString::fromUtf8(QJsonDocument(list).toJson(QJsonDocument::Compact)));
}

QJsonArray LaunchBindings::commandBindings()
{
	return QJsonDocument::fromJson(ConfigManager::inst()->value(kCommandSection, kCommandKey).toUtf8()).array();
}

void LaunchBindings::setCommandBindings(const QJsonArray& list)
{
	ConfigManager::inst()->setValue(kCommandSection, kCommandKey, QString::fromUtf8(QJsonDocument(list).toJson(QJsonDocument::Compact)));
}

bool LaunchBindings::decode(const MidiEvent& event, LaunchTrigger* out)
{
	// MIDI client thread: nothing allocated or locked. A note-on at velocity 0 is a release, as every
	// MIDI source spells one; a control-change is an activation only while its value is above zero.
	LaunchTrigger trigger;
	trigger.channel = event.channel();
	switch (event.type())
	{
	case MidiNoteOn:
	case MidiNoteOff:
		trigger.kind = LaunchTrigger::Kind::Note;
		trigger.key = event.key();
		trigger.on = event.type() == MidiNoteOn && event.velocity() > 0;
		break;
	case MidiControlChange:
		trigger.kind = LaunchTrigger::Kind::Control;
		trigger.controller = event.controllerNumber();
		trigger.on = event.controllerValue() > 0;
		break;
	default:
		return false;
	}
	*out = trigger;
	return true;
}

void LaunchBindings::applyTrigger(const LaunchTrigger& trigger)
{
	// UI thread. A release or a zero only re-arms; one activation dispatches at most once.
	const int latch = trigger.channel * 128 + (trigger.kind == LaunchTrigger::Kind::Control ? trigger.controller : trigger.key);
	if (trigger.kind == LaunchTrigger::Kind::Control)
	{
		if (!trigger.on)
		{
			m_controlsHigh.remove(latch);
			return;
		}
		if (m_controlsHigh.contains(latch)) { return; }  // 1..127 of one sweep is one activation
		m_controlsHigh.insert(latch);
		int rank = 2;
		const QString command = commandFor(trigger, &rank);
		if (!command.isEmpty()) { ControlRegistry::instance()->invoke(command, QJsonObject{}); }
		return;
	}
	if (trigger.on)
	{
		// An armed learn takes the next note whatever it was bound to (the launcher's own rule).
		if (dispatchNoteCommand(trigger, latch)) { return; }
	}
	else if (m_commandNotesHeld.remove(latch)) { return; }  // the press ran a command; nothing to release
	applyNote(LaunchNote{trigger.channel, trigger.key, trigger.on});
}

bool LaunchBindings::dispatchNoteCommand(const LaunchTrigger& trigger, int latch)
{
	if (!m_learning.isEmpty()) { return false; }
	int rank = 2;
	const QString command = commandFor(trigger, &rank);
	if (command.isEmpty() || !noteCommandWins(trigger, rank)) { return false; }
	// The command binding takes the note: its release must not release a launcher cell.
	m_commandNotesHeld.insert(latch);
	ControlRegistry::instance()->invoke(command, QJsonObject{});
	return true;
}

void LaunchBindings::applyNote(const LaunchNote& note)
{
	const int held = note.channel * 128 + note.key;
	if (!m_learning.isEmpty() && note.on)
	{
		learnNote(m_learning, note);
		m_learning = {};
		m_learnedNotesAwaitingRelease.insert(held);
		return;
	}
	if (!note.on && m_learnedNotesAwaitingRelease.remove(held)) { return; }
	// A release goes to the binding its press launched, even when the bindings changed while the key was held.
	const QJsonObject binding = !note.on && m_held.contains(held) ? m_held.take(held) : bindingFor(note);
	if (note.on)
	{
		m_held.insert(held, binding);
		launch(binding);
	}
	else if (binding.value(QStringLiteral("action")).toString() == QLatin1String("slot")) { release(binding); }
}

} // namespace lmms
