/*
 * StepGridCanvas.cpp - the melodic step grid's cells and the scale-stepping pitch field
 *                      (include/StepGridPanel.h)
 *
 * Paints what the panel last read through note.step_get_state and hands every click to the
 * panel, which turns it into one note.step_set call. Nothing here edits the clip.
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

#include <algorithm>

#include <QMouseEvent>
#include <QPainter>

#include "StepGridPanel.h"

namespace lmms::gui
{

namespace
{

constexpr int Octave = 12;

QString keyName(int key)
{
	static const char* const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	return QLatin1String(names[key % Octave]) + QString::number(key / Octave);
}

} // namespace

StepGridCanvas::StepGridCanvas(StepGridPanel* panel) :
	QWidget(panel),
	m_panel(panel)
{
	setObjectName(QStringLiteral("stepGridCanvas"));
	setAccessibleName(tr("Step cells"));
	setMouseTracking(false);
}

QRect StepGridCanvas::cellRect(int step, int key) const
{
	const int row = m_panel->rows().indexOf(key);
	if (row < 0 || step < 0) { return {}; }
	return {KeyColumn + step * CellWidth, Ruler + row * CellHeight, CellWidth, CellHeight};
}

void StepGridCanvas::relayout()
{
	setFixedSize(KeyColumn + m_panel->steps() * CellWidth + 1, Ruler + static_cast<int>(m_panel->rows().size()) * CellHeight + 1);
	update();
}

void StepGridCanvas::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.fillRect(rect(), palette().color(QPalette::Window));
	paintRows(p);
	paintNotes(p);
}

//! A row per key: in-scale rows on the base colour, the others dimmed; a line every step, a darker one every beat.
void StepGridCanvas::paintRows(QPainter& p) const
{
	const QColor base = palette().color(QPalette::Base);
	const QColor dim = palette().color(QPalette::AlternateBase).darker(115);
	const int steps = m_panel->steps();
	const int stepTicks = std::max(1, m_panel->stepTicks());
	const QVector<int>& rows = m_panel->rows();
	for (int row = 0; row < rows.size(); ++row)
	{
		const int y = Ruler + row * CellHeight;
		p.fillRect(KeyColumn, y, steps * CellWidth, CellHeight, m_panel->inScale(rows[row]) ? base : dim);
		p.setPen(palette().color(QPalette::Text));
		p.drawText(QRect(2, y, KeyColumn - 4, CellHeight), Qt::AlignVCenter | Qt::AlignLeft, keyName(rows[row]));
	}
	const QColor line = palette().color(QPalette::Mid);
	for (int step = 0; step <= steps; ++step)
	{
		const int x = KeyColumn + step * CellWidth;
		const bool beat = (step * stepTicks) % 48 == 0;
		p.setPen(beat ? line.darker(140) : line);
		p.drawLine(x, Ruler, x, Ruler + static_cast<int>(rows.size()) * CellHeight);
		if (beat && step < steps) { p.drawText(QRect(x + 2, 0, CellWidth * 2, Ruler), Qt::AlignVCenter, QString::number(step + 1)); }
	}
}

void StepGridCanvas::paintNotes(QPainter& p) const
{
	for (const QJsonValue& value : m_panel->state().value(QStringLiteral("notes")).toArray())
	{
		paintNote(p, value.toObject());
	}
}

//! A note: its cell filled by velocity, the filled height its chance, one tick per ratchet attack,
//! a tail across the steps it lasts, and a frame when selected.
void StepGridCanvas::paintNote(QPainter& p, const QJsonObject& note) const
{
	const QRect cell = cellRect(note.value(QStringLiteral("step")).toInt(), note.value(QStringLiteral("key")).toInt());
	if (cell.isEmpty()) { return; }
	const int stepTicks = std::max(1, m_panel->stepTicks());
	const int spanned = std::max(1, (note.value(QStringLiteral("length")).toInt() + stepTicks - 1) / stepTicks);
	QColor fill = palette().color(QPalette::Highlight);
	fill.setAlphaF(0.35 + 0.65 * std::clamp(note.value(QStringLiteral("velocity")).toInt() / 200.0, 0.0, 1.0));
	if (spanned > 1) { p.fillRect(cell.adjusted(CellWidth - 2, 4, (spanned - 1) * CellWidth, -4), fill.lighter(130)); }
	const int height = std::max(2, static_cast<int>(note.value(QStringLiteral("chance")).toDouble() * (CellHeight - 2)));
	p.fillRect(QRect(cell.left() + 1, cell.bottom() - height, CellWidth - 2, height), fill);
	const int ratchet = std::max(1, note.value(QStringLiteral("ratchet")).toInt());
	p.setPen(palette().color(QPalette::HighlightedText));
	for (int hit = 1; hit < ratchet; ++hit)
	{
		const int x = cell.left() + hit * CellWidth / ratchet;
		p.drawLine(x, cell.top() + 3, x, cell.bottom() - 3);
	}
	const bool selected = note.value(QStringLiteral("note")).toString() == m_panel->selectedNote();
	p.setPen(QPen(selected ? palette().color(QPalette::Text) : fill.darker(150), selected ? 2 : 1));
	p.drawRect(cell.adjusted(1, 1, -1, -1));
}

void StepGridCanvas::mousePressEvent(QMouseEvent* event)
{
	const QPoint at = event->pos();
	const int row = (at.y() - Ruler) / CellHeight;
	const int step = (at.x() - KeyColumn) / CellWidth;
	if (at.x() < KeyColumn || at.y() < Ruler || row >= m_panel->rows().size() || step >= m_panel->steps()) { return; }
	const int key = m_panel->rows().at(row);
	if (event->button() == Qt::RightButton) { m_panel->removeCell(step, key); }
	else if (event->button() == Qt::LeftButton) { m_panel->clickCell(step, key); }
	update();
}

void ScaleKeySpinBox::stepBy(int steps)
{
	int key = value();
	const int direction = steps > 0 ? 1 : -1;
	for (int moved = 0; moved != steps; moved += direction)
	{
		// The next key whose pitch class the scale holds; with no scale, the next key.
		int next = key + direction;
		while (!m_classes.isEmpty() && next >= minimum() && next <= maximum() && !m_classes.contains(next % Octave)) { next += direction; }
		if (next < minimum() || next > maximum()) { break; }
		key = next;
	}
	setValue(key);
}

} // namespace lmms::gui
