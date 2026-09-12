/*
 * ControlDeviceSupport.cpp - shared helpers for the plugin.* / dsp.* command
 *                            group (SPEC A11-A14).
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

#include "ControlDeviceSupport.h"

#include <QDomDocument>
#include <QHash>
#include <QLibrary>
#include <QStringList>

#include "AutomatableModel.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "LadspaBase.h"
#include "Mixer.h"
#include "Plugin.h"
#include "PluginFactory.h"
#include "SampleTrack.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

namespace
{

#if defined(LMMS_BUILD_WIN32) || defined(LMMS_BUILD_CYGWIN)
const QString LadspaSuffix = QStringLiteral(".dll");
#else
const QString LadspaSuffix = QStringLiteral(".so");
#endif

QString trackTypeName(Track::Type type)
{
	switch (type)
	{
		case Track::Type::Instrument: return QStringLiteral("instrument");
		case Track::Type::Pattern: return QStringLiteral("pattern");
		case Track::Type::Sample: return QStringLiteral("sample");
		case Track::Type::Event: return QStringLiteral("event");
		case Track::Type::Video: return QStringLiteral("video");
		case Track::Type::Automation: return QStringLiteral("automation");
		case Track::Type::HiddenAutomation: return QStringLiteral("hidden_automation");
		case Track::Type::Count: break;
	}
	return QStringLiteral("unknown");
}

//! Look \a name up among the build's loaded plugin descriptors.
const Plugin::Descriptor* findDescriptor(const QString& name, Plugin::Type type)
{
	for (const Plugin::Descriptor* descriptor : getPluginFactory()->descriptors(type))
	{
		if (descriptor != nullptr && name == QLatin1String(descriptor->name))
		{
			return descriptor;
		}
	}
	return nullptr;
}

//! The LADSPA file key is the library file name, extension included.
QString ladspaFileKey(const QString& file)
{
	return file.endsWith(LadspaSuffix) ? file : file + LadspaSuffix;
}

bool resolveTrackTarget(const QString& id, ControlTarget* target, ControlResult* error)
{
	const int index = control::idToIndex(id, QStringLiteral("trk-"));
	if (index < 0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is not a target id of the form trk-<n>").arg(id));
		return false;
	}
	const TrackContainer::TrackList& tracks = Engine::getSong()->tracks();
	if (index >= static_cast<int>(tracks.size()))
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no track %1 (the song has %2)").arg(id).arg(tracks.size()));
		return false;
	}

	Track* track = tracks[index];
	target->id = id;
	target->kind = QStringLiteral("track");
	target->typeName = trackTypeName(track->type());
	if (auto* instrumentTrack = dynamic_cast<InstrumentTrack*>(track))
	{
		target->instrumentTrack = instrumentTrack;
		target->chain = instrumentTrack->audioBusHandle()->effects();
		return true;
	}
	if (auto* sampleTrack = dynamic_cast<SampleTrack*>(track))
	{
		target->chain = sampleTrack->audioBusHandle()->effects();
		return true;
	}
	*error = ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("track %1 is a %2 track and carries no device chain")
			.arg(id, target->typeName));
	return false;
}

bool resolveChannelTarget(const QString& id, ControlTarget* target, ControlResult* error)
{
	const int index = control::idToIndex(id, QStringLiteral("ch-"));
	Mixer* mixer = Engine::mixer();
	if (index < 0 || mixer == nullptr || index >= static_cast<int>(mixer->numChannels()))
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no mixer channel %1 (the mixer has %2)")
				.arg(id).arg(mixer == nullptr ? 0 : static_cast<int>(mixer->numChannels())));
		return false;
	}
	target->id = id;
	target->kind = QStringLiteral("channel");
	target->typeName = QStringLiteral("channel");
	target->chain = &mixer->mixerChannel(index)->m_fxChain;
	target->instrumentTrack = nullptr;
	return true;
}

} // namespace

bool controlPluginIsInstantiable(const QString& name, ControlResult* error)
{
	// Checked before instantiation because Plugin::instantiate() puts up a modal
	// QMessageBox on either failure, which would deadlock a headless
	// control-socket call.
	const PluginFactory::PluginInfo& info = getPluginFactory()->pluginInfo(name.toUtf8());
	if (info.isNull() || info.library == nullptr ||
		info.library->resolve("lmms_plugin_main") == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("plugin module '%1' is not loadable in this build (%2)")
				.arg(name, getPluginFactory()->errorString(name)));
		return false;
	}
	return true;
}

bool resolveControlTarget(const QString& id, ControlTarget* target, ControlResult* error)
{
	if (id.startsWith(QLatin1String("trk-"))) { return resolveTrackTarget(id, target, error); }
	if (id.startsWith(QLatin1String("ch-"))) { return resolveChannelTarget(id, target, error); }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("'%1' is not a target id of the form trk-<n> or ch-<n>").arg(id));
	return false;
}

Effect* resolveControlEffect(const ControlTarget& target, const QString& pluginId, ControlResult* error)
{
	if (pluginId == QLatin1String("inst"))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'inst' is an instrument, not an effect: it has no unload or bypass. "
				"Read it with dsp.get_state, address its parameters with plugin.param_get / "
				"plugin.param_set and save it with plugin.preset_save / plugin.state_save"));
		return nullptr;
	}
	const int index = control::idToIndex(pluginId, QStringLiteral("fx-"));
	if (index < 0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is not a device instance id of the form fx-<n> (or 'inst')")
				.arg(pluginId));
		return nullptr;
	}
	const std::vector<Effect*>& effects = target.chain->effects();
	if (index >= static_cast<int>(effects.size()))
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no device %1 on %2 (it carries %3)")
				.arg(pluginId, target.id).arg(effects.size()));
		return nullptr;
	}
	return effects[static_cast<std::size_t>(index)];
}

QList<AutomatableModel*> controlEffectParameters(Effect* effect)
{
	QList<AutomatableModel*> models;
	if (effect == nullptr) { return models; }
	// Same rule as Instrument::parameterCount(): a model without a display name
	// is internal bookkeeping, not a user-facing parameter, and the QObject
	// child order is stable for the device's lifetime.
	for (AutomatableModel* model : effect->findChildren<AutomatableModel*>(
			QString(), Qt::FindChildrenRecursively))
	{
		if (!model->displayName().isEmpty()) { models.append(model); }
	}
	return models;
}

QJsonObject controlParameterJson(const AutomatableModel* model, int index)
{
	QJsonObject out;
	out.insert(QStringLiteral("index"), index);
	out.insert(QStringLiteral("name"), model->displayName());
	out.insert(QStringLiteral("value"), static_cast<double>(model->value<float>()));
	out.insert(QStringLiteral("min"), static_cast<double>(model->minValue<float>()));
	out.insert(QStringLiteral("max"), static_cast<double>(model->maxValue<float>()));
	out.insert(QStringLiteral("step"), static_cast<double>(model->step<float>()));
	out.insert(QStringLiteral("type"),
		dynamic_cast<const BoolModel*>(model) != nullptr ? QStringLiteral("boolean")
														 : QStringLiteral("number"));
	return out;
}

QJsonArray controlParameterList(const QList<AutomatableModel*>& models)
{
	QJsonArray parameters;
	for (int i = 0; i < models.size(); ++i)
	{
		parameters.append(controlParameterJson(models.at(i), i));
	}
	return parameters;
}

QJsonArray controlParameterList(Effect* effect)
{
	return controlParameterList(controlEffectParameters(effect));
}

QList<AutomatableModel*> controlInstrumentParameters(Instrument* instrument)
{
	QList<AutomatableModel*> models;
	if (instrument == nullptr) { return models; }
	for (int i = 0; i < instrument->parameterCount(); ++i)
	{
		AutomatableModel* model = instrument->parameterModel(i);
		if (model != nullptr) { models.append(model); }
	}
	return models;
}

QStringList parameterNames(const QList<AutomatableModel*>& models)
{
	QStringList names;
	for (const AutomatableModel* model : models) { names.append(model->displayName()); }
	return names;
}

//! The parameter at \a index, or a typed not_found.
AutomatableModel* parameterByIndex(const QList<AutomatableModel*>& models, int index,
	ControlResult* error)
{
	if (index < 0 || index >= models.size())
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no parameter index %1 on this device (it has %2 parameters)")
				.arg(index).arg(models.size()));
		return nullptr;
	}
	return models.at(index);
}

//! The single parameter named \a name; an ambiguous name is invalid_args and an
//! unknown one is not_found, both naming what was found.
AutomatableModel* parameterByName(const QList<AutomatableModel*>& models, const QString& name,
	ControlResult* error)
{
	if (name.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("give either 'name' or 'index'"));
		return nullptr;
	}

	QList<int> matches;
	for (int i = 0; i < models.size(); ++i)
	{
		if (models.at(i)->displayName() == name) { matches.append(i); }
	}
	if (matches.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("'%1' is not a parameter of this device (%2 parameters: %3)")
				.arg(name).arg(models.size())
				.arg(parameterNames(models).join(QLatin1String(", "))));
		return nullptr;
	}
	if (matches.size() > 1)
	{
		QStringList indexes;
		for (int value : matches) { indexes.append(QString::number(value)); }
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("parameter name '%1' is ambiguous: %2 models carry it (indexes %3); "
				"address it with 'index'")
				.arg(name).arg(matches.size()).arg(indexes.join(QLatin1String(", "))));
		return nullptr;
	}
	return models.at(matches.first());
}

AutomatableModel* controlResolveParameterIn(const QList<AutomatableModel*>& models,
	const QString& name, int index, bool hasIndex, ControlResult* error)
{
	if (hasIndex) { return parameterByIndex(models, index, error); }
	return parameterByName(models, name, error);
}

AutomatableModel* controlResolveParameter(Effect* effect, const QString& name, int index,
	bool hasIndex, ControlResult* error)
{
	return controlResolveParameterIn(controlEffectParameters(effect), name, index, hasIndex, error);
}

QJsonObject controlEffectJson(Effect* effect, int index)
{
	QJsonObject out;
	out.insert(QStringLiteral("id"), control::effectId(index));
	out.insert(QStringLiteral("index"), index);
	out.insert(QStringLiteral("plugin"), QString::fromUtf8(effect->descriptor()->name));
	out.insert(QStringLiteral("display_name"), QString::fromUtf8(effect->descriptor()->displayName));
	out.insert(QStringLiteral("enabled"), effect->isEnabled());
	out.insert(QStringLiteral("processing"), effect->isProcessingAudio());
	out.insert(QStringLiteral("wet"), static_cast<double>(effect->wetLevel()));
	out.insert(QStringLiteral("latency_frames"), effect->latencyFrames());
	out.insert(QStringLiteral("parameters"), controlParameterList(effect));
	return out;
}

QString controlEffectStateXml(Effect* effect)
{
	QDomDocument doc;
	QDomElement root = doc.createElement(QStringLiteral("zenepluginstate"));
	root.setAttribute(QStringLiteral("version"), 1);
	root.setAttribute(QStringLiteral("plugin"), QString::fromUtf8(effect->descriptor()->name));
	const Plugin::Descriptor::SubPluginFeatures::Key& key = effect->key();
	const bool hosted = key.isValid() && !key.attributes.isEmpty();
	if (hosted)
	{
		// A hosted plugin's identity is its key (LADSPA: file + label); the
		// descriptor name alone ("ladspaeffect") would not identify it.
		root.setAttribute(QStringLiteral("hosted_file"), key.attributes.value(QStringLiteral("file")));
		root.setAttribute(QStringLiteral("hosted_id"), key.attributes.value(QStringLiteral("plugin")));
	}
	doc.appendChild(root);

	QDomElement body = effect->saveState(doc, root);
	body.setAttribute(QStringLiteral("name"), QString::fromUtf8(effect->descriptor()->name));
	if (hosted) { body.appendChild(key.saveXML(doc)); }
	return doc.toString();
}

ControlResult controlRestoreEffectState(Effect* effect, const QByteArray& xml)
{
	QDomDocument doc;
	QString parseError;
	int line = 0;
	int column = 0;
	if (!doc.setContent(xml, &parseError, &line, &column))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("the state file is not valid XML: %1 (line %2, column %3)")
				.arg(parseError).arg(line).arg(column));
	}

	const QDomElement root = doc.documentElement();
	if (root.tagName() != QLatin1String("zenepluginstate"))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("the state file is not a zenepluginstate document (root <%1>)")
				.arg(root.tagName()));
	}
	const QString plugin = QString::fromUtf8(effect->descriptor()->name);
	if (root.attribute(QStringLiteral("plugin")) != plugin)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the state file holds '%1' state; this device is '%2'")
				.arg(root.attribute(QStringLiteral("plugin")), plugin));
	}
	const QString hostedId = root.attribute(QStringLiteral("hosted_id"));
	const Plugin::Descriptor::SubPluginFeatures::Key& key = effect->key();
	if (!hostedId.isEmpty() && hostedId != key.attributes.value(QStringLiteral("plugin")))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the state file holds hosted plugin '%1'; this device is '%2'")
				.arg(hostedId, key.attributes.value(QStringLiteral("plugin"))));
	}

	const QDomElement body = root.firstChildElement(effect->nodeName());
	if (body.isNull())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("the state file carries no <%1> element").arg(effect->nodeName()));
	}
	effect->restoreState(body);

	QJsonObject result;
	result.insert(QStringLiteral("restored"), true);
	result.insert(QStringLiteral("plugin"), plugin);
	return ControlResult::success(result);
}

Effect* controlInstantiateDevice(const ControlDeviceEntry& entry, EffectChain* chain,
	ControlResult* error)
{
	if (!entry.loadable)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("device '%1' is a %2/%3 and this build has no host for it: only "
				"built-in effect and instrument modules and LADSPA effects load into a chain")
				.arg(entry.name, entry.format, entry.kind));
		return nullptr;
	}

	QString pluginName = entry.name;
	const Plugin::Descriptor* descriptor = findDescriptor(entry.name, Plugin::Type::Effect);
	Plugin::Descriptor::SubPluginFeatures::Key key;
	bool useKey = false;
	if (entry.format == QLatin1String("ladspa"))
	{
		// The whole LADSPA format is hosted by one plugin module; a LADSPA
		// device is selected by its key (file + label), not by plugin name.
		pluginName = QStringLiteral("ladspaeffect");
		descriptor = findDescriptor(pluginName, Plugin::Type::Effect);
		if (descriptor == nullptr)
		{
			*error = ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("the LADSPA host plugin 'ladspaeffect' is not in this build"));
			return nullptr;
		}
		key = ladspaKeyToSubPluginKey(descriptor, entry.displayName,
			ladspa_key_t(ladspaFileKey(entry.file), entry.label));
		useKey = true;
	}
	if (!controlPluginIsInstantiable(pluginName, error)) { return nullptr; }

	Effect* effect = Effect::instantiate(pluginName, chain, useKey ? &key : nullptr);
	if (effect == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the engine could not instantiate '%1'").arg(pluginName));
		return nullptr;
	}
	chain->appendEffect(effect);
	return effect;
}

} // namespace lmms
