/*
 * ControlDeviceSupport.h - shared helpers for the plugin.* / dsp.* and
 *                          settings.* command groups (SPEC A11-A14).
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

#ifndef LMMS_CONTROL_DEVICE_SUPPORT_H
#define LMMS_CONTROL_DEVICE_SUPPORT_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "ControlRegistry.h"
#include "lmms_export.h"
#include "Plugin.h"

namespace lmms
{

class AutomatableModel;
class Effect;
class EffectChain;
class Instrument;
class InstrumentTrack;
class Track;

//! One device of the build's catalogue: a built-in plugin module or a plugin
//! hosted from one of the formats the build includes (LADSPA and LV2 in this
//! tree).
struct ControlDeviceEntry
{
	QString name;        //!< plugin name ("amplifier"), LADSPA label ("amp_mono")
	                     //!< or, for LV2, the plugin URI
	QString displayName; //!< human name ("Amplifier")
	QString format;      //!< "builtin" | "ladspa" | "lv2"
	QString kind;        //!< "effect" | "instrument" | "tool" | "other"
	QString file;        //!< LADSPA file stem, empty for other formats
	QString label;       //!< LADSPA label, empty for other formats
	QString uri;         //!< LV2 plugin URI, empty for other formats
	bool loadable;       //!< plugin.load accepts this entry
};

//! Appends this build's LV2 devices to \a out, through the LV2 host module's
//! own discovery path (the SubPluginFeatures::listSubPluginKeys() dispatch the
//! effect and instrument select dialogs use) - not a second scanner. A no-op
//! when the build has no LV2 host or the process has no LV2 world, so the
//! catalogue is simply built-in + LADSPA there. Called by
//! controlDeviceCatalogue() after the LADSPA block.
LMMS_EXPORT void controlLv2DeviceEntries(QList<ControlDeviceEntry>* out);

//! The build's device catalogue in the stable order the dev-<n> ids use:
//! built-in effects, built-in instruments, LADSPA (sorted by name), then LV2
//! (sorted by URI). The order is deterministic for a binary, which is what
//! makes dev-<n> stable; the hosted formats are appended after the built-ins so
//! a build that gains a host does not renumber the existing dev-<n> ids.
LMMS_EXPORT QList<ControlDeviceEntry> controlDeviceCatalogue();

//! The catalogue entry whose dev-<n> id is \a id; \a index receives n.
LMMS_EXPORT bool controlDeviceById(const QString& id, ControlDeviceEntry* entry, int* index,
	ControlResult* error);

//! JSON for one catalogue entry (the plugin.list element shape).
LMMS_EXPORT QJsonObject controlDeviceJson(const ControlDeviceEntry& entry, int index);

//! A plugin.* target: a Song track or a mixer channel and the effect chain it owns.
struct ControlTarget
{
	QString id;                      //!< "trk-<n>" / "ch-<n>"
	QString kind;                    //!< "track" | "channel"
	QString typeName;                //!< the track's type, or "channel"
	EffectChain* chain = nullptr;    //!< never null on success
	InstrumentTrack* instrumentTrack = nullptr;
};

//! Resolves "trk-<n>" or "ch-<n>"; false and *error set on failure.
LMMS_EXPORT bool resolveControlTarget(const QString& id, ControlTarget* target, ControlResult* error);

//! The effect addressed by \a pluginId ("fx-<n>") inside \a target.
LMMS_EXPORT Effect* resolveControlEffect(const ControlTarget& target, const QString& pluginId,
	ControlResult* error);

//! Named automatable models of \a effect in the engine's own QObject child
//! order. Same rule as Instrument::parameterCount(): only models with a
//! non-empty display name are user-facing parameters. Allocates - control
//! thread only.
LMMS_EXPORT QList<AutomatableModel*> controlEffectParameters(Effect* effect);

//! The same list for an instrument, through Instrument's own parameter API.
LMMS_EXPORT QList<AutomatableModel*> controlInstrumentParameters(Instrument* instrument);

//! One parameter as plugin.param_get reports it.
LMMS_EXPORT QJsonObject controlParameterJson(const AutomatableModel* model, int index);

//! Every parameter of \a effect (or of any model list).
LMMS_EXPORT QJsonArray controlParameterList(Effect* effect);
LMMS_EXPORT QJsonArray controlParameterList(const QList<AutomatableModel*>& models);

//! Resolves a parameter by \a name or \a index (exactly one must be given).
//! An ambiguous name is a typed invalid_args naming the candidate indexes.
LMMS_EXPORT AutomatableModel* controlResolveParameterIn(const QList<AutomatableModel*>& models,
	const QString& name, int index, bool hasIndex, ControlResult* error);
LMMS_EXPORT AutomatableModel* controlResolveParameter(Effect* effect, const QString& name,
	int index, bool hasIndex, ControlResult* error);

//! The effect as dsp.get_state reports it, addressing it as "fx-<index>".
LMMS_EXPORT QJsonObject controlEffectJson(Effect* effect, int index);

//! The effect's own state XML, wrapped in this fork's container element
//! (<zenepluginstate plugin="..." ladspa_file="..." ladspa_label="...">).
LMMS_EXPORT QString controlEffectStateXml(Effect* effect);

//! Restores \a effect from a document written by controlEffectStateXml().
//! Refuses (typed) when the document was written for a different device.
LMMS_EXPORT ControlResult controlRestoreEffectState(Effect* effect, const QByteArray& xml);

//! "Add an effect to this chain" for a built-in or hosted device.
LMMS_EXPORT Effect* controlInstantiateDevice(const ControlDeviceEntry& entry, EffectChain* chain,
	ControlResult* error);

//! Which plugin module instantiates \a entry and with which SubPluginFeatures
//! key - the same key shape the effect/instrument select dialogs build (LADSPA:
//! file + label; LV2: the URI).
//! \param pluginName receives the module to instantiate ("amplifier",
//!        "ladspaeffect" or "lv2effect").
//! \param key receives the sub-plugin key; \param useKey says whether the
//!        engine's instantiate call must be given it (false for a built-in,
//!        whose plugin name alone selects it).
//! False and *error set when this build does not ship the host the entry needs.
LMMS_EXPORT bool controlDeviceModule(const ControlDeviceEntry& entry, QString* pluginName,
	Plugin::Descriptor::SubPluginFeatures::Key* key, bool* useKey, ControlResult* error);

//! True when the module is present and exposes lmms_plugin_main - the check
//! that keeps Plugin::instantiate()'s modal error box out of a headless call.
LMMS_EXPORT bool controlPluginIsInstantiable(const QString& pluginName, ControlResult* error);

// ---------------------------------------------------------------------------
// State files and presets (plugin.state_* / plugin.preset_*)
// ---------------------------------------------------------------------------

//! Bounded snapshot size: SPEC A16's fallback is a *bounded* record, not an
//! unbounded one.
constexpr int ControlSnapshotLimit = 65536;

//! One device of a target: an effect instance, or a track's instrument.
struct ControlDeviceHandle
{
	Effect* effect = nullptr;
	InstrumentTrack* track = nullptr;
	QString pluginName; //!< "amplifier", "ladspaeffect", "tripleoscillator"
	QString folder;     //!< the preset sub-directory the product uses for this device
};

//! Resolves \a pluginId ("fx-<n>" or "inst") on \a target.
LMMS_EXPORT bool resolveControlDevice(const ControlTarget& target, const QString& pluginId,
	ControlDeviceHandle* handle, ControlResult* error);

//! The device's state as bytes: an effect uses this fork's zenepluginstate
//! document, an instrument the product's own instrument-track preset document.
LMMS_EXPORT QByteArray controlDeviceStateBytes(const ControlDeviceHandle& handle);

//! Restores \a handle from bytes written by controlDeviceStateBytes().
LMMS_EXPORT ControlResult controlRestoreDeviceState(const ControlDeviceHandle& handle,
	const QByteArray& bytes);

/*! Restores a device to a state captured earlier, RE-RESOLVING the device by id
 * (SPEC A16).
 *
 * An undo step may run long after the command that recorded it, and a
 * ControlDeviceHandle holds raw device pointers, so an inverse step must not
 * carry one: it carries the target id, the plugin id and the captured bytes,
 * and this helper resolves the device again at undo time. A device that is no
 * longer there is a typed not_found, not a dangling dereference.
 */
