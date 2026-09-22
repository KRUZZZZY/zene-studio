/*
 * ProjectIds.cpp - the project-scoped monotonic id counter.
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

#include "ProjectIds.h"

#include <QCryptographicHash>
#include <QDomElement>
#include <QDomNode>
#include <QStringList>
#include <QUuid>

namespace lmms
{

namespace
{

// The one piece of state. begin = 0 so the first object of a new document is
// <prefix>-0, which is what the index-derived surface already reported for the
// first object and therefore the smallest surprise for a legacy file's first
// save (the spec's example shows a first track with id 3; the value is arbitrary
// as long as it is creation-assigned and monotonic).
int s_next = 0;
int s_loadAssignments = 0;

/*! The highest number this session has handed out to a surviving object or read
 *  from a document, +1 - the floor restoreFromDocument() may never go below.
 *
 *  The distinction that makes the round trip byte-identical: a walk's
 *  placeholders (allocate() while a load pass is open) do NOT count, because the
 *  element's own id overwrites them; the ids the document carries (observe(),
 *  from setUp) and the ones an id-less element KEEPS (noteLoadAssignment())
 *  do. Without it, a max-merge kept the walk's placeholders in the counter
 *  forever and every save/load/save round trip wrote a larger `next-id` than
 *  the file it came from.
 */
int s_documentFloor = 0;

//! True between beginLoad() and endLoad(): allocate() is handing out placeholders.
bool s_inLoad = false;

/*! The containers a COPY payload puts an object element in, i.e. everything
 *  `ProjectIds::isDocumentElement` must answer false for. EVERY entry is the
 *  name of an element a writer in this tree produces, measured - never a guess
 *  that a name "sounds like" a clipboard (the guard this replaces tested
 *  `clipboard`/`dnddata` while the clip drag payload's actual parent is
 *  `<clip>`, so it wrote the id into every copy payload it meant to protect).
 *
 *    clonedtrack   Track::clone() - the temporary <clonedtrack> document the
 *                  duplicate-track path re-loads as a NEW track.
 *    clip          ClipView::createClipDataFiles() - the wrapper each clip of a
 *                  drag/copy payload is written into (the paste path reads the
 *                  clip element back out of it: TrackContentWidget::pasteSelection).
 *    note-list     PianoRoll::copyToClipboard() - the wrapper the notes of the
 *                  piano roll's clipboard are written into.
 *    dnddata       a DataFile root of type DragNDropData: the track drag payload
 *                  TrackGrip writes (TrackContainerView drops it with
 *                  Track::create) and the clip drag payload.
 *    clipboard-data  a DataFile root of type ClipboardData, the piano roll's own
 *                  note clipboard.
 *    zenepluginstate  the device-state document controlEffectStateXml() writes
 *                  and controlRestoreEffectState() reads (ControlDeviceSupport,
 *                  ControlChainPresetSupport): one effect's state, restored into
 *                  an effect that is already alive and has an id of its own.
 *    instrumenttracksettings  a DataFile root of type InstrumentTrackSettings:
 *                  the instrument preset document Track::savePreset/loadPreset
 *                  and plugin.state_* use.
 *
 *  A project file is <song>, a journal checkpoint is <journaldata>, a track's
 *  clips sit under <track> and a clip's notes under the clip element - none of
 *  those is listed here, which is what makes the distinction the one the
 *  contract needs.
 */
bool isCopyContainer(const QString& name)
{
	return name == QLatin1String("clonedtrack")
		|| name == QLatin1String("clip")
		|| name == QLatin1String("note-list")
		|| name == QLatin1String("dnddata")
		|| name == QLatin1String("clipboard-data")
		|| name == QLatin1String("zenepluginstate")
		|| name == QLatin1String("instrumenttracksettings");
}

/*! The content fingerprint behind the revision pair: a hex digest of the
 *  element's own attributes (sorted, so Qt's attribute order cannot fake a
 *  change; `rev`/`writer` excluded, because they ARE the revision) plus its
 *  ELEMENT children in document order (whitespace text nodes skipped - the
 *  file's indentation is not content).
 *
 *  Two children are excluded for one reason: they are bookkeeping this writer
 *  itself adds or removes AFTER the fingerprint is taken, so including them
 *  would make the fingerprint differ on every single save:
 *    - metadata="1" elements: DataFile::write's cleanMetaNodes() deletes them
 *      from the file (a <takelanes> the model holds would be in memory and not
 *      on disk), so the LOADED element never has what the BUILT element has;
 *    - <journallingObject>: the undo machinery's own node, stripped from a
 *      comparison by ClipSerialisationTest for the same reason.
 *
 *  Every other byte the writer emits reaches this function, so "fingerprint
 *  changed" means "this save writes different bytes for this object" - the
 *  stated form of what a merge would otherwise rebuild from an XML diff.
 */
QString contentSignature(const QDomElement& element)
{
	QStringList attributes;
	const QDomNamedNodeMap map = element.attributes();
	for (int i = 0; i < map.length(); ++i)
	{
		const QDomNode attribute = map.item(i);
		const QString name = attribute.nodeName();
		if (name == QLatin1String("rev") || name == QLatin1String("writer"))
		{
			continue;
		}
		attributes.append(name + QLatin1Char('=') + attribute.nodeValue());
	}
	attributes.sort();

	QString structural = attributes.join(QLatin1Char('\n'));
	for (QDomNode child = element.firstChild(); !child.isNull(); child = child.nextSibling())
	{
		if (!child.isElement()) { continue; }
		const QDomElement childElement = child.toElement();
		if (childElement.attribute(QStringLiteral("metadata")).toInt()) { continue; }
		if (childElement.nodeName() == QLatin1String("journallingObject")) { continue; }
		structural.append(QLatin1Char('\n')).append(childElement.nodeName())
			.append(QLatin1Char('{')).append(contentSignature(childElement))
			.append(QLatin1Char('}'));
	}
	return QString::fromLatin1(
		QCryptographicHash::hash(structural.toUtf8(), QCryptographicHash::Sha256).toHex());
}

