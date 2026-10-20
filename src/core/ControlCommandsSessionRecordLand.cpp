/*
 * ControlCommandsSessionRecordLand.cpp - Arrangement Record's one writing verb:
 *                                        the pass that turns the recorded
 *                                        performance into arrangement clips
 *                                        (SPEC-zene-studio §4.1; board task
 *                                        #641, the #596 half).
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

/* WHY THIS IS ITS OWN TRANSLATION UNIT. The group measured 518 lines against
 * this fork's 500-line file-length ratchet as one file, and a fork-new file over
 * the limit fails the gate - so the tap (arm / status / back-to-arrangement) and
 * the pass are two files, on the same read/edit seam the automation, warp, vca
 * and chain-preset groups use. The pairing the two share is in
 * ControlCommandsSessionRecordInternal.h.
 *
 * WHAT A LANDED CLIP CARRIES. Position and length are the recorded span. MIDI
 * notes are copied from the referenced PatternStore column into an instrument
 * clip, and audio cells are loaded into a sample clip. A content/track mismatch
 * or unreadable audio source is refused before the ring is consumed.
 *
 * THE INVERSE. The pass creates clips over ONE composite journal checkpoint
 * covering every touched track - the clip.add and midi.retro_capture_to_clip
 * shape - so ONE control.undo takes the whole pass back. `before`/`inverse` in
 * the recorded transaction describe the FIRST clip the pass created, which is a
 * complete and exact inverse for that clip (clip.delete removes it); the
 * mechanism names the checkpoint that carries the rest, because a pass creates
 * one clip per completed launch and a transaction carries one inverse.
 */

#include <memory>
#include <utility>
#include <vector>

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "ControlCommandsSessionRecordInternal.h"
#include "ControlCommandsSessionShared.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "SessionArrangementRecorder.h"
#include "SessionModel.h"
#include "SessionScheduler.h"
#include "Song.h"

namespace lmms
{

using namespace control;
using namespace sessioncontrol;
using namespace sessionrecord;

namespace
{

QString trackTypeName(Track::Type type)
{
	switch (type)
	{
	case Track::Type::Instrument: return QStringLiteral("instrument");
	case Track::Type::Pattern: return QStringLiteral("pattern");
	case Track::Type::Sample: return QStringLiteral("sample");
	case Track::Type::Event: return QStringLiteral("event");
	case Track::Type::Video: return QStringLiteral("video");
	case Track::Type::Automation: return QStringLiteral("automation");
	case Track::Type::HiddenAutomation: return QStringLiteral("hidden automation");
	case Track::Type::Folder: return QStringLiteral("folder");
	case Track::Type::Count: break;
	}
	return QStringLiteral("unknown");
}

ControlResult validateOpenStarts(const OpenStart* open, int openCount)
{
	QStringList stillPlaying;
	for (int i = 0; i < openCount; ++i)
	{
		stillPlaying << QStringLiteral("(track %1, scene %2) from tick %3")
			.arg(open[i].track).arg(open[i].scene).arg(open[i].tick);
	}
	return ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("%1 recorded launch(es) have not stopped yet: %2. End the "
			"session (session.stop_all, or session.back_to_arrangement) and land "
			"again - nothing was consumed.").arg(openCount)
			.arg(stillPlaying.join(QStringLiteral(", "))));
}

bool pairHasSlot(const Song& song, const SessionModel& model, const LandPair& pair)
{
	return pair.track >= 0 && pair.track < static_cast<int>(song.tracks().size())
		&& pair.track < model.trackCount() && pair.scene >= 0 && pair.scene < model.sceneCount();
}

bool canLandMidiOn(Track* track, int pattern)
{
	if (dynamic_cast<InstrumentTrack*>(track) != nullptr) { return true; }
	auto* patternTrack = dynamic_cast<PatternTrack*>(track);
	return patternTrack != nullptr && patternTrack->patternIndex() == pattern;
}

ControlResult validateMidiDestination(int pattern, Track* track, const LandPair& pair)
{
	if (canLandMidiOn(track, pattern)) { return ControlResult::success(); }
	return ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("cannot land MIDI pattern %1 from (track %2, scene %3) onto a %4 track; "
			"this destination cannot hold copied MIDI notes, nothing was consumed")
			.arg(pattern).arg(pair.track).arg(pair.scene)
			.arg(track == nullptr ? QStringLiteral("missing") : trackTypeName(track->type())));
}

