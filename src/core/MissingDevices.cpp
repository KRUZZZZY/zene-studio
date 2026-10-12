/*
 * MissingDevices.cpp - the devices a project asked for that this session could not load
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

#include "MissingDevices.h"

#include <QDomDocument>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

#include "AudioBusHandle.h"
#include "DummyEffect.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "PluginVendors.h"
#include "SampleTrack.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

QJsonObject MissingDevice::toJson() const
{
	return QJsonObject{{QStringLiteral("kind"), kind}, {QStringLiteral("name"), name},
		{QStringLiteral("format"), format}, {QStringLiteral("vendor"), vendor},
		{QStringLiteral("track"), track}, {QStringLiteral("reason"), reason},
		{QStringLiteral("identity"), identity}};
}

namespace missingdevices
{

QString formatOf(const QString& moduleName)
{
	const QString n = moduleName.toLower();
	if (n == QLatin1String("vestige") || n == QLatin1String("vsteffect")) { return QStringLiteral("vst"); }
	if (n == QLatin1String("vst3instrument") || n == QLatin1String("vst3effect")) { return QStringLiteral("vst3"); }
	if (n == QLatin1String("clapinstrument") || n == QLatin1String("clapeffect")) { return QStringLiteral("clap"); }
	if (n == QLatin1String("lv2instrument") || n == QLatin1String("lv2effect")) { return QStringLiteral("lv2"); }
	return QStringLiteral("native");
}

QDomElement detachedCopy(const QDomElement& element)
{
	if (element.isNull()) { return {}; }
	QDomDocument doc;
	const QDomNode copy = doc.importNode(element, true);
	doc.appendChild(copy);
	return doc.documentElement();
}

namespace
{

QMap<QString, QString> keyAttributes(const QDomElement& original)
{
	QMap<QString, QString> attrs;
	const QDomNodeList keys = original.elementsByTagName(QStringLiteral("key"));
	if (keys.isEmpty()) { return attrs; }
	const QDomNodeList list = keys.item(0).toElement().elementsByTagName(QStringLiteral("attribute"));
	for (int i = 0; i < list.size(); ++i)
	{
		const QDomElement a = list.item(i).toElement();
		attrs.insert(a.attribute(QStringLiteral("name")), a.attribute(QStringLiteral("value")));
	}
	return attrs;
}

//! The inner element a plug-in wrote its own state into (<vestige plugin="...">), or a null element.
QDomElement innerElement(const QDomElement& original, const QString& moduleName)
{
	if (original.tagName() == moduleName) { return original; }
	return original.firstChildElement(moduleName);
}

} // namespace

namespace
{

//! The display name and the vendor the plug-in scan knows, for one format (either may stay empty).
struct Named
{
	QString name;
	QString scanVendor;
};

Named namedVst3(const QMap<QString, QString>& attrs)
{
	const QString name = attrs.value(QStringLiteral("class"));
	return {name, pluginvendors::vendorOf(attrs.value(QStringLiteral("file")), name)};
}

Named namedClap(const QMap<QString, QString>& attrs)
{
	const QString name = attrs.value(QStringLiteral("name"));
	return {name.isEmpty() ? attrs.value(QStringLiteral("id")) : name,
		pluginvendors::vendorOf(attrs.value(QStringLiteral("file")), attrs.value(QStringLiteral("id")))};
}

Named namedLv2(const QMap<QString, QString>& attrs)
{
	const QString uri = attrs.value(QStringLiteral("uri"));
	const QString tail = uri.section(QRegularExpression(QStringLiteral("[/#]")), -1, -1, QString::SectionSkipEmpty);
	return {tail.isEmpty() ? uri : tail, QUrl(uri).host()};
}

Named namedVst(const QDomElement& original, const QString& moduleName, QStringList& identity)
{
	const QString file = innerElement(original, moduleName).attribute(QStringLiteral("plugin"));
	identity << QStringLiteral("plugin=") + file;
	return {QFileInfo(file).completeBaseName(), QString()};
}

Named namedFor(const QString& format, const QDomElement& original, const QString& moduleName,
	const QMap<QString, QString>& attrs, QStringList& identity)
{
	if (format == QLatin1String("vst3")) { return namedVst3(attrs); }
	if (format == QLatin1String("clap")) { return namedClap(attrs); }
	if (format == QLatin1String("lv2")) { return namedLv2(attrs); }
	if (format == QLatin1String("vst")) { return namedVst(original, moduleName, identity); }
	return {QString(), QStringLiteral("LMMS")};
}

//! The first non-empty of the vendor the file carries, then what the scan or the URI suggests, else "unknown".
QString pickVendor(const QString& fromFile, const QString& fromScan)
{
	if (!fromFile.isEmpty()) { return fromFile; }
	return fromScan.isEmpty() ? QStringLiteral("unknown") : fromScan;
}

} // namespace

MissingDevice describe(const QDomElement& original, const QString& kind, const QString& track, const QString& reason)
{
	MissingDevice d;
	d.kind = kind;
	d.track = track;
	d.reason = reason;
	// An old compat-format project names the module by the element itself, with no name attribute.
	const QString moduleName = original.hasAttribute(QStringLiteral("name"))
		? original.attribute(QStringLiteral("name")) : original.tagName();
	d.format = formatOf(moduleName);

	const auto attrs = keyAttributes(original);
	QStringList identity{moduleName};
	for (auto it = attrs.constBegin(); it != attrs.constEnd(); ++it) { identity << it.key() + QLatin1Char('=') + it.value(); }

	const Named named = namedFor(d.format, original, moduleName, attrs, identity);
	d.name = named.name.isEmpty() ? moduleName : named.name;
	d.vendor = pickVendor(original.attribute(QStringLiteral("vendor")).trimmed(), named.scanVendor);
	d.identity = identity.join(QLatin1Char(';'));
	return d;
}

namespace
{

void collectChain(EffectChain* chain, const QString& where, QList<MissingDevice>& out)
{
	if (chain == nullptr) { return; }
	for (const Effect* e : chain->effects())
	{
		if (const auto* dummy = dynamic_cast<const DummyEffect*>(e))
		{
			out.append(describe(dummy->originalPluginData(), QStringLiteral("effect"), where,
				QStringLiteral("the effect plug-in module was not found or could not be created")));
		}
		else if (!e->preservedPluginData().isNull())
		{
			out.append(describe(e->preservedPluginData(), QStringLiteral("effect"), where, e->unavailableReason()));
		}
	}
}

} // namespace

QList<MissingDevice> collect(Song* song)
{
	QList<MissingDevice> out;
	if (song == nullptr) { return out; }
	for (Track* t : song->tracks())
	{
		if (auto* it = dynamic_cast<InstrumentTrack*>(t))
		{
			if (!it->preservedInstrument().isNull() && it->instrument() != nullptr)
			{
				out.append(describe(it->preservedInstrument(), QStringLiteral("instrument"), it->name(),
					it->instrument()->unavailableReason()));
			}
			collectChain(it->audioBusHandle()->effects(), it->name(), out);
		}
		else if (auto* st = dynamic_cast<SampleTrack*>(t))
		{
			collectChain(st->audioBusHandle()->effects(), st->name(), out);
		}
	}
	if (Mixer* mixer = Engine::mixer())
	{
		for (int i = 0; i < static_cast<int>(mixer->numChannels()); ++i)
		{
			collectChain(&mixer->mixerChannel(i)->m_fxChain, QStringLiteral("mixer channel %1").arg(i), out);
		}
	}
	return out;
}

QString noticeText(const QList<MissingDevice>& devices)
{
	if (devices.isEmpty()) { return {}; }
	QStringList lines;
	for (const MissingDevice& d : devices)
	{
		lines << QStringLiteral("%1: %2 (format %3, vendor %4) on %5").arg(d.kind, d.name, d.format, d.vendor, d.track);
	}
	return QStringLiteral("%1 device(s) could not be loaded. Their original data is kept and saved back unchanged; "
		"install the plug-in and reopen the project to restore them.\n\n%2")
		.arg(devices.size()).arg(lines.join(QLatin1Char('\n')));
}

} // namespace missingdevices
} // namespace lmms
