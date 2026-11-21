/*
 * SfzTestSupport.h - Q050-484: a small SFZ instrument written to disk, and a way to play it and read what it sounds like
 *
 * Three constant-level WAVs (0.25, 0.5, 0.75) make every sample distinguishable by its level alone, and the SFZ
 * maps them so that a velocity boundary, a round-robin sequence and a key switch each pick a different one.
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_SFZ_TEST_SUPPORT_H
#define LMMS_SFZ_TEST_SUPPORT_H

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QString>

#include <algorithm>
#include <cmath>
#include <vector>

#include "AudioEngine.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Midi.h"
#include "MidiEvent.h"
#include "SampleFrame.h"
#include "Song.h"
#include "Track.h"

namespace lmms::sfz_test
{

//! A 16-bit mono WAV of \a frames frames at the engine's rate holding the constant \a level.
inline bool writeWav(const QString& path, float level, int frames)
{
	const int rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	QByteArray pcm;
	const auto value = static_cast<qint16>(level * 32767.f);
	for (int i = 0; i < frames; ++i)
	{
		pcm.append(static_cast<char>(value & 0xff));
		pcm.append(static_cast<char>((value >> 8) & 0xff));
	}
	QByteArray out;
	const auto put32 = [&out](quint32 v) { for (int s = 0; s < 32; s += 8) { out.append(static_cast<char>((v >> s) & 0xff)); } };
	const auto put16 = [&out](quint16 v) { out.append(static_cast<char>(v & 0xff)); out.append(static_cast<char>((v >> 8) & 0xff)); };
	out.append("RIFF");
	put32(36 + static_cast<quint32>(pcm.size()));
	out.append("WAVEfmt ");
	put32(16); put16(1); put16(1);
	put32(static_cast<quint32>(rate)); put32(static_cast<quint32>(rate * 2));
	put16(2); put16(16);
	out.append("data");
	put32(static_cast<quint32>(pcm.size()));
	out.append(pcm);
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(out) == out.size();
}

/*! Writes low.wav, mid.wav, high.wav (levels 0.25, 0.5, 0.75, \a sampleFrames long) and layers.sfz into \a dir:
 *   - key 60: velocity 1..63 plays low, 64..127 plays mid (the velocity layers);
 *   - key 62: three regions in a round-robin sequence, low, mid, high, low, ...;
 *   - key 64: low, or high after key switch 37 (key switch 36, the default, keeps low).
 *  Levels do not follow velocity (amp_veltrack=0), so a note's level names its sample. Returns the .sfz path. */
inline QString writeLayeredInstrument(const QString& dir, int sampleFrames = 4000)
{
	const bool wavs = writeWav(dir + "/low.wav", 0.25f, sampleFrames) && writeWav(dir + "/mid.wav", 0.5f, sampleFrames)
		&& writeWav(dir + "/high.wav", 0.75f, sampleFrames);
	if (!wavs) { return {}; }
	const QByteArray sfz =
		"<global> amp_veltrack=0 ampeg_release=0.01\n"
		"<group> key=60\n"
		"<region> sample=low.wav lovel=1 hivel=63\n"
		"<region> sample=mid.wav lovel=64 hivel=127\n"
		"<group> key=62 seq_length=3\n"
		"<region> sample=low.wav seq_position=1\n"
		"<region> sample=mid.wav seq_position=2\n"
		"<region> sample=high.wav seq_position=3\n"
		"<group> key=64 sw_lokey=36 sw_hikey=37 sw_default=36\n"
		"<region> sample=low.wav sw_last=36\n"
		"<region> sample=high.wav sw_last=37\n";
	QFile file(dir + "/layers.sfz");
	if (!file.open(QIODevice::WriteOnly) || file.write(sfz) != sfz.size()) { return {}; }
	return file.fileName();
}

//! The SFZ Player on a fresh track, played through its own MIDI entry point and rendered period by period.
class SfzSession
{
public:
	Instrument* instrument = nullptr;
	InstrumentTrack* track = nullptr;

	bool start()
	{
		Engine::getSong()->clearProject();
		track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		instrument = track != nullptr ? track->loadInstrument(QStringLiteral("sfzplayer")) : nullptr;
		return instrument != nullptr;
	}

	//! A state restore (control.undo, plugin.open_file's refusal) rebuilds the track's instrument: look it up again.
	void refresh() { instrument = track->instrument(); }

	int regions()
	{
		refresh();
		int n = -1;
		QMetaObject::invokeMethod(instrument, "regionCount", Q_RETURN_ARG(int, n));
		return n;
	}

	bool open(const QString& sfz)
	{
		refresh();
		bool ok = false;
		QMetaObject::invokeMethod(instrument, "openFile", Q_RETURN_ARG(bool, ok), Q_ARG(QString, sfz), Q_ARG(bool, false));
		return ok;
	}

	void noteOn(int key, int velocity, int offset = 0)
	{
		refresh();
		instrument->handleMidiEvent(MidiEvent(MidiNoteOn, 0, key, velocity), TimePos(), offset);
	}

	void noteOff(int key, int offset = 0)
	{
		refresh();
		instrument->handleMidiEvent(MidiEvent(MidiNoteOff, 0, key, 0), TimePos(), offset);
	}

	//! Renders \a periods periods and returns the left channel of all of them.
	std::vector<float> render(int periods)
	{
		refresh();
		const auto frames = static_cast<std::size_t>(Engine::audioEngine()->framesPerPeriod());
		std::vector<float> left;
		std::vector<SampleFrame> buffer(frames);
		for (int i = 0; i < periods; ++i)
		{
			instrument->play(std::span<SampleFrame>{buffer});
			for (const SampleFrame& frame : buffer) { left.push_back(frame[0]); }
		}
		return left;
	}

	//! The level of a short note: played for a few periods, then released and let die away.
	float levelOf(int key, int velocity)
	{
		noteOn(key, velocity);
		const std::vector<float> sound = render(6);
		noteOff(key);
		render(4);
		return peak(sound, sound.size() / 4, sound.size());
	}

	static float peak(const std::vector<float>& v, std::size_t from, std::size_t to)
	{
		float p = 0.f;
		for (std::size_t i = from; i < to && i < v.size(); ++i) { p = std::max(p, std::abs(v[i])); }
		return p;
	}
};

} // namespace lmms::sfz_test

#endif // LMMS_SFZ_TEST_SUPPORT_H
