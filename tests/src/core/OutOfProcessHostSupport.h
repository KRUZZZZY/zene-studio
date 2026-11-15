/*
 * OutOfProcessHostSupport.h - what the two `oop.*` test sources share
 *                             (feature row 80, board card #670)
 *
 * Header-only, like its BrowserTestSupport.h / RackTestSupport.h /
 * ReversibilityTestSupport.h neighbours: the helpers are small, and a header
 * keeps them out of the LMMS_TESTS loop, which creates one test EXECUTABLE per
 * source and would have nothing to link a bare support translation unit to.
 *
 * The probes read the surface the way a client does: every answer below comes
 * out of a registered `oop.*` command, never out of the engine's internals, so
 * a case that passes is one a socket client could reproduce.
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

#ifndef LMMS_TESTS_OUT_OF_PROCESS_HOST_SUPPORT_H
#define LMMS_TESTS_OUT_OF_PROCESS_HOST_SUPPORT_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "ControlRegistry.h"

namespace ooptest
{

//! The client executable the family table gives the ZynAddSubFx family
inline constexpr auto ZynClient = "RemoteZynAddSubFx";
//! The client executable the family table gives the VST2 family
inline constexpr auto Vst2Client = "RemoteVstPlugin";

#ifdef OUTOFPROC_ZYN_PLUGIN_PATH
inline auto zynModulePath() -> QString { return QStringLiteral(OUTOFPROC_ZYN_PLUGIN_PATH); }
#else
inline auto zynModulePath() -> QString { return {}; }
#endif

#ifdef OUTOFPROC_ZYN_CLIENT_PATH
inline auto zynClientPath() -> QString { return QStringLiteral(OUTOFPROC_ZYN_CLIENT_PATH); }
#else
inline auto zynClientPath() -> QString { return {}; }
#endif

//! The five ids of the group, in the order the registry registers them
inline auto oopIds() -> QStringList
{
	return {
		QStringLiteral("oop.get_state"),
		QStringLiteral("oop.list_families"),
		QStringLiteral("oop.set_mode"),
		QStringLiteral("oop.restart"),
		QStringLiteral("oop.reset_crashes"),
	};
}

//! The two reads; the other three write
inline auto oopReads() -> QStringList
{
	return {QStringLiteral("oop.get_state"), QStringLiteral("oop.list_families")};
}

//! The family of one key, as a JSON object out of a command result
inline auto familyOf(const QJsonArray& families, const QString& key) -> QJsonObject
{
	for (const QJsonValue& value : families)
	{
		const QJsonObject family = value.toObject();
		if (family.value(QStringLiteral("key")).toString() == key) { return family; }
	}
	return {};
}

//! The `hosting` object `oop.get_state` reports for one addressed device (an
//! EMPTY object when the device is not in the answer at all)
inline auto deviceHosting(lmms::ControlRegistry* registry, const QJsonObject& address) -> QJsonObject
{
	const lmms::ControlResult state = registry->invoke(QStringLiteral("oop.get_state"), QJsonObject());
	if (!state.ok) { return {}; }
	for (const QJsonValue& chain : state.result.value(QStringLiteral("chains")).toArray())
	{
		const QJsonObject object = chain.toObject();
		if (object.value(QStringLiteral("id")).toString()
			!= address.value(QStringLiteral("target")).toString())
		{
			continue;
		}
		// The addressed device is either the chain's `inst` entry or one of its
		// fx-<n> instances; the surface reports both in the same shape.
		const QString device = address.value(QStringLiteral("plugin")).toString();
		if (device == QLatin1String("inst"))
		{
			return object.value(QStringLiteral("instrument")).toObject()
				.value(QStringLiteral("hosting")).toObject();
		}
		for (const QJsonValue& entry : object.value(QStringLiteral("devices")).toArray())
		{
			if (entry.toObject().value(QStringLiteral("device")).toString() == device)
			{
				return entry.toObject().value(QStringLiteral("hosting")).toObject();
			}
		}
	}
	return {};
}

//! The pid the SURFACE reports for the addressed device, 0 when none
inline auto liveClientPid(lmms::ControlRegistry* registry, const QJsonObject& address) -> qint64
{
	return deviceHosting(registry, address).value(QStringLiteral("client_process_id"))
		.toVariant().toLongLong();
}

//! One string field of the addressed device's resolved hosting
inline auto hostingField(lmms::ControlRegistry* registry, const QJsonObject& address,
	const QString& field) -> QString
{
	return deviceHosting(registry, address).value(field).toString();
}

//! The record the surface keeps for one client executable
inline auto clientRecord(lmms::ControlRegistry* registry, const QString& client) -> QJsonObject
{
	const lmms::ControlResult state = registry->invoke(QStringLiteral("oop.get_state"), QJsonObject());
	if (!state.ok) { return {}; }
	for (const QJsonValue& value : state.result.value(QStringLiteral("clients")).toArray())
	{
		const QJsonObject record = value.toObject();
		if (record.value(QStringLiteral("client")).toString() == client) { return record; }
	}
	return {};
}

} // namespace ooptest

#endif // LMMS_TESTS_OUT_OF_PROCESS_HOST_SUPPORT_H
