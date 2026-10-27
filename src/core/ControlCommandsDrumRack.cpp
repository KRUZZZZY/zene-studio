/*
 * ControlCommandsDrumRack.cpp - track.drum_rack: a drum rack of pads, each a sampler with its own channel
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
/*! 0.5.0 L10 (MX-111): a drum rack with a chain per pad, made of the parts the product has. Each
 *  pad is a sampler track (AudioFileProcessor) playing its sample at its key, inside one folder
 *  track; each pad plays into a mixer channel of its own - the pad's chain, where its inserts go -
 *  and every pad channel sends to one bus channel, the rack's output. One undo step takes the whole
 *  rack away; its redo builds it again from the same pads, under the same track and channel ids.
 *
 *  Q050-066 stage 1: the rack's pads are read back (track.drum_rack_get_state, through the shared
 *  DrumPadState contract), a pad is added to a rack (track.drum_rack_pad_add) and a pad's name, trigger
 *  key and sample are edited (track.drum_rack_pad_set), each one undo step. No new project data: a pad
 *  is still only a track, its sampler and its channel, so a rack saved before reads back the same. */

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#include <QFileInfo>
#include <QJsonArray>

#include "ControlEdit.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlTrackCreation.h"
#include "ControlVocabulary.h"
#include "DrumPadState.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "ProjectJournal.h"
#include "SampleDecoder.h"
#include "Song.h"
#include "TrackFolder.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int MaxPads = 64;
constexpr int FirstPadKey = 36;  // C2: General MIDI's kick, where drum maps start

struct Pad
{
	QString sample;
	QString name;
	int key = FirstPadKey;
};

/*! What one build made, so its undo can take exactly that away (and a redo fill it again). A redo gives
 *  every track and channel back the id its first build had, so the trk-/ch- ids an agent was answered, and
 *  a later step that names them (a pad added to the rack), stay valid across undo and redo. */
struct Built
{
	std::vector<int> trackIds;  //!< the pads first, the folder last, by stable id (a removed track's undo frees it)
	std::vector<int> channelIds;  //!< the pad channels and the bus, by persistent id
	std::vector<int> undoneTracks;  //!< what the last undo took away, for the redo to name again
	std::vector<int> undoneChannels;
};

int reusedId(const std::vector<int>& undone, std::size_t n)
{
	return n < undone.size() ? undone[n] : -1;
}

void keepTrack(Track* track, Built* built)
{
	if (const int id = reusedId(built->undoneTracks, built->trackIds.size()); id >= 0) { track->setId(id); }
	built->trackIds.push_back(track->id());
}

MixerChannel* channelById(int id)
{
	Mixer* mixer = Engine::mixer();
	for (int i = 0; i < mixer->numChannels(); ++i)
	{
		if (mixer->mixerChannel(i)->id() == id) { return mixer->mixerChannel(i); }
	}
	return nullptr;
}

//! A new channel named @a name sending to @a to instead of the master.
int addChannel(const QString& name, int to, Built* built)
{
	Mixer* mixer = Engine::mixer();
	const int index = mixer->createChannel();
	MixerChannel* channel = mixer->mixerChannel(index);
	channel->m_name = name;
	if (to > 0)
	{
		mixer->deleteChannelSend(static_cast<mix_ch_t>(index), 0);
		mixer->createChannelSend(static_cast<mix_ch_t>(index), static_cast<mix_ch_t>(to));
	}
	if (const int id = reusedId(built->undoneChannels, built->channelIds.size()); id >= 0) { channel->setId(id); }
	built->channelIds.push_back(channel->id());
	return index;
}

//! One pad: a sampler track playing @a pad in @a folder, into a channel of its own sending to channel @a bus.
InstrumentTrack* buildPad(const Pad& pad, TrackFolder* folder, int bus, Built* built)
{
	// The channel before the track: a track's channel model takes the range of the channels there are when it is
	// made, so a channel made after it would be clamped away and the pad play straight into the bus.
	const int channel = addChannel(pad.name, bus, built);
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	if (track == nullptr) { return nullptr; }
	// The sample before the name: the sampler names a track after the first file it loads.
	if (Instrument* sampler = track->loadInstrument(drumPadSampler())) { sampler->loadFile(pad.sample); }
	track->setName(pad.name);
	track->baseNoteModel()->setValue(pad.key);
	track->mixerChannelModel()->setValue(channel);
	if (folder != nullptr) { track->setParentFolder(folder); }
	keepTrack(track, built);
	return track;
}

