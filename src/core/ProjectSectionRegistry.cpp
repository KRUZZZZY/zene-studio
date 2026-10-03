/*
 * ProjectSectionRegistry.cpp - every project section and audio-thread singleton built once, before the engine runs
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
#include "ProjectSectionRegistry.h"

#include "ArrangementSections.h"
#include "AutomationLatency.h"
#include "AutomationSwitches.h"
#include "EditLocks.h"
#include "MetronomeOptions.h"
#include "MidiPlayback.h"
#include "MixerOptions.h"
#include "MixerSnapshots.h"
#include "ProjectInfo.h"
#include "ProjectMarkers.h"
#include "TrackKeyNames.h"
#include "TrackLooks.h"
#include "TrackVersions.h"
#include "ViewMemory.h"

namespace lmms::projectsections
{

void registerAll()
{
	ArrangementSections::instance();
	AutomationSwitches::instance();
	MidiPlayback::instance();
	MixerOptions::instance();
	ProjectMarkers::instance();
	TrackKeyNames::instance();
	TrackVersions::instance();
	EditLocks::instance();  // W3 L12
	MixerSnapshots::instance();
	ProjectInfo::instance();
	TrackLooks::instance();  // W3 L13
	ViewMemory::instance();
	AutomationLatency::instance();  // W3 engine: read by the audio thread's automation ramps
	registerAlternatives();
	registerSubprojects();
	metronomeoptions::options();  // reads the config file here, not on the audio thread at the first click
}

} // namespace lmms::projectsections
