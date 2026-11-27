/*
 * carlarack.cpp - Carla for LMMS (Rack)
 *
 * Copyright (C) 2014-2018 Filipe Coelho <falktx@falktx.com>
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

#include "Carla.h"

#include "embed.h"
#include "plugin_export.h"
#include "InstrumentTrack.h"
#include "PluginInstantiationFailure.h"

namespace lmms
{


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT carlarack_plugin_descriptor =
{
    LMMS_STRINGIFY( PLUGIN_NAME ),
    "Carla Rack",
    QT_TRANSLATE_NOOP( "PluginBrowser",
                       "Carla Rack Instrument" ),
    "falkTX <falktx/at/falktx.com>",
    CARLA_VERSION_HEX,
    Plugin::Type::Instrument,
    new PluginPixmapLoader( "logo" ),
    nullptr,
    nullptr,
} ;

PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* m, void*)
{
    // Q050-359: a build without a Carla runtime links the weak stand-in (plugins/CarlaBase/DummyCarla.cpp),
    // whose carla_get_native_rack_plugin() answers nullptr - and CarlaInstrument's constructor dereferences
    // it. Choosing Carla Rack to host a VST3 therefore killed Zene with SIGSEGV in the render/UI path
    // instead of saying that this build hosts nothing. Decline here, as carlavst2 does: Plugin::instantiate
    // keeps the track's XML (Q050-018) and plugin.load refuses, typed, quoting the reason below.
    if (carla_get_native_rack_plugin() == nullptr)
    {
        pluginload::noteInstantiationFailure(QStringLiteral(
            "this build has no Carla runtime, so Carla Rack cannot host anything: the module links the "
            "weak stand-in. Install a Carla runtime (or build one with tools/build-carla-runtime.sh) to "
            "load plug-ins inside Carla Rack; a VST3 loads through Zene's own VST3 host instead "
            "(plugin.list format vst3, then plugin.load its dev-<n>)"));
        return nullptr;
    }
    return new CarlaInstrument(static_cast<InstrumentTrack*>(m), &carlarack_plugin_descriptor, false);
}

}


} // namespace lmms
