/*
 * ControlCommandsMeter.cpp - the `meter.*` command group (SPEC A11-A16): the
 *                             BS.1770-4 loudness and true-peak surface, live and
 *                             offline.
 *
 * THE ITEM THIS CLOSES. LUFS / loudness metering is row 24 of
 * docs/FEATURE-LIST-0.3.0.md and one of the audit's sixteen "in the tree but not
 * drivable through the socket" features: the measurement core (include/LufsMeter.h,
 * the ITU-R BS.1770-4 / EBU R128 meter) and its OFFLINE consumer
 * (include/LoudnessReport.h, which the render path feeds and the export dialog
 * reports) were already merged and proven by LufsMeterTest, LoudnessReportTest
 * and MasteringTest - but no `lufs.` or `meter.` id existed, so nothing an agent
 * could send measured anything, and export.get_settings did not expose the
 * render-path report either.
 *
 * WHAT THIS FILE REGISTERS, and what it deliberately does not:
 *
 *  * `meter.get_state`    - the LIVE master readout: the passive tap's LUFS-I,
 *                           LUFS-M, LUFS-S, the loudest 3 s window and true
 *                           peak, plus whether the tap is armed and how many
 *                           periods it has been fed.
 *  * `meter.arm`          - arms or disarms that tap. Arming STARTS a
 *                           measurement; disarming keeps the last reading
 *                           readable. The tap itself is the engine's
 *                           (include/MasterLoudnessTap.h), fed one period per
 *                           rendered period out of AudioEngine::renderStageMix().
 *  * `meter.measure_file` - the same five numbers for a RENDERED FILE, measured
 *                           now, from the file's own bytes, with the EBU R128
 *                           verdict the render path already grades against.
 *
 *  * There is NO new DSP in this file and no second meter. Every loudness number
 *    this group publishes comes out of the SAME `LufsMeter` the merged engine
 *    uses: the live half through `MasterLoudnessTap` (which owns one meter) and
 *    the file half through `LoudnessReport` (which owns one meter, and is the
 *    exact object the render path feeds). This file converts floats to JSON and
 *    does nothing else with the signal.
 *  * Nothing here writes audio, starts a render or touches a project file:
 *    measure_file READS the file it is given and the live tap READS the master
 *    mix. There is no render verb in this group - render.render and
 *    mastering.run already own rendering, and a second one would be a second
 *    truth.
 *
 * THE SENTINEL, and why the JSON carries null. `LufsMeter` reports
 * `-std::numeric_limits<float>::infinity()` (LufsMeter::MinusInfinity) while a
 * value has no measurement - silence, or a window that is not full yet. That
 * sentinel is NOT a number a caller should have to compare against a magic
 * float, and it cannot travel in JSON (which has no infinity), so every reading
 * below is `null` when it is not finite and a number otherwise - the convention
 * MasteringReport.cpp already uses for the same values. A silent render or a
 * silent master therefore reads `null`, never `-70` and never `-inf`.
 *
 * A16, in one line each, with the whole reasoning in
 * ControlReversibilityTableMeter.cpp:
 *   meter.get_state     not_mutating  - reads the tap's published snapshot
 *   meter.arm           true_inverse  - an action checkpoint restores the flag
 *   meter.measure_file  not_mutating  - reads a file and hashes it
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <cmath>
#include <memory>
#include <vector>

#include <sndfile.h>

#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "ControlEdit.h"
#include "ControlMasteringSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"

#include "AudioEngine.h"
#include "AudioDevice.h"
#include "Engine.h"
#include "LoudnessReport.h"
#include "LufsMeter.h"
#include "MasterLoudnessTap.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! Frames per read for the file half. Large enough that a 5-minute render is a
//! few thousand reads, small enough that the working set is a few dozen KB.
constexpr int FileChunkFrames = 4096;

//! The layouts the file half measures: LufsMeter's Table 3 weights are defined
//! for 1, 2 and 6 channels and every other count is measured with every channel
//! at 1.0, which the header documents - but a file with more than six channels
//! cannot be fed to a six-channel meter at all, so it is refused, typed.
constexpr int MaxMeasuredChannels = LufsMeter::MaxChannels;

/*! A loudness or peak value as JSON: null when the meter has no measurement
 *  (its -infinity sentinel, whatever the reason - silence, or a window that has
 *  not filled), the number otherwise. The same rule MasteringReport.cpp applies
 *  to the same values, so a caller parses one convention across this release.
 */
