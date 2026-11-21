/*
 * ReceiverEffectTest.cpp - the Receiver device mixes a sidechain input into a chain, and InsertRecorder passes its input
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

/*! The Receiver module (plugins/Receiver) is the device chain.receive places: it adds the chain's sidechain input
 *  to the signal at 'level', keeping the chain's own signal or ('replace') standing in for it. Driven here through
 *  the product's own EffectChain with a sidechain buffer handed in, as the mixer hands one in - the arithmetic is
 *  measured on real buffers, the controls round-trip through the project's save/load, and a chain with no sidechain
 *  leaves the signal as it was. InsertRecorder (plugins/InsertRecorder) is loaded the same way and must pass its
 *  input through unchanged while exposing its recorder. */

#include <QtTest>

#include <QDir>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <sndfile.h>

#include "AudioBuffer.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectControls.h"
#include "Engine.h"
#include "TakeRecorder.h"

using namespace lmms;

namespace
{

constexpr f_cnt_t Frames = 64;

//! A stereo block (interleaved, as the device reads it) holding @a left / @a right in every frame.
AudioBuffer stereoBlock(float left, float right)
{
	AudioBuffer buffer{Frames, DEFAULT_CHANNELS};
	buffer.allocateInterleavedBuffer();
	for (f_cnt_t f = 0; f < Frames; ++f)
	{
		buffer.interleavedBuffer()[f][0] = left;
		buffer.interleavedBuffer()[f][1] = right;
	}
	buffer.assumeNonSilent(0);
	buffer.assumeNonSilent(1);
	return buffer;
}

//! A planar sidechain block holding @a left / @a right on its two channels.
AudioBuffer sidechainBlock(float left, float right)
{
	AudioBuffer buffer{Frames, DEFAULT_CHANNELS};
	for (f_cnt_t f = 0; f < Frames; ++f)
	{
		buffer.buffer(0)[f] = left;
		buffer.buffer(1)[f] = right;
	}
	buffer.assumeNonSilent(0);
	buffer.assumeNonSilent(1);
	return buffer;
}

//! The settings element a Receiver's controls read: its level and replace switch.
QDomElement settings(QDomDocument& doc, float level, bool replace)
{
	QDomElement element = doc.createElement(QStringLiteral("ReceiverControls"));
	element.setAttribute(QStringLiteral("level"), static_cast<double>(level));
	element.setAttribute(QStringLiteral("replace"), replace ? 1 : 0);
	return element;
}

//! The bytes of the file at @a path, or nothing when it cannot be read.
QByteArray bytesOf(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

//! Writes @a bytes to the file at @a path, replacing what is there.
void writeBytes(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(file.errorString()));
	QCOMPARE(file.write(bytes), static_cast<qint64>(bytes.size()));
}

//! The frame count of the WAV at @a path, or -1 when it cannot be opened.
sf_count_t wavFrames(const QString& path)
{
	SF_INFO info{};
	SNDFILE* wav = sf_open(path.toLocal8Bit().constData(), SFM_READ, &info);
	if (wav == nullptr) { return -1; }
	sf_close(wav);
	return info.frames;
}

//! True when @a why reports that the file at @a path was refused because it already exists.
bool refusedAsExisting(const QString& why, const QString& path)
{
	return why.contains(path) && why.contains(QStringLiteral("already exists"));
}

//! Records @a blocks blocks of sound passing through @a chain to a new take at @a path, then stops it.
void recordTake(EffectChain& chain, TakeRecorder& take, const QString& path, int blocks)
{
	QString why;
	const bool started = take.start(path, 44100, &why);
	QVERIFY2(started, qPrintable(why));
	QVERIFY(take.recording());
	for (int i = 0; i < blocks; ++i)
	{
		AudioBuffer passing = stereoBlock(0.25f, 0.5f);
		chain.processAudioBuffer(passing, nullptr);
	}
	take.stop();
	QVERIFY(!take.recording());
}

} // namespace

class ReceiverEffectTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		// The modules under test are built with this target (tests/CMakeLists.txt add_dependencies) and found in the
		// build's plugin directory, which CMake hands this target; there is no fallback to a global variable.
		qputenv("LMMS_PLUGIN_DIR", QByteArrayLiteral(LMMS_TEST_PLUGIN_DIR));
		Engine::init(true);
	}

	void cleanupTestCase() { Engine::destroy(); }

	void aChainMixesItsSidechainInAtTheLevel()
	{
		EffectChain chain{nullptr};
		Effect* receiver = Effect::instantiate(QStringLiteral("receiver"), &chain, nullptr);
		QVERIFY2(receiver != nullptr, "the Receiver module did not load: it is built with this test, so this is a failure");
		chain.appendEffect(receiver);

		QDomDocument doc;
		receiver->controls()->restoreState(settings(doc, 0.5f, false));
		AudioBuffer block = stereoBlock(0.25f, -0.25f);
		const AudioBuffer received = sidechainBlock(0.5f, 0.25f);
		chain.processAudioBuffer(block, &received);
		for (f_cnt_t f = 0; f < Frames; ++f)
		{
			QCOMPARE(block.interleavedBuffer()[f][0], 0.5f);    // 0.25 + 0.5 * 0.5
			QCOMPARE(block.interleavedBuffer()[f][1], -0.125f); // -0.25 + 0.25 * 0.5
		}
		chain.clear();
	}

	void replaceStandsInForTheChainsOwnSignal()
	{
		EffectChain chain{nullptr};
		Effect* receiver = Effect::instantiate(QStringLiteral("receiver"), &chain, nullptr);
		QVERIFY2(receiver != nullptr, "the Receiver module did not load: it is built with this test, so this is a failure");
		chain.appendEffect(receiver);

		QDomDocument doc;
		receiver->controls()->restoreState(settings(doc, 1.f, true));
		AudioBuffer block = stereoBlock(0.9f, 0.9f);
		const AudioBuffer received = sidechainBlock(0.5f, 0.25f);
		chain.processAudioBuffer(block, &received);
		QCOMPARE(block.interleavedBuffer()[0][0], 0.5f);
		QCOMPARE(block.interleavedBuffer()[Frames - 1][1], 0.25f);
		chain.clear();
	}

	//! With no sidechain input the device hears nothing from outside: the own signal passes, or silence on replace.
	void withoutASidechainTheOwnSignalPassesOrSilenceOnReplace()
	{
		EffectChain chain{nullptr};
		Effect* receiver = Effect::instantiate(QStringLiteral("receiver"), &chain, nullptr);
		QVERIFY2(receiver != nullptr, "the Receiver module did not load: it is built with this test, so this is a failure");
		chain.appendEffect(receiver);

		QDomDocument doc;
		receiver->controls()->restoreState(settings(doc, 1.f, false));
		AudioBuffer kept = stereoBlock(0.25f, -0.5f);
		chain.processAudioBuffer(kept, nullptr);
		QCOMPARE(kept.interleavedBuffer()[3][0], 0.25f);
		QCOMPARE(kept.interleavedBuffer()[3][1], -0.5f);

		receiver->controls()->restoreState(settings(doc, 1.f, true));
		AudioBuffer replaced = stereoBlock(0.25f, -0.5f);
		chain.processAudioBuffer(replaced, nullptr);
		QCOMPARE(replaced.interleavedBuffer()[3][0], 0.f);
		QCOMPARE(replaced.interleavedBuffer()[3][1], 0.f);
		chain.clear();
	}

	void theControlsSaveAndLoadTheirTwoSettings()
	{
		EffectChain chain{nullptr};
		Effect* receiver = Effect::instantiate(QStringLiteral("receiver"), &chain, nullptr);
		QVERIFY2(receiver != nullptr, "the Receiver module did not load: it is built with this test, so this is a failure");
		chain.appendEffect(receiver);
		QCOMPARE(receiver->controls()->controlCount(), 2);
		QCOMPARE(receiver->controls()->nodeName(), QStringLiteral("ReceiverControls"));

		QDomDocument doc;
		receiver->controls()->restoreState(settings(doc, 0.75f, true));
		QDomElement parent = doc.createElement(QStringLiteral("root"));
		doc.appendChild(parent);
		const QDomElement saved = receiver->controls()->saveState(doc, parent);
		QCOMPARE(saved.attribute(QStringLiteral("level")).toFloat(), 0.75f);
		QCOMPARE(saved.attribute(QStringLiteral("replace")).toInt(), 1);

		// A second Receiver given the saved element behaves as the first: replace at 0.75.
		Effect* second = Effect::instantiate(QStringLiteral("receiver"), &chain, nullptr);
		QVERIFY(second != nullptr);
		second->controls()->restoreState(saved);
		QDomElement again = doc.createElement(QStringLiteral("again"));
		doc.appendChild(again);
		const QDomElement resaved = second->controls()->saveState(doc, again);
		QCOMPARE(resaved.attribute(QStringLiteral("level")).toFloat(), 0.75f);
		QCOMPARE(resaved.attribute(QStringLiteral("replace")).toInt(), 1);
		delete second;
		chain.clear();
	}

	//! InsertRecorder passes its input through unchanged and records it to a take while one is running.
	void theInsertRecorderPassesItsInputThroughAndRecordsATake()
	{
		EffectChain chain{nullptr};
		Effect* recorder = Effect::instantiate(QStringLiteral("insertrecorder"), &chain, nullptr);
		QVERIFY2(recorder != nullptr, "the InsertRecorder module did not load: it is built with this test, so this is a failure");
		chain.appendEffect(recorder);
		QVERIFY(recorder->controls() != nullptr);

		AudioBuffer block = stereoBlock(0.125f, -0.375f);
		chain.processAudioBuffer(block, nullptr);
		QCOMPARE(block.interleavedBuffer()[0][0], 0.125f);
		QCOMPARE(block.interleavedBuffer()[Frames - 1][1], -0.375f);

		// Its state is the last take: nothing recorded, so nothing saved.
		QDomDocument doc;
		QDomElement parent = doc.createElement(QStringLiteral("root"));
		doc.appendChild(parent);
		QVERIFY(!recorder->controls()->saveState(doc, parent).hasAttribute(QStringLiteral("last-take")));

		// A take: the recorder of this device writes what passes it to a WAV, and the sound still passes unchanged.
		TakeRecorder* take = TakeRecorder::of(recorder);
		QVERIFY(take != nullptr);
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath(QStringLiteral("take.wav"));
		QString why;
		QVERIFY2(take->start(path, 44100, &why), qPrintable(why));
		QVERIFY(take->recording());
		for (int i = 0; i < 10; ++i)
		{
			AudioBuffer passing = stereoBlock(0.25f, 0.5f);
			chain.processAudioBuffer(passing, nullptr);
			QCOMPARE(passing.interleavedBuffer()[0][0], 0.25f);
		}
		take->stop();
		QVERIFY(!take->recording());
		QCOMPARE(take->lastTake(), path);
		SF_INFO info{};
		SNDFILE* wav = sf_open(path.toLocal8Bit().constData(), SFM_READ, &info);
		QVERIFY2(wav != nullptr, "the take was not written");
		QCOMPARE(info.channels, 2);
		QCOMPARE(info.frames, static_cast<sf_count_t>(10 * Frames));
		sf_close(wav);

		// With a take on disk the device's saved state names it, and a second recorder loads the name back.
		QDomDocument saved;
		QDomElement savedParent = saved.createElement(QStringLiteral("root"));
		saved.appendChild(savedParent);
		const QDomElement element = recorder->controls()->saveState(saved, savedParent);
		QCOMPARE(element.attribute(QStringLiteral("last-take")), path);
		Effect* second = Effect::instantiate(QStringLiteral("insertrecorder"), &chain, nullptr);
		QVERIFY(second != nullptr);
		second->controls()->restoreState(element);
		QCOMPARE(TakeRecorder::of(second)->lastTake(), path);
		delete second;
		chain.clear();
	}

	void takeRefusesExistingFile()
	{
		EffectChain chain{nullptr};
		Effect* recorder = Effect::instantiate(QStringLiteral("insertrecorder"), &chain, nullptr);
		QVERIFY2(recorder != nullptr, "the InsertRecorder module did not load: it is built with this test, so this is a failure");
		chain.appendEffect(recorder);
		TakeRecorder* take = TakeRecorder::of(recorder);
		QVERIFY(take != nullptr);
		QTemporaryDir dir;
		QVERIFY(dir.isValid());

		// A file already at the path is refused: its bytes stay, and no take starts or is named.
		const QString sentinel = dir.filePath(QStringLiteral("sentinel.wav"));
		const QByteArray known = QByteArrayLiteral("not a take: keep these bytes");
		writeBytes(sentinel, known);
		const QString lastBefore = take->lastTake();
		QString why;
		QVERIFY(!take->start(sentinel, 44100, &why));
		QVERIFY2(refusedAsExisting(why, sentinel), qPrintable(why));
		QVERIFY(!take->recording());
		QCOMPARE(bytesOf(sentinel), known);
		QCOMPARE(take->lastTake(), lastBefore);

		// A recorded take is not truncated by a second start on its path: the refusal keeps every frame and byte.
		const QString first = dir.filePath(QStringLiteral("first.wav"));
		recordTake(chain, *take, first, 10);
		QCOMPARE(take->lastTake(), first);
		const QByteArray firstBytes = bytesOf(first);
		QCOMPARE(wavFrames(first), static_cast<sf_count_t>(10 * Frames));
		QString again;
		QVERIFY(!take->start(first, 44100, &again));
		QVERIFY2(refusedAsExisting(again, first), qPrintable(again));
		QVERIFY(!take->recording());
		QCOMPARE(take->lastTake(), first);
		QCOMPARE(wavFrames(first), static_cast<sf_count_t>(10 * Frames));
		QCOMPARE(bytesOf(first), firstBytes);

		// After the refusal a fresh path under a folder that does not exist yet still records.
		const QString nested = dir.filePath(QStringLiteral("not/yet/there/take.wav"));
		recordTake(chain, *take, nested, 10);
		QCOMPARE(take->lastTake(), nested);
		QCOMPARE(wavFrames(nested), static_cast<sf_count_t>(10 * Frames));

		// A directory at the path is refused: it is still a directory, and nothing was created in it.
		const QString folder = dir.filePath(QStringLiteral("folder"));
		QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("folder")));
		QString asDirectory;
		QVERIFY(!take->start(folder, 44100, &asDirectory));
		QVERIFY(!take->recording());
		QVERIFY(QFileInfo(folder).isDir());
		QVERIFY(QDir(folder).isEmpty());
		chain.clear();
	}
};

QTEST_GUILESS_MAIN(ReceiverEffectTest)
#include "ReceiverEffectTest.moc"
