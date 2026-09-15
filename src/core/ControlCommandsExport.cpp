/*
 * ControlCommandsExport.cpp - the export.* (render settings) command group
 *                              (SPEC A11-A16).
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
 */

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "ExportDither.h"
#include "ExportRenderSettings.h"
#include "SrcQuality.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The four quality names, in the order the enum declares them, as the result
//! of export.get_settings reports them. Built from srcQualityName() so the
//! choice list and the parser cannot drift apart.
QJsonArray srcQualityChoices()
{
	QJsonArray choices;
	for (int i = 0; i <= static_cast<int>(SrcQuality::SincBest); ++i)
	{
		choices.append(QString::fromLatin1(srcQualityName(static_cast<SrcQuality>(i))));
	}
	return choices;
}

QString qualityWireName(SrcQuality quality)
{
	return QString::fromLatin1(srcQualityName(quality));
}

//! export.get_settings' payload, also used as the result half of the two
//! setters so a caller sees the new state without a second round trip.
QJsonObject exportSettingsJson()
{
	QJsonObject result;
	result.insert(QStringLiteral("dither"), ExportRenderSettings::dither());
	result.insert(QStringLiteral("src_quality"), qualityWireName(ExportRenderSettings::srcQuality()));
	result.insert(QStringLiteral("src_quality_choices"), srcQualityChoices());
	// Feature row 24: the render path's EBU R128 loudness report, which used to
	// be reachable only from the export dialog's checkbox and the CLI's
	// `--loudness-report` flag - the second half of the audit's complaint about
	// this feature. Read from the same process-wide holder the other two are.
	result.insert(QStringLiteral("loudness_report"), ExportRenderSettings::loudnessReport());
	return result;
}

void registerExportGetSettings(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("export.get_settings");
	cmd.group = QStringLiteral("export");
	cmd.verb = QStringLiteral("get_settings");
	cmd.description = QStringLiteral("Read the render settings that outlive one OutputSettings: whether "
		"the next export dithers, which sample-rate-conversion quality it resamples with, and "
		"whether it writes an EBU R128 loudness report beside its output. All three are "
		"OFF/DEFAULT unless something asked for otherwise - dither is off by default because "
		"this release's render-reproducibility claim depends on it, 'linear' is the converter "
		"every render the engine has produced so far used, and the loudness report is opt-in "
		"because it is a measurement, not a render choice. Read-only: no transaction is "
		"recorded. There is no interface for the first two; drive them through export.set_* "
		"(docs/KNOWN-LIMITATIONS.md). The loudness report has an interface - the export dialog's "
		"checkbox - and export.set_loudness_report drives the same value from here.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("dither"), booleanProperty()},
		{QStringLiteral("src_quality"), stringProperty()},
		{QStringLiteral("src_quality_choices"), arrayProperty()},
		{QStringLiteral("loudness_report"), booleanProperty()},
	});
	// A read of a process-wide value: it answers before an engine exists, which
	// is when an agent most needs to know what the next render will do.
	cmd.mutating = false;
	cmd.requiresEngine = false;
	cmd.handler = [](const QJsonObject&) {
		return ControlResult::success(exportSettingsJson());
	};
	registry.registerCommand(cmd);
}

