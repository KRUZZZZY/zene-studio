/*
 * ControlCommandsRenderBounce.cpp - render a bounce asynchronously, then apply its edit on the UI thread
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option)
 * any later version.
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

#include <limits>
#include <memory>
#include <vector>

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonObject>

#include "ArrangementEdits.h"
#include "BounceInPlace.h"
#include "Clip.h"
#include "ControlBounceSupport.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlStructuralSupport.h"
#include "ControlTrackCreation.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "RenderQueue.h"
#include "SampleClip.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;

namespace
{

struct Target
{
	int id = -1;
	QByteArray fingerprint;
	bool wasMuted = false;
};

struct BounceContext
{
	QString operation;
	BounceInPlace::Range range;
	std::vector<Target> targets;
	QString sourceId;
	QString sourceName;
	QString sourceXml;
	int sourceIndex = -1;
	bool wholeTrack = true;
	bool muteSources = true;
	int sampleRate = 44100;
	int tempo = 0;
	TempoMap tempoMap;
	bool rangeAllTracks = true;
	QVector<int> rangeTrackIds;
};

struct RangeLanding
{
	std::vector<int> sourceIds;
	std::vector<bool> muted;
	QString path;
	tick_t at = 0;
	bool mute = true;
	int landed = -1;
};

QString trackFingerprint(Track* track)
{
	QString xml;
	if (track == nullptr || !captureTrackXml(track, &xml, std::numeric_limits<int>::max())) { return QString(); }
	return QString::fromLatin1(QCryptographicHash::hash(xml.toUtf8(), QCryptographicHash::Sha256).toHex());
}

Track* currentTrack(int id)
{
	return trackWithId(id);
}

QJsonObject frozenState(Track* track)
{
	return {{QStringLiteral("track"), control::trackIdOf(track)}, {QStringLiteral("name"), track->name()},
		{QStringLiteral("frozen"), track->isFrozen()}, {QStringLiteral("audio"), track->frozenTake().path},
		{QStringLiteral("audio_ready"), track->frozenAudioReady()},
		{QStringLiteral("start_ticks"), static_cast<double>(track->frozenTake().startTicks)},
		{QStringLiteral("end_ticks"), static_cast<double>(track->frozenTake().endTicks)},
		{QStringLiteral("muted_clips"), static_cast<int>(track->frozenTake().mutedClips.size())}};
}

std::vector<Track::FrozenTake::MutedClip> clipsInside(Track* track, const BounceInPlace::Range& range,
		QJsonArray* overlapping)
{
	std::vector<Track::FrozenTake::MutedClip> inside;
	for (const Clip* clip : track->getClips())
	{
		if (clip == nullptr || clip->isMuted()) { continue; }
		const tick_t start = clip->startPosition().getTicks();
		const tick_t end = clip->endPosition();
		if (start >= range.start && start < range.end)
		{
			inside.push_back({start, clip->length().getTicks()});
		}
		else if (start < range.start && end > range.start)
		{
			overlapping->append(QJsonObject{{QStringLiteral("start_ticks"), static_cast<double>(start)},
				{QStringLiteral("length_ticks"), static_cast<double>(clip->length().getTicks())}});
		}
	}
	return inside;
}

QVector<int> currentRangeTrackIds(const QVector<Track*>& tracks)
{
	QVector<int> ids;
	ids.reserve(tracks.size());
	for (const Track* track : tracks) { ids.append(track->id()); }
	return ids;
}

bool rangeStillMatches(const BounceContext& context, QString* error)
{
	const ArrangerRange& range = ArrangerRange::instance();
	QVector<int> expectedTrackIds;
	expectedTrackIds.reserve(static_cast<int>(context.targets.size()));
	for (const Target& target : context.targets) { expectedTrackIds.append(target.id); }
	if (range.active() && range.start() == context.range.start && range.end() == context.range.end
		&& range.allTracks() == context.rangeAllTracks && range.trackIds() == context.rangeTrackIds
		&& currentRangeTrackIds(range.tracks()) == expectedTrackIds) { return true; }
	*error = QStringLiteral("the selected range changed while the render was running; no edit was applied");
	return false;
}

bool tempoStillMatches(const BounceContext& context, QString* error)
{
	if (context.tempo != static_cast<int>(Engine::getSong()->getTempo()))
	{
		*error = QStringLiteral("the project tempo changed while the render was running; no edit was applied");
		return false;
	}
	const TempoMap& liveMap = Engine::getSong()->tempoMap().map();
	if (liveMap.active() != context.tempoMap.active() || liveMap.size() != context.tempoMap.size())
	{
		*error = QStringLiteral("the tempo map changed while the render was running; no edit was applied");
		return false;
	}
	for (int i = 0; i < liveMap.size(); ++i)
	{
		if (liveMap[i] != context.tempoMap[i])
		{
			*error = QStringLiteral("the tempo map changed while the render was running; no edit was applied");
			return false;
		}
	}
	return true;
}

bool targetStillMatches(const BounceContext& context, QString* error)
{
	for (const Target& expected : context.targets)
	{
		Track* track = currentTrack(expected.id);
		if (track == nullptr || trackFingerprint(track).toLatin1() != expected.fingerprint)
		{
			*error = QStringLiteral("target trk-%1 changed while the render was running; no edit was applied").arg(expected.id);
			return false;
		}
	}
	if (context.operation == QLatin1String("range_bounce") && !rangeStillMatches(context, error)) { return false; }
	return tempoStillMatches(context, error);
}

QJsonObject applyFreeze(const std::shared_ptr<BounceContext>& context,
		Track* source, const BounceInPlace::Result& rendered, QString* error)
{
	QJsonArray overlapping;
	std::vector<Track::FrozenTake::MutedClip> muted;
	if (!context->wholeTrack) { muted = clipsInside(source, context->range, &overlapping); }
	const int trackId = source->id();
	const QString path = rendered.path;
	const tick_t start = context->wholeTrack ? 0 : context->range.start;
	const tick_t end = rendered.range.end;
	QString freezeError;
	if (!source->freezeTo(path, start, end, muted, &freezeError))
	{
		*error = freezeError;
		return {};
	}
	control::addUndoStep(
		[trackId] { if (Track* live = trackWithId(trackId)) { live->unfreeze(); } },
		[trackId, path, start, end, muted] {
			if (Track* live = trackWithId(trackId))
			{
				QString ignored;
				live->freezeTo(path, start, end, muted, &ignored);
			}
		});
	QJsonObject result = frozenState(source);
	result.insert(QStringLiteral("overlapping_clips"), overlapping);
	result.insert(QStringLiteral("undo"), QStringLiteral("control.undo"));
	return result;
}

QJsonObject applyBounceLand(const std::shared_ptr<BounceContext>& context,
		Track* source, const QString& path, QString* error)
{
	const QString sourceId = context->sourceId;
	const QString name = context->sourceName + QObject::tr(" (bounce)");
	const tick_t at = context->range.start;
	const bool wasMuted = context->targets.front().wasMuted;
	auto landed = std::make_shared<int>(-1);
	Track* made = control::landAudioTrack(name, path, at, -1);
	if (made == nullptr)
	{
		*error = QStringLiteral("could not create the landed audio track");
		return {};
	}
	*landed = made->id();
	addUndoStep(
		[landed, sourceId, wasMuted] {
			if (Track* live = control::trackWithId(*landed)) { control::removeTrack(live); }
			*landed = -1;
			if (Track* current = control::trackWithId(sourceId.mid(4).toInt())) { current->setMuted(wasMuted); }
		},
		[landed, sourceId, name, path, at] {
			if (Track* live = control::trackWithId(*landed)) { control::removeTrack(live); }
			Track* replacement = control::landAudioTrack(name, path, at, -1);
			*landed = replacement != nullptr ? replacement->id() : -1;
			if (Track* current = control::trackWithId(sourceId.mid(4).toInt())) { current->setMuted(true); }
		});
	source->setMuted(true);
	return {{QStringLiteral("track"), context->sourceId}, {QStringLiteral("landed_track"), control::trackIdOf(made)},
		{QStringLiteral("undo"), QStringLiteral("control.undo")}};
}

QJsonObject applyFlatten(const std::shared_ptr<BounceContext>& context,
		Track* source, const QString& path, QString* error)
{
	const QString sourceId = context->sourceId;
	const int index = context->sourceIndex;
	const QString name = context->sourceName;
	const tick_t at = context->range.start;
	auto landed = std::make_shared<int>(-1);
	auto restored = std::make_shared<int>(-1);
	Track* made = control::landAudioTrack(name, path, at, index);
	if (made == nullptr)
	{
		*error = QStringLiteral("could not create the flattened audio track");
		return {};
	}
	*landed = made->id();
	Song* song = Engine::getSong();
	addStructuralUndoStep(
		[landed, restored, song, xml = context->sourceXml, index] {
			if (Track* live = trackWithId(*landed)) { removeTrack(live); }
			*landed = -1;
			Track* back = restoreTrackFromXml(xml, song);
			*restored = back != nullptr ? back->id() : -1;
			if (back != nullptr) { moveTrackToIndex(back, index); }
		},
		[landed, restored, name, path, at, index] {
			if (Track* live = trackWithId(*restored)) { removeTrack(live); }
			*restored = -1;
			Track* replacement = landAudioTrack(name, path, at, index);
			*landed = replacement != nullptr ? replacement->id() : -1;
		}, context->sourceXml.size());
	control::removeTrack(source);
	return {{QStringLiteral("flattened"), sourceId}, {QStringLiteral("track"), control::trackIdOf(made)},
		{QStringLiteral("undo"), QStringLiteral("control.undo")}};
}

std::shared_ptr<RangeLanding> makeRangeLanding(const BounceContext& context, const QString& path)
{
	auto landing = std::make_shared<RangeLanding>();
	landing->path = path;
	landing->at = context.range.start;
	landing->mute = context.muteSources;
	for (const Target& target : context.targets)
	{
		landing->sourceIds.push_back(target.id);
		landing->muted.push_back(target.wasMuted);
	}
	return landing;
}

Track* createRangeLanding(RangeLanding* landing)
{
	Track* made = control::landAudioTrack(QObject::tr("Bounce"), landing->path, landing->at, -1);
	if (made == nullptr) { return nullptr; }
	landing->landed = made->id();
	for (const int id : landing->sourceIds)
	{
		if (Track* current = control::trackWithId(id); current != nullptr && landing->mute) { current->setMuted(true); }
	}
	return made;
}

void removeRangeLanding(RangeLanding* landing)
{
	if (Track* live = control::trackWithId(landing->landed)) { control::removeTrack(live); }
	landing->landed = -1;
	for (std::size_t i = 0; i < landing->sourceIds.size(); ++i)
	{
		if (Track* current = control::trackWithId(landing->sourceIds[i]); current != nullptr && landing->mute)
		{
			current->setMuted(landing->muted[i]);
		}
	}
}

QJsonObject applyRangeBounce(const std::shared_ptr<BounceContext>& context, const QString& path, QString* error)
{
	const auto landing = makeRangeLanding(*context, path);
	Track* made = createRangeLanding(landing.get());
	if (made == nullptr)
	{
		*error = QStringLiteral("could not create the bounced audio track");
		return {};
	}
	addUndoStep([landing] { removeRangeLanding(landing.get()); }, [landing] { createRangeLanding(landing.get()); });
	return {{QStringLiteral("landed_track"), control::trackIdOf(made)},
		{QStringLiteral("tracks"), static_cast<int>(context->targets.size())},
		{QStringLiteral("undo"), QStringLiteral("control.undo")}};
}

QJsonObject applyBounce(const std::shared_ptr<BounceContext>& context,
		const BounceInPlace::Result& rendered, QString* error)
{
	if (context->operation == QLatin1String("bounce")) { return {}; }
	if (context->operation == QLatin1String("range_bounce")) { return applyRangeBounce(context, rendered.path, error); }
	Track* source = context->targets.empty() ? nullptr : currentTrack(context->targets.front().id);
	if (source == nullptr)
	{
		*error = QStringLiteral("the target track no longer exists");
		return {};
	}
	if (context->operation == QLatin1String("freeze_track") || context->operation == QLatin1String("freeze_region"))
	{
		return applyFreeze(context, source, rendered, error);
	}
	if (context->operation == QLatin1String("bounce_land")) { return applyBounceLand(context, source, rendered.path, error); }
	if (context->operation == QLatin1String("flatten")) { return applyFlatten(context, source, rendered.path, error); }
	*error = QStringLiteral("unsupported bounce operation");
	return {};
}

bool supportedOperation(const QString& operation)
{
	return operation == QLatin1String("bounce") || operation == QLatin1String("freeze_track")
		|| operation == QLatin1String("freeze_region") || operation == QLatin1String("bounce_land")
		|| operation == QLatin1String("flatten") || operation == QLatin1String("range_bounce");
}


ControlResult capacityError(const RenderQueue& queue)
{
	return ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("the render queue holds %1 jobs; clear finished ones first (capacity refusal %2)")
			.arg(RenderQueue::MaxJobs).arg(queue.capacityRefusals()));
}

ControlResult capacityRefusal(RenderQueue& queue)
{
	queue.recordCapacityRefusal();
	return capacityError(queue);
}

ControlResult collectRangeTargets(const QJsonObject& args, BounceContext* context, QVector<Track*>* tracks)
{
	const ArrangerRange& range = ArrangerRange::instance();
	if (!range.active()) { return ControlResult::failure(ControlErrorKind::Refused, "no range is selected (range.set)"); }
	if (args.contains(QStringLiteral("start")) || args.contains(QStringLiteral("end")))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, "range_bounce uses the current range.set selection");
	}
	*tracks = range.tracks();
	if (tracks->isEmpty()) { return ControlResult::failure(ControlErrorKind::NotFound, "the range covers no track"); }
	context->range = {range.start(), range.end()};
	context->rangeAllTracks = range.allTracks();
	context->rangeTrackIds = range.trackIds();
	context->muteSources = args.value(QStringLiteral("mute_sources")).toBool(true);
	return ControlResult::success();
}

ControlResult collectTrackTarget(const QJsonObject& args, BounceContext* context, QVector<Track*>* tracks)
{
	ControlResult error;
	Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	tracks->append(track);
	if (context->operation == QLatin1String("freeze_track")
		&& (args.contains(QStringLiteral("start")) || args.contains(QStringLiteral("end"))))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, "use freeze_region for a tick range");
	}
	const QString problem = parseRange(args, context->operation == QLatin1String("freeze_region"), &context->range);
	return problem.isEmpty() ? ControlResult::success() : ControlResult::failure(ControlErrorKind::InvalidArgs, problem);
}

ControlResult collectTargets(const QJsonObject& args, const std::shared_ptr<BounceContext>& context, QVector<Track*>* tracks)
{
	return context->operation == QLatin1String("range_bounce")
		? collectRangeTargets(args, context.get(), tracks)
		: collectTrackTarget(args, context.get(), tracks);
}

ControlResult captureOperationState(const std::shared_ptr<BounceContext>& context, const QVector<Track*>& tracks)
{
	context->wholeTrack = context->range.coversWholeTrack();
	Track* first = tracks.front();
	if ((context->operation == QLatin1String("freeze_track") || context->operation == QLatin1String("freeze_region")) && first->isFrozen())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is already frozen; freeze.unfreeze first").arg(control::trackIdOf(first)));
	}
	if (context->operation == QLatin1String("flatten")
		&& !captureTrackXml(first, &context->sourceXml, StructuralSnapshotLimit))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is too large to keep for an undo").arg(control::trackIdOf(first)));
	}
	context->sourceId = control::trackIdOf(first);
	context->sourceName = first->name();
	context->sourceIndex = trackIndexIn(Engine::getSong(), first);
	for (Track* track : tracks) { context->targets.push_back({track->id(), {}, track->isMuted()}); }
	return ControlResult::success();
}

QString outputVerb(const QString& operation)
{
	if (operation == QLatin1String("freeze_track")) { return QStringLiteral("frozen"); }
	if (operation == QLatin1String("freeze_region")) { return QStringLiteral("frozen-region"); }
	if (operation == QLatin1String("flatten")) { return QStringLiteral("flatten"); }
	return operation == QLatin1String("range_bounce") ? QStringLiteral("selection") : QStringLiteral("bounce");
}

bool prepareBounceJob(const QJsonObject& args, const std::shared_ptr<BounceContext>& context,
		const QVector<Track*>& tracks, RenderJob* job, QString* outPath, QString* error)
{
	*outPath = resolveOutPath(args, tracks.front(), outputVerb(context->operation), context->range);
	BounceInPlace::PreparedRender prepared;
	std::vector<Track*> renderTracks;
	for (Track* track : tracks) { renderTracks.push_back(track); }
	if (!BounceInPlace::prepareRenderTracks(renderTracks, *outPath, context->range, context->sampleRate, &prepared, error)) { return false; }
	for (int i = 0; i < tracks.size(); ++i)
	{
		const QString fingerprint = trackFingerprint(tracks.at(static_cast<int>(i)));
		if (fingerprint.isEmpty())
		{
			BounceInPlace::discardPreparedRender(&prepared);
			*error = QStringLiteral("could not capture the target track for stale-job checking");
			return false;
		}
		context->targets[i].fingerprint = fingerprint.toLatin1();
	}

	job->kind = RenderJob::Kind::Bounce;
	job->operation = context->operation;
	job->commandArgs = args;
	for (const Target& target : context->targets) { job->targetIds.push_back(target.id); }
	job->source = prepared.projectPath;
	job->out = *outPath;
	job->bounce = std::move(prepared);
	job->validateBounce = [context](QString* reason) { return targetStillMatches(*context, reason); };
	job->applyBounce = [context](const BounceInPlace::Result& rendered, QString* reason) {
		return applyBounce(context, rendered, reason);
	};
	return true;
}

ControlResult queueBounce(const QJsonObject& args)
{
	const QString operation = args.value(QStringLiteral("operation")).toString();
	if (!supportedOperation(operation))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'operation' must be bounce, freeze_track, freeze_region, bounce_land, flatten, or range_bounce"));
	}
	RenderQueue& queue = RenderQueue::instance();
	if (!queue.hasCapacity()) { return capacityRefusal(queue); }
	auto context = std::make_shared<BounceContext>();
	context->operation = operation;
	context->tempo = static_cast<int>(Engine::getSong()->getTempo());
	context->tempoMap = Engine::getSong()->tempoMap().map();
	QVector<Track*> tracks;
	ControlResult captured = collectTargets(args, context, &tracks);
	if (!captured.ok) { return captured; }
	captured = captureOperationState(context, tracks);
	if (!captured.ok) { return captured; }
	RenderJob job;
	QString outPath;
	QString error;
	if (!prepareBounceJob(args, context, tracks, &job, &outPath, &error))
	{
		return ControlResult::failure(ControlErrorKind::Refused, error);
	}
	const int id = queue.add(std::move(job));
	if (id < 0)
	{
		BounceInPlace::discardPreparedRender(&job.bounce);
		return capacityError(queue);
	}
	queue.start();
	const RenderJob* accepted = queue.job(id);
	return ControlResult::success({{QStringLiteral("job"), id}, {QStringLiteral("kind"), QStringLiteral("bounce")},
		{QStringLiteral("operation"), operation}, {QStringLiteral("state"), accepted != nullptr ? RenderQueue::stateName(accepted->state) : QStringLiteral("queued")},
		{QStringLiteral("out"), outPath}});
}

} // namespace

void registerRenderBounceCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("render.queue_bounce");
	cmd.group = QStringLiteral("render");
	cmd.verb = QStringLiteral("queue_bounce");
	cmd.description = QStringLiteral("Render a track or the selected range in the background, then apply one bounce, freeze, land, or flatten edit on the UI thread. Returns a job id immediately; follow it with render.queue_list or render.queue_report and cancel with render.queue_cancel. The task indicator also reports the running phase and Cancel. The target is checked before the output is installed; if it changed, the job fails without changing the project or output. Existing bounce.in_place, freeze.track, freeze.region, bounce.land, track.flatten and range.bounce commands keep their synchronous result contracts for compatibility.");
	cmd.argsSchema = objectSchema({{QStringLiteral("operation"), enumProperty({QStringLiteral("bounce"), QStringLiteral("freeze_track"),
		QStringLiteral("freeze_region"), QStringLiteral("bounce_land"), QStringLiteral("flatten"), QStringLiteral("range_bounce")})},
		{QStringLiteral("track"), stringProperty()}, {QStringLiteral("out"), stringProperty()},
		{QStringLiteral("start"), integerProperty()}, {QStringLiteral("end"), integerProperty()},
		{QStringLiteral("mute_sources"), booleanProperty()}}, {QStringLiteral("operation")});
	cmd.resultSchema = objectSchema({{QStringLiteral("job"), integerProperty()}, {QStringLiteral("kind"), stringProperty()},
		{QStringLiteral("operation"), stringProperty()}, {QStringLiteral("state"), stringProperty()}, {QStringLiteral("out"), stringProperty()}});
	cmd.mutating = true;
	cmd.handler = queueBounce;
	registry.registerCommand(cmd);
}

} // namespace lmms
