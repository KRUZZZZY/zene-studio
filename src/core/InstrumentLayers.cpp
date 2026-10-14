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

#include <algorithm>

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

bool consumePending(std::atomic<std::uint32_t>& count)
{
	auto pending = count.load(std::memory_order_relaxed);
	while (pending != 0)
	{
		if (count.compare_exchange_weak(pending, pending - 1, std::memory_order_relaxed)) { return true; }
	}
	return false;
}

//! A MIDI velocity in the zones' velocity space (velocity 64 is note volume 100).
int volumeOfVelocity(int velocity) { return velocity * DefaultVolume / MidiDefaultVelocity; }

//! @a mask with bit @a removed dropped and every higher bit moved down one.
std::uint32_t dropBit(std::uint32_t mask, int removed)
{
	const std::uint32_t below = mask & ((1u << removed) - 1u);
	return below | ((mask >> (removed + 1)) << removed);
}

//! 0.5.0 F2 interface: a layer's mix as attributes of its element (only what differs from the default). The
//! names carry a prefix because a layer's own <track> element already has a "muted" of its own.
void writeMix(QDomElement& element, const InstrumentLayers::Mix& mix)
{
	if (mix.muted) { element.setAttribute(QStringLiteral("layermuted"), 1); }
	if (mix.solo) { element.setAttribute(QStringLiteral("layersolo"), 1); }
	if (mix.level != 1.0f) { element.setAttribute(QStringLiteral("layerlevel"), static_cast<double>(mix.level)); }
}

auto readMix(const QDomElement& element) -> InstrumentLayers::Mix
{
	InstrumentLayers::Mix mix;
	mix.muted = element.attribute(QStringLiteral("layermuted")).toInt() != 0;
	mix.solo = element.attribute(QStringLiteral("layersolo")).toInt() != 0;
	bool ok = false;
	const float level = element.attribute(QStringLiteral("layerlevel")).toFloat(&ok);
	mix.level = ok ? std::clamp(level, 0.0f, InstrumentLayers::MaxLevel) : 1.0f;
	return mix;
}

bool isDefaultMix(const InstrumentLayers::Mix& mix)
{
	return !mix.muted && !mix.solo && mix.level == 1.0f;
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
	if (track == nullptr)
	{
		ChangeGuard guard;
		delete created;
		if (previous != nullptr && InstrumentTrack::autoAssignedTrack() != previous)
		{
			previous->autoAssignMidiDevice(true);
		}
		return nullptr;
	}
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
	m_mix[static_cast<std::size_t>(index)] = Mix{};
	publishMix();
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
	makeRoomForMix(index, readMix(element));
	adopt(track, index);
	publishMix();
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
	dropMix(index);
	delete track;
	publishMix();
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
	QDomElement saved = wrapper.firstChildElement();
	writeMix(saved, m_mix[static_cast<std::size_t>(index)]);  // the undo of a removal brings the mix back too
	QString xml;
	QTextStream stream(&xml);
	saved.save(stream, -1);
	return xml;
}

void InstrumentLayers::clear()
{
	while (layerCount() > 0) { removeLayer(layerCount()); }
	m_zones.clear();
	m_mix.fill(Mix{});  // not published: the owner may be going away (loadSettings publishes)
	for (auto& live : m_liveLayers) { live.store(0, std::memory_order_relaxed); }
	for (auto& accepted : m_baseAccepted) { accepted.store(0, std::memory_order_relaxed); }
	for (auto& skipped : m_baseSkipped) { skipped.store(0, std::memory_order_relaxed); }
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
	if (!active() && m_zones.zoneCount() == 0 && isDefaultMix(m_mix[0])) { return; }
	QDomElement element = doc.createElement(QStringLiteral("layers"));
	writeMix(element, m_mix[0]);
	m_zones.saveSettings(doc, element);
	for (int index = 1; index <= layerCount(); ++index)
	{
		QDomElement entry = doc.createElement(QStringLiteral("layer"));
		writeMix(entry, m_mix[static_cast<std::size_t>(index)]);
		layer(index)->saveState(doc, entry);
		element.appendChild(entry);
	}
	parent.appendChild(element);
}

