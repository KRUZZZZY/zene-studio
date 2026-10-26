/*
 * LaunchBindings.h - the controller's bindings: launcher actions and built-in commands
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
#ifndef LMMS_LAUNCH_BINDINGS_H
#define LMMS_LAUNCH_BINDINGS_H

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>

#include "MidiSurfaceInput.h"
#include "lmms_export.h"

namespace lmms
{

//! One note as the MIDI thread hands it over.
struct LaunchNote
{
	int channel = 0;
	int key = 0;
	bool on = true;
};

/*! One activation as the MIDI thread hands it over: a note's press/release, or a control-change
 *  with its value already reduced to "positive" (an activation) or zero (a rearm). Plain numbers and
 *  a bool, because MpscQueue copies it: the MIDI thread allocates nothing. */
struct LaunchTrigger
{
	enum class Kind
	{
		Note,
		Control,
	};

	Kind kind = Kind::Note;
	int channel = 0;
	int key = 0;          //!< the note (Kind::Note)
	int controller = 0;   //!< the control-change number (Kind::Control)
	bool on = true;       //!< a note press, or a control-change value above zero
};

/*! 0.5.0 L02 (WF-069) and Q050-217.1: the controller's bindings.
 *
 *  Launcher bindings (launcher/bindings) bind a note to an action - launch a cell (track, scene),
 *  launch a scene, or stop everything - and pressing it runs session.launch_slot /
 *  session.launch_scene / session.stop_all through the registry; releasing a cell's note releases it
 *  (a Gate or Repeat cell stops or stops repeating). learn() binds the next note played to an action.
 *
 *  Command bindings (midi/commandbindings, Q050-217.1) bind a note OR a control-change to a
 *  built-in command that needs no arguments, and one activation runs it once through the registry:
 *
 *      {trigger:"note", key:60, channel:0, command:"mixer.add_channel"}
 *      {trigger:"cc", controller:74, channel:-1, command:"transport.stop"}
 *
 *  A note fires on its press and rearms on its release; a control-change fires when it goes from 0
 *  to a positive value and rearms at 0, so a knob sweeping 1..127 is ONE activation. The CC value is
 *  not passed on as an argument - a bound command takes none. For notes, the more channel-specific
 *  binding wins; a command wins over a launcher binding at equal specificity. A channel-specific
 *  command leaves a wildcard launcher available on other channels.
 *
 *  Both stores live in the config file (the user's controller, not the project). Both surfaces are
 *  opened by the same connection (session.bindings_connect): one port, one 15 ms UI-thread drain.
 *  MidiSurfaceInput decodes on the MIDI thread (no allocation, no lock) and the UI-thread timer
 *  applies, which is where the registry, the handlers and the project are touched. A harness feeds
 *  processInEvent() and calls drainPending() to apply synchronously. */
class LMMS_EXPORT LaunchBindings : public QObject, public MidiSurfaceInput<LaunchTrigger>
{
	Q_OBJECT
public:
	LaunchBindings() = default;
	~LaunchBindings() override;

	static LaunchBindings* instance();
	static LaunchBindings* connect(const QString& input, const QString& output);
	static void disconnect();

	//! The stored launcher bindings: [{key, channel (-1 any), action: slot|scene|stop_all, track, scene}].
	static QJsonArray bindings();
	static void setBindings(const QJsonArray& bindings);
	//! The next note played binds to @a action (an object without key / channel).
	void learn(const QJsonObject& action) { m_learning = action; }
	const QJsonObject& learning() const { return m_learning; }

	//! The stored command bindings: [{trigger: note|cc, key|controller, channel (-1 any), command}].
	static QJsonArray commandBindings();
	static void setCommandBindings(const QJsonArray& list);

	//! What a note does (the UI thread's half; the tests drive it directly).
	void applyNote(const LaunchNote& note);
	//! What one activation does: the more specific binding; command wins ties, launcher is note-only.
	void applyTrigger(const LaunchTrigger& trigger);

protected:
	bool decode(const MidiEvent& event, LaunchTrigger* out) override;
	void apply(const LaunchTrigger& trigger) override { applyTrigger(trigger); }

private:
	bool dispatchNoteCommand(const LaunchTrigger& trigger, int latch);

	QJsonObject m_learning;
	QHash<int, QJsonObject> m_held;  //!< channel * 128 + key -> the binding its press launched
	QSet<int> m_learnedNotesAwaitingRelease;  //!< channel * 128 + key -> ignore the consumed press's release
	QSet<int> m_controlsHigh;  //!< channel * 128 + controller -> a control-change not yet back at zero
	QSet<int> m_commandNotesHeld;  //!< channel * 128 + key -> its press ran a command binding, so its release does nothing
};

} // namespace lmms

#endif // LMMS_LAUNCH_BINDINGS_H
