/*
 * ControlCommandsPluginParams.cpp - plugin.param_get / plugin.param_set, the
 *                                   generic device-parameter surface (SPEC A14).
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

#include "AutomatableModel.h"

#include "ControlVocabulary.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "Effect.h"
#include "Instrument.h"
#include "InstrumentTrack.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

QJsonObject parameterSchema()
{
	return objectSchema({
		{QStringLiteral("index"), integerProperty()},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("value"), numberProperty()},
		{QStringLiteral("min"), numberProperty()},
		{QStringLiteral("max"), numberProperty()},
		{QStringLiteral("type"), stringProperty()},
	});
}

//! The parameters of the device \a pluginId names on \a target, plus the
//! device's plugin name. "inst" is an instrument; everything else is an
//! effect instance id.
bool deviceParameters(const ControlTarget& target, const QString& pluginId,
	QList<AutomatableModel*>* models, QString* pluginName, ControlResult* error)
{
	if (pluginId == QLatin1String("inst"))
	{
		if (target.instrumentTrack == nullptr || target.instrumentTrack->instrument() == nullptr)
		{
			*error = ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("target %1 carries no instrument").arg(target.id));
			return false;
		}
		Instrument* instrument = target.instrumentTrack->instrument();
		*models = controlInstrumentParameters(instrument);
		*pluginName = QString::fromUtf8(instrument->descriptor()->name);
		return true;
	}

	Effect* effect = resolveControlEffect(target, pluginId, error);
	if (effect == nullptr) { return false; }
	*models = controlEffectParameters(effect);
	*pluginName = QString::fromUtf8(effect->descriptor()->name);
	return true;
}

//! Index of a model within a list of non-const models, from a const view of it.
//! `QList::indexOf`'s Qt5 overload takes `AutomatableModel* const&`, so calling it with a
//! `const AutomatableModel*` is a hard error on every Qt5 CI job (the Qt6 build on the
//! development box accepts it through its templated overload, which is why this only ever
//! failed in CI):
//!   ControlCommandsPluginParams.cpp:89:72: error: binding reference of type
//!     'lmms::AutomatableModel* const&' to 'const lmms::AutomatableModel*' discards qualifiers
//! Comparing the pointers directly is independent of both the Qt version and the overload set,
//! and needs no const_cast.
int indexOfModel(const QList<AutomatableModel*>& models, const AutomatableModel* model)
{
	for (int i = 0; i < models.size(); ++i)
	{
		if (models.at(i) == model) { return i; }
	}
	return -1;
}

//! One parameter plus the plugin it belongs to.
ControlResult parameterResult(const QString& target, const QString& pluginId,
	const QString& pluginName, const AutomatableModel* model,
	const QList<AutomatableModel*>& models)
{
	QJsonObject entry = controlParameterJson(model, indexOfModel(models, model));
	QJsonObject result;
	result.insert(QStringLiteral("target"), target);
	result.insert(QStringLiteral("plugin"), pluginId);
	result.insert(QStringLiteral("device_plugin"), pluginName);
	result.insert(QStringLiteral("parameter"), entry);
	return ControlResult::success(result);
}

void registerParamGet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.param_get");
	cmd.group = QStringLiteral("plugin");
	cmd.verb = QStringLiteral("param_get");
	cmd.description = QStringLiteral("Read one device parameter by 'name' or by 'index'. "
		"'plugin' is an fx-<n> instance id or 'inst' for the target track's instrument. The range "
		"reported is the engine's own model range.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("plugin"), stringProperty()},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("index"), integerProperty()},
	}, {QStringLiteral("target"), QStringLiteral("plugin")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("plugin"), stringProperty()},
		{QStringLiteral("parameter"), parameterSchema()},
	});
	cmd.handler = [](const QJsonObject& args) {
		ControlTarget target;
		ControlResult error;
		if (!resolveControlTarget(args.value(QStringLiteral("target")).toString(), &target, &error))
		{
			return error;
		}
		const QString pluginId = args.value(QStringLiteral("plugin")).toString();
		QList<AutomatableModel*> models;
		QString pluginName;
		if (!deviceParameters(target, pluginId, &models, &pluginName, &error)) { return error; }

		const bool hasIndex = args.contains(QStringLiteral("index"));
		AutomatableModel* model = controlResolveParameterIn(models,
			args.value(QStringLiteral("name")).toString(),
			args.value(QStringLiteral("index")).toInt(), hasIndex, &error);
		if (model == nullptr) { return error; }
		return parameterResult(target.id, pluginId, pluginName, model, models);
	};
	registry.registerCommand(cmd);
}

//! The typed range refusal: the engine's own min/max is the authority.
ControlResult rangeRefusal(const AutomatableModel* model, double value)
{
	return ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("value %1 is outside the range %2..%3 of parameter '%4'")
			.arg(value)
			.arg(static_cast<double>(model->minValue<float>()))
			.arg(static_cast<double>(model->maxValue<float>()))
			.arg(model->displayName()));
}

void registerParamSet(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.param_set");
	cmd.group = QStringLiteral("plugin");
	cmd.verb = QStringLiteral("param_set");
	cmd.description = QStringLiteral("Set one device parameter by 'name' or by 'index' with the "
		"engine's own range enforcement: a value outside the model's min..max is refused with "
		"invalid_args rather than silently clamped. Reversible through the ProjectJournal.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("plugin"), stringProperty()},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("index"), integerProperty()},
		{QStringLiteral("value"), numberProperty()},
	}, {QStringLiteral("target"), QStringLiteral("plugin"), QStringLiteral("value")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("target"), stringProperty()},
		{QStringLiteral("plugin"), stringProperty()},
		{QStringLiteral("parameter"), parameterSchema()},
		{QStringLiteral("previous"), numberProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlTarget target;
		ControlResult error;
		if (!resolveControlTarget(args.value(QStringLiteral("target")).toString(), &target, &error))
		{
			return error;
		}
		const QString pluginId = args.value(QStringLiteral("plugin")).toString();
		QList<AutomatableModel*> models;
		QString pluginName;
		if (!deviceParameters(target, pluginId, &models, &pluginName, &error)) { return error; }

		const bool hasIndex = args.contains(QStringLiteral("index"));
		AutomatableModel* model = controlResolveParameterIn(models,
			args.value(QStringLiteral("name")).toString(),
			args.value(QStringLiteral("index")).toInt(), hasIndex, &error);
		if (model == nullptr) { return error; }

		const double value = args.value(QStringLiteral("value")).toDouble();
		if (value < static_cast<double>(model->minValue<float>()) ||
			value > static_cast<double>(model->maxValue<float>()))
		{
			return rangeRefusal(model, value);
		}
		const float previous = model->value<float>();
		// The model is a JournallingObject, so the checkpoint is a real inverse
		// for the engine's own undo stack (SPEC A16) - the same mechanism
		// mixer.set_volume uses.
		model->addJournalCheckPoint();
		model->setValue(static_cast<float>(value));

		QJsonObject result;
		result.insert(QStringLiteral("target"), target.id);
		result.insert(QStringLiteral("plugin"), pluginId);
		result.insert(QStringLiteral("device_plugin"), pluginName);
		result.insert(QStringLiteral("previous"), static_cast<double>(previous));
		result.insert(QStringLiteral("parameter"),
			controlParameterJson(model, indexOfModel(models, model)));

		QJsonObject inverseArgs;
		inverseArgs.insert(QStringLiteral("target"), target.id);
		inverseArgs.insert(QStringLiteral("plugin"), pluginId);
		inverseArgs.insert(QStringLiteral("index"), models.indexOf(model));
		inverseArgs.insert(QStringLiteral("value"), static_cast<double>(previous));
		QJsonObject transaction;
		transaction.insert(QStringLiteral("before"),
			QJsonObject{{QStringLiteral("parameter"), model->displayName()},
				{QStringLiteral("value"), static_cast<double>(previous)}});
		transaction.insert(QStringLiteral("inverse"),
			QJsonObject{{QStringLiteral("op"), QStringLiteral("plugin.param_set")},
				{QStringLiteral("args"), inverseArgs}});
		transaction.insert(QStringLiteral("reversible"), true);
		transaction.insert(QStringLiteral("mechanism"),
			QStringLiteral("ProjectJournal (parameter model checkpoint)"));
		result.insert(QStringLiteral("__transaction"), transaction);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerPluginParameterCommands(ControlRegistry& registry)
{
	registerParamGet(registry);
	registerParamSet(registry);
}

} // namespace lmms
