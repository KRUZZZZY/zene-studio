/*
 * ControlCommandsDocuments.cpp - project.new_document, project.recent, project.switch (WF-048, WF-042)
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
 *
 */
/*! 0.5.0 F3:
 *  - project.new_document (WF-048): a document type carries a template - the tracks and devices that kind
 *    of work starts with. The user's own <template dir>/<type>.mpt wins; without one, production is the
 *    default template (what project.new opens) and mastering and live are built from the recipes below,
 *    written to a temporary template and opened as one - so the new document is unmodified, has no file
 *    and an empty undo history, exactly as a template's is.
 *  - project.recent / project.switch (WF-042): a quick switch between recent projects - the session is
 *    saved first - and, with keep_as_reference, the project left behind stays open as the reference
 *    (ReferenceProject.h) to copy back from. */

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QStandardPaths>
#include <QTemporaryFile>

#include "ConfigManager.h"
#include "ControlArrangingCommands.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "DataFile.h"
#include "ProjectInfo.h"
#include "ProjectJournal.h"
#include "ProvenanceSection.h"
#include "ReferenceProject.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

struct RecipeTrack
{
	Track::Type type;
	const char* name;
	const char* instrument;  //!< nullptr for a sample track
	bool muted;
};

//! A built-in document template: the song's tracks (the default template's are replaced) and the
//! master channel's devices.
struct Recipe
{
	const char* type;
	std::vector<RecipeTrack> tracks;
	QStringList masterDevices;
};

const std::vector<Recipe>& recipes()
{
	using T = Track::Type;
	// Mastering: the mix to master on one sample track, a reference to compare it with (muted: solo it to
	// listen - it must not run through the mastering chain), and the chain on the master channel. Live: a
	// launcher's worth of tracks, two playable instruments and three audio tracks for loops and stems.
	static const std::vector<Recipe> all{
		{"mastering", {{T::Sample, "Mix", nullptr, false}, {T::Sample, "Reference", nullptr, true}},
			{QStringLiteral("eq"), QStringLiteral("lomm"), QStringLiteral("compressor"), QStringLiteral("analyzer")}},
		{"live", {{T::Instrument, "Keys", "tripleoscillator", false}, {T::Instrument, "Bass", "lb302", false},
			{T::Sample, "Drums", nullptr, false}, {T::Sample, "Loops", nullptr, false}, {T::Sample, "Vocals", nullptr, false}}, {}},
	};
	return all;
}

const Recipe* recipeFor(const QString& type)
{
	for (const Recipe& recipe : recipes()) { if (type == QLatin1String(recipe.type)) { return &recipe; } }
	return nullptr;
}

void addTrack(const RecipeTrack& spec)
{
	Track* track = Track::create(spec.type, Engine::getSong());
	if (track == nullptr) { return; }
	track->setName(QString::fromLatin1(spec.name));
	track->setMuted(spec.muted);
	if (auto* instrument = dynamic_cast<InstrumentTrack*>(track); instrument != nullptr && spec.instrument != nullptr)
	{
		instrument->loadInstrument(QString::fromLatin1(spec.instrument));
	}
}

//! The master's devices of @a recipe; the names this build cannot load come back.
QStringList addMasterDevices(const Recipe& recipe)
{
	QStringList missing;
	EffectChain& chain = Engine::mixer()->mixerChannel(0)->m_fxChain;
	for (const QString& name : recipe.masterDevices)
	{
		Effect* effect = Effect::instantiate(name, &chain, nullptr);
		if (effect != nullptr && effect->isOkay()) { chain.appendEffect(effect); }
		else { delete effect; missing << name; }
	}
	return missing;
}

//! The session rebuilt as @a recipe's document - outside the journal: it becomes a template next.
QStringList buildRecipe(const Recipe& recipe)
{
	Engine::projectJournal()->setJournalling(false);
	const std::vector<Track*> defaults = Engine::getSong()->tracks();
	for (Track* track : defaults) { removeTrack(track); }
	for (const RecipeTrack& spec : recipe.tracks) { addTrack(spec); }
	const QStringList missing = addMasterDevices(recipe);
	ProjectInfo::instance().setDocumentType(QLatin1String(recipe.type));
	Engine::projectJournal()->setJournalling(true);
	return missing;
}

