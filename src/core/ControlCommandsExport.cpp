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
	return result;
}

void registerExportGetSettings(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("export.get_settings");
	cmd.group = QStringLiteral("export");
	cmd.verb = QStringLiteral("get_settings");
	cmd.description = QStringLiteral("Read the render settings that outlive one OutputSettings: whether "
		"the next export dithers, and which sample-rate-conversion quality it resamples with. "
		"Both are OFF/DEFAULT unless something asked for otherwise - dither is off by default "
		"because this release's render-reproducibility claim depends on it, and 'linear' is the "
		"converter every render the engine has produced so far used. Read-only: no transaction "
		"is recorded. There is no interface for either setting; drive them through export.set_* "
		"(docs/KNOWN-LIMITATIONS.md).");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("dither"), booleanProperty()},
		{QStringLiteral("src_quality"), stringProperty()},
		{QStringLiteral("src_quality_choices"), arrayProperty()},
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

} // namespace

void registerExportCommands(ControlRegistry& registry)
{
	registerExportGetSettings(registry);
	registerExportSetDither(registry);
	registerExportSetSrcQuality(registry);
}

} // namespace lmms
