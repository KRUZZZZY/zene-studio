/*
 * ControlCommandsSlicerSlices.cpp - per-slice SlicerT controls on the agent surface (0.5.0 L10, Q050-263)
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

/*! Q050-263: every playable SlicerT slice has a gain, a pan, a filter and a random amount
 *  (include/SlicerSliceControls.h). They are not AutomatableModels (there would be 520 of them), so
 *  plugin.param_get / plugin.param_set cannot reach them; these two commands do.
 *
 *  - plugin.slice_get reads one slice (or, with 'slice' left out, all of them): its MIDI key, its
 *    start and end as a fraction of the sample, and the four values; plus the controls' ranges.
 *  - plugin.slice_set writes any of the four values of one slice. Every value is range-checked
 *    before any is written (a refusal writes nothing). One call is one undo step: the track's
 *    checkpoint, whose saveSettings/loadSettings carry the slice values.
 *
 *  The SlicerT plug-in is reached through its meta-object (sliceCount / slicePoint / sliceControl /
 *  setSliceControl are Q_INVOKABLE), the way ControlCommandsOutOfProcessSupport.cpp reaches its
 *  plug-ins, so the core carries no plug-in header. The GUI's per-slice panel calls these same
 *  commands. */

#include <cmath>

#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QString>
#include <QVector>

#include "ControlDeviceSupport.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "SlicerSliceControls.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

//! The track @a args names, its SlicerT and the number of slices it holds, or false with @a error set.
bool slicerOf(const QJsonObject& args, ControlTarget* target, Instrument** device, int* count, ControlResult* error)
{
	if (!resolveControlTarget(args.value(QStringLiteral("target")).toString(), target, error)) { return false; }
	if (target->instrumentTrack == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("target %1 is not an instrument track").arg(target->id));
		return false;
	}
	*device = target->instrumentTrack->instrument();
	if (*device == nullptr || (*device)->nodeName().toLower() != QLatin1String("slicert"))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("target %1 does not hold a SlicerT: per-slice controls belong to SlicerT only")
				.arg(target->id));
		return false;
	}
	*count = 0;
	QMetaObject::invokeMethod(*device, "sliceCount", Qt::DirectConnection, Q_RETURN_ARG(int, *count));
	return true;
}

double controlValue(Instrument* device, int slice, const char* name)
{
	double value = std::nan("");
	QMetaObject::invokeMethod(device, "sliceControl", Qt::DirectConnection, Q_RETURN_ARG(double, value),
		Q_ARG(int, slice), Q_ARG(QString, QString::fromLatin1(name)));
	return value;
}

double slicePointValue(Instrument* device, int index)
{
	double value = -1.0;
	QMetaObject::invokeMethod(device, "slicePoint", Qt::DirectConnection, Q_RETURN_ARG(double, value),
		Q_ARG(int, index));
	return value;
}

QJsonObject sliceJson(const ControlTarget& target, Instrument* device, int slice)
{
	QJsonObject entry;
	entry.insert(QStringLiteral("slice"), slice);
	entry.insert(QStringLiteral("key"), target.instrumentTrack->baseNote() + 1 + slice);
	entry.insert(QStringLiteral("start"), slicePointValue(device, slice));
	entry.insert(QStringLiteral("end"), slicePointValue(device, slice + 1));
	for (const auto& spec : slicer::SliceControlSpecs)
	{
		entry.insert(QString::fromLatin1(spec.name), controlValue(device, slice, spec.name));
	}
	return entry;
}

QJsonArray controlSpecsJson()
{
	QJsonArray specs;
	for (const auto& spec : slicer::SliceControlSpecs)
	{
		specs.append(QJsonObject{{QStringLiteral("name"), QString::fromLatin1(spec.name)},
			{QStringLiteral("min"), spec.lowest}, {QStringLiteral("max"), spec.highest},
			{QStringLiteral("neutral"), spec.neutral}, {QStringLiteral("unit"), QString::fromLatin1(spec.unit)}});
	}
	return specs;
}

