/*
 * RangeBounceRenderSideTest.cpp - cluster 3971: what a range bounce renders and lands
 *
 * A range bounce that mutes its sources replaces only the clip pieces inside [start,end). The render must hold
 * exactly those pieces plus the sound that rings on past `end` (the live mix loses both when the pieces are muted),
 * and nothing of the pieces that stay audible. The render is a child process, so this test is its own stand-in
 * for `zene render` (fakeRender): it reads the serialised session it is given and draws a block of 0.2 for every
 * unmuted clip of every unmuted audio track, plus a 0.1 "release tail" of ZENE_FAKE_TAIL_TICKS ticks after each
 * clip's end - the part of the engine that decides which pieces sound is the session the product prepares.
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

#include <QDomDocument>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <sndfile.h>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Clip.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

constexpr int Rate = 44100;
constexpr float Body = 0.2f;
constexpr float Tail = 0.1f;

//! The stand-in for `zene render <session> -o <out> -f wav -s <rate>`: eight bars of the mix described above.
int fakeRender(int argc, char** argv)
{
	QString project;
	QString out;
	int rate = Rate;
	if (argc > 2) { project = QString::fromLocal8Bit(argv[2]); }
	for (int i = 3; i + 1 < argc; ++i)
	{
		if (std::strcmp(argv[i], "-o") == 0) { out = QString::fromLocal8Bit(argv[++i]); }
		else if (std::strcmp(argv[i], "-s") == 0) { rate = QString::fromLocal8Bit(argv[++i]).toInt(); }
	}
	QFile file(project);
	QDomDocument document;
	if (!file.open(QIODevice::ReadOnly) || !document.setContent(&file)) { return 4; }
	const double bpm = document.elementsByTagName(QStringLiteral("head")).at(0).toElement().attribute(QStringLiteral("bpm")).toDouble();
	const double framesPerTick = rate * 60.0 / (std::max(1.0, bpm) * 48.0);
	std::vector<float> mix(static_cast<size_t>(std::llround(framesPerTick * 192 * 8)), 0.0f);
	const int tail = qEnvironmentVariableIntValue("ZENE_FAKE_TAIL_TICKS");
	const auto draw = [&](int from, int to, float level) {
		const qint64 first = std::llround(framesPerTick * from);
		const qint64 last = std::min<qint64>(std::llround(framesPerTick * to), static_cast<qint64>(mix.size()));
		for (qint64 frame = first; frame < last; ++frame) { mix[static_cast<size_t>(frame)] += level; }
	};
	const QDomNodeList tracks = document.elementsByTagName(QStringLiteral("track"));
	for (int i = 0; i < tracks.count(); ++i)
	{
		const QDomElement track = tracks.at(i).toElement();
		if (track.attribute(QStringLiteral("type")).toInt() >= 5 || track.attribute(QStringLiteral("muted")).toInt() != 0) { continue; }
		for (QDomElement clip = track.firstChildElement(); !clip.isNull(); clip = clip.nextSiblingElement())
		{
			if (!clip.hasAttribute(QStringLiteral("len")) || clip.attribute(QStringLiteral("muted")).toInt() != 0) { continue; }
			const int at = clip.attribute(QStringLiteral("pos")).toInt();
			const int end = at + clip.attribute(QStringLiteral("len")).toInt();
			draw(at, end, Body);
			draw(end, end + tail, Tail);
		}
	}
	SF_INFO info{};
	info.channels = 1;
	info.samplerate = rate;
	info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
	SNDFILE* wav = sf_open(out.toLocal8Bit().constData(), SFM_WRITE, &info);
	if (wav == nullptr) { return 5; }
	sf_writef_float(wav, mix.data(), static_cast<sf_count_t>(mix.size()));
	sf_close(wav);
	return 0;
}

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

qint64 framesOf(int ticks) { return std::llround(Engine::framesPerTick(static_cast<sample_rate_t>(Rate)) * ticks); }

//! A track holding one default-length (one bar) clip at each of \a positions.
Track* trackWithClips(const QString& name, std::initializer_list<int> positions)
{
	Track* track = Track::create(Track::Type::Instrument, Engine::getSong());
	track->setName(name);
	for (const int at : positions) { track->createClip(TimePos(at)); }
	return track;
}

//! Every clip as "start[a|M],..." in tick order: shows the cuts and which pieces are muted.
QString clipState(Track* track)
{
	std::vector<std::pair<tick_t, bool>> clips;
	for (const Clip* clip : track->getClips()) { clips.emplace_back(clip->startPosition().getTicks(), clip->isMuted()); }
	std::sort(clips.begin(), clips.end());
	QString state;
	for (const auto& clip : clips) { state += QStringLiteral("%1%2,").arg(clip.first).arg(clip.second ? 'M' : 'a'); }
	return state;
}

//! The peak magnitude of \a path between two ticks of the bounce, which starts at timeline tick \a origin.
float peakBetween(const QString& path, int origin, int fromTick, int toTick)
{
	SF_INFO info{};
	SNDFILE* wav = sf_open(path.toLocal8Bit().constData(), SFM_READ, &info);
	if (wav == nullptr) { return -1.0f; }
	std::vector<float> data(static_cast<size_t>(info.frames) * static_cast<size_t>(std::max(1, info.channels)));
	const sf_count_t got = sf_readf_float(wav, data.data(), info.frames);
	sf_close(wav);
	float peak = 0.0f;
	for (qint64 frame = framesOf(fromTick - origin); frame < std::min<qint64>(framesOf(toTick - origin), got); ++frame)
	{
		peak = std::max(peak, std::fabs(data[static_cast<size_t>(frame) * static_cast<size_t>(info.channels)]));
	}
	return peak;
}

qint64 framesIn(const QString& path)
{
	SF_INFO info{};
	SNDFILE* wav = sf_open(path.toLocal8Bit().constData(), SFM_READ, &info);
	if (wav == nullptr) { return -1; }
	sf_close(wav);
	return info.frames;
}

bool writeConstantTake(const QString& path)
{
	SF_INFO info{};
	info.channels = 1;
	info.samplerate = Rate;
	info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
	SNDFILE* wav = sf_open(path.toLocal8Bit().constData(), SFM_WRITE, &info);
	if (wav == nullptr) { return false; }
	const std::vector<float> samples(static_cast<size_t>(Rate * 4), Body);
	const sf_count_t written = sf_writef_float(wav, samples.data(), static_cast<sf_count_t>(samples.size()));
	return sf_close(wav) == 0 && written == static_cast<sf_count_t>(samples.size());
}

QJsonObject waitForJob(int id)
{
	QElapsedTimer timer;
	timer.start();
	QJsonObject last;
	while (timer.elapsed() < 15000)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		const ControlResult report = run(QStringLiteral("render.queue_report"), {{QStringLiteral("job"), id}});
		if (!report.ok) { return {}; }
		last = report.result;
		const QString state = last.value(QStringLiteral("state")).toString();
		if (state == QLatin1String("done") || state == QLatin1String("failed")) { return last; }
		QTest::qWait(5);
	}
	return last;
}

} // namespace

class RangeBounceRenderSideTest : public QObject
{
	Q_OBJECT

	QTemporaryDir m_dir;

	QString out(const QString& name) const { return m_dir.filePath(name); }
	int trackCount() const { return static_cast<int>(Engine::getSong()->tracks().size()); }

	void selectRange(Track* track, int start, int end)
	{
		QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), start}, {QStringLiteral("end"), end},
			{QStringLiteral("tracks"), QJsonArray{control::trackIdOf(track)}}}).ok);
	}

	//! Runs a queued bounce of \a operation to completion; the bounced file is at \a path.
	void queued(const QString& operation, const QJsonObject& extra, const QString& path)
	{
		QJsonObject args = extra;
		args.insert(QStringLiteral("operation"), operation);
		args.insert(QStringLiteral("out"), path);
		const ControlResult accepted = run(QStringLiteral("render.queue_bounce"), args);
		QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
		QCOMPARE(waitForJob(accepted.result.value(QStringLiteral("job")).toInt()).value(QStringLiteral("state")).toString(),
			QStringLiteral("done"));
	}

	//! A bounce of range 48..144 over a clip 0..192 that rings 96 ticks past its end: the file must hold the
	//! 96 in-range ticks and the 96 ticks of tail, the in-range piece alone sounding, the tail at its own level.
	void checkTailLanded(const QString& path)
	{
		QVERIFY2(std::abs(framesIn(path) - framesOf(192)) <= 3, qPrintable(QString::number(framesIn(path))));
		QVERIFY(std::abs(peakBetween(path, 48, 52, 140) - Body) < 0.005f);
		QVERIFY(std::abs(peakBetween(path, 48, 148, 236) - Tail) < 0.005f);  // not the muted [144,192) piece
	}

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		ControlRegistry::setReady(true);
	}

	void init() { qputenv("ZENE_FAKE_TAIL_TICKS", "96"); }
	void cleanup() { qunsetenv("ZENE_FAKE_TAIL_TICKS"); }

	void cleanupTestCase() { Engine::destroy(); }

	//! A note sustaining past `end`: the in-range piece's ring-out lands in the bounce, and one undo takes it all back.
	void rangeBounceLandsTheTailOfASoundSustainingPastEnd()
	{
		Track* source = trackWithClips(QStringLiteral("Sustain"), {0});
		selectRange(source, 48, 144);
		const int before = trackCount();
		const ControlResult bounced = run(QStringLiteral("range.bounce"), {{QStringLiteral("out"), out(QStringLiteral("a.wav"))}});
		QVERIFY2(bounced.ok, qPrintable(bounced.errorMessage));
		checkTailLanded(out(QStringLiteral("a.wav")));
		QCOMPARE(bounced.result.value(QStringLiteral("frames")).toInt(), static_cast<int>(framesIn(out(QStringLiteral("a.wav")))));
		QCOMPARE(clipState(source), QStringLiteral("0a,48M,144a,"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(trackCount(), before);
		QCOMPARE(clipState(source), QStringLiteral("0a,"));
	}

	//! A sound crossing `start`: the piece before the range stays audible in the song, so its own ring-out must not
	//! be bounced a second time into the range.
	void rangeBounceDoesNotBounceTheTailOfAPieceBeforeItsStart()
	{
		Track* source = trackWithClips(QStringLiteral("Crossing"), {0, 192});
		selectRange(source, 96, 192);
		const ControlResult bounced = run(QStringLiteral("range.bounce"), {{QStringLiteral("out"), out(QStringLiteral("b.wav"))}});
		QVERIFY2(bounced.ok, qPrintable(bounced.errorMessage));
		const QString path = out(QStringLiteral("b.wav"));
		QVERIFY2(std::abs(framesIn(path) - framesOf(96)) <= 3, qPrintable(QString::number(framesIn(path))));  // the clip at 192 starts at `end`: no tail room
		QVERIFY2(std::abs(peakBetween(path, 96, 98, 190) - Body) < 0.005f, qPrintable(QString::number(peakBetween(path, 96, 98, 190))));
		QCOMPARE(clipState(source), QStringLiteral("0a,96M,192a,"));
	}

	//! A whole-track mute (every clip inside the range) whose clip ends at `end` with a release tail: the tail
	//! lands in the bounce, since the muted track no longer plays it.
	void rangeBounceOfAWholeMutedSourceLandsItsReleaseTail()
	{
		Track* source = trackWithClips(QStringLiteral("Release"), {0});
		selectRange(source, 0, 192);
		const int before = trackCount();
		const ControlResult bounced = run(QStringLiteral("range.bounce"), {{QStringLiteral("out"), out(QStringLiteral("c.wav"))}});
		QVERIFY2(bounced.ok, qPrintable(bounced.errorMessage));
		const QString path = out(QStringLiteral("c.wav"));
		QVERIFY(source->isMuted());
		QVERIFY2(std::abs(framesIn(path) - framesOf(288)) <= 3, qPrintable(QString::number(framesIn(path))));
		QVERIFY(std::abs(peakBetween(path, 0, 200, 284) - Tail) < 0.005f);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!source->isMuted());
		QCOMPARE(trackCount(), before);
	}

	//! A frozen source needs no extra tail itself, but must not suppress another source's tail in a group bounce.
	void rangeBounceKeepsOtherSourceTailBesideFrozenTrack()
	{
		Track* source = trackWithClips(QStringLiteral("Unfrozen"), {0});
		Track* frozen = Track::create(Track::Type::Instrument, Engine::getSong());
		frozen->setName(QStringLiteral("Frozen"));
		const QString take = out(QStringLiteral("frozen.wav"));
		QVERIFY(writeConstantTake(take));
		QString freezeError;
		QVERIFY2(frozen->freezeTo(take, 0, 192, {}, &freezeError), qPrintable(freezeError));
		const auto selected = QJsonArray{control::trackIdOf(source), control::trackIdOf(frozen)};
		QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 192},
			{QStringLiteral("tracks"), selected}}).ok);

		const QString path = out(QStringLiteral("mixed-frozen.wav"));
		const ControlResult bounced = run(QStringLiteral("range.bounce"), {{QStringLiteral("out"), path}});
		QVERIFY2(bounced.ok, qPrintable(bounced.errorMessage));
		QVERIFY2(std::abs(framesIn(path) - framesOf(288)) <= 3, qPrintable(QString::number(framesIn(path))));
		QVERIFY(std::abs(peakBetween(path, 0, 200, 284) - Tail) < 0.005f);
		QVERIFY(source->isMuted());
		QVERIFY(frozen->isFrozen() && frozen->frozenTake().path != take);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!source->isMuted());
		QCOMPARE(frozen->frozenTake().path, take);
	}

	//! When every selected source plays a frozen take there is no unfrozen audio source to ring on: no tail.
	void rangeBounceOfOnlyFrozenSourcesKeepsThePlainWindow()
	{
		Track* frozen = trackWithClips(QStringLiteral("Only frozen"), {0});
		const QString take = out(QStringLiteral("only-frozen.wav"));
		QVERIFY(writeConstantTake(take));
		QString freezeError;
		QVERIFY2(frozen->freezeTo(take, 0, 192, {}, &freezeError), qPrintable(freezeError));
		selectRange(frozen, 0, 192);
		const QString path = out(QStringLiteral("only-frozen-bounce.wav"));
		QVERIFY(run(QStringLiteral("range.bounce"), {{QStringLiteral("out"), path}}).ok);
		QVERIFY2(std::abs(framesIn(path) - framesOf(192)) <= 3, qPrintable(QString::number(framesIn(path))));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(frozen->frozenTake().path, take);
	}

	//! The tail stops where the next unmuted clip of a source starts, and never runs past the one-bar cap.
	void rangeBounceTailStopsAtTheNextClipAndAtTheCap()
	{
		Track* next = trackWithClips(QStringLiteral("Next"), {0, 240});
		selectRange(next, 0, 192);
		QVERIFY(run(QStringLiteral("range.bounce"), {{QStringLiteral("out"), out(QStringLiteral("d.wav"))}}).ok);
		QVERIFY2(std::abs(framesIn(out(QStringLiteral("d.wav"))) - framesOf(240)) <= 3, qPrintable(QString::number(framesIn(out(QStringLiteral("d.wav"))))));

		qputenv("ZENE_FAKE_TAIL_TICKS", "400");
		Track* capped = trackWithClips(QStringLiteral("Capped"), {0});
		selectRange(capped, 0, 192);
		QVERIFY(run(QStringLiteral("range.bounce"), {{QStringLiteral("out"), out(QStringLiteral("e.wav"))}}).ok);
		QVERIFY2(std::abs(framesIn(out(QStringLiteral("e.wav"))) - framesOf(384)) <= 3, qPrintable(QString::number(framesIn(out(QStringLiteral("e.wav"))))));
	}

	//! Without 'mute_sources' nothing is muted afterwards, so the render stays the plain window: no tail.
	void rangeBounceWithoutMutingKeepsThePlainWindow()
	{
		Track* source = trackWithClips(QStringLiteral("Plain"), {0});
		selectRange(source, 48, 144);
		QVERIFY(run(QStringLiteral("range.bounce"), {{QStringLiteral("out"), out(QStringLiteral("f.wav"))},
			{QStringLiteral("mute_sources"), false}}).ok);
		QVERIFY2(std::abs(framesIn(out(QStringLiteral("f.wav"))) - framesOf(96)) <= 3, qPrintable(QString::number(framesIn(out(QStringLiteral("f.wav"))))));
		QCOMPARE(clipState(source), QStringLiteral("0a,"));
	}

	//! The bounce.land sibling with 'start'/'end' renders the same pieces and tail.
	void bounceLandOfARangeLandsTheTail()
	{
		Track* source = trackWithClips(QStringLiteral("Land"), {0});
		QVERIFY(run(QStringLiteral("bounce.land"), {{QStringLiteral("track"), control::trackIdOf(source)},
			{QStringLiteral("out"), out(QStringLiteral("g.wav"))}, {QStringLiteral("start"), 48}, {QStringLiteral("end"), 144}}).ok);
		checkTailLanded(out(QStringLiteral("g.wav")));
		QCOMPARE(clipState(source), QStringLiteral("0a,48M,144a,"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(clipState(source), QStringLiteral("0a,"));
	}

	//! The queued range_bounce renders the same pieces and tail, and its undo is still one step.
	void queuedRangeBounceLandsTheTail()
	{
		Track* source = trackWithClips(QStringLiteral("Queued"), {0});
		selectRange(source, 48, 144);
		const int before = trackCount();
		queued(QStringLiteral("range_bounce"), {}, out(QStringLiteral("h.wav")));
		checkTailLanded(out(QStringLiteral("h.wav")));
		QCOMPARE(trackCount(), before + 1);
		QCOMPARE(clipState(source), QStringLiteral("0a,48M,144a,"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(trackCount(), before);
		QCOMPARE(clipState(source), QStringLiteral("0a,"));
	}

	//! The queued range_bounce of a whole-muted source lands its release tail too.
	void queuedRangeBounceOfAWholeMutedSourceLandsItsReleaseTail()
	{
		Track* source = trackWithClips(QStringLiteral("Queued release"), {0});
		selectRange(source, 0, 192);
		queued(QStringLiteral("range_bounce"), {}, out(QStringLiteral("i.wav")));
		QVERIFY(source->isMuted());
		QVERIFY2(std::abs(framesIn(out(QStringLiteral("i.wav"))) - framesOf(288)) <= 3, qPrintable(QString::number(framesIn(out(QStringLiteral("i.wav"))))));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!source->isMuted());
	}

	//! The queued bounce_land of a range renders the same pieces and tail.
	void queuedBounceLandOfARangeLandsTheTail()
	{
		Track* source = trackWithClips(QStringLiteral("Queued land"), {0});
		queued(QStringLiteral("bounce_land"), {{QStringLiteral("track"), control::trackIdOf(source)},
			{QStringLiteral("start"), 48}, {QStringLiteral("end"), 144}}, out(QStringLiteral("j.wav")));
		checkTailLanded(out(QStringLiteral("j.wav")));
		QCOMPARE(clipState(source), QStringLiteral("0a,48M,144a,"));
	}
};

int main(int argc, char** argv)
{
	if (argc > 1 && std::strcmp(argv[1], "render") == 0) { return fakeRender(argc, argv); }
	QCoreApplication app(argc, argv);
	QTEST_SET_MAIN_SOURCE_PATH
	RangeBounceRenderSideTest test;
	return QTest::qExec(&test, argc, argv);
}
#include "RangeBounceRenderSideTest.moc"
