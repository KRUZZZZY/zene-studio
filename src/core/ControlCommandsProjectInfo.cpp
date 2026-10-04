/*
 * ControlCommandsProjectInfo.cpp - project settings, metadata, document type and edit locks
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
/*! 0.5.0 L12, what belongs to the project as a whole:
 *  - project.settings (WF-043, WF-048): tempo, time signature, master volume and pitch, pan law and the
 *    document type in one place - what the Project Settings dialog reads and writes;
 *  - project.metadata (WF-047): title, artist, album, genre, year, composer, copyright, comment;
 *  - track.set_locked / clip.set_locked / project.locks (WF-044): locks against editing (EditLocks.h). */

#include <utility>

#include <QJsonArray>

#include "Clip.h"
#include "ControlArrangingCommands.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "EditLocks.h"
#include "Engine.h"
#include "MixerOptions.h"
#include "ProjectInfo.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;

namespace
{

//! The project-wide settings the dialog shows, as plain values (what an undo step carries).
struct ProjectSettings
{
	int tempo = 0;
	int numerator = 0;
	int denominator = 0;
	int volume = 0;
	int pitch = 0;
	int panLaw = 0;
	QString type;
	bool operator==(const ProjectSettings&) const = default;
};

ProjectSettings currentSettings()
{
	Song* song = Engine::getSong();
	return ProjectSettings{song->tempoModel().value(), song->getTimeSigModel().numeratorModel().value(),
		song->getTimeSigModel().denominatorModel().value(), song->masterVolumeModel().value(), song->masterPitchModel().value(),
		static_cast<int>(MixerOptions::instance().panLaw()), ProjectInfo::instance().documentType()};
}

void applySettings(const ProjectSettings& settings)
{
	Song* song = Engine::getSong();
	song->tempoModel().setValue(settings.tempo);
	song->getTimeSigModel().numeratorModel().setValue(settings.numerator);
	song->getTimeSigModel().denominatorModel().setValue(settings.denominator);
	song->masterVolumeModel().setValue(settings.volume);
	song->masterPitchModel().setValue(settings.pitch);
	MixerOptions::instance().setPanLaw(static_cast<PanLaw>(settings.panLaw));
	ProjectInfo::instance().setDocumentType(settings.type);
}

QJsonObject settingsJson(const ProjectSettings& settings)
{
	return {{QStringLiteral("tempo"), settings.tempo}, {QStringLiteral("numerator"), settings.numerator},
		{QStringLiteral("denominator"), settings.denominator}, {QStringLiteral("master_volume"), settings.volume},
		{QStringLiteral("master_pitch"), settings.pitch}, {QStringLiteral("pan_law"), MixerOptions::panLawNames().value(settings.panLaw)},
		{QStringLiteral("document_type"), settings.type}, {QStringLiteral("file"), Engine::getSong()->projectFileName()}};
}

ControlResult settings(const QJsonObject& args)
{
	const ProjectSettings before = currentSettings();
	ProjectSettings after = before;
	const std::pair<const char*, int ProjectSettings::*> numbers[] = {{"tempo", &ProjectSettings::tempo},
		{"numerator", &ProjectSettings::numerator}, {"denominator", &ProjectSettings::denominator},
		{"master_volume", &ProjectSettings::volume}, {"master_pitch", &ProjectSettings::pitch}};
	for (const auto& [key, member] : numbers)
	{
		if (args.contains(QLatin1String(key))) { after.*member = args.value(QLatin1String(key)).toInt(); }
	}
	if (args.contains(QStringLiteral("pan_law"))) { after.panLaw = static_cast<int>(MixerOptions::panLawNames().indexOf(args.value(QStringLiteral("pan_law")).toString())); }
	after.type = args.value(QStringLiteral("document_type")).toString(after.type);
	if (!(after == before))
	{
		applySettings(after);
		addUndoStep([before]() { applySettings(before); }, [after]() { applySettings(after); });
		Engine::getSong()->setModified();
	}
	return ControlResult::success(settingsJson(currentSettings()));
}

QJsonObject metadataJson()
{
	QJsonObject out;
	for (const QString& name : ProjectInfo::fieldNames()) { out.insert(name, ProjectInfo::instance().field(name)); }
	return out;
}

ControlResult metadata(const QJsonObject& args)
{
	ProjectInfo& info = ProjectInfo::instance();
	const QHash<QString, QString> before = info.fields();
	for (const QString& name : ProjectInfo::fieldNames())
	{
		if (args.contains(name)) { info.setField(name, args.value(name).toString().trimmed()); }
	}
	const QHash<QString, QString> after = info.fields();
	if (after != before)
	{
		addUndoStep([before]() { ProjectInfo::instance().setFields(before); }, [after]() { ProjectInfo::instance().setFields(after); });
		Engine::getSong()->setModified();
	}
	return ControlResult::success({{QStringLiteral("metadata"), metadataJson()}, {QStringLiteral("document_type"), info.documentType()}});
}

void lockTrack(int id, bool locked) { EditLocks::instance().setTrackLocked(id, locked); }
void lockClip(int id, bool locked) { EditLocks::instance().setClipLocked(id, locked); }

//! Lock or unlock one object through @a set, as one undo step when it changes.
void recordLock(void (*set)(int, bool), int id, bool before, bool locked)
{
	if (before == locked) { return; }
	set(id, locked);
	addUndoStep([set, id, before]() { set(id, before); }, [set, id, locked]() { set(id, locked); });
	Engine::getSong()->setModified();
}

ControlResult setTrackLocked(const QJsonObject& args)
{
	ControlResult error;
	Track* track = resolveTrack(args.value(QStringLiteral("track")).toString(), &error);
	if (track == nullptr) { return error; }
	const bool locked = args.value(QStringLiteral("locked")).toBool(true);
	recordLock(lockTrack, track->id(), EditLocks::instance().trackLocked(track->id()), locked);
	return ControlResult::success({{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("locked"), locked}});
}

ControlResult setClipLocked(const QJsonObject& args)
{
	ControlResult error;
	ClipRef ref;
	if (!resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &error)) { return error; }
	const bool locked = args.value(QStringLiteral("locked")).toBool(true);
	recordLock(lockClip, ref.clip->id(), EditLocks::instance().clipLocked(ref.clip->id()), locked);
	return ControlResult::success({{QStringLiteral("clip"), clipIdOf(ref.clip)}, {QStringLiteral("locked"), locked},
		{QStringLiteral("track_locked"), EditLocks::instance().trackLocked(ref.track->id())}});
}

