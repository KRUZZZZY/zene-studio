/*
 * LegacyVst2Conversion.cpp - legacy VST2 state conversion.
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

#include "LegacyVst2Conversion.h"

#include <atomic>

#include <QDomDocument>
#include <QFileInfo>
#include <QRegularExpression>

#include "PathUtil.h"
#include "PluginFactory.h"

namespace lmms::legacyvst2
{
namespace
{
using Key = Plugin::Descriptor::SubPluginFeatures::Key;
std::atomic<bool> enabled{true};

QString renamed(const QString& name)
{
	static const QRegularExpression pattern(QStringLiteral("^param([0-9]+)$"));
	const auto match = pattern.match(name);
	return match.hasMatch() ? QStringLiteral("PARAM_KNOB_") + match.captured(1) : name;
}

QDomNode copyRenamed(QDomDocument& doc, const QDomNode& source)
{
	if (!source.isElement()) { return doc.importNode(source, true); }
	const auto old = source.toElement();
	auto copy = doc.createElement(renamed(old.tagName()));
	const auto attrs = old.attributes();
	for (int i = 0; i < attrs.count(); ++i)
	{
		const auto attr = attrs.item(i);
		copy.setAttribute(attr.nodeName(), attr.nodeName() == QLatin1String("nodename")
			? renamed(attr.nodeValue()) : attr.nodeValue());
	}
	for (auto child = old.firstChild(); !child.isNull(); child = child.nextSibling())
	{
		copy.appendChild(copyRenamed(doc, child));
	}
	return copy;
}

QDomElement state(QDomDocument& doc, const QDomElement& old, const QString& name, const QString& host)
{
	auto result = doc.createElement(name);
	auto marker = doc.createElement(legacyStateTagName());
	marker.setAttribute("host", host);
	const auto attrs = old.attributes();
	for (int i = 0; i < attrs.count(); ++i)
	{
		const auto attr = attrs.item(i);
		const QString key = attr.nodeName();
		if (key == QLatin1String("chunk") || key == QLatin1String("program") ||
			key == QLatin1String("numparams") || renamed(key) != key)
		{
			marker.setAttribute(key, attr.nodeValue());
		}
	}
	result.appendChild(marker);
	for (auto child = old.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
	{
		// AudioPortsModel saves pins (inputs, outputs, track_channels, in_matrix, out_matrix).
		if (child.tagName() != QLatin1String("pins") && child.tagName() != QLatin1String("key")) { result.appendChild(copyRenamed(doc, child)); }
	}
	return result;
}

std::optional<Attributes> resolvePath(const QString& path, Plugin::Type type, const Resolver& resolver)
{
	if (path.isEmpty()) { return std::nullopt; }
	return resolver(PathUtil::toAbsolute(path), type);
}
}

QString legacyStateTagName() { return QStringLiteral("zene-legacy-vst2"); }
bool conversionEnabled() { return enabled.load(); }
void setConversionEnabled(bool value) { enabled.store(value); }

std::optional<Attributes> resolveFromFactory(const QString& absoluteFile, Plugin::Type type)
{
	const QFileInfo file(absoluteFile);
	if (!file.exists() || (type != Plugin::Type::Instrument && type != Plugin::Type::Effect)) { return std::nullopt; }
	const auto host = type == Plugin::Type::Instrument ? "carlavst2" : "carlavst2effect";
	for (const auto* descriptor : getPluginFactory()->descriptors(type))
	{
		if (!descriptor || QLatin1String(descriptor->name) != QLatin1String(host)) { continue; }
		if (!descriptor->subPluginFeatures) { return std::nullopt; }
		Plugin::Descriptor::SubPluginFeatures::KeyList keys;
		descriptor->subPluginFeatures->listSubPluginKeys(descriptor, keys);
		for (const auto& key : keys)
		{
			const auto candidate = QFileInfo(key.attributes.value(QStringLiteral("file"))).canonicalFilePath();
			if (!candidate.isEmpty() && candidate == file.canonicalFilePath()) { return key.attributes; }
		}
		return Attributes{{QStringLiteral("file"), absoluteFile}, {QStringLiteral("name"), file.completeBaseName()}};
	}
	return std::nullopt;
}

QDomElement convertInstrument(const QDomElement& node, const Resolver& resolve)
{
	if (!conversionEnabled() || node.tagName() != QLatin1String("instrument") || node.attribute("name") != QLatin1String("vestige")) { return {}; }
	const auto old = node.firstChildElement("vestige");
	const auto attrs = resolvePath(old.attribute("plugin"), Plugin::Type::Instrument, resolve);
	if (!attrs) { return {}; }
	auto doc = node.ownerDocument();
	auto result = doc.createElement("instrument");
	result.setAttribute("name", "carlavst2");
	auto converted = state(doc, old, QStringLiteral("carlavst2"), QStringLiteral("vestige"));
	converted.appendChild(Key(nullptr, QString(), *attrs).saveXML(doc));
	result.appendChild(converted);
	return result;
}

QDomElement convertEffect(const QDomElement& node, const Resolver& resolve)
{
	if (!conversionEnabled() || node.tagName() != QLatin1String("effect") || node.attribute("name") != QLatin1String("vsteffect")) { return {}; }
	const auto oldKey = node.firstChildElement("key");
	const auto controls = node.firstChildElement("vsteffectcontrols");
	QString path = Key(oldKey).attributes.value(QStringLiteral("file"));
	if (path.isEmpty()) { path = controls.attribute("plugin"); }
	const auto attrs = resolvePath(path, Plugin::Type::Effect, resolve);
	if (!attrs) { return {}; }
	auto doc = node.ownerDocument();
	auto result = node.cloneNode(false).toElement();
	result.setAttribute("name", "carlavst2effect");
	for (auto child = node.firstChild(); !child.isNull(); child = child.nextSibling())
	{
		if (child == oldKey) { result.appendChild(Key(nullptr, QString(), *attrs).saveXML(doc)); }
		else if (child == controls) { result.appendChild(state(doc, controls, QStringLiteral("carlavst2effectcontrols"), QStringLiteral("vsteffect"))); }
		else { result.appendChild(doc.importNode(child, true)); }
	}
	return result;
}
}
