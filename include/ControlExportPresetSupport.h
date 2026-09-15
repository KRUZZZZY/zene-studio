/*
 * ControlExportPresetSupport.h - the saved render/export preset store and the
 *                                settings the NEXT render asks for (SPEC A11-A16).
 *
 * WHAT A RENDER PRESET IS HERE. One named record of the three OutputSettings
 * fields the shipped render CLI can be told: the output sample rate, the bit
 * depth and the stereo mode. It is the item the 0.3.0 feature list carries as
 * "Render / export presets" (row 70). It is deliberately NOT the whole
 * OutputSettings: the CLI has no flag for OutputSettings::bitrate(),
 * compressionLevel() or loudnessReport(), so a preset that carried them would
 * be a promise no render could keep. What it does carry, a render honours.
 *
 * WHERE IT IS STORED, AND WHY NOT IN THE PROJECT. The store is the product's own
 * user preset tree beside the chain presets (ConfigManager::userPresetsDir() +
 * "renderpresets/"), one JSON document per preset. A render preset exists to be
 * reused - the 24/96 master, the 16-bit distribution render - and "reused" means
 * the next project too: project state would make it a setting of ONE song, which
 * is what OutputSettings already is. The store therefore survives project.save /
 * project.open by construction (it is not in the document at all), and
 * docs/KNOWN-LIMITATIONS.md says so, so an agent reads it rather than infers it.
 *
 * WHAT apply() MEANS, AND WHY IT IS A PROCESS-WIDE HOLDER. include/
 * ExportRenderSettings.h already answers "what does the next render ask for?"
 * for dither and SRC quality, and its argument is the same here: the values a
 * render must obey are read at two ends of one process. The render the control
 * surface runs is a CHILD process (src/core/ControlCommandsProject.cpp's
 * runCliRender: the shipped `render` action, so the caller's session and its
 * audio engine are untouched), and the only channel into it is the command line.
 * That is the whole design: an applied preset becomes the ARGUMENTS the child is
 * started with (controlExportPresetRenderArgs), so a preset is honoured by the
 * one render path the product ships instead of by a second one that reimplements
 * it. The defaults of the holder are the CLI's own defaults (44100 Hz, 16-bit,
 * joint stereo - src/core/main.cpp:466), so a render that never saw an apply is
 * byte-identical to what it was before this file existed.
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

#ifndef LMMS_CONTROL_EXPORT_PRESET_SUPPORT_H
#define LMMS_CONTROL_EXPORT_PRESET_SUPPORT_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "ControlDeviceSupport.h" // ControlResult, controlSafePresetName
#include "LmmsTypes.h"
#include "OutputSettings.h"
#include "lmms_export.h"

namespace lmms
{

//! One stored render/export preset: a name plus the three OutputSettings fields
//! the render path can actually be told. The defaults are the shipped CLI's own
//! defaults (src/core/main.cpp:466), so a default-constructed value describes
//! exactly what `zene render <project>` produces with no options.
struct ControlExportPreset
{
	QString name;                  //!< the store's key: the document's base file name
	QString path;                  //!< the document it was read from (empty until stored)
	sample_rate_t sampleRate = 44100;
	OutputSettings::BitDepth bitDepth = OutputSettings::BitDepth::Depth16Bit;
	OutputSettings::StereoMode stereoMode = OutputSettings::StereoMode::JointStereo;
};

//! The store's directory (trailing slash included) and its file suffix. Named
//! once, here, so the commands and the test that reads the store back cannot
//! disagree about where a preset lives.
LMMS_EXPORT QString controlExportPresetDir();
LMMS_EXPORT QString controlExportPresetSuffix();

//! The document path for \a name. Refuses a name that would escape the store -
//! the ONE name rule controlSafePresetName already enforces for every preset
//! tree this product has (empty, a separator, "..", a leading dot), reused
//! rather than restated.
LMMS_EXPORT bool controlExportPresetPath(const QString& name, QString* path, ControlResult* error);
//! The store's key for a document path: its base name, without the suffix.
LMMS_EXPORT QString controlExportPresetName(const QString& path);
//! Every stored document, absolute paths, sorted.
LMMS_EXPORT QStringList controlExportPresetFiles();

//! The wire names of the three settings. One name per enum value, and the
//! parsers built from the same lists, so an argument schema, a document field
//! and the CLI flag a preset becomes cannot drift apart.
LMMS_EXPORT const char* controlExportPresetBitDepthName(OutputSettings::BitDepth depth);
LMMS_EXPORT bool controlExportPresetBitDepthFromName(const QString& name,
	OutputSettings::BitDepth* out);
LMMS_EXPORT const char* controlExportPresetStereoModeName(OutputSettings::StereoMode mode);
LMMS_EXPORT bool controlExportPresetStereoModeFromName(const QString& name,
	OutputSettings::StereoMode* out);

//! The sample-rate window the shipped render CLI accepts (src/core/main.cpp's
//! own check, `sr >= 44100 && sr <= 192000`). A preset outside it names a render
//! no build of this product can perform, so the store refuses it at the point
//! it is written rather than letting it fail later as a child-process error.
LMMS_EXPORT sample_rate_t controlExportPresetMinSampleRate();
LMMS_EXPORT sample_rate_t controlExportPresetMaxSampleRate();

//! Checks every field of \a preset, naming the field and the accepted values in
//! the refusal. Empty string means the record can be stored and honoured.
LMMS_EXPORT QString controlExportPresetValidate(const ControlExportPreset& preset);

//! The stored document (JSON) and its reader. The reader refuses rather than
//! guesses: a missing field, an unknown wire name or a sample rate outside the
//! CLI's window is a typed invalid_args naming what it found, never a silently
//! defaulted render.
LMMS_EXPORT QByteArray controlExportPresetDocument(const ControlExportPreset& preset);
LMMS_EXPORT bool controlExportPresetFromBytes(const QByteArray& bytes, const QString& path,
	ControlExportPreset* out, ControlResult* error);

//! One preset as the wire reports it. \\a detailed adds the document's own size
//! and hash (what a caller compares after an undo).
LMMS_EXPORT QJsonObject controlExportPresetJson(const ControlExportPreset& preset, bool detailed);

/*! The ARGUMENTS the render child is started with to honour \\a settings: the
 *  `-s <rate>` the render path already passed, plus `--bit-depth <16|24|32>`
 *  and `-m <s|j|m>` when they differ from the CLI's own defaults. Returned as
 *  the list so there is ONE definition of how a preset reaches a render, and so
 *  a render that has no preset applied passes exactly the one argument
 *  (`-s 44100`) it passed before this file existed.
 */
