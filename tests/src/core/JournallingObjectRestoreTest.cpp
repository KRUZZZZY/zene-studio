/*
 * JournallingObjectRestoreTest.cpp - restoring a journal id with no project journal.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of Zene Studio (an LMMS-derived product).
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

// JournallingObject::restoreState() re-binds the object to the id its file
// carries (`<journallingObject id=N>`), and that goes through changeID(), which
// talks to Engine::projectJournal(). Outside a live engine that journal is
// null: an object built before Engine::init() - or restored after
// Engine::destroy() - must restore without dereferencing it. The binary never
// calls Engine::init(), so the journal is null for the whole run.

#include "Engine.h"
#include "JournallingObject.h"

#include <QDomDocument>
#include <QDomElement>
#include <QString>
#include <QtTest>

namespace
{

//! A concrete journalling object: JournallingObject is abstract only through
//! SerializingObject's settings hooks, which this test has no use for.
class ProbeObject : public lmms::JournallingObject
{
public:
	QString nodeName() const override
	{
		return QStringLiteral("probeobject");
	}

	void saveSettings( QDomDocument &, QDomElement & ) override {}
	void loadSettings( const QDomElement & ) override {}
};

//! `<probeobject><journallingObject id="N" metadata="true"/></probeobject>`,
//! the shape JournallingObject::saveState writes.
QDomElement savedElement( QDomDocument & doc, const lmms::jo_id_t id )
{
	QDomElement root = doc.createElement( "probeobject" );
	doc.appendChild( root );

	QDomElement journalNode = doc.createElement( "journallingObject" );
	journalNode.setAttribute( "id", QString::number( id ) );
	journalNode.setAttribute( "metadata", true );
	root.appendChild( journalNode );

	return root;
}

} // namespace


class JournallingObjectRestoreTest : public QObject
{
	Q_OBJECT

private slots:

	void initTestCase()
	{
		// The precondition the whole test rests on: no engine, so no journal.
		QVERIFY( lmms::Engine::projectJournal() == nullptr );
	}

	void restoringAFileIdWithNoProjectJournalDoesNotDereferenceIt()
	{
		// Before the fix this line dereferenced the null journal in changeID()
		// and the process died with SIGSEGV; the test is RED by crashing.
		ProbeObject probe;
		QCOMPARE( probe.id(), lmms::jo_id_t{ 0 } );

		QDomDocument doc;
		const QDomElement root = savedElement( doc, 77 );
		probe.restoreState( root );

		// Nothing can be registered without a journal, so the object keeps the
		// id 0 that "no journal saw this object" means (JournallingObject.cpp).
		QCOMPARE( probe.id(), lmms::jo_id_t{ 0 } );
		QVERIFY( lmms::Engine::projectJournal() == nullptr );
	}

	void restoringAZeroIdWithNoProjectJournalIsStillANoOp()
	{
		// The zero-id branch was never reached through changeID(); keep it so.
		ProbeObject probe;

		QDomDocument doc;
		const QDomElement root = savedElement( doc, 0 );
		probe.restoreState( root );

		QCOMPARE( probe.id(), lmms::jo_id_t{ 0 } );
	}

	void restoringTheSameObjectTwiceWithNoProjectJournalStaysSafe()
	{
		// A second restore of an already-restored object takes the same path
		// (no journal at either call) and must still leave the id at 0.
		ProbeObject probe;

		QDomDocument doc;
		const QDomElement root = savedElement( doc, 1234 );
		probe.restoreState( root );
		probe.restoreState( root );

		QCOMPARE( probe.id(), lmms::jo_id_t{ 0 } );
		QVERIFY( probe.isJournalling() );
	}
};

QTEST_GUILESS_MAIN( JournallingObjectRestoreTest )

#include "JournallingObjectRestoreTest.moc"