/*! Where a built-in recipe's template is written before it is opened: one file per type in the cache
 *  directory, rewritten each time. A stable path rather than a temporary file because opening a template
 *  lists it in the recent projects (Song::loadProject), and a deleted file there would be a dead entry. */
QString recipeTemplatePath(const Recipe& recipe)
{
	const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("document-templates"));
	QDir().mkpath(dir);
	return QDir(dir).filePath(QLatin1String(recipe.type) + QStringLiteral(".mpt"));
}

//! Check the cache destination before project.new can discard the current session.
bool recipeTemplateWritable(const QString& path)
{
	const QFileInfo destination(path);
	if (destination.exists() && (!destination.isFile() || !destination.isWritable())) { return false; }
	const QString dir = destination.absolutePath();
	if (!QDir().mkpath(dir)) { return false; }
	QTemporaryFile probe(QDir(dir).filePath(QStringLiteral(".zene-template-XXXXXX")));
	if (!probe.open()) { return false; }
	const bool written = probe.write("zene") == 4 && probe.flush();
	probe.close();
	return written && probe.remove();
}

//! Reload the session saved before a built-in recipe was tried: its file, modified flag, document type and,
//! because the restore is an undo-snapshot restore (Song::restoreUndoSnapshot), the undo/redo steps around it
//! and the append-only provenance log. The content and the history are one document again, not just the file.
ControlResult restoreSession(Song& song, const QByteArray& snapshot, const QString& previousFile,
	const QStringList& skippedSections, bool previousModified, bool previousContainerV2)
{
	if (!song.restoreUndoSnapshot(snapshot, previousFile, skippedSections))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the original session could not be restored"));
	}
	// The restore keeps the format of the session it replaces; the original session's own format goes back.
	song.setContainerV2Project(previousContainerV2);
	// The restore marks the session modified; put the flag back the way it was (the same two-step
	// Song::noteProjectSaved performs for a save, here for a rollback).
	song.noteProjectSaved(previousFile);
	if (previousModified) { song.setModified(); }
	return ControlResult::success();
}

//! @a failed as it stands once the original session is back; otherwise @a failed with the restore failure added.
ControlResult restoredOr(const ControlResult& failed, const ControlResult& restored)
{
	if (restored.ok) { return failed; }
	return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("%1; the original session could not be restored: %2")
		.arg(failed.errorMessage, restored.errorMessage));
}

