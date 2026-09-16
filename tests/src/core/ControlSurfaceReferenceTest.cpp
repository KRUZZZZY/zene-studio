/*
 * ControlSurfaceReferenceTest.cpp - the REGISTERED REFERENCE for the command
 * ids that no other registered test artefact names.
 *
 * Why this file exists. docs/COVERAGE-MATRIX-2026-09-13.md measures a gap as
 * `tree ids - {ids literal-referenced by any registered test artefact}` and
 * calls the result "the queue" - ids a reader cannot find in any proof that
 * the suite actually runs. The 2026-09-16 closure pass (board task #680) re-ran
 * that method at the wave-9 tip and found sixteen ids with no reference at all,
 * in six groups that arrived after the audit:
 *
 *   controller  surface_state, template_save, template_list, template_apply,
 *               template_delete            (the MIDI controller surfaces)
 *   dawproject  convention                 (the interchange convention report)
 *   midi        clients_list, reconnect_arm (controller auto-reconnection)
 *   plugin      host_chunking              (chunked host processing)
 *   project     diff, merge, conflicts, audible_diff (the mmpz-git merge set)
 *   session     follow_set, follow_get_state, arrangement_record_status
 *
 * What this file claims - and what it does NOT. It is a REACHABILITY AND
 * CONTRACT reference, deliberately: each id is asserted to be registered with
 * the four parts the scope contract names for a command (a group.verb id, both
 * schemas, a description), to carry an A16 row, to agree with that row's
 * mutating verdict, and to answer through the registry with a TYPED reply
 * (never an unknown command, never a crash). It does NOT claim each id behaves
 * correctly - a typed refusal passes here. The behavioural proofs for these
 * groups are named per group in
 * docs/reports/COVERAGE-CLOSURE-2026-09-16.md, and the ids that still have none
 * are named there as the open queue rather than papered over here.
 *
 * The distinction is the matrix's own rule: "a literal mention is all the
 * method tests, so a 0 here is a statement about the QUEUE being empty, never
 * about correctness". This file empties the queue and says which half it did.
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

#include <QtTest>

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cstdio>

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "Engine.h"

using namespace lmms;

namespace
{

//! One evidence line per measurement, flushed: the test prints what it
//! measured, not only that it passed.
void evidence( const char* label, const QString& detail )
{
	std::fprintf( stdout, "COVERAGE_REFERENCE %s %s\n", label, detail.toUtf8().constData() );
	std::fflush( stdout );
}


/*! The ids the 2026-09-16 closure pass measured as referenced by no registered
 *  test artefact. Grouped by the group that owns them, so a reader can see
 *  which feature each one belongs to; the groups are the six named in the file
 *  header. Sorted WITHIN the groups in the order the registry declares them.
 */
QStringList unreferencedIds()
{
	return {
		QStringLiteral( "controller.surface_state" ),
		QStringLiteral( "controller.template_save" ),
		QStringLiteral( "controller.template_list" ),
		QStringLiteral( "controller.template_apply" ),
		QStringLiteral( "controller.template_delete" ),
		QStringLiteral( "dawproject.convention" ),
		QStringLiteral( "midi.clients_list" ),
		QStringLiteral( "midi.reconnect_arm" ),
		QStringLiteral( "plugin.host_chunking" ),
		QStringLiteral( "project.diff" ),
		QStringLiteral( "project.merge" ),
		QStringLiteral( "project.conflicts" ),
		QStringLiteral( "project.audible_diff" ),
		QStringLiteral( "session.follow_set" ),
		QStringLiteral( "session.follow_get_state" ),
		QStringLiteral( "session.arrangement_record_status" ),
	};
}

} // namespace


class ControlSurfaceReferenceTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		Engine::init( true );
		// The registry only dispatches handlers once the application says the
		// model is fully up; the unit test sets that flag itself.
		ControlRegistry::setReady( true );
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady( false );
		Engine::destroy();
	}

	//! Every id carries the four parts the scope contract names for a command,
	//! and the A16 row that classifies it. A missing row is a failure, not a
	//! "nothing to check" - the same rule the contract's own test follows.
	void everyUnreferencedIdCarriesTheCommandContract()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		int checked = 0;
		for ( const QString& id : unreferencedIds() )
		{
			const QByteArray why = id.toUtf8();

			QVERIFY2( registry->hasCommand( id ), why.constData() );
			const ControlCommand* cmd = registry->command( id );
			QVERIFY2( cmd != nullptr, why.constData() );

			// A group.verb id whose group is the id's own prefix - what the MCP
			// bridge derives its tool grouping from.
			QCOMPARE( cmd->group, id.section( QStringLiteral( "." ), 0, 0 ) );
			QCOMPARE( cmd->group + QStringLiteral( "." ) + cmd->verb, id );
			QVERIFY2( !cmd->description.isEmpty(), why.constData() );
			QVERIFY2( !cmd->argsSchema.isEmpty(), why.constData() );
			QVERIFY2( !cmd->resultSchema.isEmpty(), why.constData() );

			const control::ReversibilityEntry* row =
				control::ReversibilityTable::instance().lookup( id );
			QVERIFY2( row != nullptr,
				qPrintable( id + QStringLiteral( ": no A16 row in the contract table" ) ) );
			QCOMPARE( row->command, id );

			// The direction the contract's own test asserts, per id: a command
			// cannot be declared mutating and classed not_mutating at the same
			// time.
			if ( row->cls == control::ReversibilityClass::NotMutating )
			{
				QVERIFY2( !cmd->mutating,
					qPrintable( id + QStringLiteral( " is mutating but classed not_mutating" ) ) );
			}
			++checked;
		}
		QCOMPARE( checked, unreferencedIds().size() );
		evidence( "contract", QStringLiteral( "%1 ids: id, schemas, description, A16 row")
			.arg( checked ) );
	}

	//! ...and each one ANSWERS: a typed reply through the registry - the
	//! command's own result, or a typed refusal. Never an unknown command, never
	//! a hang and never a crash. This is the reachability half of the
	//! reference; a typed refusal is an acceptable outcome here.
	void everyUnreferencedIdAnswersWithATypedReply()
	{
		ControlRegistry* registry = ControlRegistry::instance();
		int answered = 0;
		for ( const QString& id : unreferencedIds() )
		{
			const ControlResult reply = registry->invoke( id, QJsonObject() );
			QVERIFY2( reply.ok || !reply.errorMessage.isEmpty(),
				qPrintable( id + QStringLiteral( ": neither a result nor a typed error - the "
					"reply cannot be read as either" ) ) );
			QVERIFY2( reply.errorKind != ControlErrorKind::None || reply.ok,
				qPrintable( id + QStringLiteral( ": an error kind with nothing said" ) ) );
			if ( !reply.ok )
			{
				evidence( "typed-refusal", QStringLiteral( "%1: %2")
					.arg( id, controlErrorKindName( reply.errorKind ) ) );
			}
			++answered;
		}
		QCOMPARE( answered, unreferencedIds().size() );
		evidence( "reachability", QStringLiteral( "%1 ids answered a typed reply" ).arg( answered ) );
	}
};

QTEST_GUILESS_MAIN( ControlSurfaceReferenceTest )
#include "ControlSurfaceReferenceTest.moc"
