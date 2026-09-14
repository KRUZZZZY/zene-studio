/*
 * RetroMidiCaptureSettings.cpp - the persisted arm switch (owner item 14)
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

#include "RetroMidiCaptureSettings.h"

#include "ConfigManager.h"
#include "Engine.h"
#include "AudioEngine.h"
#include "MidiClient.h"
#include "RetroMidiCapture.h"

namespace lmms
{

namespace
{

//! The config file's own key form: "<class>/<attribute>" (settings.get/set use
//! the same split, src/core/ControlCommandsSettings.cpp).
const QString RetroCaptureClass = QStringLiteral("midi");
const QString RetroCaptureAttribute = QStringLiteral("retrocapture");
const QString RetroCaptureOff = QStringLiteral("0");
const QString RetroCaptureOn = QStringLiteral("1");

} // namespace


bool retroCapturePersistedArmed()
{
	return ConfigManager::inst()->value(RetroCaptureClass, RetroCaptureAttribute,
		RetroCaptureOff) == RetroCaptureOn;
}


void setRetroCapturePersistedArmed(bool armed)
{
	ConfigManager::inst()->setValue(RetroCaptureClass, RetroCaptureAttribute,
		armed ? RetroCaptureOn : RetroCaptureOff);
	ConfigManager::inst()->saveConfigFile();
}


void applyPersistedRetroCaptureArm()
{
	// Called from the GUI's startup path (MainWindow::finalize), which is the
	// first point where qApp exists AND a MIDI client is open. Silent when there
	// is no client; the default ("0") disarms, so a fresh instance keeps the
	// feature off.
	AudioEngine* engine = Engine::audioEngine();
	MidiClient* client = engine != nullptr ? engine->midiClient() : nullptr;
	if (client == nullptr) { return; }
	client->retroCapture().arm(retroCapturePersistedArmed());
}


} // namespace lmms
