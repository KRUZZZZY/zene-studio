/*
 * ExportQualityControls.h - the export dialog's dither and resampling choices
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

#ifndef LMMS_GUI_EXPORT_QUALITY_CONTROLS_H
#define LMMS_GUI_EXPORT_QUALITY_CONTROLS_H

#include "lmms_export.h"

class QGroupBox;
class QWidget;

namespace lmms::gui
{

/*! KNOWN-LIMITATIONS "dither ... SRC quality ... there is still no interface for any of it": a
 *  "Render quality" group for the export dialog with two choices - Dither (off / TPDF /
 *  noise-shaped TPDF) and Resampling (linear / the three sinc converters) - read from
 *  export.get_settings and set through export.set_dither / export.set_src_quality, the commands an
 *  agent and the CLI's settings reach. The dialog's OutputSettings inherits the selection
 *  (ExportRenderSettings), so choosing here is what the next render uses. */
LMMS_EXPORT QGroupBox* makeExportQualityControls(QWidget* parent);

} // namespace lmms::gui

#endif // LMMS_GUI_EXPORT_QUALITY_CONTROLS_H
