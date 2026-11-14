/*
 * CarlaVst2Legacy.cpp - restore converted legacy VST2 state.
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

#include "CarlaVst2Legacy.h"
#include <QDebug>
#include "LocaleHelper.h"

namespace lmms::carlavst2
{
QString legacyStateTag()
{
	return QStringLiteral("zene-legacy-vst2");
}

void applyLegacyState(const QDomElement& legacy, const LegacyCalls& calls)
{
	bool ok = false;
	const int program = legacy.attribute("program").toInt(&ok);
	if (ok && program >= 0 && static_cast<uint32_t>(program) < calls.programCount())
	{
		calls.setProgram(program);
	}
	const QByteArray chunk = legacy.attribute("chunk").toUtf8();
	if (!chunk.isEmpty() && !QByteArray::fromBase64(chunk).isEmpty())
	{
		calls.setChunk(chunk.constData());
		return;
	}
	const int count = legacy.attribute("numparams").toInt();
	for (int i = 0; i < count; ++i)
	{
		const QString value = legacy.attribute(QStringLiteral("param") + QString::number(i));
		const int first = value.indexOf(':');
		const int last = value.lastIndexOf(':');
		bool indexOk = false, valueOk = false;
		const int index = value.left(first).toInt(&indexOk);
		const float number = LocaleHelper::toFloat(value.mid(last + 1), &valueOk);
		if (first < 0 || last <= first || !indexOk || index < 0 ||
			static_cast<uint32_t>(index) >= calls.parameterCount() || !valueOk)
		{
			qWarning() << "Invalid legacy VST2 parameter" << value;
			continue;
		}
		calls.setParameter(static_cast<uint32_t>(index), number);
	}
}

bool hasModelSettings(const QDomElement& state, const QString& name)
{
	if (!state.firstChildElement(name).isNull() || !state.firstChildElement("connection").firstChildElement(name).isNull())
	{
		return true;
	}
	for (auto child = state.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
		if (child.attribute("nodename") == name)
		{
			return true;
		}
	return false;
}

void warnExtraModelSettings(const QDomElement& state, std::size_t count)
{
	for (auto child = state.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
	{
		const QString name = child.tagName();
		if (!name.startsWith("PARAM_KNOB_"))
		{
			continue;
		}
		bool ok = false;
		const auto index = name.mid(11).toULongLong(&ok);
		if (ok && index >= count)
		{
			qWarning() << "Ignoring out-of-range VST2 model" << name;
			return;
		}
	}
}
}
