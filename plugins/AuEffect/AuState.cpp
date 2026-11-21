/*
 * AuState.cpp - an Audio Unit's saved state in a project (Q050-491, macOS only)
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

#include <QDebug>
#include <QDomDocument>
#include <QDomElement>

#include "AuParameter.h"
#include "AudioEngine.h"
#include "Engine.h"

namespace lmms
{

void saveAuState(au::HostedUnit* unit, QDomDocument& doc, QDomElement& element)
{
	QByteArray state;
	if (!unit->saveState(&state)) { return; }
	auto stateElement = doc.createElement(QStringLiteral("state"));
	stateElement.appendChild(doc.createTextNode(QString::fromLatin1(state.toBase64())));
	element.appendChild(stateElement);
}

bool loadAuState(au::HostedUnit* unit, const QDomElement& element)
{
	const auto stateElement = element.firstChildElement(QStringLiteral("state"));
	if (stateElement.isNull() || !unit->isLoaded()) { return false; }
	AudioEngine* engine = Engine::audioEngine();
	// Not while the unit renders: the engine is held for the property write.
	if (engine != nullptr) { engine->requestChangeInModel(); }
	QString error;
	const bool loaded = unit->loadState(QByteArray::fromBase64(stateElement.text().toLatin1()), &error);
	if (engine != nullptr) { engine->doneChangeInModel(); }
	if (!loaded) { qWarning() << "AU:" << error; }
	return loaded;
}

} // namespace lmms
