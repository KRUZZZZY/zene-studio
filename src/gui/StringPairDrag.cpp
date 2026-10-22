/*
 * StringPairDrag.cpp - class StringPairDrag which provides general support
 *                        for drag'n'drop of string-pairs and which is the base
 *                        for all drag'n'drop-actions within LMMS
 *
 * Copyright (c) 2005-2008 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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


#include <QMimeData>
#include <QDragEnterEvent>
#include <QUrl>


#include "StringPairDrag.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "Clipboard.h"


namespace lmms::gui
{

namespace
{

/*! Whether \a _key is one the browser starts a drag under with a file path as the value: the file
 *  lists' rows (FileBrowser) and the search results (BrowserResults) drag every file-backed kind
 *  this way, and every consumer of these keys already treats the value as a filesystem path. Only
 *  such a drag also publishes its file as a `text/uri-list` URL; every other StringPair value -
 *  a serialized clip, embedded sample data, a model id - is not a path and is never read as one. */
bool carriesAFilePath( const QString & _key )
{
	return _key == QLatin1String( "samplefile" )
		|| _key == QLatin1String( "presetfile" )
		|| _key == QLatin1String( "pluginpresetfile" )
		|| _key == QLatin1String( "soundfontfile" )
		|| _key == QLatin1String( "patchfile" )
		|| _key == QLatin1String( "vstpluginfile" )
		|| _key == QLatin1String( "importedproject" )
		|| _key == QLatin1String( "projectfile" );
}

} // namespace


StringPairDrag::StringPairDrag( const QString & _key, const QString & _value,
					const QPixmap & _icon, QWidget * _w ) :
	QDrag( _w )
{
	if( _icon.isNull() && _w )
	{
		setPixmap( _w->grab().scaled(
						64, 64,
						Qt::KeepAspectRatio,
						Qt::SmoothTransformation ) );
	}
	else
	{
		setPixmap( _icon );
	}
	setMimeData( createMimeData( _key, _value ) );
	exec( Qt::CopyAction, Qt::CopyAction );
}




QMimeData * StringPairDrag::createMimeData( const QString & _key, const QString & _value )
{
	// For mimeType() and MimeType enum class
	using namespace Clipboard;

	auto m = new QMimeData();
	m->setData( mimeType( MimeType::StringPair ),
			( Clipboard::encodeStringPairKey( _key ) + ":" + _value ).toUtf8() );
	m->setData( mimeType( MimeType::StringPairV2 ),
			( Clipboard::encodeStringPairKey( _key ) + ":" + _value ).toUtf8() );
	if( carriesAFilePath( _key ) )
	{
		// The same file as a URL, for a plug-in editor or the desktop that takes file URLs; the
		// StringPair payload above still drives every drop target inside Zene unchanged.
		m->setUrls( { QUrl::fromLocalFile( _value ) } );
	}
	return m;
}




StringPairDrag::~StringPairDrag()
{
	// during a drag, we might have lost key-press-events, so reset
	// modifiers of main-win
	if( getGUI()->mainWindow() )
	{
		getGUI()->mainWindow()->clearKeyModifiers();
	}
}




bool StringPairDrag::processDragEnterEvent( QDragEnterEvent * _dee,
						const QString & _allowed_keys )
{
	// For mimeType() and MimeType enum class
	using namespace Clipboard;

	if( !_dee->mimeData()->hasFormat( mimeType( MimeType::StringPair ) ) &&
		!_dee->mimeData()->hasFormat( mimeType( MimeType::StringPairV2 ) ) )
	{
		return( false );
	}
	// The key is read through the shared decoder, so a key that carries a colon is compared whole
	// and not split at that colon.
	QString key = Clipboard::decodeKey( _dee->mimeData() );
	if( _allowed_keys.split( ',' ).contains( key ) )
	{
		_dee->acceptProposedAction();
		return( true );
	}
	_dee->ignore();
	return( false );
}




QString StringPairDrag::decodeKey( QDropEvent * _de )
{
	return Clipboard::decodeKey( _de->mimeData() );
}




QString StringPairDrag::decodeValue( QDropEvent * _de )
{
	return Clipboard::decodeValue( _de->mimeData() );
}


} // namespace lmms::gui
