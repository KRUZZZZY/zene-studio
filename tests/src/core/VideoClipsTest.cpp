/*
 * VideoClipsTest.cpp - Q050-139/Q050-180.1: several positioned video clips on the project's one video track
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
/*! The acceptance path for Q050-180.1 (with Q050-139 folded in), through the registered commands:
 *  - two different files are separate clips with their own ids, offsets and metadata, and all of it survives
 *    save/reopen; an old single-<video> project loads as one clip; project.new empties the track;
 *  - every mutating clip command is exactly one undo step, and video.attach's undo puts EVERY replaced clip back;
 *  - the video window follows the clip at the play position during real playback (the engine renders periods), and
 *    never shows another clip's picture: not when the play position jumps from one clip straight into the other,
 *    and not when a frame asked for one clip arrives after the play position moved to the other;
 *  - video.render_frames renders a range spanning both clips at the project's tempo: each image is the right
 *    clip's right second, and before, after and between the clips it is black.
 *  The two clips are told apart by colour: the first is red then blue, the second green then yellow (VideoTestClip.h).
 *  The lane's own add / move / seek paths are VideoClipsGuiTest. */

#include <QtTest>

#include <algorithm>
#include <cmath>

#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>

#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Song.h"
#include "VideoDecoder.h"
#include "VideoTestClip.h"
#include "VideoWindow.h"

using namespace lmms;

