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

/*! The key half of a StringPair payload, unescaped. A percent sign that is not followed by `3A` or
 *  `25` stays a literal percent sign, so a payload that was never escaped - one written by an older
 *  Zene, or by hand - decodes exactly as it was read before. */
QString decodeStringPairKey( const QString & encodedKey )
{
	QString key;
	key.reserve( encodedKey.size() );
	for( int i = 0; i < encodedKey.size(); ++i )
	{
		const QChar c = encodedKey.at( i );
		if( c == QLatin1Char( '%' ) && i + 2 < encodedKey.size() )
		{
			const QString escape = encodedKey.mid( i + 1, 2 );
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
		QApplication::clipboard()->setMimeData( content, QClipboard::Clipboard );
	}




	QString encodeStringPairKey( const QString & key )
	{
		QString encoded;
		encoded.reserve( key.size() );
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
		const QString payload = QString::fromUtf8(
			mimeData->data( mimeType( MimeType::StringPair ) ) );
		return( decodeStringPairKey( payload.section( ':', 0, 0 ) ) );
	}




	QString decodeValue( const QMimeData * mimeData )
	{
		return( QString::fromUtf8( mimeData->data( mimeType( MimeType::StringPair ) ) ).section( ':', 1, -1 ) );
	}


} // namespace lmms::Clipboard
