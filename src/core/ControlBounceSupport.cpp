/*
 * ControlBounceSupport.cpp - what the bounce and freeze commands share
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

// Moved out of ControlCommandsFreeze.cpp (0.5.0 L01) when bounce.land needed the same range and
// output rules: one definition, used by bounce.in_place, freeze.* and bounce.land.

#include "ControlBounceSupport.h"

#include <QDir>
#include <QFileInfo>

#include "ControlVocabulary.h"
#include "Engine.h"
#include "Song.h"
#include "Track.h"

namespace lmms::control
{

static QString checkRangeArgs(const QJsonObject& args, bool required)
{
	const bool hasStart = args.contains(QStringLiteral("start"));
	const bool hasEnd = args.contains(QStringLiteral("end"));
	if (hasStart != hasEnd)
	{
		return QStringLiteral("'start' and 'end' go together: give both, or neither for the "
			"whole track");
	}
	if (required && !hasStart)
	{
		return QStringLiteral("a region needs both 'start' and 'end' (ticks)");
	}
	return QString();
}

QString parseRange(const QJsonObject& args, bool required, BounceInPlace::Range* range)
{
	const QString problem = checkRangeArgs(args, required);
	if (!problem.isEmpty()) { return problem; }
	if (!args.contains(QStringLiteral("start"))) { return QString(); }

	if (!args.value(QStringLiteral("start")).isDouble()
		|| !args.value(QStringLiteral("end")).isDouble())
	{
		return QStringLiteral("'start' and 'end' are tick positions, in integers");
	}
	range->start = static_cast<tick_t>(args.value(QStringLiteral("start")).toDouble());
	range->end = static_cast<tick_t>(args.value(QStringLiteral("end")).toDouble());
	if (range->start < 0) { return QStringLiteral("'start' must not be negative"); }
	if (range->end <= range->start) { return QStringLiteral("'end' must be past 'start'"); }
	if (range->end > MaxSongLength)
	{
		return QStringLiteral("'end' is past the end of the timeline");
	}
	return QString();
}

//! Where a bounce lands when the caller does not say: beside the project, in a
//! "<project>-bounces" directory, named after the track's stable id and the verb
//! (and the tick range, for a region), so two bounces never overwrite each
//! other and a reader can tell what produced the file.
static QString defaultBouncePath(Track* track, const QString& verb, const BounceInPlace::Range& range)
{
	const QString project = Engine::getSong()->projectFileName();
	const QDir base = project.isEmpty() ? QDir(QDir::tempPath()) : QFileInfo(project).dir();
	const QString name = project.isEmpty()
		? QStringLiteral("untitled")
		: QFileInfo(project).completeBaseName();
	const QDir directory(base.filePath(name + QStringLiteral("-bounces")));

	QString file = QStringLiteral("%1-%2").arg(control::trackIdOf(track), verb);
	if (!range.coversWholeTrack())
	{
		file += QStringLiteral("-%1-%2").arg(range.start).arg(range.end);
	}
	return directory.filePath(file + QStringLiteral(".wav"));
}

QString resolveOutPath(const QJsonObject& args, Track* track, const QString& verb,
		const BounceInPlace::Range& range)
{
	const QString requested = args.value(QStringLiteral("out")).toString();
	return requested.isEmpty() ? defaultBouncePath(track, verb, range) : requested;
}

//! The render facts every verb of this file reports.
QJsonObject renderJson(const BounceInPlace::Result& rendered)
{
	QJsonObject result;
	result.insert(QStringLiteral("path"), rendered.path);
	result.insert(QStringLiteral("sample_rate"), rendered.sampleRate);
	result.insert(QStringLiteral("frames"), rendered.frames);
	result.insert(QStringLiteral("bytes"), rendered.bytes);
	result.insert(QStringLiteral("sha256"), rendered.sha256);
	result.insert(QStringLiteral("start_ticks"), static_cast<qint64>(rendered.range.start));
	result.insert(QStringLiteral("end_ticks"), static_cast<qint64>(rendered.range.end));
	return result;
}

} // namespace lmms::control
