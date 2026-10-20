/*
 * LadspaManagerTest.cpp - descriptor port-count scan boundaries
 *
 * This file is part of LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "LadspaManager.h"

#include <QtTest>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <climits>
#include <cstdint>

namespace lmms
{

namespace
{

// The scan directories are process-wide state: the current directory, the
// LADSPA_PATH environment variable and the "plugins" search path are all
// restored when the guard goes out of scope, so a case can never leak its
// environment into the next one.
class ScopedCurrentDirectory
{
public:
	ScopedCurrentDirectory() : m_original(QDir::currentPath()) {}
	~ScopedCurrentDirectory() { QDir::setCurrent(m_original); }

private:
	QString m_original;
};

class ScopedLadspaPath
{
public:
	ScopedLadspaPath() : m_original(qgetenv("LADSPA_PATH")) {}
	~ScopedLadspaPath() { qputenv("LADSPA_PATH", m_original); }

private:
	QByteArray m_original;
};

class ScopedSearchPaths
{
public:
	ScopedSearchPaths(const QString& prefix)
		: m_prefix(prefix), m_original(QDir::searchPaths(prefix))
	{
	}
	~ScopedSearchPaths() { QDir::setSearchPaths(m_prefix, m_original); }

	void replaceWith(const QStringList& paths) { QDir::setSearchPaths(m_prefix, paths); }

private:
	QString m_prefix;
	QStringList m_original;
};

// Copies the LADSPA fixture module into `directory` under a name of its own, so
// a scan that finds the copy can be told apart from a scan that found the
// original. Returns the copied file name, empty when there is nothing to copy.
QString copyFixtureModule(const QString& directory, const QString& name)
{
	const QDir source(QString::fromUtf8(LADSPA_MANAGER_FIXTURE_DIR));
	const auto modules = source.entryList({QStringLiteral("*.so"), QStringLiteral("*.dll")},
		QDir::Files);
	if (modules.isEmpty()) { return {}; }

	const QString probe = name + "." + QFileInfo(modules.front()).suffix();
	if (!QFile::copy(source.filePath(modules.front()), QDir(directory).filePath(probe)))
	{
		return {};
	}
	return probe;
}

// True when the manager registered a plugin whose library is `fileName`.
bool hasPluginForFile(const l_sortable_plugin_t& plugins, const QString& fileName)
{
	return std::any_of(plugins.cbegin(), plugins.cend(),
		[&fileName](const auto& plugin) { return plugin.second.first == fileName; });
}

} // namespace

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

	// The bundled LADSPA directory of the working directory ("plugins/ladspa")
	// is a scan location of its own: a module installed there has to be found.
	void bundledLadspaDirectoryIsScanned()
	{
		QTemporaryDir workingDirectory;
		QVERIFY(workingDirectory.isValid());

		const QString bundledDirectory = QDir(workingDirectory.path()).filePath("plugins/ladspa");
		QVERIFY(QDir().mkpath(bundledDirectory));

		const QString probe = copyFixtureModule(bundledDirectory,
			QStringLiteral("zene_bundled_ladspa_probe"));
		QVERIFY2(!probe.isEmpty(), "no LADSPA fixture module to copy");

		ScopedCurrentDirectory currentDirectory;
		ScopedLadspaPath ladspaPath;
		QVERIFY(QDir::setCurrent(workingDirectory.path()));
		qputenv("LADSPA_PATH", QByteArray{});

		LadspaManager manager;
		QVERIFY2(hasPluginForFile(manager.getSortedPlugins(), probe),
			"the LADSPA module in the working directory's plugins/ladspa was not scanned");
	}

	// The "plugins" search path (the application relative plugin directory)
	// stays a scan location: the bundled LADSPA modules of a build live there.
	void bundledSearchPathDirectoryIsScanned()
	{
		QTemporaryDir root;
		QVERIFY(root.isValid());

		const QString pluginsRoot = QDir(root.path()).filePath("plugins");
		const QString bundledDirectory = QDir(pluginsRoot).filePath("ladspa");
		QVERIFY(QDir().mkpath(bundledDirectory));

		const QString probe = copyFixtureModule(bundledDirectory,
			QStringLiteral("zene_bundled_search_path_probe"));
		QVERIFY2(!probe.isEmpty(), "no LADSPA fixture module to copy");

		ScopedSearchPaths searchPaths(QStringLiteral("plugins"));
		searchPaths.replaceWith(QStringList{pluginsRoot});
		ScopedLadspaPath ladspaPath;
		qputenv("LADSPA_PATH", QByteArray{});

		LadspaManager manager;
		QVERIFY2(hasPluginForFile(manager.getSortedPlugins(), probe),
			"the LADSPA module in the plugins search path was not scanned");
	}
};

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::LadspaManagerTest)

#include "LadspaManagerTest.moc"
