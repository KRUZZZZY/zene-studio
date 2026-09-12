/*
 * RetroMidiCaptureSettings.h - the persisted arm switch of retrospective MIDI
 *                              capture (owner item 14)
 *
 * Copyright (c) 2026 LMMS developers
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

#ifndef LMMS_RETRO_MIDI_CAPTURE_SETTINGS_H
#define LMMS_RETRO_MIDI_CAPTURE_SETTINGS_H

#include "lmms_export.h"

namespace lmms
{

//! The persisted arm switch of retrospective MIDI capture, and why it lives here.
/*!
 * The key is the config file's `midi/retrocapture`: "1" armed, "0" (the default)
 * disarmed. It is read and written HERE - in the layer that can legitimately use
 * ConfigManager - and not in RetroMidiCapture's constructor, because one
 * MidiClient in this tree is a file-static global built before main()
 * (src/core/midi/MidiPort.cpp) and ConfigManager's constructor dereferences qApp
 * (src/core/ConfigManager.cpp), so a ConfigManager read that early is a null
 * dereference at static-initialisation time (docs/MIDI-RETRO-CAPTURE.md 3.6, as
 * corrected by slice 1). ConfigManager is not a JournallingObject and the mode is
 * not project state, so this is a machine-local default, not a project file
 * field; the arm command reports it as `persisted`, and it records no
 * transaction.
 */

//! The persisted arm state; false when the key is absent or is not "1".
LMMS_EXPORT bool retroCapturePersistedArmed();

//! Write the persisted arm state and save the config file. Called only when the
//! mode actually moved, so re-arming an armed capture never rewrites the file.
LMMS_EXPORT void setRetroCapturePersistedArmed(bool armed);


} // namespace lmms

#endif // LMMS_RETRO_MIDI_CAPTURE_SETTINGS_H