namespace
{

constexpr int TicksPerBeat = 48;  //!< 4/4 at DefaultTicksPerBar 192

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QJsonArray clipList()
{
	return run(QStringLiteral("video.get_state")).result.value(QStringLiteral("clips")).toArray();
}

QJsonObject clipState(const QString& id)
{
	for (const QJsonValue& value : clipList())
	{
		const QJsonObject clip = value.toObject();
		if (clip.value(QStringLiteral("id")).toString() == id) { return clip; }
	}
	return {};
}

QString addClip(const QString& path, int offset)
{
	const ControlResult added = run(QStringLiteral("video.clip_add"), {{QStringLiteral("path"), path}, {QStringLiteral("offset"), offset}});
	return added.ok ? added.result.value(QStringLiteral("clip")).toObject().value(QStringLiteral("id")).toString() : QString();
}

enum class Colour { Black, Red, Blue, Green, Yellow, Other };

const char* nameOf(Colour colour)
{
	static const char* names[] = {"black", "red", "blue", "green", "yellow", "other"};
	return names[static_cast<int>(colour)];
}

//! The pure colour @a pixel is near: one channel set per colour (bright above 150, dark below 100).
Colour colourOf(QRgb pixel)
{
	const int r = qRed(pixel);
	const int g = qGreen(pixel);
	const int b = qBlue(pixel);
	if (std::max({r, g, b}) < 40) { return Colour::Black; }
	struct Pure { Colour colour; int r; int g; int b; };  // 1: bright, 0: dark
	static const Pure pures[] = {{Colour::Red, 1, 0, 0}, {Colour::Blue, 0, 0, 1}, {Colour::Green, 0, 1, 0},
		{Colour::Yellow, 1, 1, 0}};
	const auto level = [](int value) { return value > 150 ? 1 : value < 100 ? 0 : -1; };
	for (const Pure& pure : pures)
	{
		if (level(r) == pure.r && level(g) == pure.g && level(b) == pure.b) { return pure.colour; }
	}
	return Colour::Other;
}

//! What the window's picture shows: sampled in the upper part of the picture, clear of the centred status text.
Colour shown(gui::VideoWindow& window)
{
	const QImage image = window.grab().toImage();
	return colourOf(image.pixel(image.width() / 2, image.height() / 6));
}

//! The song seconds of @a tick at a constant @a bpm: the oracle, independent of the tempo map code under test.
double songSeconds(tick_t tick, int bpm)
{
	return static_cast<double>(tick) * 60.0 / (bpm * TicksPerBeat);
}

void settle(gui::VideoWindow& window)
{
	QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
	window.follow();
}

//! Which clip the play position at song second @a s is clearly inside, with A at 1..3 s and B at 5..7 s (0 for
//! A, 1 for B, -1 outside both), or -2 within 0.02 s of an edge, where either answer is right.
int clipOfSecond(double s)
{
	constexpr double Edge = 0.02;
	const double edges[] = {1.0, 3.0, 5.0, 7.0};
	for (double edge : edges)
	{
		if (std::abs(s - edge) < Edge) { return -2; }
	}
	if (s > 1.0 && s < 3.0) { return 0; }
	return s > 5.0 && s < 7.0 ? 1 : -1;
}

//! Empty when @a colour may show at song second @a s - black anywhere, else only the colours of the clip there.
QString wrongColour(double s, Colour colour)
{
	const int clip = clipOfSecond(s);
	if (clip == -2 || colour == Colour::Black) { return {}; }
	// A clip's two colours (QList, not QSet: an enum class has no qHash before Qt 6).
	const QList<Colour> allowed = clip == 0 ? QList<Colour>{Colour::Red, Colour::Blue}
		: clip == 1 ? QList<Colour>{Colour::Green, Colour::Yellow} : QList<Colour>{};
	if (allowed.contains(colour)) { return {}; }
	return QStringLiteral("%1 s: %2 shown %3").arg(s).arg(nameOf(colour))
		.arg(clip < 0 ? QStringLiteral("outside both clips") : QStringLiteral("in clip %1").arg(clip));
}

//! What the render should show @a intoA s after A's start and @a intoB s after B's: the clip and its colour.
struct Expect
{
	QString id;
	double into = 0.0;
	Colour colour = Colour::Black;
};

Expect expectedAt(double intoA, double intoB, const QString& a, const QString& b)
{
	if (intoA > 0.0 && intoA < 2.0) { return {a, intoA, intoA < 1.0 ? Colour::Red : Colour::Blue}; }
	if (intoB > 0.0 && intoB < 2.0) { return {b, intoB, intoB < 1.0 ? Colour::Green : Colour::Yellow}; }
	return {};  // before, between, after: black, no clip
}

//! Within 0.03 s of a clip's start, its colour change or its end - where a tick's rounding may land either side.
bool nearAnEdge(double into)
{
	return std::abs(into) < 0.03 || std::abs(into - 1.0) < 0.03 || std::abs(into - 2.0) < 0.03;
}

//! The rendered @a frame of the range against the oracle (A starting at @a startA s, B at @a startB s); empty when
//! right or when the sample sits near an edge (@a checked counts the frames compared).
QString wrongFrame(const QJsonObject& frame, double startA, double startB, const QString& a, const QString& b, int* checked)
{
	const double s = frame.value(QStringLiteral("index")).toInt() / 25.0;  // song time of the sample, by construction
	if (nearAnEdge(s - startA) || nearAnEdge(s - startB)) { return {}; }
	++*checked;
	const QImage image(frame.value(QStringLiteral("file")).toString());
	if (image.size() != QSize(64, 36)) { return QStringLiteral("%1 s: no 64x36 image").arg(s); }
	const Colour colour = colourOf(image.pixel(32, 18));
	const QString id = frame.value(QStringLiteral("clip")).toString();
	const Expect expect = expectedAt(s - startA, s - startB, a, b);
	const bool timed = id.isEmpty()
		|| (frame.value(QStringLiteral("decoded")).toBool() && std::abs(frame.value(QStringLiteral("clip_seconds")).toDouble() - expect.into) < 0.02);
	if (id == expect.id && colour == expect.colour && timed) { return {}; }
	return QStringLiteral("%1 s: clip '%2' (want '%3'), %4 (want %5)").arg(s).arg(id, expect.id).arg(nameOf(colour), nameOf(expect.colour));
}

} // namespace

