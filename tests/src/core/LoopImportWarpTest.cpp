/*
 * LoopImportWarpTest.cpp - an imported loop's tempo and key detected, and followed or kept (Q050-093)
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
/*! 0.5.0 L05 (Q050-093): clip.import_file analyses an audio loop and takes a loop mode. A four-bar A major loop at
 *  100 BPM imported into a 140 BPM C major project: follow_project makes it lead at 100 BPM - four bars of the project,
 *  rendered in 100/140 of its own duration - and transposes it +3 semitones (A -> C); keep_unstretched renders its
 *  own duration at zero transpose. The project's tempo and key never move, the remembered preference decides when
 *  no mode is given, one undo removes the import with its configuration, and the state round-trips a save. */

#include <QtTest>

#include <cmath>
#include <memory>
#include <vector>

#include <QDomDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "AudioEngine.h"
#include "LoopFixture.h"
#include "LoopImportWarp.h"
#include "ProjectJournal.h"
#include "ProjectKey.h"
#include "SampleClip.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

constexpr double SourceBpm = 100.0;
constexpr int ProjectTempo = 140;
constexpr int SourceTonic = 9;   // A
constexpr int ProjectTonic = 0;  // C
constexpr tick_t FourBars = 4 * 192;

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The frames the real playback path renders for \a clip: the handle SampleTrack::play builds.
qint64 renderedFrames(SampleClip* clip)
{
	SamplePlayHandle handle(clip, clip->sampleWindow());
	return static_cast<qint64>(handle.totalFrames());
}

SampleClip* clipOf(const ControlResult& imported)
{
	Clip* found = nullptr;
	const QString id = imported.result.value(QStringLiteral("clip")).toString();
	for (Track* track : Engine::getSong()->tracks())
	{
		for (Clip* clip : track->getClips())
		{
			if (control::clipId(clip->id()) == id) { found = clip; }
		}
	}
	return dynamic_cast<SampleClip*>(found);
}

} // namespace

class LoopImportWarpTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		for (const char* name : {"HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"})
		{
			qputenv(name, m_home.path().toLocal8Bit());
		}
		ConfigManager::inst()->loadConfigFile(m_home.filePath(QStringLiteral("zene.xml")));
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY(loopfixture::writeWave(loopPath(), loopfixture::loop(SourceBpm, 220.0)));
		QVERIFY(loopfixture::writeWave(tonePath(), loopfixture::steadyTone()));
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();
		Engine::getSong()->setTempo(ProjectTempo);
		Engine::getSong()->projectKey().set(QStringLiteral("C"), ProjectTonic, QStringLiteral("Major"), 1.0, 1.0,
			QStringLiteral("test"), QString());
		ConfigManager::inst()->deleteValue(QStringLiteral("import"), QStringLiteral("loopwarp"));
	}

	QString loopPath() const { return m_home.filePath(QStringLiteral("a-major-100.wav")); }
	QString tonePath() const { return m_home.filePath(QStringLiteral("steady.wav")); }

	ControlResult import(const QString& mode, const QString& path = QString())
	{
		QJsonObject args{{QStringLiteral("path"), path.isEmpty() ? loopPath() : path}};
		if (!mode.isEmpty()) { args.insert(QStringLiteral("loop_mode"), mode); }
		return run(QStringLiteral("clip.import_file"), args);
	}

	//! Acceptance 1: detected source tempo and key, the clip follows the project tempo and is transposed to its key.
	void followProjectLeadsAtTheDetectedTempoAndTransposesToTheProjectKey()
	{
		const ControlResult imported = import(QStringLiteral("follow_project"));
		QVERIFY2(imported.ok, qPrintable(imported.errorMessage));
		const QJsonObject loop = imported.result.value(QStringLiteral("loop")).toObject();
		QVERIFY(loop.value(QStringLiteral("analysed")).toBool());
		const QJsonObject tempo = loop.value(QStringLiteral("source_tempo")).toObject();
		QVERIFY(tempo.value(QStringLiteral("found")).toBool());
		QVERIFY2(std::abs(tempo.value(QStringLiteral("bpm")).toDouble() - SourceBpm) < 0.5,
			qPrintable(QString::number(tempo.value(QStringLiteral("bpm")).toDouble())));
		const QJsonObject key = loop.value(QStringLiteral("source_key")).toObject();
		QCOMPARE(key.value(QStringLiteral("pitch_class")).toInt(), SourceTonic);
		QCOMPARE(loop.value(QStringLiteral("loop_mode")).toString(), QStringLiteral("follow_project"));
		QCOMPARE(loop.value(QStringLiteral("mode_from")).toString(), QStringLiteral("argument"));

		SampleClip* clip = clipOf(imported);
		QVERIFY(clip != nullptr);
		QCOMPARE(clip->warpTempoMode(), WarpTempoMode::SourceTempo);
		QCOMPARE(clip->sourceTempo(), static_cast<float>(SourceBpm));
		QVERIFY(warpModeKeepsPitch(clip->warpStretchMode()));
		QCOMPARE(clip->clipEdits().transpose, 3);  // A up to C, the nearest way
		QVERIFY(clip->loopSource().tempoFollowed && clip->loopSource().keyFollowed);
		// Its 16 beats occupy four bars of the project.
		QVERIFY2(std::abs(clip->length().getTicks() - FourBars) <= 1, qPrintable(QString::number(clip->length().getTicks())));
		// The project itself is untouched.
		QCOMPARE(static_cast<int>(Engine::getSong()->getTempo()), ProjectTempo);
		QCOMPARE(Engine::getSong()->projectKey().tonicPitchClass(), ProjectTonic);
		QVERIFY(!Engine::getSong()->tempoMap().map().active());
	}

	//! Acceptance 2: the rendered duration is the project grid's when following, the source's when kept.
	void theRenderedDurationMatchesTheLoopMode()
	{
		const ControlResult followed = import(QStringLiteral("follow_project"));
		const ControlResult kept = import(QStringLiteral("keep_unstretched"));
		QVERIFY(followed.ok && kept.ok);
		SampleClip* follow = clipOf(followed);
		SampleClip* keep = clipOf(kept);
		QVERIFY(follow != nullptr && keep != nullptr);

		QCOMPARE(keep->warpTempoMode(), WarpTempoMode::FollowProject);
		QCOMPARE(keep->clipEdits().transpose, 0);
		QVERIFY(keep->rendersLinearly());
		QVERIFY(!keep->loopSource().tempoFollowed && !keep->loopSource().keyFollowed);
		QVERIFY(keep->loopSource().analysed);  // detection stays available to inspect

		const double rate = Engine::audioEngine()->outputSampleRate();
		const double sourceSeconds = static_cast<double>(keep->sample().sampleSize()) / loopfixture::SampleRate;
		const double keptSeconds = static_cast<double>(renderedFrames(keep)) / rate;
		const double followedSeconds = static_cast<double>(renderedFrames(follow)) / rate;
		const double gridSeconds = loopfixture::Beats * 60.0 / ProjectTempo;
		qInfo("source %.4f s, kept render %.4f s, followed render %.4f s, four bars at %d BPM %.4f s",
			sourceSeconds, keptSeconds, followedSeconds, ProjectTempo, gridSeconds);
		QVERIFY2(std::abs(keptSeconds - sourceSeconds) < 0.005, "the kept loop did not render its own duration");
		QVERIFY2(std::abs(followedSeconds - gridSeconds) < 0.01, "the followed loop did not render four project bars");
	}

	//! Acceptance 2: the remembered preference decides when no mode is given; the argument overrides it.
	void theRememberedPreferenceIsTheDefaultAndAnArgumentOverridesIt()
	{
		const ControlResult unset = import(QString());
		QVERIFY(unset.ok);
		QCOMPARE(clipOf(unset)->warpTempoMode(), WarpTempoMode::FollowProject);  // no preference: as an import always was

		QVERIFY(run(QStringLiteral("settings.set"), {{QStringLiteral("key"), loopwarp::preferenceKey()},
			{QStringLiteral("value"), QStringLiteral("follow_project")}}).ok);
		const ControlResult preferred = import(QStringLiteral("preference"));
		QVERIFY(preferred.ok);
		QCOMPARE(preferred.result.value(QStringLiteral("loop")).toObject().value(QStringLiteral("mode_from")).toString(),
			QStringLiteral("preference"));
		QCOMPARE(clipOf(preferred)->warpTempoMode(), WarpTempoMode::SourceTempo);
		const ControlResult later = import(QString());
		QCOMPARE(clipOf(later)->warpTempoMode(), WarpTempoMode::SourceTempo);  // reused by a later import
		const ControlResult overridden = import(QStringLiteral("keep_unstretched"));
		QCOMPARE(clipOf(overridden)->warpTempoMode(), WarpTempoMode::FollowProject);

		QVERIFY(run(QStringLiteral("settings.set"), {{QStringLiteral("key"), loopwarp::preferenceKey()},
			{QStringLiteral("value"), QStringLiteral("keep_unstretched")}}).ok);
		const ControlResult keptByPreference = import(QString());
		QCOMPARE(clipOf(keptByPreference)->warpTempoMode(), WarpTempoMode::FollowProject);
		QCOMPARE(clipOf(keptByPreference)->clipEdits().transpose, 0);
	}

	//! Acceptance 3 (core half): one undo removes the import with its configuration; redo brings both back.
	void oneUndoRemovesTheImportAndItsConfiguration()
	{
		const int tracks = static_cast<int>(Engine::getSong()->tracks().size());
		const int depth = Engine::projectJournal()->undoDepth();
		const ControlResult imported = import(QStringLiteral("follow_project"));
		QVERIFY(imported.ok);
		QCOMPARE(Engine::projectJournal()->undoDepth(), depth + 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracks);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracks + 1);
		auto* track = dynamic_cast<SampleTrack*>(Engine::getSong()->tracks().back());
		QVERIFY(track != nullptr && track->numOfClips() == 1);
		auto* clip = dynamic_cast<SampleClip*>(track->getClip(0));
		QCOMPARE(clip->warpTempoMode(), WarpTempoMode::SourceTempo);
		QCOMPARE(clip->clipEdits().transpose, 3);
		QVERIFY(clip->loopSource().keyFollowed);

		// On an existing track the step is the track's checkpoint: undo takes the clip off, the track stays.
		const ControlResult onTrack = run(QStringLiteral("clip.import_file"), {{QStringLiteral("path"), loopPath()},
			{QStringLiteral("track"), control::trackIdOf(track)}, {QStringLiteral("position"), 4 * 192},
			{QStringLiteral("loop_mode"), QStringLiteral("follow_project")}});
		QVERIFY(onTrack.ok);
		QCOMPARE(track->numOfClips(), 2);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(track->numOfClips(), 1);
	}

	//! A followed clip's longer rendered duration must be included in the preflight song-end check.
	void aFollowedLoopNearTheSongEndIsRefusedBeforeChangingTheProject()
	{
		Engine::getSong()->setTempo(40);
		const ControlResult kept = import(QStringLiteral("keep_unstretched"));
		QVERIFY(kept.ok);
		const qint64 sourceLength = kept.result.value(QStringLiteral("length")).toInt();
		QVERIFY(sourceLength > 0);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		const int tracks = static_cast<int>(Engine::getSong()->tracks().size());
		const int depth = Engine::projectJournal()->undoDepth();
		const ControlResult followed = run(QStringLiteral("clip.import_file"), {
			{QStringLiteral("path"), loopPath()},
			{QStringLiteral("position"), static_cast<double>(MaxSongLength - sourceLength)},
			{QStringLiteral("loop_mode"), QStringLiteral("follow_project")}});
		QVERIFY(!followed.ok);
		QCOMPARE(followed.errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracks);
		QCOMPARE(Engine::projectJournal()->undoDepth(), depth);
	}

	//! clip.source_get_state reads back what was detected and what the clip plays; it refuses a non-sample clip.
	void theStateReadsBackThroughTheAgentCommand()
	{
		const ControlResult imported = import(QStringLiteral("follow_project"));
		QVERIFY(imported.ok);
		const ControlResult state = run(QStringLiteral("clip.source_get_state"),
			{{QStringLiteral("clip"), imported.result.value(QStringLiteral("clip"))}});
		QVERIFY2(state.ok, qPrintable(state.errorMessage));
		QCOMPARE(state.result.value(QStringLiteral("tempo_mode")).toString(), QStringLiteral("source"));
		QCOMPARE(state.result.value(QStringLiteral("warp_source_tempo")).toDouble(), SourceBpm);
		QCOMPARE(state.result.value(QStringLiteral("transpose")).toInt(), 3);
		QCOMPARE(state.result.value(QStringLiteral("loop_mode")).toString(), QStringLiteral("follow_project"));
		QCOMPARE(state.result.value(QStringLiteral("source_key")).toObject().value(QStringLiteral("pitch_class")).toInt(), SourceTonic);

		const ControlResult refusedMode = import(QStringLiteral("sideways"));
		QVERIFY(!refusedMode.ok);
		QCOMPARE(refusedMode.errorKind, ControlErrorKind::InvalidArgs);
	}

	//! Nothing detected is reported, never guessed: the steady tone imports, keeps its duration and says why.
	void aLoopWithNothingToDetectStillImportsAndSaysWhatWasNotApplied()
	{
		const ControlResult imported = import(QStringLiteral("follow_project"), tonePath());
		QVERIFY2(imported.ok, qPrintable(imported.errorMessage));
		const QJsonObject loop = imported.result.value(QStringLiteral("loop")).toObject();
		QVERIFY(!loop.value(QStringLiteral("tempo_followed")).toBool());
		QVERIFY(!loop.value(QStringLiteral("notes")).toArray().isEmpty());
		QVERIFY(clipOf(imported)->rendersLinearly());
		QCOMPARE(static_cast<int>(Engine::getSong()->getTempo()), ProjectTempo);

		// A project with no key: the tempo is followed, the key is not, and the note says so.
		Engine::getSong()->projectKey().clear();
		const ControlResult keyless = import(QStringLiteral("follow_project"));
		const QJsonObject keylessLoop = keyless.result.value(QStringLiteral("loop")).toObject();
		QVERIFY(keylessLoop.value(QStringLiteral("tempo_followed")).toBool());
		QVERIFY(!keylessLoop.value(QStringLiteral("key_followed")).toBool());
		QCOMPARE(clipOf(keyless)->clipEdits().transpose, 0);
		QVERIFY(keylessLoop.value(QStringLiteral("notes")).toArray().first().toString().contains(QStringLiteral("no key")));
		QVERIFY(Engine::getSong()->projectKey().empty());
	}

	//! The state round-trips the clip's own element; a clip element written before this feature loads as before.
	void theLoopSourceRoundTripsAndAnOldClipLoadsAsBefore()
	{
		SampleClip* clip = clipOf(import(QStringLiteral("follow_project")));
		QVERIFY(clip != nullptr);
		QDomDocument doc;
		QDomElement saved = doc.createElement(QStringLiteral("sampleclip"));
		doc.appendChild(saved);
		clip->saveSettings(doc, saved);
		QVERIFY(!saved.firstChildElement(QStringLiteral("loopsource")).isNull());
		const LoopSource before = clip->loopSource();
		clip->setLoopSource(LoopSource{});
		clip->loadSettings(saved);
		QVERIFY(clip->loopSource() == before);

		SampleClip* plain = clipOf(import(QStringLiteral("keep_unstretched")));
		QDomElement old = doc.createElement(QStringLiteral("sampleclip"));
		plain->saveSettings(doc, old);
		old.removeChild(old.firstChildElement(QStringLiteral("loopsource")));  // what a project before Q050-093 holds
		plain->loadSettings(old);
		QVERIFY(!plain->loopSource().analysed);
		QVERIFY(plain->rendersLinearly());
		QCOMPARE(plain->clipEdits().transpose, 0);
	}

	void theNearestTransposeIsWithinATritone()
	{
		QCOMPARE(loopwarp::semitonesToward(9, 0), 3);
		QCOMPARE(loopwarp::semitonesToward(0, 9), -3);
		QCOMPARE(loopwarp::semitonesToward(0, 6), 6);
		QCOMPARE(loopwarp::semitonesToward(4, 4), 0);
		QCOMPARE(loopwarp::semitonesToward(11, 4), 5);
	}

private:
	QTemporaryDir m_home;
};

QTEST_GUILESS_MAIN(LoopImportWarpTest)
#include "LoopImportWarpTest.moc"
