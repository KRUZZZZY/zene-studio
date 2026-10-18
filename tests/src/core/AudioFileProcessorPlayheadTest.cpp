/*
 * AudioFileProcessorPlayheadTest.cpp - regression for cluster new-p12-315
 *
 * The playhead must leave the render thread: rendering a note must NOT emit
 * isPlaying synchronously (which would post a GUI event on every audio period),
 * and the GUI poll must deliver the coalesced value exactly once per change.
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>

#include <QMetaObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QVariant>

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "SampleFrame.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

//! A tiny real 16-bit mono WAV on disk so AudioFileProcessor can load a sample.
QString writeToneWav(const QString& path)
{
	constexpr int rate = 44100;
	constexpr int seconds = 1;
	const auto frames = rate * seconds;
	QByteArray pcm;
	pcm.reserve(frames * 2);
	for (int i = 0; i < frames; ++i)
	{
		const auto value = static_cast<qint16>(
			0.5 * 32767.0 * std::sin(2.0 * M_PI * 440.0 * i / rate));
		pcm.append(static_cast<char>(value & 0xff));
		pcm.append(static_cast<char>((value >> 8) & 0xff));
	}

	QByteArray out;
	const auto put32 = [&out](quint32 v) {
		out.append(static_cast<char>(v & 0xff));
		out.append(static_cast<char>((v >> 8) & 0xff));
		out.append(static_cast<char>((v >> 16) & 0xff));
		out.append(static_cast<char>((v >> 24) & 0xff));
	};
	const auto put16 = [&out](quint16 v) {
		out.append(static_cast<char>(v & 0xff));
		out.append(static_cast<char>((v >> 8) & 0xff));
	};
	out.append("RIFF");
	put32(36 + static_cast<quint32>(pcm.size()));
	out.append("WAVEfmt ");
	put32(16);
	put16(1);
	put16(1);
	put32(rate);
	put32(rate * 2);
	put16(2);
	put16(16);
	out.append("data");
	put32(static_cast<quint32>(pcm.size()));
	out.append(pcm);

	QFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(out) != out.size()) { return {}; }
	file.close();
	return path;
}

} // namespace

class AudioFileProcessorPlayheadTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		#ifdef LMMS_TEST_PLUGIN_DIR
		// A real instrument, so the note actually renders (NotePlaybackTest's pattern).
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
		#endif
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	//! BUG 10 (cluster new-p12-315): playback must not emit isPlaying from the
	//! render path, and the GUI poll must deliver the coalesced value.
	void renderPathPublishesPlayheadWithoutSynchronousSignal()
	{
		Engine::getSong()->clearProject();
		auto* track = dynamic_cast<InstrumentTrack*>(
			Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		Instrument* instrument = track->loadInstrument(QStringLiteral("audiofileprocessor"));
		QVERIFY(instrument != nullptr);

		QTemporaryDir dir;
		const QString wav = writeToneWav(dir.filePath(QStringLiteral("tone.wav")));
		QVERIFY2(!wav.isEmpty(), "could not write a WAV fixture");
		// setAudioFile is a public slot on AudioFileProcessor, driven without
		// including the plugin header.
		QVERIFY(QMetaObject::invokeMethod(instrument, "setAudioFile",
			Q_ARG(QString, wav), Q_ARG(bool, false)));

		const f_cnt_t frames = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> output(static_cast<std::size_t>(frames));
		const Note note(TimePos(384), TimePos(0), 60);
		NotePlayHandle* nph = NotePlayHandleManager::acquire(track, 0, frames, note);
		QVERIFY(nph != nullptr);

		QSignalSpy spy(instrument, SIGNAL(isPlaying(lmms::f_cnt_t)));

		// First period: the render path must publish, not emit.
		instrument->playNote(nph, std::span<SampleFrame>{output});
		// Regression gate: the original code emits isPlaying synchronously here.
		QCOMPARE(spy.count(), 0);

		// The GUI poll must deliver the coalesced playhead exactly once.
		QVERIFY(QMetaObject::invokeMethod(instrument, "pollPlayhead"));
		QCOMPARE(spy.count(), 1);
		const auto firstFrame = spy.at(0).at(0).value<lmms::f_cnt_t>();
		QVERIFY(firstFrame > 0);

		// Second period: render advances the playhead.
		instrument->playNote(nph, std::span<SampleFrame>{output});
		QCOMPARE(spy.count(), 1); // still no per-period emit

		QVERIFY(QMetaObject::invokeMethod(instrument, "pollPlayhead"));
		QCOMPARE(spy.count(), 2);
		const auto secondFrame = spy.at(1).at(0).value<lmms::f_cnt_t>();
		QVERIFY(secondFrame > firstFrame);

		// Coalescing: a repeat poll with no new frame emits nothing.
		QVERIFY(QMetaObject::invokeMethod(instrument, "pollPlayhead"));
		QCOMPARE(spy.count(), 2);

		NotePlayHandleManager::release(nph);
	}
};

QTEST_MAIN(AudioFileProcessorPlayheadTest)
#include "AudioFileProcessorPlayheadTest.moc"
