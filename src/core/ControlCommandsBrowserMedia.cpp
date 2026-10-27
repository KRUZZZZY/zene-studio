/*
 * ControlCommandsBrowserMedia.cpp - waveform thumbnails and the tempo- and key-matched preview
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
/*! 0.5.0 L11, what the browser shows and plays of a file:
 *  - browser.thumbnail (WF-018): a small waveform picture (PNG) of an audio file, drawn from the peak
 *    cache and kept beside the index in the config directory - what the gallery view shows;
 *  - browser.preview / browser.preview_stop (WF-009): play a file to hear it, at its own speed or matched
 *    to the song - stretched to the song's tempo (WSOLA, pitch kept) and/or moved to its key - using the
 *    tempo and key the import detection finds in the file. The audio is prepared here, on the control
 *    thread, and handed to the engine as one finished buffer; stopping is a generation number the play
 *    handle reads (an atomic), so the audio thread never waits, allocates or frees for it;
 *  - browser.load (theme 3, 0.5.0 UX): what Enter on a result does - the file on a new track, one undo step. */

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QPainter>

#include "AudioEngine.h"
#include "AudioStretcher.h"
#include "BounceInPlace.h"
#include "BrowserCatalog.h"
#include "BrowserPeakCache.h"
#include "ConfigManager.h"
#include "ControlBrowserSupport.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlScaleShared.h"
#include "ControlTrackCreation.h"
#include "ControlVocabulary.h"
#include "DataFile.h"
#include "Engine.h"
#include "ImportDetection.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "PluginFactory.h"
#include "Sample.h"
#include "SampleClip.h"
#include "SampleDecoder.h"
#include "SamplePlayHandle.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr double PreviewSeconds = 30.0;

// ---------------------------------------------------------------------------
// browser.thumbnail
// ---------------------------------------------------------------------------

QString thumbnailPath(const QString& key, int width, int height)
{
	const QFileInfo info(key);
	const QByteArray identity = QStringLiteral("%1|%2|%3|%4x%5").arg(key).arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch())
		.arg(width).arg(height).toUtf8();
	const QString name = QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha1).toHex()) + QStringLiteral(".png");
	return QDir(ConfigManager::inst()->workingDir()).filePath(QStringLiteral("browser-thumbnails/") + name);
}

QImage drawWaveform(const QList<BrowserPeak>& peaks, int width, int height)
{
	QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	painter.setPen(QColor(QStringLiteral("#7aa2f7")));
	const double middle = height / 2.0;
	for (int x = 0; x < width && x < peaks.size(); ++x)
	{
		const BrowserPeak& peak = peaks.at(x);
		painter.drawLine(x, static_cast<int>(middle - std::clamp(peak.max, -1.f, 1.f) * middle),
			x, static_cast<int>(middle - std::clamp(peak.min, -1.f, 1.f) * middle));
	}
	return image;
}

ControlResult thumbnail(const QJsonObject& args)
{
	QString key;
	ControlResult error;
	if (!readBrowserFileArgument(args, &key, &error)) { return error; }
	if (browserKindForPath(key) != BrowserKind::Audio)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("a thumbnail is drawn for an audio file; %1 is a %2")
			.arg(key, browserKindName(browserKindForPath(key))));
	}
	const int width = args.value(QStringLiteral("width")).toInt(160);
	const int height = args.value(QStringLiteral("height")).toInt(48);
	const QString out = args.value(QStringLiteral("out")).toString(thumbnailPath(key, width, height));
	if (!QFileInfo(out).isAbsolute()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'out' must be absolute")); }
	// The PNG replaces whatever the name resolves to: an output that is the audio file itself would destroy it.
	if (BounceInPlace::namesTheSameFile(key, out))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'out' is the audio file itself: a thumbnail cannot be written over its own source"));
	}
	// A cached file counts only if it decodes: an interrupted save must not stand in for the thumbnail for good.
	const bool cached = !args.contains(QStringLiteral("out")) && !QImage(out).isNull();
	if (!cached)
	{
		BrowserPeakCache::Answer answer;
		QString why;
		if (!BrowserPeakCache::instance().peaksFor(key, width, &answer, &why))
		{
			return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("no waveform for %1: %2").arg(key, why));
		}
		QDir().mkpath(QFileInfo(out).absolutePath());
		if (!drawWaveform(answer.peaks, width, height).save(out, "PNG"))
		{
			return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the thumbnail could not be written to %1").arg(out));
		}
	}
	return ControlResult::success({{QStringLiteral("path"), key}, {QStringLiteral("image"), out}, {QStringLiteral("width"), width},
		{QStringLiteral("height"), height}, {QStringLiteral("cached"), cached}});
}

