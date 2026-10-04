/*
 * ComboBox.cpp - implementation of LMMS combobox
 *
 * Copyright (c) 2006-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2008-2009 Paul Giblock <pgib/at/users.sourceforge.net>
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


#include "ComboBox.h"

#include <QPainterPath>

#include "DesignTokens.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOptionFrame>
#include <QScreen>

#include "AccessibleWidgets.h"
#include "CaptionMenu.h"
#include "DpiHelper.h"
#include "FontHelper.h"
#include "DeprecationHelper.h"

namespace lmms::gui
{
const int CB_ARROW_BTN_WIDTH = 18;


ComboBox::ComboBox( QWidget * _parent, const QString & _name ) :
	QWidget( _parent ),
	IntModelView( new ComboBoxModel( nullptr, QString(), true ), this ),
	m_menu( this ),
	m_pressed( false )
{
	setFixedHeight(scaledPixels(ComboBox::DEFAULT_HEIGHT));

	setFont(adjustedToPixelSize(font(), DEFAULT_FONT_SIZE));

	connect( &m_menu, SIGNAL(triggered(QAction*)),
				this, SLOT(setItem(QAction*)));

	setWindowTitle( _name );
	setFocusPolicy( Qt::TabFocus );  // 0.5.0 (theme 10): Tab reaches it; a click still leaves focus where it was
	setAttribute( Qt::WA_Hover, true );
	doConnections();
}






void ComboBox::selectNext()
{
	model()->setValue(model()->value() + 1);
}




void ComboBox::selectPrevious()
{
	model()->setValue(model()->value() - 1);
}



void ComboBox::contextMenuEvent( QContextMenuEvent * event )
{
	if( model() == nullptr || event->x() <= width() - CB_ARROW_BTN_WIDTH )
	{
		QWidget::contextMenuEvent( event );
		return;
	}

	CaptionMenu contextMenu( model()->displayName() );
	addDefaultActions( &contextMenu );
	contextMenu.exec( QCursor::pos() );
}




void ComboBox::mousePressEvent( QMouseEvent* event )
{
	if( model() == nullptr )
	{
		return;
	}

	const auto pos = position(event);

	if( event->button() == Qt::LeftButton && ! ( event->modifiers() & Qt::ControlModifier ) )
	{
		if (pos.x() > width() - CB_ARROW_BTN_WIDTH)
		{
			showMenu();
		}
		else if( event->button() == Qt::LeftButton )
		{
			selectNext();
			update();
		}
	}
	else if( event->button() == Qt::RightButton )
	{
		selectPrevious();
		update();
	}
	else
	{
		IntModelView::mousePressEvent( event );
	}
}




void ComboBox::showMenu()
{
	if( model() == nullptr ) { return; }
	m_pressed = true;
	update();

	m_menu.clear();
	for( int i = 0; i < model()->size(); ++i )
	{
		QAction * a = m_menu.addAction( model()->itemPixmap( i ) ? model()->itemPixmap( i )->pixmap() : QPixmap(),
											model()->itemText( i ) );
		a->setData( i );
		if( i == model()->value() ) { m_menu.setActiveAction( a ); }  // the keyboard starts on the current item
	}

	QPoint gpos = mapToGlobal(QPoint(0, height()));

	bool const menuCanBeFullyShown = screen()->geometry().contains(QRect(gpos, m_menu.sizeHint()));
	if (menuCanBeFullyShown)
	{
		m_menu.exec(gpos);
	}
	else
	{
		m_menu.exec(mapToGlobal(QPoint(width(), 0)));
	}

	m_pressed = false;
	update();
}




void ComboBox::keyPressEvent( QKeyEvent* event )
{
	// 0.5.0 (theme 10): Space, Enter or Alt+Down opens the menu; Down/Right/PageDown choose the next item and
	// Up/Left/PageUp the previous one, as in a list (and as the wheel does), Home/End the first and last; other
	// keys pass on.
	const int key = event->key();
	const bool opens = key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter
		|| (key == Qt::Key_Down && ( event->modifiers() & Qt::AltModifier ));
	const float down = key == Qt::Key_Up || key == Qt::Key_Down ? -1.f : 1.f;  // a list counts down the screen
	if( model() != nullptr && opens ) { showMenu(); }
	else if( !a11y::applyKeyStep( model(), a11y::keyStep( key ), down, -1.f ) )
	{
		QWidget::keyPressEvent( event );
		return;
	}
	event->accept();
}




void ComboBox::paintEvent( QPaintEvent * )
{
	// 0.5.0 (docs/UI-SHELL.md §1): a flat dropdown painted from the design tokens - rounded field,
	// the current item in the interface's typeface, a vector chevron - in place of the bevelled
	// frame, the shadowed text and the arrow bitmap.
	const tokens::Palette& pal = tokens::palette();
	QPainter p( this );
	p.setRenderHint( QPainter::Antialiasing );
	const QRectF field = QRectF( rect() ).adjusted( 0.5, 0.5, -0.5, -0.5 );
	p.setPen( QPen( hasFocus() ? pal.focusRing : pal.separator, 1 ) );
	p.setBrush( m_pressed ? pal.selected : underMouse() ? pal.controlHover : pal.control );
	p.drawRoundedRect( field, tokens::radius::md, tokens::radius::md );

	const qreal cx = width() - CB_ARROW_BTN_WIDTH / 2.0;
	const qreal cy = height() / 2.0;
	QPainterPath chevron;
	chevron.moveTo( cx - 3.5, cy - 1.5 );
	chevron.lineTo( cx, cy + 2.0 );
	chevron.lineTo( cx + 3.5, cy - 1.5 );
	p.setPen( QPen( isEnabled() ? pal.textMuted : pal.textDisabled, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ) );
	p.setBrush( Qt::NoBrush );
	p.drawPath( chevron );

	if( model() == nullptr || model()->size() <= 0 ) { return; }
	p.setClipRect( QRect( 4, 1, width() - CB_ARROW_BTN_WIDTH - 6, height() - 2 ) );
	QPixmap pm = model()->currentData() ? model()->currentData()->pixmap() : QPixmap();
	int tx = 7;
	if( !pm.isNull() )
	{
		pm = pm.scaledToHeight( std::min( 14, pm.height() ), Qt::SmoothTransformation );
		p.drawPixmap( tx, ( height() - pm.height() ) / 2, pm );
		tx += pm.width() + 4;
	}
	QFont f = tokens::font( tokens::TextRole::Small );
	p.setFont( f );
	p.setPen( isEnabled() ? pal.text : pal.textDisabled );
	// A value too long for the field ends in an ellipsis (inside the clip), never a silent cut.
	const QRect textRect( tx, 0, width() - tx - CB_ARROW_BTN_WIDTH - 2, height() );
	p.drawText( textRect, Qt::AlignVCenter | Qt::AlignLeft,
		QFontMetrics( f ).elidedText( model()->currentText(), Qt::ElideRight, textRect.width() ) );
}




void ComboBox::wheelEvent( QWheelEvent* event )
{
	if( model() )
	{
		const int direction = (event->angleDelta().y() < 0 ? 1 : -1) * (event->inverted() ? -1 : 1);
		model()->setValue(model()->value() + direction);
		update();
		event->accept();
	}
}




void ComboBox::setItem( QAction* item )
{
	if( model() )
	{
		model()->setValue(item->data().toInt());
	}
}


} // namespace lmms::gui


