/*
 * MixerMaskingReport.h - the Masking Report window over mixer.masking_report
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

#ifndef LMMS_GUI_MIXER_MASKING_REPORT_H
#define LMMS_GUI_MIXER_MASKING_REPORT_H

#include "lmms_export.h"

namespace lmms::gui
{

/*! Q050-231.1 (R-mixing-7): the Masking Report window - tick the channels to compare, give each a priority
 *  (front, middle, back), press Report, and read each clash: the pair, the frequency range and the proposed
 *  cut on the lower-priority channel. Every report is one mixer.masking_report call; it changes no EQ.
 *  One window, raised when asked for again. */
LMMS_EXPORT void showMaskingReport();

} // namespace lmms::gui

#endif // LMMS_GUI_MIXER_MASKING_REPORT_H
