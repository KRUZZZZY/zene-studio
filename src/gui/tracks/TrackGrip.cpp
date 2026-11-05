/*
 * TrackGrip.cpp - Grip that can be used to move tracks
 *
 * Copyright (c) 2024- Michael Gregorius
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

#include "TrackGrip.h"

#include "DataFile.h"
#include "DesignTokens.h"
#include "KeyboardShortcuts.h"
#include "StringPairDrag.h"
#include "Track.h"
#include "TrackView.h"

#include <QMouseEvent>
#include <QPainter>


namespace lmms::gui
{

namespace
{

/*! 0.5.0 Q050-442: the grip's dots, drawn from the palette's primary foreground role rather than a
 *  fixed bitmap. The inherited artwork was a 6 x 25 bitmap scaled down from a 24 px glyph, so its
 *  dots reached the row at a fraction of the colour's opacity and the handle disappeared in the
 *  dark palette (and stayed out of every contrast check). Drawing whole-pixel squares in the role's
 *  exact colour makes the handle as legible as the role itself, so it follows the high-contrast
 *  palette and a theme's 'text' override. */
constexpr int c_releasedDot = 2;  //!< one dot's side when released
constexpr int c_grabbedDot = 3;   //!< one dot's side while grabbed: the handle reads as picked up
constexpr int c_columnGap = 4;    //!< between the two dot columns
constexpr int c_rowStep = 5;      //!< from one dot row to the next
constexpr int c_rows = 5;         //!< dot rows, as the inherited 6 x 25 artwork had
//! The widget's width: the inherited 6 px artwork plus its 2 px margin. The grabbed dots are
//! exactly this wide, so the row's buttons keep their places in either state.
constexpr int c_gripWidth = 2 * c_grabbedDot + c_columnGap;

//! The dots' overall height when @a dot is one dot's side.
constexpr int dotsHeight(int dot)
{
	return (c_rows - 1) * c_rowStep + dot;
}

} // namespace

TrackGrip::TrackGrip(TrackView* trackView, QWidget* parent) :
	QWidget(parent),
	m_trackView(trackView)
{
	setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);

	setCursor(Qt::OpenHandCursor);

	setFixedWidth(c_gripWidth);
}

void TrackGrip::mousePressEvent(QMouseEvent* m)
{
	// Allow the user to Ctrl-drag copy the track if it is not a pattern track
	if (m->button() == Qt::LeftButton
		&& m->modifiers() & KBD_COPY_MODIFIER
		&& m_trackView->getTrack()->type() != Track::Type::Pattern)
	{
		m->accept();
		DataFile dataFile(DataFile::Type::DragNDropData);
		m_trackView->getTrack()->saveState(dataFile, dataFile.content());
		new StringPairDrag(
			QString("track_%1").arg(static_cast<int>(m_trackView->getTrack()->type())),
			dataFile.toString(),
			m_trackView->getTrackSettingsWidget()->grab(),
			this
		);
	}
	else
	{
		m->accept();

		m_isGrabbed = true;
		setCursor(Qt::ClosedHandCursor);

		emit grabbed();
		update();
	}
}

void TrackGrip::mouseReleaseEvent(QMouseEvent* m)
{
	m->accept();

	m_isGrabbed = false;
	setCursor(Qt::OpenHandCursor);

	emit released();

	update();
}

void TrackGrip::paintEvent(QPaintEvent*)
{
	QPainter p(this);

	// Check if the color of the track should be used for the background
	const auto color = m_trackView->getTrack()->color();
	const auto muted = m_trackView->getTrack()->getMutedModel()->value();

	if (color.has_value() && !muted)
	{
		p.fillRect(rect(), color.value());
	}

	// 0.5.0 Q050-442: the handle is drawn from the palette's primary foreground role - the colour
	// text and the inherited icons use - so the high-contrast palette strengthens it and a theme's
	// 'text' override redraws it from the next start, like every theme colour (tokens::palette() is
	// read once per start). No fixed pixmap and no downscaled 24 px SVG: pixel-aligned squares at
	// full opacity are what make the rendered handle meet WCAG 1.4.11's 3:1 against the row surface.
	const int dot = m_isGrabbed ? c_grabbedDot : c_releasedDot;
	const int dotsWidth = 2 * dot + c_columnGap;
	const int left = (width() - dotsWidth) / 2;
	const int top = (height() - dotsHeight(dot)) / 2;

	p.setRenderHint(QPainter::Antialiasing, false);
	p.setPen(Qt::NoPen);
	p.setBrush(tokens::palette().text);
	for (int row = 0; row < c_rows; ++row)
	{
		const int y = top + row * c_rowStep;
		p.drawRect(left, y, dot, dot);
		p.drawRect(left + dot + c_columnGap, y, dot, dot);
	}
}

} // namespace lmms::gui
