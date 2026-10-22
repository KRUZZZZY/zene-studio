/*
 * Clipboard.cpp - the clipboard for clips, notes etc.
 *
 * Copyright (c) 2004-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include <QApplication>
#include <QClipboard>
#include <QMimeData>

#include "Clipboard.h"


namespace lmms::Clipboard
{

namespace
{

/*! The version marker that opens the key half of a StringPair payload whose key Zene had to
 *  escape. A key that carries neither a colon nor a percent sign needs no escaping and is written
 *  as it always was, so every payload Zene wrote before keeps its bytes; only a key that carries
 *  one of them is marked and escaped. A key half that does not open with this marker was written
 *  by an older Zene, or by hand, and is read literally - so a legacy key that carries the text
 *  "%3A" or "%25" keeps its spelling instead of being decoded into something else. */
const QString markedKeyPrefix = QStringLiteral( "%1" );


/*! Whether \a text carries an escape sequence the encoder writes: "%3A" for a colon, "%25" for a
 *  percent sign. */
bool carriesEscape( const QString & text )
{
	return( text.contains( QLatin1String( "%3A" ) ) || text.contains( QLatin1String( "%25" ) ) );
}


/*! The key half of a StringPair payload, unescaped. Only a marked key half is unescaped; an
 *  unmarked one is a payload that was never escaped and is read literally. */
QString decodeStringPairKey( const QString & keyHalf )
{
	if( !keyHalf.startsWith( markedKeyPrefix ) )
	{
		// An older Zene's payload, or one written by hand: the key is read as it was written.
		return keyHalf;
	}
	const QString escaped = keyHalf.mid( markedKeyPrefix.size() );
	if( !carriesEscape( escaped ) )
	{
		// A marked key half always carries an escape, so one that carries none was never marked.
		return keyHalf;
	}
	QString key;
	key.reserve( escaped.size() );
	for( int i = 0; i < escaped.size(); ++i )
	{
		const QChar c = escaped.at( i );
		if( c == QLatin1Char( '%' ) && i + 2 < escaped.size() )
		{
			const QString escape = escaped.mid( i + 1, 2 );
			if( escape == QLatin1String( "3A" ) ) { key += QLatin1Char( ':' ); i += 2; continue; }
			if( escape == QLatin1String( "25" ) ) { key += QLatin1Char( '%' ); i += 2; continue; }
		}
		key += c;
	}
	return key;
}

} // namespace


	const QMimeData * getMimeData()
	{
		// The clipboard can hold nothing at all - no owner yet, an offscreen or headless
		// session - and Qt then returns null, which every caller read through: opening any
		// control's context menu crashed in floatFromClipboard (BUGS_FOUND 11.13). An empty
		// one stands in, so "nothing to paste" is what they see.
		static const QMimeData empty;
		const QMimeData * data = QApplication::clipboard()->mimeData( QClipboard::Clipboard );
		return data != nullptr ? data : &empty;
	}




	bool hasFormat( MimeType mT )
	{
		return getMimeData()->hasFormat( mimeType( mT ) );
	}




	void copyString( const QString & str, MimeType mT )
	{
		auto content = new QMimeData;

		content->setData( mimeType( mT ), str.toUtf8() );
		QApplication::clipboard()->setMimeData( content, QClipboard::Clipboard );
	}




	QString getString( MimeType mT )
	{
		return QString( getMimeData()->data( mimeType( mT ) ) );
	}




	void copyStringPair( const QString & key, const QString & value )
	{
		QString finalString = encodeStringPairKey( key ) + ":" + value;

		auto content = new QMimeData;
		content->setData( mimeType( MimeType::StringPair ), finalString.toUtf8() );
		content->setData( mimeType( MimeType::StringPairV2 ),
			( encodeStringPairKey( key ) + ":" + value ).toUtf8() );
		QApplication::clipboard()->setMimeData( content, QClipboard::Clipboard );
	}




	QString encodeStringPairKey( const QString & key )
	{
		if( !key.contains( QLatin1Char( ':' ) ) && !key.contains( QLatin1Char( '%' ) ) )
		{
			// Nothing to escape: the key half is written as it always was, so every payload Zene
			// wrote before is written the same way now.
			return key;
		}
		QString encoded;
		encoded.reserve( key.size() + markedKeyPrefix.size() );
		encoded += markedKeyPrefix;
		for( const QChar c : key )
		{
			if( c == QLatin1Char( ':' ) ) { encoded += QLatin1String( "%3A" ); }
			else if( c == QLatin1Char( '%' ) ) { encoded += QLatin1String( "%25" ); }
			else { encoded += c; }
		}
		return encoded;
	}




	QString decodeKey( const QMimeData * mimeData )
	{
		const bool isV2 = mimeData->hasFormat( mimeType( MimeType::StringPairV2 ) );
		const QString payload = QString::fromUtf8(
			mimeData->data( mimeType( isV2 ? MimeType::StringPairV2 : MimeType::StringPair ) ) );
		const QString key = payload.section( ':', 0, 0 );
		return isV2 ? decodeStringPairKey( key ) : key;
	}




	QString decodeValue( const QMimeData * mimeData )
	{
		const MimeType type = mimeData->hasFormat( mimeType( MimeType::StringPairV2 ) )
			? MimeType::StringPairV2 : MimeType::StringPair;
		return( QString::fromUtf8( mimeData->data( mimeType( type ) ) ).section( ':', 1, -1 ) );
	}


} // namespace lmms::Clipboard
