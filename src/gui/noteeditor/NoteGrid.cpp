/*
 * NoteGrid.cpp - the note editor's geometry: zoom, scroll, folded key rows, snapping, hit tests
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

#include "NoteGrid.h"

#include <algorithm>
#include <cmath>

namespace lmms::gui
{

namespace
{

constexpr int kKeys = 128;
constexpr int kFoldedAway = -1000000;

} // namespace

NoteGrid::NoteGrid()
{
	m_rowOfKey.fill(-1);
	rebuildRows();
}

void NoteGrid::setPixelsPerTick(double ppt)
{
	m_pixelsPerTick = std::clamp(ppt, minPixelsPerTick, maxPixelsPerTick);
}

void NoteGrid::zoomAround(double factor, double anchorX)
{
	const double anchorTick = tickAtX(anchorX);
	setPixelsPerTick(m_pixelsPerTick * factor);
	setScrollTicks(anchorTick - anchorX / m_pixelsPerTick);
}

void NoteGrid::setRowHeight(int px)
{
	m_rowHeight = std::clamp(px, 6, 40);
	clampScrollY();
}

void NoteGrid::setScrollTicks(double ticks) { m_scrollTicks = std::max(0.0, ticks); }

void NoteGrid::setScrollY(int px)
{
	m_scrollY = px;
	clampScrollY();
}

void NoteGrid::setViewport(int width, int height)
{
	m_width = std::max(1, width);
	m_height = std::max(1, height);
	clampScrollY();
}

void NoteGrid::clampScrollY()
{
	m_scrollY = std::clamp(m_scrollY, 0, std::max(0, contentHeight() - m_height));
}

void NoteGrid::setScaleMask(const std::array<bool, 12>& mask, int root)
{
	m_scaleMask = mask;
	m_scaleRoot = ((root % 12) + 12) % 12;
	if (m_fold == NoteFold::Scale) { rebuildRows(); }
}

bool NoteGrid::inScale(int key) const
{
	return key >= 0 && m_scaleMask[static_cast<std::size_t>(key % 12)];
}

void NoteGrid::setUsedKeys(const std::vector<int>& keys)
{
	m_used.fill(false);
	for (int key : keys)
	{
		if (key >= 0 && key < kKeys) { m_used[static_cast<std::size_t>(key)] = true; }
	}
	if (m_fold == NoteFold::Used) { rebuildRows(); }
}

void NoteGrid::setFold(NoteFold fold)
{
	// The key at the centre of the view stays at the centre (or the nearest key still shown
	// does): folding changes which rows exist, not where the user is looking.
	// With fewer rows than the view is tall the centre pixel is past the last row, so the row
	// nearest it stands for "the key the user is looking at".
	const int centreRow = std::clamp((m_scrollY + m_height / 2) / m_rowHeight, 0, rowCount() - 1);
	const int centre = rowCount() > 0 ? m_rows[static_cast<std::size_t>(centreRow)] : -1;
	m_fold = fold;
	rebuildRows();
	if (centre >= 0) { centreOnKey(centre); }
}

void NoteGrid::centreOnKey(int key)
{
	int nearest = -1;
	for (int row = 0; row < rowCount(); ++row)
	{
		const int candidate = m_rows[static_cast<std::size_t>(row)];
		if (nearest < 0 || std::abs(candidate - key) < std::abs(m_rows[static_cast<std::size_t>(nearest)] - key)) { nearest = row; }
	}
	if (nearest >= 0) { setScrollY(nearest * m_rowHeight + m_rowHeight / 2 - m_height / 2); }
}

void NoteGrid::rebuildRows()
{
	m_rows.clear();
	m_rowOfKey.fill(-1);
	for (int key = kKeys - 1; key >= 0; --key)
	{
		const bool shown = m_fold == NoteFold::None
			|| (m_fold == NoteFold::Scale && inScale(key))
			|| (m_fold == NoteFold::Used && m_used[static_cast<std::size_t>(key)]);
		if (!shown) { continue; }
		m_rowOfKey[static_cast<std::size_t>(key)] = static_cast<int>(m_rows.size());
		m_rows.push_back(key);
	}
	// An empty fold (no scale set, or an empty clip) would leave nothing to draw into: show
	// every key instead, which is what the user has before choosing anything to fold to.
	if (m_rows.empty() && m_fold != NoteFold::None)
	{
		const NoteFold keep = m_fold;
		m_fold = NoteFold::None;
		rebuildRows();
		m_fold = keep;
		return;
	}
	clampScrollY();
}

int NoteGrid::rowOfKey(int key) const
{
	return key >= 0 && key < kKeys ? m_rowOfKey[static_cast<std::size_t>(key)] : -1;
}

int NoteGrid::keyAtY(int y) const
{
	const int row = (y + m_scrollY) / m_rowHeight;
	if (y + m_scrollY < 0 || row >= rowCount()) { return -1; }
	return m_rows[static_cast<std::size_t>(row)];
}

int NoteGrid::yOfKey(int key) const
{
	const int row = rowOfKey(key);
	return row < 0 ? kFoldedAway : row * m_rowHeight - m_scrollY;
}

double NoteGrid::xOfTick(double tick) const { return (tick - m_scrollTicks) * m_pixelsPerTick; }

double NoteGrid::tickAtX(double x) const { return x / m_pixelsPerTick + m_scrollTicks; }

double NoteGrid::lastVisibleTick() const { return tickAtX(m_width); }

int NoteGrid::snap(int tick, int gridTicks)
{
	if (gridTicks <= 1) { return tick; }
	return static_cast<int>(std::lround(static_cast<double>(tick) / gridTicks)) * gridTicks;
}

int NoteGrid::snapDown(int tick, int gridTicks)
{
	if (gridTicks <= 1) { return tick; }
	return static_cast<int>(std::floor(static_cast<double>(tick) / gridTicks)) * gridTicks;
}

int NoteGrid::visibleGridTicks(int gridTicks, int minPixels) const
{
	int ticks = std::max(1, gridTicks);
	while (ticks * m_pixelsPerTick < minPixels && ticks < (1 << 20)) { ticks *= 2; }
	return ticks;
}

bool NoteGrid::isBlackKey(int key)
{
	static constexpr std::array<bool, 12> black{false, true, false, true, false, false, true, false, true, false, true, false};
	return black[static_cast<std::size_t>(((key % 12) + 12) % 12)];
}

QString NoteGrid::keyName(int key)
{
	static const char* const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	// The engine's numbering: key 0 is C0 and its default A is key 57 (A4, include/Note.h).
	return QStringLiteral("%1%2").arg(QLatin1String(names[((key % 12) + 12) % 12])).arg(key / 12);
}

} // namespace lmms::gui
