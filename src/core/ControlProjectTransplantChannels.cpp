/*
 * ControlProjectTransplantChannels.cpp - the mixer channels of tracks copied between projects
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
/*! 0.5.0 L09 (Q050-027): project.import_tracks, project.paste_tracks and reference.copy_tracks keep a track's
 *  mixer channel and the channel's hardware routing. A channel number means nothing in another project, so the
 *  track carries a description of its channel (see ControlProjectTransplant.h) and building the tracks resolves
 *  it by name - the lowest-numbered channel here with that name is reused as it is, else a channel is created
 *  from the description (name, colour, bus flag, volume, pan and its <hardwareio>: direct out, cue sends,
 *  hardware insert send and return). The tracks and the channels made for them are ONE undo step. */

#include <algorithm>
#include <memory>

#include <QDomDocument>
#include <QJsonArray>

#include "AudioEngine.h"
#include "ControlEdit.h"
#include "ControlProjectTransplant.h"
#include "ControlTrackCreation.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;

namespace control::transplant
{

namespace
{

constexpr const char* MixchAttribute = "mixch";

//! A model's plain value as a project file holds it: an attribute, or an element's 'value'.
double plainValue(const QDomElement& channel, const char* name, double fallback)
{
	if (channel.hasAttribute(QLatin1String(name))) { return channel.attribute(QLatin1String(name)).toDouble(); }
	const QDomElement child = channel.firstChildElement(QLatin1String(name));
	return child.hasAttribute(QStringLiteral("value")) ? child.attribute(QStringLiteral("value")).toDouble() : fallback;
}

//! The description of the source channel @a channel: only what a copied track brings along.
QString describeSource(const QDomElement& channel)
{
	QDomDocument doc;
	QDomElement out = doc.createElement(QStringLiteral("mixerchannel"));
	doc.appendChild(out);
	out.setAttribute(QStringLiteral("name"), channel.attribute(QStringLiteral("name")));
	if (channel.hasAttribute(QStringLiteral("color"))) { out.setAttribute(QStringLiteral("color"), channel.attribute(QStringLiteral("color"))); }
	out.setAttribute(QStringLiteral("bus"), channel.firstChildElement(QStringLiteral("bus")).isNull() ? 0 : 1);
	out.setAttribute(QStringLiteral("volume"), plainValue(channel, "volume", 1.0));
	out.setAttribute(QStringLiteral("pan"), plainValue(channel, "pan", 0.0));
	const QDomElement hardware = channel.firstChildElement(QStringLiteral("hardwareio"));
	if (!hardware.isNull()) { out.appendChild(doc.importNode(hardware, true)); }
	return doc.toString();
}

//! The channel numbered @a num in the source project's <mixer>, or a null element.
QDomElement sourceChannel(const QDomElement& mixer, int num)
{
	for (QDomElement channel = mixer.firstChildElement(QStringLiteral("mixerchannel")); !channel.isNull();
		channel = channel.nextSiblingElement(QStringLiteral("mixerchannel")))
	{
		if (channel.attribute(QStringLiteral("num")).toInt() == num) { return channel; }
	}
	return {};
}

//! Make @a channel what @a description says. The render thread reads these, so the engine's change lock is held.
void applyDescription(MixerChannel& channel, const QDomElement& description)
{
	AudioEngine* engine = Engine::audioEngine();
	engine->requestChangeInModel();
	channel.m_name = description.attribute(QStringLiteral("name"));
	channel.m_volumeModel.setDisplayName(channel.m_name + QLatin1Char('>') + Mixer::tr("Volume"));
	channel.m_panModel.setDisplayName(channel.m_name + QLatin1Char('>') + Mixer::tr("Pan"));
	channel.m_muteModel.setDisplayName(channel.m_name + QLatin1Char('>') + Mixer::tr("Mute"));
	channel.m_soloModel.setDisplayName(channel.m_name + QLatin1Char('>') + Mixer::tr("Solo"));
	if (description.hasAttribute(QStringLiteral("color"))) { channel.setColor(QColor{description.attribute(QStringLiteral("color"))}); }
	channel.setIsBus(description.attribute(QStringLiteral("bus")).toInt() != 0);
	channel.m_volumeModel.setValue(static_cast<float>(plainValue(description, "volume", 1.0)));
	channel.m_panModel.setValue(static_cast<float>(plainValue(description, "pan", 0.0)));
	channel.m_hardwareIo.load(description, engine->framesPerPeriod());
	engine->doneChangeInModel();
	emit Engine::mixer()->dataChanged();
}

//! The channel here that @a need names: the lowest-numbered non-master one of that name, else a new one made from it
//! (its id appended to @a created).
int resolveNeed(const ChannelNeed& need, std::vector<int>* created)
{
	Mixer* mixer = Engine::mixer();
	for (int i = 1; i < static_cast<int>(mixer->numChannels()); ++i)
	{
		if (mixer->mixerChannel(i)->m_name == need.name) { return i; }
	}
	QDomDocument doc;
	doc.setContent(need.xml);
	const int index = mixer->createChannel();
	applyDescription(*mixer->mixerChannel(index), doc.documentElement());
	created->push_back(mixer->mixerChannel(index)->id());
	return index;
}

//! The index of the live channel whose stable id is @a id, or -1.
int indexOfChannelId(int id)
{
	Mixer* mixer = Engine::mixer();
	for (int i = 1; i < static_cast<int>(mixer->numChannels()); ++i)
	{
		if (mixer->mixerChannel(i)->id() == id) { return i; }
	}
	return -1;
}

//! Delete the channels of @a ids (highest number first, so the others keep theirs while the loop runs).
void deleteChannels(std::vector<int>* ids)
{
	std::vector<int> indices;
	for (const int id : *ids) { if (const int index = indexOfChannelId(id); index > 0) { indices.push_back(index); } }
	std::sort(indices.rbegin(), indices.rend());
	for (const int index : indices) { Engine::mixer()->deleteChannel(index); }
	ids->clear();
}

//! Delete the channels @a made created that no track of it plays into (a document that made no track - a type this
//! build cannot create - leaves its channel with nothing to serve).
void dropUnservedChannels(Built* made)
{
	std::vector<int> unused;
	for (const int id : made->channels)
	{
		const int index = indexOfChannelId(id);
		const bool served = std::any_of(made->tracks.begin(), made->tracks.end(), [index](int trackId) {
			Track* live = trackWithId(trackId);
			IntModel* model = live != nullptr ? live->mixerChannelModel() : nullptr;
			return model != nullptr && model->value() == index;
		});
		if (!served) { unused.push_back(id); }
	}
	deleteChannels(&unused);
	made->channels.erase(std::remove_if(made->channels.begin(), made->channels.end(),
		[](int id) { return indexOfChannelId(id) < 0; }), made->channels.end());
}

//! Point the document @a element at the channel its need @a need stands for, resolving (or creating) it once.
void settleChannel(QDomElement element, const TrackPlan& plan, int need, std::vector<int>* resolved, std::vector<int>* created)
{
	if (need < 0 || need >= static_cast<int>(plan.needs.size())) { return; }
	int& index = (*resolved)[static_cast<std::size_t>(need)];
	if (index < 0) { index = resolveNeed(plan.needs[static_cast<std::size_t>(need)], created); }
	channelSettingsOf(element).setAttribute(QLatin1String(MixchAttribute), index);
}

//! The tracks of @a plan in the song, each on its channel; @a made receives them and the channels created for them.
void build(const TrackPlan& plan, Built* made)
{
	made->tracks.clear();
	made->channels.clear();
	std::vector<int> resolved(plan.needs.size(), -1);
	for (int i = 0; i < plan.documents.size(); ++i)
	{
		QDomDocument doc;
		doc.setContent(plan.documents[i]);
		QDomElement element = doc.documentElement();
		settleChannel(element, plan, static_cast<std::size_t>(i) < plan.need.size() ? plan.need[static_cast<std::size_t>(i)] : -1,
			&resolved, &made->channels);
		if (Track* track = Track::create(element, Engine::getSong())) { made->tracks.push_back(track->id()); }
	}
	dropUnservedChannels(made);
}

} // namespace

QDomElement channelSettingsOf(const QDomElement& track)
{
	for (QDomElement child = track.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
	{
		if (child.hasAttribute(QLatin1String(MixchAttribute))) { return child; }
	}
	return {};
}

//! Whether @a track is of a type that plays into a mixer channel of its own choosing.
static bool choosesItsChannel(const QDomElement& track)
{
	const int type = track.attribute(QStringLiteral("type")).toInt();
	return type == static_cast<int>(Track::Type::Instrument) || type == static_cast<int>(Track::Type::Sample);
}

void noteLiveChannel(QDomElement track, QDomElement mixer, const QString& attribute)
{
	QDomElement settings = channelSettingsOf(track);
	if (settings.isNull() || !choosesItsChannel(track)) { return; }
	settings.setAttribute(attribute, settings.attribute(QLatin1String(MixchAttribute)));
	describeLiveChannel(mixer, settings.attribute(QLatin1String(MixchAttribute)).toInt());
}

int ChannelNeeds::noteFor(QDomElement track, const QString& attribute)
{
	QDomElement settings = channelSettingsOf(track);
	bool ok = false;
	const int num = settings.attribute(attribute).toInt(&ok);
	if (attribute != QLatin1String(MixchAttribute)) { settings.removeAttribute(attribute); }
	if (!choosesItsChannel(track) || !ok || num <= 0) { return -1; }
	if (const auto known = m_seen.find(num); known != m_seen.end()) { return known->second; }
	const QDomElement channel = sourceChannel(m_mixer, num);
	if (channel.isNull() || channel.attribute(QStringLiteral("name")).isEmpty()) { return -1; }
	m_plan->needs.push_back({channel.attribute(QStringLiteral("name")), describeSource(channel)});
	m_seen[num] = static_cast<int>(m_plan->needs.size()) - 1;
	return m_seen[num];
}

void describeLiveChannel(QDomElement parent, int num)
{
	Mixer* mixer = Engine::mixer();
	if (num <= 0 || num >= static_cast<int>(mixer->numChannels())) { return; }
	for (QDomElement known = parent.firstChildElement(QStringLiteral("mixerchannel")); !known.isNull();
		known = known.nextSiblingElement(QStringLiteral("mixerchannel")))
	{
		if (known.attribute(QStringLiteral("num")).toInt() == num) { return; }
	}
	MixerChannel* channel = mixer->mixerChannel(num);
	QDomDocument doc = parent.ownerDocument();
	QDomElement out = doc.createElement(QStringLiteral("mixerchannel"));
	parent.appendChild(out);
	out.setAttribute(QStringLiteral("num"), num);
	out.setAttribute(QStringLiteral("name"), channel->m_name);
	if (const auto& color = channel->color()) { out.setAttribute(QStringLiteral("color"), color->name()); }
	if (channel->isBus()) { out.appendChild(doc.createElement(QStringLiteral("bus"))); }
	out.setAttribute(QStringLiteral("volume"), static_cast<double>(channel->m_volumeModel.value()));
	out.setAttribute(QStringLiteral("pan"), static_cast<double>(channel->m_panModel.value()));
	channel->m_hardwareIo.save(doc, out);
}

Built buildAsOneStep(const TrackPlan& plan)
{
	auto made = std::make_shared<Built>();
	addUndoStep([made]() {
		for (auto it = made->tracks.rbegin(); it != made->tracks.rend(); ++it) { if (Track* live = trackWithId(*it)) { removeTrack(live); } }
		made->tracks.clear();
		deleteChannels(&made->channels);
	}, [made, plan]() { build(plan, made.get()); });
	build(plan, made.get());
	return *made;
}

QJsonObject madeResult(const QString& key, const Built& built, int asked)
{
	QJsonArray ids;
	for (const int id : built.tracks) { ids.append(trackId(id)); }
	QJsonObject result{{key, ids}, {QStringLiteral("count"), ids.size()}};
	if (ids.size() < asked) { result.insert(QStringLiteral("skipped"), asked - static_cast<int>(ids.size())); }
	QJsonArray channels;
	for (const int id : built.channels) { channels.append(channelId(id)); }
	result.insert(QStringLiteral("created_channels"), channels);
	return result;
}

} // namespace control::transplant

} // namespace lmms
