/*
 * AafImportTest.cpp - WF-038: AAF import through LibAAF (include/AafImport.h,
 *                     src/core/ControlCommandsAafImport.cpp).
 *
 * Three halves, so the logic is proved on every machine and the library on the
 * machines that have it:
 *
 *   - the TIME RULE and the small pure helpers - no song, no library;
 *   - the BUILDER, from a hand-made AafModel onto a real song - tracks, clips
 *     (position, length, sample-accurate source window, gain, fades, gain
 *     curve, mute), markers, the missing-media and past-the-song skips - and
 *     a save/load round trip of what it built. No library needed;
 *   - the COMMANDS: the convention and the OMF / missing-file / unavailable
 *     refusals everywhere, and - only when this build has LibAAF (QSKIP
 *     otherwise) - aaf_read and aaf_import of a real AAF fixture, with one
 *     control.undo taking the whole import back and control.redo bringing it.
 *
 * The fixture tests/data/aaf/PR_WAV_Internal.aaf comes from LibAAF's own test
 * suite (GPL-2.0-or-later); see tests/data/aaf/README.md.
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

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "AafImport.h"
#include "BrowserTestSupport.h"
#include "ControlRegistry.h"
#include "ControlTrackCreation.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "ProjectMarkers.h"
#include "SampleClip.h"
#include "SampleTrack.h"
#include "Song.h"

#if __has_include(<libaaf.h>)
#define ZENE_HAVE_LIBAAF
#define aafImportAvailable aafImportAvailableForWarningTest
#define aafLibraryVersion aafLibraryVersionForWarningTest
#define readAaf readAafForWarningTest
#include "../../../src/core/AafImportRead.cpp"
#undef readAaf
#undef aafLibraryVersion
#undef aafImportAvailable
#undef ZENE_HAVE_LIBAAF
#endif

using namespace lmms;
using namespace lmms::interchange;

namespace
{

#if __has_include(<libaaf.h>)
QStringList unsupportedMediaWarningsForTest(int mediaCount)
{
	AafLossReport loss;
	Walk walk{nullptr, nullptr, &loss, {}};
	aafiAudioEssenceFile essence{};
	QByteArray path = QByteArrayLiteral("unsupported.qwenmedia");
	essence.usable_file_path = path.data();
	const AafReadOptions options;
	for (int i = 0; i < mediaCount; ++i)
	{
		AafMedia entry;
		resolveEssence(walk, &essence, options, &entry);
	}
	return loss.warnings;
}
#endif

const QString Fixture = QStringLiteral(LMMS_TEST_DATA_DIR "/aaf/PR_WAV_Internal.aaf");

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

SampleClip* onlyClipOf(Track* track)
{
	return track != nullptr && track->getClips().size() == 1 ? dynamic_cast<SampleClip*>(track->getClips().front())
		: nullptr;
}

//! Two tracks: one with a fully-edited clip, one with a clip whose media is missing and one past the song.
AafModel authoredModel(const QString& wave)
{
	AafClip edited;
	edited.name = QStringLiteral("Line 12");
	edited.mediaName = QStringLiteral("dialogue.wav");
	edited.sourcePath = wave;
	edited.positionFrames = 48000;      // 1 s -> 2 beats at 120 BPM -> 96 ticks
	edited.lengthFrames = 48000;        // 96 ticks
	edited.sourceOffsetFrames = 24000;  // half a second into the file
	edited.gain = 0.5;
	edited.envelope = {AafGainPoint{0.0, -6.f}, AafGainPoint{1.0, 0.f}};
	edited.fadeIn = AafFade{12000, FadeShape::EqualPower};  // 24 ticks
	edited.fadeOut = AafFade{6000, FadeShape::Linear};      // 12 ticks
	edited.muted = true;

	AafTrack dialogue;
	dialogue.number = 1;
	dialogue.name = QStringLiteral("Dialogue");
	dialogue.gain = 0.5;
	dialogue.hasPan = true;
	dialogue.pan = 0.75;
	dialogue.clips = {edited};

	AafClip missing;
	missing.name = QStringLiteral("gone");
	missing.positionFrames = 0;
	missing.lengthFrames = 48000;
	AafClip tooLate = edited;
	tooLate.muted = false;
	tooLate.positionFrames = qint64{48000} * 60 * 60 * 24;  // a day in: past MaxSongLength at any tempo here

	AafTrack effects;
	effects.number = 2;
	effects.clips = {missing, tooLate};

	AafModel model;
	model.compositionName = QStringLiteral("Reel 1");
	model.sampleRate = 48000;
	model.tracks = {dialogue, effects};
	model.markers = {AafMarker{96000, QStringLiteral("Verse"), QStringLiteral("#ff0000")}};
	return model;
}

} // namespace

class AafImportTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY(m_dir.isValid());
		browserfixture::WaveSpec spec;
		spec.sampleRate = 48000;
		spec.channels = 1;
		spec.frames = 96000;
		m_wave = m_dir.filePath(QStringLiteral("dialogue.wav"));
		QVERIFY(browserfixture::writeWave(m_wave, spec));
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();
		ProjectMarkers::instance().clear();
		Engine::getSong()->setTempo(120);
		Engine::projectJournal()->clearJournal();
		ControlRegistry::instance()->clearTransactions();
	}

	void theTimeRuleIsFramesThenTicksAtTheTempo()
	{
		// 30 frames of 29.97 video are 1.001 s: 48048 samples at 48 kHz, exactly.
		QCOMPARE(aafFramesFromEditUnits(30, 30000, 1001, 48000), qint64{48048});
		// A sample-accurate rate is the identity.
		QCOMPARE(aafFramesFromEditUnits(12345, 48000, 1, 48000), qint64{12345});
		// One edit unit of 29.97 at 48 kHz is 1601.6 samples: rounded, not truncated.
		QCOMPARE(aafFramesFromEditUnits(1, 30000, 1001, 48000), qint64{1602});
		QCOMPARE(aafFramesFromEditUnits(10, 0, 1, 48000), qint64{0});
		// One second at 120 BPM is two beats of 48 ticks; at 90 BPM it is 72 ticks.
		QCOMPARE(aafTicksFromFrames(48000, 48000, 120.0), qint64{96});
		QCOMPARE(aafTicksFromFrames(44100, 44100, 90.0), qint64{72});
		QCOMPARE(aafTicksFromFrames(48000, 0, 120.0), qint64{0});
	}

	void theSmallHelpersSayWhatTheyMean()
	{
		QVERIFY(aafIsOmfPath(QStringLiteral("/a/reel.OMF")));
		QVERIFY(aafIsOmfPath(QStringLiteral("/a/reel.omfi")));
		QVERIFY(!aafIsOmfPath(QStringLiteral("/a/reel.aaf")));
		QCOMPARE(aafDefaultMediaDir(QStringLiteral("/in/Reel 1.aaf"), QString()),
			QStringLiteral("/in/Reel 1-aaf-media"));
		QCOMPARE(aafDefaultMediaDir(QStringLiteral("/in/Reel 1.aaf"), QStringLiteral("/songs/Film.mmpz")),
			QStringLiteral("/songs/Film-aaf-media/Reel 1"));
		AafModel model = authoredModel(m_wave);
		QCOMPARE(model.clipCount(), 3);
		QCOMPARE(model.firstClipFrame(), qint64{0});
		model.media = {AafMedia{QStringLiteral("a"), QStringLiteral("file:///x.wav"), QString(), false, true, 1, 48000},
			AafMedia{QStringLiteral("b"), QStringLiteral("file:///x.wav"), QString(), false, true, 1, 48000},
			AafMedia{QStringLiteral("c"), QString(), QString(), true, false, 1, 48000}};
		QCOMPARE(model.missingMedia(), QStringList{QStringLiteral("file:///x.wav")});  // once, and not the embedded one
	}

	//! The builder, on a hand-made model: everything the model carries lands where it says.
	void theBuilderMakesTracksClipsAndMarkers()
	{
		std::vector<int> made;
		const AafBuildReport report = buildAafSession(Engine::getSong(), authoredModel(m_wave), AafBuildOptions{}, &made);
		QCOMPARE(report.tracks, 2);
		QCOMPARE(report.clips, 1);
		QCOMPARE(report.markers, 1);
		QCOMPARE(report.missingMediaClips, 1);
		QCOMPARE(report.beyondSongLength, 1);
		QCOMPARE(made.size(), std::size_t{2});

		auto* dialogue = dynamic_cast<SampleTrack*>(control::trackWithId(made[0]));
		QVERIFY(dialogue != nullptr);
		QCOMPARE(dialogue->name(), QStringLiteral("Dialogue"));
		QCOMPARE(dialogue->volumeModel()->value(), 50.f);
		QCOMPARE(dialogue->panningModel()->value(), 50.f);
		QCOMPARE(control::trackWithId(made[1])->name(), QStringLiteral("AAF track 2"));
		QVERIFY(control::trackWithId(made[1])->getClips().empty());

		SampleClip* clip = onlyClipOf(dialogue);
		QVERIFY(clip != nullptr);
		QCOMPARE(clip->startPosition().getTicks(), 96);
		QCOMPARE(clip->length().getTicks(), 96);
		QCOMPARE(clip->sampleWindow().sourceIn, f_cnt_t{24000});
		QCOMPARE(clip->name(), QStringLiteral("Line 12"));
		QVERIFY(clip->isMuted());
		QVERIFY(!clip->getAutoResize());
		const ClipEdits& edits = clip->clipEdits();
		QCOMPARE(edits.gain, 0.5f);
		QCOMPARE(edits.fadeInTicks, 24);
		QCOMPARE(edits.fadeOutTicks, 12);
		QCOMPARE(edits.fadeInShape, FadeShape::EqualPower);
		QCOMPARE(edits.envelopeCount, 2);
		QCOMPARE(edits.envelope[1].tick, 96);
		QCOMPARE(edits.envelope[0].gainDb, -6.f);

		const QVector<Marker>& markers = ProjectMarkers::instance().markers();
		QCOMPARE(markers.size(), 1);
		QCOMPARE(markers[0].tick, tick_t{192});
		QCOMPARE(markers[0].name, QStringLiteral("Verse"));
		QCOMPARE(markers[0].color, QStringLiteral("#ff0000"));
	}

	void theShiftStartsTheFirstClipAtZero()
	{
		AafModel model = authoredModel(m_wave);
		model.tracks.removeLast();
		AafBuildOptions options;
		options.shiftFrames = model.firstClipFrame();
		options.markers = false;
		std::vector<int> made;
		buildAafSession(Engine::getSong(), model, options, &made);
		SampleClip* clip = onlyClipOf(control::trackWithId(made.at(0)));
		QVERIFY(clip != nullptr);
		QCOMPARE(clip->startPosition().getTicks(), 0);
		QVERIFY(ProjectMarkers::instance().markers().isEmpty());
	}

	//! What the builder made is ordinary project state: it survives a save and a load.
	void whatItBuildsSurvivesASaveAndALoad()
	{
		std::vector<int> made;
		buildAafSession(Engine::getSong(), authoredModel(m_wave), AafBuildOptions{}, &made);
		const QString project = m_dir.filePath(QStringLiteral("imported.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(project));
		Engine::getSong()->clearProject();
		Engine::getSong()->loadProject(project);
		SampleClip* clip = nullptr;
		for (Track* track : Engine::getSong()->tracks())
		{
			if (track->name() == QStringLiteral("Dialogue")) { clip = onlyClipOf(track); }
		}
		QVERIFY(clip != nullptr);
		QCOMPARE(clip->startPosition().getTicks(), 96);
		QCOMPARE(clip->sampleWindow().sourceIn, f_cnt_t{24000});
		QCOMPARE(clip->clipEdits().fadeInTicks, 24);
		QCOMPARE(clip->clipEdits().envelopeCount, 2);
		QCOMPARE(ProjectMarkers::instance().markers().size(), 1);
	}

	void theConventionSaysWhetherThisBuildReadsAaf()
	{
		const ControlResult result = run(QStringLiteral("interchange.aaf_convention"));
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		QCOMPARE(result.result.value(QStringLiteral("available")).toBool(), aafImportAvailable());
		QCOMPARE(result.result.value(QStringLiteral("omf_supported")).toBool(), false);
		QVERIFY(!result.result.value(QStringLiteral("stated_losses")).toString().isEmpty());
	}

	void unsupportedMediaWarningsRespectTheReportLimit()
	{
#if __has_include(<libaaf.h>)
		if (!aafImportAvailable()) { QSKIP("this build has no LibAAF (ZENE_HAVE_LIBAAF not defined)"); }
		QCOMPARE(unsupportedMediaWarningsForTest(AafMaxWarnings + 5).size(), AafMaxWarnings);
#else
		QSKIP("this build has no LibAAF headers");
#endif
	}

	//! Refusals every build gives, library or not.
	void theRefusalsAreTyped()
	{
		QCOMPARE(run(QStringLiteral("interchange.aaf_read"), {{QStringLiteral("path"), m_dir.filePath("absent.aaf")}})
			.errorKind, ControlErrorKind::NotFound);
		const QString omf = m_dir.filePath(QStringLiteral("reel.omf"));
		QFile file(omf);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write("not really OMF");
		file.close();
		const ControlResult refused = run(QStringLiteral("interchange.aaf_import"), {{QStringLiteral("path"), omf}});
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QVERIFY(refused.errorMessage.contains(QStringLiteral("OMF")));

		const QString junk = m_dir.filePath(QStringLiteral("junk.aaf"));
		QFile junkFile(junk);
		QVERIFY(junkFile.open(QIODevice::WriteOnly));
		junkFile.write("this is not a compound file");
		junkFile.close();
		const ControlResult unreadable = run(QStringLiteral("interchange.aaf_read"), {{QStringLiteral("path"), junk}});
		QVERIFY(!unreadable.ok);
		if (aafImportAvailable()) { QCOMPARE(unreadable.errorKind, ControlErrorKind::InvalidArgs); }
		else
		{
			QCOMPARE(unreadable.errorKind, ControlErrorKind::Requires);
			QVERIFY(unreadable.errorMessage.startsWith(QStringLiteral("unavailable")));
		}
		QCOMPARE(Engine::projectJournal()->undoDepth(), 0);  // a refusal records nothing
	}

	//! LibAAF on the real fixture: the dry run reads it and writes nothing.
	void theDryRunReadsTheFixture()
	{
		if (!aafImportAvailable()) { QSKIP("this build has no LibAAF (ZENE_HAVE_LIBAAF not defined)"); }
		const int tracksBefore = static_cast<int>(Engine::getSong()->tracks().size());
		const ControlResult result = run(QStringLiteral("interchange.aaf_read"), {{QStringLiteral("path"), Fixture}});
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		const QJsonObject reply = result.result;
		QCOMPARE(reply.value(QStringLiteral("track_count")).toInt(), 1);
		QCOMPARE(reply.value(QStringLiteral("clip_count")).toInt(), 1);
		QCOMPARE(reply.value(QStringLiteral("sample_rate")).toInt(), 48000);
		QCOMPARE(reply.value(QStringLiteral("edit_rate")).toString(), QStringLiteral("30000/1001"));
		QCOMPARE(reply.value(QStringLiteral("embedded_media")).toInt(), 1);
		QVERIFY(reply.value(QStringLiteral("missing_media")).toArray().isEmpty());
		QCOMPARE(reply.value(QStringLiteral("loss")).toObject().value(QStringLiteral("video_tracks")).toInt(), 1);
		const QJsonObject clip = reply.value(QStringLiteral("model")).toObject().value(QStringLiteral("tracks"))
			.toArray().at(0).toObject().value(QStringLiteral("clips")).toArray().at(0).toObject();
		QCOMPARE(clip.value(QStringLiteral("length_frames")).toInteger(), qint64{48048});
		QCOMPARE(clip.value(QStringLiteral("source_offset_frames")).toInteger(), qint64{1602});
		QVERIFY(!QFileInfo::exists(reply.value(QStringLiteral("media_dir")).toString()));  // nothing extracted
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracksBefore);
	}

	//! The import extracts the embedded WAV, builds the track and its clip, and is ONE undo step.
	void theImportIsOneUndoStep()
	{
		if (!aafImportAvailable()) { QSKIP("this build has no LibAAF (ZENE_HAVE_LIBAAF not defined)"); }
		const int tracksBefore = static_cast<int>(Engine::getSong()->tracks().size());
		const QString media = m_dir.filePath(QStringLiteral("media"));
		const ControlResult result = run(QStringLiteral("interchange.aaf_import"), {{QStringLiteral("path"), Fixture},
			{QStringLiteral("media_dir"), media}, {QStringLiteral("start_at_first_clip"), true}});
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		QCOMPARE(result.result.value(QStringLiteral("built_tracks")).toInt(), 1);
		QCOMPARE(result.result.value(QStringLiteral("built_clips")).toInt(), 1);
		QVERIFY(QFileInfo::exists(QDir(media).filePath(QStringLiteral("1000hz-18dbs16b44.1k.wav"))));
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracksBefore + 1);

		Track* imported = Engine::getSong()->tracks().back();
		QCOMPARE(imported->name(), QStringLiteral("Audio 1"));
		SampleClip* clip = onlyClipOf(imported);
		QVERIFY(clip != nullptr);
		QCOMPARE(clip->startPosition().getTicks(), 0);
		QCOMPARE(clip->length().getTicks(), static_cast<int>(aafTicksFromFrames(48048, 48000, 120.0)));
		QCOMPARE(clip->sampleWindow().sourceIn, f_cnt_t{1602});

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracksBefore);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), tracksBefore + 1);
		QVERIFY(onlyClipOf(Engine::getSong()->tracks().back()) != nullptr);
	}

private:
	QTemporaryDir m_dir;
	QString m_wave;
};

QTEST_GUILESS_MAIN(AafImportTest)
#include "AafImportTest.moc"
