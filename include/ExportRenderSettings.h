/*
 * ExportRenderSettings.h - the export choices that outlive one OutputSettings:
 *                          the dither switch and the SRC quality.
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

#ifndef LMMS_EXPORT_RENDER_SETTINGS_H
#define LMMS_EXPORT_RENDER_SETTINGS_H

#include <atomic>

#include "SrcQuality.h"
#include "lmms_export.h"

namespace lmms
{

/*! The current dither / SRC-quality selection, readable from both ends of a
 *  render.
 *
 *  **Why a process-wide holder exists at all.** `OutputSettings` is a value
 *  built per render - by `main.cpp` for the CLI, by the export dialog for the
 *  GUI - and it is gone when the render is. The two ends that must agree,
 *  however, are far apart: the quantiser (`AudioFileWave::writeBuffer`) does
 *  receive the `OutputSettings`, but the resampler that has to obey the SRC
 *  quality is built deep inside `Sample::play`, which never sees one. This class
 *  is the one place both ends can read.
 *
 *  **Ownership, stated so it cannot be read two ways:**
 *  - it holds the CURRENT selection - what the *next* render asks for;
 *  - `OutputSettings`' two new fields default to its values, so a caller that
 *    does not mention them (the export dialog, the CLI) inherits whatever was
 *    last selected through `export.set_dither` / `export.set_src_quality`;
 *  - `ProjectRenderer` publishes the `OutputSettings` it was handed here for the
 *    render's duration and restores the previous values when it is destroyed
 *    (including on abort), so an explicit per-render choice wins and nothing
 *    leaks from one render into the next.
 *
 *  **Defaults are the pre-existing behaviour**: dither OFF, `Linear`. That is
 *  not a convenience - an always-on dither would falsify this release's
 *  reproducibility claim, and a non-Linear default would change the bytes of
 *  every render the engine has ever produced.
 */
class LMMS_EXPORT ExportRenderSettings
{
public:
	//! The dither choice for the next render. OFF by default.
	static bool dither() noexcept;
	static void setDither(bool enabled) noexcept;

	//! The loudness report setting that outlives one OutputSettings: whether the
	//! NEXT render measures itself and writes the EBU R128 report beside its
	//! output (include/LoudnessReport.h). Process-wide exactly like the dither
	//! and SRC choices above, so the CLI, the export dialog and an agent over
	//! the control socket all read one value.
	static bool loudnessReport() noexcept;
	static void setLoudnessReport(bool enabled) noexcept;

	static SrcQuality srcQuality() noexcept;
	static void setSrcQuality(SrcQuality quality) noexcept;

	//! Back to the compile-time defaults: dither off, `SrcQuality::Linear`,
	//! loudness report OFF.
	static void reset() noexcept;

private:
	static std::atomic<bool> s_dither;
	static std::atomic<int> s_srcQuality;
	static std::atomic<bool> s_loudnessReport;
};

} // namespace lmms

#endif // LMMS_EXPORT_RENDER_SETTINGS_H
