/*
 * ClipViewTheme.cpp - ClipView's CSS theming accessors, moved out of ClipView.cpp unchanged
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

#include "ClipView.h"

namespace lmms::gui
{

// qproperty access functions, to be inherited & used by Clipviews
//! \brief CSS theming qproperty access method
QColor ClipView::mutedColor() const
{ return m_mutedColor; }

QColor ClipView::mutedBackgroundColor() const
{ return m_mutedBackgroundColor; }

QColor ClipView::selectedColor() const
{ return m_selectedColor; }

QColor ClipView::textColor() const
{ return m_textColor; }

QColor ClipView::textBackgroundColor() const
{
	return m_textBackgroundColor;
}

QColor ClipView::textShadowColor() const
{ return m_textShadowColor; }

QColor ClipView::patternClipBackground() const
{ return m_patternClipBackground; }

bool ClipView::gradient() const
{ return m_gradient; }

QColor ClipView::markerColor() const
{ return m_markerColor; }

//! \brief CSS theming qproperty access method
void ClipView::setMutedColor( const QColor & c )
{ m_mutedColor = QColor( c ); }

void ClipView::setMutedBackgroundColor( const QColor & c )
{ m_mutedBackgroundColor = QColor( c ); }

void ClipView::setSelectedColor( const QColor & c )
{ m_selectedColor = QColor( c ); }

void ClipView::setTextColor( const QColor & c )
{ m_textColor = QColor( c ); }

void ClipView::setTextBackgroundColor( const QColor & c )
{
	m_textBackgroundColor = c;
}

void ClipView::setTextShadowColor( const QColor & c )
{ m_textShadowColor = QColor( c ); }

void ClipView::setPatternClipBackground( const QColor & c )
{ m_patternClipBackground = QColor( c ); }

void ClipView::setGradient( const bool & b )
{ m_gradient = b; }

void ClipView::setMarkerColor(const QColor & c)
{ m_markerColor = QColor(c); }

// access needsUpdate member variable
bool ClipView::needsUpdate()
{ return m_needsUpdate; }
void ClipView::setNeedsUpdate( bool b )
{ m_needsUpdate = b; }

} // namespace lmms::gui
