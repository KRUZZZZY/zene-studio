/*
 * ControlAutomationScriptTest.cpp - unit tests for the automation.* and
 *                                   script.* command groups
 *                                   (SPEC-zene-studio.md A11-A16).
 *
 * The end-to-end behaviour (rendered audio, control.undo, the wire) lives in
 * tests/control-socket-integration.py; this file pins the registry surface those
 * flows rely on: the ids, the schemas, the typed refusal each error path must
 * produce, and the fact that script.run drives the embedded ScriptEngine rather
 * than a child process.
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

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QTemporaryDir>
#include <QThread>
#include <QVector>
#include <QtGlobal>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QByteArrayView>
#endif

#include "AutomationClip.h"
#include "AutomationTrack.h"
#include "ControlAutomationSupport.h"
#include "ControlDeviceSupport.h"
#include "ControlAutomationSupport.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "ReversibilityTestSupport.h"
#include "ScriptEngine.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

//! The shipped Lua examples; tests/CMakeLists.txt injects the absolute path.
#ifndef LUA_SCRIPT_DIR
#define LUA_SCRIPT_DIR "data/scripts"
#endif

namespace
{
/*!
 * Windows: a test host cannot load a plugin MODULE library at runtime - the module's
 * import descriptor names zene.exe (ERROR_MOD_NOT_FOUND, 126) and the product loads them
 * inside zene.exe; AudioPluginTest.cpp carries the full mechanism and the CI evidence.
 */
constexpr auto testHostCanLoadPluginModules() -> bool
{
#ifdef Q_OS_WIN
	return false;
#else
	return true;
#endif
}

} // namespace

class ControlAutomationScriptTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase();

	void cleanupTestCase();

	//! Every command of the group is registered, under a group.verb id, with
	//! both schemas, and the mutating ones say so.
	void automationCommandsAreRegistered();

	void scriptCommandsAreRegistered();

	//! A fresh song has no user tracks, so the read-back must say that rather
	//! than inventing one.
	void automationGetStateOnAFreshSongIsHonest();

	//! Each refusing path returns the closed-set kind it promises.
	void automationTypedErrors();

	//! Refuse an inverted transform range before editableClip can create automation.
	void transformRefusesAnInvertedRangeWithoutCreatingAutomation();

	//! mode_set on a valid track+parameter but with no automation clip still
	//! succeeds: the mode is per-model, not per-clip, and it can be set before
	//! any automation exists.
	void automationModeSetSucceedsOnABareParameter();

	//! record_mode_set requires an existing automation clip, because the record
	//! flag lives on the clip, not on the parameter.
	void automationRecordModeSetRequiresAClip();

	//! A destination before a clip's origin reports a tick refusal for an in-range value.
	void movingBeforeTheClipOriginReportsATickError();

	//! The MODES' own proof (the no-destruction property, the mode round trip for all
	//! five modes and the write-mode sensitivity leg) lives in its own binary,
	//! tests/src/core/ControlAutomationModesTest.cpp: it drives plugin.param_set and
	//! Song::processNextBuffer() while the transport walks over a recorded curve,
	//! which is more machinery than this file's registry contract needs - and the
	//! file-length ratchet is not moved for a new feature.

	void scriptListShowsTheShippedScripts();

	//! A script whose bytes could NOT be read gets an EMPTY hash, never the
	//! well-known SHA-256 of zero bytes: 'bytes: 412, sha256: e3b0c442...' reads as
	//! "here are 412 bytes and this is what they hash to", which is a lie a client
	//! can act on. The listing says the file is there and says nothing about its
	//! content, and the empty hash is what says so.
	void anUnreadableScriptIsListedWithAnEmptyHash();

	void scriptListHashesScriptsLargerThanControlDataLimit();

	//! The hash reads in 64 KiB chunks, so the boundaries are exactly one chunk, one
	//! chunk plus a byte, and none at all. The expected digests are the SHA-256 of the
	//! bytes each file actually holds, computed with sha256sum rather than with the
	//! product's own hasher, so a fix that hashed the wrong bytes would fail here too.
	void scriptListHashesTheChunkBoundaries();

	//! script.run drives the embedded engine on its own worker thread; the log
	//! the script wrote comes back, and the run is not a second process.
	void scriptRunsInProcessAndReturnsItsLog();

	void scriptRefusesAMissingFileAndTheBudgetExhaustion();

	//! script.run is mutating, and records the only honest answer: a Lua script's
	//! edits have no registry inverse (SPEC A16).
	void scriptRunRecordsAnHonestTransaction();

private:
	static QString qstr(const char* text) { return QString::fromLatin1(text); }

	//! One parameter's "mode" as automation.get_state reports it, or an empty
	//! string when the track or the parameter is not in the read-back.
	static QString modeOf(ControlRegistry* registry, const QString& trackId, const QString& paramId)
	{
		const ControlResult state = registry->invoke(QStringLiteral("automation.get_state"),
			QJsonObject{{QStringLiteral("track"), trackId}});
		if (!state.ok) { return QString(); }
		for (const QJsonValue& t : state.result.value(QStringLiteral("tracks")).toArray())
		{
			const QJsonObject track = t.toObject();
			if (track.value(QStringLiteral("id")).toString() != trackId) { continue; }
			for (const QJsonValue& p : track.value(QStringLiteral("parameters")).toArray())
			{
				const QJsonObject parameter = p.toObject();
				if (parameter.value(QStringLiteral("id")).toString() == paramId)
				{
					return parameter.value(QStringLiteral("mode")).toString();
				}
			}
		}
		return QString();
	}

private:
	//! The track initTestCase put a real device on (empty when this build
	//! exposes no loadable instrument module - the slots' own QVERIFY2 says so
	//! with the reason, rather than skipping silently).
	QString m_devicedTrack;
};

#include "ControlAutomationScriptTestParts1.h"
#include "ControlAutomationScriptTestParts2.h"

QTEST_GUILESS_MAIN(ControlAutomationScriptTest)
#include "ControlAutomationScriptTest.moc"
