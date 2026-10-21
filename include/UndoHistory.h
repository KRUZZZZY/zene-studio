/*
 * UndoHistory.h - WF-033: the undo history saved with the project
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

#ifndef LMMS_UNDO_HISTORY_H
#define LMMS_UNDO_HISTORY_H

#include <QJsonObject>
#include <QString>

#include "lmms_export.h"
#include "ProjectJournal.h"
#include "ProjectSection.h"

class QDomDocument;

namespace lmms
{

class DataFile;

/*! WF-033 - the newest undo steps saved WITH the project, as compressed
 *  whole-project snapshots (option C of plans/DESIGN-WF-033-UNDO-PERSISTENCE.md;
 *  the decision record is docs/UNDO-HISTORY-PERSISTENCE.md).
 *
 *  The owner's defaults, each a user setting (config class `undohistory`):
 *    - `persist` 0  - OFF by default; a project opts in with its own setting
 *                     (project.set_undo_history), which overrides this default;
 *    - `steps`   20 - the newest steps kept;
 *    - `capmb`   64 - the compressed bytes they may take together. Over it the
 *                     OLDEST go first; a snapshot that alone exceeds it means the
 *                     history is not saved from that step back, and the save says
 *                     so (the result's `undo_history`, a status message).
 *  Plug-in state is whatever a normal save writes - the snapshot IS a save's
 *  document - so there is no second plug-in format.
 *
 *  FORMAT: one top-level `<z:undohistory v="1">` in <song> (the SPEC-ARCH-4
 *  compatibility rule: new state only in a namespaced top-level section), written
 *  only when the project opted in - a project that did not saves the bytes it
 *  always did. Its `fingerprint` digests the rest of the document, so a file a
 *  build that ignores the section re-saved after an edit (keeping the section
 *  verbatim as unclaimed, SPEC-ARCH-4 1.6.1) is detected and its stale history
 *  discarded instead of undoing to the wrong project.
 *
 *  The in-memory session bounds (docs/UNDO-BOUNDS.md) are unchanged and separate:
 *  snapshots are counted against THESE caps, never against the session's budget. */
class LMMS_EXPORT UndoHistory : public ProjectSection, public ProjectJournal::SnapshotPolicy
{
public:
	static constexpr int DefaultSteps = 20;
	static constexpr int MaxStepsLimit = 1000;
	static constexpr int DefaultCapMb = 64;
	static constexpr int MaxCapMb = 1024;

	static UndoHistory& instance();

	//! Installs this policy in \a journal (Song's construction, via projectsections::registerAll).
	void attach(ProjectJournal* journal);

	// ---- the user settings (ConfigManager, class "undohistory") ----
	static bool defaultEnabled();
	static int maxSteps();
	static qint64 capBytes();
	static void setDefaultEnabled(bool on);
	//! False, and nothing changes, outside [1, MaxStepsLimit] / [1, MaxCapMb].
	static bool setMaxSteps(int steps);
	static bool setCapMb(int megabytes);

	//! The project's own setting: follow the default, or opt in / out.
	enum class Setting { Default, On, Off };
	static QString settingName(Setting setting);
	static bool parseSetting(const QString& name, Setting* setting);
	Setting projectSetting() const { return m_setting; }
	void setProjectSetting(Setting setting);
	//! On for this project: its own setting, else the default.
	bool enabled() const;

	//! What the last real save did with the history (project.save's `undo_history`).
	struct SaveReport
	{
		bool enabled = false;
		bool written = false;
		int savedSteps = 0;
		qint64 savedBytes = 0;
		int droppedOldest = 0;
		bool oversize = false;
		qint64 oversizeBytes = 0;
		int unavailable = 0;
		//! Non-empty when something the user would expect was NOT saved: shown.
		QString warning;
		QJsonObject toJson() const;
	};
	//! What the last load did with a history it found.
	struct LoadReport
	{
		bool found = false;
		int loadedSteps = 0;
		QString discarded;
		QJsonObject toJson() const;
	};
	const SaveReport& lastSave() const { return m_save; }
	const LoadReport& lastLoad() const { return m_load; }
	/*! The journal serial of the newest step the last load put back, 0 when it
	 *  put back none. control.undo reads it: the open's own record is
	 *  irreversible, yet the steps the open loaded are undoable - and while that
	 *  record is still the newest, the journal's steps are exactly those. */
	quint64 loadedTopSerial() const { return m_loadedTopSerial; }