QJsonObject sliceSchemaProperties()
{
	QJsonObject properties{{QStringLiteral("target"), stringProperty()}, {QStringLiteral("slice"), integerProperty()}};
	for (const auto& spec : slicer::SliceControlSpecs)
	{
		properties.insert(QString::fromLatin1(spec.name), numberProperty());
	}
	return properties;
}

ControlResult sliceGet(const QJsonObject& args)
{
	ControlTarget target;
	Instrument* device = nullptr;
	int count = 0;
	ControlResult error;
	if (!slicerOf(args, &target, &device, &count, &error)) { return error; }

	QJsonArray slices;
	if (args.contains(QStringLiteral("slice")))
	{
		const int slice = args.value(QStringLiteral("slice")).toInt(-1);
		if (slice < 0 || slice >= count)
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("no slice %1 on %2 (it has %3 slices, numbered from 0)")
					.arg(slice).arg(target.id).arg(count));
		}
		slices.append(sliceJson(target, device, slice));
	}
	else
	{
		for (int slice = 0; slice < count; ++slice) { slices.append(sliceJson(target, device, slice)); }
	}
	return ControlResult::success(QJsonObject{{QStringLiteral("target"), target.id},
		{QStringLiteral("device_plugin"), device->nodeName()}, {QStringLiteral("slice_count"), count},
		{QStringLiteral("base_key"), target.instrumentTrack->baseNote()},
		{QStringLiteral("controls"), controlSpecsJson()}, {QStringLiteral("slices"), slices}});
}

//! The controls @a args gives, each checked against its range, in @a given; false with @a error set when one is
//! not a number, one is out of range, or none is given. Nothing is written here.
bool givenControls(const QJsonObject& args, QVector<const slicer::SliceControlSpec*>* given, ControlResult* error)
{
	for (const auto& spec : slicer::SliceControlSpecs)
	{
		const QString name = QString::fromLatin1(spec.name);
		if (!args.contains(name)) { continue; }
		const QJsonValue raw = args.value(name);
		const double value = raw.toDouble(std::nan(""));
		if (!raw.isDouble() || !std::isfinite(value))
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'%1' must be a number").arg(name));
			return false;
		}
		if (static_cast<float>(value) < static_cast<float>(spec.lowest) || static_cast<float>(value) > static_cast<float>(spec.highest))
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("%1 %2 is outside the range %3..%4 of slice control '%1'")
					.arg(name).arg(value).arg(spec.lowest).arg(spec.highest));
			return false;
		}
		given->append(&spec);
	}
	if (given->isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("give at least one of gain, pan, filter, random"));
		return false;
	}
	return true;
}

//! The slice @a args names, checked against @a count; -1 with @a error set otherwise.
int sliceArgument(const QJsonObject& args, const QString& targetId, int count, ControlResult* error)
{
	if (!args.contains(QStringLiteral("slice")) || !args.value(QStringLiteral("slice")).isDouble())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'slice' (a slice number) is required"));
		return -1;
	}
	const int slice = args.value(QStringLiteral("slice")).toInt(-1);
	if (slice < 0 || slice >= count)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no slice %1 on %2 (it has %3 slices, numbered from 0)").arg(slice).arg(targetId).arg(count));
		return -1;
	}
	return slice;
}

