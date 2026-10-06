/*
 * ControlCommandsHandoff.cpp - project.handoff: a folder another person can open
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
/*! 0.5.0 L12 (WF-049): share or hand off a project. project.handoff writes a folder someone else can
 *  open as it is: a copy of the session (.mmpz), every external file it uses copied into media/ beside
 *  it with the copy rewritten to name them (project.collect_media's work, on the copy), and
 *  HANDOFF.json - what the project needs and what it is: the application version, tempo, time
 *  signature, length, the devices it uses (to install before opening) and the media, with what could
 *  not be found. The session and its own file are not touched. */

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

#include "AudioBusHandle.h"
#include "ControlProjectAssets.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "SampleTrack.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

void addChain(const EffectChain* chain, QSet<QString>* out)
{
	if (chain == nullptr) { return; }
	for (const Effect* effect : chain->effects()) { out->insert(effect->displayName()); }
}

//! A mixer channel's devices: its chain, its rack's parallel chains and its post-fader inserts.
void addChannel(const MixerChannel* channel, QSet<QString>* out)
{
	addChain(&channel->m_fxChain, out);
	for (int c = 1; c < channel->m_rack.chainCount(); ++c) { addChain(channel->m_rack.chain(c), out); }
	addChain(&channel->m_postInserts.chain(), out);
}

//! The devices the session uses: instruments, track effects, mixer effects.
QJsonArray devicesUsed()
{
	QSet<QString> names;
	for (Track* track : Engine::getSong()->tracks())
	{
		if (auto* instrument = dynamic_cast<InstrumentTrack*>(track))
		{
			if (instrument->instrument() != nullptr) { names.insert(instrument->instrument()->displayName()); }
			addChain(instrument->audioBusHandle()->effects(), &names);
		}
		if (auto* sample = dynamic_cast<SampleTrack*>(track)) { addChain(sample->audioBusHandle()->effects(), &names); }
	}
	for (int i = 0; i < Engine::mixer()->numChannels(); ++i) { addChannel(Engine::mixer()->mixerChannel(i), &names); }
	QStringList sorted(names.begin(), names.end());
	sorted.sort(Qt::CaseInsensitive);
	return QJsonArray::fromStringList(sorted);
}

QJsonObject manifest(const QString& file, const QJsonArray& copied, const QJsonArray& failed, int missing)
{
	Song* song = Engine::getSong();
	return {{QStringLiteral("project"), QFileInfo(file).fileName()}, {QStringLiteral("created"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
		{QStringLiteral("application"), QCoreApplication::applicationName() + QLatin1Char(' ') + QCoreApplication::applicationVersion()},
		{QStringLiteral("tempo"), song->getTempo()},
		{QStringLiteral("time_signature"), QStringLiteral("%1/%2").arg(song->getTimeSigModel().getNumerator()).arg(song->getTimeSigModel().getDenominator())},
		{QStringLiteral("length_bars"), static_cast<int>(song->length())}, {QStringLiteral("devices"), devicesUsed()},
		{QStringLiteral("media"), copied}, {QStringLiteral("media_failed"), failed}, {QStringLiteral("media_missing"), missing}};
}

//! Where the copy of the session goes in @a dir: the session's own base name, as a .mmpz.
QString copyPath(const QString& dir)
{
	const QString base = QFileInfo(Engine::getSong()->projectFileName()).completeBaseName();
	return QDir(dir).filePath((base.isEmpty() ? QStringLiteral("project") : base) + QStringLiteral(".mmpz"));
}

//! Whether @a file is the session's own file (a handoff into the project's folder would overwrite it).
bool isSessionFile(const QString& file)
{
	const QString own = QFileInfo(Engine::getSong()->projectFileName()).canonicalFilePath();
	return !own.isEmpty() && QFileInfo(file).canonicalFilePath() == own;
}

//! Whether anything already occupies the hand-off folder's slot for @a file: a hand off never
//! replaces a file it did not write itself.
bool isTaken(const QString& file)
{
	return QFileInfo::exists(file);
}

//! The first of the hand-off folder's own files that already exists, or an empty string when the
//! folder is free for this hand off.
QString takenFile(const QString& file, const QString& manifestFile)
{
	for (const QString& candidate : {file, manifestFile})
	{
		if (isTaken(candidate)) { return candidate; }
	}
	return {};
}

//! Whether the hand-off manifest lands at @a file.
bool writeManifest(const QString& file, const QJsonObject& info)
{
	QSaveFile out(file);
	return out.open(QIODevice::WriteOnly) && out.write(QJsonDocument(info).toJson()) >= 0 && out.commit();
}

ControlResult handoff(const QJsonObject& args)
{
	const QString dir = args.value(QStringLiteral("dir")).toString();
	if (!QDir::isAbsolutePath(dir)) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'dir' is an absolute folder")); }
	if (!QDir().mkpath(dir)) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("cannot create '%1'").arg(dir)); }
	Song* song = Engine::getSong();
	const QString file = copyPath(dir);
	if (isSessionFile(file))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("'%1' is the session's own file; hand off into another folder").arg(file));
	}
	const QString manifestFile = QDir(dir).filePath(QStringLiteral("HANDOFF.json"));
	const QString taken = takenFile(file, manifestFile);
	if (!taken.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("'%1' already exists; hand off into an empty folder").arg(taken));
	}
	if (!song->saveProjectFile(file))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the project could not be written to '%1': %2").arg(file, song->saveRefusal()));
	}
	QJsonArray copied;
	QJsonArray failed;
	int missing = 0;
	ControlResult error;
	if (!collectProjectMediaInto(file, QStringLiteral("media"), &copied, &failed, &missing, &error)) { return error; }
	const QJsonObject info = manifest(file, copied, failed, missing);
	if (!writeManifest(manifestFile, info))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("HANDOFF.json could not be written"));
	}
	QJsonObject result = info;
	result.insert(QStringLiteral("dir"), dir);
	result.insert(QStringLiteral("file"), file);
	return ControlResult::success(result);
}

} // namespace

void registerHandoffCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("project.handoff");
	cmd.group = QStringLiteral("project");
	cmd.verb = QStringLiteral("handoff");
	cmd.description = QStringLiteral("Write a folder ('dir', absolute) someone else can open: a copy of the session, every file it "
		"uses copied into media/ beside it (the copy names them), and HANDOFF.json - application version, tempo, time "
		"signature, length, the devices to install first, the media and any that could not be found. The session and its "
		"own file are untouched (nothing to undo; delete the folder).");
	cmd.argsSchema = objectSchema({{QStringLiteral("dir"), stringProperty()}}, {QStringLiteral("dir")});
	cmd.resultSchema = objectProperty();
	cmd.mutating = false;
	cmd.handler = handoff;
	registry.registerCommand(cmd);
}

} // namespace lmms
