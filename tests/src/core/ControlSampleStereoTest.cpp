/*
 * ControlSampleStereoTest.cpp - Q050-106's registered proof
 *
 * Copyright (2026) Zene Studio contributors
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

/*! Q050-106 proves the stereo pair through the registry (the socket's own door):
 *  - sample.split_stereo writes an ASYMMETRIC fixture's left and right channels
 *    to two mono WAVs; both files are decoded and compared sample for sample,
 *    rate and length against the source, and the clip is unchanged;
 *  - sample.edit_range op "mono" bakes the documented (L + R) / 2 mean on both
 *    sides, keeps rate/frame count/clip length, reads back mono through
 *    arrangement.get_state, and ONE control.undo restores the exact stereo
 *    samples and the stereo layout;
 *  - the mono layout round-trips through a real project save and load.
 *  The test refers to no sample-layout API, so reverting the product hunks
 *  leaves it compiling and failing at the command/state level. */

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <vector>

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <sndfile.h>

#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "ReversibilityTestSupport.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "Song.h"

using namespace lmms;
using namespace revtest;

namespace
{

struct Fixture
{
	QString clipId;
	std::vector<SampleFrame> frames;
	tick_t length = 0;
	int rate = 0;
};

//! An ASYMMETRIC stereo clip: the product's own white-noise generator writes
//! different left and right values, so the fixture is never dual-mono.
Fixture makeStereoClip()
{
	Fixture fixture;
	const QString track = addTrack(QStringLiteral("sample"));
	if (track.isEmpty()) { return fixture; }
	const ControlResult generated = run(QStringLiteral("sample.generate"),
		{{QStringLiteral("track"), track}, {QStringLiteral("position"), 0},
			{QStringLiteral("length"), 192}, {QStringLiteral("kind"), QStringLiteral("white")}});
	if (!generated.ok) { return fixture; }
	fixture.clipId = generated.result.value(QStringLiteral("clip")).toString();
	control::ClipRef ref;
	ControlResult error;
	if (!control::resolveClip(fixture.clipId, &ref, &error)) { return fixture; }
	auto* clip = dynamic_cast<SampleClip*>(ref.clip);
	if (clip == nullptr) { return fixture; }
	fixture.frames.assign(clip->sample().data(),
		clip->sample().data() + clip->sample().sampleSize());
	fixture.length = ref.clip->length().getTicks();
	fixture.rate = static_cast<int>(clip->sample().sampleRate());
	return fixture;
}

SampleClip* clipById(const QString& id)
{
	control::ClipRef ref;
	ControlResult error;
	if (!control::resolveClip(id, &ref, &error)) { return nullptr; }
	return dynamic_cast<SampleClip*>(ref.clip);
}

QJsonObject clipState(const QString& id)
{
	for (const QJsonValue& value : run(QStringLiteral("arrangement.get_state"))
		.result.value(QStringLiteral("clips")).toArray())
	{
		const QJsonObject clip = value.toObject();
		if (clip.value(QStringLiteral("id")).toString() == id) { return clip; }
	}
	return QJsonObject();
}

bool sameFrames(const std::vector<SampleFrame>& a, const std::vector<SampleFrame>& b)
{
	if (a.size() != b.size()) { return false; }
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		if (a[i].left() != b[i].left()) { return false; }
		if (a[i].right() != b[i].right()) { return false; }
	}
	return true;
}

struct DecodedMono
{
	int channels = 0;
	int rate = 0;
	sf_count_t frames = 0;
	std::vector<float> samples;
};

DecodedMono decodeMono(const QString& path)
{
	DecodedMono out;
	SF_INFO info{};
	SNDFILE* file = sf_open(QFile::encodeName(path).constData(), SFM_READ, &info);
	if (file == nullptr) { return out; }
	out.channels = info.channels;
	out.rate = info.samplerate;
	out.frames = info.frames;
	out.samples.resize(static_cast<std::size_t>(info.frames));
	sf_readf_float(file, out.samples.data(), info.frames);
	sf_close(file);
	return out;
}

} // namespace

class ControlSampleStereoTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! AC1: two destination files, each one channel of the source, at its rate
	//! and frame count; the source clip is untouched. File export is
	//! not_mutating in the A16 table.
	void splitWritesEachChannelAsItsOwnMonoFile()
	{
		const Fixture fixture = makeStereoClip();
		QVERIFY2(!fixture.clipId.isEmpty(), "the fixture clip was not created");
		QVERIFY(!fixture.frames.empty());
		const int frames = static_cast<int>(fixture.frames.size());
		const QJsonObject before = clipState(fixture.clipId);
		QCOMPARE(before.value(QStringLiteral("channels")).toInt(), 2);
		QCOMPARE(before.value(QStringLiteral("layout")).toString(), QStringLiteral("stereo"));

		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString leftPath = directory.filePath(QStringLiteral("left.wav"));
		const QString rightPath = directory.filePath(QStringLiteral("right.wav"));
		const ControlResult split = run(QStringLiteral("sample.split_stereo"),
			{{QStringLiteral("clip"), fixture.clipId}, {QStringLiteral("left_path"), leftPath},
				{QStringLiteral("right_path"), rightPath}});
		QVERIFY2(split.ok, qPrintable(split.errorMessage));
		QCOMPARE(split.result.value(QStringLiteral("frames")).toInt(), frames);
		QCOMPARE(split.result.value(QStringLiteral("sample_rate")).toInt(), fixture.rate);
		QCOMPARE(split.result.value(QStringLiteral("channels")).toInt(), 1);
		QCOMPARE(split.result.value(QStringLiteral("layout")).toString(), QStringLiteral("mono"));

		const DecodedMono left = decodeMono(leftPath);
		const DecodedMono right = decodeMono(rightPath);
		QCOMPARE(left.channels, 1);
		QCOMPARE(right.channels, 1);
		QCOMPARE(left.rate, fixture.rate);
		QCOMPARE(right.rate, fixture.rate);
		QCOMPARE(left.frames, static_cast<sf_count_t>(frames));
		QCOMPARE(right.frames, static_cast<sf_count_t>(frames));
		QVERIFY(left.samples.size() == static_cast<std::size_t>(frames));
		for (int i = 0; i < frames; ++i)
		{
			QCOMPARE(left.samples[static_cast<std::size_t>(i)], fixture.frames[static_cast<std::size_t>(i)].left());
			QCOMPARE(right.samples[static_cast<std::size_t>(i)], fixture.frames[static_cast<std::size_t>(i)].right());
		}

		// The export changed neither the clip's frames nor its layout/timing.
		SampleClip* clip = clipById(fixture.clipId);
		QVERIFY(clip != nullptr);
		std::vector<SampleFrame> after(clip->sample().data(),
			clip->sample().data() + clip->sample().sampleSize());
		QVERIFY2(sameFrames(after, fixture.frames), "the split changed the source samples");
		const QJsonObject afterState = clipState(fixture.clipId);
		QCOMPARE(afterState.value(QStringLiteral("channels")).toInt(), 2);
		QCOMPARE(afterState.value(QStringLiteral("layout")).toString(), QStringLiteral("stereo"));
		QCOMPARE(afterState.value(QStringLiteral("length")).toInt(), static_cast<int>(fixture.length));

		const control::ReversibilityEntry* row =
			control::ReversibilityTable::instance().lookup(QStringLiteral("sample.split_stereo"));
		QVERIFY(row != nullptr);
		QCOMPARE(control::reversibilityClassName(row->cls), QStringLiteral("not_mutating"));
		QVERIFY(!run(QStringLiteral("sample.split_stereo")).ok);  // the schema still requires the three arguments
	}

	//! AC2: the documented mean on both sides, unchanged rate/count/timing, the
	//! state reads mono, and ONE undo restores the exact stereo source.
	void monoDownmixUsesTheMeanAndOneUndoRestoresStereo()
	{
		const Fixture fixture = makeStereoClip();
		QVERIFY(!fixture.clipId.isEmpty());
		const int frames = static_cast<int>(fixture.frames.size());

		const ControlResult mono = run(QStringLiteral("sample.edit_range"),
			{{QStringLiteral("clip"), fixture.clipId}, {QStringLiteral("from"), 0},
				{QStringLiteral("to"), frames}, {QStringLiteral("op"), QStringLiteral("mono")}});
		QVERIFY2(mono.ok, qPrintable(mono.errorMessage));
		QCOMPARE(mono.result.value(QStringLiteral("frames")).toInt(), frames);
		QCOMPARE(mono.result.value(QStringLiteral("sample_rate")).toInt(), fixture.rate);
		QCOMPARE(mono.result.value(QStringLiteral("channels")).toInt(), 1);
		QCOMPARE(mono.result.value(QStringLiteral("layout")).toString(), QStringLiteral("mono"));

		SampleClip* clip = clipById(fixture.clipId);
		QVERIFY(clip != nullptr);
		QCOMPARE(static_cast<int>(clip->sample().sampleSize()), frames);
		QCOMPARE(static_cast<int>(clip->sample().sampleRate()), fixture.rate);
		for (int i = 0; i < frames; ++i)
		{
			const SampleFrame& source = fixture.frames[static_cast<std::size_t>(i)];
			const float mean = (source.left() + source.right()) / 2.0f;
			QCOMPARE(clip->sample().data()[i].left(), mean);
			QCOMPARE(clip->sample().data()[i].right(), mean);
		}
		const QJsonObject monoState = clipState(fixture.clipId);
		QCOMPARE(monoState.value(QStringLiteral("channels")).toInt(), 1);
		QCOMPARE(monoState.value(QStringLiteral("layout")).toString(), QStringLiteral("mono"));
		QCOMPARE(monoState.value(QStringLiteral("length")).toInt(), static_cast<int>(fixture.length));

		REV_UNDO_OR_FAIL();
		clip = clipById(fixture.clipId);
		QVERIFY(clip != nullptr);
		std::vector<SampleFrame> restored(clip->sample().data(),
			clip->sample().data() + clip->sample().sampleSize());
		QVERIFY2(sameFrames(restored, fixture.frames),
			"one undo did not restore the exact stereo samples");
		const QJsonObject stereoState = clipState(fixture.clipId);
		QCOMPARE(stereoState.value(QStringLiteral("channels")).toInt(), 2);
		QCOMPARE(stereoState.value(QStringLiteral("layout")).toString(), QStringLiteral("stereo"));
		QCOMPARE(stereoState.value(QStringLiteral("length")).toInt(), static_cast<int>(fixture.length));

		// A partial range would make the layout a lie: refused before any checkpoint.
		const ControlResult partial = run(QStringLiteral("sample.edit_range"),
			{{QStringLiteral("clip"), fixture.clipId}, {QStringLiteral("from"), 4},
				{QStringLiteral("to"), frames - 4}, {QStringLiteral("op"), QStringLiteral("mono")}});
		QCOMPARE(partial.ok, false);
		QCOMPARE(partial.errorKind, ControlErrorKind::Refused);
		SampleClip* refused = clipById(fixture.clipId);
		QVERIFY(refused != nullptr);
		std::vector<SampleFrame> unchanged(refused->sample().data(),
			refused->sample().data() + refused->sample().sampleSize());
		QVERIFY2(sameFrames(unchanged, fixture.frames), "a refused mono conversion wrote frames");
	}

	//! Existing sample bakers must retain the layout introduced by the mono operation.
	void makeUniqueKeepsMonoLayout()
	{
		const Fixture fixture = makeStereoClip();
		QVERIFY(!fixture.clipId.isEmpty());
		QVERIFY(run(QStringLiteral("sample.edit_range"),
			{{QStringLiteral("clip"), fixture.clipId}, {QStringLiteral("op"), QStringLiteral("mono")}}).ok);
		QVERIFY(run(QStringLiteral("sample.make_unique"),
			{{QStringLiteral("clip"), fixture.clipId}}).ok);
		const QJsonObject state = clipState(fixture.clipId);
		QCOMPARE(state.value(QStringLiteral("channels")).toInt(), 1);
		QCOMPARE(state.value(QStringLiteral("layout")).toString(), QStringLiteral("mono"));
	}

	void cropKeepsMonoLayout()
	{
		const Fixture fixture = makeStereoClip();
		QVERIFY(!fixture.clipId.isEmpty());
		QVERIFY(run(QStringLiteral("sample.edit_range"),
			{{QStringLiteral("clip"), fixture.clipId}, {QStringLiteral("op"), QStringLiteral("mono")}}).ok);
		SampleClip* clip = clipById(fixture.clipId);
		QVERIFY(clip != nullptr);
		const f_cnt_t half = static_cast<f_cnt_t>(fixture.frames.size() / 2);
		clip->setSampleWindow(SampleWindow(0, half));
		QVERIFY(run(QStringLiteral("sample.crop"),
			{{QStringLiteral("clip"), fixture.clipId}}).ok);
		const QJsonObject state = clipState(fixture.clipId);
		QCOMPARE(state.value(QStringLiteral("channels")).toInt(), 1);
		QCOMPARE(state.value(QStringLiteral("layout")).toString(), QStringLiteral("mono"));
		QCOMPARE(clip->sample().sampleSize(), static_cast<std::size_t>(half));
	}

	//! Case-only destination differences alias on the default Windows/macOS filesystems.
	void splitRejectsCaseAliasedDestinationsOnCaseInsensitivePlatforms()
	{
		const Fixture fixture = makeStereoClip();
		QVERIFY(!fixture.clipId.isEmpty());
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString left = directory.filePath(QStringLiteral("Alias.wav"));
		const QString right = directory.filePath(QStringLiteral("alias.wav"));
		const ControlResult split = run(QStringLiteral("sample.split_stereo"),
			{{QStringLiteral("clip"), fixture.clipId}, {QStringLiteral("left_path"), left},
				{QStringLiteral("right_path"), right}});
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
		QVERIFY(!split.ok);
		QVERIFY(!QFile::exists(left));
		QVERIFY(!QFile::exists(right));
#else
		QVERIFY2(split.ok, qPrintable(split.errorMessage));
		QVERIFY(QFile::exists(left));
		QVERIFY(QFile::exists(right));
#endif
	}

