/*
 * ReversibilityTestSupport.h - the SHARED helpers of the SPEC A16 acceptance
 *                              tests: one definition for the two test files
 *                              that use them (the contract half and the
 *                              apply -> undo -> read back half).
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

#ifndef LMMS_REVERSIBILITY_TEST_SUPPORT_H
#define LMMS_REVERSIBILITY_TEST_SUPPORT_H

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ControlRegistry.h"

//! Shared helpers of the SPEC A16 acceptance tests, ONE definition for the two
//! test files that use them. Free inline functions in a namespace, not statics
//! in each class: a second copy of a helper is exactly the drift this split
//! exists to prevent.
//!
//! REV_UNDO_OR_FAIL is a macro rather than a function because QVERIFY2 needs a
//! void return; it is the one place that decides what "the undo worked" means.
#define REV_UNDO_OR_FAIL() \
	do { \
		const ControlResult _rev_undo = revtest::run(QStringLiteral("control.undo")); \
		QVERIFY2(_rev_undo.ok, qPrintable(_rev_undo.errorMessage)); \
		QVERIFY2(_rev_undo.result.value(QStringLiteral("undone")).toBool(), \
			qPrintable(QStringLiteral("nothing was undone: %1").arg( \
				QString::fromUtf8(QJsonDocument(_rev_undo.result).toJson(QJsonDocument::Compact))))); \
	} while (0)

namespace revtest
{
using namespace lmms;

//! Forward declaration: addInstrumentTrack() uses the catalogue helpers that are
//! defined below it (one pass, no reordering of the file's narrative).
inline QString firstLoadable(const QString& kind);

inline ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The last recorded transaction for \a command. An empty object when the
//! engine recorded none; the callers' QCOMPAREs turn that into a clear failure
//! rather than a silent pass (QVERIFY needs a void function).
inline QJsonObject stateOf(const QString& command)
{
	const QJsonArray all = run(QStringLiteral("control.transactions"))
		.result.value(QStringLiteral("transactions")).toArray();
	QJsonObject found;
	for (const QJsonValue& value : all)
	{
		if (value.toObject().value(QStringLiteral("command")).toString() == command)
		{
			found = value.toObject();
		}
	}
	return found;
}

inline QString addTrack(const QString& type = QStringLiteral("instrument"))
{
	const ControlResult added = run(QStringLiteral("track.add"),
		QJsonObject{{QStringLiteral("type"), type}});
	return added.ok ? added.result.value(QStringLiteral("track")).toString() : QString();
}

//! An instrument track with a real instrument on it (plugin.load), or empty
//! when this build exposes none.
inline QString addInstrumentTrack()
{
	const QString track = addTrack();
	if (track.isEmpty()) { return QString(); }
	const QString device = firstLoadable(QStringLiteral("instrument"));
	if (device.isEmpty()) { return QString(); }
	const ControlResult loaded = run(QStringLiteral("plugin.load"),
		{{QStringLiteral("target"), track}, {QStringLiteral("device"), device}});
	return loaded.ok ? track : QString();
}

//! Every loadable device of \a kind, whatever host it came from (built-in,
//! LADSPA, LV2): the point of these tests is a REAL device to drive.
inline QStringList loadableDevices(const QString& kind)
{
	const ControlResult listed = run(QStringLiteral("plugin.list"),
		{{QStringLiteral("kind"), kind}, {QStringLiteral("loadable_only"), true}});
	QStringList ids;
	for (const QJsonValue& value : listed.result.value(QStringLiteral("devices")).toArray())
	{
		ids.append(value.toObject().value(QStringLiteral("id")).toString());
	}
	return ids;
}

inline QString firstLoadable(const QString& kind)
{
	const QStringList ids = loadableDevices(kind);
	return ids.isEmpty() ? QString() : ids.first();
}

inline QString firstLoadableEffect() { return firstLoadable(QStringLiteral("effect")); }

inline int trackCount()
{
	return run(QStringLiteral("track.list")).result.value(QStringLiteral("count")).toInt();
}

inline bool hasTrack(const QString& id)
{
	return run(QStringLiteral("track.get_state"),
		QJsonObject{{QStringLiteral("track"), id}}).ok;
}

inline QString trackName(const QString& id)
{
	return run(QStringLiteral("track.get_state"),
		QJsonObject{{QStringLiteral("track"), id}})
		.result.value(QStringLiteral("name")).toString();
}

inline bool trackMuted(const QString& id)
{
	return run(QStringLiteral("track.get_state"),
		QJsonObject{{QStringLiteral("track"), id}})
		.result.value(QStringLiteral("muted")).toBool();
}

//! Every track's {id, muted, soloed}: the state track.set_solo writes.
inline QJsonArray muteSoloState()
{
	QJsonArray out;
	for (const QJsonValue& value : run(QStringLiteral("arrangement.get_state"))
		.result.value(QStringLiteral("tracks")).toArray())
	{
		const QJsonObject track = value.toObject();
		out.append(QJsonObject{{QStringLiteral("id"), track.value(QStringLiteral("id"))},
			{QStringLiteral("muted"), track.value(QStringLiteral("muted"))},
			{QStringLiteral("soloed"), track.value(QStringLiteral("soloed"))}});
	}
	return out;
}

inline int mixerChannelCount()
{
	return run(QStringLiteral("mixer.get_state")).result.value(QStringLiteral("count")).toInt();
}

inline QString channelId(int index)
{
	return QStringLiteral("ch-") + QString::number(index);
}

inline double channelVolume(const QString& channel)
{
	for (const QJsonValue& value : run(QStringLiteral("mixer.get_state"))
		.result.value(QStringLiteral("channels")).toArray())
	{
		const QJsonObject entry = value.toObject();
		if (entry.value(QStringLiteral("id")).toString() == channel)
		{
			return entry.value(QStringLiteral("volume")).toDouble();
		}
	}
	return -1.0;
}

inline int rollNoteCount(const QString& clip)
{
	return run(QStringLiteral("roll.get_state"),
		QJsonObject{{QStringLiteral("clip"), clip}})
		.result.value(QStringLiteral("note_count")).toInt();
}

inline QJsonObject noteState(const QString& clip, int index)
{
	const QJsonArray notes = run(QStringLiteral("roll.get_state"),
		QJsonObject{{QStringLiteral("clip"), clip}})
		.result.value(QStringLiteral("notes")).toArray();
	return index < notes.size() ? notes.at(index).toObject() : QJsonObject();
}

inline int notePosition(const QString& clip, int index)
{
	return noteState(clip, index).value(QStringLiteral("position")).toInt();
}

inline int noteKey(const QString& clip, int index)
{
	return noteState(clip, index).value(QStringLiteral("key")).toInt();
}

inline double noteVelocity(const QString& clip, int index)
{
	return noteState(clip, index).value(QStringLiteral("velocity")).toDouble();
}

//! The clip ids of \a track, from arrangement.get_state (track.get_state
//! reports the track itself, not its clips).
inline QJsonArray clipsOf(const QString& track)
{
	for (const QJsonValue& value : run(QStringLiteral("arrangement.get_state"))
		.result.value(QStringLiteral("tracks")).toArray())
	{
		const QJsonObject entry = value.toObject();
		if (entry.value(QStringLiteral("id")).toString() == track)
		{
			return entry.value(QStringLiteral("clips")).toArray();
		}
	}
	return QJsonArray();
}

inline int clipIndex(const QString& clip)
{
	const QJsonArray clips = run(QStringLiteral("arrangement.get_state"))
		.result.value(QStringLiteral("clips")).toArray();
	for (int i = 0; i < clips.size(); ++i)
	{
		if (clips.at(i).toObject().value(QStringLiteral("id")).toString() == clip) { return i; }
	}
	return -1;
}

inline int clipPosition(const QString& clip)
{
	return run(QStringLiteral("arrangement.get_state")).result.value(QStringLiteral("clips"))
		.toArray().at(clipIndex(clip)).toObject().value(QStringLiteral("position")).toInt();
}

inline int clipLength(const QString& clip)
{
	return run(QStringLiteral("arrangement.get_state")).result.value(QStringLiteral("clips"))
		.toArray().at(clipIndex(clip)).toObject().value(QStringLiteral("length")).toInt();
}

inline QJsonArray deviceChain(const QString& target)
{
	const QJsonArray chains = run(QStringLiteral("dsp.get_state"),
		QJsonObject{{QStringLiteral("target"), target}})
		.result.value(QStringLiteral("chains")).toArray();
	return chains.isEmpty() ? QJsonArray()
		: chains.first().toObject().value(QStringLiteral("devices")).toArray();
}

inline int deviceCount(const QString& target) { return deviceChain(target).size(); }

inline QJsonObject deviceEntry(const QString& target, const QString& fx)
{
	for (const QJsonValue& value : deviceChain(target))
	{
		if (value.toObject().value(QStringLiteral("id")).toString() == fx)
		{
			return value.toObject();
		}
	}
	return QJsonObject();
}

inline bool deviceEnabled(const QString& target, const QString& fx)
{
	return deviceEntry(target, fx).value(QStringLiteral("enabled")).toBool();
}

inline QJsonArray deviceParameters(const QString& target, const QString& fx)
{
	return deviceEntry(target, fx).value(QStringLiteral("parameters")).toArray();
}

inline double deviceParameterValue(const QString& target, const QString& fx, int index)
{
	const ControlResult read = run(QStringLiteral("plugin.param_get"),
		{{QStringLiteral("target"), target}, {QStringLiteral("plugin"), fx},
			{QStringLiteral("index"), index}});
	return read.result.value(QStringLiteral("parameter")).toObject()
		.value(QStringLiteral("value")).toDouble();
}

inline QString firstAutomationParameter(const QString& track)
{
	const ControlResult state = run(QStringLiteral("automation.get_state"),
		QJsonObject{{QStringLiteral("track"), track}});
	const QJsonArray tracks = state.result.value(QStringLiteral("tracks")).toArray();
	if (tracks.isEmpty()) { return QString(); }
	const QJsonArray parameters = tracks.first().toObject()
		.value(QStringLiteral("parameters")).toArray();
	return parameters.isEmpty() ? QString()
		: parameters.first().toObject().value(QStringLiteral("id")).toString();
}

inline QByteArray readBytes(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QByteArray(); }
	return file.readAll();
}

} // namespace revtest

#endif // LMMS_REVERSIBILITY_TEST_SUPPORT_H
