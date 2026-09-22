/*
 * LmmsStyle.cpp - the graphical style used by LMMS to create a consistent
 *				  interface
 *
 * Copyright (c) 2007-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include <array>

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>  // IWYU pragma: keep
#include <QStyleFactory>
#include <QStyleOption>

#include "embed.h"
#include "LmmsStyle.h"
#include "TextFloat.h"


namespace lmms::gui
{


QPalette * LmmsStyle::s_palette = nullptr;

QLinearGradient getGradient( const QColor & _col, const QRectF & _rect )
{
	QLinearGradient g( _rect.topLeft(), _rect.bottomLeft() );

	qreal hue = _col.hueF();
	qreal value = _col.valueF();
	qreal saturation = _col.saturationF();

	QColor c = _col;
	c.setHsvF( hue, 0.42 * saturation, 0.98 * value ); // TODO: MIDI clip: 1.08
	g.setColorAt( 0, c );
	c.setHsvF( hue, 0.58 * saturation, 0.95 * value ); // TODO: MIDI clip: 1.05
	g.setColorAt( 0.25, c );
	c.setHsvF( hue, 0.70 * saturation, 0.93 * value ); // TODO: MIDI clip: 1.03
	g.setColorAt( 0.5, c );

	c.setHsvF( hue, 0.95 * saturation, 0.9 * value );
	g.setColorAt( 0.501, c );
	c.setHsvF( hue * 0.95, 0.95 * saturation, 0.95 * value );
	g.setColorAt( 0.75, c );
	c.setHsvF( hue * 0.90, 0.95 * saturation, 1 * value );
	g.setColorAt( 1.0, c );

	return g;
}



QLinearGradient darken( const QLinearGradient & _gradient )
{
	QGradientStops stops = _gradient.stops();
	for (auto& stop : stops)
	{
		QColor color = stop.second;
		stop.second = color.lighter(133);
	}

	QLinearGradient g = _gradient;
	g.setStops(stops);
	return g;
}



void drawPath( QPainter *p, const QPainterPath &path,
			   const QColor &col, const QColor &borderCol,
			   bool dark = false )
{
	const QRectF pathRect = path.boundingRect();

	const QLinearGradient baseGradient = getGradient(col, pathRect);
	const QLinearGradient darkGradient = darken(baseGradient);

	p->setOpacity(0.25);

	// glow
	if (dark)
		p->strokePath(path, QPen(darkGradient, 4));
	else
		p->strokePath(path, QPen(baseGradient, 4));

	p->setOpacity(1.0);

	// fill
	if (dark)
		p->fillPath(path, darkGradient);
	else
		p->fillPath(path, baseGradient);

	// TODO: Remove??
	/*
	QLinearGradient g(pathRect.topLeft(), pathRect.topRight());
	g.setCoordinateMode(QGradient::ObjectBoundingMode);

	p->setOpacity(0.2);
	p->fillPath(path, g);*/
	// END: Remove??

	p->setOpacity(0.5);

	// highlight (pattern)
	if (dark)
		p->strokePath(path, QPen(borderCol.lighter(133), 2));
	else
		p->strokePath(path, QPen(borderCol, 2));
}



LmmsStyle::LmmsStyle() :
	QProxyStyle()
{
	QFile file( "resources:style.css" );
	file.open( QIODevice::ReadOnly );
	qApp->setStyleSheet( file.readAll() );

	m_styleReloader.addPath(QFileInfo{file}.absoluteFilePath());
	connect(&m_styleReloader, &QFileSystemWatcher::fileChanged, this,
		[this](const QString& path)
		{
			if (auto file = QFile{path}; file.exists())
			{
				file.open(QIODevice::ReadOnly);
				qApp->setStyleSheet(file.readAll());
				TextFloat::displayMessage(
					tr("Theme updated"),
					tr("Zene Studio theme file %1 has been reloaded.").arg(file.fileName()),
					embed::getIconPixmap("colorize"),
					3000
				);
				// Handle delete + overwrite events
				if (!m_styleReloader.files().contains(path))
				{
					m_styleReloader.addPath(path);
				}
			}
		}
	);

	if( s_palette != nullptr ) { qApp->setPalette( *s_palette ); }

	setBaseStyle( QStyleFactory::create( "Fusion" ) );
}