// ---------------------------------------------------------------------------
// browser.preview
// ---------------------------------------------------------------------------

//! Bumped by every new preview and by a stop: a preview handle plays while its generation is current.
std::atomic<int> s_previewGeneration{0};

class PreviewPlayHandle : public SamplePlayHandle
{
public:
	PreviewPlayHandle(Sample* sample, int generation) :
		SamplePlayHandle(sample),
		m_generation(generation)
	{
	}

	bool isFinished() const override
	{
		return s_previewGeneration.load(std::memory_order_relaxed) != m_generation || SamplePlayHandle::isFinished();
	}

private:
	const int m_generation;
};

//! What a matched preview does to the file: how much faster it plays and how far it moves.
struct PreviewPlan
{
	double fileBpm = 0.0;
	int fileKey = -1;
	double speed = 1.0;   //!< > 1 plays faster (the song is quicker than the file)
	int semitones = 0;
};

//! The song's key: 'key' when given, else a detected key, else the scale the control surface set; -1 for none.
int songKey(const QJsonObject& args)
{
	if (args.contains(QStringLiteral("key"))) { return args.value(QStringLiteral("key")).toInt(); }
	const ProjectKey& detected = Engine::getSong()->projectKey();
	if (!detected.empty()) { return detected.tonicPitchClass(); }
	return scaleContext().scale.isEmpty() ? -1 : scaleContext().root;
}

//! How much faster the file plays to match the song, folded into half..double (the detection's own
//! half/double ambiguity, ImportDetectionDsp.h); 1 when the file has no tempo.
double tempoSpeed(double fileBpm)
{
	if (fileBpm <= 0.0) { return 1.0; }
	double ratio = Engine::getSong()->tempoModel().value() / fileBpm;
	while (ratio > std::sqrt(2.0)) { ratio /= 2.0; }
	while (ratio < std::sqrt(0.5)) { ratio *= 2.0; }
	return ratio;
}

int fileKeyOf(const std::vector<float>& mono, int rate)
{
	std::vector<std::uint32_t> masks;
	for (const ScaleCandidate& candidate : candidateScales()) { masks.push_back(candidate.mask); }
	const detection::KeyEstimate estimate = detection::estimateKey(mono.data(), mono.size(), rate, masks, PreviewSeconds);
	return estimate.found ? estimate.tonicPitchClass : -1;
}

PreviewPlan planFor(const std::vector<float>& mono, int rate, const QString& match, int key)
{
	PreviewPlan plan;
	const detection::TempoEstimate found = detection::estimateTempo(mono.data(), mono.size(), rate, PreviewSeconds);
	plan.fileBpm = found.found ? found.bpm : 0.0;
	plan.fileKey = fileKeyOf(mono, rate);
	if (match == QLatin1String("tempo") || match == QLatin1String("both")) { plan.speed = tempoSpeed(plan.fileBpm); }
	const bool pitch = match == QLatin1String("key") || match == QLatin1String("both");
	if (pitch && plan.fileKey >= 0 && key >= 0) { plan.semitones = ((key - plan.fileKey) % 12 + 18) % 12 - 6; }
	return plan;
}

