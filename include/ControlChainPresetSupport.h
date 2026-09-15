/*
 * ControlChainPresetSupport.h - the named chain store behind the chain.*
 *                               command group (SPEC A11-A16): a chain captured
 *                               with every device's own state, kept as one file
 *                               per preset in the user's preset tree so it is
 *                               reuseable across projects.
 *
 * WHY A PRIVATE STORE AND NOT A PROJECT ELEMENT. A preset whose whole point is
 * "apply this to another track in another project" cannot live in the project
 * that captured it: docs/KNOWN-LIMITATIONS.md states the finding as the shipped limitation, and the brief for
 * OWNER-31 item 2 states it as the requirement ("a store that survives
 * project.save/project.open AND is usable across projects"). The store is
 * therefore the product's own user preset root - the same directory tree the
 * browser shows as "My Presets" and plugin.preset_save writes into - under a
 * chainpresets/ subdirectory, one .zcp document per preset. It survives a
 * project round trip because it is not in the project, which is the stronger
 * property: it survives project.open of a DIFFERENT project too.
 *
 * WHAT A DEVICE'S STATE IS. Not a second serialiser: the device document
 * plugin.state_save writes (controlDeviceStateBytes -> controlEffectStateXml, a
 * <zenepluginstate> document) is embedded verbatim, and applying it is
 * plugin.state_load's own restore (controlRestoreDeviceState ->
 * controlRestoreEffectState, which refuses a document written for a different
 * device). The chain's ORDER is the file's device order.
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

#ifndef LMMS_CONTROL_CHAIN_PRESET_SUPPORT_H
#define LMMS_CONTROL_CHAIN_PRESET_SUPPORT_H

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "ControlDeviceSupport.h" // ControlDeviceEntry, ControlResult
#include "lmms_export.h"

namespace lmms
{

class EffectChain;

//! One device of a stored chain preset: the device's own state document plus
//! the identity parsed out of that document.
//!
//! The identity is NOT a second copy of the state: it is read from the
//! <zenepluginstate> root the document itself carries (its plugin, hosted_file,
//! hosted_id and hosted_uri attributes - what controlRestoreEffectState already
//! matches on), so a preset cannot describe one device and carry another one's
//! settings.
struct ControlChainPresetDevice
{
	QByteArray state;  //!< the <zenepluginstate> document, verbatim
	QString plugin;    //!< descriptor name, LADSPA label or LV2 URI
	QString format;    //!< "builtin" | "ladspa" | "lv2"
	QString file;      //!< hosted_file, empty for a built-in device
	QString label;     //!< hosted_id (the LADSPA label), empty otherwise
	QString uri;       //!< hosted_uri (the LV2 URI), empty otherwise
};

//! One stored chain preset: its name, its file and its devices in order.
struct ControlChainPreset
{
	QString name;
	QString path;
	QList<ControlChainPresetDevice> devices;
};

// ---------------------------------------------------------------------------
// The store
// ---------------------------------------------------------------------------

/*! The directory the presets live in: the product's user preset root plus
 *  "chainpresets/", with the trailing slash included (the convention
 *  controlUserPresetDir already uses). Outside the project on purpose - see the
 *  file header.
 */
LMMS_EXPORT QString controlChainPresetDir();

/*! The absolute path of \\a name, with ".zcp" appended when it is missing.
 *
 *  False and *error set (typed invalid_args) for a name that is empty or would
 *  escape the directory - the same rule controlSafePresetName enforces for
 *  plugin.preset_save, reused rather than restated.
 */
LMMS_EXPORT bool controlChainPresetPath(const QString& name, QString* path, ControlResult* error);

//! The preset name a file carries (its complete base name).
LMMS_EXPORT QString controlChainPresetName(const QString& path);

//! Every stored preset file, in file-name order (deterministic for the store).
LMMS_EXPORT QStringList controlChainPresetFiles();

/*! The preset file \\a path holds. False and *error set when the file cannot be
 *  read, is not XML, is not a <zenechainpreset> document, or carries a device
 *  with no state document - a half-readable preset is refused rather than
 *  applied with a device silently missing its settings.
 */
LMMS_EXPORT bool controlReadChainPreset(const QString& path, ControlChainPreset* out,
	ControlResult* error);

//! The document a captured chain is written as.
LMMS_EXPORT QByteArray controlChainPresetDocument(const QString& name,
	const QList<ControlChainPresetDevice>& devices);

/*! Fills \\a device's identity fields (plugin, format, file, label, uri) out of
 *  its OWN state document, rather than out of a second description of the
 *  device that could disagree with it. False and *error set (typed invalid_args)
 *  when the bytes are not a <zenepluginstate> document.
 *
 *  This is the one place the identity is derived, so the capture (which starts
 *  from a live device) and the read (which starts from a file) cannot produce
 *  two different identities for one device.
 */
LMMS_EXPORT bool controlChainPresetIdentity(ControlChainPresetDevice* device, ControlResult* error);

/*! Resolves a stored device to a catalogue entry BY ITS OWN IDENTITY.
 *
 *  Not by dev-<n>: that id is a catalogue INDEX ("deterministic for one build",
 *  plugin.list's own words), so a preset captured on one build would name a
 *  different device on the next. The identity in the document (LV2 URI, LADSPA
 *  file+label, or the built-in descriptor name) is what survives.
 */
LMMS_EXPORT bool controlChainPresetEntry(const ControlChainPresetDevice& device,
	ControlDeviceEntry* entry, int* index, ControlResult* error);

// ---------------------------------------------------------------------------
// The chain
// ---------------------------------------------------------------------------

/*! \\a chain's own <fxchain> XML: EffectChain::saveSettings, the same call the
 *  project file's element is written by. Empty when the XML is larger than the
 *  bounded snapshot size (SPEC A16's record is BOUNDED, so an oversized one is
 *  reported by size rather than truncated into a corrupt snapshot).
 */
LMMS_EXPORT QString controlChainXml(EffectChain* chain);

//! Restores \\a chain from XML captured by controlChainXml, through
//! EffectChain::loadSettings - the project loader's own path.
LMMS_EXPORT bool controlRestoreChainXml(EffectChain* chain, const QString& xml);

/*! Replaces \\a chain with \\a preset: every device in the preset's order, each
 *  restored from its own state document.
 *
 *  Every device is resolved to a LOADABLE catalogue entry BEFORE the first
 *  write, so a preset this build cannot load refuses and leaves the chain the
 *  caller already had untouched (the refusal-before-write rule every mutating
 *  handler in this surface follows). The existing devices are dropped the way
 *  plugin.unload drops one (removeEffect + deleteLater, so the audio thread is
 *  parked by EffectChain's own model change).
 */
LMMS_EXPORT ControlResult controlApplyChainPreset(EffectChain* chain,
	const ControlChainPreset& preset);

// ---------------------------------------------------------------------------
// The wire
// ---------------------------------------------------------------------------

//! One stored device as chain.get_state reports it: its order, its identity and
//! the state document's size and hash - never the document itself.
LMMS_EXPORT QJsonObject controlChainPresetDeviceJson(const ControlChainPresetDevice& device,
	int index);

//! A stored preset: name, path, device count and the device summaries (plus the
//! file size and hash when \\a detailed).
LMMS_EXPORT QJsonObject controlChainPresetJson(const ControlChainPreset& preset, bool detailed);

} // namespace lmms

#endif // LMMS_CONTROL_CHAIN_PRESET_SUPPORT_H
