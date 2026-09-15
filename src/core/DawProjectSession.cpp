/*
 * DawProjectSession.cpp - the session and the DAWproject model, both ways.
 *
 * The engine-coupling half of feature row 37 (docs/FEATURE-LIST-0.3.0.md
 * section 7). It is its own translation unit for the reason the SMF module's
 * reader is: gate 7 measures a file, not a feature.
 *
 * WHAT IT READS, and where each number comes from - the point being that every
 * value is the ENGINE's own accessor rather than a second implementation of it:
 *
 *   tempo, metre       Song::getTempo(), Song::getTimeSigModel()
 *   the map            Song::tempoMap().map(), TempoMapEvent for event
 *   tracks             Song::tracks(), in container order
 *   the track's type   control::trackTypeNameOf(Track::type())
 *   the track's name   Track::name(), Track::color()
 *   clips              Track::getClips(), MidiClip::notes()
 *   clip geometry      Clip::startPosition(), length(), startTimeOffset()
 *   notes              Note::pos(), length(), key(), getVolume()
 *   mixer strip        Track::mixerChannelModel() -> Mixer::mixerChannel(n),
 *                      then MixerChannel::m_volumeModel / m_muteModel /
 *                      m_soloModel / m_name (the same four the mixer group's
 *                      own get_state publishes)
 *   pan                InstrumentTrack::panningModel() /
 *                      SampleTrack::panningModel() - LOSSY #6, because LMMS'
 *                      MixerChannel has no pan of its own
 *
 * WHAT IT WRITES BACK. `applyDawProjectModel` REPLACES the session: it clears
 * every track, writes the global tempo and metre, replaces the tempo map and
 * creates one track per model track with its clips and notes, then applies each
 * track's mixer strip to the channel the engine assigned it. The caller owns the
 * inverse - see the `dawproject.import` handler, which captures the whole
 * previous session as one recorded action before calling this.
 *
 * A NOTE ON TRACK TYPES. Track::create() supports Instrument, Pattern, Sample,
 * Automation, HiddenAutomation and Folder; Event and Video have no
 * implementation (src/core/Track.cpp's switch leaves them null) - the same
 * limit the project loader has. A model track naming a type the engine cannot
 * create is a refusal here rather than a silently missing track.
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

#include <QColor>
#include <QString>

#include <cmath>
#include <map>

#include "lmmsversion.h"

#include "Clip.h"
#include "ControlEdit.h"
#include "DawProjectInterchange.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MeterModel.h"
#include "MidiClip.h"
#include "Mixer.h"
#include "Note.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TempoMap.h"
#include "TimePos.h"
#include "Track.h"
#include "TrackContainer.h"

namespace lmms
{
namespace interchange
{

namespace
{

QString colorText(const std::optional<QColor>& color)
{
	if (!color.has_value() || !color->isValid()) { return QString(); }
	return color->name(QColor::HexRgb);
}

//! The engine's own panning scale is -100..100 with 0 centre (include/panning.h);
//! the format's Pan is normalized 0..1 with 0.5 centre. Both are linear and
//! centred at the same place, so the conversion is exact in both directions.
double panFromEngine(double panning) { return (panning + 100.0) / 200.0; }
double panToEngine(double pan) { return pan * 200.0 - 100.0; }

//! The Track::Type a model track asks to be created as. The names are the
//! canonical lowercase ones control::trackTypeNameOf emits; the comparison is
//! case-insensitive so a hand-built model that spells one differently still
//! imports as the type it names rather than silently as an instrument.
Track::Type trackTypeFromName(const QString& typeName)
{
	const auto is = [&typeName](const char* name)
	{
		return typeName.compare(QLatin1String(name), Qt::CaseInsensitive) == 0;
	};
	if (is("pattern")) { return Track::Type::Pattern; }
	if (is("sample")) { return Track::Type::Sample; }
	if (is("automation")) { return Track::Type::Automation; }
	if (is("hidden_automation") || is("hiddenautomation"))
	{
		return Track::Type::HiddenAutomation;
	}
	if (is("folder")) { return Track::Type::Folder; }
	if (is("video")) { return Track::Type::Video; }
	return Track::Type::Instrument;
}

//! The product's own creation path for a type: a Pattern track goes through
//! Song::addPatternTrack (which the container needs), everything else through
//! Track::create - the same two arms the arrangement group uses.
Track* createTrackOfType(Track::Type type, Song* song)
{
	if (type == Track::Type::Pattern) { song->addPatternTrack(); return song->tracks().back(); }
	return Track::create(type, song);
}

//! The panning model a track type has, or nullptr. This is the LOSSY #6 seam:
//! MixerChannel carries volume, mute and solo but no pan, so the value the
//! format's <Pan> carries comes from the TRACK - and only InstrumentTrack
//! EXPOSES its panning model. SampleTrack has one (m_panningModel) but no
//! public accessor, so a sample track's pan is not reachable from here and is
//! reported rather than guessed at through a private member.
FloatModel* panningModelForTrack(Track* track)
{
	if (auto* instrument = dynamic_cast<InstrumentTrack*>(track))
	{
		return instrument->panningModel();
	}
	return nullptr;
}

//! The volume model a track type exposes, or nullptr - the same seam as the
//! panning one, and for the same reason.
FloatModel* volumeModelForTrack(Track* track)
{
	if (auto* instrument = dynamic_cast<InstrumentTrack*>(track))
	{
		return instrument->volumeModel();
	}
	return nullptr;
}

/*! LMMS has TWO volume scales and the format has ONE. A MixerChannel's fader is
 *  0..2 with 1.0 at unity (what `mixer.set_volume` takes); an
 *  InstrumentTrack's own volume is 0..200 with 100 at unity
 *  (include/volume.h: MinVolume 0, MaxVolume 200, DefaultVolume 100). The
 *  format's <Volume> is `unit="linear"` min 0 max 2, so the mixer fader is
 *  written as-is and the TRACK volume is divided by this. Getting it wrong would
 *  put every track 100x too loud, which is exactly the kind of mapping the
 *  release contract asks to have written down. */
