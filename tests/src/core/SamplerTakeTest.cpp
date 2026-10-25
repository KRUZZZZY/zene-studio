/*
 * SamplerTakeTest.cpp - record a bounded input take straight into AudioFileProcessor / SlicerT
 *                       (0.5.0 L10, Q050-156)
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

/*! THE PROOF for Q050-156, driven through the agent surface on the Dummy device. The test drives the
 *  periods itself and feeds the engine's input a KNOWN signal (frame n is a value only n has), so a
 *  take can be checked frame by frame:
 *   - record.sampler_capture arms a bounded take for an AudioFileProcessor or SlicerT track and the
 *     audio thread stages exactly the requested frames (after the recording round trip);
 *   - record.sampler_capture_stop ends a longer take early, and its frames are what was taken;
 *   - record.sampler_capture_land writes the take to a WAV, loads it into the device (the track's
 *     saved state points at it; a note renders it; SlicerT has slice points) and is one undo step:
 *     control.undo puts the previous sample back, control.redo loads it again;
 *   - the project saves and reopens with the sample and the SlicerT state intact.
 *  Nothing here uses the engine class directly, so the same binary built on the base answers every
 *  command unknown and fails each case: the red half of the proof. */

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

#include <sndfile.h>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Note.h"
#include "NotePlayHandle.h"
#include "PathUtil.h"
#include "SampleFrame.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! Input frame @a n: a value no other frame of the first 2^24 has, exact in a float and in a 24-bit file.
SampleFrame inputFrame(std::uint64_t n)
{
	// A known, zero-mean ramp gated in 2048-frame bursts: the test can name every frame, and
	// SlicerT's onset detector has real transients to cut at.
	const bool on = ((n / 2048) % 2) == 0;
	const float gain = on ? 1.0f : 0.0f;
	const float sign = (n % 2) == 0 ? -1.0f : 1.0f;
	return SampleFrame(gain * sign * (static_cast<float>(n % 4096) / 4096.0f - 0.5f),
		gain * sign * (static_cast<float>((n / 4096) % 4096) / 4096.0f - 0.5f));
}

//! Two steps of a 24-bit file: libsndfile writes with 2^23 - 1 and reads with 2^23.
constexpr float Pcm24Tolerance = 2.0f / 8388608.0f;

std::vector<SampleFrame> readWav(const QString& path, int* rate)
{
	SF_INFO info{};
	SNDFILE* file = sf_open(path.toUtf8().constData(), SFM_READ, &info);
	if (file == nullptr || info.channels != 2) { if (file) { sf_close(file); } return {}; }
	std::vector<float> interleaved(static_cast<std::size_t>(info.frames) * 2);
	const sf_count_t read = sf_readf_float(file, interleaved.data(), info.frames);
	sf_close(file);
	*rate = info.samplerate;
	std::vector<SampleFrame> frames(static_cast<std::size_t>(read));
	for (std::size_t i = 0; i < frames.size(); ++i) { frames[i] = SampleFrame(interleaved[2 * i], interleaved[2 * i + 1]); }
	return frames;
}

QDomElement withAttribute(const QDomElement& element, const QString& attribute)
{
	if (element.hasAttribute(attribute)) { return element; }
	for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
	{
		const QDomElement found = withAttribute(child, attribute);
		if (!found.isNull()) { return found; }
	}
	return {};
}

//! The device's serialized element inside the track's own saved state (the project's save path).
QDomElement deviceState(Track* track)
{
	QString xml;
	if (!control::captureTrackXml(track, &xml, 1 << 20)) { return {}; }
	QDomDocument document;
	if (!document.setContent(xml)) { return {}; }
	return withAttribute(document.documentElement(), QStringLiteral("src"));
}

QString canonical(const QString& path)
{
	return QFileInfo(PathUtil::toAbsolute(path)).canonicalFilePath();
}

} // namespace

class SamplerTakeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test drives the periods itself
		NotePlayHandleManager::init();
		ControlRegistry::setReady(true);
		QVERIFY(m_dir.isValid());
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! AudioFileProcessor: bounded frames, a loaded and saved sample, playback, undo/redo, save/reopen.
	void audioFileProcessorTakesBoundedInputAndPlaysIt()
	{
		InstrumentTrack* track = samplerTrack(QStringLiteral("audiofileprocessor"));
		QVERIFY(track != nullptr);
		QCOMPARE(track->instrument()->nodeName().toLower(), QStringLiteral("audiofileprocessor"));

		QJsonObject take;
		QVERIFY(recordBounded(track, 0.1, &take));
		QCOMPARE(take.value(QStringLiteral("state")).toString(), QStringLiteral("done"));
		const auto target = framesOf(take);
		QCOMPARE(target, expectedFrames(take));
		QVERIFY(target > 0);
		QVERIFY(take.value(QStringLiteral("dropped")).toDouble() == 0.0);

		const QString path = m_dir.filePath(QStringLiteral("audiofileprocessor-take.wav"));
		const ControlResult landed = run(QStringLiteral("record.sampler_capture_land"), {{QStringLiteral("file"), path}});
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));
		QCOMPARE(static_cast<std::uint64_t>(landed.result.value(QStringLiteral("frames")).toDouble()), target);
		QCOMPARE(landed.result.value(QStringLiteral("device")).toString().toLower(), QStringLiteral("audiofileprocessor"));
		QVERIFY(QFileInfo::exists(path));

		QCOMPARE(canonical(deviceState(track).attribute(QStringLiteral("src"))), canonical(path));
		checkTakePcm(path, take);

		// The device plays the take a note renders.
		QVERIFY2(renders(track, track->baseNote()), "AudioFileProcessor rendered silence after landing the take");

		// One undo step puts the previous (empty) sample back; redo loads the take again.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(deviceState(track).attribute(QStringLiteral("src")).isEmpty());
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(canonical(deviceState(track).attribute(QStringLiteral("src"))), canonical(path));
		QVERIFY(renders(track, track->baseNote()));

		// Saved and reopened: the sample is still there and still plays.
		const QString id = control::trackIdOf(track);
		const QString project = m_dir.filePath(QStringLiteral("audiofileprocessor.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(project));
		Engine::getSong()->loadProject(project);
		ControlResult error;
		auto* reloaded = dynamic_cast<InstrumentTrack*>(control::resolveTrack(id, &error));
		QVERIFY2(reloaded != nullptr, qPrintable(error.errorMessage));
		QCOMPARE(canonical(deviceState(reloaded).attribute(QStringLiteral("src"))), canonical(path));
		QVERIFY(renders(reloaded, reloaded->baseNote()));
	}

	//! SlicerT: bounded frames, loaded waveform with slice points, a slice renders, undo/redo, save/reopen.
	void slicerTTakesBoundedInputAndSlicesIt()
	{
		InstrumentTrack* track = samplerTrack(QStringLiteral("slicert"));
		QVERIFY(track != nullptr);
		QCOMPARE(track->instrument()->nodeName().toLower(), QStringLiteral("slicert"));

		QJsonObject take;
		QVERIFY(recordBounded(track, 0.1, &take));
		QCOMPARE(take.value(QStringLiteral("state")).toString(), QStringLiteral("done"));
		const auto target = framesOf(take);
		QCOMPARE(target, expectedFrames(take));

		const QString path = m_dir.filePath(QStringLiteral("slicert-take.wav"));
		const ControlResult landed = run(QStringLiteral("record.sampler_capture_land"), {{QStringLiteral("file"), path}});
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));
		QCOMPARE(landed.result.value(QStringLiteral("device")).toString().toLower(), QStringLiteral("slicert"));
		checkTakePcm(path, take);

		QDomElement saved = deviceState(track);
		QCOMPARE(canonical(saved.attribute(QStringLiteral("src"))), canonical(path));
		const int slices = saved.attribute(QStringLiteral("totalSlices")).toInt();
		QVERIFY2(slices >= 2, qPrintable(QStringLiteral("SlicerT saved %1 slice points").arg(slices)));
		QCOMPARE(saved.attribute(QStringLiteral("slice_0")).toDouble(), 0.0);
		QCOMPARE(saved.attribute(QStringLiteral("slice_%1").arg(slices - 1)).toDouble(), 1.0);

		// A slice note (base + 1 is the first slice after the head) renders the take.
		QVERIFY2(renders(track, track->baseNote() + 1), "SlicerT rendered silence after landing the take");

		// One undo step restores the previous (empty) sample; redo loads the take and its slices again.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(deviceState(track).attribute(QStringLiteral("src")).isEmpty());
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(canonical(deviceState(track).attribute(QStringLiteral("src"))), canonical(path));
		QVERIFY(renders(track, track->baseNote() + 1));

		// Saved and reopened: the sample and SlicerT's own slice points survive.
		const QString id = control::trackIdOf(track);
		const QString project = m_dir.filePath(QStringLiteral("slicert.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(project));
		Engine::getSong()->loadProject(project);
		ControlResult error;
		auto* reloaded = dynamic_cast<InstrumentTrack*>(control::resolveTrack(id, &error));
		QVERIFY2(reloaded != nullptr, qPrintable(error.errorMessage));
		QDomElement reopened = deviceState(reloaded);
		QCOMPARE(canonical(reopened.attribute(QStringLiteral("src"))), canonical(path));
		QCOMPARE(reopened.attribute(QStringLiteral("totalSlices")).toInt(), slices);
		QVERIFY(renders(reloaded, reloaded->baseNote() + 1));
	}

	//! A stop ends the take early; refusals leave it in hand so it can still be landed.
	void stoppingEarlyKeepsTheFramesTakenAndRefusalsLeaveTheTake()
	{
		InstrumentTrack* track = samplerTrack(QStringLiteral("audiofileprocessor"));
		QVERIFY(track != nullptr);

		const ControlResult started = run(QStringLiteral("record.sampler_capture"),
			{{QStringLiteral("track"), control::trackIdOf(track)}, {QStringLiteral("seconds"), 10.0}});
		QVERIFY2(started.ok, qPrintable(started.errorMessage));
		QCOMPARE(started.result.value(QStringLiteral("state")).toString(), QStringLiteral("recording"));
		for (int i = 0; i < 6; ++i) { period(); }

		const ControlResult stopped = run(QStringLiteral("record.sampler_capture_stop"));
		QVERIFY2(stopped.ok, qPrintable(stopped.errorMessage));
		QCOMPARE(stopped.result.value(QStringLiteral("state")).toString(), QStringLiteral("done"));
		QVERIFY(stopped.result.value(QStringLiteral("stopped")).toBool());
		const auto frames = framesOf(stopped.result);
		QVERIFY(frames > 0);
		QVERIFY(frames < expectedFrames(stopped.result));

		// A second take is refused while this one waits to land.
		QCOMPARE(run(QStringLiteral("record.sampler_capture"),
			{{QStringLiteral("track"), control::trackIdOf(track)}, {QStringLiteral("seconds"), 0.1}}).errorKind,
			ControlErrorKind::Busy);
		// A relative landing path is refused without consuming the take.
		QCOMPARE(run(QStringLiteral("record.sampler_capture_land"), {{QStringLiteral("file"), QStringLiteral("relative.wav")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("record.sampler_capture_status")).result.value(QStringLiteral("state")).toString(),
			QStringLiteral("done"));

		const QString path = m_dir.filePath(QStringLiteral("stopped.wav"));
		const ControlResult landed = run(QStringLiteral("record.sampler_capture_land"), {{QStringLiteral("file"), path}});
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));
		int rate = 0;
		const std::vector<SampleFrame> wav = readWav(path, &rate);
		QCOMPARE(static_cast<std::uint64_t>(wav.size()), frames);

		// Cancel drops a take without changing the project; unsupported tracks are refused.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(run(QStringLiteral("record.sampler_capture"),
			{{QStringLiteral("track"), control::trackIdOf(track)}, {QStringLiteral("seconds"), 1.0}}).ok);
		const ControlResult cancelled = run(QStringLiteral("record.sampler_capture"),
			{{QStringLiteral("track"), control::trackIdOf(track)}, {QStringLiteral("cancel"), true}});
		QVERIFY2(cancelled.ok, qPrintable(cancelled.errorMessage));
		QCOMPARE(cancelled.result.value(QStringLiteral("state")).toString(), QStringLiteral("idle"));
		QVERIFY(cancelled.result.value(QStringLiteral("cancelled")).toBool());

		Engine::getSong()->clearProject();
		auto* plain = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(plain != nullptr);
		QVERIFY(!run(QStringLiteral("record.sampler_capture"),
			{{QStringLiteral("track"), control::trackIdOf(plain)}, {QStringLiteral("seconds"), 0.1}}).ok);
		QVERIFY(!run(QStringLiteral("record.sampler_capture"), {{QStringLiteral("track"), QStringLiteral("trk-999")}}).ok);
	}

	//! The agent surface states the commands and their contract classes.
	void theAgentSurfaceRegistersTheTakeCommands()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		const control::ReversibilityTable& table = control::ReversibilityTable::instance();
		const QStringList ids = {QStringLiteral("record.sampler_capture"),
			QStringLiteral("record.sampler_capture_stop"),
			QStringLiteral("record.sampler_capture_status"),
			QStringLiteral("record.sampler_capture_land")};
		for (const QString& id : ids)
		{
			const ControlCommand* command = registry->command(id);
			QVERIFY2(command != nullptr, qPrintable(id + " is not registered"));
			QVERIFY2(!command->argsSchema.isEmpty(), qPrintable(id + " has no args schema"));
			QVERIFY2(!command->resultSchema.isEmpty(), qPrintable(id + " has no result schema"));
			QVERIFY2(table.lookup(id) != nullptr, qPrintable(id + " has no reversibility row"));
		}
		QVERIFY(!registry->command(QStringLiteral("record.sampler_capture"))->mutating);
		QVERIFY(!registry->command(QStringLiteral("record.sampler_capture_stop"))->mutating);
		QVERIFY(!registry->command(QStringLiteral("record.sampler_capture_status"))->mutating);
		QVERIFY(registry->command(QStringLiteral("record.sampler_capture_land"))->mutating);
		QCOMPARE(table.lookup(QStringLiteral("record.sampler_capture_land"))->cls,
			control::ReversibilityClass::TrueInverse);
		QCOMPARE(table.lookup(QStringLiteral("record.sampler_capture_status"))->cls,
			control::ReversibilityClass::NotMutating);
	}

