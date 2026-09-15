/*
 * DawProjectRead.cpp - a DAWproject document as the model.
 *
 * The reader half of feature row 37 (docs/FEATURE-LIST-0.3.0.md section 7). It
 * reads a file a FOREIGN program wrote as readily as one this module wrote, so
 * everything it does is a decision about a document it did not produce:
 *
 *   - The version is checked, not assumed. Project.xsd declares version="1.0"
 *     on the root and the README says the format "is version 1.0 and is
 *     stable"; a document declaring another MAJOR version is REFUSED with the
 *     version named. Reading a 2.0 document with 1.0's assumptions is how a
 *     format dialect starts.
 *   - A document with no <Project> root, no <Application>, or a <Tempo> whose
 *     value the engine will not accept is refused. <Application> and the root's
 *     version attribute are `use="required"` in Project.xsd, so their absence
 *     is a malformed document rather than a default.
 *   - A timeline that declares seconds AND carries notes is refused. LMMS
 *     places notes on a beat grid; converting a seconds timeline needs the
 *     tempo curve in force, and a silent misplacement is worse than a named
 *     refusal. (Audio timelines are seconds in the format's own example and are
 *     skipped whole: this module carries notes and not audio - LOSSY #1.)
 *   - Track elements NEST in the format and LMMS' container is FLAT, so a
 *     nested track is read at the container root and the relation is counted as
 *     lost. A document that nests is not refused - its tracks are all there.
 *   - Times are beats, and LMMS' grid is 48 ticks per beat, so a foreign time
 *     that is not on that grid is ROUNDED and counted (`rounded_times`). The
 *     count is reported rather than hidden, the same rule the SMF reader
 *     follows for a foreign division.
 *
 * WHAT IT DOES NOT READ, and says so: <Devices> (plugin and built-in device
 * state), <Channel>/<Sends>, fades, loop points, <Audio>, <Video>, <Markers>,
 * <Scenes> and <ClipSlot>. A document carrying any of them reads fine; the
 * parts this module does not carry are counted in the loss report instead of
 * being silently dropped.
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
#ifndef LMMS_DAWPROJECT_READ_SHARED_H
#define LMMS_DAWPROJECT_READ_SHARED_H

/*!
 * The seam the DAWproject READER is split along, so gate 7's file-length cap
 * does not decide how much of a foreign document this build can survive.
 *
 * src/core/DawProjectReadTracks.cpp holds the walk over <Track>, <Channel>,
 * <Lanes>, <Clips>, <Clip>, <Notes> and <Note> - the elements whose nesting the
 * format leaves free and whose shape a foreign DAW therefore varies. This header
 * carries what both halves need to agree on: the reader's own state (the id to
 * track map the arrangement's IDREFs resolve against, the track a <Clips> chain
 * currently belongs to, and the effective timeUnit), the attribute readers, and
 * the tick conversion that COUNTS a time off LMMS' grid instead of hiding it.
 *
 * It is an internal header (src/core, not include/) because nothing outside the
 * reader uses it, and everything in it is in the named `readdetail` namespace
 * rather than an anonymous one - an anonymous namespace here would give each
 * translation unit its own ReadState and the two halves would silently disagree.
 */

#include <QMap>
#include <QString>
#include <QXmlStreamReader>

#include <cmath>

#include "DawProjectInterchange.h"

namespace lmms
{
namespace interchange
{
namespace readdetail
{


//! A document's own id. `xs:ID` values are opaque to a reader, so the map from
//! id to track is the only thing this reader needs to keep.
using IdMap = QMap<QString, int>;

inline QString attribute(const QXmlStreamAttributes& attributes, const QString& name)
{
	return attributes.value(name).toString();
}

inline bool flagAttribute(const QXmlStreamAttributes& attributes, const QString& name)
{
	const QString value = attribute(attributes, name);
	return value == QLatin1String("true") || value == QLatin1String("1");
}

inline double doubleAttribute(const QXmlStreamAttributes& attributes, const QString& name, double fallback)
{
	const QString value = attribute(attributes, name);
	if (value.isEmpty()) { return fallback; }
	bool ok = false;
	const double parsed = value.toDouble(&ok);
	return ok ? parsed : fallback;
}

inline int intAttribute(const QXmlStreamAttributes& attributes, const QString& name, int fallback)
{
	const QString value = attribute(attributes, name);
	if (value.isEmpty()) { return fallback; }
	bool ok = false;
	const int parsed = value.toInt(&ok);
	return ok ? parsed : fallback;
}

//! Consume the element the reader is standing on, including everything nested
//! inside it, and leave the reader on its end element.
inline void skipElement(QXmlStreamReader& reader)
{
	reader.skipCurrentElement();
}


/*! The reader's own state. A struct rather than a dozen parameters, because
 *  every walk below needs the same four things and the recursion is deep
 *  enough that a missed reference would be a silent bug. */
struct ReadState
{
	DawProjectModel* model = nullptr;
	DawProjectReadReport* report = nullptr;
	QString error;
	//! id -> index in model->tracks, filled from <Structure> before the
	//! arrangement is walked (the arrangement's <Lanes track="..."> IDREFs
	//! point at ids the structure declared).
	IdMap trackIndexById;
	//! The track index a <Lanes>/<Clips> chain currently belongs to.
	int currentTrack = -1;
	//! The effective timeUnit, inherited down the <Lanes> chain.
	QString timeUnit = QString::fromLatin1(DawProjectTimeUnit);
};


//! A time value in beats as LMMS ticks, counting a value that was NOT on
//! LMMS' 48-ticks-per-beat grid (`rounded_times`): the count is reported rather
//! than hidden, the rule the SMF reader follows for a foreign division.
inline qint64 ticksFromBeats(ReadState& state, double beats)
{
	const double exact = beats * DawProjectTicksPerQuarterNote;
	const double rounded = std::floor(exact + 0.5);
	if (std::fabs(exact - rounded) > 1e-6) { state.report->loss.roundedTimes++; }
	return static_cast<qint64>(rounded);
}

//! Walk the <Arrangement>/<Lanes> tree (defined in DawProjectReadTracks.cpp).
void parseLanes(QXmlStreamReader& reader, ReadState& state);
//! One <Track>, at any depth (defined in DawProjectReadTracks.cpp).
void parseTrack(QXmlStreamReader& reader, ReadState& state, int depth);

} // namespace readdetail
} // namespace interchange
} // namespace lmms

#endif // LMMS_DAWPROJECT_READ_SHARED_H
