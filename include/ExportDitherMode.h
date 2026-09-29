/*
 * ExportDitherMode.h - which dither the integer export path applies
 *
 * The export's dither used to be a switch (on = TPDF). It is now a mode, so a caller
 * can ask for noise-shaped dither without anything that asked for the switch changing
 * meaning: `true` still means TPDF, `false` still means off (owner decision 13,
 * plans/RELEASE-0.4.0-PLAN.md §9.1).
 *
 *   Off          - no dither; the bytes are exactly the pre-dither encoder's.
 *   Tpdf         - triangular-PDF dither (include/ExportDither.h), 16- and 24-bit.
 *   NoiseShaped  - TPDF plus error-feedback noise shaping (include/ExportNoiseShaper.h),
 *                  which moves the requantisation noise out of the band the ear is most
 *                  sensitive to. 16-bit only: the encoder quantises 16-bit itself, so the
 *                  error it feeds back is known; the 24-bit path hands quantisation to
 *                  libsndfile, which cannot shape, so a noise-shaped request renders
 *                  24-bit with plain TPDF (and says so in export.get_settings).
 *
 * RPDF is deliberately absent - ExportDither.h says why.
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

#ifndef LMMS_EXPORT_DITHER_MODE_H
#define LMMS_EXPORT_DITHER_MODE_H

#include <QString>
#include <QStringList>

namespace lmms
{

enum class DitherMode : int
{
	Off = 0,
	Tpdf = 1,
	NoiseShaped = 2,
};

//! The wire name of @a mode: "off", "tpdf" or "noise_shaped".
inline QString ditherModeName(DitherMode mode)
{
	switch (mode)
	{
	case DitherMode::Tpdf: return QStringLiteral("tpdf");
	case DitherMode::NoiseShaped: return QStringLiteral("noise_shaped");
	case DitherMode::Off:
	default: return QStringLiteral("off");
	}
}

//! Every wire name, in enum order (the command schema's enum).
inline QStringList ditherModeNames()
{
	return {QStringLiteral("off"), QStringLiteral("tpdf"), QStringLiteral("noise_shaped")};
}

//! Parses a wire name; false (and @a mode untouched) for anything else.
inline bool ditherModeFromName(const QString& name, DitherMode* mode)
{
	const QStringList names = ditherModeNames();
	const int index = names.indexOf(name);
	if (index < 0) { return false; }
	*mode = static_cast<DitherMode>(index);
	return true;
}

} // namespace lmms

#endif // LMMS_EXPORT_DITHER_MODE_H