private:
	InstrumentTrack* samplerTrack(const QString& plugin)
	{
		Engine::getSong()->clearProject();
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		if (track == nullptr) { return nullptr; }
		Instrument* instrument = track->loadInstrument(plugin);
		if (instrument == nullptr || instrument->nodeName().toLower() != plugin) { return nullptr; }
		return track;
	}

	//! Starts a bounded take and drives the periods until it is done; returns its status.
	bool recordBounded(InstrumentTrack* track, double seconds, QJsonObject* status)
	{
		const ControlResult started = run(QStringLiteral("record.sampler_capture"),
			{{QStringLiteral("track"), control::trackIdOf(track)}, {QStringLiteral("seconds"), seconds}});
		if (!started.ok)
		{
			qWarning() << "record.sampler_capture:" << started.errorMessage;
			return false;
		}
		m_takeStartFed = m_fed;
		if (!recordUntilDone())
		{
			qWarning() << "the sampler take never reached its requested length";
			return false;
		}
		*status = run(QStringLiteral("record.sampler_capture_status")).result;
		return true;
	}

	QString takeState() const
	{
		return run(QStringLiteral("record.sampler_capture_status")).result.value(QStringLiteral("state")).toString();
	}

	bool recordUntilDone()
	{
		for (int i = 0; i < 2000 && takeState() != QStringLiteral("done"); ++i) { period(); }
		return takeState() == QStringLiteral("done");
	}

	//! One period: the next input frames are staged, then rendered.
	void period()
	{
		AudioEngine* engine = Engine::audioEngine();
		const f_cnt_t fpp = engine->framesPerPeriod();
		std::vector<SampleFrame> block(static_cast<std::size_t>(fpp));
		for (f_cnt_t f = 0; f < fpp; ++f) { block[static_cast<std::size_t>(f)] = inputFrame(m_fed + static_cast<std::uint64_t>(f)); }
		engine->pushInputFrames(block.data(), fpp);
		m_fed += static_cast<std::uint64_t>(fpp);
		engine->renderNextPeriod();
	}

	static std::uint64_t framesOf(const QJsonObject& status)
	{
		return static_cast<std::uint64_t>(status.value(QStringLiteral("frames")).toDouble());
	}

	static std::uint64_t expectedFrames(const QJsonObject& status)
	{
		return static_cast<std::uint64_t>(status.value(QStringLiteral("target_frames")).toDouble());
	}

	//! The WAV holds the take's own PCM: the input one recording round trip after the start.
	void checkTakePcm(const QString& path, const QJsonObject& status)
	{
		int rate = 0;
		const std::vector<SampleFrame> wav = readWav(path, &rate);
		QCOMPARE(static_cast<std::uint64_t>(wav.size()), framesOf(status));
		QCOMPARE(rate, status.value(QStringLiteral("sample_rate")).toInt());
		const auto latency = static_cast<std::uint64_t>(status.value(QStringLiteral("latency_frames")).toInt());
		const std::uint64_t first = m_takeStartFed + latency;
		for (std::size_t i = 0; i < wav.size(); ++i)
		{
			const SampleFrame expected = inputFrame(first + i);
			QVERIFY2(std::fabs(wav[i].left() - expected.left()) <= Pcm24Tolerance
					&& std::fabs(wav[i].right() - expected.right()) <= Pcm24Tolerance,
				qPrintable(QStringLiteral("take frame %1 is (%2, %3), the input fed was (%4, %5)").arg(i)
					.arg(wav[i].left()).arg(wav[i].right()).arg(expected.left()).arg(expected.right())));
		}
	}

	//! Whether a note on @a key through the instrument produces any non-silent frame.
	bool renders(InstrumentTrack* track, int key)
	{
		const f_cnt_t fpp = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> out(static_cast<std::size_t>(fpp));
		const Note note(TimePos(384), TimePos(0), key);
		NotePlayHandle* handle = NotePlayHandleManager::acquire(track, 0, fpp, note);
		if (handle == nullptr) { return false; }
		track->instrument()->playNote(handle, std::span<SampleFrame>{out});
		NotePlayHandleManager::release(handle);
		for (const SampleFrame& frame : out)
		{
			if (std::fabs(frame.left()) > 1e-6f || std::fabs(frame.right()) > 1e-6f) { return true; }
		}
		return false;
	}

	QTemporaryDir m_dir;
	std::uint64_t m_fed = 0;
	std::uint64_t m_takeStartFed = 0;
};

QTEST_GUILESS_MAIN(SamplerTakeTest)
#include "SamplerTakeTest.moc"
