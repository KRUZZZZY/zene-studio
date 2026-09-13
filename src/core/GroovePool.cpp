/*
 * GroovePool.cpp - the project's named grooves: lookup, replace, rename and
 *                  the <groove-pool> element
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

#include "GroovePool.h"

#include <QDomDocument>
#include <QDomElement>

namespace lmms
{

namespace
{

const QString kPoolElement = QStringLiteral("groove-pool");
const QString kGrooveElement = QStringLiteral("groove");

} // namespace


int GroovePool::indexOf(const QString& name) const
{
	for (int index = 0; index < size(); ++index)
	{
		if (m_templates[static_cast<std::size_t>(index)].name() == name) { return index; }
	}
	return -1;
}


const GrooveTemplate* GroovePool::find(const QString& name) const
{
	const int index = indexOf(name);
	return index < 0 ? nullptr : &m_templates[static_cast<std::size_t>(index)];
}


bool GroovePool::set(const GrooveTemplate& groove, bool* replaced)
{
	if (replaced != nullptr) { *replaced = false; }
	if (!groove.valid()) { return false; }

	const int existing = indexOf(groove.name());
	if (existing >= 0)
	{
		m_templates[static_cast<std::size_t>(existing)] = groove;
		if (replaced != nullptr) { *replaced = true; }
		return true;
	}
	if (size() >= MaxTemplates) { return false; }

	m_templates.push_back(groove);
	return true;
}


bool GroovePool::remove(const QString& name)
{
	const int index = indexOf(GrooveTemplate::normalisedName(name));
	if (index < 0) { return false; }
	m_templates.erase(m_templates.begin() + index);
	return true;
}


bool GroovePool::rename(const QString& from, const QString& to)
{
	const QString wanted = GrooveTemplate::normalisedName(to);
	if (wanted.isEmpty()) { return false; }

	const QString current = GrooveTemplate::normalisedName(from);
	const int index = indexOf(current);
	if (index < 0) { return false; }
	if (wanted == current) { return true; }

	// A rename onto a DIFFERENT template's name would silently destroy it (the
	// name is the key), so it is refused and the caller reports it.
	const int taken = indexOf(wanted);
	if (taken >= 0 && taken != index) { return false; }

	const GrooveTemplate& existing = m_templates[static_cast<std::size_t>(index)];
	GrooveTemplate renamed(wanted, existing.lengthTicks(), existing.stepTicks());
	for (int slot = 0; slot < existing.slotCount(); ++slot)
	{
		renamed.setStep(slot, existing.step(slot));
	}
	m_templates[static_cast<std::size_t>(index)] = renamed;
	return true;
}


QDomElement GroovePool::toElement(QDomDocument& doc) const
{
	QDomElement poolElement = doc.createElement(kPoolElement);
	poolElement.setAttribute(QStringLiteral("grooves"), size());
	poolElement.setAttribute(QStringLiteral("max-grooves"), MaxTemplates);
	for (const GrooveTemplate& groove : m_templates)
	{
		groove.saveXml(doc, poolElement);
	}
	return poolElement;
}


void GroovePool::saveSettings(QDomDocument& doc, QDomElement& parent) const
{
	parent.appendChild(toElement(doc));
}


bool GroovePool::loadSettings(const QDomElement& element)
{
	// CLEARED FIRST, unconditionally - see the header: the state an absent
	// element has to restore is the empty pool.
	clear();
	if (element.isNull() || element.tagName() != kPoolElement) { return false; }

	for (QDomElement child = element.firstChildElement(kGrooveElement); !child.isNull();
		child = child.nextSiblingElement(kGrooveElement))
	{
		GrooveTemplate groove;
		if (!groove.loadXml(child)) { continue; }
		set(groove, nullptr);
	}
	return true;
}


QString GroovePool::toXml() const
{
	// The pool element IS the document root, so the text is a well-formed
	// document fromXml() can parse straight back (and a reader can diff).
	QDomDocument doc(QStringLiteral("zene-groove-pool"));
	doc.appendChild(toElement(doc));
	return doc.toString();
}


bool GroovePool::fromXml(const QString& xml)
{
	clear();
	QDomDocument doc;
	if (!doc.setContent(xml)) { return false; }
	return loadSettings(doc.documentElement());
}

} // namespace lmms