QJsonValue readingJson(float value)
{
	if (!std::isfinite(value)) { return QJsonValue(QJsonValue::Null); }
	return QJsonValue(static_cast<double>(value));
}

/*! The five numbers, under the names both halves of this group use. One
 *  function, so the live readout and a measured file can never drift into two
 *  spellings of "integrated_lufs".
 */
QJsonObject readingsJson(const LufsMeter::Reading& reading, float shortTermMaxLufs)
{
	QJsonObject out;
	out.insert(QStringLiteral("integrated_lufs"), readingJson(reading.integratedLufs));
	out.insert(QStringLiteral("momentary_lufs"), readingJson(reading.momentaryLufs));
	out.insert(QStringLiteral("short_term_lufs"), readingJson(reading.shortTermLufs));
	out.insert(QStringLiteral("short_term_max_lufs"), readingJson(shortTermMaxLufs));
	out.insert(QStringLiteral("true_peak_dbtp"), readingJson(reading.truePeakDbtp));
	return out;
}

/*! The target the report is graded against, published rather than implied: EBU
 *  R 128's delivery guidance over the BS.1770-4 measurement (EBU Tech 3343) is
 *  -23.0 LUFS-I +/- 0.5 LU with a -1.0 dBTP ceiling, and the streaming
 *  services' -14 LUFS-I is carried beside it as a CONVENTION, not a
 *  specification - it has no published tolerance to grade against, which is why
 *  nothing here grades against it. The numbers are read from
 *  LoudnessReport's own constants, not restated.
 */
QJsonObject targetJson()
{
	QJsonObject out;
	out.insert(QStringLiteral("standard"), QStringLiteral("EBU R 128 (EBU Tech 3343)"));
	out.insert(QStringLiteral("integrated_lufs"),
		static_cast<double>(LoudnessReport::EbuR128TargetLufs));
	out.insert(QStringLiteral("tolerance_lu"),
		static_cast<double>(LoudnessReport::EbuR128ToleranceLu));
	out.insert(QStringLiteral("true_peak_ceiling_dbtp"),
		static_cast<double>(LoudnessReport::EbuR128TruePeakCeilingDbtp));
	out.insert(QStringLiteral("streaming_reference_lufs"),
		static_cast<double>(LoudnessReport::StreamingReferenceLufs));
	return out;
}

//! Whether an audio device is up and running right now: the live tap is only
//! fed by audio periods, so this is the fact that explains a still readout.
bool engineRunning()
{
	const AudioEngine* engine = Engine::audioEngine();
	const AudioDevice* device = engine != nullptr ? engine->audioDev() : nullptr;
	return device != nullptr && device->isRunning();
}

//! The live half's payload: the tap's snapshot, in the same key names as a
//! measured file, plus the facts that make the numbers interpretable.
QJsonObject liveJson(const MasterLoudnessTap& tap)
{
	const MasterLoudnessTap::Snapshot live = tap.snapshot();

	LufsMeter::Reading reading;
	reading.integratedLufs = live.integratedLufs;
	reading.momentaryLufs = live.momentaryLufs;
	reading.shortTermLufs = live.shortTermLufs;
	reading.truePeakDbtp = live.truePeakDbtp;

	QJsonObject out;
	out.insert(QStringLiteral("enabled"), live.enabled);
	out.insert(QStringLiteral("sample_rate"), static_cast<int>(live.sampleRate));
	out.insert(QStringLiteral("channels"), static_cast<int>(live.channels));
	out.insert(QStringLiteral("blocks_fed"), static_cast<qint64>(live.blocksFed));
	out.insert(QStringLiteral("frames_fed"), static_cast<qint64>(live.framesFed));
	out.insert(QStringLiteral("seconds_fed"),
		static_cast<double>(live.framesFed) / static_cast<double>(live.sampleRate));
	out.insert(QStringLiteral("engine_running"), engineRunning());
	QJsonObject readings = readingsJson(reading, live.shortTermMaxLufs);
	for (auto it = readings.constBegin(); it != readings.constEnd(); ++it)
	{
		out.insert(it.key(), it.value());
	}
	return out;
}

