/*
 * SamplerTestSupport.h - Q050-103: the sampler fixture shared by SamplerEnvelopeTest and SamplerVoiceReuseTest
 *
 * A real audiofileprocessor track, a WAV written to a temporary folder, notes rendered period by period through
 * NotePlayHandle::play, and the sampler's models found by name - the way an agent finds them.
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_SAMPLER_TEST_SUPPORT_H
#define LMMS_SAMPLER_TEST_SUPPORT_H

#include <QtTest>

#include <QTemporaryDir>

#include <cmath>
#include <initializer_list>
#include <vector>

#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "SampleFrame.h"
#include "Song.h"
#include "Track.h"

namespace lmms::sampler_test
{

//! A real 16-bit mono WAV: constant level (so a note's output IS its envelope) or a rising ramp (so a note's
//! first frames show where its playback state started).
inline QString writeWav(const QString& path, bool ramp)
{
	const int rate = Engine::audioEngine()->outputSampleRate();
	const int frames = rate * 2;
	QByteArray pcm;
	pcm.reserve(frames * 2);
	for (int i = 0; i < frames; ++i)
	{
		const double level = ramp ? 0.9 * i / frames : 0.5;
		const auto value = static_cast<qint16>(level * 32767.0);
		pcm.append(static_cast<char>(value & 0xff));
		pcm.append(static_cast<char>((value >> 8) & 0xff));
	}
	QByteArray out;
	const auto put32 = [&out](quint32 v) { for (int s = 0; s < 32; s += 8) { out.append(static_cast<char>((v >> s) & 0xff)); } };
	const auto put16 = [&out](quint16 v) { out.append(static_cast<char>(v & 0xff)); out.append(static_cast<char>((v >> 8) & 0xff)); };
	out.append("RIFF");
	put32(36 + static_cast<quint32>(pcm.size()));
	out.append("WAVEfmt ");
	put32(16);
	put16(1);
	put16(1);
	put32(static_cast<quint32>(rate));
	put32(static_cast<quint32>(rate * 2));
	put16(2);
	put16(16);
	out.append("data");
	put32(static_cast<quint32>(pcm.size()));
	out.append(pcm);
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(out) != out.size()) { return {}; }
	return path;
}

//! One sounding note: its handle and the audio it has produced, period by period.
struct Played
{
	NotePlayHandle* nph = nullptr;
	std::vector<float> left; //!< every rendered frame, appended period after period
};


//! One engine, one sampler track at a time; the helpers every sampler test uses.
class SamplerFixture : public QObject
{
public:
	InstrumentTrack* m_track = nullptr;
	Instrument* m_instrument = nullptr;
	QTemporaryDir m_dir;
	f_cnt_t m_fpp = 0;
	int m_rate = 0;

	//! A fresh project with one sampler track playing \a wav.
	void freshSampler(const QString& wav)
	{
		Engine::getSong()->clearProject();
		m_track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(m_track != nullptr);
		m_instrument = m_track->loadInstrument(QStringLiteral("audiofileprocessor"));
		QVERIFY(m_instrument != nullptr);
		QVERIFY(QMetaObject::invokeMethod(m_instrument, "setAudioFile", Q_ARG(QString, wav), Q_ARG(bool, false)));
	}

	AutomatableModel* model(const QString& name) const
	{
		for (int i = 0; i < m_instrument->parameterCount(); ++i)
		{
			if (m_instrument->parameterName(i) == name) { return m_instrument->parameterModel(i); }
		}
		return nullptr;
	}

	int parameterIndex(const QString& name) const
	{
		for (int i = 0; i < m_instrument->parameterCount(); ++i)
		{
			if (m_instrument->parameterName(i) == name) { return i; }
		}
		return -1;
	}

	void setValue(const QString& name, float value)
	{
		AutomatableModel* m = model(name);
		QVERIFY2(m != nullptr, qPrintable(name));
		m->setValue(value);
	}

	int voices() const
	{
		int n = -1;
		QMetaObject::invokeMethod(m_instrument, "activeVoices", Q_RETURN_ARG(int, n));
		return n;
	}

	//! A note of key 69 (the sample's own pitch, so one output frame per sample frame) that is held open.
	Played start(int key = 69, int lengthPeriods = 4000)
	{
		const Note note(TimePos(384), TimePos(0), key);
		Played p;
		p.nph = NotePlayHandleManager::acquire(m_track, 0, m_fpp * lengthPeriods, note);
		return p;
	}

	void renderPeriod(Played& p)
	{
		std::vector<SampleFrame> buffer(static_cast<std::size_t>(m_fpp));
		p.nph->play(std::span<SampleFrame>{buffer});
		for (const SampleFrame& frame : buffer) { p.left.push_back(frame[0]); }
	}

	void renderPeriods(std::initializer_list<Played*> notes, int periods)
	{
		for (int i = 0; i < periods; ++i)
		{
			for (Played* p : notes) { renderPeriod(*p); }
		}
	}

	static float peakOf(const std::vector<float>& v, std::size_t from, std::size_t to)
	{
		float peak = 0.f;
		for (std::size_t i = from; i < to && i < v.size(); ++i) { peak = std::max(peak, std::abs(v[i])); }
		return peak;
	}

	long frames(float ms) const { return static_cast<long>(std::ceil(ms * m_rate / 1000.f)); }


	void initEngine()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
		m_fpp = Engine::audioEngine()->framesPerPeriod();
		m_rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	}

	void cleanupEngine()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		Engine::destroy();
	}
};

} // namespace lmms::sampler_test

#endif // LMMS_SAMPLER_TEST_SUPPORT_H