	/*! Marks a REAL save (project.save, the interface's Save / Save As): only
	 *  inside one is the history written. Templates, autosave, render and stem
	 *  temporaries hold none - they are not the project's own file. */
	class SaveScope
	{
	public:
		SaveScope() { ++instance().m_saveDepth; }
		~SaveScope() { --instance().m_saveDepth; }
		SaveScope(const SaveScope&) = delete;
		SaveScope& operator=(const SaveScope&) = delete;
	};

	/*! The last step of a save (Song::saveProjectFile): fills the fingerprint
	 *  over the finished document and settles the report. A bundle save
	 *  (\a withResources) rewrites sample paths after this point, so a history
	 *  is never put in a bundle - and the report says so. */
	void seal(DataFile& file, bool withResources);
	/*! The first step of a load (Song::loadProject, before any reader runs):
	 *  takes the document's fingerprint while the DOM is still exactly the
	 *  file's, so no reader's in-place repair can make a sound history look
	 *  stale. Only when the document carries a history section. */
	void beginLoad(const DataFile& file);

	//! Restore bracket for Song::restoreUndoSnapshot: a restore's load must not
	//! change the project's setting or the load report.
	struct ProjectState
	{
		Setting setting = Setting::Default;
		LoadReport load;
		quint64 loadedTopSerial = 0;
	};
	ProjectState projectState() const { return ProjectState{m_setting, m_load, m_loadedTopSerial}; }
	void setProjectState(const ProjectState& state);

	/*! The document's fingerprint: <head> and every <song> section except the
	 *  history itself, the append-only provenance log and the document index,
	 *  each as DocumentIndex's canonical digest (process-independent), plus the
	 *  root's format attributes - the SCHEME v2 hash, whose value says which
	 *  scheme wrote it ("sha256:v2:...").
	 *  \a version and \a type are the root's attributes AS THE FILE HAD THEM:
	 *  a load that upgraded the file must still measure the version the file
	 *  claims, or a file whose root version was changed under it would look
	 *  like the project its history was written from.
	 *  \a stripMetadata drops `metadata="1"` nodes first, as the writer does on
	 *  its way to disk - so the save-time and load-time values agree. */
	static QString fingerprint(const QDomDocument& document, bool stripMetadata,
		const QString& version, const QString& type);

	/*! SCHEME v1: the sections only, as written before the root's format
	 *  attributes were part of the fingerprint. A history written by such a
	 *  build is still honoured - but only when the file's format did not
	 *  change under the load, which is what the version the file claims would
	 *  have told us. */
	static QString legacyFingerprint(const QDomDocument& document, bool stripMetadata);

	// ---- ProjectSection ----
	QString elementName() const override;
	bool shouldPersist() const override;
	void save(QDomDocument& doc, QDomElement& parent) const override;
	void load(const QDomElement& element) override;
	void clear() override;

	// ---- ProjectJournal::SnapshotPolicy ----
	bool capturing() const override;
	int keepSteps() const override { return maxSteps(); }
	qint64 keepBytes() const override { return capBytes(); }
	ProjectJournal::Snapshot capture() override;

private:
	UndoHistory();
	//! The plan a save would carry out now, over the live undo stack.
	ProjectJournal::PersistPlan currentPlan() const;
	void loadSteps(const QDomElement& section);
	//! Whether the stored fingerprint describes THIS document.
	bool fingerprintMatches(const QString& stored) const;

	//! beginLoad()'s fingerprints, consumed by load(): the scheme v2 one and
	//! the scheme v1 one (a history written before the scheme was versioned).
	QString m_loadFingerprint;
	QString m_loadLegacyFingerprint;
	//! True when the file's format changed under the load (an upgrade ran).
	bool m_loadSourceFormatChanged = false;
	Setting m_setting = Setting::Default;
	int m_saveDepth = 0;
	SaveReport m_save;
	LoadReport m_load;
	quint64 m_loadedTopSerial = 0;
};

} // namespace lmms

#endif // LMMS_UNDO_HISTORY_H
