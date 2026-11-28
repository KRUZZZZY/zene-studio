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
#include <cstdint>

#include "ExportDitherMode.h"
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
 *  - `ProjectRenderer` publishes the `OutputSettings` it was handed here for as
 *    long as it lives, and retires that publication when it is destroyed
 *    (including on abort). The selection IN FORCE is the newest LIVE
 *    publication's; the selection found before the first publication returns
 *    when the last one is retired. So an explicit per-render choice wins, a
 *    renderer that ends while another is still alive cannot put its own older
 *    snapshot back under it (cluster 5239), and nothing leaks into the next
 *    render.
 *
 *  **Defaults are the pre-existing behaviour**: dither OFF, `Linear`. That is
 *  not a convenience - an always-on dither would falsify this release's
 *  reproducibility claim, and a non-Linear default would change the bytes of
 *  every render the engine has ever produced.
 */
class LMMS_EXPORT ExportRenderSettings
{
public:
	//! The dither choice for the next render (include/ExportDitherMode.h). OFF by
	//! default. The bool pair is the pre-mode API, kept exactly: dither() is "any
	//! dither", setDither(true) selects TPDF.
	static DitherMode ditherMode() noexcept;
	static void setDitherMode(DitherMode mode) noexcept;
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
	//! Change the selection and all retained converters as one transaction.
	static auto setSrcQuality(SrcQuality quality) noexcept -> bool;
	//! Called by the engine while it owns the change lock, after all states prepare.
	static void commitProposedSrcQuality(SrcQuality quality) noexcept;
	using SrcQualityListener = bool (*)(SrcQuality quality) noexcept;
	static void setSrcQualityListener(SrcQualityListener listener) noexcept;
	static std::uint64_t srcQualityPropagationFailures() noexcept;

#ifdef LMMS_TESTING
	using SrcQualityPublicationProbe = void (*)(void* userData) noexcept;
	static void setSrcQualityPublicationProbeForTest(SrcQualityPublicationProbe probe,
		void* userData) noexcept;
#endif

	//! Back to the compile-time defaults: dither off, `SrcQuality::Linear`,
	//! loudness report OFF. Every live publication is dropped too: `reset()`
	//! promises the defaults, and a renderer constructed before it must not put a
	//! selection from before it back afterwards.
	static void reset() noexcept;

	/*! A render's own publication of the two choices that outlive one
	 *  OutputSettings. `token` identifies it so its owner can retire it; 0 means
	 *  nothing was published. `srcQualityApplied` is false when the engine could
	 *  not prepare converters for the requested quality - the render is refused
	 *  (`ProjectRenderer::refusal()`), and the publication is still retired
	 *  normally by the renderer's destruction.
	 */
	struct RenderSelection
	{
		std::uint64_t token = 0;
		bool srcQualityApplied = false;
	};

	/*! Publishes \a mode and \a quality for as long as the renderer that asked
	 *  lives, and returns the token its destruction retires with.
	 *
	 *  **Ownership, in the presence of more than one renderer.** The selection in
	 *  force is the newest live publication's, and retiring is safe in ANY order:
	 *  the retirement re-applies the newest publication that is still alive, and
	 *  when the last one goes it re-applies the selection found before the first
	 *  one published. A renderer's own "previous value" snapshot is deliberately
	 *  NOT what a retirement puts back - that snapshot belongs to whoever was
	 *  current when this renderer was built, and it is the snapshot of an OLDER
	 *  renderer that used to land under a newer one (cluster 5239).
	 *
	 *  Control threads only: a renderer publishes in its constructor and retires
	 *  in its destructor. The audio and render paths read `ditherMode()` /
	 *  `srcQuality()` and never take the registry's lock.
	 */
	static auto publishRenderSelection(DitherMode mode, SrcQuality quality) -> RenderSelection;
	/*! Retires one publication and re-applies the newest remaining one, or the
	 *  pre-render selection when none is left. A token that was already retired,
	 *  or that `reset()` dropped, is ignored. False only when re-applying the
	 *  SRC quality failed.
	 */
	static auto retireRenderSelection(std::uint64_t token) -> bool;

private:
	static std::atomic<int> s_ditherMode;
	static std::atomic<int> s_srcQuality;
	static std::atomic<int> s_proposedSrcQuality;
	static std::atomic<bool> s_loudnessReport;
	static std::atomic<SrcQualityListener> s_srcQualityListener;
	static std::atomic<std::uint64_t> s_srcQualityPropagationFailures;
#ifdef LMMS_TESTING
	static std::atomic<SrcQualityPublicationProbe> s_srcQualityPublicationProbe;
	static std::atomic<void*> s_srcQualityPublicationProbeUserData;
#endif
};

} // namespace lmms

#endif // LMMS_EXPORT_RENDER_SETTINGS_H