QPalette LmmsStyle::standardPalette() const
{
	if( s_palette != nullptr) { return * s_palette; }

	QPalette pal = QProxyStyle::standardPalette();

	return( pal );
}


void LmmsStyle::drawComplexControl( ComplexControl control,
					const QStyleOptionComplex * option,
					QPainter *painter,
						const QWidget *widget ) const
{
	// fix broken titlebar styling on win32
	if( control == CC_TitleBar )
	{
		const auto titleBar = qstyleoption_cast<const QStyleOptionTitleBar*>(option);
		if( titleBar )
		{
			// The palette decides both text roles now (spec §4.3: roles, not
			// literals). The three hardcoded colours this branch used to force
			// over it - #ffffff / #c0c0c0 for the active/inactive caption and
			// #404040 for the text - collapsed every colour group to one pair
			// of constants, which is exactly what a dark palette inverts.
			QStyleOptionTitleBar so( *titleBar );
			so.palette = standardPalette();
			QProxyStyle::drawComplexControl( control, &so,
							painter, widget );
			return;
		}
	}
	// CC_MdiControls used to force QPalette::Button to a hardcoded light grey
	// (#dfe4ec); the palette's own role decides the buttons now, so the only
	// thing that branch drew is the fall-through below.
/*	else if( control == CC_ScrollBar )
	{
		painter->fillRect( option->rect, QApplication::palette().color( QPalette::Active,
							QPalette::Window ) );

	}*/
	QProxyStyle::drawComplexControl( control, option, painter, widget );
}




void LmmsStyle::drawPrimitive( PrimitiveElement element,
		const QStyleOption *option, QPainter *painter,
		const QWidget *widget) const
{
	if( element == QStyle::PE_Frame ||
			element == QStyle::PE_FrameLineEdit ||
			element == QStyle::PE_PanelLineEdit )
	{
		const QRect rect = option->rect;

		// The inner bevel comes from the palette's Shadow role instead of a
		// hardcoded #000 (spec §4.3: role lookups, not literals); LmmsPalette
		// defines Shadow as black, so the paint is unchanged under this theme.
		QColor inner = option->palette.color(QPalette::Active, QPalette::Shadow);
		QColor shadow = option->palette.shadow().color();
		QColor highlight = option->palette.highlight().color();

		int a100 = 165;
		int a75 = static_cast<int>( a100 * .75 );
		int a50 = static_cast<int>( a100 * .6 );
		int a25 = static_cast<int>( a100 * .33 );

		auto lines = std::array<QLine, 4>{};
		auto points = std::array<QPoint, 4>{};

		// inner lines
		// 50%
		inner.setAlpha(a100);
		painter->setPen(QPen(inner, 0));
		lines[0] = QLine(rect.left() + 2, rect.top() + 1,
					rect.right() - 2, rect.top() + 1);
		lines[1] = QLine(rect.left() + 2, rect.bottom() - 1,
					rect.right() - 2, rect.bottom() - 1);
		lines[2] = QLine(rect.left() + 1, rect.top() + 2,
					rect.left() + 1, rect.bottom() - 2);
		lines[3] = QLine(rect.right() - 1, rect.top() + 2,
					rect.right() - 1, rect.bottom() - 2);
		painter->drawLines(lines.data(), 4);

		// inner dots
		inner.setAlpha(a50);
		painter->setPen(QPen(inner, 0));
		points[0] = QPoint(rect.left() + 2, rect.top() + 2);
		points[1] = QPoint(rect.left() + 2, rect.bottom() - 2);
		points[2] = QPoint(rect.right() - 2, rect.top() + 2);
		points[3] = QPoint(rect.right() - 2, rect.bottom() - 2);
		painter->drawPoints(points.data(), 4);


		// outside lines - shadow
		// 100%
		shadow.setAlpha(a75);
		painter->setPen(QPen(shadow, 0));
		lines[0] = QLine(rect.left() + 2, rect.top(),
						rect.right() - 2, rect.top());
		lines[1] = QLine(rect.left(), rect.top() + 2,
						rect.left(), rect.bottom() - 2);
		painter->drawLines(lines.data(), 2);

		// outside corner dots - shadow
		// 75%
		shadow.setAlpha(a50);
		painter->setPen(QPen(shadow, 0));
		points[0] = QPoint(rect.left() + 1, rect.top() + 1);
		points[1] = QPoint(rect.right() - 1, rect.top() + 1);
		painter->drawPoints(points.data(), 2);

		// outside end dots - shadow
		// 50%
		shadow.setAlpha(a25);
		painter->setPen(QPen(shadow, 0));
		points[0] = QPoint(rect.left() + 1, rect.top());
		points[1] = QPoint(rect.left(), rect.top() + 1);
		points[2] = QPoint(rect.right() - 1, rect.top());
		points[3] = QPoint(rect.left(), rect.bottom() - 1);
		painter->drawPoints(points.data(), 4);


		// outside lines - highlight
		// 100%
		highlight.setAlpha(a75);
		painter->setPen(QPen(highlight, 0));
		lines[0] = QLine(rect.left() + 2, rect.bottom(),
					rect.right() - 2, rect.bottom());
		lines[1] = QLine(rect.right(), rect.top() + 2,
					rect.right(), rect.bottom() - 2);
		painter->drawLines(lines.data(), 2);

		// outside corner dots - highlight
		// 75%
		highlight.setAlpha(a50);
		painter->setPen(QPen(highlight, 0));
		points[0] = QPoint(rect.left() + 1, rect.bottom() - 1);
		points[1] = QPoint(rect.right() - 1, rect.bottom() - 1);
		painter->drawPoints(points.data(), 2);

		// outside end dots - highlight
		// 50%
		highlight.setAlpha(a25);
		painter->setPen(QPen(highlight, 0));
		points[0] = QPoint(rect.right() - 1, rect.bottom());
		points[1] = QPoint(rect.right(), rect.bottom() - 1);
		points[2] = QPoint(rect.left() + 1, rect.bottom());
		points[3] = QPoint(rect.right(), rect.top() + 1);
		painter->drawPoints(points.data(), 4);
	}
	else
	{
		QProxyStyle::drawPrimitive( element, option, painter, widget );
	}

}


