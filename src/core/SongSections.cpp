/*
 * SongSections.cpp - the song-state sections written beside the track container
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

/*! Split out of Song.cpp (0.5.0, for the file-length ratchet and as the extension point new
 *  project sections need): the song-state elements written beside the track container - the
 *  tempo map, the modulation layer, the groove pool, the chord track, the detected key, the
 *  visibility sets - and their readers, moved here unchanged; and the ProjectSection registry,
 *  which a new section joins instead of adding a branch here. */

#include <algorithm>

#include <QDomElement>

#include "ChordTrack.h"
#include "DataFile.h"
#include "GroovePool.h"
#include "ModulationLayer.h"
#include "ProjectKey.h"
#include "ProjectSection.h"
#include "Song.h"
#include "TempoMap.h"

namespace lmms
{

void ProjectSection::registerSection(ProjectSection* section)
{
	auto& all = const_cast<std::vector<ProjectSection*>&>(sections());
	if (std::find(all.begin(), all.end(), section) == all.end()) { all.push_back(section); }
}

const std::vector<ProjectSection*>& ProjectSection::sections()
{
	static std::vector<ProjectSection*> registered;
	return registered;
}

void ProjectSection::clearAll()
{
	for (ProjectSection* section : sections()) { section->clear(); }
}


void Song::saveProjectSections( DataFile & dataFile )
{
	// Written ONLY when the map is active or holds an event, so a project that
	// never used a tempo map re-saves exactly the bytes it has always had -
	// the property the release's reproducibility claim rests on
	// (docs/TEMPO-MAP.md section 3).
	if( m_tempoMap.map().shouldPersist() )
	{
		m_tempoMap.map().saveSettings( dataFile, dataFile.content() );
	}

	// The same rule for the modulation layer (#602): written ONLY when the
	// layer holds a modulator, so a project that never used one re-saves exactly
	// the bytes it has always had (docs/MODULATION.md section 3).
	if( m_modulationLayer.layer().shouldPersist() )
	{
		m_modulationLayer.layer().saveSettings( dataFile, dataFile.content() );
	}

	// And for the groove pool (docs/GROOVE-POOL.md section 4): written ONLY
	// when the project actually holds a groove, so a project that never used
	// one re-saves exactly the bytes it has always had.
	if( m_groovePool.shouldPersist() )
	{
		m_groovePool.saveSettings( dataFile, dataFile.content() );
	}

	// And for the chord track: written ONLY when the project holds a chord, so
	// a project that never used one re-saves exactly the bytes it has always
	// had (include/ChordTrack.h).
	if( m_chordTrack.shouldPersist() )
	{
		m_chordTrack.saveSettings( dataFile, dataFile.content() );
	}

	// And for the detected key (feature row 34, import detection): written ONLY
	// when a detect.apply has put one there, so a project that never ran a
	// detection re-saves exactly the bytes it has always had
	// (include/ProjectKey.h).
	if( m_projectKey.shouldPersist() )
	{
		m_projectKey.saveSettings( dataFile, dataFile.content() );
	}

	// The named visibility sets (owner items 3+20+21): project state beside the
	// tempo map and the modulation layer, and written only when there is at
	// least one, so a project that never made one re-saves the bytes it always
	// had. It is NOT an element inside <trackcontainer>: that loader builds a
	// Track from every child it is not told to skip, and the marker that would
	// say "skip me" is the one DataFile::write deletes.
	if( !visibilitySets().isEmpty() )
	{
		saveVisibilitySetState( dataFile, dataFile.content() );
	}

	for( const ProjectSection* section : ProjectSection::sections() )
	{
		if( section->shouldPersist() ) { section->save( dataFile, dataFile.content() ); }
	}
}


bool Song::restorePublisherBackedSection(const QDomNode &node)
{
	// The tempo map (D11, docs/TEMPO-MAP.md). A project that has no
	// block loads into an EMPTY map, which is the state the engine was
	// in before this element existed - so the reader is a no-op for
	// every project that predates the feature.
	if (node.nodeName() == "tempo-map")
	{
		const QDomElement mapElement = node.toElement();
		m_tempoMap.edit([&mapElement](TempoMap& map) { return map.loadSettings(mapElement); });
		m_tempoMapAppliedTempo = -1;
		return true;
	}
	// The modulation layer (#602). A project with no block loads into an
	// EMPTY layer, and the write targets are re-resolved here rather
	// than carried across a load: the devices a route names are the
	// NEW project's, and a pointer from the old one must never survive
	// (docs/MODULATION.md section 4).
	if (node.nodeName() == "modulation-layer")
	{
		const QDomElement layerElement = node.toElement();
		m_modulationLayer.edit([&layerElement](ModulationLayer& layer,
			ModulationRuntime& runtime) {
			layer.loadSettings(layerElement);
			rebuildModulationRuntime(layer, &runtime);
			return true;
		});
		return true;
	}
	// The groove pool (docs/GROOVE-POOL.md). A project whose file carries no
	// <groove-pool> element - every project saved before this existed, and
	// every project that never captured a groove - loads into an EMPTY pool,
	// which is the state the engine was in before the feature. The clear in
	// GroovePool::loadSettings is what makes that true: the element is written
	// only when the pool is non-empty, so an absent element has to take the
	// pool back to empty rather than leave the previous project's grooves
	// behind (the reset-on-absence rule docs/UNDO-BOUNDS.md records).
	if (node.nodeName() == "groove-pool")
	{
		m_groovePool.loadSettings(node.toElement());
		return true;
	}
	// The chord track. A project whose file carries no <chord-track> element -
	// every project saved before this existed, and every project that never
	// wrote a chord - loads into an EMPTY track, which is the state the engine
	// was in before the feature and the same reset-on-absence rule the groove
	// pool's branch above follows (ChordTrack::loadSettings clears first).
	if (node.nodeName() == "chord-track")
	{
		m_chordTrack.loadSettings(node.toElement());
		return true;
	}

	// The detected key (feature row 34, import detection). The same
	// reset-on-absence rule: the element is written only when a detection has
	// been applied, so an absent element has to take the key back to EMPTY
	// rather than leave the previous project's key behind - and ProjectKey::
	// loadSettings clears unconditionally, which is what makes the
	// pre-first-detection state reachable from a journal checkpoint too.
	if (node.nodeName() == ProjectKey::ElementName)
	{
		m_projectKey.loadSettings(node.toElement());
		return true;
	}
	for (ProjectSection* section : ProjectSection::sections())
	{
		if (node.nodeName() == section->elementName())
		{
			section->load(node.toElement());
			return true;
		}
	}
	return false;
}

} // namespace lmms
