/*
 * LegacyVst2Conversion.h - legacy VST2 state conversion.
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

#pragma once

#include <functional>
#include <optional>

#include <QDomElement>

#include "Plugin.h"

namespace lmms::legacyvst2
{
using Attributes = Plugin::Descriptor::SubPluginFeatures::Key::AttributeMap;
using Resolver = std::function<std::optional<Attributes>(const QString& absoluteFile, Plugin::Type type)>;

//! Resolve a plug-in binary through the factory.
LMMS_EXPORT std::optional<Attributes> resolveFromFactory(const QString& absoluteFile, Plugin::Type type);
//! Convert a legacy instrument node.
LMMS_EXPORT QDomElement convertInstrument(const QDomElement& instrumentNode, const Resolver& resolve = resolveFromFactory);
//! Convert a legacy effect node.
LMMS_EXPORT QDomElement convertEffect(const QDomElement& effectNode, const Resolver& resolve = resolveFromFactory);
//! Return the legacy state marker name.
LMMS_EXPORT QString legacyStateTagName();
//! Return whether conversion is enabled.
LMMS_EXPORT bool conversionEnabled();
//! Enable or disable conversion.
LMMS_EXPORT void setConversionEnabled(bool enabled);
}
