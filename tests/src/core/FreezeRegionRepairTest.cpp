/*
 * FreezeRegionRepairTest.cpp - Q050-008: freeze.region audio and recovery
 *
 * Three behaviours of a frozen region that were broken:
 *   1. a source clip crossing the region's edge sounded twice inside it, and the source was silent
 *      outside it as well;
 *   2. the take started at a frame derived from the constant tempo, not from the tempo map the
 *      render used to cut it;
 *   3. a take whose file had moved left the track silent with no way out.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <vector>

#include "ArrangementEdits.h"
#include "AudioEngine.h"
#include "AudioEngineWorkerThread.h"
#include "AudioDevice.h"
#include "BounceInPlace.h"
#include "Clip.h"
#include "ControlProjectAssets.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "SampleClip.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TakeLaneTestSupport.h"
#include "TempoMap.h"
#include "Timeline.h"
#include "Track.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

double rms(std::span<const SampleFrame> period)
{
	double sum = 0.0;
	for (const SampleFrame& frame : period) { sum += frame[0] * frame[0] + frame[1] * frame[1]; }
	return period.empty() ? 0.0 : std::sqrt(sum / (2.0 * static_cast<double>(period.size())));
}

//! One sample track holding a single 440 Hz clip that starts at tick 0 (about 224 ticks long).
SampleTrack* trackWithOneClip(const QString& dir)
{
	Song* const song = Engine::getSong();
	song->clearProject();
	song->getTimeline(Song::PlayMode::Song).setLoopEnabled(false);
	QVector<SampleClip*> clips;
	auto* track = taketest::makeTakeTrack(dir, 1, &clips);
	return track;
}

std::vector<Track::FrozenTake::MutedClip> asMuted(const std::vector<std::pair<tick_t, tick_t>>& pieces)
{
	std::vector<Track::FrozenTake::MutedClip> muted;
	for (const auto& piece : pieces) { muted.push_back({piece.first, piece.second}); }
	return muted;
}

Track* trackById(int id)
{
	for (Track* candidate : Engine::getSong()->tracks())
	{
		if (candidate->id() == id) { return candidate; }
	}
	return nullptr;
}

bool noClipMuted(const Track* track)
{
	return std::none_of(track->getClips().begin(), track->getClips().end(),
		[](const Clip* clip) { return clip->isMuted(); });
}

//! Plays the song from tick 0 to @a stopTick and returns the loudest period lying within [@a from, @a to].
double loudestBetween(tick_t from, tick_t to, tick_t stopTick)
{
	Song* const song = Engine::getSong();
	song->getTimeline(Song::PlayMode::Song).setLoopEnabled(false);
	song->setPlayPos(0, Song::PlayMode::Song);
	song->playSong();
	double loudest = 0.0;
	for (int period = 0; period < 1200 && song->getPlayPos().getTicks() < stopTick; ++period)
	{
		const tick_t begin = song->getPlayPos().getTicks();
		const double level = rms(Engine::audioEngine()->renderNextPeriod());
		if (begin >= from && song->getPlayPos().getTicks() <= to) { loudest = std::max(loudest, level); }
	}
	song->stop();
	return loudest;
}

} // namespace

class FreezeRegionRepairTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		AudioEngineWorkerThread::setDeterministicProcessing(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		AudioEngineWorkerThread::setDeterministicProcessing(false);
		Engine::destroy();
	}

	//! A1: a clip crossing both edges is silent only inside the region (the take is silence, so any
	//! signal in the window is the source sounding twice) and plays on either side of it.
	void overlappingClipIsSilentOnlyInsideTheRegion()
	{
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		SampleTrack* track = trackWithOneClip(scratch.path());
		QVERIFY(track != nullptr);
		const tick_t clipEnd = track->getClips().front()->endPosition().getTicks();
		constexpr tick_t regionStart = 60;
		constexpr tick_t regionEnd = 120;
		QVERIFY2(clipEnd > regionEnd + 40, "the fixture clip must run well past the region");

		const QString silence = taketest::writeToneWav(scratch.filePath(QStringLiteral("silence.wav")), 0.0);
		std::vector<std::pair<tick_t, tick_t>> cut;
		const auto inside = arrangement::isolateRegion(track, regionStart, regionEnd, &cut);
		QCOMPARE(cut.size(), std::size_t{2});  // one cut per edge
		QCOMPARE(inside.size(), std::size_t{1});
		QString why;
		QVERIFY2(track->freezeTo(silence, regionStart, regionEnd, asMuted(inside), &why, true), qPrintable(why));
		QCOMPARE(track->getClips().size(), 3);

		Song* const song = Engine::getSong();
		song->setPlayPos(0, Song::PlayMode::Song);
		song->playSong();
		double before = 0.0;
		double within = 0.0;
		double after = 0.0;
		for (int period = 0; period < 1200 && song->getPlayPos().getTicks() < clipEnd - 4; ++period)
		{
			const tick_t from = song->getPlayPos().getTicks();
			const double level = rms(Engine::audioEngine()->renderNextPeriod());
			const tick_t to = song->getPlayPos().getTicks();
			if (to < regionStart - 4) { before = std::max(before, level); }
			else if (from >= regionStart + 6 && to <= regionEnd - 6) { within = std::max(within, level); }
			else if (from >= regionEnd + 6) { after = std::max(after, level); }
		}
		song->stop();
		QVERIFY2(before > 0.01, "the source must play before the region");
		QVERIFY2(within < 1e-4, qPrintable(QStringLiteral("the source sounded inside the region: %1").arg(within)));
		QVERIFY2(after > 0.01, "the source must resume after the region");
	}

	//! A1b: the region flag survives a save/load, and a whole-track take keeps replacing the source everywhere.
	void regionFlagRoundTripsAndWholeTrackTakeIsUnchanged()
	{
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		SampleTrack* track = trackWithOneClip(scratch.path());
		const QString wav = taketest::writeToneWav(scratch.filePath(QStringLiteral("t.wav")), 330.0);
		QString why;
		QVERIFY2(track->freezeTo(wav, 10, 50, {}, &why, true), qPrintable(why));
		QVERIFY(track->frozenTakeSilencesSource(20));
		QVERIFY(!track->frozenTakeSilencesSource(5));
		QVERIFY(!track->frozenTakeSilencesSource(50));
		Track* reloaded = taketest::reloadTrack(track);
		QVERIFY(reloaded != nullptr);
		QVERIFY(reloaded->frozenTake().region);
		QVERIFY(!reloaded->frozenTakeSilencesSource(5));
		track->unfreeze();
		QVERIFY2(track->freezeTo(wav, 0, 50, {}, &why, false), qPrintable(why));
		QVERIFY(track->frozenTakeSilencesSource(5));
		QVERIFY(track->frozenTakeSilencesSource(500));
	}

	//! A2: with a tempo map in force, the take's playback frames at the region's START and END equal the
	//! frames the render actually cuts for those ticks (BounceInPlace::prepareRenderTracks' window) and the
	//! timeline frames the tempo map gives; the one-rate arithmetic would be off by thousands of frames.
	void takeFramesMatchTheRenderedRegionAtBothEnds()
	{
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		SampleTrack* track = trackWithOneClip(scratch.path());
		const QString wav = taketest::writeToneWav(scratch.filePath(QStringLiteral("t.wav")), 330.0);
		constexpr tick_t regionStart = 24;
		constexpr tick_t regionEnd = 120;
		QString why;
		QVERIFY2(track->freezeTo(wav, regionStart, 190, {}, &why, true), qPrintable(why));

		Song* const song = Engine::getSong();
		QVERIFY(song->tempoMap().edit([](TempoMap& map) {
			map.setActive(true);
			TempoMapEvent event;
			event.tick = 48;
			event.hasTempo = true;
			event.tempo = 70;
			return map.addEvent(event);
		}));
		const int rate = 44100;
		const int globalTempo = static_cast<int>(song->getTempo());
		const TempoMap map = song->tempoMap().map();

		// The render side: the window the bounce cuts out of the session's render.
		BounceInPlace::PreparedRender prepared;
		QString prepareError;
		QVERIFY2(BounceInPlace::prepareRenderTracks({track}, scratch.filePath(QStringLiteral("out.wav")),
			BounceInPlace::Range{regionStart, regionEnd}, rate, &prepared, &prepareError), qPrintable(prepareError));
		const qint64 renderStart = prepared.startFrame;
		const qint64 renderLength = prepared.wantedFrames;
		BounceInPlace::discardPreparedRender(&prepared);
		QCOMPARE(renderStart, static_cast<qint64>(std::llround(map.framesAtTick(regionStart, rate, globalTempo))));
		const double oneRate = (regionEnd - regionStart) * static_cast<double>(Engine::framesPerTick(rate));
		QVERIFY2(std::abs(renderLength - oneRate) > 1000.0, "the fixture tempo map must change the length");

		// The playback side: where in the take the voice starts at the region's start and at its end.
		AudioEngine* const audio = Engine::audioEngine();
		const auto playbackFrame = [&](tick_t position) -> qint64 {
			if (!track->play(TimePos(position), audio->framesPerPeriod(), 0, -1)) { return -1; }
			audio->renderNextPeriod();  // the queued voice joins the engine's handle list
			qint64 frame = -1;
			for (PlayHandle* handle : audio->playHandles())
			{
				auto* voice = handle->type() == PlayHandle::Type::SamplePlayHandle
					? static_cast<SamplePlayHandle*>(handle) : nullptr;
				if (voice != nullptr && voice->isFromTrack(track) && voice->isFrozenTakePoolVoice())
				{
					frame = static_cast<qint64>(SampleBuffer::fromFile(wav)->size()) - voice->totalFrames();
				}
			}
			audio->removePlayHandlesOfTypes(track, PlayHandle::Type::SamplePlayHandle);
			QTest::qWait(130);
			QMetaObject::invokeMethod(song, "playbackPositionJumped", Qt::DirectConnection);
			return frame;
		};
		QCOMPARE(playbackFrame(regionStart), qint64{0});
		QCOMPARE(playbackFrame(regionEnd), renderLength);
		song->tempoMap().edit([](TempoMap& m) { m.clear(); return true; });
	}

	//! A3: a take whose file is gone is reported with its ways out, the source plays on, unfreeze
	//! restores the muted clips and one control.undo brings the take back.
	void missingTakeRecoversThroughUnfreezeAndUndo()
	{
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		SampleTrack* track = trackWithOneClip(scratch.path());
		const QString wav = taketest::writeToneWav(scratch.filePath(QStringLiteral("t.wav")), 330.0);
		QString why;
		QVERIFY2(track->freezeTo(wav, 0, 500, {}, &why, false), qPrintable(why));
		QVERIFY(!track->frozenTakeFileMissing());
		QVERIFY(track->frozenTakeSilencesSource(10));

		const QString id = QStringLiteral("trk-%1").arg(track->id());
		const auto frozenTrack = [&]() -> Track* {
			for (Track* candidate : Engine::getSong()->tracks())
			{
				if (candidate->id() == track->id()) { return candidate; }
			}
			return nullptr;
		};
		// The file moves; the next load of the track (here control.undo's checkpoint restore, the path
		// project.open takes too) finds the take gone.
		QVERIFY(run(QStringLiteral("freeze.unfreeze"), {{QStringLiteral("track"), id}}).ok);
		QVERIFY(QFile::rename(wav, scratch.filePath(QStringLiteral("moved.wav"))));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		Track* restored = frozenTrack();
		QVERIFY(restored != nullptr);
		QVERIFY(restored->isFrozen());
		QVERIFY(restored->frozenTakeFileMissing());
		QVERIFY(!restored->frozenAudioReady());
		QVERIFY2(!restored->frozenTakeSilencesSource(10), "a missing take must not silence the source");

		const ControlResult listed = run(QStringLiteral("track.get_state"), {{QStringLiteral("track"), id}});
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		QVERIFY(QJsonDocument(listed.result).toJson().contains("frozen_audio_missing"));

		// The offered way out: freeze.unfreeze, and one control.undo puts the (still missing) take back.
		const ControlResult off = run(QStringLiteral("freeze.unfreeze"), {{QStringLiteral("track"), id}});
		QVERIFY2(off.ok, qPrintable(off.errorMessage));
		QVERIFY(!frozenTrack()->isFrozen());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY2(frozenTrack()->isFrozen(), "one control.undo must put the unfrozen take back");
	}

	//! A3c: a REGION take saved, moved away, then reopened: the load error and the recovery fields are
	//! there, the source (whose clips the freeze had muted) is audible again, and unfreeze plus undo work.
	void missingRegionTakeIsAudibleAfterReopen()
	{
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		SampleTrack* track = trackWithOneClip(scratch.path());
		const QString wav = taketest::writeToneWav(scratch.filePath(QStringLiteral("region.wav")), 330.0);
		std::vector<std::pair<tick_t, tick_t>> cut;
		const auto inside = arrangement::isolateRegion(track, 60, 120, &cut);
		QString why;
		QVERIFY2(track->freezeTo(wav, 60, 120, asMuted(inside), &why, true), qPrintable(why));
		const int trackId = track->id();
		const QString project = scratch.filePath(QStringLiteral("p.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(project));
		QVERIFY(QFile::remove(wav));

		const ControlResult opened = run(QStringLiteral("project.open"), {{QStringLiteral("path"), project}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		QVERIFY2(QJsonDocument(opened.result).toJson().contains("Frozen take not found"), "the load names the missing take");
		Track* reopened = trackById(trackId);
		QVERIFY(reopened != nullptr);
		QVERIFY(reopened->isFrozen() && reopened->frozenTake().region);
		QVERIFY(reopened->frozenTakeFileMissing() && !reopened->frozenAudioReady());
		QVERIFY2(noClipMuted(reopened), "a clip the freeze muted stays silent");
		const QString id = QStringLiteral("trk-%1").arg(trackId);
		const ControlResult state = run(QStringLiteral("track.get_state"), {{QStringLiteral("track"), id}});
		QVERIFY(state.ok && QJsonDocument(state.result).toJson().contains("frozen_audio_missing"));

		// The source is audible inside the window: the take cannot cover it.
		const double within = loudestBetween(70, 112, 118);
		QVERIFY2(within > 0.01, qPrintable(QStringLiteral("the source is silent inside the missing region take: %1").arg(within)));

		QVERIFY(run(QStringLiteral("freeze.unfreeze"), {{QStringLiteral("track"), id}}).ok);
		QVERIFY(!reopened->isFrozen());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(reopened->isFrozen());
		QVERIFY(noClipMuted(reopened));
	}

	//! A3b: the project-asset scan lists a moved take as missing and project.relink's engine repoints it.
	void missingTakeIsAMissingAssetAndRelinks()
	{
		QTemporaryDir scratch;
		QVERIFY(scratch.isValid());
		const QString gone = scratch.filePath(QStringLiteral("gone.wav"));
		const QString found = taketest::writeToneWav(scratch.filePath(QStringLiteral("found.wav")), 220.0);
		const QString project = scratch.filePath(QStringLiteral("p.mmp"));
		QFile file(project);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(("<?xml version=\"1.0\"?><lmms-project version=\"1.0\" type=\"song\"><head/><song>"
			"<trackcontainer type=\"song\"><track type=\"2\" frozenAudio=\"" + gone + "\"/></trackcontainer>"
			"</song></lmms-project>").toUtf8());
		file.close();

		control::ProjectAssetScan scan;
		ControlResult error;
		QVERIFY(control::controlScanProjectAssets(project, &scan, &error));
		QCOMPARE(scan.references.size(), 1);
		QCOMPARE(scan.references.front().tag, QStringLiteral("track"));
		QCOMPARE(scan.references.front().attribute, QStringLiteral("frozenAudio"));
		QCOMPARE(scan.missingCount(), 1);

		control::ProjectAssetRelink relink;
		QVERIFY(control::controlRelinkProjectAsset(project, gone, found, QString(), false, &relink, &error));
		QCOMPARE(relink.replaced, 1);
		control::ProjectAssetScan after;
		QVERIFY(control::controlScanProjectAssets(project, &after, &error));
		QCOMPARE(after.missingCount(), 0);
	}
};

QTEST_GUILESS_MAIN(FreezeRegionRepairTest)
#include "FreezeRegionRepairTest.moc"
