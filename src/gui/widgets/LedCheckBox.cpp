/*
 * LedCheckBox.cpp - class LedCheckBox, an improved QCheckBox
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "LedCheckBox.h"
#include "ControlCues.h"

#include <QFontMetrics>
#include <QPainter>
#include <array>

#include "DeprecationHelper.h"
#include "DesignTokens.h"
#include "embed.h"
#include "FontHelper.h"

namespace lmms::gui
{


static const auto names = std::array<QString, 3>
{
	"led_yellow", "led_green", "led_red"
} ;




LedCheckBox::LedCheckBox( const QString & _text, QWidget * _parent,
				const QString & _name, LedColor _color, bool legacyMode ) :
	AutomatableButton( _parent, _name ),
	m_text( _text ),
	m_legacyMode(legacyMode)
{
	setLedColor(_color);
}




LedCheckBox::LedCheckBox( QWidget * _parent,
				const QString & _name, LedColor _color, bool legacyMode ) :
	LedCheckBox( QString(), _parent, _name, _color, legacyMode )
{
}

void LedCheckBox::setText( const QString &s )
{
	m_text = s;
	onTextUpdated();
}




void LedCheckBox::paintEvent( QPaintEvent * pe )
{
	if (!m_legacyMode)
	{
		paintNonLegacy(pe);
	}
	else
	{
		paintLegacy(pe);
	}
}




void LedCheckBox::setLedColor(LedColor color)
{
	setCheckable( true );

	m_ledOnPixmap = embed::getIconPixmap(names[static_cast<std::size_t>(color)].toUtf8().constData());
	m_ledOffPixmap = embed::getIconPixmap("led_off");

	if (m_legacyMode){ setFont(adjustedToPixelSize(font(), DEFAULT_FONT_SIZE)); }

	setText( m_text );
}




void LedCheckBox::onTextUpdated()
{
	QFontMetrics const fm = fontMetrics();
	// Legacy mode paints its text in the caption token (paintLegacy), which follows the interface's text size:
	// wide enough for that too, so a larger text size does not cut the label off.
	int const advance = m_legacyMode
		? std::max(fm.horizontalAdvance(text()), QFontMetrics(tokens::font(tokens::TextRole::Caption)).horizontalAdvance(text()))
		: fm.horizontalAdvance(text());

	int const width = m_ledOffPixmap.width() + 5 + advance;
	int const height = m_legacyMode ? m_ledOffPixmap.height() : qMax(m_ledOffPixmap.height(), fm.height());

	setFixedSize(width, height);
}

void LedCheckBox::paintIndicator(QPainter& p, const QRect& box) const
{
	// 0.5.0 (docs/UI-SHELL.md §1): a flat indicator in place of the LED bitmaps - the accent dot
	// when on, a muted ring when off - centred in the box the old LED occupied.
	const tokens::Palette& pal = tokens::palette();
	const qreal d = std::min(box.width(), box.height()) * 0.7;
	const QRectF dot(box.center().x() - d / 2.0 + 0.5, box.center().y() - d / 2.0 + 0.5, d, d);
	p.setRenderHint(QPainter::Antialiasing);
	const bool hot = cues::hovered(this);  // 0.5.0 theme 23: the hover state brightens the indicator, on or off
	if (model() != nullptr && model()->value())
	{
		p.setPen(Qt::NoPen);
		p.setBrush(!isEnabled() ? pal.textDisabled : hot ? pal.accentHover : pal.accent);
	}
	else
	{
		p.setPen(QPen(!isEnabled() ? pal.textDisabled : hot ? pal.text : pal.textMuted, 1.4));
		p.setBrush(Qt::NoBrush);
	}
	p.drawEllipse(dot);
}

void LedCheckBox::paintLegacy(QPaintEvent*)
{
	QPainter p(this);
	paintIndicator(p, QRect(QPoint(0, 0), m_ledOffPixmap.size()));
	p.setFont(tokens::font(tokens::TextRole::Caption));
	p.setPen(isEnabled() ? tokens::palette().text : tokens::palette().textDisabled);
	p.drawText(QRect(m_ledOffPixmap.width() + 3, 0, width(), m_ledOffPixmap.height()), Qt::AlignVCenter | Qt::AlignLeft, text());
}

void LedCheckBox::paintNonLegacy(QPaintEvent*)
{
	QPainter p(this);
	paintIndicator(p, QRect(0, rect().height() / 2 - m_ledOffPixmap.height() / 2, m_ledOffPixmap.width(), m_ledOffPixmap.height()));
	QRect r = rect();
	r -= QMargins(m_ledOffPixmap.width() + 5, 0, 0, 0);
	p.setPen(isEnabled() ? tokens::palette().text : tokens::palette().textDisabled);
	p.drawText(r, Qt::AlignVCenter | Qt::AlignLeft, text());
}

} // namespace lmms::gui
