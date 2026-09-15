/*
 * ControlScaleShared.h - the vocabulary the `scale.*` group's read half and its
 *                        edit half share (board task #648, feature-list row 66).
 *
 * The scale and key vocabulary is the ENGINE's (InstrumentFunctionNoteStacking::
 * ChordTable, include/InstrumentFunctions.h:137-185); this header owns only what
 * the group adds on top of it: the one place that resolves a root and a scale name
 * into pitch classes, the context the two writers set, and the wire forms of both.
 * Two translation units use it (ControlCommandsScale.cpp and
 * ControlCommandsScaleEdit.cpp) - the read/edit split every recent group in this
 * fork follows, because Gate 7 measures a FILE and the group is one group.
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

#ifndef LMMS_CONTROL_SCALE_SHARED_H
#define LMMS_CONTROL_SCALE_SHARED_H

#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace lmms
{

class MidiClip;
struct ControlResult;

namespace control
{

//! The twelve key names, in the plain-sharp spelling the wire uses. The piano
//! roll's own combo spells them "C# / Db" (src/gui/editors/PianoRoll.cpp:129-133);
//! this group publishes one spelling per key and ACCEPTS several - see readRoot().
const QStringList& scaleRootNames();

/*! The scale context this group resolves against: a root and a scale NAME.
 *
 *  Process state, deliberately NOT serialized - the piano roll's own key/scale
 *  selection belongs to its window (PianoRollWindow::saveSettings) and a headless
 *  instance has no such window. The precedent is MpeExpression::isEnabled()
 *  (include/MpeExpression.h:88-93, "Deliberately NOT serialized"), and the rule is
 *  the same one: a project never changes meaning because of a control-surface
 *  setting. `scale` empty means "no scale set yet", which is what a caller sees
 *  before it calls scale.set - not an error, and not a default scale either, since
 *  inventing one would make a snap move notes nobody chose a scale for.
 */
struct ScaleContext
{
	int root = 0;
	QString scale;
};

ScaleContext& scaleContext();

//! The semitone offsets a scale holds. \a scale must be a name ChordTable knows;
//! an unknown or empty name yields an empty vector.
std::vector<int> scaleDegreesOf(const QString& scale);

//! The pitch classes a root + degree set covers, sorted and deduplicated.
std::vector<int> scalePitchClasses(int root, const std::vector<int>& degrees);

//! The scale as a twelve-character membership mask, index 0 = C ("100000000000"
//! is C alone): one string a caller can read at a glance and compare cheaply.
QString scaleMask(const std::vector<int>& classes);

QJsonArray scaleClassesJson(const std::vector<int>& classes);
QJsonArray scaleDegreesJson(const std::vector<int>& degrees);

/*! Reads a root argument: either a key index in 0..11 or a name.
 *
 *  Accepted spellings: "C", "c", "C#", "C#3" (an octave is ignored - a root is a
 *  pitch CLASS here), "Db", "D♭", and the piano roll's "C# / Db". Anything else is
 *  refused typed rather than defaulted to C, because a typo that silently snapped a
 *  clip to C major is a result a caller cannot tell from the one it asked for.
 *  Absent leaves \a out untouched.
 */
bool readScaleRoot(const QJsonObject& args, const QString& key, int* out, ControlResult* error);

/*! Reads a scale NAME into \a out. An empty string is allowed and CLEARS the
 *  context; a name ChordTable does not know is refused NotFound rather than
 *  silently resolving to the empty chord ChordTable::getByName returns. */
bool readScaleName(const QJsonObject& args, QString* out, ControlResult* error);

//! The context block every read returns, so "what this group resolves against" and
//! "what the caller asked for" cannot be two different stories.
QJsonObject scaleContextState();

//! A root + scale a verb was asked about, resolved into pitch classes.
struct ResolvedScale
{
	int root = 0;
	QString scale;
	std::vector<int> degrees;
	std::vector<int> classes;
	//! True when the call NAMED a root or a scale (so a result never looks like the
	//! context when the arguments were what answered it).
	bool fromArguments = false;
};

/*! Resolves the root + scale a verb was asked about: the arguments when named, the
 *  group's context otherwise. */
bool resolveScale(const QJsonObject& args, ResolvedScale* out, ControlResult* error);

//! How many of a clip's notes are in the scale, through the engine's own predicate
//! (NoteTransform::matches with a scale clause).
int countNotesInScale(const MidiClip& clip, const std::vector<int>& classes);

//! The schema property the two writers declare for 'root' (the vocabulary's own
//! `integerProperty()` carries no range, and a root is 0..11).
QJsonObject rootArgumentSchema();

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_SCALE_SHARED_H
