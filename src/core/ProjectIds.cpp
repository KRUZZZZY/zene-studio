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

#include <QDomNode>

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

} // namespace

int ProjectIds::next()
{
	return s_next;
}

void ProjectIds::observeNext(int next)
{
	if (next > s_next) { s_next = next; }
}

void ProjectIds::observe(int id)
{
	if (id >= s_next) { s_next = id + 1; }
}

int ProjectIds::allocate()
{
	const int id = s_next;
	observe(id);
	return id;
}

void ProjectIds::reset()
{
	s_next = 0;
	s_loadAssignments = 0;
}

int ProjectIds::loadAssignments()
{
	return s_loadAssignments;
}

void ProjectIds::beginLoad()
{
	s_loadAssignments = 0;
}

void ProjectIds::noteLoadAssignment()
{
	++s_loadAssignments;
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

} // namespace lmms