/*! Runs @a work with the project journal off. A build writes models (a pad's key) that would each push a step
 *  of their own: inside the command that is a second undo step for one edit, and inside a redo it empties
 *  the redo stack behind the step being redone. */
void unjournalled(const std::function<void()>& work)
{
	ProjectJournal* journal = Engine::projectJournal();
	const bool was = journal != nullptr && journal->isJournalling();
	if (journal != nullptr) { journal->setJournalling(false); }
	work();
	if (journal != nullptr) { journal->setJournalling(was); }
}

void build(const QString& name, const std::vector<Pad>& pads, Built* built)
{
	const int bus = addChannel(name, 0, built);
	auto* folder = dynamic_cast<TrackFolder*>(Track::create(Track::Type::Folder, Engine::getSong()));
	for (const Pad& pad : pads) { buildPad(pad, folder, bus, built); }
	if (folder != nullptr)
	{
		folder->setName(name);
		keepTrack(folder, built);
	}
}

void unbuild(Built* built)
{
	for (auto it = built->trackIds.rbegin(); it != built->trackIds.rend(); ++it)
	{
		if (Track* live = trackWithId(*it)) { removeTrack(live); }
	}
	built->undoneTracks = std::move(built->trackIds);
	built->trackIds.clear();
	// The highest-numbered channel first, so the others keep their numbers while they go.
	for (auto it = built->channelIds.rbegin(); it != built->channelIds.rend(); ++it)
	{
		if (MixerChannel* channel = channelById(*it)) { Engine::mixer()->deleteChannel(channel->index()); }
	}
	built->undoneChannels = std::move(built->channelIds);
	built->channelIds.clear();
}

//! Whether @a sample is a file this build's sampler plays; @a error names pad @a index when not.
bool sampleOk(const QString& sample, int index, ControlResult* error)
{
	if (!QFileInfo(sample).isFile())
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("pad %1: no sample file '%2'").arg(index).arg(sample));
		return false;
	}
	if (!SampleDecoder::decode(sample))
	{
		// The sampler would load nothing and the pad stay silent.
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("pad %1: '%2' is not an audio file this build reads")
			.arg(index).arg(sample));
		return false;
	}
	return true;
}

bool keyOk(int key, int index, ControlResult* error)
{
	if (key >= 0 && key <= 127) { return true; }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("pad %1: 'key' is 0..127").arg(index));
	return false;
}

//! The pad @a object describes (@a index-th of its list, keyed @a defaultKey when it names no key).
bool padOf(const QJsonObject& object, int index, int defaultKey, Pad* out, ControlResult* error)
{
	Pad pad;
	pad.sample = object.value(QStringLiteral("sample")).toString();
	pad.key = object.value(QStringLiteral("key")).toInt(defaultKey);
	pad.name = object.value(QStringLiteral("name")).toString(QFileInfo(pad.sample).completeBaseName());
	if (!sampleOk(pad.sample, index, error) || !keyOk(pad.key, index, error)) { return false; }
	*out = pad;
	return true;
}

bool padsOf(const QJsonArray& list, std::vector<Pad>* out, ControlResult* error)
{
	for (int i = 0; i < list.size(); ++i)
	{
		Pad pad;
		if (!padOf(list.at(i).toObject(), i, FirstPadKey + i, &pad, error)) { return false; }
		out->push_back(pad);
	}
	return true;
}

ControlResult drumRack(const QJsonObject& args)
{
	const QJsonArray list = args.value(QStringLiteral("pads")).toArray();
	if (list.isEmpty() || list.size() > MaxPads)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'pads' is 1..%1 pads").arg(MaxPads));
	}
	std::vector<Pad> pads;
	ControlResult error;
	if (!padsOf(list, &pads, &error)) { return error; }
	const QString name = args.value(QStringLiteral("name")).toString(QStringLiteral("Drum Rack"));
	auto built = std::make_shared<Built>();
	const auto make = [built, name, pads]() { unjournalled([&] { build(name, pads, built.get()); }); };
	addUndoStep([built]() { unbuild(built.get()); }, make);
	make();
	Engine::getSong()->setModified();
	QJsonArray tracks;
	for (const int id : built->trackIds) { tracks.append(trackId(id)); }
	QJsonArray channels;
	for (const int id : built->channelIds) { channels.append(channelIdOf(channelById(id))); }
	QJsonObject result{{QStringLiteral("folder"), tracks.isEmpty() ? QString() : tracks.last().toString()},
		{QStringLiteral("tracks"), tracks}, {QStringLiteral("bus"), channels.isEmpty() ? QString() : channels.first().toString()},
		{QStringLiteral("channels"), channels}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("pads"), list.size()}}, QStringLiteral("control.undo"), {},
		true, QStringLiteral("action checkpoint: the recorded undo step removes the rack's tracks and channels; its redo builds "
			"the rack again from the same pads")));
	return ControlResult::success(result);
}

