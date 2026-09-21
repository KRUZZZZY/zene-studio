/*
 * UnclaimedElements.cpp - the elements a loader did not claim, kept verbatim
 *                        so that opening and re-saving cannot destroy them.
 *                        ARCH-4 S1b.
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
 */

#include "UnclaimedElements.h"

#include <QTextStream>

namespace lmms
{

void captureUnclaimed( const QDomElement & element, const QString & key,
	QVector<UnclaimedElement> & into )
{
	if( element.isNull() ) { return; }
	UnclaimedElement captured;
	captured.key = key;
	QTextStream stream( &captured.xml );
	element.save( stream, 2 );
	stream.flush();
	into.append( captured );
}

bool reemitUnclaimed( const QVector<UnclaimedElement> & elements,
	QDomDocument & document, QDomElement & parent )
{
	bool reemitted = false;
	for( const UnclaimedElement & element : elements )
	{
		// Parsed with namespace processing OFF, the way DataFile reads a project
		// and the way the <session> verbatim path reads its block: the capture is
		// `prefix:local` exactly as the writer wrote it, and processing it here
		// would rewrite a name against namespaces this build does not know.
		QDomDocument holder;
		if( !holder.setContent( element.xml, false ) ) { continue; }
		const QDomElement root = holder.documentElement();
		if( root.isNull() ) { continue; }
		// A COPY into `document`: `root` belongs to the throwaway `holder`, which
		// dies with this iteration.
		parent.appendChild( document.importNode( root, true ) );
		reemitted = true;
	}
	return reemitted;
}

} // namespace lmms
