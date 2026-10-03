/*
 * DeviceOptions.cpp - what a user sets on one device: parameter names, folding, editor scale, compatibility
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

#include "DeviceOptions.h"

#include <algorithm>
#include <cmath>

#include <QJsonDocument>
#include <QJsonObject>

namespace lmms
{

namespace
{

const QString kAliases = QStringLiteral("aliases");
const QString kFolded = QStringLiteral("folded");
const QString kEditorScale = QStringLiteral("editor-scale");
const QString kGenericEditor = QStringLiteral("generic-editor");
const QString kKeepRunning = QStringLiteral("keep-running");
const QString kLatency = QStringLiteral("latency-override");

void setFlag(QDomElement& element, const QString& name, bool on)
{
	if (on) { element.setAttribute(name, 1); }
}

bool flag(const QDomElement& element, const QString& name)
{
	return element.attribute(name, QStringLiteral("0")).toInt() != 0;
}

} // namespace

QString DeviceOptions::shownName(const QString& name) const
{
	const QString given = alias(name);
	return given.isEmpty() ? name : given;
}

void DeviceOptions::setAlias(const QString& name, const QString& alias)
{
	const QString trimmed = alias.trimmed().left(MaxAliasLength);
	if (trimmed.isEmpty() || trimmed == name) { m_aliases.remove(name); }
	else { m_aliases.insert(name, trimmed); }
}

bool DeviceOptions::isDefault() const
{
	return m_aliases.isEmpty() && !folded && editorScale == 1.f && !genericEditor && !keepRunning && latencyOverride < 0;
}

bool DeviceOptions::operator==(const DeviceOptions& other) const
{
	return m_aliases == other.m_aliases && folded == other.folded && editorScale == other.editorScale
		&& genericEditor == other.genericEditor && keepRunning == other.keepRunning && latencyOverride == other.latencyOverride;
}

void DeviceOptions::save(QDomElement& element) const
{
	if (!m_aliases.isEmpty())
	{
		QJsonObject names;
		for (auto it = m_aliases.cbegin(); it != m_aliases.cend(); ++it) { names.insert(it.key(), it.value()); }
		element.setAttribute(kAliases, QString::fromUtf8(QJsonDocument(names).toJson(QJsonDocument::Compact)));
	}
	setFlag(element, kFolded, folded);
	if (editorScale != 1.f) { element.setAttribute(kEditorScale, static_cast<double>(editorScale)); }
	setFlag(element, kGenericEditor, genericEditor);
	setFlag(element, kKeepRunning, keepRunning);
	if (latencyOverride >= 0) { element.setAttribute(kLatency, latencyOverride); }
}

void DeviceOptions::load(const QDomElement& element)
{
	m_aliases.clear();
	const QJsonObject names = QJsonDocument::fromJson(element.attribute(kAliases).toUtf8()).object();
	for (auto it = names.begin(); it != names.end(); ++it) { setAlias(it.key(), it.value().toString()); }
	folded = flag(element, kFolded);
	// A value that does not read as a number is the default, not a zero clamped into range.
	bool ok = false;
	const float scale = element.attribute(kEditorScale).toFloat(&ok);
	editorScale = ok && std::isfinite(scale) ? std::clamp(scale, MinEditorScale, MaxEditorScale) : 1.f;
	genericEditor = flag(element, kGenericEditor);
	keepRunning = flag(element, kKeepRunning);
	const int latency = element.attribute(kLatency).toInt(&ok);
	latencyOverride = ok ? std::clamp(latency, -1, MaxLatencyOverride) : -1;
}

} // namespace lmms
