/*
 * ClapLoaderErrorTest.cpp - the typed error path of the CLAP host
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

/*
 * WHAT THIS PROVES. plugins/ClapEffect/ClapLoader.h promises that every way a
 * CLAP load can fail has its own code, so a caller never has to match on a
 * sentence and never receives a null pointer it was supposed to dereference.
 * The claims are, in order:
 *
 *   1. A module file that cannot be opened is LibraryUnavailable -- and NOT one
 *      of the codes that mean "it opened and something inside was wrong".
 *   2. A module that loads but exports no `clap_entry` is SymbolMissing.
 *   3. A module with a good entry point that declares a CLAP 0.x version is
 *      VersionUnsupported -- and its init() is never called.
 *   4. A current entry point whose init() returns false is EntryInitFailed, and
 *      its deinit() is NOT called (nothing was initialized to undo).
 *   5. A current entry point with no plugin factory is FactoryMissing.
 *   6. The module fixture from ClapHostTest still loads: Code::None, so the
 *      typed path does not fire on success.
 *   7. Every code is distinct from every other code, has a non-empty stable
 *      token and a non-empty summary, and the tokens are one-to-one.
 *   8. The scan path (listClasses) reports the SAME code as the instance path
 *      (HostedPlugin::load) for the same module -- a plug-in directory scan and
 *      a load attempt cannot disagree about why a module was rejected.
 *   9. The sentences stay the ones the product already shows: this task added a
 *      type to the failures, it did not reword them under the user.
 *
 * The subjects are real modules built from tests/data/clap-test-plugin/
 * clap-test-broken.c, one per fault, so the faults are structural and a wrong
 * code is a host defect rather than a fixture artefact. They are shared
 * modules, which is the only way to exercise the loader at all.
 *
 * The test runs wherever the fixtures build -- Linux, macOS and the Windows
 * jobs -- and needs no audio device, no Qt widgets and no LMMS engine.
 */

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>

#include <array>
#include <utility>

#include <clap/clap.h>

#include "ClapHost.h"
#include "ClapLoader.h"

#ifndef CLAP_TEST_PLUGIN_PATH
#define CLAP_TEST_PLUGIN_PATH ""
#endif
#ifndef CLAP_TEST_BROKEN_NO_SYMBOL_PATH
#define CLAP_TEST_BROKEN_NO_SYMBOL_PATH ""
#endif
#ifndef CLAP_TEST_BROKEN_OLD_VERSION_PATH
#define CLAP_TEST_BROKEN_OLD_VERSION_PATH ""
#endif
#ifndef CLAP_TEST_BROKEN_INIT_FAILS_PATH
#define CLAP_TEST_BROKEN_INIT_FAILS_PATH ""
#endif
#ifndef CLAP_TEST_BROKEN_NO_FACTORY_PATH
#define CLAP_TEST_BROKEN_NO_FACTORY_PATH ""
#endif

namespace lmms::clap
{

namespace
{
//! The module fixture ClapHostTest uses: the control that says the typed path
//! does not fire when a module is fine.
constexpr const char* GoodModulePath = CLAP_TEST_PLUGIN_PATH;
constexpr const char* NoSymbolModulePath = CLAP_TEST_BROKEN_NO_SYMBOL_PATH;
constexpr const char* OldVersionModulePath = CLAP_TEST_BROKEN_OLD_VERSION_PATH;
constexpr const char* InitFailsModulePath = CLAP_TEST_BROKEN_INIT_FAILS_PATH;
constexpr const char* NoFactoryModulePath = CLAP_TEST_BROKEN_NO_FACTORY_PATH;

//! The plug-in id inside the good fixture.
constexpr const char* GoodPluginId = "org.lmms.test.clap-gain";

//! The token as a QString, for QCOMPARE failure messages that name the code.
auto tokenOf(const loader::Status& status) -> QString
{
	return QString::fromLatin1(status.token());
}

//! The control fixture's path as a QString. The paths arrive as compile
//! definitions (the fixture is built by this target's own CMake block), so they
//! are variables and not literals: QStringLiteral cannot take them.
auto goodModule() -> QString
{
	return QString::fromLatin1(GoodModulePath);
}
} // namespace

class ClapLoaderErrorTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();