//! A new document from @a recipe: built on a new project, written as a template, opened as one. The session it
//! replaces is captured first as an undo snapshot and put back if any step fails, so a failed creation leaves
//! that session - its content, its undo/redo history and its provenance - the way it found it.
ControlResult newFromRecipe(const Recipe& recipe, QStringList* missing)
{
	const QString path = recipeTemplatePath(recipe);
	const ControlResult writeFailed = ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("the %1 template could not be written to %2").arg(QLatin1String(recipe.type), path));
	if (!recipeTemplateWritable(path)) { return writeFailed; }

	Song* const song = Engine::getSong();
	const QString previousFile = song->projectFileName();
	const bool previousModified = song->isModified();
	const QString previousType = ProjectInfo::instance().documentType();
	QStringList skippedSections;
	const std::shared_ptr<const QByteArray> snapshot = song->captureUndoSnapshot(&skippedSections);
	const bool previousContainerV2 = song->isContainerV2Project();
	if (!snapshot)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the current session could not be preserved before creating a %1 document")
				.arg(QLatin1String(recipe.type)));
	}
	// The undo/redo stacks a project load clears (Song::clearProject -> clearJournal). Held here and put
	// back with the restore, so a rollback gives the history back rather than an empty one.
	ProjectJournal::DetachedHistory history = Engine::projectJournal()->detachHistory();
	// Provenance is append-only and document-scoped: project.new replaces the session and records its own
	// change, so restoreUndoSnapshot (which keeps the log current at restore time) would carry the new
	// document's log back instead of this session's. Capture this session's log now and put it back on
	// rollback.
	DataFile originalLogCarrier(DataFile::Type::SongProject);
	provenance::writeTo(originalLogCarrier);
	QDomDocument originalLogDocument;
	QDomElement originalLog = originalLogCarrier.content().firstChildElement(provenance::nodeName());
	if (!originalLog.isNull())
	{
		originalLog = originalLogDocument.importNode(originalLog, true).toElement();
		originalLogDocument.appendChild(originalLog);
	}
	const auto restorePrevious = [&]()
	{
		const ControlResult restored = restoreSession(*song, *snapshot, previousFile, skippedSections, previousModified, previousContainerV2);
		if (restored.ok)
		{
			ProjectInfo::instance().setDocumentType(previousType);
			Engine::projectJournal()->reattachHistory(std::move(history));
			// restoreUndoSnapshot kept the log current at restore time; replace it with the original one.
			provenance::Section::instance().clear();
			if (!originalLog.isNull()) { provenance::Section::instance().load(originalLog); }
		}
		return restored;
	};

	ControlResult made = run(QStringLiteral("project.new"));
	if (!made.ok) { return restoredOr(made, restorePrevious()); }
	*missing = buildRecipe(recipe);
	if (!song->saveProjectFile(path)) { return restoredOr(writeFailed, restorePrevious()); }
	const ControlResult opened = run(QStringLiteral("project.new"), {{QStringLiteral("template"), path}});
	return opened.ok ? opened : restoredOr(opened, restorePrevious());
}

ControlResult newDocument(const QJsonObject& args)
{
	const QString type = args.value(QStringLiteral("type")).toString();
	const QString own = QDir(ConfigManager::inst()->userTemplateDir()).filePath(type + QStringLiteral(".mpt"));
	const Recipe* recipe = recipeFor(type);
	QStringList missing;
	ControlResult made = QFileInfo::exists(own) ? run(QStringLiteral("project.new"), {{QStringLiteral("template"), own}})
		: recipe != nullptr ? newFromRecipe(*recipe, &missing) : run(QStringLiteral("project.new"));
	if (!made.ok) { return made; }
	ProjectInfo::instance().setDocumentType(type);
	ProjectInfo::instance().announceCreated();
	made.result.insert(QStringLiteral("document_type"), type);
	made.result.insert(QStringLiteral("template"), QFileInfo::exists(own) ? own : recipe != nullptr ? QStringLiteral("built-in") : QStringLiteral("default"));
	if (!missing.isEmpty()) { made.result.insert(QStringLiteral("missing_devices"), QJsonArray::fromStringList(missing)); }
	return made;
}

bool sameFile(const QString& a, const QString& b)
{
	return !a.isEmpty() && QFileInfo(a).absoluteFilePath() == QFileInfo(b).absoluteFilePath();
}

//! The recent projects other than the session's own file, most recent first. Templates (.mpt) are listed
//! there too once opened; they are not projects to switch to.
QStringList recentOthers()
{
	QStringList out;
	for (const QString& path : ConfigManager::inst()->recentlyOpenedProjects())
	{
		const bool project = !path.endsWith(QStringLiteral(".mpt"), Qt::CaseInsensitive);
		if (project && !sameFile(path, Engine::getSong()->projectFileName())) { out << path; }
	}
	return out;
}

ControlResult recent(const QJsonObject&)
{
	QJsonArray out;
	const QStringList others = recentOthers();
	for (int i = 0; i < others.size(); ++i)
	{
		out.append(QJsonObject{{QStringLiteral("index"), i}, {QStringLiteral("path"), others[i]},
			{QStringLiteral("name"), QFileInfo(others[i]).completeBaseName()}, {QStringLiteral("exists"), QFileInfo::exists(others[i])}});
	}
	return ControlResult::success({{QStringLiteral("projects"), out}, {QStringLiteral("current"), Engine::getSong()->projectFileName()}});
}