ControlResult validateAudioDestination(const ClipSlot& slot, Track* track, const LandPair& pair,
	std::shared_ptr<const SampleBuffer>* audioSamples, int pairIndex,
	QHash<QString, std::shared_ptr<const SampleBuffer>>& audioCache)
{
	if (dynamic_cast<SampleTrack*>(track) == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("cannot land audio cell from (track %1, scene %2) onto a %3 track; "
				"this destination cannot hold copied audio, nothing was consumed")
				.arg(pair.track).arg(pair.scene)
				.arg(track == nullptr ? QStringLiteral("missing") : trackTypeName(track->type())));
	}
	const QString source = slot.audioSource();
	if (source.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("cannot land audio cell from (track %1, scene %2): its audio source is empty; "
				"nothing was consumed").arg(pair.track).arg(pair.scene));
	}
	if (!audioCache.contains(source)) { audioCache.insert(source, SampleBuffer::fromFile(source)); }
	audioSamples[pairIndex] = audioCache.value(source);
	if (!audioSamples[pairIndex] || audioSamples[pairIndex]->empty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("cannot land audio source '%1' from (track %2, scene %3): it is missing, "
				"unreadable, or unsupported; nothing was consumed")
				.arg(source).arg(pair.track).arg(pair.scene));
	}
	return ControlResult::success();
}

ControlResult validateRecordedAudioDestination(const LandPair& pair, Track* track,
	std::shared_ptr<const SampleBuffer>* audioSamples, int pairIndex,
	QHash<const SampleBuffer*, std::shared_ptr<const SampleBuffer>>& audioCache)
{
	if (dynamic_cast<SampleTrack*>(track) == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("cannot land recorded audio cell from (track %1, scene %2) onto a %3 track; "
				"this destination cannot hold copied audio, nothing was consumed")
				.arg(pair.track).arg(pair.scene)
				.arg(track == nullptr ? QStringLiteral("missing") : trackTypeName(track->type())));
	}
	if (pair.audioClip == nullptr || pair.audioClip->empty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("cannot land the recorded audio cell from (track %1, scene %2): its launched "
				"audio is unavailable; nothing was consumed").arg(pair.track).arg(pair.scene));
	}
	if (!audioCache.contains(pair.audioClip))
	{
		std::vector<SampleBuffer::value_type> frames(pair.audioClip->data(),
			pair.audioClip->data() + pair.audioClip->size());
		audioCache.insert(pair.audioClip, std::make_shared<const SampleBuffer>(
			std::move(frames), static_cast<int>(pair.audioClip->sampleRate()), pair.audioClip->audioFile()));
	}
	audioSamples[pairIndex] = audioCache.value(pair.audioClip);
	return ControlResult::success();
}

ControlResult validatePairContent(const Song& song, const SessionModel& model, const LandPair& pair,
	int pairIndex, std::shared_ptr<const SampleBuffer>* audioSamples,
	QHash<QString, std::shared_ptr<const SampleBuffer>>& audioCache,
	QHash<const SampleBuffer*, std::shared_ptr<const SampleBuffer>>& recordedAudioCache)
{
	using ContentKind = SessionArrangementRecorder::Event::ContentKind;
	Track* track = song.tracks()[static_cast<std::size_t>(pair.track)];
	if (pair.contentKind == ContentKind::Midi)
	{
		return validateMidiDestination(pair.patternId, track, pair);
	}
	if (pair.contentKind == ContentKind::Audio)
	{
		return validateRecordedAudioDestination(pair, track, audioSamples, pairIndex, recordedAudioCache);
	}
	if (pair.contentKind == ContentKind::Empty) { return ControlResult::success(); }
	const ClipSlot& slot = model.slot(pair.track, pair.scene);
	if (slot.type() == ClipSlot::Type::Midi) { return validateMidiDestination(slot.patternId(), track, pair); }
	if (slot.type() == ClipSlot::Type::Audio)
	{
		return validateAudioDestination(slot, track, pair, audioSamples, pairIndex, audioCache);
	}
	return ControlResult::success();
}