//! The rack folder the 'track' argument names; nullptr with @a error filled when it is not a folder.
TrackFolder* rackOf(const QJsonObject& args, ControlResult* error)
{
	Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), error);
	if (track == nullptr) { return nullptr; }
	auto* folder = dynamic_cast<TrackFolder*>(track);
	if (folder == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("%1 is not a folder track (a drum rack is one)").arg(trackIdOf(track)));
	}
	return folder;
}

//! The pad the 'track' argument names; nullptr with @a error filled when it is not a sampler pad.
InstrumentTrack* padTrackOf(const QJsonObject& args, ControlResult* error)
{
	Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), error);
	if (track == nullptr) { return nullptr; }
	if (!isDrumPad(track))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("%1 is not a pad (an instrument track playing through the sampler)").arg(trackIdOf(track)));
		return nullptr;
	}
	return static_cast<InstrumentTrack*>(track);
}

QJsonObject padJson(InstrumentTrack* track)
{
	DrumPadState pad;
	readDrumPad(track, &pad);
	return pad.toJson();
}

ControlResult rackState(const QJsonObject& args)
{
	ControlResult error;
	TrackFolder* folder = rackOf(args, &error);
	return folder == nullptr ? error : ControlResult::success(drumRackState(folder));
}

//! The bus channel (by persistent id) the rack's pads send to, or -1 when they share none (the pad goes to the master).
int rackBusId(const TrackFolder* folder)
{
	ControlResult ignored;
	const MixerChannel* bus = resolveMixerChannel(drumRackState(folder).value(QStringLiteral("bus")).toString(), &ignored);
	return bus != nullptr ? bus->id() : -1;
}

void rebuildPad(const Pad& pad, int folderId, int busId, Built* built)
{
	const MixerChannel* bus = busId >= 0 ? channelById(busId) : nullptr;
	buildPad(pad, dynamic_cast<TrackFolder*>(trackWithId(folderId)), bus != nullptr ? bus->index() : 0, built);
}

ControlResult padAdd(const QJsonObject& args)
{
	ControlResult error;
	TrackFolder* folder = rackOf(args, &error);
	if (folder == nullptr) { return error; }
	const int count = static_cast<int>(drumPadsOf(folder).size());
	if (count >= MaxPads)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the rack has %1 pads, the most it holds").arg(MaxPads));
	}
	Pad pad;
	if (!padOf(args, count, std::min(127, FirstPadKey + count), &pad, &error)) { return error; }
	const int folderId = folder->id();
	const int busId = rackBusId(folder);
	auto built = std::make_shared<Built>();
	const auto make = [built, pad, folderId, busId]() { unjournalled([&] { rebuildPad(pad, folderId, busId, built.get()); }); };
	addUndoStep([built]() { unbuild(built.get()); }, make);
	make();
	Engine::getSong()->setModified();
	auto* track = dynamic_cast<InstrumentTrack*>(built->trackIds.empty() ? nullptr : trackWithId(built->trackIds.front()));
	QJsonObject result = track != nullptr ? padJson(track) : QJsonObject{};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("folder"), trackIdOf(folder)}},
		QStringLiteral("control.undo"), {}, true, QStringLiteral("action checkpoint: the recorded undo step removes the pad's "
			"track and channel; its redo builds the pad again under the same ids")));
	return ControlResult::success(result);
}

//! What track.drum_rack_pad_set was asked to change, checked before anything is written.
struct PadEdit
{
	QString name;
	QString sample;
	int key = -1;
};

bool padEditOf(const QJsonObject& args, PadEdit* edit, ControlResult* error)
{
	const bool hasName = args.contains(QStringLiteral("name"));
	const bool hasSample = args.contains(QStringLiteral("sample"));
	const bool hasKey = args.contains(QStringLiteral("key"));
	if (!hasName && !hasSample && !hasKey)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("give at least one of 'name', 'key', 'sample'"));
		return false;
	}
	edit->name = args.value(QStringLiteral("name")).toString();
	if (hasName && edit->name.trimmed().isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'name' must not be empty"));
		return false;
	}
	edit->sample = args.value(QStringLiteral("sample")).toString();
	edit->key = hasKey ? args.value(QStringLiteral("key")).toInt(-1) : -1;
	return (!hasSample || sampleOk(edit->sample, 0, error)) && (!hasKey || keyOk(edit->key, 0, error));
}