LMMS_EXPORT QStringList controlExportPresetRenderArgs(const ControlExportPreset& settings);

/*! The render settings the NEXT render asks for: the applied preset, or the
 *  CLI's own defaults when none has been applied.
 *
 *  Ownership, stated so it cannot be read two ways:
 *   - it is PROCESS-WIDE, not project state: it is not saved, not journalled
 *     and not carried by project.save/project.open, exactly like
 *     ExportRenderSettings' dither and SRC choices beside it;
 *   - it holds the CURRENT selection only, and `export.preset_apply` is the one
 *     command that writes it (with the recorded undo step that puts the
 *     previous selection back);
 *   - it is read from the control surface's own thread and from no other: the
 *     handlers run on the UI thread, and the render itself is a child process.
 */
class LMMS_EXPORT ControlExportPresetSettings
{
public:
	//! The applied preset; false when none is applied (the defaults are in
	//! force, and \a out is left untouched).
	static bool active(ControlExportPreset* out);
	//! The name of the applied preset, empty when none is applied.
	static QString activeName();
	//! The settings the next render will be started with: the applied preset, or
	//! the defaults. Always answers, so a caller needs no branch.
	static ControlExportPreset effective();

	static void setActive(const ControlExportPreset& preset);
	static void clear();

private:
	static bool s_active;
	static ControlExportPreset s_preset;
};

} // namespace lmms

#endif // LMMS_CONTROL_EXPORT_PRESET_SUPPORT_H
