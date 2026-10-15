/*
 * ControlBounceRenderTest.cpp - bounce.land, track.flatten, range.bounce and the subproject verbs, through a stand-in render child
 *
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

/*! These verbs render through a CHILD PROCESS: BounceInPlace and project.subproject_add run the executable
 *  they live in as `<exe> render <project> -o <out> ...` (the product's CLI renderer), because a render in the
 *  instance itself would drive its audio device. A test binary is not the product, so its main() answers that
 *  command line itself (fakeRender): it reads the serialised project it is given, counts the audio tracks the
 *  bounce left UNMUTED, and writes a WAV whose level is 0.1 per such track. Everything around the child is the
 *  product's own code and runs for real - the serialisation, the muting of the other tracks, the trim to a
 *  range, the new audio track, the undo steps - and the level in the written file proves which tracks the
 *  render was told to leave audible. Setting ZENE_FAKE_RENDER_FAIL in the environment makes the child fail
 *  (exit 3, no file), which is how the render-failed refusals are reached. */

#include <QtTest>

#include <QDateTime>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <sndfile.h>

#include "BounceInPlace.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "MidiClip.h"
#include "PathUtil.h"
#include "SampleClip.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

constexpr int FakeRate = 44100;
constexpr int FakeSeconds = 4;

struct RenderRequest
{
	QString project;
	QString out;
	int rate = FakeRate;
};

RenderRequest parseRenderArguments(int argc, char** argv)
{
	RenderRequest request;
	if (argc > 2) { request.project = QString::fromLocal8Bit(argv[2]); }
	for (int i = 3; i + 1 < argc; ++i)
	{
		const QString arg = QString::fromLocal8Bit(argv[i]);
		if (arg == QLatin1String("-o")) { request.out = QString::fromLocal8Bit(argv[++i]); }
		else if (arg == QLatin1String("-s")) { request.rate = QString::fromLocal8Bit(argv[++i]).toInt(); }
	}
	return request;
}

//! How many audio tracks (instrument, pattern, sample) of the serialised project are left unmuted; -1 when unreadable.
int audibleTracks(const QString& project)
{
	QFile file(project);
	QDomDocument document;
	if (!file.open(QIODevice::ReadOnly) || !document.setContent(&file)) { return -1; }
	int audible = 0;
	const QDomNodeList tracks = document.elementsByTagName(QStringLiteral("track"));
	for (int i = 0; i < tracks.count(); ++i)
	{
		const QDomElement track = tracks.at(i).toElement();
		const bool audio = track.attribute(QStringLiteral("type")).toInt() <= 2;
		if (audio && track.attribute(QStringLiteral("muted")).toInt() == 0) { ++audible; }
	}
	return audible;
}

bool writeTone(const QString& path, int rate, double level)
{
	SF_INFO info{};
	info.channels = 2;
	info.samplerate = rate;
	info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
	SNDFILE* wav = sf_open(path.toLocal8Bit().constData(), SFM_WRITE, &info);
	if (wav == nullptr) { return false; }
	std::vector<float> block(2 * 1024);
	const qint64 total = static_cast<qint64>(rate) * FakeSeconds;
	for (qint64 done = 0; done < total; done += 1024)
	{
		const int frames = static_cast<int>(std::min<qint64>(1024, total - done));
		for (int f = 0; f < frames; ++f)
		{
			block[2 * f] = block[2 * f + 1] = static_cast<float>(level * std::sin(2.0 * M_PI * 440.0 * static_cast<double>(done + f) / rate));
		}
		sf_writef_float(wav, block.data(), frames);
	}
	sf_close(wav);
	return true;
}

//! The stand-in for `zene render <project> -o <out> ...`: a tone at 0.1 per unmuted audio track of the project.
int fakeRender(int argc, char** argv)
{
	if (std::getenv("ZENE_FAKE_RENDER_FAIL") != nullptr) { return 3; }
	const RenderRequest request = parseRenderArguments(argc, argv);
	const int audible = audibleTracks(request.project);
	if (request.out.isEmpty() || audible < 0) { return 4; }
	// main.cpp replaces the requested suffix with the selected render format;
	// the BounceInPlace child always asks for WAV.
	const QFileInfo requested(request.out);
	const QString output = requested.dir().filePath(requested.completeBaseName() + QStringLiteral(".wav"));
	return writeTone(output, request.rate, 0.1 * std::max(1, audible)) ? 0 : 5;
}

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! An instrument track named @a name holding one clip (a bounce of a track with no clip is refused).
Track* trackWithClip(const QString& name)
{
	Track* track = Track::create(Track::Type::Instrument, Engine::getSong());
	track->setName(name);
	track->createClip(TimePos(0));
	return track;
}

