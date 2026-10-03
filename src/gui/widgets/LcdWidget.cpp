/*
 * LcdWidget.cpp - a widget for displaying numbers in LCD style
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2008 Paul Giblock <pgllama/at/gmail.com>
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



#include <QStyleOptionFrame>
#include <QPainter>

#include "LcdWidget.h"

#include "DesignTokens.h"
#include "DeprecationHelper.h"
#include "embed.h"
#include "FontHelper.h"


namespace lmms::gui
{

LcdWidget::LcdWidget(QWidget* parent, const QString& name, bool leadingZero) :
	LcdWidget(1, parent, name, leadingZero)
{
}




LcdWidget::LcdWidget(int numDigits, QWidget* parent, const QString& name, bool leadingZero) :
	LcdWidget(numDigits, QString("19green"), parent, name, leadingZero)
{
}




LcdWidget::LcdWidget(int numDigits, const QString& style, QWidget* parent, const QString& name, bool leadingZero) :
	QWidget( parent ),
	m_label(),
	m_textColor( 255, 255, 255 ),
	m_textShadowColor( 64, 64, 64 ),
	m_numDigits(numDigits),
	m_seamlessLeft(false),
	m_seamlessRight(false),
	m_leadingZero(leadingZero)
{
	initUi( name, style );
}

void LcdWidget::setValue(int value)
{
	QString s = m_textForValue[value];
	if (s.isEmpty())
	{
		s = QString::number(value);
		if (m_leadingZero)
		{
			s = s.rightJustified(m_numDigits, '0');
		}
	}

	if (m_display != s)
	{
		m_display = s;

		update();
	}
}

void LcdWidget::setValue(float value)
{
	if (-1 < value && value < 0)
	{
		QString s = QString::number(static_cast<int>(value));
		s.prepend('-');
		
		if (m_display != s)
		{
			m_display = s;
			update();
		}
	}
	else
	{
		setValue(static_cast<int>(value));
	}
}




QColor LcdWidget::textColor() const
{
	return m_textColor;
}

void LcdWidget::setTextColor( const QColor & c )
{
	m_textColor = c;
}




QColor LcdWidget::textShadowColor() const
{
	return m_textShadowColor;
}

void LcdWidget::setTextShadowColor( const QColor & c )
{
	m_textShadowColor = c;
}




void LcdWidget::paintEvent( QPaintEvent* )
{
	// 0.5.0 (docs/UI-SHELL.md §1): a flat number field painted from the design tokens - the
	// value in the interface's typeface on a dark rounded field, the label as a caption below -
	// in place of the seven-segment bitmap. The cell geometry the bitmap defined is kept, so no
	// layout moves; a seamless edge (LcdFloatSpinBox's halves) is drawn square and open, the other keeps its
	// rounding and border - the field runs on past the widget on that side, where the widget clips it.
	const tokens::Palette& pal = tokens::palette();
	QPainter p( this );
	p.setRenderHint( QPainter::Antialiasing );
	const int marginX1 = m_seamlessLeft ? 0 : 1 + m_marginWidth;
	const int marginX2 = m_seamlessRight ? 0 : 1 + m_marginWidth;
	const QRectF field( 0.5, 0.5, m_cellWidth * m_numDigits + marginX1 + marginX2 - 1.0, m_cellHeight + 1.0 );
	const qreal radius = tokens::radius::md;
	const QRectF body = field.adjusted( m_seamlessLeft ? -2 * radius : 0, 0, m_seamlessRight ? 2 * radius : 0, 0 );
	p.setPen( QPen( pal.separator, 1 ) );
	p.setBrush( pal.base );
	p.drawRoundedRect( body, radius, radius );

	QFont digits = tokens::font( tokens::TextRole::Body );
	digits.setPixelSize( std::max( 9, static_cast<int>( m_cellHeight * 0.78 ) ) );
	digits.setWeight( QFont::Medium );
	p.setFont( digits );
	p.setPen( isEnabled() ? pal.text : pal.textDisabled );
	const Qt::Alignment align = m_seamlessRight ? ( Qt::AlignRight | Qt::AlignVCenter )
		: m_seamlessLeft ? ( Qt::AlignLeft | Qt::AlignVCenter ) : Qt::AlignCenter;
	p.drawText( field.adjusted( m_seamlessLeft ? 1 : 3, 0, m_seamlessRight ? -1 : -3, 0 ), align, m_display );

	if( !m_label.isEmpty() )
	{
		p.setFont( tokens::font( tokens::TextRole::Caption ) );
		p.setPen( pal.textMuted );
		p.drawText( QRect( 0, static_cast<int>( field.bottom() ), width(), height() - static_cast<int>( field.bottom() ) ),
			Qt::AlignHCenter | Qt::AlignBottom, m_label );
	}
}




void LcdWidget::setLabel( const QString& label )
{
	m_label = label;
	updateSize();
}




void LcdWidget::setMarginWidth( int width )
{
	m_marginWidth = width;

	updateSize();
}




void LcdWidget::updateSize()
{
	const int marginX1 = m_seamlessLeft ? 0 : 1 + m_marginWidth;
	const int marginX2 = m_seamlessRight ? 0 : 1 + m_marginWidth;
	const int marginY = 1;
	if (m_label.isEmpty())
	{
		setFixedSize(
			m_cellWidth * m_numDigits + marginX1 + marginX2,
			m_cellHeight + (2 * marginY)
		);
	}
	else
	{
		setFixedSize(
			qMax<int>(
				m_cellWidth * m_numDigits + marginX1 + marginX2,
				QFontMetrics(adjustedToPixelSize(font(), DEFAULT_FONT_SIZE)).horizontalAdvance(m_label)
			),
			m_cellHeight + (2 * marginY) + 9
		);
	}

	update();
}




void LcdWidget::initUi(const QString& name , const QString& style)
{
	setEnabled( true );

	setWindowTitle( name );

	// We should make a factory for these or something.
	//m_lcdPixmap = embed::getIconPixmap(QString("lcd_" + style).toUtf8().constData());
	//m_lcdPixmap = embed::getIconPixmap("lcd_19green"); // TODO!!

	m_lcdPixmap = embed::getIconPixmap(QString("lcd_" + style).toUtf8().constData());
	m_cellWidth = m_lcdPixmap.size().width() / LcdWidget::charsPerPixmap;
	m_cellHeight = m_lcdPixmap.size().height() / 2;

	m_marginWidth =  m_cellWidth / 2;

	updateSize();
}

} // namespace lmms::gui
