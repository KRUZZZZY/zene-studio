/*
 * SafeStartLoadPathTest.cpp - the safe-start predicate on the REAL load path.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of Zene Studio (an LMMS-derived product).
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

// The load-time half of safe-start mode (feature row 77 of
// docs/FEATURE-LIST-0.3.0.md, board task #666), with a real plugin module:
//
//   * with safe-start active, the REAL Plugin::instantiate() returns the
//     engine's DummyPlugin for a THIRD-PARTY module file - and that module is in
//     the factory's own catalogue, so the skip is the mode's doing and not a
//     plugin that failed to load;
//   * with the session switch off, the SAME call really loads the module. That
//     control is what makes the first half mean anything.
//
// It is its own binary because the file-length ratchet is not moved for a new
// feature (the ClipLinkTest / ClipLinkPersistenceTest arrangement): the marker
// and the signal half live in tests/src/core/SafeStartTest.cpp, and this file
// owns only the load path.
//
// WHY THE FIXTURE IS THIRD-PARTY: the module is copied into a directory this
// build does not ship from (a QTemporaryDir), and the plugin search path is
// pinned to that directory alone, so the name resolves to the fixture copy and
// the classification says "third-party". A module under the build's own plugin
// directory is NOT third-party and is never skipped.

#include "SafeStart.h"

#include "DummyPlugin.h"
#include "Plugin.h"
#include "PluginFactory.h"

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

//! Build-tree plugin directory, injected by tests/CMakeLists.txt.
#ifndef LMMS_TEST_SAFE_START_PLUGIN_DIR
#define LMMS_TEST_SAFE_START_PLUGIN_DIR "plugins"
#endif

namespace
{

using namespace lmms::safestart;

//! The module this test copies in. It is a CORE-ONLY exporter rather than an
//! instrument: the product itself instantiates it with a null parent
//! (Song::exportProjectMidi), so the control half cannot fail for a reason that
//! has nothing to do with safe-start mode.
constexpr auto moduleFileName() -> const char*
{
#ifdef LMMS_BUILD_WIN32
	return "midiexport.dll";
#else
	return "libmidiexport.so";
#endif
}

} // namespace


class SafeStartLoadPathTest : public QObject
{
	Q_OBJECT

private slots:

	void initTestCase()
	{
		// This binary is about the load path and nothing else: the path is
		// pinned per test, and no session is begun here.
		resetSkippedInstances();
	}

	void theLoadPathReallySkipsAThirdPartyInstance()
	{
#ifdef Q_OS_WIN
		QSKIP("a Windows test host cannot load plugin MODULE libraries: plugin modules link the "
			"zene executable, so their import descriptor names zene.exe and a test host cannot "
			"satisfy it (the same reason ScriptEngineTest and PluginScanCacheTest skip here)");
#else
		QTemporaryDir fixture;
		QVERIFY2(fixture.isValid(), "could not create the plugin directory fixture");
		const QFileInfo module(QDir(QStringLiteral(LMMS_TEST_SAFE_START_PLUGIN_DIR))
			.filePath(QString::fromUtf8(moduleFileName())));
		QVERIFY2(module.exists(),
			qPrintable(QStringLiteral("the build's own plugin module is missing: %1")
				.arg(module.absoluteFilePath())));
		QVERIFY2(QFile::copy(module.absoluteFilePath(),
				fixture.filePath(QString::fromUtf8(moduleFileName()))),
			"cannot copy the module into the fixture");

		// The fixture is a directory this build does not ship from, which is
		// exactly what makes the file third-party for the predicate - and it is
		// the ONLY plugin directory in the search path, so the name below
		// resolves to it.
		QVERIFY(isThirdPartyPluginFile(fixture.filePath(QString::fromUtf8(moduleFileName()))
			.toStdString()));
		// LMMS_PLUGIN_DIR is the one other way the build's own modules get into
		// the search path, so it is cleared: without that, the name could resolve
		// to a module this build ships and this case would prove nothing.
		qunsetenv("LMMS_PLUGIN_DIR");
		QDir::setSearchPaths(QStringLiteral("plugins"), QStringList{fixture.path()});

		QTemporaryDir dir;
		QVERIFY2(dir.isValid(), "could not create a temporary working directory");
		QVERIFY(install(dir.path().toStdString()));
		// A marker left by a previous run, written the way a crashed session
		// would have written it: safe-start mode is active for this session.
		{
			QFile marker(QString::fromStdString(markerPath()));
			QVERIFY(marker.open(QIODevice::WriteOnly | QIODevice::Truncate));
			marker.write("Zene Studio safe-start marker v1\npid=4245\n");
		}
		beginSession();
		QVERIFY(safeStartActive());
		resetSkippedInstances();

		// 1) SAFE: the instance is NOT created - and the module IS in the
		//    factory's catalogue, so the skip is the mode's doing.
		lmms::PluginFactory* factory = lmms::getPluginFactory();
		QVERIFY2(!factory->pluginInfo("midiexport").isNull(),
			"the fixture module was not discovered, so this case would prove nothing");
		QVERIFY2(factory->pluginInfo("midiexport").file.absoluteFilePath()
				== fixture.filePath(QString::fromUtf8(moduleFileName())),
			"the name must resolve to the FIXTURE copy: that is what makes it third-party");
		lmms::Plugin* skipped = lmms::Plugin::instantiate(QStringLiteral("midiexport"),
			nullptr, nullptr);
		QVERIFY2(skipped != nullptr, "Plugin::instantiate must still return an object");
		QVERIFY2(dynamic_cast<lmms::DummyPlugin*>(skipped) != nullptr,
			"safe-start mode must hand back the engine's DummyPlugin for a third-party module");
		QCOMPARE(skippedCount(), 1);
		delete skipped;

		// 2) THE CONTROL: the same call, with the session switch off, really
		//    loads the module (not a DummyPlugin).
		setSkipEnabled(false);
		lmms::Plugin* loaded = lmms::Plugin::instantiate(QStringLiteral("midiexport"),
			nullptr, nullptr);
		QVERIFY2(loaded != nullptr, "Plugin::instantiate must return an object");
		QVERIFY2(dynamic_cast<lmms::DummyPlugin*>(loaded) == nullptr,
			"with the session switch off the module must really load, or the skip above proves "
			"nothing about safe-start mode");
		delete loaded;
		QCOMPARE(skippedCount(), 1);   // the control added no record

		endSession();
		QDir::setSearchPaths(QStringLiteral("plugins"), QStringList{});
#endif
	}
};

QTEST_GUILESS_MAIN(SafeStartLoadPathTest)
#include "SafeStartLoadPathTest.moc"