//! @a frames played at @a plan: stretched (pitch kept) by speed / pitch factor; the pitch factor itself
//! is applied by the rate the buffer is declared at, so the engine's resampler moves the pitch.
std::vector<SampleFrame> stretched(const std::vector<SampleFrame>& frames, const PreviewPlan& plan)
{
	const double pitchFactor = std::pow(2.0, plan.semitones / 12.0);
	const double speed = plan.speed / pitchFactor;
	if (std::abs(speed - 1.0) < 1e-3) { return frames; }
	auto stretcher = std::make_unique<AudioStretcher>();
	stretcher->prepare();
	std::vector<SampleFrame> out(static_cast<std::size_t>(static_cast<double>(frames.size()) / speed));
	stretcher->process(frames.data(), static_cast<f_cnt_t>(frames.size()), out.data(), static_cast<f_cnt_t>(out.size()), speed);
	return out;
}

ControlResult preview(const QJsonObject& args)
{
	QString key;
	ControlResult error;
	if (!readBrowserFileArgument(args, &key, &error)) { return error; }
	std::optional<SampleDecoder::Result> decoded = SampleDecoder::decode(key);
	if (!decoded || decoded->data.empty() || decoded->sampleRate <= 0)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("%1 could not be read as audio").arg(key));
	}
	const int rate = decoded->sampleRate;
	decoded->data.resize(std::min(decoded->data.size(), static_cast<std::size_t>(PreviewSeconds * rate)));
	const QString match = args.value(QStringLiteral("match")).toString(QStringLiteral("none"));
	const int key12 = songKey(args);
	const PreviewPlan plan = planFor(monoMix(decoded->data.data(), decoded->data.size()), rate, match, key12);
	const std::vector<SampleFrame> audio = stretched(decoded->data, plan);
	const int declaredRate = static_cast<int>(std::lround(rate * std::pow(2.0, plan.semitones / 12.0)));
	const int generation = s_previewGeneration.fetch_add(1) + 1;
	const bool playing = Engine::audioEngine()->addPlayHandle(new PreviewPlayHandle(new Sample(audio.data(), audio.size(), declaredRate), generation));
	return ControlResult::success({{QStringLiteral("path"), key}, {QStringLiteral("playing"), playing}, {QStringLiteral("match"), match},
		{QStringLiteral("file_bpm"), plan.fileBpm}, {QStringLiteral("file_key"), plan.fileKey},
		{QStringLiteral("song_bpm"), Engine::getSong()->tempoModel().value()}, {QStringLiteral("song_key"), key12},
		{QStringLiteral("speed"), plan.speed}, {QStringLiteral("semitones"), plan.semitones},
		{QStringLiteral("seconds"), static_cast<double>(audio.size()) / declaredRate}});
}

ControlResult previewStop(const QJsonObject&)
{
	s_previewGeneration.fetch_add(1);
	return ControlResult::success({{QStringLiteral("stopped"), true}});
}

// ---------------------------------------------------------------------------
// browser.load
// ---------------------------------------------------------------------------

//! A new sample track playing @a path from the song's start (the file lists' Shift+Enter).
Track* loadAsSampleTrack(const QString& path)
{
	Track* track = Track::create(Track::Type::Sample, Engine::getSong());
	if (track == nullptr) { return nullptr; }
	if (auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)))) { clip->setSampleFile(path); }
	return track;
}

//! A new instrument track holding @a path: an .xpf preset as the track's settings, any other file in the
//! instrument that opens its extension (a sample in AudioFileProcessor, an .xiz in ZynAddSubFX) - the file
//! lists' Enter (FileBrowserTreeWidget::handleFile). Sets @a error and builds nothing when no instrument opens it.
Track* loadAsInstrument(const QString& path, ControlResult* error)
{
	const QString ext = QFileInfo(path).suffix().toLower();
	const PluginFactory::PluginInfoAndKey plugin = getPluginFactory()->pluginSupportingExtension(ext);
	if (ext != QLatin1String("xpf") && plugin.isNull())
	{
		*error = ControlResult::failure(ControlErrorKind::Requires, QStringLiteral("no instrument in this build opens .%1 files").arg(ext));
		return nullptr;
	}
	auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
	if (track == nullptr) { return nullptr; }
	if (ext == QLatin1String("xpf"))
	{
		track->replaceInstrument(DataFile(path));
		return track;
	}
	if (Instrument* instrument = track->loadInstrument(plugin.info.name(), &plugin.key)) { instrument->loadFile(path); }
	return track;
}

