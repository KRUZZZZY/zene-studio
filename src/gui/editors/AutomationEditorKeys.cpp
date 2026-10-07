/*
 * AutomationEditorKeys.cpp - the automation editor's keyboard point editing (ED-194)
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


/*! ED-194: the automation editor edited from the keyboard. The arrows used to scroll only; now Ctrl+Left/Right choose
 *  a point (from none: the first, or the last), and while one is chosen Left/Right move it a grid step (Shift: four)
 *  and Up/Down change its value (Shift: coarsely) - each key ONE edit through AutomationPointEdit.h, so one undo step,
 *  and a refusal (a point already there) is spoken. Escape lets go of the point and the arrows scroll again. The chosen
 *  point is ringed. Ctrl+Shift+L opens the points as an editable list (window.item_list "automation"). */

#include <algorithm>

#include <QKeyEvent>
#include <QPainter>

#include "AccessibleWidgets.h"
#include "AutomationClip.h"
#include "AutomationEditor.h"
#include "AutomationPointEdit.h"
#include "ControlRegistry.h"
#include "MusicalTime.h"

namespace lmms::gui
{

namespace
{

constexpr int CoarseSteps = 4;   //!< grid steps a Shift+Left/Right moves a point
constexpr int RingRadius = 7;    //!< px, the ring around the chosen point

//! Ctrl+Shift+L: the points as an editable list.
bool isListKey(int key, Qt::KeyboardModifiers mods)
{
	return key == Qt::Key_L && mods == (Qt::ControlModifier | Qt::ShiftModifier);
}

//! Ctrl+Right / Ctrl+Left: choose the next (1) or previous (-1) point; 0 for any other key.
int choiceDirection(int key, Qt::KeyboardModifiers mods)
{
	if (mods != Qt::ControlModifier) { return 0; }
	return key == Qt::Key_Right ? 1 : key == Qt::Key_Left ? -1 : 0;
}

//! Right and Up go forward (1), Left and Down back (-1); 0 for any other key.
int arrowSign(int key)
{
	switch (key)
	{
		case Qt::Key_Right:
		case Qt::Key_Up: return 1;
		case Qt::Key_Left:
		case Qt::Key_Down: return -1;
		default: return 0;
	}
}

} // namespace

int AutomationEditor::keyPoint() const
{
	if (m_clip == nullptr || m_keyTick < 0 || !m_clip->getTimeMap().contains(TimePos(m_keyTick))) { return -1; }
	return m_keyTick;
}

void AutomationEditor::setKeyPoint(int tick)
{
	if (tick == m_keyTick) { return; }
	m_keyTick = tick;
	if (keyPoint() >= 0)
	{
		const float value = m_clip->getTimeMap().value(TimePos(tick)).getInValue();
		setAccessibleDescription(tr("Point at %1: %2").arg(musicaltime::positionText(tick)).arg(static_cast<double>(value)));
	}
	emit keyPointChanged();
	QWidget::update();
}

bool AutomationEditor::editPointByKey(QKeyEvent* ke)
{
	if (m_clip == nullptr) { return false; }
	const Qt::KeyboardModifiers mods = ke->modifiers() & ~Qt::KeypadModifier;
	if (isListKey(ke->key(), mods))
	{
		ControlRegistry::instance()->invoke(QStringLiteral("window.item_list"), {{QStringLiteral("surface"), QStringLiteral("automation")}});
		return true;
	}
	if (const int direction = choiceDirection(ke->key(), mods); direction != 0)
	{
		stepKeyPoint(direction);
		return true;
	}
	if (keyPoint() < 0) { return false; }
	if (ke->key() == Qt::Key_Escape && mods == Qt::NoModifier)
	{
		setKeyPoint(-1);
		return true;
	}
	if (mods != Qt::NoModifier && mods != Qt::ShiftModifier) { return false; }
	return moveByArrow(ke->key(), mods == Qt::ShiftModifier);
}

bool AutomationEditor::moveByArrow(int key, bool coarse)
{
	const int sign = arrowSign(key);
	if (sign == 0) { return false; }
	if (key == Qt::Key_Left || key == Qt::Key_Right)
	{
		moveKeyPoint(sign * AutomationClip::quantization() * (coarse ? CoarseSteps : 1), 0.f);
	}
	else { moveKeyPoint(0, static_cast<float>(sign) * automationedit::valueStep(m_clip, coarse)); }
	return true;
}

void AutomationEditor::stepKeyPoint(int direction)
{
	const timeMap& map = m_clip->getTimeMap();
	if (map.isEmpty()) { return; }
	const int current = keyPoint();
	if (current < 0)
	{
		setKeyPoint(direction > 0 ? map.firstKey() : map.lastKey());
		return;
	}
	auto it = map.constFind(TimePos(current));
	if (direction > 0 && std::next(it) != map.constEnd()) { setKeyPoint(std::next(it).key()); }
	else if (direction < 0 && it != map.constBegin()) { setKeyPoint(std::prev(it).key()); }
}

void AutomationEditor::moveKeyPoint(int ticks, float value)
{
	const int tick = keyPoint();
	const float from = m_clip->getTimeMap().value(TimePos(tick)).getInValue();
	const int to = std::max(0, tick + ticks);
	automationedit::Range range;
	const float toValue = automationedit::rangeOf(m_clip, &range) ? std::clamp(from + value, range.minimum, range.maximum) : from;
	if (to == tick && toValue == from) { return; }  // at an end already: no edit, no undo step
	QString error;
	if (!automationedit::movePoint(m_clip, tick, to, toValue, &error))
	{
		a11y::announceStatus(this, tr("Point not moved: %1").arg(error), a11y::Politeness::Assertive);
		return;
	}
	m_keyTick = -1;  // the point is now elsewhere: choose it there, which re-describes it
	setKeyPoint(to);
}

void AutomationEditor::drawKeyPoint(QPainter& p)
{
	const int tick = keyPoint();
	if (tick < 0) { return; }
	const float value = m_clip->getTimeMap().value(TimePos(tick)).getInValue();
	const QPointF centre(xCoordOfTick(tick), yCoordOfLevel(value));
	p.save();
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(m_crossColor, 2));
	p.drawEllipse(centre, RingRadius, RingRadius);
	p.restore();
}

} // namespace lmms::gui
