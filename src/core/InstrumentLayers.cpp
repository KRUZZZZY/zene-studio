/*
 * InstrumentLayers.cpp - 0.5.0 MX-112: an instrument layer rack - several
 *                        instruments on one track, each with key and
 *                        velocity zones.
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

#include "InstrumentLayers.h"

#include <QDomDocument>
#include <QDomElement>
#include <QTextStream>

#include "AudioEngine.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "MidiEvent.h"
#include "Mixer.h"
#include "NotePlayHandle.h"
#include "TrackContainer.h"
#include "volume.h"

namespace lmms
{

namespace
{

//! RAII for the audio engine's model-change guard (recursive).
class ChangeGuard
{
public:
	ChangeGuard() : m_engine(Engine::audioEngine()) { if (m_engine != nullptr) { m_engine->requestChangeInModel(); } }
	~ChangeGuard() { if (m_engine != nullptr) { m_engine->doneChangeInModel(); } }
	ChangeGuard(const ChangeGuard&) = delete;
	auto operator=(const ChangeGuard&) -> ChangeGuard& = delete;

private:
	AudioEngine* m_engine;
};

//! The layers' own container: never the song's, so a layer has no row, no clips and no trk- id.
class LayerContainer : public TrackContainer
{
public:
	auto nodeName() const -> QString override { return QStringLiteral("layercontainer"); }
};

bool validKey(int key) { return key >= 0 && key < NumKeys; }

//! A MIDI velocity in the zones' velocity space (velocity 64 is note volume 100).
int volumeOfVelocity(int velocity) { return velocity * DefaultVolume / MidiDefaultVelocity; }

//! @a mask with bit @a removed dropped and every higher bit moved down one.
std::uint32_t dropBit(std::uint32_t mask, int removed)
{
	const std::uint32_t below = mask & ((1u << removed) - 1u);
	return below | ((mask >> (removed + 1)) << removed);
}

} // namespace

InstrumentLayers::InstrumentLayers(InstrumentTrack* owner) :
	m_owner(owner)
{
}

InstrumentLayers::~InstrumentLayers()
{
	clear();
}

auto InstrumentLayers::layerCount() const -> int
{
	int count = 0;
	while (count < MaxLayers && m_slots[static_cast<std::size_t>(count)].load(std::memory_order_acquire) != nullptr)
	{
		++count;
	}
	return count;
}

auto InstrumentLayers::layer(int index) const -> InstrumentTrack*
{
	if (index < 1 || index > MaxLayers) { return nullptr; }
	return m_slots[static_cast<std::size_t>(index - 1)].load(std::memory_order_acquire);
}

auto InstrumentLayers::ensureContainer() -> TrackContainer*
{
	if (m_container == nullptr) { m_container = std::make_unique<LayerContainer>(); }
	return m_container.get();
}

auto InstrumentLayers::newLayerTrack(const QDomElement* element) -> InstrumentTrack*
{
	InstrumentTrack* previous = InstrumentTrack::autoAssignedTrack();
	Track* created = element != nullptr ? Track::create(*element, ensureContainer())
		: Track::create(Track::Type::Instrument, ensureContainer());
	auto* track = dynamic_cast<InstrumentTrack*>(created);
	if (track == nullptr) { return nullptr; }
	// A hidden layer must not become the track a keyboard plays (InstrumentTrack's constructor assigns it).
	track->autoAssignMidiDevice(false);
	if (previous != nullptr && previous != track) { previous->autoAssignMidiDevice(true); }
	return track;
}

void InstrumentLayers::adopt(InstrumentTrack* track, int index)
{
	// The layer sounds as part of the track: its volume, pan and mute ARE the track's.
	track->volumeModel()->linkToModel(m_owner->volumeModel());
	track->panningModel()->linkToModel(m_owner->panningModel());
	track->getMutedModel()->linkToModel(m_owner->getMutedModel());
	track->baseNoteModel()->setValue(m_owner->baseNoteModel()->value());
	ChangeGuard guard;
	for (int slot = MaxLayers - 1; slot >= index; --slot)
	{
		const auto at = static_cast<std::size_t>(slot);
		m_slots[at].store(m_slots[at - 1].load(std::memory_order_relaxed), std::memory_order_release);
	}
	m_slots[static_cast<std::size_t>(index - 1)].store(track, std::memory_order_release);
	followMixerChannel(m_owner->mixerChannelModel()->value());
}

auto InstrumentLayers::addLayer(const QString& plugin, const Plugin::Descriptor::SubPluginFeatures::Key* key) -> int
{
	const int index = layerCount() + 1;
	if (index > MaxLayers) { return -1; }
	InstrumentTrack* track = newLayerTrack(nullptr);
	if (track == nullptr) { return -1; }
	Instrument* instrument = track->loadInstrument(plugin, key);
	if (instrument == nullptr || QString::fromUtf8(instrument->descriptor()->name) != plugin)
	{
		ChangeGuard guard;
		delete track;
		return -1;
	}
	track->setName(QStringLiteral("%1 layer %2").arg(m_owner->name()).arg(index));
	adopt(track, index);
	RackZone wholeKeyboard;
	wholeKeyboard.chain = index;
	m_zones.addZone(wholeKeyboard);
	return index;
}

auto InstrumentLayers::insertLayer(int index, const QString& xml) -> int
{
	if (index < 1 || index > layerCount() + 1 || layerCount() >= MaxLayers) { return -1; }
	QDomDocument doc;
	if (!doc.setContent(xml)) { return -1; }
	const QDomElement element = doc.documentElement();
	InstrumentTrack* track = newLayerTrack(&element);
	if (track == nullptr) { return -1; }
	m_zones.assign(zonesWithRoomAt(index));
	adopt(track, index);
	return index;
}

auto InstrumentLayers::removeLayer(int index) -> bool
{
	InstrumentTrack* track = layer(index);
	if (track == nullptr) { return false; }
	m_zones.assign(zonesWithout(index));
	ChangeGuard guard;
	for (int slot = index - 1; slot < MaxLayers; ++slot)
	{
		const auto at = static_cast<std::size_t>(slot);
		m_slots[at].store(slot + 1 < MaxLayers ? m_slots[at + 1].load(std::memory_order_relaxed) : nullptr,
			std::memory_order_release);
	}
	for (auto& live : m_liveLayers) { live.store(dropBit(live.load(std::memory_order_relaxed), index), std::memory_order_relaxed); }
	delete track;
	return true;
}

auto InstrumentLayers::layerXml(int index) const -> QString
{
	InstrumentTrack* track = layer(index);
	if (track == nullptr) { return QString(); }
	QDomDocument doc;
	QDomElement wrapper = doc.createElement(QStringLiteral("layer"));
	doc.appendChild(wrapper);
	track->saveState(doc, wrapper);
	QString xml;
	QTextStream stream(&xml);
	wrapper.firstChildElement().save(stream, -1);
	return xml;
}

void InstrumentLayers::clear()
{
	while (layerCount() > 0) { removeLayer(layerCount()); }
	m_zones.clear();
	for (auto& live : m_liveLayers) { live.store(0, std::memory_order_relaxed); }
	for (auto& skipped : m_baseSkipped) { skipped.store(false, std::memory_order_relaxed); }
}

auto InstrumentLayers::zonesWithout(int index) const -> std::vector<RackZone>
{
	std::vector<RackZone> zones;
	for (int i = 0; i < m_zones.zoneCount(); ++i)
	{
		RackZone zone = *m_zones.zone(i);
		if (zone.chain == index) { continue; }
		if (zone.chain > index) { --zone.chain; }
		zones.push_back(std::move(zone));
	}
	return zones;
}

auto InstrumentLayers::zonesWithRoomAt(int index) const -> std::vector<RackZone>
{
	std::vector<RackZone> zones;
	for (int i = 0; i < m_zones.zoneCount(); ++i)
	{
		RackZone zone = *m_zones.zone(i);
		if (zone.chain >= index) { ++zone.chain; }
		zones.push_back(std::move(zone));
	}
	return zones;
}

void InstrumentLayers::followMixerChannel(int channel)
{
	const int last = Engine::mixer() != nullptr ? static_cast<int>(Engine::mixer()->numChannels()) - 1 : 0;
	for (int index = 1; index <= layerCount(); ++index)
	{
		IntModel* model = layer(index)->mixerChannelModel();
		model->setRange(0, std::max(last, 0), 1);
		model->setValue(channel);
	}
}

void InstrumentLayers::saveSettings(QDomDocument& doc, QDomElement& parent) const
{
	if (!active() && m_zones.zoneCount() == 0) { return; }
	QDomElement element = doc.createElement(QStringLiteral("layers"));
	m_zones.saveSettings(doc, element);
	for (int index = 1; index <= layerCount(); ++index)
	{
		QDomElement entry = doc.createElement(QStringLiteral("layer"));
		layer(index)->saveState(doc, entry);
		element.appendChild(entry);
	}
	parent.appendChild(element);
}

void InstrumentLayers::loadSettings(const QDomElement& parent)
{
	clear();
	const QDomElement element = parent.firstChildElement(QStringLiteral("layers"));
	if (element.isNull()) { return; }
	int index = 0;
	for (QDomElement entry = element.firstChildElement(QStringLiteral("layer"));
		!entry.isNull() && index < MaxLayers; entry = entry.nextSiblingElement(QStringLiteral("layer")))
	{
		const QDomElement element = entry.firstChildElement(QStringLiteral("track"));
		if (InstrumentTrack* track = newLayerTrack(&element)) { adopt(track, ++index); }
	}
	m_zones.loadSettings(element);
}

auto InstrumentLayers::layerMask(int key, int volume) const -> std::uint32_t
{
	std::uint32_t layers = 0;
	const std::uint64_t matches = m_zones.matchMask(key, volume);
	for (int zone = 0; zone < 64 && (matches >> zone) != 0; ++zone)
	{
		if (((matches >> zone) & 1u) == 0) { continue; }
		const int chain = m_zones.zone(zone)->chain;
		if (chain >= 0 && chain <= MaxLayers) { layers |= 1u << chain; }
	}
	return layers;
}

auto InstrumentLayers::basePlays(int key, int volume) const -> bool
{
	if (!active()) { return true; }
	bool zoned = false;
	for (int i = 0; i < m_zones.zoneCount() && !zoned; ++i) { zoned = m_zones.zone(i)->chain == 0; }
	// A track instrument nobody gave a zone plays every note, as it did before the rack had layers.
	return !zoned || (layerMask(key, volume) & 1u) != 0;
}

auto InstrumentLayers::baseTakesNoteOn(int key, int velocity) -> bool
{
	if (!validKey(key)) { return true; }
	const bool plays = basePlays(key, volumeOfVelocity(velocity));
	m_baseSkipped[static_cast<std::size_t>(key)].store(!plays, std::memory_order_relaxed);
	return plays;
}

auto InstrumentLayers::baseTakesNoteOff(int key) -> bool
{
	return !validKey(key) || !m_baseSkipped[static_cast<std::size_t>(key)].exchange(false, std::memory_order_relaxed);
}

void InstrumentLayers::playNote(const NotePlayHandle& note, f_cnt_t offset, f_cnt_t frames, Track* patternTrack)
{
	if (!active()) { return; }
	const std::uint32_t layers = layerMask(note.key(), note.getVolume());
	for (int index = 1; index <= MaxLayers; ++index)
	{
		InstrumentTrack* track = ((layers >> index) & 1u) != 0 ? layer(index) : nullptr;
		if (track == nullptr) { continue; }
		NotePlayHandle* handle = NotePlayHandleManager::acquire(track, offset, frames, note);
		handle->setPatternTrack(patternTrack);
		Engine::audioEngine()->addPlayHandle(handle);
	}
}

void InstrumentLayers::sendToLayers(std::uint32_t layers, const MidiEvent& event, const TimePos& time, f_cnt_t offset)
{
	for (int index = 1; index <= MaxLayers; ++index)
	{
		InstrumentTrack* track = ((layers >> index) & 1u) != 0 ? layer(index) : nullptr;
		if (track != nullptr) { track->processInEvent(event, time, offset); }
	}
}

auto InstrumentLayers::liveLayersFor(const MidiEvent& event) -> std::uint32_t
{
	const bool noteOn = event.type() == MidiNoteOn && event.velocity() > 0;
	const bool keyed = event.type() == MidiNoteOff || event.type() == MidiNoteOn || event.type() == MidiKeyPressure;
	if (!keyed) { return ~1u; }  // a channel-wide message (bend, sustain, a controller) reaches every layer
	const int key = event.key();
	if (!validKey(key)) { return 0; }
	auto& live = m_liveLayers[static_cast<std::size_t>(key)];
	if (!noteOn) { return event.type() == MidiKeyPressure ? live.load(std::memory_order_relaxed) : live.exchange(0, std::memory_order_relaxed); }
	// A note-on: every layer a zone names, remembered for the note-off.
	const std::uint32_t layers = layerMask(key, event.volume(m_owner->midiPort()->baseVelocity())) & ~1u;
	live.store(layers, std::memory_order_relaxed);
	return layers;
}

void InstrumentLayers::forwardInEvent(const MidiEvent& event, const TimePos& time, f_cnt_t offset)
{
	if (!active()) { return; }
	sendToLayers(liveLayersFor(event), event, time, offset);
}

} // namespace lmms