/*! The whole `meter.get_state` payload. \a tap is the engine's tap; the caller
 *  has already checked it exists.
 */
QJsonObject liveStateJson(const MasterLoudnessTap& tap)
{
	QJsonObject result;
	result.insert(QStringLiteral("live"), liveJson(tap));
	result.insert(QStringLiteral("target"), targetJson());
	result.insert(QStringLiteral("note"),
		QStringLiteral("`live` is the engine's PASSIVE tap on the master mix: one period in, one "
			"reading out, read-only (nothing in this path writes, copies or delays a sample), so a "
			"render or a playback is bit-for-bit what it was with the tap disarmed. It is DISARMED "
			"by default - arm it with meter.arm, and note that ARMING STARTS A MEASUREMENT, so the "
			"integrated loudness and true peak it reports are those of everything played since the "
			"last arm. A reading is null, never a plausible number, while the meter has no "
			"measurement (silence, or a window that has not filled); `blocks_fed` counts the periods "
			"the tap has measured, which is how a caller tells 'nothing has sounded yet' from 'the "
			"tap is not running', and `engine_running` is whether an audio device is up at all. "
			"Measured with the engine's own BS.1770-4 meter (LufsMeter); `target` is the standard "
			"this release grades renders against, for reference - this command grades nothing."));
	return result;
}

/*! The file half: measure \a path now, from its own bytes.
 *
 *  Reads the file in bounded chunks and feeds the SAME `LoudnessReport` the
 *  render path feeds (which owns a `LufsMeter`). Interleaved for one and two
 *  channels, planar above that, because that is what the meter's two entry
 *  points are exact for: `processBlock` measures channels 0 and 1, and a mono
 *  file fed that way would read 3 LU loud (one channel weighted twice) while a
 *  5.1 file needs its six channels weighted separately.
 *
 *  Refusals are typed and leave nothing behind: a missing path, a relative
 *  path, a file libsndfile cannot read (with the library's own words), a
 *  channel count no meter can weigh, and a file with no frames at all.
 */
