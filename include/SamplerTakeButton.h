/*
 * SamplerTakeButton.h - the "Record take" button of a sampler's own view (0.5.0 L10, Q050-156)
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

#ifndef LMMS_GUI_SAMPLER_TAKE_BUTTON_H
#define LMMS_GUI_SAMPLER_TAKE_BUTTON_H

#include <QString>

#include "lmms_export.h"

class QPushButton;

namespace lmms::gui
{

/*! Q050-156: one button, one capture, the same registered commands an agent runs.
 *
 *  The first click starts record.sampler_capture for the button's track (a bounded take; the
 *  view sets 'zeneSamplerTakeSeconds' when it wants another length). While the take runs the
 *  button says Stop and asks record.sampler_capture_status on a timer; a second click stops it
 *  early (record.sampler_capture_stop). A finished take - stopped or reached its length - is
 *  landed with record.sampler_capture_land and loaded into the device. Nothing here talks to the
 *  instrument directly: the view and the agent drive one command surface. */

//! A click: start a take, stop and land the one in hand, or land a finished one.
LMMS_EXPORT void samplerTakeButtonStep(QPushButton* button, const QString& trackId);
//! The view's polling entry point: land the take once it finished on its own.
LMMS_EXPORT void samplerTakeButtonPoll(QPushButton* button);

} // namespace lmms::gui

#endif // LMMS_GUI_SAMPLER_TAKE_BUTTON_H
