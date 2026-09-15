/*
 * ProjectKey.h - the project's DETECTED KEY: one value on the Song, written
 *                into the project file as ONE `<detected-key>` element.
 *
 * WHY THIS EXISTS AND WHY IT IS HERE. Feature row 34 (transient / BPM / key
 * detection on import) has to put its answer somewhere the project keeps. The
 * tempo half has a home already - the tempo map (include/TempoMap.h) - and the
 * key half has NONE in this tree: the piano roll's key and scale are WINDOW
 * state (src/gui/editors/PianoRoll.cpp writes them as attributes of the piano
 * roll's own element through MainWindow::saveWidgetState), which a headless
 * build has no object for and a command cannot reach. So the detected key gets
 * the treatment every other piece of project-wide non-track state in this fork
 * gets (the groove pool, the modulation layer, the tempo map): a plain value on
 * the Song, written only when it holds something, so a project that never ran a
 * detection re-saves byte for byte as before.
 *
 * THE VOCABULARY IS NOT INVENTED HERE. `scale` is a name the PRE-EXISTING
 * scale/key vocabulary already answers to:
 * InstrumentFunctionNoteStacking::ChordTable::getScaleByName() in
 * include/InstrumentFunctions.h - the same names the piano roll's scale combo
 * is filled from. The command that writes this field resolves the detected
 * template mask against that table and REFUSES rather than writing a name the
 * table does not know (ControlCommandsDetect.cpp). `tonic` is the pitch class's
 * own name ("A", "C#", ...) from the same twelve-name spelling the piano roll
 * uses for its key column.
 *
 * WHAT IT IS NOT: it is not a tuning (Song's microtuner Scale list is), it does
 * not transpose anything, and it does not move the piano roll's own key/scale
 * combo - the entry in docs/KNOWN-LIMITATIONS.md states that absence in one
 * line.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef LMMS_PROJECT_KEY_H
#define LMMS_PROJECT_KEY_H

#include <QString>

class QDomDocument;
class QDomElement;

namespace lmms
{

/*! The key a detection found, as the project keeps it.
 *
 *  Every field is optional in the file and the whole element is absent until a
 *  detection has been APPLIED: an empty ProjectKey writes nothing, which is what
 *  keeps every project saved before this feature byte-identical.
 */
class ProjectKey
{
public:
	//! The value the wire and the file use for "no pitch class named".
	static constexpr int UnknownPitchClass = -1;

	//! The element name inside `<song>`; also the name loadSettings looks for.
	static constexpr const char* ElementName = "detected-key";

	bool empty() const { return m_scaleName.isEmpty(); }
	//! True when the project file needs a `<detected-key>` element.
	bool shouldPersist() const { return !empty(); }

	//! Forgets the detected key. Called by Song::clearProject, so a new project
	//! never inherits the previous one's key.
	void clear();

	const QString& tonicName() const { return m_tonicName; }
	int tonicPitchClass() const { return m_tonicPitchClass; }
	//! A name InstrumentFunctionNoteStacking::ChordTable::getScaleByName()
	//! answers to. Empty means there is no detected key.
	const QString& scaleName() const { return m_scaleName; }
	//! The DETECTOR's own score for the winning template - a rank margin in the
	//! score's units, not a probability (see ImportDetectionDsp.h).
	double confidence() const { return m_confidence; }
	double margin() const { return m_margin; }
	//! The method that produced it ("chroma-tonic-weighted-template-correlation"), so a
	//! project read years later can say how the key was guessed.
	const QString& method() const { return m_method; }
	//! The file the detection read. Absolute when the caller named an absolute
	//! path, which is what the command surface requires.
	const QString& sourcePath() const { return m_sourcePath; }

	void set(const QString& tonicName, int tonicPitchClass, const QString& scaleName,
		double confidence, double margin, const QString& method, const QString& sourcePath);

	//! Write the `<detected-key>` child of \a parent. The CALLER decides whether
	//! to call this; shouldPersist() is the rule.
	void saveSettings(QDomDocument& doc, QDomElement& parent) const;
	//! Read a `<detected-key>` element back. An absent or unreadable element
	//! leaves the key EMPTY - the reset-on-absence rule the groove pool and the
	//! punch region also follow, and what makes a journal checkpoint of the
	//! pre-first-detection state restorable.
	bool loadSettings(const QDomElement& element);

	//! The element as its own document's text, for the recorded ACTION
	//! checkpoint the detect.* commands take (the shape GroovePool::toXml uses).
	QString toXml() const;
	//! Restores the text toXml() produced. False when it does not parse.
	bool fromXml(const QString& xml);

private:
	QString m_tonicName;
	int m_tonicPitchClass = UnknownPitchClass;
	QString m_scaleName;
	double m_confidence = 0.0;
	double m_margin = 0.0;
	QString m_method;
	QString m_sourcePath;
};

} // namespace lmms

#endif // LMMS_PROJECT_KEY_H