#ifndef Q_OS_WIN
	//! Two spellings of one output - a directory and a symlink to it - are the same destination.
	void splitRejectsDestinationsAliasedThroughASymlinkedDirectory()
	{
		const Fixture fixture = makeStereoClip();
		QVERIFY(!fixture.clipId.isEmpty());
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString real = directory.filePath(QStringLiteral("real"));
		const QString link = directory.filePath(QStringLiteral("link"));
		QVERIFY(QDir().mkpath(real));
		QVERIFY(QFile::link(real, link));
		const QString viaReal = real + QStringLiteral("/out.wav");
		const QString viaLink = link + QStringLiteral("/out.wav");
		const ControlResult split = run(QStringLiteral("sample.split_stereo"),
			{{QStringLiteral("clip"), fixture.clipId}, {QStringLiteral("left_path"), viaReal},
				{QStringLiteral("right_path"), viaLink}});
		QVERIFY2(!split.ok, "a symlinked directory aliased the two exports");
		QVERIFY(!QFile::exists(viaReal));

		// A symlinked parent with a not-yet-created subdirectory still resolves through the existing part.
		const ControlResult nested = run(QStringLiteral("sample.split_stereo"),
			{{QStringLiteral("clip"), fixture.clipId}, {QStringLiteral("left_path"), real + QStringLiteral("/a/../out.wav")},
				{QStringLiteral("right_path"), link + QStringLiteral("/out.wav")}});
		QVERIFY(!nested.ok);

		// Distinct files through the symlink are still fine.
		const ControlResult distinct = run(QStringLiteral("sample.split_stereo"),
			{{QStringLiteral("clip"), fixture.clipId}, {QStringLiteral("left_path"), viaReal},
				{QStringLiteral("right_path"), link + QStringLiteral("/other.wav")}});
		QVERIFY2(distinct.ok, qPrintable(distinct.errorMessage));
		QVERIFY(QFile::exists(viaReal));
		QVERIFY(QFile::exists(real + QStringLiteral("/other.wav")));
	}
