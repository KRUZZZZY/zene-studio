/*
 * ProjectKey.cpp - the project's detected key (see include/ProjectKey.h for why
 *                  it exists and where its vocabulary comes from).
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

#include "ProjectKey.h"

#include <QDomDocument>
#include <QDomElement>

namespace lmms
{

namespace
{

//! The attribute names the element uses. Fixed strings, in one place, so the
//! writer, the reader and the tests cannot drift apart.
constexpr const char* kTonic = "tonic";
constexpr const char* kPitchClass = "pitch-class";
constexpr const char* kScale = "scale";
constexpr const char* kConfidence = "confidence";
constexpr const char* kMargin = "margin";
constexpr const char* kMethod = "method";
constexpr const char* kSource = "source";

QDomElement toElement(QDomDocument& doc, const ProjectKey& key)
{
	QDomElement element = doc.createElement(QString::fromLatin1(ProjectKey::ElementName));
	element.setAttribute(QString::fromLatin1(kTonic), key.tonicName());
	element.setAttribute(QString::fromLatin1(kPitchClass), key.tonicPitchClass());
	element.setAttribute(QString::fromLatin1(kScale), key.scaleName());
	element.setAttribute(QString::fromLatin1(kConfidence), key.confidence());
	element.setAttribute(QString::fromLatin1(kMargin), key.margin());
	element.setAttribute(QString::fromLatin1(kMethod), key.method());
	element.setAttribute(QString::fromLatin1(kSource), key.sourcePath());
	return element;
}

} // namespace


void ProjectKey::clear()
{
	m_tonicName.clear();
	m_tonicPitchClass = UnknownPitchClass;
	m_scaleName.clear();
	m_confidence = 0.0;
	m_margin = 0.0;
	m_method.clear();
	m_sourcePath.clear();
}


void ProjectKey::set(const QString& tonicName, int tonicPitchClass, const QString& scaleName,
	double confidence, double margin, const QString& method, const QString& sourcePath)
{
	m_tonicName = tonicName;
	m_tonicPitchClass = tonicPitchClass;
	m_scaleName = scaleName;
	m_confidence = confidence;
	m_margin = margin;
	m_method = method;
	m_sourcePath = sourcePath;
}


void ProjectKey::saveSettings(QDomDocument& doc, QDomElement& parent) const
{
	parent.appendChild(toElement(doc, *this));
}


bool ProjectKey::loadSettings(const QDomElement& element)
{
	// CLEARED FIRST, unconditionally: the state an ABSENT element has to restore
	// is the empty key (the reset-on-absence rule), and a caller that has just
	// loaded another project must not keep the previous one's key.
	clear();
	if (element.isNull() || element.tagName() != QString::fromLatin1(ElementName)) { return false; }

	const QString scale = element.attribute(QString::fromLatin1(kScale));
	if (scale.isEmpty()) { return false; }

	m_tonicName = element.attribute(QString::fromLatin1(kTonic));
	m_tonicPitchClass = element.attribute(QString::fromLatin1(kPitchClass), QStringLiteral("-1")).toInt();
	m_scaleName = scale;
	m_confidence = element.attribute(QString::fromLatin1(kConfidence)).toDouble();
	m_margin = element.attribute(QString::fromLatin1(kMargin)).toDouble();
	m_method = element.attribute(QString::fromLatin1(kMethod));
	m_sourcePath = element.attribute(QString::fromLatin1(kSource));
	return true;
}


QString ProjectKey::toXml() const
{
	// The element IS the document root, so the text is a well-formed document
	// fromXml() parses straight back (the shape GroovePool::toXml uses).
	QDomDocument doc(QStringLiteral("zene-detected-key"));
	doc.appendChild(toElement(doc, *this));
	return doc.toString();
}


bool ProjectKey::fromXml(const QString& xml)
{
	clear();
	QDomDocument doc;
	if (!doc.setContent(xml)) { return false; }
	return loadSettings(doc.documentElement());
}

} // namespace lmms
