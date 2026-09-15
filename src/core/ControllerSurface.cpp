/*
 * ControllerSurface.cpp - mapping templates and controller-surface state.
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

#include "ControllerSurface.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include "AutomatableModel.h"
#include "ConfigManager.h"
#include "ControllerConnection.h"
#include "Engine.h"
#include "MidiController.h"
#include "Song.h"

namespace lmms
{

QJsonObject ControllerTemplateBinding::toJson() const
{
	QJsonObject obj;
	obj.insert(QStringLiteral("channel"), channel);
	obj.insert(QStringLiteral("controller"), controller);
	obj.insert(QStringLiteral("target"), targetName);
	return obj;
}

ControllerTemplateBinding ControllerTemplateBinding::fromJson(const QJsonObject& obj)
{
	ControllerTemplateBinding b;
	b.channel = obj.value(QStringLiteral("channel")).toInt(1);
	b.controller = obj.value(QStringLiteral("controller")).toInt(0);
	b.targetName = obj.value(QStringLiteral("target")).toString();
	return b;
}

QJsonObject ControllerTemplate::toJson() const
{
	QJsonObject obj;
	obj.insert(QStringLiteral("name"), name);
	QJsonArray arr;
	for (const auto& b : bindings)
	{
		arr.append(b.toJson());
	}
	obj.insert(QStringLiteral("bindings"), arr);
	return obj;
}

ControllerTemplate ControllerTemplate::fromJson(const QJsonObject& obj)
{
	ControllerTemplate t;
	t.name = obj.value(QStringLiteral("name")).toString();
	const QJsonArray arr = obj.value(QStringLiteral("bindings")).toArray();
	for (const auto& v : arr)
	{
		t.bindings.append(ControllerTemplateBinding::fromJson(v.toObject()));
	}
	return t;
}

ControllerSurface& ControllerSurface::instance()
{
	static ControllerSurface s_instance;
	return s_instance;
}

QString ControllerSurface::templateDirectory()
{
	return ConfigManager::inst()->userConfigDir() + QStringLiteral("controller-templates");
}

QString ControllerSurface::templatePath(const QString& name) const
{
	return templateDirectory() + QDir::separator() + name + QStringLiteral(".json");
}

bool ControllerSurface::saveTemplate(const QString& name)
{
	QDir dir(templateDirectory());
	if (!dir.exists())
	{
		dir.mkpath(QStringLiteral("."));
	}

	ControllerTemplate t;
	t.name = name;
	t.bindings = currentBindings();

	QFile file(templatePath(name));
	if (!file.open(QIODevice::WriteOnly))
	{
		return false;
	}
	file.write(QJsonDocument(t.toJson()).toJson(QJsonDocument::Compact));
	return true;
}

QStringList ControllerSurface::listTemplates() const
{
	QDir dir(templateDirectory());
	if (!dir.exists())
	{
		return QStringList();
	}
	QStringList result;
	for (const QString& f : dir.entryList(QStringList() << QStringLiteral("*.json"), QDir::Files))
	{
		result.append(f.left(f.length() - 5)); // strip .json
	}
	result.sort();
	return result;
}

ControllerTemplate ControllerSurface::loadTemplate(const QString& name) const
{
	QFile file(templatePath(name));
	if (!file.open(QIODevice::ReadOnly))
	{
		return ControllerTemplate();
	}
	QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
	return ControllerTemplate::fromJson(doc.object());
}

int ControllerSurface::applyTemplate(const QString& name)
{
	const ControllerTemplate t = loadTemplate(name);
	if (t.name.isEmpty())
	{
		return 0;
	}

	int created = 0;
	for (const auto& b : t.bindings)
	{
		AutomatableModel* target = resolveTarget(b.targetName);
		if (target == nullptr)
		{
			continue;
		}

		auto* controller = new MidiController(nullptr);
		controller->midiPort().setInputChannel(b.channel);
		controller->midiPort().setInputController(b.controller);

		MidiPort::Map ports = controller->midiPort().readablePorts();
		for (auto it = ports.begin(); it != ports.end(); ++it)
		{
			it.value() = true;
		}
		controller->subscribeReadablePorts(ports);
		controller->updateName();
		controller->midiPort().setName(target->fullDisplayName());

		auto* connection = new ControllerConnection(controller);
		target->setControllerConnection(connection);

		++created;
	}

	if (created > 0 && Engine::getSong() != nullptr)
	{
		Engine::getSong()->setModified();
	}
	return created;
}

bool ControllerSurface::removeTemplate(const QString& name)
{
	return QFile::remove(templatePath(name));
}

QVector<ControllerTemplateBinding> ControllerSurface::currentBindings() const
{
	QVector<ControllerTemplateBinding> result;
	for (ControllerConnection* conn : ControllerConnection::connections())
	{
		auto* mc = dynamic_cast<MidiController*>(conn->getController());
		if (mc == nullptr) { continue; }

		ControllerTemplateBinding b;
		b.channel = mc->midiPort().inputChannel();
		b.controller = mc->midiPort().inputController();
		b.targetName = mc->midiPort().displayName();
		result.append(b);
	}
	return result;
}

AutomatableModel* ControllerSurface::resolveTarget(const QString& targetName) const
{
	if (targetName.isEmpty())
	{
		return nullptr;
	}
	Song* song = Engine::getSong();
	if (song == nullptr)
	{
		return nullptr;
	}
	// Breadth-first search over the QObject tree starting at the song.
	QList<QObject*> queue;
	queue.append(song);
	while (!queue.isEmpty())
	{
		QObject* obj = queue.takeFirst();
		auto* am = dynamic_cast<AutomatableModel*>(obj);
		if (am != nullptr && am->fullDisplayName() == targetName)
		{
			return am;
		}
		queue.append(obj->findChildren<QObject*>(QString(), Qt::FindDirectChildrenOnly));
	}
	return nullptr;
}

} // namespace lmms
