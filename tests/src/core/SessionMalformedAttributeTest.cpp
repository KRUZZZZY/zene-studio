/*
 * SessionMalformedAttributeTest.cpp - a malformed session attribute must not
 * become a valid target
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
 * License along with this program (if not, see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

//! WHAT THIS FILE PROVES. The session serialisation readers take their
//! INDICES and TARGETS out of the XML as text, and QString::toInt() answers 0
//! for a value it cannot convert - a nonnumeric text and an overflow alike. 0
//! is a VALID row, a VALID track and a VALID scene, so an unchecked
//! conversion does not fail loudly: it silently addresses the FIRST scene, the
//! FIRST track or the FIRST cell.
//!
//! The cases below are the three places that happened:
//!   - the Follow Action's jump target (SessionClip.cpp's chain reader);
//!   - a legacy <scene index="..."> row (SessionModel.cpp's scene reader);
//!   - a legacy <clip track="..." scene="..."> cell (the clip reader).
//!
//! Each case also names the well-formed document that must keep loading
//! exactly as before, so the fix cannot be "reject everything".
//!
//! The fixtures are XML strings parsed into a QDomDocument: no engine, no
//! clock, no thread and no timing, so the result is the same on every run.

#include <QtTest>

#include <QDomDocument>
#include <QDomElement>
#include <QString>

#include "SessionFollow.h"
#include "SessionModel.h"

using namespace lmms;

namespace
{

//! One XML element, parsed. A null element means the fixture itself is broken.
QDomElement parseElement( const QString& xml )
{
	QDomDocument doc;
	if( !doc.setContent( xml ) ) { return QDomElement(); }
	return doc.documentElement();
}

} // namespace


class SessionMalformedAttributeTest : public QObject
{
	Q_OBJECT
private slots:

	//! new-p12-238: a Follow Action whose jump target is not a number must not
	//! become a jump to the FIRST scene. The action keeps its chain entry, but
	//! the target it names is refused - the same refusal an out-of-grid row
	//! gets (include/SessionFollow.h's followTargetScene).
	void malformedJumpTargetIsNotSceneZero()
	{
		const QDomElement clip = parseElement( QStringLiteral(
			"<clip type=\"1\" pattern=\"3\">"
			"<followactions>"
			"<followaction type=\"9\" chance=\"1\" linked=\"1\" time=\"1\" jump=\"oops\"/>"
			"<followaction type=\"9\" chance=\"1\" linked=\"1\" time=\"1\" jump=\"99999999999999999999\"/>"
			"</followactions>"
			"</clip>" ) );
		QVERIFY( !clip.isNull() );

		ClipSlot slot;
		slot.restoreState( clip );
		QCOMPARE( slot.followActions().size(), 2 );
		for( const FollowAction& action : slot.followActions() )
		{
			QCOMPARE( action.type, FollowAction::Type::Jump );
			QVERIFY2( action.jumpTo != 0, "a malformed jump target became scene zero" );
			QCOMPARE( followTargetScene( action.type, 0, 4, action.jumpTo, 0.5 ), -1 );
		}
	}

	//! new-p12-238 (control): a well-formed chain still names its own row.
	void wellFormedJumpTargetStillNamesItsRow()
	{
		const QDomElement clip = parseElement( QStringLiteral(
			"<clip type=\"1\" pattern=\"3\">"
			"<followactions>"
			"<followaction type=\"9\" chance=\"1\" linked=\"1\" time=\"1\" jump=\"2\"/>"
			"</followactions>"
			"</clip>" ) );
		QVERIFY( !clip.isNull() );

		ClipSlot slot;
		slot.restoreState( clip );
		QCOMPARE( slot.followActions().size(), 1 );
		QCOMPARE( slot.followActions().front().jumpTo, 2 );
		QCOMPARE( followTargetScene( FollowAction::Type::Jump, 0, 4, 2, 0.5 ), 2 );
	}

	//! new-p12-238 (control): a chain entry with NO jump attribute at all keeps
	//! the legacy default of 0 - only a jump attribute that is there and is not
	//! a number is refused.
	void absentJumpTargetKeepsTheLegacyDefault()
	{
		const QDomElement clip = parseElement( QStringLiteral(
			"<clip type=\"1\" pattern=\"3\">"
			"<followactions>"
			"<followaction type=\"9\" chance=\"1\" linked=\"1\" time=\"1\"/>"
			"</followactions>"
			"</clip>" ) );
		QVERIFY( !clip.isNull() );

		ClipSlot slot;
		slot.restoreState( clip );
		QCOMPARE( slot.followActions().size(), 1 );
		QCOMPARE( slot.followActions().front().jumpTo, 0 );
	}

	//! new-p14-301: a legacy <scene index="bad"> must not have its data applied
	//! to scene 0. The malformed row is refused, and the block that carries it
	//! is declined - the same answer an out-of-grid index gets.
	void malformedLegacySceneIndexIsRejected()
	{
		const QDomElement session = parseElement( QStringLiteral(
			"<session version=\"1\" tracks=\"1\" scenes=\"2\">"
			"<scenes><scene index=\"bad\" name=\"Intro\"/></scenes>"
			"</session>" ) );
		QVERIFY( !session.isNull() );

		SessionModel model;
		QVERIFY( !model.restoreState( session ) );
		QVERIFY( model.scene( 0 ).name().isEmpty() );
		QCOMPARE( model.sceneCount(), 0 );
	}

	//! new-p14-301 (control): a well-formed legacy scene row still lands on the
	//! row it names.
	void wellFormedLegacySceneIndexStillLandsOnItsRow()
	{
		const QDomElement session = parseElement( QStringLiteral(
			"<session version=\"1\" tracks=\"1\" scenes=\"2\">"
			"<scenes><scene index=\"1\" name=\"Drop\"/></scenes>"
			"</session>" ) );
		QVERIFY( !session.isNull() );

		SessionModel model;
		QVERIFY( model.restoreState( session ) );
		QCOMPARE( model.sceneCount(), 2 );
		QCOMPARE( model.scene( 1 ).name(), QStringLiteral( "Drop" ) );
		QVERIFY( model.scene( 0 ).name().isEmpty() );
	}

	//! new-p14-302: a legacy <clip track="bad" ...> must not be restored into
	//! track 0 / scene 0 - the cell it would overwrite.
	void malformedLegacyClipCoordinatesAreRejected()
	{
		const QDomElement session = parseElement( QStringLiteral(
			"<session version=\"1\" tracks=\"2\" scenes=\"2\">"
			"<clips><clip track=\"bad\" scene=\"0\" type=\"1\" pattern=\"7\"/></clips>"
			"</session>" ) );
		QVERIFY( !session.isNull() );

		SessionModel model;
		QVERIFY( !model.restoreState( session ) );
		QVERIFY( model.slot( 0, 0 ).isEmpty() );
		QCOMPARE( model.trackCount(), 0 );
	}

	//! new-p14-302 (control): a well-formed legacy clip still lands in the cell
	//! it names.
	void wellFormedLegacyClipCoordinatesStillLandInTheirCell()
	{
		const QDomElement session = parseElement( QStringLiteral(
			"<session version=\"1\" tracks=\"2\" scenes=\"2\">"
			"<clips><clip track=\"1\" scene=\"1\" type=\"1\" pattern=\"7\"/></clips>"
			"</session>" ) );
		QVERIFY( !session.isNull() );

		SessionModel model;
		QVERIFY( model.restoreState( session ) );
		QCOMPARE( model.slot( 1, 1 ).patternId(), 7 );
		QVERIFY( model.slot( 0, 0 ).isEmpty() );
	}
};

QTEST_GUILESS_MAIN(SessionMalformedAttributeTest)
#include "SessionMalformedAttributeTest.moc"