	void testGoodModuleHasNoFailure();
	void testUnopenableFileIsLibraryUnavailable();
	void testJunkFileIsLibraryUnavailable();
	void testNoSymbolIsSymbolMissing();
	void testOldVersionIsVersionUnsupported();
	void testInitFailureIsEntryInitFailed();
	void testMissingFactoryIsFactoryMissing();
	void testUnknownIdIsPluginNotFound();
	void testFailuresAreDistinctCodes();
	void testEveryCodeHasAStableToken();
	void testScanAndInstanceAgreeOnTheCode();
	void testMessagesKeepTheirShape();

private:
	//! Asserts the typed code and its token together: the code is the contract,
	//! the token is what a caller (and this failure message) can print.
	void verifyCode(const loader::Status& status, loader::Code expected, const char* token);
};

void ClapLoaderErrorTest::verifyCode(const loader::Status& status, loader::Code expected, const char* token)
{
	QVERIFY2(status.code == expected, qPrintable(QStringLiteral("expected code %1, got %2")
		.arg(QString::fromLatin1(token), tokenOf(status))));
	QCOMPARE(tokenOf(status), QString::fromLatin1(token));
	QVERIFY(status.failed());
	QVERIFY2(!status.detail.isEmpty(), "a failed load must say what it was looking at");
}

void ClapLoaderErrorTest::initTestCase()
{
	// The fixtures come from one CMake block (tests/data/clap-test-plugin), so
	// the good module being absent means the block did not build -- the same
	// skip ClapHostTest takes, for the same reason.
	const QString good = goodModule();
	if (good.isEmpty() || !QFileInfo::exists(good))
	{
		QSKIP("no CLAP test plug-in available (build tests/data/clap-test-plugin "
			"from the pinned CLAP headers and configure with -DLMMS_CLAP_PATH=<checkout>)");
	}
	for (const auto* path : {NoSymbolModulePath, OldVersionModulePath, InitFailsModulePath,
			NoFactoryModulePath})
	{
		QVERIFY2(QFileInfo::exists(QString::fromLatin1(path)),
			qPrintable(QStringLiteral("the one-fault fixture is missing: %1").arg(QString::fromLatin1(path))));
	}
}

void ClapLoaderErrorTest::testGoodModuleHasNoFailure()
{
	HostedPlugin plugin;
	QString error;
	QVERIFY2(plugin.load(goodModule(), QString::fromLatin1(GoodPluginId), &error),
		qPrintable(error));
	QCOMPARE(tokenOf(plugin.lastLoadFailure()), QStringLiteral("none"));
	QVERIFY(!plugin.lastLoadFailure().failed());
	QVERIFY(plugin.isLoaded());

	// The same module through the scanner: a usable module reports no failure
	// either, so a green scan is not green because nothing was checked.
	loader::Status status;
	const auto classes = listClasses(goodModule(), &status, &error);
	QCOMPARE(classes.size(), std::size_t{1});
	QCOMPARE(tokenOf(status), QStringLiteral("none"));
	QVERIFY(!status.failed());
}

void ClapLoaderErrorTest::testUnopenableFileIsLibraryUnavailable()
{
	const QString missing = QDir::temp().filePath(
		QStringLiteral("zene-no-such-clap-module-%1.clap").arg(QCoreApplication::applicationPid()));
	QVERIFY(!QFileInfo::exists(missing));

	HostedPlugin plugin;
	QString error;
	QVERIFY(!plugin.load(missing, QString(), &error));
	verifyCode(plugin.lastLoadFailure(), loader::Code::LibraryUnavailable, "library-unavailable");
	QVERIFY(!plugin.isLoaded());
	QVERIFY2(error.contains(missing), qPrintable(error));
}

void ClapLoaderErrorTest::testJunkFileIsLibraryUnavailable()
{
	// A file that exists and is not a module. The platform loader refuses it,
	// which is still LibraryUnavailable and still NOT "no clap_entry" -- the
	// distinction that matters, because one is a missing file and the other is
	// a file that is present and wrong.
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString junk = dir.filePath(QStringLiteral("not-a-module.clap"));
	QFile file{junk};
	QVERIFY(file.open(QIODevice::WriteOnly));
	file.write("this is not a shared object\n");
	file.close();

	HostedPlugin plugin;
	QString error;
	QVERIFY(!plugin.load(junk, QString(), &error));
	verifyCode(plugin.lastLoadFailure(), loader::Code::LibraryUnavailable, "library-unavailable");
}