ControlResult padSet(const QJsonObject& args)
{
	ControlResult error;
	InstrumentTrack* track = padTrackOf(args, &error);
	PadEdit edit;
	if (track == nullptr || !padEditOf(args, &edit, &error)) { return error; }
	const QJsonObject before = padJson(track);
	// One undo step: the track's checkpoint carries its name, base note and sampler (sample and settings).
	addUndoStep(QVector<JournallingObject*>{track});
	if (!edit.sample.isEmpty()) { track->instrument()->loadFile(edit.sample); }  // first: the sampler may rename the track
	if (!edit.name.isEmpty()) { track->setName(edit.name); }
	if (edit.key >= 0) { track->baseNoteModel()->setValue(static_cast<float>(edit.key), false, false); }
	Engine::getSong()->setModified();
	QJsonObject result = padJson(track);
	result.insert(QStringLiteral("__transaction"), transactionPayload(before, QStringLiteral("control.undo"), {}, true,
		QStringLiteral("ProjectJournal (Track checkpoint): undo restores the pad's name, trigger key and sample")));
	return ControlResult::success(result);
}

ControlCommand trackCommand(const QString& verb, const QString& description,
	std::function<ControlResult(const QJsonObject&)> handler)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("track.") + verb;
	cmd.group = QStringLiteral("track");
	cmd.verb = verb;
	cmd.description = description;
	cmd.resultSchema = objectProperty();
	cmd.handler = std::move(handler);
	return cmd;
}

} // namespace

void registerDrumRackCommand(ControlRegistry& registry)
{
	ControlCommand cmd = trackCommand(QStringLiteral("drum_rack"), QStringLiteral("Build a drum rack: 'pads' [{sample (a file), "
		"key (0..127; 36 upward when left out), name}] - each pad a sampler track playing its sample at its key, inside one "
		"folder named 'name', and each playing into a mixer channel of its own (the pad's chain, for its inserts) that sends "
		"to one bus channel, the rack's output. Answers the folder, the pad tracks, the bus and the channels. One undo step."),
		drumRack);
	cmd.argsSchema = objectSchema({{QStringLiteral("name"), stringProperty()}, {QStringLiteral("pads"), arrayProperty()}},
		{QStringLiteral("pads")});
	cmd.mutating = true;
	registry.registerCommand(cmd);

	cmd = trackCommand(QStringLiteral("drum_rack_get_state"), QStringLiteral("Read a drum rack back: 'track' is its folder. "
		"Answers the folder, its name, the bus its pads send to and 'pads', each {track, name, sample (absolute path; empty "
		"when embedded), key (the trigger key: the note that plays the sample at its own pitch), channel (the pad's own "
		"chain), sends_to, playback {amp, start, end, loop_start, loop, reversed, stutter, interpolation}} - what the project "
		"saves. Writes nothing."), rackState);
	cmd.argsSchema = objectSchema({{QStringLiteral("track"), stringProperty()}}, {QStringLiteral("track")});
	registry.registerCommand(cmd);

	cmd = trackCommand(QStringLiteral("drum_rack_pad_add"), QStringLiteral("Add a sample pad to a drum rack: 'track' is the "
		"rack's folder, 'sample' a file, 'name' (the file's name when left out), 'key' (0..127; the next after 36 when left "
		"out). The pad is a sampler track in the folder with a channel of its own sending to the rack's bus. Answers the "
		"pad's state as track.drum_rack_get_state reads it. One undo step."), padAdd);
	cmd.argsSchema = objectSchema({{QStringLiteral("track"), stringProperty()}, {QStringLiteral("sample"), stringProperty()},
		{QStringLiteral("name"), stringProperty()}, {QStringLiteral("key"), integerProperty(0, 127)}},
		{QStringLiteral("track"), QStringLiteral("sample")});
	cmd.mutating = true;
	registry.registerCommand(cmd);

	cmd = trackCommand(QStringLiteral("drum_rack_pad_set"), QStringLiteral("Edit a drum-rack pad: 'track' is the pad, and any "
		"of 'name', 'key' (the trigger key, 0..127) and 'sample' (a file). Answers the pad's state as "
		"track.drum_rack_get_state reads it. One undo step."), padSet);
	cmd.argsSchema = objectSchema({{QStringLiteral("track"), stringProperty()}, {QStringLiteral("name"), stringProperty()},
		{QStringLiteral("key"), integerProperty(0, 127)}, {QStringLiteral("sample"), stringProperty()}},
		{QStringLiteral("track")});
	cmd.mutating = true;
	registry.registerCommand(cmd);
}

} // namespace lmms
