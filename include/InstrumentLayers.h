/*
 * InstrumentLayers.h - 0.5.0 MX-112: an instrument layer rack - several
 *                      instruments on one track, each with key and
 *                      velocity zones.
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

#ifndef LMMS_INSTRUMENT_LAYERS_H
#define LMMS_INSTRUMENT_LAYERS_H

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <QString>

#include "LmmsTypes.h"
#include "Note.h"
#include "Plugin.h"
#include "RackZones.h"
#include "lmms_export.h"

class QDomDocument;
class QDomElement;

namespace lmms
{

class InstrumentTrack;
class MidiEvent;
class NotePlayHandle;
class TimePos;
class Track;
class TrackContainer;

/**
 * @brief 0.5.0 MX-112: the instrument layer rack of one instrument track.
 *
 * Layer 0 is the track's own instrument. Layers 1..layerCount() are further instruments, each hosted in
 * a private InstrumentTrack that lives in a container of its own (never in the song's track list, so it
 * has no row, no clips and no trk- id of its own): every instrument kind - a note-driven built-in, a
 * hosted CLAP/VST3/LV2 plug-in - plays in a layer exactly as it plays on a track.
 *
 * WHICH LAYER PLAYS A NOTE is decided by key and velocity zones, and those are the rack's own model:
 * a RackZones list (RackZones.h - the same struct, validation, `<zone>` persistence and lookup the mixer
 * rack's zones use) whose `chain` names a layer. Unlike a routing rack, a layer rack STACKS: a note
 * plays on EVERY layer a matching zone names (RackZones::matchMask), which is what makes layering a
 * pad under a piano, a velocity split (soft and hard samples) or a key split (bass below C3) the same
 * mechanism. A new layer gets a whole-keyboard zone; the track's own instrument plays every note until
 * it is given a zone of its own (a rack with no zone on layer 0 leaves it unfiltered).
 *
 * A layer plays into the track's mixer channel and follows the track's volume, pan and mute (its models
 * are linked to the track's); the track's own effect chain processes the track's instrument only. Notes
 * reach a layer where the track starts them: a clip's note (InstrumentTrack::triggerNote, the render
 * thread) starts the same note, with the same length, on each matching layer; a live note
 * (InstrumentTrack::processInEvent, the MIDI or GUI thread) is passed on with its note-off. With no
 * layers the track behaves exactly as it did before this class existed.
 *
 * Threading: add/remove/zones/persistence are control thread only and publish under the audio engine's
 * model-change guard. The note paths read fixed atomics and the zone list; they allocate nothing beyond
 * what starting a note on a track already does (NotePlayHandleManager's pool, the engine's lock-free
 * play-handle list).
 */
class LMMS_EXPORT InstrumentLayers
{
public:
	static constexpr int MaxLayers = 8;

	explicit InstrumentLayers(InstrumentTrack* owner);
	~InstrumentLayers();
	InstrumentLayers(const InstrumentLayers&) = delete;
	auto operator=(const InstrumentLayers&) -> InstrumentLayers& = delete;

	// --- control thread ---

	//! The layers past the track's own instrument.
	auto layerCount() const -> int;
	//! Layer @a index (1..layerCount()); nullptr otherwise.
	auto layer(int index) const -> InstrumentTrack*;
	auto active() const -> bool { return layerCount() > 0; }

	//! Adds a layer playing @a plugin (with @a key for a hosted plug-in) and a whole-keyboard zone for it.
	//! The layer's index, or -1 when the rack is full or the instrument did not load.
	auto addLayer(const QString& plugin, const Plugin::Descriptor::SubPluginFeatures::Key* key) -> int;
	//! Re-creates a layer from the XML layerXml() captured, at @a index (undo of a removal); -1 on failure.
	auto insertLayer(int index, const QString& xml) -> int;
	//! Drops layer @a index; zones naming it go with it and the later layers' zones move down one.
	auto removeLayer(int index) -> bool;
	//! The layer's own track state as XML (its instrument and the instrument's state), bounded by the caller.
	auto layerXml(int index) const -> QString;
	//! Drops every layer and zone.
	void clear();

	//! The zones: `chain` is the layer they name (0 = the track's own instrument).
	auto zones() -> RackZones& { return m_zones; }
	auto zones() const -> const RackZones& { return m_zones; }

	//! The track's mixer channel changed: the layers follow it (InstrumentTrack::updateMixerChannel).
	void followMixerChannel(int channel);

	//! <layers> with the zones and one <layer> per layer, when there is a layer or a zone.
	void saveSettings(QDomDocument& doc, QDomElement& parent) const;
	//! Reads the <layers> child of @a parent; none leaves an empty rack.
	void loadSettings(const QDomElement& parent);

	// --- note paths ---

	//! Whether the track's own instrument plays a note of @a key at note volume @a volume.
	auto basePlays(int key, int volume) const -> bool;
	//! processOutEvent's note-on gate for the track's instrument (MIDI velocity); remembered for the note-off.
	auto baseTakesNoteOn(int key, int velocity) -> bool;
	auto baseTakesNoteOff(int key) -> bool;
	//! Render thread: starts @a note (the track's own, decorated) on every layer it falls into, in
	//! @a patternTrack's pattern when it plays in one.
	void playNote(const NotePlayHandle& note, f_cnt_t offset, f_cnt_t frames, Track* patternTrack);
	//! MIDI/GUI thread: passes a live event on to the layers it concerns.
	void forwardInEvent(const MidiEvent& event, const TimePos& time, f_cnt_t offset);

private:
	//! One bit per layer index (bit k = layer k) a note of @a key at @a volume plays on.
	auto layerMask(int key, int volume) const -> std::uint32_t;
	auto ensureContainer() -> TrackContainer*;
	//! A new layer track - from @a element when given, else empty - that does not take the live MIDI input
	//! the track constructor auto-assigns to every new track.
	auto newLayerTrack(const QDomElement* element) -> InstrumentTrack*;
	//! Links the new layer to the track (volume, pan, mute, channel) and publishes it at slot @a index - 1.
	void adopt(InstrumentTrack* track, int index);
	//! The zones after layer @a index is removed (dropped) or inserted (made room for).
	auto zonesWithout(int index) const -> std::vector<RackZone>;
	auto zonesWithRoomAt(int index) const -> std::vector<RackZone>;
	//! The layers a live @a event goes to: a note-on's zones (remembered per key), the layers its note-on
	//! reached for a note-off or key pressure, every layer for a channel-wide message.
	auto liveLayersFor(const MidiEvent& event) -> std::uint32_t;
	void sendToLayers(std::uint32_t layers, const MidiEvent& event, const TimePos& time, f_cnt_t offset);

	InstrumentTrack* m_owner;
	std::unique_ptr<TrackContainer> m_container;
	std::array<std::atomic<InstrumentTrack*>, MaxLayers> m_slots{};
	RackZones m_zones;
	//! Per key: the layers a live note-on was passed to (bit k = layer k), for its note-off.
	std::array<std::atomic<std::uint32_t>, NumKeys> m_liveLayers{};
	//! Per key: whether the track's own instrument declined the note-on (processOutEvent), so its note-off is
	//! declined too.
	std::array<std::atomic<bool>, NumKeys> m_baseSkipped{};
};

} // namespace lmms

#endif // LMMS_INSTRUMENT_LAYERS_H