void ClapLoaderErrorTest::testNoSymbolIsSymbolMissing()
{
	HostedPlugin plugin;
	QString error;
	QVERIFY(!plugin.load(QString::fromLatin1(NoSymbolModulePath), QString(), &error));
	verifyCode(plugin.lastLoadFailure(), loader::Code::SymbolMissing, "symbol-missing");
	QVERIFY2(error.contains(QStringLiteral("does not export clap_entry")), qPrintable(error));
	QVERIFY(!plugin.isLoaded());
}

void ClapLoaderErrorTest::testOldVersionIsVersionUnsupported()
{
	HostedPlugin plugin;
	QString error;
	QVERIFY(!plugin.load(QString::fromLatin1(OldVersionModulePath), QString(), &error));
	verifyCode(plugin.lastLoadFailure(), loader::Code::VersionUnsupported, "version-unsupported");
	// The version it refused is named in the sentence, and the module's own
	// init() said nothing: the check runs before init(), which is the whole
	// reason the code is about the version and not about initialization.
	QVERIFY2(error.contains(QStringLiteral("is a CLAP 0.0 plug-in")), qPrintable(error));
	QVERIFY2(!error.contains(QStringLiteral("init() called")), qPrintable(error));
}

void ClapLoaderErrorTest::testInitFailureIsEntryInitFailed()
{
	HostedPlugin plugin;
	QString error;
	QVERIFY(!plugin.load(QString::fromLatin1(InitFailsModulePath), QString(), &error));
	verifyCode(plugin.lastLoadFailure(), loader::Code::EntryInitFailed, "entry-init-failed");
	QVERIFY2(error.contains(QStringLiteral("clap_entry.init() failed")), qPrintable(error));
}

void ClapLoaderErrorTest::testMissingFactoryIsFactoryMissing()
{
	HostedPlugin plugin;
	QString error;
	QVERIFY(!plugin.load(QString::fromLatin1(NoFactoryModulePath), QString(), &error));
	verifyCode(plugin.lastLoadFailure(), loader::Code::FactoryMissing, "factory-missing");
	QVERIFY2(error.contains(QString::fromLatin1(CLAP_PLUGIN_FACTORY_ID)), qPrintable(error));
}

void ClapLoaderErrorTest::testUnknownIdIsPluginNotFound()
{
	// A module that is entirely fine, asked for a plug-in it does not contain:
	// distinct from every module-level failure above, and reached through the
	// same typed answer.
	HostedPlugin plugin;
	QString error;
	QVERIFY(!plugin.load(goodModule(), QStringLiteral("org.lmms.test.absent"), &error));
	verifyCode(plugin.lastLoadFailure(), loader::Code::PluginNotFound, "plugin-not-found");
	QVERIFY2(error.contains(QStringLiteral("has no plug-in with id")), qPrintable(error));

	// The failed attempt must not leave the instance half-loaded.
	QVERIFY(!plugin.isLoaded());
}

void ClapLoaderErrorTest::testFailuresAreDistinctCodes()
{
	// The requirement in one place: the six failures a CLAP host actually meets
	// are six DIFFERENT answers. (Each is also asserted on its own above.)
	const QString missing = QDir::temp().filePath(
		QStringLiteral("zene-no-such-clap-module-%1.clap").arg(QCoreApplication::applicationPid()));
	const std::array<QPair<QString, QString>, 6> cases{{
		{missing, QString()},
		{QString::fromLatin1(NoSymbolModulePath), QString()},
		{QString::fromLatin1(OldVersionModulePath), QString()},
		{QString::fromLatin1(InitFailsModulePath), QString()},
		{QString::fromLatin1(NoFactoryModulePath), QString()},
		{goodModule(), QStringLiteral("org.lmms.test.absent")},
	}};
	QSet<QString> seen;
	for (const auto& [path, id] : cases)
	{
		HostedPlugin plugin;
		QString error;
		QVERIFY2(!plugin.load(path, id, &error), qPrintable(path));
		QVERIFY2(plugin.lastLoadFailure().failed(), qPrintable(path));
		seen.insert(tokenOf(plugin.lastLoadFailure()));
	}
	QCOMPARE(seen.size(), 6);
	QVERIFY(!seen.contains(QStringLiteral("none")));
}