void InstrumentLayers::loadSettings(const QDomElement& parent)
{
	clear();
	const QDomElement element = parent.firstChildElement(QStringLiteral("layers"));
	if (element.isNull()) { publishMix(); return; }
	m_mix[0] = readMix(element);
	int index = 0;
	for (QDomElement entry = element.firstChildElement(QStringLiteral("layer"));
		!entry.isNull() && index < MaxLayers; entry = entry.nextSiblingElement(QStringLiteral("layer")))
	{
		const QDomElement element = entry.firstChildElement(QStringLiteral("track"));
		if (InstrumentTrack* track = newLayerTrack(&element))
		{
			adopt(track, ++index);
			m_mix[static_cast<std::size_t>(index)] = readMix(entry);
		}
	}
	m_zones.loadSettings(element);
	publishMix();
}

auto InstrumentLayers::mix(int index) const -> Mix
{
	return index >= 0 && index <= layerCount() ? m_mix[static_cast<std::size_t>(index)] : Mix{};
}

auto InstrumentLayers::setMix(int index, const Mix& mix) -> bool
{
	if (index < 0 || index > layerCount()) { return false; }
	Mix wanted = mix;
	wanted.level = index == 0 ? 1.0f : std::clamp(mix.level, 0.0f, MaxLevel);  // layer 0's level is the track's volume
	m_mix[static_cast<std::size_t>(index)] = wanted;
	publishMix();
	return true;
}

void InstrumentLayers::publishMix()
{
	const int count = layerCount();
	const bool anySolo = std::any_of(m_mix.begin(), m_mix.begin() + count + 1, [](const Mix& mix) { return mix.solo; });
	for (int index = 0; index <= count; ++index)
	{
		const Mix& mix = m_mix[static_cast<std::size_t>(index)];
		const bool silent = mix.muted || (anySolo && !mix.solo);
		InstrumentTrack* track = index == 0 ? m_owner : layer(index);
		track->audioBusHandle()->setGain(silent ? 0.0f : mix.level);
	}
}

void InstrumentLayers::dropMix(int index)
{
	for (int i = index; i < MaxLayers; ++i) { m_mix[static_cast<std::size_t>(i)] = m_mix[static_cast<std::size_t>(i + 1)]; }
	m_mix[MaxLayers] = Mix{};
}

void InstrumentLayers::makeRoomForMix(int index, const Mix& mix)
{
	for (int i = MaxLayers; i > index; --i) { m_mix[static_cast<std::size_t>(i)] = m_mix[static_cast<std::size_t>(i - 1)]; }
	m_mix[static_cast<std::size_t>(index)] = mix;
}

auto InstrumentLayers::layerMask(int key, int volume) const -> std::uint32_t
{
	const std::uint64_t matches = m_zones.matchMask(key, volume);
	std::uint32_t layers = 0;
	for (int chain = 0; chain <= MaxLayers; ++chain)
	{
		if (((matches >> chain) & 1u) != 0) { layers |= 1u << chain; }
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

auto InstrumentLayers::baseTakesNoteOn(int key, int velocity, std::atomic_bool* noteAccepted) -> bool
{
	if (!validKey(key))
	{
		if (noteAccepted != nullptr) { noteAccepted->store(true, std::memory_order_release); }
		return true;
	}
	const bool plays = basePlays(key, volumeOfVelocity(velocity));
	if (noteAccepted != nullptr)
	{
		noteAccepted->store(plays, std::memory_order_release);
		return plays;
	}
	auto& pending = plays ? m_baseAccepted[static_cast<std::size_t>(key)] : m_baseSkipped[static_cast<std::size_t>(key)];
	pending.fetch_add(1, std::memory_order_relaxed);
	return plays;
}

auto InstrumentLayers::baseTakesNoteOff(int key, const std::atomic_bool* noteAccepted) -> bool
{
	if (!validKey(key)) { return true; }
	if (noteAccepted != nullptr) { return noteAccepted->load(std::memory_order_acquire); }
	const auto at = static_cast<std::size_t>(key);
	// Direct MIDI note-offs identify only the key: release accepted notes first, then suppress skipped-note offs.
	if (consumePending(m_baseAccepted[at])) { return true; }
	return !consumePending(m_baseSkipped[at]);
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
