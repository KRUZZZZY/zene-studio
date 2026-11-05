/*
 * ArrangerFileDrop.cpp - files dropped on the arranger become clips
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
#include "ArrangerFileDrop.h"

#include <algorithm>

#include <QAction>
#include <QDir>
#include <QDropEvent>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMimeData>
#include <QStatusBar>
#include <QUrl>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "GuiApplication.h"
#include "LoopImportWarp.h"
#include "MainWindow.h"
#include "SongEditor.h"
#include "TextFloat.h"
#include "Track.h"
#include "TrackContainerView.h"
#include "TrackView.h"
#include "embed.h"

namespace lmms::gui::arrangerdrop
{

namespace
{

QStringList localFiles(const QMimeData* mime)
{
	QStringList files;
	if (mime == nullptr) { return files; }
	for (const QUrl& url : mime->urls())
	{
		if (url.isLocalFile()) { files.append(url.toLocalFile()); }
	}
	return files;
}

int trackHeadWidth()
{
	return ConfigManager::inst()->value("ui", "compacttrackbuttons").toInt() == 1
		? trackSettingsWidth(true) + trackOperationsWidth(true)
		: trackSettingsWidth(false) + trackOperationsWidth(false);
}

//! Q050-093: the loop mode of this drop - the remembered one, or (Alt held) the other one, for this drop only.
QString loopModeArgument(Qt::KeyboardModifiers modifiers)
{
	if (!modifiers.testFlag(Qt::AltModifier)) { return QStringLiteral("preference"); }
	return loopwarp::modeName(loopwarp::preferredMode() == loopwarp::Mode::FollowProject
		? loopwarp::Mode::KeepUnstretched : loopwarp::Mode::FollowProject);
}

//! Q050-093: what the import detected and applied, in a sentence after the import's own one.
QString loopSummary(const QJsonObject& loop)
{
	if (loop.isEmpty()) { return {}; }
	const QJsonObject tempo = loop.value(QStringLiteral("source_tempo")).toObject();
	const QJsonObject key = loop.value(QStringLiteral("source_key")).toObject();
	QStringList detected;
	if (tempo.value(QStringLiteral("found")).toBool())
	{
		detected.append(QObject::tr("%1 BPM").arg(tempo.value(QStringLiteral("bpm")).toDouble(), 0, 'f', 1));
	}
	if (key.value(QStringLiteral("found")).toBool())
	{
		detected.append(QStringLiteral("%1 %2").arg(key.value(QStringLiteral("tonic")).toString(),
			key.value(QStringLiteral("scale")).toString()));
	}
	QString text = detected.isEmpty() ? QObject::tr(" No tempo or key detected.")
		: QObject::tr(" Detected %1.").arg(detected.join(QStringLiteral(", ")));
	if (loop.value(QStringLiteral("tempo_followed")).toBool()) { text += QObject::tr(" Following the project tempo."); }
	else { text += QObject::tr(" Kept unstretched."); }
	const int transpose = loop.value(QStringLiteral("transpose_applied")).toInt();
	if (transpose != 0) { text += QObject::tr(" Transposed %1 semitones to the project key.").arg(transpose > 0
		? QStringLiteral("+%1").arg(transpose) : QString::number(transpose)); }
	for (const QJsonValue& note : loop.value(QStringLiteral("notes")).toArray())
	{
		text += QStringLiteral(" ") + note.toString().left(1).toUpper() + note.toString().mid(1) + QStringLiteral(".");
	}
	return text;
}

bool importPath(const QString& path, Track* track, TimePos tick, Qt::KeyboardModifiers modifiers, QString* message)
{
	QJsonObject args{{QStringLiteral("path"), QFileInfo(path).absoluteFilePath()},
		{QStringLiteral("position"), static_cast<int>(tick.getTicks())},
		{QStringLiteral("loop_mode"), loopModeArgument(modifiers)}};
	if (track != nullptr) { args.insert(QStringLiteral("track"), control::trackIdOf(track)); }
	const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("clip.import_file"), args);
	if (message != nullptr)
	{
		*message = result.ok
			? QObject::tr("Imported %1 as a clip.").arg(QFileInfo(path).fileName())
				+ loopSummary(result.result.value(QStringLiteral("loop")).toObject())
			: result.errorMessage;
	}
	return result.ok;
}

void report(const QString& message, bool made)
{
	if (message.isEmpty() || getGUI() == nullptr || getGUI()->mainWindow() == nullptr) { return; }
	getGUI()->mainWindow()->statusBar()->showMessage(message, 8000);
	if (!made)
	{
		TextFloat::displayMessage(QObject::tr("Dropped file not imported"), message,
			embed::getIconPixmap("error", 24, 24), 6000);
	}
}

} // namespace

Qt::KeyboardModifiers dropModifiers(const QDropEvent* event)
{
	if (event == nullptr) { return Qt::NoModifier; }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	return event->modifiers();
#else
	return event->keyboardModifiers();
#endif
}

bool carriesFile(const QMimeData* mime) { return !localFiles(mime).isEmpty(); }

TimePos tickAtContainerX(const TrackContainerView* view, int x)
{
	if (view == nullptr || view->fixedClips()) { return TimePos(0); }
	const int head = trackHeadWidth();
	const int xPos = std::max(x, head);
	const float snapSize = getGUI()->songEditor()->m_editor->getSnapSize();
	return TimePos(((xPos - head) / view->pixelsPerBar() * TimePos::ticksPerBar()) + view->currentPosition())
		.quantize(snapSize, true);
}

bool importDroppedFile(const QMimeData* mime, Track* track, TimePos tick, QString* message, Qt::KeyboardModifiers modifiers)
{
	const QStringList files = localFiles(mime);
	if (files.isEmpty()) { return false; }
	QString text;
	const bool made = importPath(files.first(), track, tick, modifiers, &text);
	if (files.size() > 1) { text += QObject::tr(" Only the first of the %1 dropped files was used.").arg(files.size()); }
	if (message != nullptr) { *message = text; }
	return made;
}

bool dropFileAndReport(const QMimeData* mime, Track* track, TimePos tick, Qt::KeyboardModifiers modifiers)
{
	QString message;
	const bool made = importDroppedFile(mime, track, tick, &message, modifiers);
	report(message, made);
	return made;
}

bool importFileAndReport(const QString& path, Track* track, TimePos tick, Qt::KeyboardModifiers modifiers)
{
	QString message;
	const bool made = importPath(path, track, tick, modifiers, &message);
	report(message, made);
	return made;
}

QAction* addLoopWarpAction(QMenu* menu)
{
	QAction* action = menu->addAction(QObject::tr("Auto-Warp Dropped Loops"));
	action->setObjectName(QStringLiteral("loopWarpAction"));
	action->setCheckable(true);
	action->setToolTip(QObject::tr("Dropped audio loops follow the project tempo and key (their detected tempo and key "
		"are matched); off, they keep their own speed and pitch. Hold Alt while dropping for the other choice"));
	action->setData(QStringLiteral("settings.set"));
	action->setChecked(loopwarp::preferredMode() == loopwarp::Mode::FollowProject);
	QObject::connect(menu, &QMenu::aboutToShow, action, [action] {
		action->setChecked(loopwarp::preferredMode() == loopwarp::Mode::FollowProject);
	});
	QObject::connect(action, &QAction::triggered, action, [action](bool checked) {
		const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("settings.set"),
			{{QStringLiteral("key"), loopwarp::preferenceKey()},
				{QStringLiteral("value"), loopwarp::modeName(checked ? loopwarp::Mode::FollowProject : loopwarp::Mode::KeepUnstretched)}});
		if (!result.ok) { action->setChecked(!checked); }
	});
	return action;
}

} // namespace lmms::gui::arrangerdrop
