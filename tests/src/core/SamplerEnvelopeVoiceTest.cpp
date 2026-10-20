/*
 * SamplerEnvelopeVoiceTest.cpp - Q050-103: the sampler's attack/release envelope and its voice-reuse policy
 *
 * Renders real notes through the audiofileprocessor module and measures them: the attack ramp, the release
 * tail, which voice a new note takes when the voice limit is reached, that the new note starts fresh, that
 * the envelope values automate / save / load, that projects without them render as before, and that a note
 * start and a voice reuse allocate nothing.
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>

#include <QApplication>
#include <QDomDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentView.h"
#include "Knob.h"
#include "LcdSpinBox.h"
#include "InstrumentTrack.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "ReversibilityTestSupport.h"
#include "SampleFrame.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

thread_local bool countAllocations = false;
thread_local std::uint64_t allocationCount = 0;

//! A real 16-bit mono WAV: constant level (so a note's output IS its envelope) or a rising ramp (so a note's
//! first frames show where its playback state started).
QString writeWav(const QString& path, bool ramp)
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

} // namespace

// Counting allocator: any allocation inside a counted region is a realtime violation.
void* operator new(std::size_t size)
{
	if (countAllocations) { ++allocationCount; }
	if (void* memory = std::malloc(size != 0 ? size : 1)) { return memory; }
	throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

class SamplerEnvelopeVoiceTest : public QObject
{
	Q_OBJECT

private:
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

private slots:
	void initTestCase()
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

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::getSong()->clearProject();
		Engine::destroy();
	}

	//! A sampler from before Q050-103 has no attack and a 3 ms release: the new models default to exactly that.
	void defaultsAreTheOldBehaviour()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		QCOMPARE(model(QStringLiteral("Attack"))->value<float>(), 0.f);
		QCOMPARE(model(QStringLiteral("Release"))->value<float>(), 3.f);
		QCOMPARE(model(QStringLiteral("Voice limit"))->value<float>(), 64.f);
		QCOMPARE(m_instrument->desiredReleaseTimeMs(), 3.f);
		// They are appended after the eight existing parameters, so those keep their agent-facing indices.
		QVERIFY(parameterIndex(QStringLiteral("Amplify")) == 0);
		QCOMPARE(parameterIndex(QStringLiteral("Interpolation mode")), 7);
		QCOMPARE(parameterIndex(QStringLiteral("Attack")), 8);
		QCOMPARE(parameterIndex(QStringLiteral("Release")), 9);
		QCOMPARE(parameterIndex(QStringLiteral("Voice limit")), 10);

		Played p = start();
		renderPeriods({&p}, 3);
		// No attack: full level from the first frame.
		QVERIFY2(std::abs(p.left[0]) > 0.4f, "with no attack the note starts at full level");
		NotePlayHandleManager::release(p.nph);
	}

	//! A note fades in from silence over the attack time, and a note that starts later has its own ramp.
	void attackRampsEachNoteFromSilence()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		Played reference = start();
		renderPeriods({&reference}, 4);
		const float full = reference.left[2 * m_fpp];
		QVERIFY(full > 0.4f);
		NotePlayHandleManager::release(reference.nph);

		setValue(QStringLiteral("Attack"), 10.f);
		const long attack = frames(10.f);
		QVERIFY(attack > static_cast<long>(m_fpp)); // spans several periods, so the ramp crosses period boundaries
		Played a = start();
		renderPeriods({&a}, 4);
		QVERIFY2(std::abs(a.left[0]) < 1e-4f, "the note starts from silence");
		for (const long at : {attack / 4, attack / 2, attack * 3 / 4})
		{
			const float expected = full * static_cast<float>(at) / static_cast<float>(attack);
			QVERIFY2(std::abs(a.left[at] - expected) < 0.02f * full,
				qPrintable(QStringLiteral("frame %1: %2 vs ramp %3").arg(at).arg(a.left[at]).arg(expected)));
		}
		QVERIFY2(std::abs(a.left[attack + 8] - full) < 0.01f * full, "after the attack the note is at full level");

		// A second note starting while the first is already at full level ramps on its own.
		Played b = start(69);
		renderPeriods({&a}, 1);
		renderPeriods({&b}, 1);
		QVERIFY2(std::abs(b.left[0]) < 1e-4f, "an overlapping note starts from silence too");
		QVERIFY2(std::abs(b.left[m_fpp / 2]) < full * 0.9f, "and is still rising while the earlier note is full");
		NotePlayHandleManager::release(a.nph);
		NotePlayHandleManager::release(b.nph);
	}

	//! The release time is the model's value: the note's audible tail follows it, and it is not the old fixed 3 ms.
	void releaseTailFollowsTheModel()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		const auto audibleFrames = [&](float releaseMs) {
			setValue(QStringLiteral("Release"), releaseMs);
			const Note note(TimePos(384), TimePos(0), 69);
			Played p;
			p.nph = NotePlayHandleManager::acquire(m_track, 0, m_fpp * 2, note);
			for (int i = 0; i < 400 && !p.nph->isFinished(); ++i) { renderPeriod(p); }
			if (!p.nph->isFinished()) { return -1L; }
			std::size_t audible = 0;
			for (std::size_t i = 0; i < p.left.size(); ++i) { if (std::abs(p.left[i]) > 1e-3f) { audible = i + 1; } }
			NotePlayHandleManager::release(p.nph);
			return static_cast<long>(audible);
		};
		const long noteFrames = static_cast<long>(m_fpp) * 2;
		for (const float ms : {3.f, 25.f, 80.f})
		{
			const long audible = audibleFrames(ms);
			QVERIFY2(audible > 0, "the released note ends");
			const long tail = audible - noteFrames;
			QVERIFY2(std::labs(tail - frames(ms)) <= std::max(4L, frames(ms) / 100), // the last frames sit under the audibility threshold
				qPrintable(QStringLiteral("release %1 ms: tail %2 frames, expected %3").arg(ms).arg(tail).arg(frames(ms))));
		}
	}

	//! The released note fades linearly: halfway through the tail it is at half level.
	void releaseFadesLinearly()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		setValue(QStringLiteral("Release"), 40.f);
		const Note note(TimePos(384), TimePos(0), 69);
		Played p;
		p.nph = NotePlayHandleManager::acquire(m_track, 0, m_fpp * 2, note);
		for (int i = 0; i < 400 && !p.nph->isFinished(); ++i) { renderPeriod(p); }
		const long release = frames(40.f);
		const float full = p.left[m_fpp / 2];
		const float half = p.left[static_cast<std::size_t>(static_cast<long>(m_fpp) * 2 + release / 2)];
		QVERIFY2(std::abs(half - 0.5f * full) < 0.05f * full, qPrintable(QStringLiteral("%1 vs %2").arg(half).arg(full)));
		NotePlayHandleManager::release(p.nph);
	}

	//! At the voice limit a RELEASED voice is taken before a held one - even when the held voice is older.
	void releasedVoiceIsReusedBeforeAHeldOne()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		setValue(QStringLiteral("Voice limit"), 2.f);
		setValue(QStringLiteral("Release"), 200.f); // a long tail: the released note would sound for ~34 periods
		Played held = start(69);       // oldest, held
		renderPeriods({&held}, 1);
		Played released = start(71);   // newer, then released
		renderPeriods({&held, &released}, 1);
		released.nph->noteOff(0);
		renderPeriods({&held, &released}, 2);
		QCOMPARE(voices(), 2);
		QVERIFY2(peakOf(released.left, released.left.size() - static_cast<std::size_t>(m_fpp), released.left.size()) > 0.05f,
			"the released note is still sounding its tail");

		Played incoming = start(73);
		renderPeriods({&held, &released, &incoming}, 3);

		// The released voice was reused: its tail was faded out within a few ms and is silent while the held note goes on.
		const std::size_t last = released.left.size();
		QVERIFY2(peakOf(released.left, last - static_cast<std::size_t>(m_fpp), last) < 1e-5f,
			"the released note's voice was taken: it is silent although its release tail is far from over");
		QVERIFY2(peakOf(held.left, held.left.size() - static_cast<std::size_t>(m_fpp), held.left.size()) > 0.4f,
			"the older held note was not touched");
		QVERIFY2(peakOf(incoming.left, 0, incoming.left.size()) > 0.4f, "the new note sounds");
		QCOMPARE(voices(), 2);

		// The reuse did not click: the stolen note's level falls to zero over a few ms, not in one frame.
		const std::size_t from = released.left.size() - 3 * static_cast<std::size_t>(m_fpp);
		float biggestStep = 0.f;
		for (std::size_t i = from + 1; i < released.left.size(); ++i)
		{
			biggestStep = std::max(biggestStep, std::abs(released.left[i] - released.left[i - 1]));
		}
		QVERIFY2(biggestStep < 0.05f, qPrintable(QStringLiteral("largest frame-to-frame step %1").arg(biggestStep)));
		for (Played* p : {&held, &released, &incoming}) { NotePlayHandleManager::release(p->nph); }
	}

	//! With no released voice the OLDEST held voice is the one reused.
	void oldestHeldVoiceIsReusedWhenNoneIsReleased()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		setValue(QStringLiteral("Voice limit"), 2.f);
		Played oldest = start(69);
		renderPeriods({&oldest}, 1);
		Played newer = start(71);
		renderPeriods({&oldest, &newer}, 2);
		Played incoming = start(73);
		renderPeriods({&oldest, &newer, &incoming}, 3);

		const auto lastPeriodPeak = [&](const Played& p) { return peakOf(p.left, p.left.size() - static_cast<std::size_t>(m_fpp), p.left.size()); };
		QVERIFY2(lastPeriodPeak(oldest) < 1e-5f, "the oldest held note was taken");
		QVERIFY2(lastPeriodPeak(newer) > 0.4f, "the newer held note continues");
		QVERIFY2(lastPeriodPeak(incoming) > 0.4f, "the new note sounds");
		for (Played* p : {&oldest, &newer, &incoming}) { NotePlayHandleManager::release(p->nph); }
	}

	//! The order among released voices is fixed: the one released longest ago goes first.
	void releasedVoicesAreTakenInReleaseOrder()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		setValue(QStringLiteral("Voice limit"), 3.f);
		setValue(QStringLiteral("Release"), 400.f);
		Played a = start(69);
		Played b = start(71);
		Played c = start(73);
		renderPeriods({&a, &b, &c}, 1);
		// b is released first, then a: b has been released longer, although a started earlier.
		b.nph->noteOff(0);
		renderPeriods({&a, &b, &c}, 1);
		a.nph->noteOff(0);
		renderPeriods({&a, &b, &c}, 1);

		Played d = start(75);
		renderPeriods({&a, &b, &c, &d}, 3);
		const auto lastPeriodPeak = [&](const Played& p) { return peakOf(p.left, p.left.size() - static_cast<std::size_t>(m_fpp), p.left.size()); };
		QVERIFY2(lastPeriodPeak(b) < 1e-5f, "b was released first, so b is reused first");
		QVERIFY2(lastPeriodPeak(a) > 0.05f, "a is still in its release tail");
		QVERIFY2(lastPeriodPeak(c) > 0.4f, "the held note is untouched");
		for (Played* p : {&a, &b, &c, &d}) { NotePlayHandleManager::release(p->nph); }
	}

	//! A note that takes over a voice starts from the beginning of the sample with its own attack, whatever
	//! the previous owner had played.
	void reusedVoiceStartsFresh()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("ramp.wav")), true));
		setValue(QStringLiteral("Voice limit"), 1.f);
		setValue(QStringLiteral("Attack"), 5.f);
		Played first = start(69);
		renderPeriods({&first}, 12); // well into the sample: the ramp is now far above its start
		const float firstLevel = first.left.back();
		QVERIFY(firstLevel > 0.01f);
		Played second = start(69);
		renderPeriods({&first, &second}, 1);
		QVERIFY2(std::abs(second.left[0]) < 1e-4f, "the new note's envelope starts at zero");
		// Sample start is value 0 rising to 0.9 over two seconds: after a period it is still tiny, far below
		// where the first note's playback was.
		QVERIFY2(second.left.back() < firstLevel * 0.2f,
			qPrintable(QStringLiteral("new note level %1 vs old note %2").arg(second.left.back()).arg(firstLevel)));
		NotePlayHandleManager::release(first.nph);
		NotePlayHandleManager::release(second.nph);
	}

	//! Held notes beyond the limit are not silently dropped: every note sounds when it starts.
	void everyNoteSoundsWhenItStarts()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		setValue(QStringLiteral("Voice limit"), 3.f);
		std::vector<std::unique_ptr<Played>> notes;
		for (int i = 0; i < 12; ++i)
		{
			notes.push_back(std::make_unique<Played>(start(60 + i)));
			renderPeriod(*notes.back());
			QVERIFY2(peakOf(notes.back()->left, 0, notes.back()->left.size()) > 0.4f,
				qPrintable(QStringLiteral("note %1 sounds on its first period").arg(i)));
			// Older notes keep rendering (and fade out when reused).
			for (std::size_t j = 0; j + 1 < notes.size(); ++j) { renderPeriod(*notes[j]); }
		}
		// Only the limit plus the voices still fading are in use.
		QVERIFY(voices() <= 3 + 1);
		for (auto& p : notes) { NotePlayHandleManager::release(p->nph); }
		QCOMPARE(voices(), 0);
	}

	//! Starting a note, reusing a voice and releasing a note allocate nothing.
	void noteStartAndVoiceReuseDoNotAllocate()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		setValue(QStringLiteral("Voice limit"), 2.f);
		setValue(QStringLiteral("Attack"), 4.f);
		std::vector<SampleFrame> buffer(static_cast<std::size_t>(m_fpp));
		std::vector<NotePlayHandle*> handles;
		for (int i = 0; i < 6; ++i) { handles.push_back(NotePlayHandleManager::acquire(m_track, 0, m_fpp * 100, Note(TimePos(384), TimePos(0), 60 + i))); }

		allocationCount = 0;
		countAllocations = true;
		for (std::size_t i = 0; i < handles.size(); ++i)
		{
			// Starts note i (past the limit from the third on, so it reuses a voice), then renders the earlier ones.
			for (std::size_t j = 0; j <= i; ++j) { m_instrument->playNote(handles[j], std::span<SampleFrame>{buffer}); }
			if (i == 2) { handles[0]->noteOff(0); }
		}
		countAllocations = false;
		const std::uint64_t allocations = allocationCount;
		for (auto* h : handles) { NotePlayHandleManager::release(h); }
		QCOMPARE(static_cast<qulonglong>(allocations), 0ull);
	}

	//! The envelope values and the voice limit save, load, and leave the loop controls as they were.
	void settingsRoundTripAndTheLoopControlsStayStable()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		setValue(QStringLiteral("Attack"), 12.5f);
		setValue(QStringLiteral("Release"), 40.f);
		setValue(QStringLiteral("Voice limit"), 7.f);
		setValue(QStringLiteral("Loop mode"), 2.f);
		setValue(QStringLiteral("Start of sample"), 0.1f);
		setValue(QStringLiteral("End of sample"), 0.8f);
		setValue(QStringLiteral("Loopback point"), 0.3f);
		setValue(QStringLiteral("Reverse sample"), 1.f);
		setValue(QStringLiteral("Stutter"), 1.f);

		QDomDocument doc;
		QDomElement saved = doc.createElement(QStringLiteral("state"));
		m_instrument->saveState(doc, saved);
		doc.appendChild(saved);
		const QDomElement settings = saved.firstChildElement(); // the plugin's own element
		QVERIFY(settings.hasAttribute(QStringLiteral("atk")));
		QVERIFY(settings.hasAttribute(QStringLiteral("rel")));
		QVERIFY(settings.hasAttribute(QStringLiteral("voices")));

		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		m_instrument->restoreState(settings);
		QCOMPARE(model(QStringLiteral("Attack"))->value<float>(), 12.5f);
		QCOMPARE(model(QStringLiteral("Release"))->value<float>(), 40.f);
		QCOMPARE(model(QStringLiteral("Voice limit"))->value<float>(), 7.f);
		QCOMPARE(m_instrument->desiredReleaseTimeMs(), 40.f);
		QCOMPARE(model(QStringLiteral("Loop mode"))->value<float>(), 2.f);
		QVERIFY(std::abs(model(QStringLiteral("Start of sample"))->value<float>() - 0.1f) < 1e-6f);
		QVERIFY(std::abs(model(QStringLiteral("End of sample"))->value<float>() - 0.8f) < 1e-6f);
		QVERIFY(std::abs(model(QStringLiteral("Loopback point"))->value<float>() - 0.3f) < 1e-6f);
		QCOMPARE(model(QStringLiteral("Reverse sample"))->value<float>(), 1.f);
		QCOMPARE(model(QStringLiteral("Stutter"))->value<float>(), 1.f);
	}

	//! A project saved before these settings existed has none of them: it loads with attack 0, release 3 ms and the
	//! full limit - the sampler it always was - and its loop-point compatibility rule still holds.
	void oldProjectsLoadWithTheOldBehaviour()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		setValue(QStringLiteral("Attack"), 30.f);
		setValue(QStringLiteral("Release"), 90.f);
		setValue(QStringLiteral("Voice limit"), 5.f);

		QDomDocument doc;
		QDomElement old = doc.createElement(QStringLiteral("audiofileprocessor"));
		old.setAttribute(QStringLiteral("src"), m_dir.filePath(QStringLiteral("dc.wav")));
		old.setAttribute(QStringLiteral("looped"), 1);
		old.setAttribute(QStringLiteral("amp"), 100);
		old.setAttribute(QStringLiteral("sframe"), 0.25);
		old.setAttribute(QStringLiteral("eframe"), 0.9);
		old.setAttribute(QStringLiteral("reversed"), 0);
		old.setAttribute(QStringLiteral("stutter"), 0);
		m_instrument->restoreState(old);

		QCOMPARE(model(QStringLiteral("Attack"))->value<float>(), 0.f);
		QCOMPARE(model(QStringLiteral("Release"))->value<float>(), 3.f);
		QCOMPARE(model(QStringLiteral("Voice limit"))->value<float>(), 64.f);
		QCOMPARE(m_instrument->desiredReleaseTimeMs(), 3.f);
		QCOMPARE(model(QStringLiteral("Loop mode"))->value<float>(), 1.f);
		QVERIFY(std::abs(model(QStringLiteral("Start of sample"))->value<float>() - 0.25f) < 1e-6f);
		QVERIFY(std::abs(model(QStringLiteral("End of sample"))->value<float>() - 0.9f) < 1e-6f);
		// no lframe in the old file: the loop point follows the start point, as it always did
		QVERIFY(std::abs(model(QStringLiteral("Loopback point"))->value<float>() - 0.25f) < 1e-6f);
	}

	//! The window has the controls: knobs for attack and release and a box for the voice limit, bound to the models the
	//! agent reads, and turning one in the GUI changes the model the render path reads.
	void windowControlsDriveTheModels()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		QWidget parent;
		std::unique_ptr<gui::PluginView> view(m_instrument->createView(&parent));
		QVERIFY(view != nullptr);
		view->resize(250, 250);
		view->show();
		QApplication::processEvents();

		const auto wheelUp = [](QWidget* w) {
			const QPointF at(w->width() / 2.0, w->height() / 2.0);
			QWheelEvent event(at, w->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
				Qt::NoScrollPhase, false);
			QApplication::sendEvent(w, &event);
		};

		for (const QString& name : {QStringLiteral("Attack"), QStringLiteral("Release")})
		{
			gui::Knob* knob = nullptr;
			for (gui::Knob* k : view->findChildren<gui::Knob*>())
			{
				if (k->model() == model(name)) { knob = k; }
			}
			QVERIFY2(knob != nullptr, qPrintable(name + " has a knob in the window"));
			const float before = model(name)->value<float>();
			wheelUp(knob);
			QVERIFY2(model(name)->value<float>() > before, qPrintable(name + ": the knob moved the model"));
		}
		QCOMPARE(m_instrument->desiredReleaseTimeMs(), model(QStringLiteral("Release"))->value<float>());

		gui::LcdSpinBox* box = nullptr;
		for (gui::LcdSpinBox* b : view->findChildren<gui::LcdSpinBox*>())
		{
			if (b->model() == model(QStringLiteral("Voice limit"))) { box = b; }
		}
		QVERIFY2(box != nullptr, "the voice limit has a box in the window");
		const float limit = model(QStringLiteral("Voice limit"))->value<float>();
		QVERIFY(limit > 1.f);
		QWheelEvent down(QPointF(4, 4), box->mapToGlobal(QPointF(4, 4)), QPoint(), QPoint(0, -120), Qt::NoButton,
			Qt::NoModifier, Qt::NoScrollPhase, false);
		QApplication::sendEvent(box, &down);
		QVERIFY2(model(QStringLiteral("Voice limit"))->value<float>() < limit, "the box lowered the voice limit");
	}

	//! Automation drives the attack: a clip on the parameter (recorded through automation.add_point) moves the model as the
	//! transport plays, and a note started then ramps for the automated time. The agent's plugin.param_set / param_get see
	//! the same models.
	void automationDrivesTheEnvelope()
	{
		freshSampler(writeWav(m_dir.filePath(QStringLiteral("dc.wav")), false));
		const QString trackId = control::trackIdOf(m_track);
		const int attackIndex = parameterIndex(QStringLiteral("Attack"));
		const int releaseIndex = parameterIndex(QStringLiteral("Release"));
		QVERIFY(attackIndex >= 0 && releaseIndex >= 0);
		const QString attackParam = QStringLiteral("inst/%1").arg(attackIndex);

		// The agent surface: param_set / param_get on the instrument.
		const auto setParam = [&](int index, double value) {
			return revtest::run(QStringLiteral("plugin.param_set"),
				{{QStringLiteral("target"), trackId}, {QStringLiteral("plugin"), QStringLiteral("inst")},
					{QStringLiteral("index"), index}, {QStringLiteral("value"), value}});
		};
		ControlResult set = setParam(releaseIndex, 55.0);
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(model(QStringLiteral("Release"))->value<float>(), 55.f);
		const ControlResult got = revtest::run(QStringLiteral("plugin.param_get"),
			{{QStringLiteral("target"), trackId}, {QStringLiteral("plugin"), QStringLiteral("inst")},
				{QStringLiteral("index"), releaseIndex}});
		QVERIFY2(got.ok, qPrintable(got.errorMessage));
		QCOMPARE(got.result.value(QStringLiteral("parameter")).toObject().value(QStringLiteral("value")).toDouble(), 55.0);
		QCOMPARE(m_instrument->desiredReleaseTimeMs(), 55.f);
		QCOMPARE(revtest::run(QStringLiteral("control.undo")).ok, true);
		QCOMPARE(model(QStringLiteral("Release"))->value<float>(), 3.f);

		// Automation: attack 0 ms at tick 0, 60 ms from tick 48 (a new clip holds each value until the next point).
		for (const auto& [tick, value] : {std::pair{0, 0.0}, std::pair{48, 60.0}})
		{
			const ControlResult added = revtest::run(QStringLiteral("automation.add_point"),
				{{QStringLiteral("track"), trackId}, {QStringLiteral("parameter"), attackParam},
					{QStringLiteral("ticks"), tick}, {QStringLiteral("value"), value}});
			QVERIFY2(added.ok, qPrintable(added.errorMessage));
		}
		Song* song = Engine::getSong();
		song->playSong();
		song->getTimeline().setTicks(0);
		int buffers = 0;
		for (; buffers < 400 && song->getPlayPos().getTicks() < 96; ++buffers)
		{
			AutomatableModel::incrementPeriodCounter(); // one render period, as the engine counts them
			song->processNextBuffer();
		}
		const int reached = song->getPlayPos().getTicks();
		const float automated = model(QStringLiteral("Attack"))->value<float>(); // read while playing: stop() rewinds the models
		song->stop();
		QVERIFY2(reached >= 96, "the transport never passed the second automation point");
		model(QStringLiteral("Attack"))->setValue(automated); // hold the automated value, as a note started mid-clip sees it
		QVERIFY2(std::abs(automated - 60.f) < 0.5f, qPrintable(QStringLiteral("attack %1 ms").arg(automated)));

		// The automated value is what a note started now ramps over.
		Played p = start();
		renderPeriods({&p}, 16);
		const long attack = frames(automated);
		QVERIFY(attack > 2 * static_cast<long>(m_fpp) && attack + 100 < static_cast<long>(p.left.size()));
		const float full = p.left[static_cast<std::size_t>(attack + 100)];
		QVERIFY(full > 0.4f);
		QVERIFY2(std::abs(p.left[0]) < 1e-4f, "starts from silence");
		QVERIFY2(std::abs(p.left[static_cast<std::size_t>(m_fpp)] - full * static_cast<float>(m_fpp) / static_cast<float>(attack))
				< 0.03f * full,
			"the ramp length is the automated attack");
		NotePlayHandleManager::release(p.nph);
	}
};

QTEST_MAIN(SamplerEnvelopeVoiceTest)
#include "SamplerEnvelopeVoiceTest.moc"