QString switchTarget(const QJsonObject& args, ControlResult* error)
{
	const QStringList others = recentOthers();
	const int index = args.value(QStringLiteral("index")).toInt(0);
	const QString path = args.contains(QStringLiteral("path")) ? args.value(QStringLiteral("path")).toString() : others.value(index);
	if (QFileInfo(path).isFile()) { return path; }
	*error = ControlResult::failure(ControlErrorKind::NotFound, path.isEmpty()
		? QStringLiteral("no recent project %1 to switch to (%2 others)").arg(index).arg(others.size())
		: QStringLiteral("no project file '%1'").arg(path));
	return QString();
}

//! The session saved before it is left (when it changed and save_current is not false); the refusal otherwise.
bool leaveSession(const QJsonObject& args, ControlResult* error)
{
	Song* song = Engine::getSong();
	if (!song->isModified() || !args.value(QStringLiteral("save_current")).toBool(true)) { return true; }
	if (!song->projectFileName().isEmpty() && song->guiSaveProject()) { return true; }
	*error = ControlResult::failure(ControlErrorKind::Refused, song->projectFileName().isEmpty()
		? QStringLiteral("the session has unsaved changes and no file; save it, or pass save_current false to leave it")
		: QStringLiteral("the session could not be saved first; pass save_current false to leave it"));
	return false;
}

ControlResult switchProject(const QJsonObject& args)
{
	ControlResult error;
	const QString target = switchTarget(args, &error);
	if (target.isEmpty() || !leaveSession(args, &error)) { return error; }
	const QString left = Engine::getSong()->projectFileName();
	ControlResult opened = run(QStringLiteral("project.open"), {{QStringLiteral("path"), target}});
	if (!opened.ok) { return opened; }
	QString why;
	const bool requestedReference = args.value(QStringLiteral("keep_as_reference")).toBool();
	const bool kept = requestedReference && !left.isEmpty() && ReferenceProject::instance().open(left, &why);
	opened.result.insert(QStringLiteral("left"), left);
	opened.result.insert(QStringLiteral("reference"), kept ? left : QString());
	if (requestedReference && !kept)
	{
		opened.result.insert(QStringLiteral("reference_error"), left.isEmpty()
			? QStringLiteral("the previous session has no project file to keep as a reference") : why);
	}
	return opened;
}

} // namespace

void registerDocumentCommands(ControlRegistry& registry)
{
	defineCommand(registry, QStringLiteral("project.new_document"), QStringLiteral("Replace the session with a new document of "
		"'type' (production, mastering or live), from that type's template: the user's own <template dir>/<type>.mpt when "
		"there is one; otherwise production is the default template (as project.new), mastering a 'Mix' and a muted "
		"'Reference' sample track with EQ, multiband, compressor and analyser on the master, and live two instruments and three "
		"sample tracks. The interface opens the type's view and shows the type in the title. Not undoable, as project.new; "
		"unsaved edits are gone."), {{QStringLiteral("type"), enumProperty(ProjectInfo::documentTypes())}}, {QStringLiteral("type")},
		true, newDocument);
	defineCommand(registry, QStringLiteral("project.recent"), QStringLiteral("The recently opened projects other than the session's "
		"own file, most recent first (index, path, name, whether it still exists) - what project.switch picks from."), {}, {}, false, recent);
	defineCommand(registry, QStringLiteral("project.switch"), QStringLiteral("Switch to another project: 'path', or 'index' into "
		"project.recent (0, the default, is the most recent other one - so repeating it toggles between two projects). The "
		"session is saved first when it changed (unless 'save_current' is false; an untitled session with changes is refused "
		"rather than lost). With 'keep_as_reference' the project left behind stays open as the reference to copy from "
		"(reference.*); if it cannot be opened, the switch completes and returns 'reference_error'. Opens as project.open "
		"does, so it is not undoable; switch back instead."),
		{{QStringLiteral("path"), stringProperty()}, {QStringLiteral("index"), integerProperty(0, 100)},
			{QStringLiteral("save_current"), booleanProperty()}, {QStringLiteral("keep_as_reference"), booleanProperty()}},
		{}, true, switchProject);
}

} // namespace lmms
