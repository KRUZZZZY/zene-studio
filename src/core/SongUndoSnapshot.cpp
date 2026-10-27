/*
 * SongUndoSnapshot.cpp - WF-033: the whole project as one undo snapshot, and
 *                        the in-place restore a loaded undo step runs
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

/*
 * Song member functions in their own translation unit (the SongSections.cpp
 * precedent): Song.cpp is upstream-inherited and these are the fork's WF-033
 * additions. The design is docs/UNDO-HISTORY-PERSISTENCE.md.
 *
 * A SNAPSHOT is the document a save builds (Song::buildProjectDocument - the
 * same writers, the same plug-in state, the same sample references), with two
 * differences that are the whole point:
 *   - the `<journallingObject id>` metadata a save prunes on its way to disk is
 *     KEPT, so a restore gives every object back the journal id the steps above
 *     it name (JournallingObject::restoreState reads it);
 *     ...and every AutomatableModel writes its id too (a save writes it only for an automated
 *     model), for the same reason;
 *   - the append-only `<z:provenance>` log is LEFT OUT: an undo never rewinds
 *     the record of what was done (AGENTS.md, "Known product contracts").
 * It is taken with ProjectIds::PureSaveScope so a snapshot never counts as a
 * revision of any object.
 *
 * A RESTORE is a project load of that document, in place. A load clears the
 * journal, the provenance log, the per-project undo-history setting, the
 * container format and the file name, and rewinds the id counter; each of
 * those is held across it and put back, because the restore is an undo step,
 * not an open.
 */

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "DataFile.h"
#include "Engine.h"
#include "ProjectIds.h"
#include "ProjectJournal.h"
#include "ProvenanceSection.h"
#include "Song.h"
#include "UndoHistory.h"

namespace lmms
{

std::shared_ptr<const QByteArray> Song::captureUndoSnapshot(QStringList* skippedSections)
{
	if( (isPartialLoad() && skippedSections == nullptr) || m_loadingProject ) { return {}; }
	if( skippedSections != nullptr ) { *skippedSections = m_notLoadedSections; }
	const ProjectIds::PureSaveScope pure;
	DataFile dataFile( DataFile::Type::SongProject );
	// Every model writes its journal id, exactly, while this is set (ProjectJournal::idToWrite): a save
	// writes a model's id only when it is automated, and with its top bit cleared - and an undo step
	// above this one may name any model by the id it has now.
	ProjectJournal::setSnapshotIds( true );
	buildProjectDocument( dataFile );
	ProjectJournal::setSnapshotIds( false );
	const QDomElement log = dataFile.content().firstChildElement( provenance::nodeName() );
	if( !log.isNull() ) { dataFile.content().removeChild( log ); }
	const QDomElement history = dataFile.content().firstChildElement( UndoHistory::instance().elementName() );
	if( !history.isNull() ) { dataFile.content().removeChild( history ); }
	return std::make_shared<const QByteArray>( qCompress( dataFile.toByteArray(), 6 ) );
}




bool Song::restoreUndoSnapshot( const QByteArray & compressed )
{
	return restoreUndoSnapshot( compressed, m_fileName );
}




bool Song::restoreUndoSnapshot( const QByteArray & compressed, const QString & restoredFileName,
	const QStringList& skippedSections )
{
	const QByteArray document = qUncompress( compressed );
	QTemporaryDir dir;
	if( document.isEmpty() || !dir.isValid() ) { return false; }
	const QString path = QDir( dir.path() ).filePath( QStringLiteral( "undo-snapshot.mmp" ) );
	{
		QFile file( path );
		if( !file.open( QIODevice::WriteOnly ) || file.write( document ) != document.size() ) { return false; }
	}

	// What the load would otherwise reset - held across it.
	DataFile logCarrier( DataFile::Type::SongProject );
	provenance::writeTo( logCarrier );
	const QDomElement log = logCarrier.content().firstChildElement( provenance::nodeName() );
	ProjectJournal * journal = Engine::projectJournal();
	ProjectJournal::DetachedHistory history = journal->detachHistory();
	const UndoHistory::ProjectState persistence = UndoHistory::instance().projectState();
	const QString fileName = restoredFileName;
	const QString oldFileName = m_oldFileName;
	const int nextId = ProjectIds::next();
	// The snapshot is plain XML, so the load would read the session as a legacy
	// project. The format kept here is the session's own: an ordinary undo of the
	// same project stays in its format. A caller that puts back a session captured
	// in another format (a checkpoint's displaced session) sets it after a restore.
	const bool containerV2 = m_containerV2Project;

	m_restoringSnapshot = true;
	loadProject( path );
	m_restoringSnapshot = false;
	const bool restored = m_loadRefusal.isEmpty();
	if( restored ) { m_notLoadedSections = skippedSections; }

	setProjectFileName( fileName );
	m_oldFileName = oldFileName;
	m_containerV2Project = containerV2;
	// An id is never handed out twice: an object the restore removed keeps its
	// address retired, as a structural undo does.
	ProjectIds::observeNext( nextId );
	provenance::Section::instance().clear();
	if( !log.isNull() ) { provenance::Section::instance().load( log ); }
	UndoHistory::instance().setProjectState( persistence );
	journal->reattachHistory( std::move( history ) );
	setModified();
	return restored;
}

} // namespace lmms