constexpr double TrackVolumeUnity = 100.0;

QString roleForMixerChannel(MixerChannel* channel)
{
	if (channel->index() == 0) { return QStringLiteral("master"); }
	if (channel->isBus()) { return QStringLiteral("submix"); }
	return QStringLiteral("regular");
}

} // namespace

DawProjectModel dawProjectModelFromSong(Song* song, DawProjectLossReport* loss)
{
	DawProjectLossReport local;
	DawProjectModel model;
	model.formatVersion = QString::fromLatin1(DawProjectVersionAttribute);
	model.applicationName = QStringLiteral("Zene Studio");
	model.applicationVersion = QString::fromUtf8(LMMS_VERSION);
	model.tempo = static_cast<double>(song->getTempo());
	model.numerator = song->getTimeSigModel().numeratorModel().value();
	model.denominator = song->getTimeSigModel().denominatorModel().value();

	// The mixer's own strips: one bare <Channel> per MixerChannel, in mixer
	// order, carrying the fader, its mute and its solo. A track POINTS at the
	// strip it feeds (below) rather than owning it, because an LMMS MixerChannel
	// is a summing strip several tracks may share.
	Mixer* mixer = Engine::mixer();
	for (int index = 0; index < static_cast<int>(mixer->numChannels()); index++)
	{
		MixerChannel* channel = mixer->mixerChannel(index);
		DawProjectMixerChannel entry;
		entry.index = index;
		entry.id = QStringLiteral("mixer%1").arg(index);
		entry.name = channel->m_name;
		entry.role = roleForMixerChannel(channel);
		entry.volume = static_cast<double>(channel->m_volumeModel.value());
		entry.mute = channel->m_muteModel.value();
		entry.solo = channel->m_soloModel.value();
		model.mixerChannels.append(entry);
	}

	// Which mixer strips more than one track feeds: the format joins by a single
	// IDREF, so the SHARING is what cannot be carried (LOSSY #7).
	std::map<int, int> channelUse;

	for (Track* track : song->tracks())
	{
		DawProjectTrack entry;
		entry.typeName = control::trackTypeNameOf(track->type());
		entry.contentType = dawProjectContentTypeForType(entry.typeName,
			&entry.lostContentType);
		if (entry.lostContentType) { local.unmappedTrackTypes++; }
		entry.name = track->name();
		entry.color = colorText(track->color());
		entry.channelId = QStringLiteral("strip%1").arg(model.tracks.size() + 1);
		entry.mute = track->isMuted();
		entry.solo = track->isSolo();

		if (FloatModel* volume = volumeModelForTrack(track))
		{
			entry.hasVolume = true;
			entry.volume = static_cast<double>(volume->value()) / TrackVolumeUnity;
		}

		if (IntModel* channelModel = track->mixerChannelModel())
		{
			entry.mixerChannelIndex = channelModel->value();
			if (entry.mixerChannelIndex >= 0
				&& entry.mixerChannelIndex < model.mixerChannels.size())
			{
				entry.destinationChannelId =
					model.mixerChannels[entry.mixerChannelIndex].id;
			}
			channelUse[entry.mixerChannelIndex]++;
		}
		else { local.mixerSharingLost++; }

		if (FloatModel* panning = panningModelForTrack(track))
		{
			entry.pan = panFromEngine(static_cast<double>(panning->value()));
			entry.hasPan = true;
		}
		else { local.panNotWritten++; }

		for (Clip* clip : track->getClips())
		{
			DawProjectClip clipEntry;
			clipEntry.time = dawProjectBeatsFromTicks(clip->startPosition().getTicks());
			clipEntry.duration = dawProjectBeatsFromTicks(clip->length().getTicks());
			clipEntry.playStart = dawProjectBeatsFromTicks(
				clip->startTimeOffset().getTicks());
			clipEntry.name = clip->name();
			clipEntry.color = colorText(clip->color());
			if (!clip->clipEdits().isNeutral())
			{
				// Fades / clip gain exist in the format and are deliberately
				// not written (LOSSY #1): they are counted so a session that
				// uses them says so rather than exporting as if it did not.
				local.fadesNotWritten++;
			}

			if (auto* midi = dynamic_cast<MidiClip*>(clip))
			{
				for (const Note* note : midi->notes())
				{
					DawProjectNote noteEntry;
					noteEntry.time = dawProjectBeatsFromTicks(note->pos().getTicks());
					noteEntry.duration = dawProjectBeatsFromTicks(
						note->length().getTicks());
					noteEntry.channel = 0;
					noteEntry.key = note->key();
					// LMMS' note volume is 0..200 (include/volume.h); the
					// format's vel is 0..1. MaxVolume is the format's 1.0.
					noteEntry.vel = static_cast<double>(note->getVolume()) / MaxVolume;
					noteEntry.rel = noteEntry.vel;
					clipEntry.notes.append(noteEntry);
				}
				entry.clips.append(clipEntry);
			}
			else if (entry.typeName == QLatin1String("sample"))
			{
				local.audioClipsSkipped++;
			}
			else { local.automationClipsSkipped++; }
		}

		if (track->type() == Track::Type::Folder) { local.folderChildrenLost++; }
		model.tracks.append(entry);
	}

	// Tracks sharing one mixer channel: the count of the tracks beyond the first
	// on each channel. A channel no track is assigned to is not a loss (it is a
	// bus or an unused strip, and the format can express it as a bare Channel).
	for (const auto& [index, count] : channelUse)
	{
		Q_UNUSED(index);
		if (count > 1) { local.mixerSharingLost += count - 1; }
	}

	// LMMS' mixer routes other than the implicit "every channel sums into
	// master" are not expressible in one IDREF (LOSSY #7).
	local.routingLost = static_cast<int>(Engine::mixer()->m_mixerRoutes.size());

	// The map's two arms. An event carrying both halves becomes two points
	// (LOSSY #2) - counted, because a reader of the file then sees two points
	// where the session had one event.
	for (const TempoMapEvent& event : song->tempoMap().map().all())
	{
		if (event.hasTempo)
		{
			DawProjectPoint point;
			point.time = dawProjectBeatsFromTicks(event.tick);
			point.value = static_cast<double>(event.tempo);
			model.tempoPoints.append(point);
		}
		if (event.hasTimeSignature)
		{
			DawProjectPoint point;
			point.time = dawProjectBeatsFromTicks(event.tick);
			point.isMeter = true;
			point.numerator = event.numerator;
			point.denominator = event.denominator;
			model.meterPoints.append(point);
		}
		if (event.hasTempo && event.hasTimeSignature)
		{
			model.splitEvents++;
			local.splitMapEvents++;
		}
	}

	if (loss != nullptr) { *loss = local; }
	return model;
}

