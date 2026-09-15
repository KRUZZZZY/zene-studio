/*
 * PatchWiring.cpp - the authored wiring of a derived routing graph.
 *
 * @see include/PatchWiring.h for what this is and for the three bounds it
 * states. This file is data and parsing only: no RoutingGraph, no EffectChain,
 * no audio path, which is why it can be tested without an Engine.
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

#include "PatchWiring.h"

#include <QJsonObject>
#include <QJsonValue>

namespace lmms
{

namespace
{

constexpr auto INPUT_TEXT = "input";
constexpr auto EFFECT_PREFIX = "effect:";

} // namespace

auto PatchRef::input() -> PatchRef
{
	PatchRef ref;
	ref.m_none = false;
	ref.m_input = true;
	return ref;
}

auto PatchRef::effect(int index) -> PatchRef
{
	PatchRef ref;
	ref.m_none = false;
	ref.m_index = index;
	return ref;
}

auto PatchRef::toString() const -> QString
{
	if (m_none) { return QString(); }
	if (m_input) { return QString::fromLatin1(INPUT_TEXT); }
	return QString::fromLatin1(EFFECT_PREFIX) + QString::number(m_index);
}

auto PatchRef::parse(const QString& text, PatchRef* ref, QString* error) -> bool
{
	const auto fail = [error](const QString& message) {
		if (error != nullptr) { *error = message; }
		return false;
	};
	if (ref == nullptr) { return fail(QStringLiteral("no reference to fill")); }

	const QString wanted = text.trimmed();
	if (wanted == QString::fromLatin1(INPUT_TEXT))
	{
		*ref = input();
		return true;
	}
	if (!wanted.startsWith(QString::fromLatin1(EFFECT_PREFIX)))
	{
		return fail(QStringLiteral("'%1' is not a node reference: use \"input\" or \"effect:<index>\"")
			.arg(text));
	}

	bool ok = false;
	const int index = wanted.mid(static_cast<int>(sizeof(EFFECT_PREFIX)) - 1).toInt(&ok);
	if (!ok || index < 0)
	{
		return fail(QStringLiteral("'%1' is not a node reference: an effect index is a "
			"non-negative integer").arg(text));
	}
	*ref = effect(index);
	return true;
}

auto PatchRef::equals(const PatchRef& other) const -> bool
{
	// An unset reference equals only another unset one: "names no node" is a
	// state, not "effect 0".
	if (m_none || other.m_none) { return m_none == other.m_none; }
	if (m_input != other.m_input) { return false; }
	return m_input || m_index == other.m_index;
}

void PatchWiring::clear()
{
	m_edges.clear();
	m_output = PatchRef();
}

auto PatchWiring::equals(const PatchWiring& other) const -> bool
{
	if (m_edges.size() != other.m_edges.size()) { return false; }
	if (!m_output.equals(other.m_output)) { return false; }

	for (std::size_t i = 0; i < m_edges.size(); ++i)
	{
		const PatchEdge& mine = m_edges[i];
		const PatchEdge& theirs = other.m_edges[i];
		if (mine.fromPort != theirs.fromPort || mine.toPort != theirs.toPort
			|| !mine.from.equals(theirs.from) || !mine.to.equals(theirs.to))
		{
			return false;
		}
	}
	return true;
}

auto PatchWiring::linear(int effectCount) -> PatchWiring
{
	PatchWiring wiring;
	if (effectCount <= 0) { return wiring; }

	wiring.addEdge(PatchEdge{PatchRef::input(), 0, PatchRef::effect(0), 0});
	for (int i = 1; i < effectCount; ++i)
	{
		wiring.addEdge(PatchEdge{PatchRef::effect(i - 1), 0, PatchRef::effect(i), 0});
	}
	wiring.setOutput(PatchRef::effect(effectCount - 1));
	return wiring;
}

auto PatchWiring::toJson() const -> QJsonArray
{
	QJsonArray edges;
	for (const PatchEdge& edge : m_edges)
	{
		QJsonObject entry;
		entry.insert(QStringLiteral("from"), edge.from.toString());
		entry.insert(QStringLiteral("from_port"), edge.fromPort);
		entry.insert(QStringLiteral("to"), edge.to.toString());
		entry.insert(QStringLiteral("to_port"), edge.toPort);
		edges.append(entry);
	}
	return edges;
}

auto PatchWiring::fromJson(const QJsonArray& array, PatchWiring* wiring, QString* error) -> bool
{
	const auto fail = [error](const QString& message) {
		if (error != nullptr) { *error = message; }
		return false;
	};
	if (wiring == nullptr) { return fail(QStringLiteral("no wiring to fill")); }

	// Parsed into a local first: a malformed entry halfway through must not leave
	// a half-built wiring behind.
	PatchWiring parsed;
	for (const QJsonValue& value : array)
	{
		if (!value.isObject()) { return fail(QStringLiteral("every edge must be an object")); }
		const QJsonObject entry = value.toObject();

		PatchEdge edge;
		if (!PatchRef::parse(entry.value(QStringLiteral("from")).toString(), &edge.from, error)
			|| !PatchRef::parse(entry.value(QStringLiteral("to")).toString(), &edge.to, error))
		{
			return false;
		}
		// A JSON integer, or the port the node has when the field is absent.
		const QJsonValue fromPort = entry.value(QStringLiteral("from_port"));
		const QJsonValue toPort = entry.value(QStringLiteral("to_port"));
		if (fromPort.isDouble()) { edge.fromPort = fromPort.toInt(); }
		if (toPort.isDouble()) { edge.toPort = toPort.toInt(); }
		parsed.addEdge(edge);
	}

	*wiring = parsed;
	return true;
}

} // namespace lmms
