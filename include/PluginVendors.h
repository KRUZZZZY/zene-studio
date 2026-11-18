/*
 * PluginVendors.h - who made each VST3 and CLAP class, as its own factory reports it
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

#ifndef LMMS_PLUGIN_VENDORS_H
#define LMMS_PLUGIN_VENDORS_H

#include <QString>

#include "lmms_export.h"

namespace lmms::pluginvendors
{

/*! 0.5.0 plugin-hosting lane: the vendor of each VST3 and CLAP class, as the module's own factory
 *  reports it (a VST3 class's vendor, which the SDK fills from PFactoryInfo when the class names none;
 *  a CLAP descriptor's vendor). The two hosts' discovery (Vst3SubPluginFeatures, ClapSubPluginFeatures)
 *  records it while it reads each factory, so every listing - the catalogue, the device picker - finds
 *  it without opening a module again. It is kept here and not in the sub-plug-in key, because the key's
 *  attributes are the device's identity: they are saved in projects and name the organiser's
 *  favourites, and a vendor added there would change both.
 *  Any thread; a listing is the only writer and is not on an audio path. */

//! Records @a vendor for the class @a id (the VST3 class name, the CLAP plug-in id) of @a module.
LMMS_EXPORT void remember(const QString& module, const QString& id, const QString& vendor);
//! The vendor recorded for (@a module, @a id), or empty when none was reported or none was listed.
LMMS_EXPORT QString vendorOf(const QString& module, const QString& id);

} // namespace lmms::pluginvendors

#endif // LMMS_PLUGIN_VENDORS_H
