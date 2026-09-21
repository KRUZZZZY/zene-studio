/*
 * DataFile.h - class for reading and writing LMMS data files
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2012-2013 Paul Giblock <p/at/pgiblock.net>
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

#ifndef LMMS_DATA_FILE_H
#define LMMS_DATA_FILE_H

#include <map>
#include <QDomDocument>
#include <QStringList>
#include <vector>

#include "lmms_export.h"

class QTextStream;

namespace lmms
{

class ProjectVersion;


class LMMS_EXPORT DataFile : public QDomDocument
{

	using UpgradeMethod = void(DataFile::*)();

public:
	enum class Type
	{
		Unknown,
		SongProject,
		SongProjectTemplate,
		InstrumentTrackSettings,
		DragNDropData,
		ClipboardData,
		JournalData,
		EffectSettings,
		MidiClip
	} ;

	//! ARCH-4 S2b, SPEC-ARCH-4 1.4. \a skipSections names sections of THIS
	//! document that the caller does not want loaded. Their whole subtrees are
	//! removed from the document's own bytes BEFORE anything parses them, so no
	//! XML node and no model object is ever built for one - which is what the
	//! spec's partial-load claim ("prove the rest was not parsed") owes, and what
	//! parsing everything and then dropping the nodes cannot say
	//! (DocumentIndex.h's reduceDocumentSections states the measurement).
	//!
	//! \a skippedNames, when given, is ASSIGNED what was actually removed, in
	//! document order, and only once the reduced payload has PARSED. So a caller
	//! can answer "is this a partial load?" from that one list: it is non-empty
	//! exactly when this object holds less than the file did. It is left
	//! untouched by a load that refuses, and assigned empty by one that removes
	//! nothing.
	//!
	//! Both default to "no partial load", which is the identity - the bytes go to
	//! the parser exactly as before this overload existed.
	DataFile( const QString& fileName, const QStringList& skipSections = {},
		QStringList* skippedNames = nullptr );
	DataFile( const QByteArray& data, const QStringList& skipSections = {},
		QStringList* skippedNames = nullptr );
	DataFile( Type type );

	virtual ~DataFile() = default;

	///
	/// \brief validate
	/// performs basic validation, compared to file extension.
	///
	bool validate( QString extension );

	QString nameWithExtension( const QString& fn ) const;

	void write( QTextStream& strm );
	bool writeFile(const QString& fn, bool withResources = false);
	bool copyResources(const QString& resourcesDir); //!< Copies resources to the resourcesDir and changes the DataFile to use local paths to them
	bool hasLocalPlugins(QDomElement parent = QDomElement(), bool firstCall = true) const;

	QDomElement& content()
	{
		return m_content;
	}

	QDomElement& head()
	{
		return m_head;
	}

	Type type() const
	{
		return m_type;
	}

	unsigned int legacyFileVersion();

	///
	/// \brief setDocumentIndexEnabled
	/// Ask this document to carry a <z:index> (SPEC-ARCH-4 1.4, ARCH-4 S2a).
	///
	/// The caller decides WHETHER an index is warranted - Song answers it from
	/// the preserved set - but not WHEN it is built, because the section
	/// digests must be taken after write()'s own cleanMetaNodes() prune and
	/// only the writer knows when that has run.  Measured: computing them
	/// before the prune records digests that include elements the file does not
	/// carry.  Off by default, so a DataFile that was not asked writes the
	/// bytes it wrote before this existed.
	///
	void setDocumentIndexEnabled( bool enabled )
	{
		m_documentIndexEnabled = enabled;
	}

private:
	static Type type( const QString& typeName );
	static QString typeName( Type type );

	void cleanMetaNodes( QDomElement de );

	void mapSrcAttributeInElementsWithResources(const QMap<QString, QString>& map);

	// helper upgrade routines
	void upgrade_0_2_1_20070501();
	void upgrade_0_2_1_20070508();
	void upgrade_0_3_0_rc2();
	void upgrade_0_3_0();
	void upgrade_0_4_0_20080104();
	void upgrade_0_4_0_20080118();
	void upgrade_0_4_0_20080129();
	void upgrade_0_4_0_20080409();
	void upgrade_0_4_0_20080607();
	void upgrade_0_4_0_20080622();
	void upgrade_0_4_0_beta1();
	void upgrade_0_4_0_rc2();
	void upgrade_1_0_99();
	void upgrade_1_1_0();
	void upgrade_1_1_91();
	void upgrade_1_2_0_rc3();
	void upgrade_1_3_0();
	void upgrade_noHiddenClipNames();
	void upgrade_automationNodes();
	void upgrade_extendedNoteRange();
	void upgrade_defaultTripleOscillatorHQ();
	void upgrade_mixerRename();
	void upgrade_bbTcoRename();
	void upgrade_sampleAndHold();
	void upgrade_midiCCIndexing();
	void upgrade_loopsRename();
	void upgrade_noteTypes();
	void upgrade_fixCMTDelays();
	void upgrade_fixBassLoopsTypo();
	void findProblematicLadspaPlugins();
	void upgrade_noHiddenAutomationTracks();

	// List of all upgrade methods
	static const std::vector<UpgradeMethod> UPGRADE_METHODS;
	// List of ProjectVersions for the legacyFileVersion method
	static const std::vector<ProjectVersion> UPGRADE_VERSIONS;

	// Map with DOM elements that access resources (for making bundles)
	using ResourcesMap = std::map<QString, std::vector<QString>>;
	static const ResourcesMap ELEMENTS_WITH_RESOURCES;

	void upgrade();

	//! ARCH-4 S2b. Reduce \a data by \a skipSections and parse the answer into
	//! \a document. Answers true when the parser accepted it; on a refusal
	//! \a errorMsg / \a line / \a col carry the parser's report of the FAILED
	//! attempt, exactly as lmms::setContent left them.
	//!
	//! The removal report is written to \a skippedNames only once the reduced
	//! payload has parsed, so a reduction that succeeds and a parse that then
	//! fails cannot leave the caller holding sections removed from a document it
	//! never got.
	static bool reduceAndParse( QDomDocument& document, const QByteArray& data,
		const QStringList& skipSections, QStringList* skippedNames,
		QString& errorMsg, int& line, int& col );

	//! ARCH-4 S2b. The element name a document's own root declares as its CONTENT
	//! element - `typeName( type( root's "type" attribute ) )`, the same round
	//! trip loadData() makes - recovered from a bounded prologue scan that reads
	//! only the first StartElement. Answers an empty string for a payload no
	//! parser can start on, which makes the reducer refuse and hand the bytes on
	//! unchanged.
	static QString contentElementNameFor( const QByteArray& data );

	void loadData( const QByteArray & _data, const QString & _sourceFile,
		const QStringList & skipSections, QStringList * skippedNames );

	QString m_fileName; //!< The origin file name or "" if this DataFile didn't originate from a file
	QDomElement m_content;
	QDomElement m_head;
	Type m_type;
	unsigned int m_fileVersion;
	bool m_documentIndexEnabled = false;
} ;


} // namespace lmms

#endif // LMMS_DATA_FILE_H