ControlResult validateLandPairs(const Song& song, const SessionModel& model,
	const LandPair* pairs, int pairCount, std::shared_ptr<const SampleBuffer>* audioSamples,
	QHash<QString, std::shared_ptr<const SampleBuffer>>& audioCache,
	QHash<const SampleBuffer*, std::shared_ptr<const SampleBuffer>>& recordedAudioCache)
{
	for (int i = 0; i < pairCount; ++i)
	{
		const LandPair& pair = pairs[i];
		if (!pairHasSlot(song, model, pair) || pair.stopTick <= pair.startTick) { continue; }
		if (ControlResult validation = validatePairContent(song, model, pair, i,
			audioSamples, audioCache, recordedAudioCache); !validation.ok) { return validation; }
	}
	return ControlResult::success();
}

ControlResult validateLandRequest(const Song& song, const SessionModel& model,
	const OpenStart* open, int openCount, const LandPair* pairs, int pairCount,
	std::shared_ptr<const SampleBuffer>* audioSamples,
	QHash<QString, std::shared_ptr<const SampleBuffer>>& audioCache,
	QHash<const SampleBuffer*, std::shared_ptr<const SampleBuffer>>& recordedAudioCache)
{
	if (openCount > 0) { return validateOpenStarts(open, openCount); }
	return validateLandPairs(song, model, pairs, pairCount, audioSamples, audioCache, recordedAudioCache);
}

int copyPatternNotes(MidiClip& destination, int pattern)
{
	PatternStore* store = Engine::patternStore();
	if (store == nullptr || pattern < 0 || pattern >= store->numOfPatterns()) { return 0; }
	int copied = 0;
	for (Track* sourceTrack : store->tracks())
	{
		auto* source = dynamic_cast<MidiClip*>(sourceTrack->getClip(static_cast<std::size_t>(pattern)));
		if (source == nullptr) { continue; }
		for (const Note* note : source->notes())
		{
			destination.addNote(*note, false);
			++copied;
		}
	}
	return copied;
}

QString copySlotContent(Clip* clip, const ClipSlot& slot,
	const std::shared_ptr<const SampleBuffer>& audioSample, const LandPair& pair)
{
	const bool captured = pair.contentKind != SessionArrangementRecorder::Event::ContentKind::Unspecified;
	const bool midi = captured ? pair.contentKind == SessionArrangementRecorder::Event::ContentKind::Midi
		: slot.type() == ClipSlot::Type::Midi;
	if (midi)
	{
		if (auto* destination = dynamic_cast<MidiClip*>(clip))
		{
			copyPatternNotes(*destination, captured ? pair.patternId : slot.patternId());
			return QStringLiteral("midi_pattern_copied");
		}
		// PatternTrack clips are references to their track's PatternStore column.
		return QStringLiteral("pattern_reference");
	}
	const bool audio = captured ? pair.contentKind == SessionArrangementRecorder::Event::ContentKind::Audio
		: slot.type() == ClipSlot::Type::Audio;
	if (audio)
	{
		if (auto* destination = dynamic_cast<SampleClip*>(clip))
		{
			destination->setSampleBuffer(audioSample);
			return QStringLiteral("audio_sample_copied");
		}
	}
	return QStringLiteral("empty");
}

QJsonObject pairState(const LandPair& pair)
{
	return QJsonObject{
		{QStringLiteral("track_index"), pair.track},
		{QStringLiteral("scene"), pair.scene},
		{QStringLiteral("start_tick"), static_cast<int>(pair.startTick)},
		{QStringLiteral("stop_tick"), static_cast<int>(pair.stopTick)},
	};
}

QJsonObject pairNotLanded(const LandPair& pair, const QString& reason)
{
	QJsonObject landed = pairState(pair);
	landed.insert(QStringLiteral("landed"), false);
	landed.insert(QStringLiteral("reason"), reason);
	return landed;
}