void registerExportSetDither(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("export.set_dither");
	cmd.group = QStringLiteral("export");
	cmd.verb = QStringLiteral("set_dither");
	cmd.description = QStringLiteral("Turn TPDF dither for the integer export formats on or off. The "
		"dither is applied by the WAV encoder immediately before quantisation, at the depth "
		"actually written (16- and 24-bit; 32-bit float has no quantisation step and is left "
		"alone). It is DETERMINISTIC - seeded from a constant - so a dithered render is still "
		"reproducible and two runs produce identical files. Off by default; turning it on "
		"changes the bytes of every subsequent render, which is the point.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("dither"), booleanProperty()},
	}, {QStringLiteral("dither")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("dither"), booleanProperty()},
		{QStringLiteral("previous"), booleanProperty()},
		{QStringLiteral("src_quality"), stringProperty()},
		{QStringLiteral("src_quality_choices"), arrayProperty()},
		{QStringLiteral("loudness_report"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		const bool previous = ExportRenderSettings::dither();
		const bool requested = args.value(QStringLiteral("dither")).toBool();

		// SPEC A16: the selection is a bounded scalar owned by a subsystem the
		// engine does not journal, so the inverse is a recorded undo STEP on the
		// engine's own stack - exactly the mechanism settings.set uses, and the
		// reason the class below is true_inverse rather than "not reversible".
		control::addUndoStep(
			[previous]() { ExportRenderSettings::setDither(previous); },
			[requested]() { ExportRenderSettings::setDither(requested); });
		ExportRenderSettings::setDither(requested);

		QJsonObject result = exportSettingsJson();
		result.insert(QStringLiteral("previous"), previous);

		QJsonObject transaction;
		transaction.insert(QStringLiteral("before"),
			QJsonObject{{QStringLiteral("dither"), previous}});
		transaction.insert(QStringLiteral("inverse"),
			QJsonObject{{QStringLiteral("op"), QStringLiteral("export.set_dither")},
				{QStringLiteral("args"),
					QJsonObject{{QStringLiteral("dither"), previous}}}});
		transaction.insert(QStringLiteral("reversible"), true);
		transaction.insert(QStringLiteral("mechanism"),
			QStringLiteral("action checkpoint: the recorded undo step restores the previous "
				"value, exactly as this command sets the new one"));
		result.insert(QStringLiteral("__transaction"), transaction);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerExportSetSrcQuality(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("export.set_src_quality");
	cmd.group = QStringLiteral("export");
	cmd.verb = QStringLiteral("set_src_quality");
	cmd.description = QStringLiteral("Choose the sample-rate-conversion converter the render resampler "
		"uses: 'linear' (the default, and the converter this engine has always used), "
		"'sinc_fastest', 'sinc_medium' or 'sinc_best' (libsamplerate's sinc converters, in "
		"increasing quality and cost). The choice reaches the resampler through "
		"Sample::play, so it governs every mismatch-rate source in the render, not just the "
		"export file. It takes effect at the next render; a render already in flight keeps "
		"the converter it started with.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("src_quality"), stringProperty()},
	}, {QStringLiteral("src_quality")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("src_quality"), stringProperty()},
		{QStringLiteral("previous"), stringProperty()},
		{QStringLiteral("dither"), booleanProperty()},
		{QStringLiteral("src_quality_choices"), arrayProperty()},
		{QStringLiteral("loudness_report"), booleanProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		const QString requested = args.value(QStringLiteral("src_quality")).toString();
		SrcQuality parsed = SrcQuality::Linear;
		if (!srcQualityFromName(requested.toLatin1().constData(), &parsed))
		{
			// The wire names are the enum's own, so the refusal can name them
			// without a second list to drift against.
			QStringList names;
			for (int i = 0; i <= static_cast<int>(SrcQuality::SincBest); ++i)
			{
				names << QString::fromLatin1(srcQualityName(static_cast<SrcQuality>(i)));
			}
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("unknown src_quality '%1'; expected one of %2")
					.arg(requested, names.join(QStringLiteral(", "))));
		}

		const SrcQuality previous = ExportRenderSettings::srcQuality();
		control::addUndoStep(
			[previous]() { ExportRenderSettings::setSrcQuality(previous); },
			[parsed]() { ExportRenderSettings::setSrcQuality(parsed); });
		ExportRenderSettings::setSrcQuality(parsed);

		QJsonObject result = exportSettingsJson();
		result.insert(QStringLiteral("previous"), qualityWireName(previous));

		QJsonObject transaction;
		transaction.insert(QStringLiteral("before"),
			QJsonObject{{QStringLiteral("src_quality"), qualityWireName(previous)}});
		transaction.insert(QStringLiteral("inverse"),
			QJsonObject{{QStringLiteral("op"), QStringLiteral("export.set_src_quality")},
				{QStringLiteral("args"),
					QJsonObject{{QStringLiteral("src_quality"), qualityWireName(previous)}}}});
		transaction.insert(QStringLiteral("reversible"), true);
		transaction.insert(QStringLiteral("mechanism"),
			QStringLiteral("action checkpoint: the recorded undo step restores the previous "
				"converter selection, exactly as this command sets the new one"));
		result.insert(QStringLiteral("__transaction"), transaction);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerExportSetLoudnessReport(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("export.set_loudness_report");
	cmd.group = QStringLiteral("export");
	cmd.verb = QStringLiteral("set_loudness_report");
	cmd.description = QStringLiteral("Turn the render path's EBU R128 loudness report on or off for "
		"the NEXT render (feature row 24 of docs/FEATURE-LIST-0.3.0.md, \"LUFS / loudness "
		"metering\"). With it on, the render measures every block it writes with the engine's "
		"BS.1770-4 meter (the same LufsMeter `meter.get_state` and `meter.measure_file` report "
		"through) and writes the report beside the output as <output>.loudness.txt - integrated "
		"LUFS, the loudest 3 s window, true peak in dBTP and the EBU R128 verdict against -23.0 "
		"LUFS-I +/- 0.5 LU and -1.0 dBTP. MEASURE-ONLY: the rendered audio is byte-identical "
		"whether the report is on or off (the tap reads the frames immediately before the file "
		"device writes them), which is why the default is off rather than on. The same value is "
		"the export dialog's \"Loudness report (EBU R128)\" checkbox and the CLI's "
		"--loudness-report flag; this verb is how an agent sets it. Reversible: control.undo "
		"restores the previous selection.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("enabled"), booleanProperty()},
	}, {QStringLiteral("enabled")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("loudness_report"), booleanProperty()},
		{QStringLiteral("previous"), booleanProperty()},
		{QStringLiteral("dither"), booleanProperty()},
		{QStringLiteral("src_quality"), stringProperty()},
		{QStringLiteral("src_quality_choices"), arrayProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		const bool previous = ExportRenderSettings::loudnessReport();
		const bool requested = args.value(QStringLiteral("enabled")).toBool();

		// SPEC A16: one bounded scalar owned by a subsystem the engine does not
		// journal, so the inverse is a recorded undo STEP on the engine's own
		// stack - the mechanism export.set_dither and export.set_src_quality use
		// beside it.
		control::addUndoStep(
			[previous]() { ExportRenderSettings::setLoudnessReport(previous); },
			[requested]() { ExportRenderSettings::setLoudnessReport(requested); });
		ExportRenderSettings::setLoudnessReport(requested);

		QJsonObject result = exportSettingsJson();
		result.insert(QStringLiteral("previous"), previous);

		QJsonObject transaction;
		transaction.insert(QStringLiteral("before"),
			QJsonObject{{QStringLiteral("loudness_report"), previous}});
		transaction.insert(QStringLiteral("inverse"),
			QJsonObject{{QStringLiteral("op"), QStringLiteral("export.set_loudness_report")},
				{QStringLiteral("args"),
					QJsonObject{{QStringLiteral("enabled"), previous}}}});
		transaction.insert(QStringLiteral("reversible"), true);
		transaction.insert(QStringLiteral("mechanism"),
			QStringLiteral("action checkpoint: the recorded undo step restores the previous "
				"selection, exactly as this command sets the new one"));
		result.insert(QStringLiteral("__transaction"), transaction);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerExportCommands(ControlRegistry& registry)
{
	registerExportGetSettings(registry);
	registerExportSetDither(registry);
	registerExportSetSrcQuality(registry);
	// Feature row 24: the render-path loudness report, so the half of the
	// feature that only the export dialog's checkbox could reach is drivable
	// from the socket too.
	registerExportSetLoudnessReport(registry);
}

} // namespace lmms
