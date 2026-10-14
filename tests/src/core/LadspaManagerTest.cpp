/*
 * LadspaManagerTest.cpp - descriptor port-count scan boundaries
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "LadspaManager.h"

#include <QtTest>

#include <algorithm>
#include <climits>
#include <cstdint>

namespace lmms
{

class LadspaManagerTest : public QObject
{
	Q_OBJECT

private slots:
	void scansWidePortCountsAndRejectsUnrepresentableDescriptors()
	{
		LadspaManager manager;
		const auto plugins = manager.getSortedPlugins();
		const auto hasLabel = [&plugins](const QString& label)
		{
			return std::any_of(plugins.cbegin(), plugins.cend(), [&label](const auto& plugin)
			{
				return plugin.second.second == label;
			});
		};
		const auto wide = std::find_if(plugins.cbegin(), plugins.cend(), [](const auto& plugin)
		{
			return plugin.second.second == QLatin1String("zene_test_control_65536");
		});

		QVERIFY(wide != plugins.cend());
		QCOMPARE(manager.getPortCount(wide->second), std::uint32_t{65536});
		QVERIFY(!hasLabel(QLatin1String("zene_test_audio_65536")));

#if ULONG_MAX > UINT32_MAX
		QVERIFY(!hasLabel(QLatin1String("zene_test_port_count_overflow")));
#endif
	}
};

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::LadspaManagerTest)

#include "LadspaManagerTest.moc"