void addLandedClipFields(QJsonObject* landed, const Clip* clip, const Track* track,
	const ClipSlot& slot, int pattern, const QString& content)
{
	landed->insert(QStringLiteral("landed"), true);
	landed->insert(QStringLiteral("track"), control::trackIdOf(track));
	landed->insert(QStringLiteral("clip"), control::clipIdOf(clip));
	landed->insert(QStringLiteral("position"), static_cast<int>(clip->startPosition().getTicks()));
	landed->insert(QStringLiteral("length"), static_cast<int>(clip->length().getTicks()));
	landed->insert(QStringLiteral("name"), clip->name());
	landed->insert(QStringLiteral("pattern"), pattern);
	landed->insert(QStringLiteral("content"), content);
}

} // namespace

QJsonObject sessionrecord::landPairState(Song& song, const SessionModel& model, const LandPair& pair,
	const std::shared_ptr<const SampleBuffer>& audioSample)
{
	QJsonObject landed = pairState(pair);
	if (pair.track < 0 || pair.track >= static_cast<int>(song.tracks().size()))
	{
		return pairNotLanded(pair, QStringLiteral("column_has_no_song_track"));
	}
	const tick_t length = pair.stopTick - pair.startTick;
	if (length <= 0)
	{
		return pairNotLanded(pair, QStringLiteral("empty_span"));
	}
	Track* track = song.tracks()[static_cast<std::size_t>(pair.track)];
	if (track == nullptr) { return pairNotLanded(pair, QStringLiteral("track_created_no_clip")); }
	const ClipSlot& slot = model.slot(pair.track, pair.scene);
	const bool captured = pair.contentKind != SessionArrangementRecorder::Event::ContentKind::Unspecified;
	const int pattern = captured
		? pair.contentKind == SessionArrangementRecorder::Event::ContentKind::Midi ? pair.patternId : -1
		: slot.patternId();
	Clip* clip = track->createClip(TimePos(pair.startTick));
	if (clip == nullptr) { return pairNotLanded(pair, QStringLiteral("track_created_no_clip")); }
	const QString content = copySlotContent(clip, slot, audioSample, pair);
	clip->changeLength(TimePos(length));
	clip->setAutoResize(false);
	if (!slot.name().isEmpty()) { clip->setName(slot.name()); }
	clip->dataChanged();

	addLandedClipFields(&landed, clip, track, slot, pattern, content);
	return landed;
}

