/*
 * CarlaVst2Legacy.h - restore converted legacy VST2 state.
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

#include <cstdint>
#include <functional>
#include <QDomElement>
#include <QString>
#include "carlabase_export.h"

namespace lmms::carlavst2
{
struct LegacyCalls
{
	std::function<uint32_t()> programCount;
	std::function<void(uint32_t)> setProgram;
	std::function<void(const char*)> setChunk;
	std::function<uint32_t()> parameterCount;
	std::function<void(uint32_t, float)> setParameter;
};
// Must equal legacyvst2::legacyStateTagName() in include/LegacyVst2Conversion.h.
CARLABASE_EXPORT QString legacyStateTag();
CARLABASE_EXPORT void applyLegacyState(const QDomElement& legacy, const LegacyCalls& calls);
CARLABASE_EXPORT bool hasModelSettings(const QDomElement& state, const QString& name);
CARLABASE_EXPORT void warnExtraModelSettings(const QDomElement& state, std::size_t count);
} // namespace lmms::carlavst2
