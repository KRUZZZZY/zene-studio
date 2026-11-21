/*
 * AafImportRead.cpp - the AAF reader: LibAAF's composition walked into the
 *                     plain AafModel (include/AafImport.h).
 *
 * The walk follows LibAAF's own tool (tools/AAFTool.c, `--aaf-clips`): every
 * audio track of the top-level composition, every timeline item of it - a
 * clip or a transition - and the essence each clip points at. Positions are
 * relative to the composition's start already (AAFTool adds the session start
 * only to DISPLAY a timecode), so the model's frame 0 is the composition's
 * first edit unit.
 *
 * Everything that touches the library is behind ZENE_HAVE_LIBAAF; without it
 * readAaf() refuses with a message that begins "unavailable", so the commands
 * still register and say why they cannot run.
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

#include "AafImport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>

#include <algorithm>
#include <cmath>

#ifdef ZENE_HAVE_LIBAAF
#include <libaaf.h>

#include "SampleDecoder.h"
#endif

namespace lmms::interchange
{

#ifdef ZENE_HAVE_LIBAAF

namespace
{

//! True when this build's sample decoder opens @a path (by extension), so a clip on it can be built
//! without the loader's "failed to load sample" box.
bool decodable(const QString& path)
{
	const std::string suffix = QFileInfo(path).suffix().toLower().toStdString();
	const auto& types = SampleDecoder::supportedAudioTypes();
	return std::any_of(types.begin(), types.end(), [&suffix](const SampleDecoder::AudioType& type) {
		return type.extension == suffix;
	});
}

QString text(const char* value)
{
	return value != nullptr ? QString::fromUtf8(value) : QString();
}

double ratio(const aafRational_t& value)
{
	return aafRationalToDouble(value);
}

//! LibAAF's interpolation flags as a fade curve (linear when they name none this engine has).
FadeShape fadeShapeFor(uint32_t flags)
{
	switch (flags & AAFI_INTERPOL_MASK)
	{
		case AAFI_INTERPOL_LOG: return FadeShape::Logarithmic;
		case AAFI_INTERPOL_POWER: return FadeShape::EqualPower;
		case AAFI_INTERPOL_BSPLINE: return FadeShape::SCurve;
		default: return FadeShape::Linear;
	}
}

//! Keeps LibAAF's warnings and errors in the report rather than on stdout.
void collectLog(struct aafLog* log, void*, int, int type, const char*, const char*, int, const char* msg, void* user)
{
	auto* loss = static_cast<AafLossReport*>(user);
	const bool kept = type == VERB_ERROR || type == VERB_WARNING;
	if (loss != nullptr && kept && msg != nullptr && loss->warnings.size() < AafMaxWarnings)
	{
		loss->warnings.append(QString::fromUtf8(msg).trimmed());
	}
	LOG_BUFFER_RESET(log);
}

//! One walk's state: the library context, the destination and the essence -> file map.
struct Walk
{
	AAF_Iface* aafi = nullptr;
	AafModel* model = nullptr;
	AafLossReport* loss = nullptr;
	QHash<const aafiAudioEssenceFile*, QString> files;

	qint64 frames(aafPosition_t value, const aafRational_t* rate) const
	{
		return rate != nullptr
			? aafFramesFromEditUnits(value, rate->numerator, rate->denominator, model->sampleRate) : 0;
	}
};

//! Where @a essence's audio is: extracted (embedded, when asked) or located by LibAAF (external).
void resolveEssence(Walk& walk, aafiAudioEssenceFile* essence, const AafReadOptions& options, AafMedia* entry)
{
	if (essence->is_embedded && options.extract)
	{
		const QByteArray dir = QFile::encodeName(options.extractDir);
		if (QDir().mkpath(options.extractDir)
			&& aafi_extractAudioEssenceFile(walk.aafi, essence, AAFI_EXTRACT_WAV, dir.constData(), 0, 0,
				nullptr, nullptr) == 0)
		{
			entry->path = text(essence->usable_file_path);
		}
		entry->missing = entry->path.isEmpty();
		return;
	}
	if (essence->is_embedded) { return; }  // a dry run: it would be extracted, so it is not missing
	entry->path = text(essence->usable_file_path);
	if (!entry->path.isEmpty() && !decodable(entry->path))
	{
		if (walk.loss != nullptr && walk.loss->warnings.size() < AafMaxWarnings)
		{
			walk.loss->warnings.append(QStringLiteral("%1: this build cannot decode .%2 files")
				.arg(entry->path, QFileInfo(entry->path).suffix()));
		}
		entry->path.clear();
	}
	entry->missing = entry->path.isEmpty();
}

void readMedia(Walk& walk, const AafReadOptions& options)
{
	aafiAudioEssenceFile* essence = nullptr;
	AAFI_foreachAudioEssenceFile(walk.aafi, essence)
	{
		AafMedia entry;
		entry.name = text(essence->unique_name);
		entry.originalUri = essence->is_embedded ? QString() : text(essence->original_file_path);
		entry.embedded = essence->is_embedded != 0;
		entry.channels = essence->channels;
		entry.sampleRate = static_cast<int>(essence->samplerate);
		resolveEssence(walk, essence, options, &entry);
		walk.files.insert(essence, entry.path);
		walk.model->media.append(entry);
	}
}

//! The transition just before (@a before) or after a clip's item, or nullptr.
aafiTransition* transitionNext(aafiTimelineItem* item, bool before)
{
	aafiTimelineItem* other = before ? item->prev : item->next;
	return other != nullptr && other->type == AAFI_TRANS ? static_cast<aafiTransition*>(other->data) : nullptr;
}

AafFade fadeOf(const Walk& walk, aafiTransition* transition, uint32_t kind, const aafRational_t* rate)
{
	AafFade fade;
	if (transition == nullptr || (transition->flags & (kind | AAFI_TRANS_XFADE)) == 0) { return fade; }
	fade.frames = walk.frames(transition->len, rate);
	fade.shape = fadeShapeFor(transition->flags);
	return fade;
}

//! The clip-gain curve. Past the envelope's MaxGainPoints the points are thinned EVENLY (the first and
//! the last kept), so the curve keeps its shape instead of freezing at its 32nd point.
void readEnvelope(Walk& walk, const aafiAudioGain* automation, AafClip* clip)
{
	if (automation == nullptr || (automation->flags & AAFI_AUDIO_GAIN_VARIABLE) == 0) { return; }
	const int count = static_cast<int>(automation->pts_cnt);
	const int kept = std::min(count, MaxGainPoints);
	walk.loss->envelopePointsDropped += count - kept;
	for (int slot = 0; slot < kept; ++slot)
	{
		const int index = kept > 1 ? static_cast<int>(std::lround(double(slot) * (count - 1) / (kept - 1))) : 0;
		const double linear = std::max(ratio(automation->value[index]), 1e-6);
		const float db = std::clamp(gainLinearToDb(static_cast<float>(linear)), MinGainPointDb, MaxGainPointDb);
		clip->envelope.append(AafGainPoint{std::clamp(ratio(automation->time[index]), 0.0, 1.0), db});
	}
}

//! The clip's source: the first essence it points at (the loss report counts the rest).
void readSource(Walk& walk, const aafiAudioClip* source, AafClip* clip)
{
	const aafiAudioEssencePointer* pointer = source->essencePointerList;
	if (pointer == nullptr || pointer->essenceFile == nullptr) { return; }
	if (pointer->next != nullptr) { walk.loss->multiEssenceClips++; }
	if (pointer->essenceChannel != 0 && pointer->essenceFile->channels > 1) { walk.loss->channelSelections++; }
	clip->mediaName = text(pointer->essenceFile->unique_name);
	clip->sourcePath = walk.files.value(pointer->essenceFile);
	if (clip->name.isEmpty()) { clip->name = text(pointer->essenceFile->name); }
	if (clip->sourcePath.isEmpty() && !pointer->essenceFile->is_embedded) { walk.loss->missingMediaClips++; }
}

AafClip readClip(Walk& walk, aafiTimelineItem* item, aafiAudioClip* source, const aafRational_t* rate)
{
	AafClip clip;
	clip.name = text(source->subClipName);
	clip.positionFrames = walk.frames(source->pos, rate);
	clip.lengthFrames = walk.frames(source->len, rate);
	clip.sourceOffsetFrames = walk.frames(source->essence_offset, rate);
	if (source->gain != nullptr && (source->gain->flags & AAFI_AUDIO_GAIN_CONSTANT) && source->gain->pts_cnt > 0)
	{
		clip.gain = ratio(source->gain->value[0]);
	}
	readEnvelope(walk, source->automation, &clip);
	clip.fadeIn = fadeOf(walk, transitionNext(item, true), AAFI_TRANS_FADE_IN, rate);
	clip.fadeOut = fadeOf(walk, transitionNext(item, false), AAFI_TRANS_FADE_OUT, rate);
	clip.muted = source->mute != 0;
	readSource(walk, source, &clip);
	return clip;
}

//! The track's own constant gain and pan; automation on either is counted as lost.
void readTrackLevels(Walk& walk, const aafiAudioTrack* source, AafTrack* track)
{
	if (const aafiAudioGain* gain = source->gain)
	{
		if ((gain->flags & AAFI_AUDIO_GAIN_CONSTANT) && gain->pts_cnt > 0) { track->gain = ratio(gain->value[0]); }
		if (gain->flags & AAFI_AUDIO_GAIN_VARIABLE) { walk.loss->trackGainAutomation++; }
	}
	if (const aafiAudioPan* pan = source->pan)
	{
		track->hasPan = (pan->flags & AAFI_AUDIO_GAIN_CONSTANT) && pan->pts_cnt > 0;
		if (track->hasPan) { track->pan = std::clamp(ratio(pan->value[0]), 0.0, 1.0); }
		if (pan->flags & AAFI_AUDIO_GAIN_VARIABLE) { walk.loss->trackPanAutomation++; }
	}
}

AafTrack readTrack(Walk& walk, aafiAudioTrack* source)
{
	AafTrack track;
	track.number = static_cast<int>(source->number);
	track.name = text(source->name);
	track.muted = source->mute != 0;
	if (source->solo) { walk.loss->soloTracks++; }
	readTrackLevels(walk, source, &track);
	aafiTimelineItem* item = nullptr;
	AAFI_foreachTrackItem(source, item)
	{
		if (aafiTransition* transition = aafi_timelineItemToCrossFade(item))
		{
			if (transition->flags & AAFI_TRANS_XFADE) { walk.loss->crossfades++; }
			continue;
		}
		if (aafiAudioClip* clip = aafi_timelineItemToAudioClip(item))
		{
			track.clips.append(readClip(walk, item, clip, source->edit_rate));
		}
	}
	return track;
}

QString colorOf(const uint16_t rgb[3])
{
	if (rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0) { return QString(); }
	const bool wide = rgb[0] > 255 || rgb[1] > 255 || rgb[2] > 255;
	const auto channel = [wide](uint16_t value) { return wide ? value >> 8 : value; };
	return QStringLiteral("#%1%2%3").arg(channel(rgb[0]), 2, 16, QLatin1Char('0'))
		.arg(channel(rgb[1]), 2, 16, QLatin1Char('0')).arg(channel(rgb[2]), 2, 16, QLatin1Char('0'));
}

void readMarkers(Walk& walk)
{
	aafiMarker* marker = nullptr;
	AAFI_foreachMarker(walk.aafi, marker)
	{
		AafMarker entry;
		entry.positionFrames = walk.frames(marker->start, marker->edit_rate);
		entry.name = text(marker->name);
		entry.color = colorOf(marker->RGBColor);
		if (marker->length > 0 || (marker->comment != nullptr && marker->comment[0] != '\0'))
		{
			walk.loss->markerExtras++;
		}
		walk.model->markers.append(entry);
	}
}

void countVideo(Walk& walk)
{
	aafiVideoTrack* track = nullptr;
	AAFI_foreachVideoTrack(walk.aafi, track)
	{
		walk.loss->videoTracks++;
		for (aafiTimelineItem* item = track->timelineItems; item != nullptr; item = item->next)
		{
			if (item->type == AAFI_VIDEO_CLIP) { walk.loss->videoClips++; }
		}
	}
}

void readComposition(Walk& walk, const AafReadOptions& options)
{
	AAF_Iface* aafi = walk.aafi;
	walk.model->compositionName = text(aafi->compositionName);
	walk.model->productName = text(aafi->aafd->Identification.ProductName);
	//! A video-only composition has no audio aggregate at all: LibAAF leaves it null, and its
	//! audio macros (essence files, tracks) dereference it - so the audio half is read only
	//! when there is one, and the model keeps the default sample rate.
	aafiAudio* audio = aafi->Audio;
	walk.model->sampleRate = audio != nullptr && audio->samplerate > 0
		? static_cast<int>(audio->samplerate) : AafDefaultSampleRate;
	if (audio != nullptr)
	{
		readMedia(walk, options);
		aafiAudioTrack* track = nullptr;
		AAFI_foreachAudioTrack(aafi, track)
		{
			if (walk.model->editRate.isEmpty() && track->edit_rate != nullptr)
			{
				walk.model->editRate = QStringLiteral("%1/%2").arg(track->edit_rate->numerator)
					.arg(track->edit_rate->denominator);
			}
			walk.model->tracks.append(readTrack(walk, track));
		}
	}
	readMarkers(walk);
	if (aafi->Video != nullptr) { countVideo(walk); }
}

//! A loaded library context, released however the read ends.
struct Context
{
	AAF_Iface* aafi = aafi_alloc(nullptr);
	~Context() { aafi_release(&aafi); }
};

} // namespace

bool aafImportAvailable()
{
	return true;
}

QString aafLibraryVersion()
{
	return QStringLiteral(LIBAAF_VERSION);
}

bool readAaf(const QString& path, const AafReadOptions& options, AafModel* model, AafLossReport* loss,
	QString* error)
{
	if (aafIsOmfPath(path))
	{
		*error = QStringLiteral("%1 is an OMF file; LibAAF reads AAF only (OMF 2.0 is a Bento container it does "
			"not parse) - export AAF from the source application").arg(path);
		return false;
	}
	*loss = AafLossReport(); // each read starts fresh: a caller may reuse one report across reads
	Context context;
	if (context.aafi == nullptr) { *error = QStringLiteral("LibAAF could not allocate a context"); return false; }
	aafi_set_debug(context.aafi, VERB_WARNING, 0, nullptr, &collectLog, loss);
	aafi_set_option_int(context.aafi, "protools", PROTOOLS_ALL_OPT);
	const QByteArray location = QFile::encodeName(options.mediaLocation);
	if (!options.mediaLocation.isEmpty()) { aafi_set_option_str(context.aafi, "media_location", location.constData()); }
	const QByteArray file = QFile::encodeName(path);
	if (aafi_load_file(context.aafi, file.constData()) != 0)
	{
		*error = QStringLiteral("LibAAF could not read %1 as an AAF file").arg(path);
		return false;
	}
	*model = AafModel();
	Walk walk{context.aafi, model, loss, {}};
	readComposition(walk, options);
	return true;
}

#else // ZENE_HAVE_LIBAAF

bool aafImportAvailable()
{
	return false;
}

QString aafLibraryVersion()
{
	return QString();
}

bool readAaf(const QString& path, const AafReadOptions&, AafModel*, AafLossReport*, QString* error)
{
	*error = aafIsOmfPath(path)
		? QStringLiteral("unavailable: this build has no LibAAF, and LibAAF reads AAF only - OMF is not supported")
		: QStringLiteral("unavailable: this build was configured without LibAAF (ZENE_WANT_LIBAAF, libaaf not "
			"found), so it cannot read AAF files");
	return false;
}

#endif // ZENE_HAVE_LIBAAF

} // namespace lmms::interchange