bool applyDawProjectModel(Song* song, const DawProjectModel& model, QString* error)
{
	song->clearAllTracks();
	song->setTempo(static_cast<bpm_t>(model.tempo));
	song->getTimeSigModel().numeratorModel().setValue(model.numerator);
	song->getTimeSigModel().denominatorModel().setValue(model.denominator);

	// The map: the two timelines merged back onto their tick list, per property,
	// which is how the map itself merges (docs/TEMPO-MAP.md decision 1).
	TempoMap rebuilt;
	for (const DawProjectPoint& point : model.tempoPoints)
	{
		TempoMapEvent event;
		event.tick = static_cast<tick_t>(dawProjectTicksFromBeats(point.time));
		event.hasTempo = true;
		event.tempo = static_cast<int>(std::floor(point.value + 0.5));
		if (!rebuilt.addEvent(event))
		{
			if (error)
			{
				*error = QStringLiteral("the file's tempo automation does not fit the engine's "
					"map (at most %1 events, tempos %2..%3)")
					.arg(TempoMap::MaxEvents).arg(DawProjectMinTempo).arg(DawProjectMaxTempo);
			}
			return false;
		}
	}
	for (const DawProjectPoint& point : model.meterPoints)
	{
		TempoMapEvent event;
		event.tick = static_cast<tick_t>(dawProjectTicksFromBeats(point.time));
		event.hasTimeSignature = true;
		event.numerator = point.numerator;
		event.denominator = point.denominator;
		if (!rebuilt.addEvent(event))
		{
			if (error)
			{
				*error = QStringLiteral("the file's time-signature automation does not fit the "
					"engine's map (at most %1 events)").arg(TempoMap::MaxEvents);
			}
			return false;
		}
	}
	if (rebuilt.size() > 0) { rebuilt.setActive(true); }
	song->tempoMap().edit([&rebuilt](TempoMap& map) { map = rebuilt; return true; });

	// The mixer's strips, by POSITION: the bare <Channel> elements of <Structure>
	// are the mixer's own channels in mixer order, so the n-th one is LMMS'
	// channel n. Creating what is missing is how a session with a one-channel
	// mixer takes a file that carries more.
	Mixer* mixer = Engine::mixer();
	for (int position = 0; position < model.mixerChannels.size(); position++)
	{
		while (static_cast<int>(mixer->numChannels()) <= position) { mixer->createChannel(); }
		MixerChannel* channel = mixer->mixerChannel(position);
		const DawProjectMixerChannel& entry = model.mixerChannels[position];
		if (!entry.name.isEmpty()) { channel->m_name = entry.name; }
		channel->m_volumeModel.setValue(static_cast<float>(entry.volume));
		channel->m_muteModel.setValue(entry.mute);
		channel->m_soloModel.setValue(entry.solo);
	}

	for (const DawProjectTrack& entry : model.tracks)
	{
		Track* track = createTrackOfType(trackTypeFromName(entry.typeName), song);
		if (track == nullptr)
		{
			if (error)
			{
				*error = QStringLiteral("the engine cannot create a '%1' track")
					.arg(entry.typeName);
			}
			return false;
		}
		if (!entry.name.isEmpty()) { track->setName(entry.name); }
		if (!entry.color.isEmpty()) { track->setColor(QColor(entry.color)); }
		track->setMuted(entry.mute);
		track->setSolo(entry.solo);

		if (entry.hasVolume)
		{
			if (FloatModel* volume = volumeModelForTrack(track))
			{
				volume->setValue(static_cast<float>(entry.volume * TrackVolumeUnity));
			}
		}

		// The strip this track feeds: the format's one routing fact that maps
		// onto LMMS. `mixerChannelModel()` returns nullptr for a track type with
		// no channel (a folder), which is not a failure - the format has no way
		// to say a track has none either.
		if (!entry.destinationChannelId.isEmpty())
		{
			int destination = -1;
			for (int position = 0; position < model.mixerChannels.size(); position++)
			{
				if (model.mixerChannels[position].id == entry.destinationChannelId)
				{
					destination = position;
					break;
				}
			}
			if (destination >= 0 && destination < static_cast<int>(mixer->numChannels()))
			{
				if (IntModel* channelModel = track->mixerChannelModel())
				{
					channelModel->setValue(destination);
				}
			}
		}

		if (entry.hasPan)
		{
			if (FloatModel* panning = panningModelForTrack(track))
			{
				panning->setValue(static_cast<float>(panToEngine(entry.pan)));
			}
		}

		for (const DawProjectClip& clipEntry : entry.clips)
		{
			Clip* clip = track->createClip(TimePos(
				static_cast<tick_t>(dawProjectTicksFromBeats(clipEntry.time))));
			if (clip == nullptr) { continue; }
			if (!clipEntry.name.isEmpty()) { clip->setName(clipEntry.name); }
			if (!clipEntry.color.isEmpty()) { clip->setColor(QColor(clipEntry.color)); }
			clip->setStartTimeOffset(TimePos(
				static_cast<tick_t>(dawProjectTicksFromBeats(clipEntry.playStart))));
			if (auto* midi = dynamic_cast<MidiClip*>(clip))
			{
				for (const DawProjectNote& noteEntry : clipEntry.notes)
				{
					Note note(TimePos(static_cast<tick_t>(
							dawProjectTicksFromBeats(noteEntry.duration))),
						TimePos(static_cast<tick_t>(
							dawProjectTicksFromBeats(noteEntry.time))),
						noteEntry.key,
						static_cast<volume_t>(std::lround(
							noteEntry.vel * MaxVolume)));
					// FALSE: the clip does not quantise a note an agent placed by
					// tick, and the quantiser reads the piano roll's GUI, which a
					// headless instance does not have.
					midi->addNote(note, false);
				}
			}
			const tick_t length = static_cast<tick_t>(
				dawProjectTicksFromBeats(clipEntry.duration));
			if (length > 0) { clip->changeLength(TimePos(length)); }
		}
	}
	return true;
}

} // namespace interchange
} // namespace lmms