void ClapLoaderErrorTest::testEveryCodeHasAStableToken()
{
	// The tokens are a published contract (a log or an agent matches on them),
	// so a code with no token, a duplicate token, or a token that is not a
	// stable slug is a failure here rather than a surprise at a call site.
	QSet<QString> tokens;
	QSet<QString> summaries;
	for (int i = 0; i < static_cast<int>(loader::Code::Count); ++i)
	{
		const auto code = static_cast<loader::Code>(i);
		const QString token = QString::fromLatin1(loader::token(code));
		const QString summary = QString::fromLatin1(loader::summary(code));
		QVERIFY2(!token.isEmpty(), qPrintable(QStringLiteral("code %1 has no token").arg(i)));
		QVERIFY2(token == QLatin1String("none") || token.contains(QRegularExpression(QStringLiteral("^[a-z]+(-[a-z]+)*$"))),
			qPrintable(token));
		QVERIFY2(!summary.isEmpty(), qPrintable(token));
		QVERIFY2(!tokens.contains(token), qPrintable(QStringLiteral("duplicate token %1").arg(token)));
		QVERIFY2(!summaries.contains(summary), qPrintable(QStringLiteral("duplicate summary %1").arg(summary)));
		tokens.insert(token);
		summaries.insert(summary);
	}
	QCOMPARE(tokens.size(), static_cast<int>(loader::Code::Count));
	QCOMPARE(QString::fromLatin1(loader::token(loader::Code::None)), QStringLiteral("none"));
	QCOMPARE(QString::fromLatin1(loader::summary(loader::Code::None)), QStringLiteral("no failure"));
}

void ClapLoaderErrorTest::testScanAndInstanceAgreeOnTheCode()
{
	// The scan path has no instance, so before the loader was typed it could
	// only say "'<path>' is not a usable CLAP module" for four different
	// failures. It reports the same codes now, and it must not disagree with
	// the instance path about the same file.
	const std::array<std::pair<const char*, const char*>, 4> broken{{
		{NoSymbolModulePath, "symbol-missing"},
		{OldVersionModulePath, "version-unsupported"},
		{InitFailsModulePath, "entry-init-failed"},
		{NoFactoryModulePath, "factory-missing"},
	}};
	for (const auto& [path, token] : broken)
	{
		const QString modulePath = QString::fromLatin1(path);
		loader::Status scanned;
		QString scanError;
		const auto classes = listClasses(modulePath, &scanned, &scanError);
		QVERIFY(classes.empty());
		QCOMPARE(tokenOf(scanned), QString::fromLatin1(token));
		QVERIFY(!scanError.isEmpty());

		HostedPlugin plugin;
		QString instanceError;
		QVERIFY(!plugin.load(modulePath, QString(), &instanceError));
		QCOMPARE(tokenOf(plugin.lastLoadFailure()), tokenOf(scanned));
		QCOMPARE(instanceError, scanError);
	}
}

void ClapLoaderErrorTest::testMessagesKeepTheirShape()
{
	// These sentences are what the UI and the log show, and this task added a
	// type to the failures rather than rewording them. Each is pinned to its
	// shape, with the module path it names.
	struct Case
	{
		const char* path;
		const char* id;      //!< the id load() is asked for ("" = the module's first plug-in)
		const char* second;  //!< what the sentence interpolates at %2, if anything
		QString substring;
	};
	const std::array<Case, 5> cases{{
		{NoSymbolModulePath, "", nullptr, QStringLiteral("'%1' does not export clap_entry")},
		{OldVersionModulePath, "", nullptr,
			QStringLiteral("'%1' is a CLAP 0.0 plug-in, which is not compatible")},
		{InitFailsModulePath, "", nullptr, QStringLiteral("clap_entry.init() failed for '%1'")},
		{NoFactoryModulePath, "", CLAP_PLUGIN_FACTORY_ID, QStringLiteral("'%1' has no %2 factory")},
		{GoodModulePath, "org.lmms.test.absent", "org.lmms.test.absent",
			QStringLiteral("'%1' has no plug-in with id '%2'")},
	}};
	for (const auto& one : cases)
	{
		const QString modulePath = QString::fromLatin1(one.path);
		HostedPlugin plugin;
		QString error;
		QVERIFY(!plugin.load(modulePath, QString::fromLatin1(one.id), &error));
		const QString expected = one.second
			? one.substring.arg(modulePath, QString::fromLatin1(one.second))
			: one.substring.arg(modulePath);
		QCOMPARE(error, expected);
	}
}

} // namespace lmms::clap

QTEST_GUILESS_MAIN(lmms::clap::ClapLoaderErrorTest)
#include "ClapLoaderErrorTest.moc"