//! True when this save has stamped a `rev` (see ProjectIds::revisionWritten()).
bool s_revisionsWritten = false;

} // namespace

int ProjectIds::next()
{
	return s_next;
}

void ProjectIds::observeNext(int next)
{
	if (next > s_next) { s_next = next; }
	if (s_next > s_documentFloor) { s_documentFloor = s_next; }
}

void ProjectIds::restoreFromDocument(int next)
{
	// The document's own counter, floored by what that document carries or kept:
	// never below the floor, so an id in the file (or a placeholder an id-less
	// element kept) can never be handed out again.
	s_next = next > s_documentFloor ? next : s_documentFloor;
}

void ProjectIds::observe(int id)
{
	// A document's id, read back by setId: it names a live object, so it is a
	// floor the counter may not sink below - but the floor it raises is THIS
	// id's successor, never the counter's current value. During a load pass
	// the counter sits ABOVE the ids the document carries (every constructor
	// before this one took a placeholder), so flooring at s_next recorded
	// those placeholders as handed out and restoreFromDocument() could not
	// put the document's own counter back: a legacy load whose FIRST loaded
	// object was a mixer channel carrying id 1 (Mixer::loadSettings ->
	// MixerChannel::setId) left the counter at 5 for a three-track project
	// and the second save wrote next-id="5" where the first wrote "3"
	// (StableTrackIdsTest::legacyProjectGetsDeterministicIdsAndResavesByteIdentically).
	if (id >= s_next) { s_next = id + 1; }
	if (id + 1 > s_documentFloor) { s_documentFloor = id + 1; }
}

int ProjectIds::allocate()
{
	const int id = s_next;
	s_next = id + 1;
	// A load pass's allocations are PLACEHOLDERS: the element's own id overwrites
	// them a moment later, and one that is KEPT reports itself through
	// noteLoadAssignment(). Counting them here is what made every round trip
	// write a larger next-id than the file it was loaded from.
	if (!s_inLoad && s_next > s_documentFloor) { s_documentFloor = s_next; }
	return id;
}

void ProjectIds::reset()
{
	s_next = 0;
	s_documentFloor = 0;
	s_loadAssignments = 0;
	s_inLoad = false;
}

int ProjectIds::loadAssignments()
{
	return s_loadAssignments;
}

void ProjectIds::beginLoad()
{
	s_loadAssignments = 0;
	s_inLoad = true;
}

void ProjectIds::endLoad()
{
	s_inLoad = false;
}

void ProjectIds::noteLoadAssignment(int id)
{
	++s_loadAssignments;
	// The object KEEPS this id, so it is live from now on.
	if (id + 1 > s_documentFloor) { s_documentFloor = id + 1; }
	if (id >= s_next) { s_next = id + 1; }
}

bool ProjectIds::isDocumentElement(const QDomNode& node)
{
	// Walk to the document node: the ancestors are the only thing that tells a
	// copy payload from the document, because both use the SAME element names for
	// the object itself (<midiclip>, <note>, <effect ...>) - the payload differs
	// only in what wraps it.
	for (QDomNode ancestor = node.parentNode(); !ancestor.isNull();
			ancestor = ancestor.parentNode())
	{
		if (!ancestor.isElement()) { continue; }
		if (isCopyContainer(ancestor.toElement().nodeName())) { return false; }
	}
	return true;
}

const char* ProjectIds::familyPrefix(IdFamily family)
{
	// Order mirrors the enum; the grammar itself is control::idToIndex's and
	// the test binds the two together, so a prefix here can never drift from
	// the formatter the surface spells it with.
	static const char* const prefixes[] = {
		"trk-", "clip-", "lane-", "note-", "scene-", "warp-", "ch-", "fx-"};
	return prefixes[static_cast<int>(family)];
}

QString ProjectIds::writerInstance()
{
	// Function-local static: one id per process, generated on first use.
	static const QString instance =
		QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
	return instance;
}

void ProjectIds::beginSave()
{
	s_revisionsWritten = false;
}

bool ProjectIds::revisionWritten()
{
	return s_revisionsWritten;
}

void ProjectIds::readRevision(const QDomElement& element, int& rev,
	QString& writer, QString& contentHash)
{
	// Absent, unparseable or non-positive: 0. "Neither is needed to load a
	// file" is exactly this line - a pre-S3 file loads with rev 0 / no writer
	// and behaves as it always did.
	bool ok = false;
	const int stored = element.attribute(QStringLiteral("rev")).toInt(&ok);
	rev = (ok && stored > 0) ? stored : 0;
	writer = element.attribute(QStringLiteral("writer"));
	contentHash = contentSignature(element);
}

void ProjectIds::writeRevision(QDomElement& element, int& rev,
	QString& writer, QString& contentHash)
{
	const QString now = contentSignature(element);
	if (contentHash.isEmpty())
	{
		// Never loaded and never saved before: this save defines the baseline.
		// A brand-new object has made no revisions, so nothing is written.
		contentHash = now;
	}
	else if (now != contentHash)
	{
		// This writer is about to emit different bytes for the object than the
		// ones it loaded: that is a revision.
		contentHash = now;
		++rev;
		writer = writerInstance();
	}
	if (rev > 0)
	{
		element.setAttribute(QStringLiteral("rev"), rev);
		if (!writer.isEmpty())
		{
			element.setAttribute(QStringLiteral("writer"), writer);
		}
		s_revisionsWritten = true;
	}
}

} // namespace lmms