namespace
{

void registerRecordLand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("session.arrangement_record_land");
	cmd.group = QStringLiteral("session");
	cmd.verb = QStringLiteral("arrangement_record_land");
	cmd.description = QStringLiteral("Land the recorded performance: pair every recorded launch "
		"with the stop that ended it and create ONE arrangement clip per pair on that column's "
		"song track, at the recorded ticks. REFUSED - consuming nothing - while any recorded start "
		"is still open, because a half-landed performance would lose starts or invent their ends; "
		"stop the session first (session.stop_all, or session.back_to_arrangement). Also REFUSED "
		"without consuming anything when MIDI or audio content cannot be copied to the destination "
		"track or an audio source cannot be loaded. Reversible: one composite ProjectJournal "
		"checkpoint covering all touched tracks, so one control.undo takes the whole pass back.");
	cmd.argsSchema = objectSchema();
	cmd.resultSchema = objectSchema({
		{QStringLiteral("landed"), arrayProperty()},
		{QStringLiteral("clips"), integerProperty()},
		{QStringLiteral("pairs"), integerProperty()},
		{QStringLiteral("events"), integerProperty()},
		{QStringLiteral("skipped_columns"), integerProperty()},
		{QStringLiteral("collapsed"), integerProperty()},
		{QStringLiteral("unmatched_stops"), integerProperty()},
		{QStringLiteral("recorded"), integerProperty()},
		{QStringLiteral("dropped"), integerProperty()},
		{QStringLiteral("armed"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject&) {
		ControlResult error;
		SessionModel* model = sessionModelOrNull(&error);
		if (model == nullptr) { return error; }
		Song* song = Engine::getSong();
		SessionArrangementRecorder& recorder = song->sessionScheduler().arrangementRecorder();
		SessionArrangementRecorder::Event events[MaxLandEvents];
		const std::size_t count = recorder.snapshot(events, MaxLandEvents);
		if (count == 0)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("nothing has been recorded: arm session.arrangement_record_arm, "
					"perform, and stop the session before landing"));
		}
		LandPair pairs[MaxLandEvents];
		std::shared_ptr<const SampleBuffer> audioSamples[MaxLandEvents]{};
		QHash<QString, std::shared_ptr<const SampleBuffer>> audioCache;
		QHash<const SampleBuffer*, std::shared_ptr<const SampleBuffer>> recordedAudioCache;
		OpenStart open[MaxLandEvents];
		int openCount = 0;
		int unmatchedStops = 0;
		const int pairCount = landPairEvents(events, count, pairs, MaxLandEvents,
			open, &openCount, MaxLandEvents, &unmatchedStops);
		if (ControlResult validation = validateLandRequest(*song, *model, open, openCount,
				pairs, pairCount, audioSamples, audioCache, recordedAudioCache); !validation.ok) { return validation; }

		// Every event is paired or an unmatched stop (which has no clip to make),
		// so the pass can consume the whole ring.
		consumeLandEvents(recorder, count);

		// ONE composite checkpoint over the distinct touched tracks, taken here -
		// before the loop below creates anything - so the pass is one undo action
		// however many clips or tracks it touches.
		checkpointLandTracks(*song, pairs, pairCount);

		QJsonArray landed;
		int clips = 0;
		int skippedColumns = 0;
		int collapsed = 0;
		for (int i = 0; i < pairCount; ++i)
		{
			const QJsonObject entry = landPairState(*song, *model, pairs[i], audioSamples[i]);
			if (!entry.value(QStringLiteral("landed")).toBool())
			{
				if (entry.value(QStringLiteral("reason")).toString()
					== QStringLiteral("column_has_no_song_track")) { ++skippedColumns; }
				else { ++collapsed; }
			}
			else { ++clips; }
			landed.append(entry);
		}

		QJsonObject result;
		result.insert(QStringLiteral("landed"), landed);
		result.insert(QStringLiteral("clips"), clips);
		result.insert(QStringLiteral("pairs"), pairCount);
		result.insert(QStringLiteral("events"), static_cast<int>(count));
		result.insert(QStringLiteral("skipped_columns"), skippedColumns);
		result.insert(QStringLiteral("collapsed"), collapsed);
		result.insert(QStringLiteral("unmatched_stops"), unmatchedStops);
		result.insert(QStringLiteral("recorded"), static_cast<double>(recorder.recorded()));
		result.insert(QStringLiteral("dropped"), static_cast<double>(recorder.dropped()));
		result.insert(QStringLiteral("armed"), recorder.armed());

		int firstLanded = -1;
		for (int i = 0; i < landed.size(); ++i)
		{
			if (landed.at(i).toObject().value(QStringLiteral("landed")).toBool())
			{
				firstLanded = i;
				break;
			}
		}
		if (firstLanded >= 0)
		{
			const QJsonObject firstClip = landed.at(firstLanded).toObject();
			result.insert(QStringLiteral("__transaction"),
				transactionPayload(
					QJsonObject{{QStringLiteral("track"),
							firstClip.value(QStringLiteral("track"))},
						{QStringLiteral("track_index"),
							firstClip.value(QStringLiteral("track_index"))},
						{QStringLiteral("lands_created"), clips}},
					QStringLiteral("clip.delete"),
					QJsonObject{{QStringLiteral("clip"),
						firstClip.value(QStringLiteral("clip"))}},
					true,
					QStringLiteral("ProjectJournal (one composite checkpoint covering all touched "
						"tracks, taken before the pass created its clips: Track::restoreState re-loads "
						"the track's serialized clips, which is how the GUI's own clip "
						"add/delete paths reverse themselves). The recorded inverse names the "
						"FIRST clip of the pass, which clip.delete removes exactly; "
						"control.undo restores every touched track, which removes them all.")));
		}
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerSessionRecordLandCommands(ControlRegistry& registry)
{
	registerRecordLand(registry);
}

} // namespace lmms