class VideoClipsTest : public QObject
{
	Q_OBJECT

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
		Engine::getSong()->stop();
		QVERIFY(run(QStringLiteral("project.new")).ok);
	}

	//! Criterion 1: two files at their own ticks keep file, metadata, position and identity across save/reopen.
	void twoClipsKeepTheirIdsOffsetsAndMediaAcrossSaveAndReopen()
	{
		if (!needClips()) { QSKIP("no FFmpeg in this build, or the test clips could not be made"); }
		const QString firstId = addClip(m_a, 96);
		const QString secondId = addClip(m_b, 480);
		QVERIFY(!firstId.isEmpty() && !secondId.isEmpty());
		QVERIFY(firstId != secondId);
		QCOMPARE(clipList().size(), 2);

		// Position edits address ONE clip; the other keeps its offset.
		QVERIFY(run(QStringLiteral("video.set_offset"), {{QStringLiteral("clip"), secondId}, {QStringLiteral("offset"), 576}}).ok);
		QVERIFY(run(QStringLiteral("video.set_offset"), {{QStringLiteral("clip"), firstId}, {QStringLiteral("offset"), 48}}).ok);
		QCOMPARE(clipState(firstId).value(QStringLiteral("offset")).toInt(), 48);
		QCOMPARE(clipState(secondId).value(QStringLiteral("offset")).toInt(), 576);
		const QJsonArray before = clipList();

		const QString project = m_dir.filePath(QStringLiteral("clips.mmp"));
		QVERIFY2(run(QStringLiteral("project.save"), {{QStringLiteral("path"), project}}).ok, "save the two clips");
		QVERIFY(run(QStringLiteral("project.new")).ok);
		QCOMPARE(clipList().size(), 0);
		const ControlResult opened = run(QStringLiteral("project.open"), {{QStringLiteral("path"), project}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));

		// Every field of both clips - id, file, offset, end, fps, duration, size, sound - reads back the same.
		QCOMPARE(clipList(), before);
		const QJsonObject first = clipState(firstId);
		const QJsonObject second = clipState(secondId);
		QCOMPARE(first.value(QStringLiteral("path")).toString(), QFileInfo(m_a).absoluteFilePath());
		QCOMPARE(second.value(QStringLiteral("path")).toString(), QFileInfo(m_b).absoluteFilePath());
		QCOMPARE(first.value(QStringLiteral("offset")).toInt(), 48);
		QCOMPARE(second.value(QStringLiteral("offset")).toInt(), 576);
		for (const QJsonObject& clip : {first, second})
		{
			QCOMPARE(clip.value(QStringLiteral("width")).toInt(), videotest::Width);
			QCOMPARE(clip.value(QStringLiteral("height")).toInt(), videotest::Height);
			QVERIFY(qAbs(clip.value(QStringLiteral("fps")).toDouble() - videotest::Fps) < 0.01);
			QVERIFY(qAbs(clip.value(QStringLiteral("duration")).toDouble() - 2.0) < 0.1);
			QVERIFY(clip.value(QStringLiteral("has_audio")).toBool());
			QVERIFY(clip.value(QStringLiteral("exists")).toBool());
		}
		// Still independently addressable after the reopen: moving one leaves the other alone.
		QVERIFY(run(QStringLiteral("video.set_offset"), {{QStringLiteral("clip"), secondId}, {QStringLiteral("offset"), 600}}).ok);
		QCOMPARE(clipState(firstId).value(QStringLiteral("offset")).toInt(), 48);
		QCOMPARE(clipState(secondId).value(QStringLiteral("offset")).toInt(), 600);
	}

	//! Criterion 5: an old project's single <video> loads as one clip.
	void anOldSingleVideoProjectLoadsAsOneClip()
	{
		if (!needClips()) { QSKIP("no FFmpeg in this build, or the test clips could not be made"); }
		QVERIFY(run(QStringLiteral("video.attach"), {{QStringLiteral("path"), m_a}, {QStringLiteral("offset"), 192}}).ok);
		const QString project = m_dir.filePath(QStringLiteral("legacy-video.mmp"));
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), project}}).ok);
		// What an older build wrote: the one <video> element's attributes, no <clip> children.
		QDomDocument document;
		QFile file(project);
		QVERIFY(file.open(QIODevice::ReadOnly));
		QVERIFY(document.setContent(file.readAll()));
		file.close();
		const QDomNodeList videos = document.elementsByTagName(QStringLiteral("video"));
		QCOMPARE(videos.size(), 1);
		QDomElement video = videos.at(0).toElement();
		while (!video.firstChildElement(QStringLiteral("clip")).isNull())
		{
			video.removeChild(video.firstChildElement(QStringLiteral("clip")));
		}
		QVERIFY(!video.hasAttribute(QStringLiteral("id")));
		QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
		QVERIFY(file.write(document.toByteArray()) > 0);
		file.close();

		QVERIFY(run(QStringLiteral("project.new")).ok);
		QVERIFY(run(QStringLiteral("project.open"), {{QStringLiteral("path"), project}}).ok);
		QCOMPARE(clipList().size(), 1);
		const QJsonObject clip = clipList().first().toObject();
		QCOMPARE(clip.value(QStringLiteral("path")).toString(), QFileInfo(m_a).absoluteFilePath());
		QCOMPARE(clip.value(QStringLiteral("offset")).toInt(), 192);
		QVERIFY(!clip.value(QStringLiteral("id")).toString().isEmpty());
		// The legacy commands still address it without a clip id.
		QVERIFY(run(QStringLiteral("video.set_offset"), {{QStringLiteral("offset"), 240}}).ok);
		QCOMPARE(clipList().first().toObject().value(QStringLiteral("offset")).toInt(), 240);
	}

	//! Criterion 5: project.new empties the video track; an empty project saves no <video> at all.
	void anEmptyProjectResetsTheVideoTrack()
	{
		if (!needClips()) { QSKIP("no FFmpeg in this build, or the test clips could not be made"); }
		QVERIFY(!addClip(m_a, 0).isEmpty());
		QVERIFY(!addClip(m_b, 480).isEmpty());
		QCOMPARE(clipList().size(), 2);
		QVERIFY(run(QStringLiteral("project.new")).ok);
		QCOMPARE(clipList().size(), 0);
		QVERIFY(!run(QStringLiteral("video.get_state")).result.value(QStringLiteral("attached")).toBool());
		const QString project = m_dir.filePath(QStringLiteral("empty.mmp"));
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), project}}).ok);
		QFile file(project);
		QVERIFY(file.open(QIODevice::ReadOnly));
		QDomDocument document;
		QVERIFY(document.setContent(file.readAll()));
		QCOMPARE(document.elementsByTagName(QStringLiteral("video")).size(), 0);
	}

	//! Criterion 5: every mutating clip command is exactly one undo step, and its redo is one step too.
	void everyClipEditIsOneUndoStep()
	{
		if (!needClips()) { QSKIP("no FFmpeg in this build, or the test clips could not be made"); }
		const QString first = addClip(m_a, 0);
		QVERIFY(!first.isEmpty());
		struct Edit { QString command; QJsonObject args; };
		const QList<Edit> edits{
			{QStringLiteral("video.clip_add"), {{QStringLiteral("path"), m_b}, {QStringLiteral("offset"), 480}}},
			{QStringLiteral("video.set_offset"), {{QStringLiteral("clip"), first}, {QStringLiteral("offset"), 96}}},
			{QStringLiteral("video.clip_remove"), {{QStringLiteral("clip"), first}}},
			{QStringLiteral("video.attach"), {{QStringLiteral("path"), m_a}, {QStringLiteral("offset"), 24}}},
			{QStringLiteral("video.detach"), {}},
		};
		for (const Edit& edit : edits)
		{
			const QJsonArray before = clipList();
			const ControlResult done = run(edit.command, edit.args);
			QVERIFY2(done.ok, qPrintable(edit.command + QStringLiteral(": ") + done.errorMessage));
			const QJsonArray after = clipList();
			QVERIFY2(after != before, qPrintable(edit.command));
			QVERIFY(run(QStringLiteral("control.undo")).ok);
			QVERIFY2(clipList() == before, qPrintable(edit.command + QStringLiteral(": one undo did not restore the clips")));
			QVERIFY(run(QStringLiteral("control.redo")).ok);
			QVERIFY2(clipList() == after, qPrintable(edit.command + QStringLiteral(": one redo did not repeat it")));
		}
	}

	//! video.attach replaces the whole track; its one undo step puts EVERY replaced clip back, ids included.
	void attachReplacesTheTrackAndItsUndoPutsEveryClipBack()
	{
		if (!needClips()) { QSKIP("no FFmpeg in this build, or the test clips could not be made"); }
		QVERIFY(!addClip(m_a, 0).isEmpty());
		QVERIFY(!addClip(m_b, 480).isEmpty());
		const QJsonArray before = clipList();
		QVERIFY(run(QStringLiteral("video.attach"), {{QStringLiteral("path"), m_b}, {QStringLiteral("offset"), 96}}).ok);
		QCOMPARE(clipList().size(), 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(clipList().size(), 2);
		QCOMPARE(clipList(), before);
	}

	//! Criteria 2/3: during real playback the window shows only the clip at the play position (or nothing).
	void playbackShowsEachClipAndNothingBeforeAfterOrInTheGap()
	{
		if (!needClips()) { QSKIP("no FFmpeg in this build, or the test clips could not be made"); }
		constexpr int Bpm = 120;  // one second is 96 ticks: A spans 96..288, the gap 288..480, B 480..672
		QVERIFY(run(QStringLiteral("transport.set_tempo"), {{QStringLiteral("bpm"), Bpm}}).ok);
		const QString a = addClip(m_a, 96);
		const QString b = addClip(m_b, 480);
		QVERIFY(!a.isEmpty() && !b.isEmpty());
		gui::VideoWindow window(nullptr);
		window.resize(320, 202);

		Song* song = Engine::getSong();
		song->setPlayPos(0, Song::PlayMode::Song);
		song->playSong();
		QCOMPARE(song->playMode(), Song::PlayMode::Song);
		QSet<int> seen;  // the clips a picture was seen of
		QString failure;
		for (int period = 0; period < 100000 && failure.isEmpty(); ++period)
		{
			Engine::audioEngine()->renderNextPeriod();
			const tick_t tick = song->getPlayPos(Song::PlayMode::Song).getTicks();
			if (tick >= 800) { break; }
			settle(window);
			const Colour colour = shown(window);
			const double s = songSeconds(tick, Bpm);
			failure = wrongColour(s, colour);
			if (colour != Colour::Black) { seen.insert(clipOfSecond(s)); }
			QTest::qWait(1);  // the frame pump decodes while the engine renders on
		}
		song->stop();
		QVERIFY2(failure.isEmpty(), qPrintable(failure));
		QVERIFY2(seen.contains(0), "no frame of the first clip was shown while it played");
		QVERIFY2(seen.contains(1), "no frame of the second clip was shown while it played");
	}

	//! Criterion 2: a jump straight from one clip into the other takes the first clip's picture down at once.
	void aJumpIntoTheOtherClipNeverShowsTheFirstClipsFrame()
	{
		if (!needClips()) { QSKIP("no FFmpeg in this build, or the test clips could not be made"); }
		QVERIFY(run(QStringLiteral("transport.set_tempo"), {{QStringLiteral("bpm"), 120}}).ok);
		const QString a = addClip(m_a, 0);
		const QString b = addClip(m_b, 480);
		gui::VideoWindow window(nullptr);
		window.resize(320, 202);
		Song* song = Engine::getSong();
		song->setPlayPos(29, Song::PlayMode::Song);  // 0.3 s into the first clip: red
		window.follow();
		QTRY_COMPARE_WITH_TIMEOUT(shown(window), Colour::Red, 10000);

		song->setPlayPos(480 + 29, Song::PlayMode::Song);  // 0.3 s into the second clip: green
		window.follow();
		QCOMPARE(shown(window), Colour::Black);  // the red picture is gone before the green one is decoded
		QTRY_COMPARE_WITH_TIMEOUT(shown(window), Colour::Green, 10000);
		QVERIFY(window.status().isEmpty());
	}

	//! Criterion 2: a frame asked for one clip that arrives once the play position is in the other is not shown.
	void aLateFrameOfTheOtherClipIsDropped()
	{
		if (!needClips()) { QSKIP("no FFmpeg in this build, or the test clips could not be made"); }
		QVERIFY(run(QStringLiteral("transport.set_tempo"), {{QStringLiteral("bpm"), 120}}).ok);
		QVERIFY(!addClip(m_a, 0).isEmpty());
		const QString b = addClip(m_b, 480);
		gui::VideoWindow window(nullptr);
		window.resize(320, 202);
		Song* song = Engine::getSong();
		song->setPlayPos(29, Song::PlayMode::Song);
		window.follow();  // the first clip's red frame is asked for; its answer cannot arrive before events run
		song->setPlayPos(480 + 120, Song::PlayMode::Song);  // 1.25 s into the second clip: yellow
		bool sawFirstClip = false;
		QElapsedTimer timer;
		timer.start();
		while (timer.elapsed() < 10000 && shown(window) != Colour::Yellow)
		{
			settle(window);
			const Colour colour = shown(window);
			sawFirstClip = sawFirstClip || colour == Colour::Red || colour == Colour::Blue;
		}
		QVERIFY2(!sawFirstClip, "the first clip's late frame was shown while the play position was in the second clip");
		QCOMPARE(shown(window), Colour::Yellow);
	}

	//! Criterion 2/3 (review round 1): video.attach can give the new file the id of the clip it replaced; a late frame
	//! of the replaced file must not be shown for it, and the new file's own frame must then appear.
	void aReplacementWithTheSameClipIdDoesNotShowTheOldFrame()
	{
		if (!needClips()) { QSKIP("no FFmpeg in this build, or the test clips could not be made"); }
		QVERIFY(run(QStringLiteral("transport.set_tempo"), {{QStringLiteral("bpm"), 120}}).ok);
		const QString oldId = addClip(m_a, 0);
		QVERIFY(!oldId.isEmpty());
		gui::VideoWindow window(nullptr);
		window.resize(320, 202);
		Engine::getSong()->setPlayPos(29, Song::PlayMode::Song);
		window.follow();  // the old file's red frame is asked for; its answer cannot arrive before events run
		QVERIFY(run(QStringLiteral("video.attach"), {{QStringLiteral("path"), m_b}, {QStringLiteral("offset"), 0}}).ok);
		QCOMPARE(clipList().first().toObject().value(QStringLiteral("id")).toString(), oldId);  // the same id, a new file

		bool sawOldFrame = false;
		QElapsedTimer timer;
		timer.start();
		while (timer.elapsed() < 10000 && shown(window) != Colour::Green)
		{
			settle(window);
			const Colour colour = shown(window);
			sawOldFrame = sawOldFrame || colour == Colour::Red || colour == Colour::Blue;
		}
		QVERIFY2(!sawOldFrame, "a late frame of the replaced file was shown for the reused clip id");
		QCOMPARE(shown(window), Colour::Green);

		// And with the old file's frame already up: replacing it under the same id takes the picture down at once.
		QVERIFY(run(QStringLiteral("video.attach"), {{QStringLiteral("path"), m_a}, {QStringLiteral("offset"), 0}}).ok);
		QCOMPARE(clipList().first().toObject().value(QStringLiteral("id")).toString(), oldId);
		window.follow();
		QCOMPARE(shown(window), Colour::Black);  // the green picture of the replaced file is gone before red decodes
		QTRY_COMPARE_WITH_TIMEOUT(shown(window), Colour::Red, 10000);
	}

	//! Criterion 3: a range render spanning both clips at the project's tempo: the right frame at every tick.
	void aRangeRenderShowsEachClipAtProjectTempoAndBlackElsewhere()
	{
		if (!needClips()) { QSKIP("no FFmpeg in this build, or the test clips could not be made"); }
		constexpr int Bpm = 90;  // one second is 72 ticks: A spans 96..240, the gap 240..480, B 480..624
		QVERIFY(run(QStringLiteral("transport.set_tempo"), {{QStringLiteral("bpm"), Bpm}}).ok);
		const QString a = addClip(m_a, 96);
		const QString b = addClip(m_b, 480);
		const QString out = m_dir.filePath(QStringLiteral("render"));
		const ControlResult rendered = run(QStringLiteral("video.render_frames"), {{QStringLiteral("from"), 0},
			{QStringLiteral("to"), 720}, {QStringLiteral("out"), out}, {QStringLiteral("fps"), 25},
			{QStringLiteral("width"), 64}, {QStringLiteral("height"), 36}});
		QVERIFY2(rendered.ok, qPrintable(rendered.errorMessage));
		const QJsonArray frames = rendered.result.value(QStringLiteral("frames")).toArray();
		QCOMPARE(frames.size(), 250);  // 720 ticks at 90 bpm are 10 s of song: 250 frames at 25 fps
		QCOMPARE(rendered.result.value(QStringLiteral("count")).toInt(), 250);
		const double startA = songSeconds(96, Bpm);
		const double startB = songSeconds(480, Bpm);
		int checked = 0;
		for (const QJsonValue& value : frames)
		{
			const QJsonObject frame = value.toObject();
			const double s = frame.value(QStringLiteral("index")).toInt() / 25.0;
			const tick_t tick = frame.value(QStringLiteral("tick")).toInt();
			QVERIFY2(std::abs(songSeconds(tick, Bpm) - s) < 1.0 / 72.0, qPrintable(QString::number(s)));  // within a tick
			const QString wrong = wrongFrame(frame, startA, startB, a, b, &checked);
			QVERIFY2(wrong.isEmpty(), qPrintable(wrong));
		}
		QVERIFY(checked > 200);
		QVERIFY(!run(QStringLiteral("video.render_frames"), {{QStringLiteral("from"), 10}, {QStringLiteral("to"), 10},
			{QStringLiteral("out"), out}}).ok);  // an empty range is refused
	}

private:
	bool needClips()
	{
		if (!VideoDecoder::available()) { return false; }
		if (!m_a.isEmpty() && !m_b.isEmpty()) { return true; }
		const QString a = m_dir.filePath(QStringLiteral("first.mkv"));
		const QString b = m_dir.filePath(QStringLiteral("second.mkv"));
		if (!videotest::makeClip(a) || !videotest::makeClip(b, videotest::Palette::GreenYellow)) { return false; }
		m_a = a;
		m_b = b;
		return true;
	}

	QTemporaryDir m_dir;
	QString m_a;
	QString m_b;
};

QTEST_MAIN(VideoClipsTest)
#include "VideoClipsTest.moc"
