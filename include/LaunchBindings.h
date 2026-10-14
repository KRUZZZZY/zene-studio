/*
 * LaunchBindings.h - launcher cells and scenes launched from a MIDI controller's notes
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

/*! 0.5.0 L02 (WF-069): launch the launcher from a controller. A note (key, on a channel or any) is
 *  bound to an action - launch a cell (track, scene), launch a scene, or stop everything - and
 *  pressing it runs session.launch_slot / session.launch_scene / session.stop_all through the
 *  registry; releasing a cell's note releases it (a Gate or Repeat cell stops or stops repeating).
 *  learn() binds the next note played to an action. The bindings are kept in the config file
 *  (launcher/bindings, a JSON array) - the user's controller, not the project. The keyboard's way
 *  in is the launcher's own shortcuts (Launch Scene 1-8, Stop All). */
class LMMS_EXPORT LaunchBindings : public QObject, public MidiSurfaceInput<LaunchNote>
{
	Q_OBJECT
public:
	LaunchBindings() = default;
	~LaunchBindings() override;

	static LaunchBindings* instance();
	static LaunchBindings* connect(const QString& input, const QString& output);
	static void disconnect();

	//! The stored bindings: [{key, channel (-1 any), action: slot|scene|stop_all, track, scene}].
	static QJsonArray bindings();
	static void setBindings(const QJsonArray& bindings);
	//! The next note played binds to @a action (an object without key / channel).
	void learn(const QJsonObject& action) { m_learning = action; }
	const QJsonObject& learning() const { return m_learning; }

	//! What a note does (the UI thread's half; the test drives it directly).
	void applyNote(const LaunchNote& note);

protected:
	bool decode(const MidiEvent& event, LaunchNote* out) override;
	void apply(const LaunchNote& note) override { applyNote(note); }

private:
	QJsonObject m_learning;
	QHash<int, QJsonObject> m_held;  //!< channel * 128 + key -> the binding its press launched
	QSet<int> m_learnedNotesAwaitingRelease;  //!< channel * 128 + key -> ignore the consumed press's release
};

} // namespace lmms

#endif // LMMS_LAUNCH_BINDINGS_H
