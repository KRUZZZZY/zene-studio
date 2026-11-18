/*
 * PresetBindings.cpp - the controller bindings a track or chain preset carries (Q050-331)
 *                                       reversibility contract (0.5.0 F6)
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

#include "PresetBindings.h"

#include <QDomDocument>
#include <QDomElement>
#include <QStringList>

#include "AudioBusHandle.h"
#include "AutomatableModel.h"
#include "ControllerConnection.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "MidiController.h"
#include "SampleTrack.h"

namespace lmms::presetbindings
{

namespace
{

const QString& manifestName()
{
	static const QString name = QStringLiteral("zenebindings");
	return name;
}

//! @a model's display path below @a root - the display names of the models between them, by object parent
//! ("TripleOscillator>Osc 1 volume") - or "" when it is not below it. Not fullDisplayName(): an instrument's
//! parameters do not carry their track's name, so that path cannot be cut at the root.
QString relativeName(const AutomatableModel* model, const Model* root)
{
	QStringList parts;
	const QObject* object = model;
	while (object != nullptr && object != root)
	{
		if (const auto* named = dynamic_cast<const Model*>(object); named != nullptr && !named->displayName().isEmpty())
		{
			parts.prepend(named->displayName());
		}
		object = object->parent();
	}
	return object == root ? parts.join(QLatin1Char('>')) : QString();
}

const MidiController* midiControllerOf(const AutomatableModel* model)
{
	ControllerConnection* connection = model->controllerConnection();
	return connection != nullptr ? dynamic_cast<const MidiController*>(connection->getController()) : nullptr;
}

QJsonObject entryOf(const Binding& binding, const QJsonValue& device)
{
	QJsonObject entry{{QStringLiteral("target"), binding.target}, {QStringLiteral("channel"), binding.channel},
		{QStringLiteral("controller"), binding.controller}, {QStringLiteral("soft_takeover"), binding.softTakeover},
		{QStringLiteral("feedback"), binding.feedback}};
	if (!device.isUndefined() && !device.isNull()) { entry.insert(QStringLiteral("device"), device); }
	return entry;
}

} // namespace

QList<Binding> collect(const Model* root, const QString& prefix)
{
	QList<Binding> out;
	if (root == nullptr) { return out; }
	QList<const QObject*> queue{root};
	while (!queue.isEmpty())
	{
		const QObject* object = queue.takeFirst();
		for (const QObject* child : object->children()) { queue.append(child); }
		const auto* model = dynamic_cast<const AutomatableModel*>(object);
		const MidiController* midi = model != nullptr ? midiControllerOf(model) : nullptr;
		if (midi == nullptr) { continue; }
		const QString name = relativeName(model, root);
		if (name.isEmpty()) { continue; }
		out.append(Binding{prefix + name, midi->midiPort().inputChannel(), midi->midiPort().inputController(),
			midi->softTakeoverEnabled(), midi->feedbackEnabled()});
	}
	return out;
}

QList<Binding> collectTrack(Track* track)
{
	QList<Binding> out = collect(track);
	if (auto* instrumentTrack = dynamic_cast<InstrumentTrack*>(track); instrumentTrack != nullptr
		&& instrumentTrack->instrument() != nullptr)
	{
		out += collect(instrumentTrack->instrument(), QStringLiteral("instrument>"));
	}
	AudioBusHandle* bus = nullptr;
	if (auto* instrumentTrack = dynamic_cast<InstrumentTrack*>(track)) { bus = instrumentTrack->audioBusHandle(); }
	else if (auto* sampleTrack = dynamic_cast<SampleTrack*>(track)) { bus = sampleTrack->audioBusHandle(); }
	EffectChain* chain = bus != nullptr ? bus->effects() : nullptr;
	if (chain == nullptr) { return out; }
	int position = 0;
	for (Effect* effect : chain->effects())
	{
		out += collect(effect, QStringLiteral("effect %1>").arg(++position));
	}
	return out;
}

void write(QDomDocument& doc, QDomElement& parent, const QList<Binding>& bindings)
{
	if (bindings.isEmpty()) { return; }
	QDomElement list = doc.createElement(manifestName());
	for (const Binding& binding : bindings)
	{
		QDomElement item = doc.createElement(QStringLiteral("binding"));
		item.setAttribute(QStringLiteral("target"), binding.target);
		item.setAttribute(QStringLiteral("channel"), binding.channel);
		item.setAttribute(QStringLiteral("controller"), binding.controller);
		item.setAttribute(QStringLiteral("soft_takeover"), binding.softTakeover ? 1 : 0);
		item.setAttribute(QStringLiteral("feedback"), binding.feedback ? 1 : 0);
		list.appendChild(item);
	}
	parent.appendChild(list);
}

QList<Binding> read(const QDomElement& parent, bool* present)
{
	QList<Binding> out;
	const QDomElement list = parent.firstChildElement(manifestName());
	if (present != nullptr) { *present = !list.isNull(); }
	for (QDomElement item = list.firstChildElement(QStringLiteral("binding")); !item.isNull();
		item = item.nextSiblingElement(QStringLiteral("binding")))
	{
		out.append(Binding{item.attribute(QStringLiteral("target")), item.attribute(QStringLiteral("channel")).toInt(),
			item.attribute(QStringLiteral("controller")).toInt(),
			item.attribute(QStringLiteral("soft_takeover")).toInt() != 0,
			item.attribute(QStringLiteral("feedback")).toInt() != 0});
	}
	return out;
}

void reconcile(const QList<Binding>& bindings, const QList<Binding>& live, const QJsonValue& device,
	QJsonArray* restored, QJsonArray* skipped)
{
	for (const Binding& saved : bindings)
	{
		bool found = false;
		for (const Binding& now : live)
		{
			if (now.target == saved.target && now.channel == saved.channel && now.controller == saved.controller)
			{
				found = true;
				restored->append(entryOf(now, device));
				break;
			}
		}
		if (found) { continue; }
		QJsonObject entry = entryOf(saved, device);
		entry.insert(QStringLiteral("reason"), QStringLiteral("the loaded device has no parameter '%1'; the binding was "
			"skipped and no mapping was made").arg(saved.target));
		skipped->append(entry);
	}
}

QJsonObject report(int saved, const QJsonArray& restored, const QJsonArray& skipped)
{
	QJsonObject out{{QStringLiteral("saved"), saved}, {QStringLiteral("restored"), restored},
		{QStringLiteral("skipped"), skipped}};
	out.insert(QStringLiteral("summary"), summary(out));
	return out;
}

QString summary(const QJsonObject& report)
{
	const QJsonArray restored = report.value(QStringLiteral("restored")).toArray();
	const QJsonArray skipped = report.value(QStringLiteral("skipped")).toArray();
	if (restored.isEmpty() && skipped.isEmpty()) { return QStringLiteral("no controller bindings in the preset"); }
	const auto describe = [](const QJsonArray& entries) {
		QStringList parts;
		for (const QJsonValue& value : entries)
		{
			const QJsonObject entry = value.toObject();
			parts << QStringLiteral("%1 (channel %2, CC %3)").arg(entry.value(QStringLiteral("target")).toString())
				.arg(entry.value(QStringLiteral("channel")).toInt()).arg(entry.value(QStringLiteral("controller")).toInt());
		}
		return parts.join(QStringLiteral(", "));
	};
	QString text = QStringLiteral("%1 of %2 controller bindings restored").arg(restored.size())
		.arg(restored.size() + skipped.size());
	if (!restored.isEmpty()) { text += QStringLiteral(": ") + describe(restored); }
	if (!skipped.isEmpty()) { text += QStringLiteral("; skipped (no such parameter): ") + describe(skipped); }
	return text;
}

} // namespace lmms::presetbindings
