/*
 * PluginExportBranchTest.cpp - every test that compiles a plug-in's own sources into
 * its binary must take that plug-in's *export* branch, not the consumer branch
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
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QStringList>

#include <array>

// Generated at configure time by the audit at the bottom of tests/CMakeLists.txt: one row
// per (test target, plug-in source) pair that compiles a plug-in's own PLUGIN_EXPORT
// definitions, and one compile definition per row holding whether that target defines
// PLUGIN_STATIC_DEFINE or the export condition of the plug-in's generated header.
// The verdicts are `1`/`0` evaluated by the compiler from the targets' own
// COMPILE_DEFINITIONS (generator expressions), so this test reads a build fact.
#include "PluginExportBranchTargets.h"

namespace
{

struct Site
{
	QString target;
	QString exportCondition;
	QString source;
};

QList<Site> auditedSites()
{
	QList<Site> sites;
#define ZENE_PLUGIN_EXPORT_SITE(index, target, condition, source, verdict) \
	sites.append(Site{QStringLiteral(target), QStringLiteral(condition), QStringLiteral(source)});
	ZENE_PLUGIN_EXPORT_SITES(ZENE_PLUGIN_EXPORT_SITE)
#undef ZENE_PLUGIN_EXPORT_SITE
	return sites;
}

std::array<int, ZENE_PLUGIN_EXPORT_SITE_COUNT> siteVerdicts()
{
	std::array<int, ZENE_PLUGIN_EXPORT_SITE_COUNT> verdicts{};
#define ZENE_PLUGIN_EXPORT_SITE(index, target, condition, source, verdict) \
	verdicts[index] = (verdict);
	ZENE_PLUGIN_EXPORT_SITES(ZENE_PLUGIN_EXPORT_SITE)
#undef ZENE_PLUGIN_EXPORT_SITE
	return verdicts;
}

} // namespace

class PluginExportBranchTest : public QObject
{
	Q_OBJECT

private slots:
	// Hosted mingw64, run 43537465762 job 132269831285: RnnoiseDenoiserEffectTest compiled
	// plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp, which *defines* the plug-in's
	// PLUGIN_EXPORT descriptor and lmms_plugin_main. The generated plugin_export.h took its
	// consumer branch, so on Windows PLUGIN_EXPORT was __declspec(dllimport) and both
	// definitions were ill-formed:
	//   error: variable 'lmms::rnnoisedenoiser_plugin_descriptor' definition is marked dllimport
	//   error: function 'lmms::Plugin* lmms::lmms_plugin_main(Model*, void*)' definition is marked dllimport
	// On ELF both branches expand to visibility("default"), so a Linux compiler cannot observe
	// the difference; the defect is a build-configuration fact, which is what this test reads.
	void everyTranslatedPluginDefinitionTakesTheExportBranch()
	{
		// A scan that finds nothing would pass vacuously; the test targets that compile a
		// plug-in's own sources are the population. The floor sits below today's count so
		// adding a sibling target needs no edit here, and a broken scan cannot pass.
		QVERIFY2(ZENE_PLUGIN_EXPORT_SITE_COUNT >= 20,
			qPrintable(QStringLiteral("the configure-time audit saw only %1 test targets compiling a "
				"plug-in's own PLUGIN_EXPORT definition; the scan is broken, not the tree")
				.arg(ZENE_PLUGIN_EXPORT_SITE_COUNT)));
		const QList<Site> sites = auditedSites();
		QCOMPARE(sites.size(), ZENE_PLUGIN_EXPORT_SITE_COUNT);
		// The target the hosted mingw64 job failed on must be part of the audited set, so a
		// rename or removal of that target cannot turn this regression green by omission.
		QCOMPARE(ZENE_PLUGIN_EXPORT_CANARY_PRESENT, 1);

		const auto verdicts = siteVerdicts();
		QStringList offenders;
		for (int i = 0; i < sites.size(); ++i)
		{
			if (verdicts[static_cast<std::size_t>(i)] != 0)
			{
				continue;
			}
			offenders.append(QStringLiteral("%1 (%2: defines neither PLUGIN_STATIC_DEFINE nor %3)")
				.arg(sites.at(i).target, sites.at(i).source, sites.at(i).exportCondition));
		}
		QVERIFY2(offenders.isEmpty(),
			qPrintable(QStringLiteral("these test targets compile a plug-in's PLUGIN_EXPORT definitions "
				"but take neither export branch, so on Windows PLUGIN_EXPORT is __declspec(dllimport) "
				"and defining it is an error: %1").arg(offenders.join(QStringLiteral("; ")))));
	}
};

QTEST_GUILESS_MAIN(PluginExportBranchTest)

#include "PluginExportBranchTest.moc"
