/*
 * LoopImportWarp.h - an imported loop's tempo and key detected, and followed or kept
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
/*! 0.5.0 L05 (Q050-093): clip.import_file runs the import detector (analyseAudioFile, the one behind
 *  detect.analyze) on every audio file it imports and then takes one of two loop modes:
 *  - **follow_project**: the clip leads at its detected tempo (warp mode `source`, the tempo rounded to a whole BPM
 *    as detect.apply rounds it), so its beats land on the project grid whatever the project tempo, rendered with
 *    the pitch kept; and its tonic is transposed to the project key's tonic (the nearest way, -5..+6 semitones)
 *    through the clip transpose that clip.set_transpose writes. Import-time only: a later key change does not
 *    retune it.
 *  - **keep_unstretched**: the clip plays at its own rate and pitch, exactly as an import always did.
 *  The project's tempo and key are never written. What could not be applied - no tempo or key detected, a project
 *  with no key, a build without Rubber Band for the transpose - is reported in `notes`, never guessed. The default
 *  is the preference `import/loopwarp` (settings.set, or Edit > Auto-Warp Dropped Loops); a per-import `loop_mode`
 *  overrides it. */
#ifndef LMMS_LOOP_IMPORT_WARP_H
#define LMMS_LOOP_IMPORT_WARP_H

#include <QJsonObject>
#include <QStringList>

#include "LoopSource.h"
#include "lmms_export.h"

class QDomDocument;
class QDomElement;

namespace lmms
{

class ProjectKey;
class SampleClip;

namespace loopwarp
{

enum class Mode
{
	KeepUnstretched,
	FollowProject
};

//! `follow_project` / `keep_unstretched`: the wire and config names.
LMMS_EXPORT QString modeName(Mode mode);
//! Reads a wire name; false for any other string.
LMMS_EXPORT bool modeFromName(const QString& name, Mode* mode);

//! The config key of the remembered default, `<class>/<attribute>` as settings.set takes it.
LMMS_EXPORT QString preferenceKey();
//! The remembered default: follow_project when the key says so, keep_unstretched otherwise (and when absent).
LMMS_EXPORT Mode preferredMode();

//! The semitones that move pitch class \a from to \a to the nearest way: -5..+6.
LMMS_EXPORT int semitonesToward(int from, int to);

//! What the detector finds in \a path (control thread: it reads and decodes the file). `analysed` is false, and
//! \a error says why, when the file could not be analysed at all.
LMMS_EXPORT LoopSource analyse(const QString& path, QString* error = nullptr);

/*! Applies \a mode to a freshly imported \a clip from \a source (control thread), records what was applied in
 *  \a source and on the clip, and answers what could not be applied, in plain words. Never writes the project. */
LMMS_EXPORT QStringList apply(SampleClip* clip, LoopSource source, Mode mode, const ProjectKey& projectKey);

//! The detected values and the import's choice, as clip.import_file and clip.source_get_state report them.
LMMS_EXPORT QJsonObject sourceJson(const LoopSource& source);
//! sourceJson plus what the clip plays NOW: its warp tempo mode, source tempo, stretch mode and transpose.
LMMS_EXPORT QJsonObject clipStateJson(const SampleClip& clip);

//! The clip's `<loopsource>` child (SampleClip::saveSettings writes it only for an analysed clip).
LMMS_EXPORT void saveLoopSource(QDomDocument& doc, QDomElement& clipElement, const LoopSource& source);
//! Reads it back; a clip element without one answers the default (not analysed).
LMMS_EXPORT LoopSource loadLoopSource(const QDomElement& clipElement);

} // namespace loopwarp

} // namespace lmms

#endif // LMMS_LOOP_IMPORT_WARP_H