ControlResult sliceSet(const QJsonObject& args)
{
	ControlTarget target;
	Instrument* device = nullptr;
	int count = 0;
	ControlResult error;
	if (!slicerOf(args, &target, &device, &count, &error)) { return error; }
	const int slice = sliceArgument(args, target.id, count, &error);
	if (slice < 0) { return error; }
	// Validate everything first: a refusal writes nothing.
	QVector<const slicer::SliceControlSpec*> given;
	if (!givenControls(args, &given, &error)) { return error; }

	const QJsonObject before = sliceJson(target, device, slice);
	// One undo step for the call: the track's checkpoint, whose settings carry the slice values.
	addUndoStep(QVector<JournallingObject*>{target.instrumentTrack});
	for (const auto* spec : given)
	{
		const QString name = QString::fromLatin1(spec->name);
		bool wrote = false;
		QMetaObject::invokeMethod(device, "setSliceControl", Qt::DirectConnection, Q_RETURN_ARG(bool, wrote),
			Q_ARG(int, slice), Q_ARG(QString, name), Q_ARG(double, args.value(name).toDouble()));
		if (!wrote)
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("SlicerT refused %1 on slice %2").arg(name).arg(slice));
		}
	}
	Engine::getSong()->setModified();

	QJsonObject result{{QStringLiteral("target"), target.id}, {QStringLiteral("device_plugin"), device->nodeName()},
		{QStringLiteral("slice"), slice}, {QStringLiteral("previous"), before},
		{QStringLiteral("current"), sliceJson(target, device, slice)}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("slice"), before}},
		QStringLiteral("control.undo"), {}, true, QStringLiteral("ProjectJournal (Track checkpoint): undo restores "
		"the SlicerT's saved state, which carries every slice's gain, pan, filter and random amount")));
	return ControlResult::success(result);
}

void registerGet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.slice_get");
	cmd.group = QStringLiteral("plugin");
	cmd.verb = QStringLiteral("slice_get");
	cmd.description = QStringLiteral("Read the per-slice controls of the SlicerT on instrument track 'target': for "
		"one 'slice' (numbered from 0; slice i plays on MIDI key base_key + 1 + i) or, with 'slice' left out, every "
		"slice. Each slice reports its key, its start and end as a fraction of the sample, and its gain (dB), pan "
		"(-1..1), filter (-1 low-pass .. +1 high-pass, 0 off) and random (0..1) - the neutral values are 0. "
		"'controls' lists each control's range. Read-only.");
	QJsonObject properties = sliceSchemaProperties();
	cmd.argsSchema = objectSchema({{QStringLiteral("target"), properties.value(QStringLiteral("target"))},
		{QStringLiteral("slice"), properties.value(QStringLiteral("slice"))}}, {QStringLiteral("target")});
	cmd.resultSchema = objectSchema({{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("device_plugin"), stringProperty()}, {QStringLiteral("slice_count"), integerProperty()},
		{QStringLiteral("base_key"), integerProperty()}, {QStringLiteral("controls"), arrayProperty()},
		{QStringLiteral("slices"), arrayProperty()}});
	cmd.mutating = false;
	cmd.handler = sliceGet;
	registry.registerCommand(cmd);
}

void registerSet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.slice_set");
	cmd.group = QStringLiteral("plugin");
	cmd.verb = QStringLiteral("slice_set");
	cmd.description = QStringLiteral("Set any of 'gain' (dB, -48..12), 'pan' (-1..1, a balance), 'filter' (-1 "
		"low-pass .. +1 high-pass, 0 off) and 'random' (0..1: each trigger varies gain by up to 6 dB and pan by up "
		"to 0.5 times this) on one slice of the SlicerT on instrument track 'target'. 'slice' counts from 0; slice i "
		"plays on MIDI key base_key + 1 + i (plugin.slice_get). Values outside a range are refused with "
		"invalid_args and nothing is written. Saved with the project. One undo step per call.");
	cmd.argsSchema = objectSchema(sliceSchemaProperties(), {QStringLiteral("target"), QStringLiteral("slice")});
	cmd.resultSchema = objectSchema({{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("device_plugin"), stringProperty()}, {QStringLiteral("slice"), integerProperty()},
		{QStringLiteral("previous"), objectProperty()}, {QStringLiteral("current"), objectProperty()}});
	cmd.mutating = true;
	cmd.handler = sliceSet;
	registry.registerCommand(cmd);
}

} // namespace

void registerSlicerSliceCommands(ControlRegistry& registry)
{
	registerGet(registry);
	registerSet(registry);
}

} // namespace lmms