ControlResult measureFile(const QJsonObject& args)
{
	const QString path = args.value(QStringLiteral("path")).toString();
	if (path.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'path' is required: the absolute path of the rendered file to measure"));
	}
	if (!path.startsWith(QLatin1Char('/')))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'path' must be an absolute path"));
	}
	const QFileInfo info(path);
	if (!info.exists() || !info.isFile())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("no such file: %1").arg(path));
	}

	SF_INFO fileInfo{};
	SNDFILE* file = sf_open(QFile::encodeName(path).constData(), SFM_READ, &fileInfo);
	if (file == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("cannot read %1: %2").arg(path, QString::fromUtf8(sf_strerror(nullptr))));
	}
	// The handle closes on every path out of this function, including the
	// refusals below.
	const std::unique_ptr<SNDFILE, int (*)(SNDFILE*)> handle(file, sf_close);

	if (fileInfo.channels < 1 || fileInfo.channels > MaxMeasuredChannels)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 has %2 channels; this meter measures 1 to %3 (BS.1770-4 Table 3 "
				"weighs mono, stereo and 5.1 exactly)").arg(path)
				.arg(fileInfo.channels).arg(MaxMeasuredChannels));
	}
	if (fileInfo.frames <= 0 || fileInfo.samplerate <= 0)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 holds no frames to measure").arg(path));
	}

	const auto channels = static_cast<ch_cnt_t>(fileInfo.channels);
	LoudnessReport report(static_cast<sample_rate_t>(fileInfo.samplerate), channels);

	// Two fixed-size buffers, reused for every chunk: the interleaved one for
	// the one- and two-channel path (SampleFrame IS two floats, which is exactly
	// libsndfile's own layout) and the planar one for the wider layouts.
	std::vector<SampleFrame> frames(FileChunkFrames);
	std::vector<sample_t> raw(static_cast<std::size_t>(FileChunkFrames) * channels);
	std::vector<std::vector<sample_t>> planar(channels, std::vector<sample_t>(FileChunkFrames));
	std::vector<const sample_t*> planarPointers(channels);

	sf_count_t remaining = fileInfo.frames;
	while (remaining > 0)
	{
		const sf_count_t want = std::min<sf_count_t>(remaining, FileChunkFrames);
		const sf_count_t got = channels <= DEFAULT_CHANNELS
			? sf_readf_float(file, reinterpret_cast<float*>(frames.data()), want)
			: sf_readf_float(file, raw.data(), want);
		if (got <= 0) { break; }

		if (channels <= DEFAULT_CHANNELS)
		{
			report.addBlock(frames.data(), static_cast<f_cnt_t>(got));
		}
		else
		{
			for (ch_cnt_t channel = 0; channel < channels; ++channel)
			{
				for (sf_count_t frame = 0; frame < got; ++frame)
				{
					planar[channel][static_cast<std::size_t>(frame)] =
						raw[static_cast<std::size_t>(frame) * channels + channel];
				}
				planarPointers[channel] = planar[channel].data();
			}
			report.addPlanarBlock(planarPointers.data(), channels, static_cast<f_cnt_t>(got));
		}
		remaining -= got;
	}

	const LoudnessReport::Verdict verdict = report.verdict();

	QJsonObject result;
	// The live shape's five numbers, from the report's own meter.
	result.insert(QStringLiteral("path"), path);
	result.insert(QStringLiteral("bytes"), static_cast<qint64>(info.size()));
	result.insert(QStringLiteral("source"), control::masteringFileFacts({path}));
	result.insert(QStringLiteral("sample_rate"), fileInfo.samplerate);
	result.insert(QStringLiteral("channels"), fileInfo.channels);
	result.insert(QStringLiteral("frames"), static_cast<qint64>(fileInfo.frames));
	result.insert(QStringLiteral("duration_seconds"),
		static_cast<double>(fileInfo.frames) / static_cast<double>(fileInfo.samplerate));
	// libsndfile's own container code (SF_FORMAT_WAV & SF_FORMAT_TYPEMASK, ...).
	// Reported as the library's value rather than as a name this file would have
	// to keep a second table for: an agent that wants "wav" reads it here as the
	// code libsndfile's own header documents.
	result.insert(QStringLiteral("format_major"),
		static_cast<int>(fileInfo.format & SF_FORMAT_TYPEMASK));
	QJsonObject readings = readingsJson(report.reading(), report.shortTermMaxLufs());
	for (auto it = readings.constBegin(); it != readings.constEnd(); ++it)
	{
		result.insert(it.key(), it.value());
	}
	result.insert(QStringLiteral("measured"), verdict.measured);
	result.insert(QStringLiteral("deviation_lu"),
		verdict.measured ? QJsonValue(static_cast<double>(verdict.deviationLu))
			: QJsonValue(QJsonValue::Null));
	result.insert(QStringLiteral("loudness_pass"), verdict.loudnessOk);
	result.insert(QStringLiteral("true_peak_pass"), verdict.truePeakOk);
	result.insert(QStringLiteral("verdict"), LoudnessReport::verdictName(verdict));
	result.insert(QStringLiteral("target"), targetJson());
	result.insert(QStringLiteral("note"),
		QStringLiteral("Measured from the file's own bytes, now, by the same BS.1770-4 meter the "
			"render path uses (LoudnessReport, which owns a LufsMeter); the file is only READ - its "
			"sha256 is in `source`, so a caller can check the file is byte-identical before and "
			"after measuring it. A silent file reports null for every reading and `measured` false "
			"with verdict NOT MEASURED rather than a plausible-looking number. `verdict` grades the "
			"integrated value against EBU R 128's -23.0 LUFS-I +/- 0.5 LU and the true peak against "
			"its -1.0 dBTP ceiling; those are the ONLY tolerances this release grades against (see "
			"`target`)."));
	return ControlResult::success(result);
}