ControlResult load(const QJsonObject& args)
{
	QString path;
	ControlResult error;
	if (!readBrowserFileArgument(args, &path, &error)) { return error; }
	const BrowserKind kind = browserKindForPath(path);
	if (kind != BrowserKind::Audio && kind != BrowserKind::Preset)
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("browser.load takes an audio file or a preset; %1 is a %2 "
			"(project.open opens a project, project.import imports a MIDI file)").arg(path, browserKindName(kind)));
	}
	const bool sampleTrack = args.value(QStringLiteral("as")).toString() == QLatin1String("sample_track");
	if (sampleTrack && kind != BrowserKind::Audio)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("only an audio file loads as a sample track"));
	}
	error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("no track could be made"));  // loadAsInstrument may say why
	Track* track = sampleTrack ? loadAsSampleTrack(path) : loadAsInstrument(path, &error);
	if (track == nullptr) { return error; }
	recordCreatedTrack(track);  // one undo step removes the track; redo builds it again (ControlTrackCreation.h)
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("path"), path}, {QStringLiteral("kind"), browserKindName(kind)}, {QStringLiteral("track"), trackIdOf(track)},
		{QStringLiteral("as"), sampleTrack ? QStringLiteral("sample_track") : QStringLiteral("instrument")}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("path"), path}}, QStringLiteral("control.undo"), {}, true,
		QStringLiteral("action checkpoint: the recorded undo step removes the created track")));
	return ControlResult::success(result);
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	const QStringList& required, ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QStringLiteral("browser.") + QLatin1String(verb), description, objectSchema(args, QJsonArray::fromStringList(required)), false, handler);
}

} // namespace

void registerBrowserMediaCommands(ControlRegistry& registry)
{
	add(registry, "thumbnail", QStringLiteral("A waveform picture (PNG, 'width' x 'height', 160 x 48 when left out) of the audio "
		"file 'path', drawn from the peak cache and kept in the config directory (or written to 'out', absolute); 'cached' "
		"says it was already there. What the browser's gallery view shows. Writes no project state."),
		{{QStringLiteral("path"), stringProperty()}, {QStringLiteral("width"), integerProperty(16, 1024)},
			{QStringLiteral("height"), integerProperty(8, 512)}, {QStringLiteral("out"), stringProperty()}}, {QStringLiteral("path")}, thumbnail);
	add(registry, "preview", QStringLiteral("Play the audio file 'path' (its first 30 seconds) to hear it - as it is ('match' none, "
		"the default), or matched to the song: 'tempo' stretches it to the song's tempo with its pitch kept, 'key' moves it to "
		"the song's key ('key' 0..11, else the detected key, else the scale set with scale.set), 'both' does both. The file's "
		"tempo and key are the import detection's (half/double tempo is folded). A new preview stops the last. Session audio, "
		"not project state."),
		{{QStringLiteral("path"), stringProperty()}, {QStringLiteral("match"), enumProperty({QStringLiteral("none"), QStringLiteral("tempo"),
			QStringLiteral("key"), QStringLiteral("both")})}, {QStringLiteral("key"), integerProperty(0, 11)}}, {QStringLiteral("path")}, preview);
	add(registry, "preview_stop", QStringLiteral("Stop the browser's preview."), {}, {}, previewStop);
	// Theme 3 (0.5.0 UX): what Enter on a browser result does.
	control::defineCommandWithSchema(registry, QStringLiteral("browser.load"), QStringLiteral("Load the audio file or preset 'path' "
		"onto a new track - what Enter does on a browser result: a preset (.xpf) becomes the new instrument track's settings, any "
		"other file opens in the instrument that takes its extension (a sample in AudioFileProcessor). 'as' sample_track puts an "
		"audio file on a new sample track instead (Shift+Enter). A project or MIDI file is refused: project.open and "
		"project.import take those. One undo step removes the track."),
		objectSchema({{QStringLiteral("path"), stringProperty()}, {QStringLiteral("as"), enumProperty({QStringLiteral("instrument"),
			QStringLiteral("sample_track")})}}, QJsonArray{QStringLiteral("path")}), true, load);
}

} // namespace lmms
