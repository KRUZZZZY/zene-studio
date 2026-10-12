/*
 * MissingDevices.h - the devices a project asked for that this session could not load
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

#ifndef LMMS_MISSING_DEVICES_H
#define LMMS_MISSING_DEVICES_H

#include <QDomElement>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

class Song;

/*! 0.5.0 Q050-018: one placeholder - an instrument or effect the project names but this session could not
 *  load (its module or hosted plug-in binary is absent). The original XML is kept by the track / chain that
 *  holds the placeholder and written back on save, so nothing of the device is lost; this record is what a
 *  user or an agent is told. */
struct MissingDevice
{
	QString kind;      //!< "instrument" or "effect"
	QString name;      //!< what the user knows it as: the VST3 class, CLAP name, LV2 URI tail, VST file, LMMS module name
	QString format;    //!< "native", "vst", "vst3", "clap" or "lv2"
	QString vendor;    //!< the maker when known (see describeMissingDevice), "unknown" otherwise
	QString track;     //!< the track or mixer channel it sits on
	QString reason;    //!< why it is a placeholder
	QString identity;  //!< the original plug-in identity: module name plus the key's attributes, for recovery

	QJsonObject toJson() const;
};

namespace missingdevices
{

//! "native" / "vst" / "vst3" / "clap" / "lv2" for an `<instrument name=...>` or `<effect name=...>` module name.
LMMS_EXPORT QString formatOf(const QString& moduleName);

//! Reads the name, format, vendor and identity out of the file's original `<instrument>` / `<effect>` element
//! (or, for an old compat-format project, the element named after the module). Vendor: the `vendor` attribute
//! a build that knew it wrote beside the module name; else what this session's plug-in scan reported for the
//! class; else "LMMS" for a native module and the host of an LV2 URI; else "unknown".
LMMS_EXPORT MissingDevice describe(const QDomElement& original, const QString& kind, const QString& track,
	const QString& reason);

//! A copy of @a element in a document of its own, so it outlives the project file's DOM.
LMMS_EXPORT QDomElement detachedCopy(const QDomElement& element);

//! Every placeholder the session holds now: instrument tracks' instruments, the effect chains of tracks, and each
//! mixer channel's own chain, parallel rack chains and post-fader inserts, in track then channel order. A placeholder the user replaced is no longer listed.
LMMS_EXPORT QList<MissingDevice> collect(Song* song);

//! The text of the one project-open notice (empty when @a devices is empty).
LMMS_EXPORT QString noticeText(const QList<MissingDevice>& devices);

} // namespace missingdevices
} // namespace lmms

#endif // LMMS_MISSING_DEVICES_H
