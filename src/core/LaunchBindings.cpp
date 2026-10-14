/*
 * LaunchBindings.cpp - launcher cells and scenes launched from a MIDI controller's notes
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

//! The binding for @a note: an exact channel first, then one for any channel.
QJsonObject bindingFor(const LaunchNote& note)
{
	QJsonObject any;
	for (const QJsonValue& value : LaunchBindings::bindings())
	{
		const QJsonObject binding = value.toObject();
		if (binding.value(QStringLiteral("key")).toInt(-1) != note.key) { continue; }
		const int channel = binding.value(QStringLiteral("channel")).toInt(-1);
		if (channel == note.channel) { return binding; }
		if (channel < 0) { any = binding; }
	}
	return any;
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
	// One undo step, as session.bind's (review L10-L03-L02-1 #13): the binding it made comes off again.
	const QJsonArray before = LaunchBindings::bindings();
	LaunchBindings::setBindings(list);
	control::addUndoStep([before]() { LaunchBindings::setBindings(before); }, [list]() { LaunchBindings::setBindings(list); });
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

bool LaunchBindings::decode(const MidiEvent& event, LaunchNote* out)
{
	// MIDI client thread: nothing allocated or locked. A note-on at velocity 0 is a release.
	if (event.type() != MidiNoteOn && event.type() != MidiNoteOff) { return false; }
	*out = LaunchNote{event.channel(), event.key(), event.type() == MidiNoteOn && event.velocity() > 0};
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