ControlResult handleGetState()
{
	const AudioEngine* engine = Engine::audioEngine();
	const MasterLoudnessTap* tap = engine != nullptr ? engine->masterLoudness() : nullptr;
	if (tap == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("this instance has no audio engine, so there is no master to measure"));
	}
	QJsonObject result = liveStateJson(*tap);
	result.insert(QStringLiteral("armed"), tap->enabled());
	return ControlResult::success(result);
}

/*! `meter.arm`: enable or disable the live tap. Arming a disarmed tap starts a
 *  FRESH measurement (documented on the class and in the command's own
 *  description); arming an armed one is idempotent so a caller cannot discard a
 *  measurement by re-sending the same command.
 */
ControlResult handleArm(const QJsonObject& args)
{
	AudioEngine* engine = Engine::audioEngine();
	MasterLoudnessTap* tap = engine != nullptr ? engine->masterLoudness() : nullptr;
	if (tap == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("this instance has no audio engine, so there is no master to measure"));
	}

	const bool requested = args.value(QStringLiteral("enabled")).toBool();
	const bool previous = tap->enabled();
	tap->setEnabled(requested);
	const bool applied = tap->enabled();

	// SPEC A16: the tap's armed flag is engine state with no JournallingObject
	// behind it, and its inverse is one bounded value, so it becomes ONE
	// recorded action step on the engine's own undo stack - the same mechanism
	// clock.master_set and export.set_dither use. What the step does NOT restore
	// is a measurement: the readings are surface memory (they are neither
	// project state nor saved with a project), and arming starts a new one. The
	// transaction says so rather than implying a restored readout.
	control::addUndoStep(
		[previous]() {
			AudioEngine* e = Engine::audioEngine();
			if (e != nullptr && e->masterLoudness() != nullptr) { e->masterLoudness()->setEnabled(previous); }
		},
		[requested]() {
			AudioEngine* e = Engine::audioEngine();
			if (e != nullptr && e->masterLoudness() != nullptr) { e->masterLoudness()->setEnabled(requested); }
		});

	QJsonObject result = liveStateJson(*tap);
	result.insert(QStringLiteral("armed"), applied);
	result.insert(QStringLiteral("previous"), previous);

	QJsonObject transaction;
	transaction.insert(QStringLiteral("before"),
		QJsonObject{{QStringLiteral("armed"), previous}});
	transaction.insert(QStringLiteral("inverse"),
		QJsonObject{{QStringLiteral("op"), QStringLiteral("meter.arm")},
			{QStringLiteral("args"), QJsonObject{{QStringLiteral("enabled"), previous}}}});
	transaction.insert(QStringLiteral("reversible"), true);
	transaction.insert(QStringLiteral("mechanism"),
		QStringLiteral("action checkpoint: the recorded undo step calls the tap's setEnabled() with "
			"the flag the before-state holds, so control.undo disarms an arm and re-arms a disarm. "
			"A measurement is NOT part of the inverse - the readings are surface memory, and arming "
			"starts a fresh one"));
	result.insert(QStringLiteral("__transaction"), transaction);
	return ControlResult::success(result);
}

} // namespace

void registerMeterCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("meter.get_state");
		cmd.group = QStringLiteral("meter");
		cmd.verb = QStringLiteral("get_state");
		cmd.description = QStringLiteral("The LIVE loudness of the master mix: gated integrated "
			"loudness (LUFS-I), momentary (LUFS-M, last 400 ms), short-term (LUFS-S, last 3 s), the "
			"loudest short-term window since the tap was armed, and true peak (dBTP) - the "
			"ITU-R BS.1770-4 / EBU R128 measures - measured by the engine's own meter from the "
			"periods it is rendering, plus whether the tap is armed and how many periods it has "
			"measured. Reads only; the tap is PASSIVE, so it never changes a sample. A reading is "
			"null (never a plausible number) while nothing measurable has been fed - which is also "
			"the state before the tap is armed: see meter.arm.");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("armed"), booleanProperty()},
			{QStringLiteral("live"), objectProperty()},
			{QStringLiteral("target"), objectProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.mutating = false;
		cmd.handler = [](const QJsonObject&) { return handleGetState(); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("meter.arm");
		cmd.group = QStringLiteral("meter");
		cmd.verb = QStringLiteral("arm");
		cmd.description = QStringLiteral("Arm or disarm the PASSIVE loudness tap on the master mix. "
			"ARMING STARTS A MEASUREMENT: the accumulated integrated loudness and true peak are "
			"dropped and everything measured from now on is the new reading, so 'arm, play the "
			"section, read meter.get_state' reports that section. Arming an already-armed tap is a "
			"no-op (it does not discard the running measurement); DISARMING keeps the last reading "
			"readable, so a caller can stop measuring and still see the result. Safe to send while "
			"the transport is running: the tap allocates nothing, locks nothing and only reads the "
			"master mix, so the audio is bit-for-bit unchanged. Reversible: control.undo restores "
			"the armed flag (the readings themselves are surface memory and are not part of the "
			"inverse).");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("enabled"), booleanProperty()},
		}, {QStringLiteral("enabled")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("armed"), booleanProperty()},
			{QStringLiteral("previous"), booleanProperty()},
			{QStringLiteral("live"), objectProperty()},
			{QStringLiteral("target"), objectProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return handleArm(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("meter.measure_file");
		cmd.group = QStringLiteral("meter");
		cmd.verb = QStringLiteral("measure_file");
		cmd.description = QStringLiteral("Measure a RENDERED FILE's loudness and true peak, now, "
			"from the file's own bytes: gated integrated loudness (LUFS-I), momentary, short-term "
			"and the loudest short-term window, true peak in dBTP, the EBU R128 verdict, and the "
			"file's own facts (sample rate, channels, frames, duration, size and sha256). The same "
			"BS.1770-4 meter the render path uses measures it, in bounded chunks, reading the file "
			"and writing nothing - so a caller can hash the file before and after and see it "
			"unchanged. 1 to 6 channels (mono and stereo exactly; 5.1 with BS.1770-4's channel "
			"weights); a file with more, or one with no frames, is refused typed. A silent file "
			"reports null readings, measured false and verdict NOT MEASURED.");
		cmd.argsSchema = objectSchema({
			{QStringLiteral("path"), stringProperty()},
		}, {QStringLiteral("path")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("path"), stringProperty()},
			{QStringLiteral("bytes"), integerProperty()},
			{QStringLiteral("source"), arrayProperty()},
			{QStringLiteral("sample_rate"), integerProperty()},
			{QStringLiteral("channels"), integerProperty()},
			{QStringLiteral("frames"), integerProperty()},
			{QStringLiteral("duration_seconds"), numberProperty()},
			{QStringLiteral("format_major"), integerProperty()},
			{QStringLiteral("integrated_lufs"), numberProperty()},
			{QStringLiteral("momentary_lufs"), numberProperty()},
			{QStringLiteral("short_term_lufs"), numberProperty()},
			{QStringLiteral("short_term_max_lufs"), numberProperty()},
			{QStringLiteral("true_peak_dbtp"), numberProperty()},
			{QStringLiteral("measured"), booleanProperty()},
			{QStringLiteral("deviation_lu"), numberProperty()},
			{QStringLiteral("loudness_pass"), booleanProperty()},
			{QStringLiteral("true_peak_pass"), booleanProperty()},
			{QStringLiteral("verdict"), stringProperty()},
			{QStringLiteral("target"), objectProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.mutating = false;
		cmd.handler = [](const QJsonObject& args) { return measureFile(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