LMMS_EXPORT ControlResult controlRestoreCapturedState(const QString& targetId,
	const QString& pluginId, const QByteArray& bytes);

//! The product's preset directories for this device (trailing slash included).
LMMS_EXPORT QString controlUserPresetDir(const ControlDeviceHandle& handle);
LMMS_EXPORT QString controlFactoryPresetDir(const ControlDeviceHandle& handle);

//! File helpers for the state/preset commands.
LMMS_EXPORT bool controlReadFileBytes(const QString& path, QByteArray* bytes, ControlResult* error);
LMMS_EXPORT bool controlWriteFileBytes(const QString& path, const QByteArray& bytes,
	bool overwrite, ControlResult* error);
LMMS_EXPORT QString controlSha256OfBytes(const QByteArray& bytes);
//! What the command replaced: existence, size, hash and the bounded content.
LMMS_EXPORT QJsonObject controlFileSnapshot(const QString& path);
//! Refuses a preset name that would escape its preset directory.
LMMS_EXPORT bool controlSafePresetName(const QString& name, ControlResult* error);
//! The preset file name for \a name (".xpf" appended when missing).
LMMS_EXPORT QString controlPresetFileName(const QString& name);

} // namespace lmms

#endif // LMMS_CONTROL_DEVICE_SUPPORT_H
