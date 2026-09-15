/*
 * ControlCommandsMeter.cpp - the `meter.*` command group (SPEC A11-A16): the
 *                             BS.1770-4 loudness and true-peak surface.
 *
 * THE ITEM THIS CLOSES. LUFS / loudness metering is row 24 of
 * docs/FEATURE-LIST-0.3.0.md and one of the audit's sixteen "in the tree but not
 * drivable through the socket" features: the measurement core
 * (include/LufsMeter.h, the ITU-R BS.1770-4 / EBU R128 meter) and its OFFLINE
 * consumer (include/LoudnessReport.h, which the render path feeds and the export
 * dialog reports) were already merged and proven by LufsMeterTest,
 * LoudnessReportTest and MasteringTest - but no `lufs.` or `meter.` id existed,
 * so nothing an agent could send measured anything.
 *
 * WHAT THIS FILE REGISTERS, and what it deliberately does not:
 *
 *  * `meter.get_state`    - the LIVE master readout: the passive tap's LUFS-I,
 *                           LUFS-M, LUFS-S, loudest 3 s window and true peak,
 *                           plus whether the tap is armed and how many audio
 *                           periods it has measured.
 *  * `meter.arm`          - arms or disarms that tap. Arming STARTS a
 *                           measurement; disarming keeps the last reading
 *                           readable. The tap is the engine's
 *                           (include/MasterLoudnessTap.h), fed one period per
 *                           rendered period out of AudioEngine::renderStageMix().
 *  * `meter.measure_file` - the same five numbers for a RENDERED FILE, measured
 *                           now from the file's own bytes with the EBU R 128
 *                           verdict. Its reader is
 *                           src/core/ControlCommandsMeterFile.cpp, which is a
 *                           separate translation unit for the file ratchet and
 *                           shares this group's JSON vocabulary through
 *                           include/ControlMeterSupport.h.
 *
 *  * There is NO new DSP in this group and no second meter. Every loudness number
 *    comes out of the SAME `LufsMeter` the merged engine uses: the live half
 *    through `MasterLoudnessTap` (one meter) and the file half through
 *    `LoudnessReport` (one meter, and the exact object the render path feeds).
 *  * Nothing here writes audio, starts a render or touches a project file. There
 *    is no render verb in this group: render.render and mastering.run own
 *    rendering, and a second one would be a second truth.
 *
 * A16, in one line each, with the whole reasoning in
 * ControlReversibilityTableMeter.cpp:
 *   meter.get_state     not_mutating  - reads the tap's published snapshot
 *   meter.arm           true_inverse  - an action checkpoint restores the flag
 *   meter.measure_file  not_mutating  - reads a file and hashes it
 *   (the feature's fourth row, export.set_loudness_report, lives in the export
 *   group's own file: src/core/ControlCommandsExport.cpp)
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

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "ControlEdit.h"
#include "ControlMeterSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "Engine.h"
#include "LufsMeter.h"
#include "MasterLoudnessTap.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The engine's tap, or nullptr when this instance has no engine to measure (a
//! headless process that never built one, a command issued before startup).
MasterLoudnessTap* engineTap()
{
	AudioEngine* engine = Engine::audioEngine();
	return engine != nullptr ? engine->masterLoudness() : nullptr;
}

//! Whether an audio device is up and running right now: the live tap is only fed
//! by audio periods, so this is the fact that explains a still readout.
bool engineRunning()
{
	AudioEngine* engine = Engine::audioEngine();
	AudioDevice* device = engine != nullptr ? engine->audioDev() : nullptr;
	return device != nullptr && device->isRunning();
}

/*! The live half's payload: the tap's snapshot, under the same five key names a
 *  measured file uses, plus the facts that make the numbers interpretable.
 */
QJsonObject liveJson(const MasterLoudnessTap& tap)
{
	const MasterLoudnessTap::Snapshot live = tap.snapshot();

	QJsonObject out;
	out.insert(QStringLiteral("enabled"), live.enabled);
	out.insert(QStringLiteral("sample_rate"), static_cast<int>(live.sampleRate));
	out.insert(QStringLiteral("channels"), static_cast<int>(live.channels));
	out.insert(QStringLiteral("blocks_fed"), static_cast<qint64>(live.blocksFed));
	out.insert(QStringLiteral("frames_fed"), static_cast<qint64>(live.framesFed));
	out.insert(QStringLiteral("seconds_fed"),
		static_cast<double>(live.framesFed) / static_cast<double>(live.sampleRate));
	out.insert(QStringLiteral("engine_running"), engineRunning());

	QJsonObject readings = meterReadingsJson(live.integratedLufs, live.momentaryLufs,
		live.shortTermLufs, live.shortTermMaxLufs, live.truePeakDbtp);
	for (auto it = readings.constBegin(); it != readings.constEnd(); ++it)
	{
		out.insert(it.key(), it.value());
	}
	return out;
}

//! The whole `meter.get_state` payload (and the result half of meter.arm, so a
//! caller reads back what it asked for without a second round trip).
QJsonObject liveStateJson(const MasterLoudnessTap& tap)
{
	QJsonObject result;
	result.insert(QStringLiteral("live"), liveJson(tap));
	result.insert(QStringLiteral("target"), meterTargetJson());
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

ControlResult handleGetState()
{
	const MasterLoudnessTap* tap = engineTap();
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
 *  description); arming an armed one is idempotent, so a caller cannot discard a
 *  running measurement by re-sending the same command.
 */
ControlResult handleArm(const QJsonObject& args)
{
	MasterLoudnessTap* tap = engineTap();
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
	// behind it, and its inverse is one bounded value, so it becomes ONE recorded
	// action step on the engine's own undo stack - the mechanism clock.master_set
	// and export.set_dither use for the same shape. What the step does NOT restore
	// is a measurement: the readings are surface memory (neither project state nor
	// saved with a project), and arming starts a new one. The transaction says so
	// rather than implying a restored readout.
	control::addUndoStep(
		[previous]() {
			MasterLoudnessTap* t = engineTap();
			if (t != nullptr) { t->setEnabled(previous); }
		},
		[requested]() {
			MasterLoudnessTap* t = engineTap();
			if (t != nullptr) { t->setEnabled(requested); }
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

ControlResult handleMeasureFile(const QJsonObject& args)
{
	// The reader and the whole payload are in ControlCommandsMeterFile.cpp; the
	// handler is the one line that hands it the path, so this file stays the
	// group's registration and its live half.
	return meterMeasureFile(args.value(QStringLiteral("path")).toString());
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
			"ARMING STARTS A MEASUREMENT, EVERY TIME - including a re-arm of an already-armed tap: "
			"the accumulated integrated loudness, short-term maximum and true peak are dropped and "
			"everything measured from now on is the new reading, so 'arm, play the section, read "
			"meter.get_state' always reports THAT section and two measured sections can never be "
			"silently averaged into one number. A caller that wants the running measurement to "
			"continue simply does not re-arm it. DISARMING keeps the last reading readable, so a "
			"caller can stop measuring and still see the result. Safe to send while the transport "
			"is running: the tap allocates nothing, locks nothing and only reads the master mix, so "
			"the audio is bit-for-bit unchanged. Reversible: control.undo restores the armed flag "
			"(the readings themselves are surface memory and are not part of the inverse).");
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
		cmd.handler = [](const QJsonObject& args) { return handleMeasureFile(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
