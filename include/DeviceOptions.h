/*
 * DeviceOptions.h - what a user sets on one device: parameter names, folding, editor scale, compatibility
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

#ifndef LMMS_DEVICE_OPTIONS_H
#define LMMS_DEVICE_OPTIONS_H

#include <QDomElement>
#include <QMap>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

/*! 0.5.0 L10 (W3): the options a user sets on one device, carried by the device itself so a copy,
 *  a preset, an A/B slot and an undo of a removal take them along. Saved as attributes on the
 *  device's own element, each only when it is not at its default - a device nobody set an option
 *  on saves the bytes it always did, and an element without them reads back as the defaults.
 *
 *  - aliases (MX-097): a name of the user's for a parameter, shown on the device's card. The
 *    parameter keeps its own name as its address: macros, modulators, automation and every
 *    plugin.param_* call still name it by that, so nothing that refers to it changes.
 *  - folded (MX-113): the device's card drawn as its header only. View state; no audio change.
 *  - editorScale (MX-094): the zoom a hosted plug-in's own window is opened at.
 *  - compatibility (MX-099): genericEditor opens the host's generic controls instead of the
 *    plug-in's own window (for an editor that misbehaves); keepRunning never puts the device to
 *    sleep on silence (for one that needs its input to keep running); latencyOverride is the
 *    latency, in frames, delay compensation uses instead of what the device reports (-1: its own).
 *
 *  Control thread, except keepRunning and latencyOverride, which the audio thread reads as plain
 *  scalars (the same contract as Effect's own bypass flag). */
class LMMS_EXPORT DeviceOptions
{
public:
	static constexpr float MinEditorScale = 0.5f;
	static constexpr float MaxEditorScale = 3.f;
	static constexpr int MaxAliasLength = 64;
	//! The largest latency override, in frames (plugin.options' bound; a file's larger value reads as this).
	static constexpr int MaxLatencyOverride = 1 << 20;

	//! The alias of parameter @a name, or empty.
	QString alias(const QString& name) const { return m_aliases.value(name); }
	//! The alias when there is one, else @a name.
	QString shownName(const QString& name) const;
	//! Gives parameter @a name the alias @a alias (trimmed, bounded); an empty one removes it.
	void setAlias(const QString& name, const QString& alias);
	const QMap<QString, QString>& aliases() const { return m_aliases; }

	bool folded = false;
	float editorScale = 1.f;
	bool genericEditor = false;
	bool keepRunning = false;
	int latencyOverride = -1;

	//! True when every option is at its default.
	bool isDefault() const;
	bool operator==(const DeviceOptions& other) const;
	bool operator!=(const DeviceOptions& other) const { return !(*this == other); }
	void save(QDomElement& element) const;
	void load(const QDomElement& element);

private:
	QMap<QString, QString> m_aliases;
};

} // namespace lmms

#endif // LMMS_DEVICE_OPTIONS_H