Track* resolved(const QString& id)
{
	ControlResult ignored;
	return control::resolveTrack(id, &ignored);
}

//! The peak of a WAV, and its frame count (-1 when it cannot be read).
struct WavFacts
{
	float peak = 0.f;
	qint64 frames = -1;
	int rate = 0;
};

WavFacts wavFacts(const QString& path)
{
	WavFacts facts;
	SF_INFO info{};
	SNDFILE* wav = sf_open(path.toLocal8Bit().constData(), SFM_READ, &info);
	if (wav == nullptr) { return facts; }
	facts.frames = info.frames;
	facts.rate = info.samplerate;
	std::vector<float> data(static_cast<size_t>(info.frames) * info.channels);
	sf_readf_float(wav, data.data(), info.frames);
	for (const float v : data) { facts.peak = std::max(facts.peak, std::fabs(v)); }
	sf_close(wav);
	return facts;
}

int trackCount() { return static_cast<int>(Engine::getSong()->tracks().size()); }

} // namespace

class ControlBounceRenderTest : public QObject
{
	Q_OBJECT

	QTemporaryDir m_dir;

	QString out(const QString& name) const { return QDir(m_dir.path()).filePath(name); }

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		qunsetenv("ZENE_FAKE_RENDER_FAIL");
		Engine::getSong()->clearProject();
	}

	void bounceLandPutsTheRenderOnANewTrackAndMutesTheSource()
	{
		Track* source = trackWithClip(QStringLiteral("Lead"));
		trackWithClip(QStringLiteral("Pad"));  // a second audible track the bounce must silence in the render
		const QString id = control::trackIdOf(source);
		const int before = trackCount();
		const QString wav = out(QStringLiteral("land.wav"));

		const ControlResult landed = run(QStringLiteral("bounce.land"), {{QStringLiteral("track"), id}, {QStringLiteral("out"), wav}});
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));
		QCOMPARE(landed.result.value(QStringLiteral("path")).toString(), wav);
		QCOMPARE(landed.result.value(QStringLiteral("track")).toString(), id);
		QCOMPARE(landed.result.value(QStringLiteral("sample_rate")).toInt(), FakeRate);
		QVERIFY(!landed.result.value(QStringLiteral("sha256")).toString().isEmpty());

		// The file is the render of ONE track: the other audible track was muted in the serialised copy.
		const WavFacts facts = wavFacts(wav);
		QCOMPARE(facts.frames, static_cast<qint64>(FakeRate) * FakeSeconds);
		QVERIFY2(std::fabs(facts.peak - 0.1f) < 0.01f, qPrintable(QStringLiteral("peak %1").arg(facts.peak)));
		QCOMPARE(landed.result.value(QStringLiteral("frames")).toInteger(), facts.frames);

		// A NEW audio track named after the source holds the file; the source is muted.
		QCOMPARE(trackCount(), before + 1);
		Track* landedTrack = resolved(landed.result.value(QStringLiteral("landed_track")).toString());
		QVERIFY(landedTrack != nullptr);
		QCOMPARE(landedTrack->type(), Track::Type::Sample);
		QCOMPARE(landedTrack->name(), QStringLiteral("Lead (bounce)"));
		QCOMPARE(landedTrack->getClips().size(), size_t{1});
		auto* clip = dynamic_cast<SampleClip*>(landedTrack->getClips().front());
		QVERIFY(clip != nullptr);
		QCOMPARE(PathUtil::toAbsolute(clip->sampleFile()), wav);
		QVERIFY(source->isMuted());

		// One undo removes the new track and restores the source's mute; redo lands the same file again.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(trackCount(), before);
		QVERIFY(!source->isMuted());
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(trackCount(), before + 1);
		QVERIFY(source->isMuted());
	}

	void bounceLandOfARangeLandsAtItsStartAndTrimsTheRender()
	{
		Track* source = trackWithClip(QStringLiteral("Keys"));
		const QString wav = out(QStringLiteral("range.wav"));
		const ControlResult landed = run(QStringLiteral("bounce.land"), {{QStringLiteral("track"), control::trackIdOf(source)},
			{QStringLiteral("out"), wav}, {QStringLiteral("start"), 48}, {QStringLiteral("end"), 144}});
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));
		QCOMPARE(landed.result.value(QStringLiteral("start_ticks")).toInt(), 48);
		// 96 ticks of the project's tempo, not the render's whole length.
		const qint64 frames = wavFacts(wav).frames;
		QVERIFY2(frames > 0 && frames < static_cast<qint64>(FakeRate) * FakeSeconds / 2, qPrintable(QString::number(frames)));
		QCOMPARE(landed.result.value(QStringLiteral("frames")).toInteger(), frames);
		Track* landedTrack = resolved(landed.result.value(QStringLiteral("landed_track")).toString());
		QVERIFY(landedTrack != nullptr);
		QCOMPARE(static_cast<int>(landedTrack->getClips().front()->startPosition().getTicks()), 48);
	}

	void bounceLandRefusesWhatItCannotRender()
	{
		Track* source = trackWithClip(QStringLiteral("Lead"));
		const QString id = control::trackIdOf(source);
		const QString wav = out(QStringLiteral("refused.wav"));
		const int before = trackCount();

		QCOMPARE(run(QStringLiteral("bounce.land"), {{QStringLiteral("track"), QStringLiteral("trk-999999")}, {QStringLiteral("out"), wav}}).errorKind,
			ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("bounce.land"), {{QStringLiteral("track"), id}, {QStringLiteral("out"), wav}, {QStringLiteral("start"), 48}}).errorKind,
			ControlErrorKind::InvalidArgs);  // a start without an end
		QCOMPARE(run(QStringLiteral("bounce.land"), {{QStringLiteral("track"), id}, {QStringLiteral("out"), wav},
			{QStringLiteral("start"), 96}, {QStringLiteral("end"), 48}}).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("bounce.land"), {{QStringLiteral("track"), id}, {QStringLiteral("out"), QStringLiteral("relative.wav")}}).errorKind,
			ControlErrorKind::Refused);  // the output path must be absolute

		Track* empty = Track::create(Track::Type::Instrument, Engine::getSong());
		const ControlResult noClips = run(QStringLiteral("bounce.land"), {{QStringLiteral("track"), control::trackIdOf(empty)}, {QStringLiteral("out"), wav}});
		QCOMPARE(noClips.errorKind, ControlErrorKind::Refused);
		QVERIFY(noClips.errorMessage.contains(QStringLiteral("no clips")));

		// A render that fails leaves the project exactly as it was: no track, no mute, no file.
		qputenv("ZENE_FAKE_RENDER_FAIL", "1");
		const int tracks = trackCount();
		const ControlResult failed = run(QStringLiteral("bounce.land"), {{QStringLiteral("track"), id}, {QStringLiteral("out"), wav}});
		QCOMPARE(failed.errorKind, ControlErrorKind::Refused);
		QVERIFY(failed.errorMessage.contains(QStringLiteral("render failed")));
		QCOMPARE(trackCount(), tracks);
		QVERIFY(!source->isMuted());
		QVERIFY(!QFileInfo::exists(wav));
		QCOMPARE(before + 1, tracks);
	}

	/*! A bounce whose output path IS the open project file must refuse. The render
	 *  is staged and then installed over the output path, so without the guard the
	 *  session file is replaced by WAV bytes. The session is saved for real first,
	 *  so it is the open project; the refusal must leave those bytes identical and
	 *  add no track.
	 */
	void bounceRefusesToOverwriteTheOpenProjectFile()
	{
		Track* source = trackWithClip(QStringLiteral("Lead"));
		const QString project = out(QStringLiteral("session.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(project));
		Engine::getSong()->noteProjectSaved(project);  // what save-as does once the file is written
		QCOMPARE(Engine::getSong()->projectFileName(), project);
		QFile saved(project);
		QVERIFY(saved.open(QIODevice::ReadOnly));
		const QByteArray session = saved.readAll();
		saved.close();
		QVERIFY(!session.isEmpty());
		const int tracks = trackCount();

		const ControlResult refused = run(QStringLiteral("bounce.land"), {{QStringLiteral("track"), control::trackIdOf(source)}, {QStringLiteral("out"), project}});
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QVERIFY2(refused.errorMessage.contains(QStringLiteral("open project file")), qPrintable(refused.errorMessage));
		QCOMPARE(trackCount(), tracks);
		QVERIFY(!source->isMuted());

		QFile after(project);
		QVERIFY(after.open(QIODevice::ReadOnly));
		QCOMPARE(after.readAll(), session);
	}

	void flattenReplacesTheTrackWithItsRenderAndUndoRestoresIt()
	{
		trackWithClip(QStringLiteral("First"));
		Track* source = trackWithClip(QStringLiteral("Second"));
		trackWithClip(QStringLiteral("Third"));
		const QString wav = out(QStringLiteral("flat.wav"));
		const int before = trackCount();
		QCOMPARE(Engine::getSong()->tracks().at(1), source);

		const ControlResult flat = run(QStringLiteral("track.flatten"), {{QStringLiteral("track"), control::trackIdOf(source)}, {QStringLiteral("out"), wav}});
		QVERIFY2(flat.ok, qPrintable(flat.errorMessage));
		QVERIFY(flat.result.value(QStringLiteral("flattened")).toString().startsWith(QStringLiteral("trk-")));
		QCOMPARE(trackCount(), before);
		// The audio track took the source's place in the list and its name, and plays the render.
		Track* audio = Engine::getSong()->tracks().at(1);
		QCOMPARE(audio->type(), Track::Type::Sample);
		QCOMPARE(audio->name(), QStringLiteral("Second"));
		QCOMPARE(control::trackIdOf(audio), flat.result.value(QStringLiteral("track")).toString());
		auto* clip = dynamic_cast<SampleClip*>(audio->getClips().front());
		QVERIFY(clip != nullptr);
		QCOMPARE(PathUtil::toAbsolute(clip->sampleFile()), wav);
		QVERIFY(std::fabs(wavFacts(wav).peak - 0.1f) < 0.01f);

		// One undo puts the instrument track back at the same place with its clip; redo flattens again.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(trackCount(), before);
		Track* back = Engine::getSong()->tracks().at(1);
		QCOMPARE(back->type(), Track::Type::Instrument);
		QCOMPARE(back->name(), QStringLiteral("Second"));
		QCOMPARE(back->getClips().size(), size_t{1});
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(Engine::getSong()->tracks().at(1)->type(), Track::Type::Sample);
	}

	void flattenRefusesAndLeavesTheTrackWhenTheRenderFails()
	{
		Track* source = trackWithClip(QStringLiteral("Solo"));
		const QString id = control::trackIdOf(source);
		const QString wav = out(QStringLiteral("flat-fail.wav"));
		QCOMPARE(run(QStringLiteral("track.flatten"), {{QStringLiteral("track"), QStringLiteral("trk-999999")}}).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("track.flatten"), {{QStringLiteral("track"), id}, {QStringLiteral("end"), 96}}).errorKind, ControlErrorKind::InvalidArgs);

		qputenv("ZENE_FAKE_RENDER_FAIL", "1");
		const ControlResult failed = run(QStringLiteral("track.flatten"), {{QStringLiteral("track"), id}, {QStringLiteral("out"), wav}});
		QCOMPARE(failed.errorKind, ControlErrorKind::Refused);
		QCOMPARE(trackCount(), 1);
		QCOMPARE(Engine::getSong()->tracks().front(), source);  // the same object: nothing was replaced
		QVERIFY(!QFileInfo::exists(wav));
	}

	void rangeBounceRendersTheSelectedTracksTogetherOntoOneNewTrack()
	{
		Track* a = trackWithClip(QStringLiteral("A"));
		Track* b = trackWithClip(QStringLiteral("B"));
		Track* c = trackWithClip(QStringLiteral("C"));
		const int before = trackCount();
		const QString wav = out(QStringLiteral("selection.wav"));

		// Nothing selected: refused, typed.
		QCOMPARE(run(QStringLiteral("range.bounce"), {{QStringLiteral("out"), wav}}).errorKind, ControlErrorKind::Refused);

		QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 192},
			{QStringLiteral("tracks"), QJsonArray{control::trackIdOf(a), control::trackIdOf(b)}}}).ok);
		const ControlResult bounced = run(QStringLiteral("range.bounce"), {{QStringLiteral("out"), wav}});
		QVERIFY2(bounced.ok, qPrintable(bounced.errorMessage));
		QCOMPARE(bounced.result.value(QStringLiteral("tracks")).toInt(), 2);
		// Two selected tracks were left audible together, the third was muted out of the render.
		QVERIFY2(std::fabs(wavFacts(wav).peak - 0.2f) < 0.01f, qPrintable(QString::number(wavFacts(wav).peak)));
		QCOMPARE(trackCount(), before + 1);
		Track* landed = resolved(bounced.result.value(QStringLiteral("landed_track")).toString());
		QVERIFY(landed != nullptr);
		QCOMPARE(landed->name(), QStringLiteral("Bounce"));
		QVERIFY(a->isMuted() && b->isMuted());
		QVERIFY(!c->isMuted());

		// One undo takes the track away and the mutes back; redo lands again.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(trackCount(), before);
		QVERIFY(!a->isMuted() && !b->isMuted());
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(trackCount(), before + 1);
		QVERIFY(a->isMuted() && b->isMuted());
	}

	void rangeBounceKeepsTheSourcesAudibleWhenAskedAndRefusesAFailedRender()
	{
		Track* a = trackWithClip(QStringLiteral("A"));
		QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 96},
			{QStringLiteral("tracks"), QJsonArray{control::trackIdOf(a)}}}).ok);
		const ControlResult kept = run(QStringLiteral("range.bounce"), {{QStringLiteral("out"), out(QStringLiteral("kept.wav"))},
			{QStringLiteral("mute_sources"), false}});
		QVERIFY2(kept.ok, qPrintable(kept.errorMessage));
		QVERIFY(!a->isMuted());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!a->isMuted());  // only the mutes land() set are put back, and it set none

		qputenv("ZENE_FAKE_RENDER_FAIL", "1");
		const int tracks = trackCount();
		QCOMPARE(run(QStringLiteral("range.bounce"), {{QStringLiteral("out"), out(QStringLiteral("failed.wav"))}}).errorKind, ControlErrorKind::Refused);
		QCOMPARE(trackCount(), tracks);
	}

	void aSubprojectIsRenderedLinkedListedAndRefreshed()
	{
		Song* song = Engine::getSong();
		trackWithClip(QStringLiteral("Inner"));
		const QString inner = out(QStringLiteral("inner.mmp"));
		QVERIFY(song->saveProjectFile(inner));
		song->clearProject();
		const QString main = out(QStringLiteral("main.mmp"));
		QVERIFY(song->guiSaveProjectAs(main));

		const ControlResult added = run(QStringLiteral("project.subproject_add"), {{QStringLiteral("path"), inner}, {QStringLiteral("at"), 96}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		const QString audio = added.result.value(QStringLiteral("audio")).toString();
		QVERIFY(QFileInfo::exists(audio));
		QVERIFY2(audio.contains(QStringLiteral("main-subprojects")), qPrintable(audio));
		Track* track = resolved(added.result.value(QStringLiteral("track")).toString());
		QVERIFY(track != nullptr);
		QCOMPARE(track->name(), QStringLiteral("inner"));
		QCOMPARE(static_cast<int>(track->getClips().front()->startPosition().getTicks()), 96);

		const ControlResult listed = run(QStringLiteral("project.subprojects"));
		QCOMPARE(listed.result.value(QStringLiteral("count")).toInt(), 1);
		const QJsonObject entry = listed.result.value(QStringLiteral("subprojects")).toArray().first().toObject();
		QCOMPARE(entry.value(QStringLiteral("path")).toString(), inner);
		QCOMPARE(entry.value(QStringLiteral("audio")).toString(), audio);
		QVERIFY(entry.value(QStringLiteral("exists")).toBool());
		QVERIFY(!entry.value(QStringLiteral("stale")).toBool());

		// The link is saved with the project and read back with it.
		const QString saved = out(QStringLiteral("main-saved.mmp"));
		QVERIFY(song->saveProjectFile(saved));
		QFile file(saved);
		QVERIFY(file.open(QIODevice::ReadOnly));
		QVERIFY(file.readAll().contains("<subprojects>"));

		// The linked file changed after its render: it is reported stale, and a refresh renders it again.
		// Stale means "modified after the render": set the file's time one millisecond past the recorded render time
		// (exact, whatever the filesystem's clock granularity), instead of waiting for the clock to move.
		const qint64 renderedMs = static_cast<qint64>(listed.result.value(QStringLiteral("subprojects")).toArray().first().toObject()
			.value(QStringLiteral("rendered_ms")).toDouble());
		QFile touch(inner);
		QVERIFY(touch.open(QIODevice::Append));
		touch.write("\n");
		QVERIFY(touch.flush());  // the write reaches the file first; the time is set after it
		QVERIFY(touch.setFileTime(QDateTime::fromMSecsSinceEpoch(renderedMs + 1), QFileDevice::FileModificationTime));
		touch.close();
		QVERIFY(run(QStringLiteral("project.subprojects")).result.value(QStringLiteral("subprojects")).toArray().first().toObject()
			.value(QStringLiteral("stale")).toBool());
		const ControlResult refreshed = run(QStringLiteral("project.subproject_refresh"), {{QStringLiteral("track"), control::trackIdOf(track)}});
		QVERIFY2(refreshed.ok, qPrintable(refreshed.errorMessage));
		const QString newAudio = refreshed.result.value(QStringLiteral("audio")).toString();
		QVERIFY(newAudio != audio);
		auto* clip = dynamic_cast<SampleClip*>(track->getClips().front());
		QVERIFY(clip != nullptr);
		QCOMPARE(PathUtil::toAbsolute(clip->sampleFile()), newAudio);
		QVERIFY(!run(QStringLiteral("project.subprojects")).result.value(QStringLiteral("subprojects")).toArray().first().toObject()
			.value(QStringLiteral("stale")).toBool());
		// One undo points the clips back at the first render.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(PathUtil::toAbsolute(dynamic_cast<SampleClip*>(track->getClips().front())->sampleFile()), audio);
		// And one more removes the track and the link.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(run(QStringLiteral("project.subprojects")).result.value(QStringLiteral("count")).toInt(), 0);
		QVERIFY(resolved(added.result.value(QStringLiteral("track")).toString()) == nullptr);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(run(QStringLiteral("project.subprojects")).result.value(QStringLiteral("count")).toInt(), 1);
	}

	void subprojectRefusalsAreTypedAndLeaveNothingBehind()
	{
		Song* song = Engine::getSong();
		trackWithClip(QStringLiteral("Inner"));
		const QString inner = out(QStringLiteral("inner2.mmp"));
		QVERIFY(song->saveProjectFile(inner));
		song->clearProject();
		const QString main = out(QStringLiteral("main2.mmp"));
		QVERIFY(song->guiSaveProjectAs(main));

		QCOMPARE(run(QStringLiteral("project.subproject_add"), {{QStringLiteral("path"), QStringLiteral("/no/such.mmp")}}).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("project.subproject_add"), {{QStringLiteral("path"), out(QStringLiteral("notes.txt"))}}).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("project.subproject_add"), {{QStringLiteral("path"), main}}).errorKind, ControlErrorKind::Refused);

		// A project that links THIS one in its own subprojects would contain it: refused, however deep.
		// Built with the product's own verbs: a project "looping" that has subproject_add'ed main, saved; then main
		// is opened again and asked to take looping as ITS subproject.
		const QString looping = out(QStringLiteral("looping.mmp"));
		song->clearProject();
		QVERIFY(song->guiSaveProjectAs(looping));
		const ControlResult linkedMain = run(QStringLiteral("project.subproject_add"), {{QStringLiteral("path"), main}});
		QVERIFY2(linkedMain.ok, qPrintable(linkedMain.errorMessage));
		QVERIFY(song->guiSaveProject());
		QVERIFY(run(QStringLiteral("project.open"), {{QStringLiteral("path"), main}}).ok);
		const ControlResult refused = run(QStringLiteral("project.subproject_add"), {{QStringLiteral("path"), looping}});
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QVERIFY(refused.errorMessage.contains(QStringLiteral("contains this project")));

		// A track that is no subproject, and one that does not exist.
		Track* plain = trackWithClip(QStringLiteral("Plain"));
		QCOMPARE(run(QStringLiteral("project.subproject_refresh"), {{QStringLiteral("track"), control::trackIdOf(plain)}}).errorKind, ControlErrorKind::NotFound);
		QCOMPARE(run(QStringLiteral("project.subproject_refresh"), {{QStringLiteral("track"), QStringLiteral("trk-999999")}}).errorKind, ControlErrorKind::NotFound);

		// A render that fails is refused and adds neither track nor link.
		qputenv("ZENE_FAKE_RENDER_FAIL", "1");
		const int tracks = trackCount();
		QCOMPARE(run(QStringLiteral("project.subproject_add"), {{QStringLiteral("path"), inner}}).errorKind, ControlErrorKind::Refused);
		QCOMPARE(trackCount(), tracks);
		QCOMPARE(run(QStringLiteral("project.subprojects")).result.value(QStringLiteral("count")).toInt(), 0);
	}
};

int main(int argc, char** argv)
{
	if (argc > 1 && std::strcmp(argv[1], "render") == 0) { return fakeRender(argc, argv); }
	QCoreApplication app(argc, argv);
	QTEST_SET_MAIN_SOURCE_PATH
	ControlBounceRenderTest test;
	return QTest::qExec(&test, argc, argv);
}
#include "ControlBounceRenderTest.moc"