ControlResult locks(const QJsonObject&)
{
	QJsonArray tracks;
	for (Track* track : Engine::getSong()->tracks())
	{
		if (EditLocks::instance().trackLocked(track->id())) { tracks.append(QJsonObject{{QStringLiteral("track"), trackIdOf(track)}, {QStringLiteral("name"), track->name()}}); }
	}
	QJsonArray clips;
	for (const ClipRef& ref : enumerateClips())
	{
		if (EditLocks::instance().clipLocked(ref.clip->id())) { clips.append(QJsonObject{{QStringLiteral("clip"), clipIdOf(ref.clip)}, {QStringLiteral("track"), trackIdOf(ref.track)}}); }
	}
	return ControlResult::success({{QStringLiteral("tracks"), tracks}, {QStringLiteral("clips"), clips}});
}

void add(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args, const QStringList& required,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	control::defineCommandWithSchema(registry, QLatin1String(id), description, objectSchema(args, QJsonArray::fromStringList(required)), mutating, handler);
}

} // namespace

void registerProjectInfoCommands(ControlRegistry& registry)
{
	ProjectInfo::instance();  // registers their project sections before a project loads
	EditLocks::instance();
	add(registry, "project.settings", QStringLiteral("The project's settings in one place - what the Project Settings dialog "
		"shows: 'tempo' (BPM), 'numerator' / 'denominator' (the time signature), 'master_volume' (0..200 %), 'master_pitch' "
		"(-12..12 semitones), 'pan_law' (as mixer.options takes it) and 'document_type' (production, mastering or live - the "
		"view the interface opens the project in). A field left out keeps its value; with no arguments it reports them. "
		"Saved with the project; one undo step."),
		{{QStringLiteral("tempo"), integerProperty(MinTempo, MaxTempo)}, {QStringLiteral("numerator"), integerProperty(1, 32)},
			{QStringLiteral("denominator"), integerProperty(1, 32)}, {QStringLiteral("master_volume"), integerProperty(0, 200)},
			{QStringLiteral("master_pitch"), integerProperty(-12, 12)}, {QStringLiteral("pan_law"), enumProperty(MixerOptions::panLawNames())},
			{QStringLiteral("document_type"), enumProperty(ProjectInfo::documentTypes())}}, {}, true, settings);
	QJsonObject fields;
	for (const QString& name : ProjectInfo::fieldNames()) { fields.insert(name, stringProperty()); }
	add(registry, "project.metadata", QStringLiteral("The project's metadata: 'title', 'artist', 'album', 'genre', 'year', "
		"'composer', 'copyright' and 'comment' (text; an empty one clears the field). A field left out keeps its value; with "
		"no arguments it reports them. Saved with the project; one undo step."), fields, {}, true, metadata);
	add(registry, "track.set_locked", QStringLiteral("Lock 'track' against editing ('locked' false unlocks it): its clips cannot "
		"be moved, resized, deleted or edited - by the mouse or by a command - until it is unlocked; mute, solo, arm, the "
		"mixer and automation stay open. Saved with the project; one undo step."),
		{{QStringLiteral("track"), stringProperty()}, {QStringLiteral("locked"), booleanProperty()}}, {QStringLiteral("track")}, true, setTrackLocked);
	add(registry, "clip.set_locked", QStringLiteral("Lock one 'clip' against editing ('locked' false unlocks it), as "
		"track.set_locked does for a whole track. Saved with the project; one undo step."),
		{{QStringLiteral("clip"), stringProperty()}, {QStringLiteral("locked"), booleanProperty()}}, {QStringLiteral("clip")}, true, setClipLocked);
	add(registry, "project.locks", QStringLiteral("The tracks and clips locked against editing."), {}, {}, false, locks);
	registerArrangingCommands(registry);  // 0.5.0 F3: spacers, the reference project, switching, typed new documents
}

} // namespace lmms
