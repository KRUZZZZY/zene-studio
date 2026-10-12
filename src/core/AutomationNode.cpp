/*
 * AutomationClip.cpp - Implementation of class AutomationNode which
 *                         holds information on a single automation clip node
 *
 * Copyright (c) 2020 Ian Caio <iancaio_dev/at/hotmail.com>
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

#include "AutomationNode.h"
#include "AutomationClip.h"

#include <algorithm>

#include <QDomElement>


namespace lmms
{

// Dummy constructor for the QMap
AutomationNode::AutomationNode() :
	m_clip(nullptr),
	m_pos(0),
	m_inValue(0),
	m_outValue(0),
	m_inTangent(0),
	m_outTangent(0),
	m_lockedTangents(false)
{
}

AutomationNode::AutomationNode(AutomationClip* clip, float value, int pos) :
	m_clip(clip),
	m_pos(pos),
	m_inValue(value),
	m_outValue(value),
	m_inTangent(0),
	m_outTangent(0),
	m_lockedTangents(false)
{
}

AutomationNode::AutomationNode(AutomationClip* clip, float inValue, float outValue, int pos) :
	m_clip(clip),
	m_pos(pos),
	m_inValue(inValue),
	m_outValue(outValue),
	m_inTangent(0),
	m_outTangent(0),
	m_lockedTangents(false)
{
}

/**
 * @brief Sets the inValue of an automation node
 * @param Float value to be assigned
*/
void AutomationNode::setInValue(float value)
{
	m_inValue = value;

	// Recalculate the tangents from neighbor nodes
	AutomationClip::timeMap & tm = m_clip->getTimeMap();

	// Get an iterator pointing to this node
	AutomationClip::timeMap::iterator it = tm.lowerBound(m_pos);
	// If it's not the first node, get the one immediately behind it
	if (it != tm.begin()) { --it; }

	// Generate tangents from the previously, current and next nodes
	m_clip->generateTangents(it, 3);
}

/**
 * @brief Sets the outValue of an automation node
 * @param Float value to be assigned
*/
void AutomationNode::setOutValue(float value)
{
	m_outValue = value;

	// Recalculate the tangents from neighbor nodes
	AutomationClip::timeMap & tm = m_clip->getTimeMap();

	// Get an iterator pointing to this node
	AutomationClip::timeMap::iterator it = tm.lowerBound(m_pos);
	// If it's not the first node, get the one immediately behind it
	if (it != tm.begin()) { --it; }

	// Generate tangents from the previously, current and next nodes
	m_clip->generateTangents(it, 3);
}

/**
 * @brief Resets the outValue so it matches inValue
*/
void AutomationNode::resetOutValue()
{
	// Calls setOutValue so it also takes care of generating
	// the tangents
	setOutValue(m_inValue);
}

void AutomationNode::setCurve(Curve curve, float bend)
{
	m_curve = curve;
	m_bend = std::clamp(bend, -1.f, 1.f);
}

void AutomationNode::saveCurve(QDomElement& element) const
{
	if (m_curve == Curve::Clip) { return; }
	element.setAttribute("curve", static_cast<int>(m_curve));
	if (m_curve == Curve::Bend) { element.setAttribute("bend", static_cast<double>(m_bend)); }
}

void AutomationNode::loadCurve(const QDomElement& element)
{
	const int curve = std::clamp(element.attribute("curve", "0").toInt(), 0, static_cast<int>(Curve::Bend));
	setCurve(static_cast<Curve>(curve), element.attribute("bend", "0").toFloat());
}

} // namespace lmms