int LmmsStyle::pixelMetric( PixelMetric _metric, const QStyleOption * _option,
						const QWidget * _widget ) const
{
	switch( _metric )
	{
		case QStyle::PM_ButtonMargin:
			return 3;

		case QStyle::PM_ButtonIconSize:
			return 20;

		case QStyle::PM_ToolBarItemMargin:
			return 1;

		case QStyle::PM_ToolBarItemSpacing:
			return 2;

		case QStyle::PM_TitleBarHeight:
			return 24;

		default:
			return QProxyStyle::pixelMetric( _metric, _option, _widget );
	}
}


QImage LmmsStyle::colorizeXpm( const char * const * xpm, const QBrush& fill ) const
{
	QImage arrowXpm( xpm );
	QImage arrow( arrowXpm.size(), QImage::Format_ARGB32 );
	QPainter arrowPainter( &arrow );
	arrowPainter.fillRect( arrow.rect(), fill );
	arrowPainter.end();
	arrow.setAlphaChannel( arrowXpm );

	return arrow;
}


void LmmsStyle::hoverColors( bool sunken, bool hover, bool active, QColor& color, QColor& blend ) const
{
	// The three states below used to hardcode their greys (#151515/#212121
	// idle, #646464/#4b4b4b hover, #4b4b4b/#414141 pressed); they are read
	// from the application palette's roles now, so the same states invert
	// under a dark theme instead of painting the same two greys over it
	// (spec §4.3: "the fix is lookup").
	const QPalette pal = QApplication::palette();
	color = pal.color( QPalette::Active, QPalette::Window );
	blend = pal.color( QPalette::Active, QPalette::Dark );
	if( active && sunken )
	{
		color = pal.color( QPalette::Active, QPalette::Shadow );
		blend = pal.color( QPalette::Active, QPalette::Dark );
	}
	else if( active && hover )
	{
		color = pal.color( QPalette::Active, QPalette::Mid );
		blend = pal.color( QPalette::Active, QPalette::Dark );
	}
}


} // namespace lmms::gui