#endif

	//! AC2's read-back must survive a save/open, not only the live buffer.
	void monoLayoutRoundTripsThroughAProject()
	{
		const Fixture fixture = makeStereoClip();
		QVERIFY(!fixture.clipId.isEmpty());
		const int frames = static_cast<int>(fixture.frames.size());
		// The agent form: op mono alone, without knowing the frame count.
		QVERIFY(run(QStringLiteral("sample.edit_range"),
			{{QStringLiteral("clip"), fixture.clipId}, {QStringLiteral("op"), QStringLiteral("mono")}}).ok);

		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString path = directory.filePath(QStringLiteral("mono.mmp"));
		QVERIFY2(Engine::getSong()->saveProjectFile(path), "the project could not be written");
		Engine::getSong()->loadProject(path);

		SampleClip* clip = clipById(fixture.clipId);
		QVERIFY2(clip != nullptr, "the mono clip did not survive the project round trip");
		QCOMPARE(static_cast<int>(clip->sample().sampleSize()), frames);
		QCOMPARE(static_cast<int>(clip->sample().sampleRate()), fixture.rate);
		const QJsonObject state = clipState(fixture.clipId);
		QCOMPARE(state.value(QStringLiteral("channels")).toInt(), 1);
		QCOMPARE(state.value(QStringLiteral("layout")).toString(), QStringLiteral("mono"));
		for (int i = 0; i < frames; ++i)
		{
			const SampleFrame& source = fixture.frames[static_cast<std::size_t>(i)];
			const float mean = (source.left() + source.right()) / 2.0f;
			QCOMPARE(clip->sample().data()[i].left(), mean);
			QCOMPARE(clip->sample().data()[i].right(), mean);
		}
	}
};

QTEST_MAIN(ControlSampleStereoTest)
#include "ControlSampleStereoTest.moc"
