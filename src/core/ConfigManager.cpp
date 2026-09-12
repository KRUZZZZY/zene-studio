/*
 * ConfigManager.cpp - implementation of class ConfigManager
 *
 * Copyright (c) 2005-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "ConfigManager.h"

#include <QApplication>
#include <QDir>
#include <QDomElement>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTextStream>

#include "DeprecationHelper.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "PathUtil.h"
#include "ProjectVersion.h"
#include "lmmsversion.h"

namespace lmms
{


// Vector with all the upgrade methods
const std::vector<ConfigManager::UpgradeMethod> ConfigManager::UPGRADE_METHODS = {
	&ConfigManager::upgrade_1_1_90    ,    &ConfigManager::upgrade_1_1_91,
	&ConfigManager::upgrade_1_2_2
};

static inline QString ensureTrailingSlash(const QString & s )
{
	if(! s.isEmpty() && !s.endsWith('/') && !s.endsWith('\\'))
	{
		return s + '/';
	}
	return s;
}


ConfigManager * ConfigManager::s_instanceOfMe = nullptr;


ConfigManager::ConfigManager() :
	m_version(defaultVersion()),
	m_configVersion( UPGRADE_METHODS.size() )
{
	if (QFileInfo::exists(qApp->applicationDirPath() + PORTABLE_MODE_FILE))
	{
		initPortableWorkingDir();
	}
	else
	{
		initInstalledWorkingDir();
	}
	m_dataDir = "data:/";
	m_vstDir = m_workingDir + "vst/";
	m_sf2Dir = m_workingDir + SF2_PATH;
	m_gigDir = m_workingDir + GIG_PATH;
	m_themeDir = defaultThemeDir();
	if (std::getenv("LMMS_DATA_DIR"))
	{
		QDir::addSearchPath("data", QString::fromLocal8Bit(std::getenv("LMMS_DATA_DIR")));
	}
	initDevelopmentWorkingDir();

#ifdef LMMS_BUILD_WIN32
	QDir::addSearchPath("data", qApp->applicationDirPath() + "/data/");
#else
	QDir::addSearchPath("data", qApp->applicationDirPath().section('/', 0, -2) + "/share/zene/");
#endif

}




ConfigManager::~ConfigManager()
{
	saveConfigFile();
}


void ConfigManager::upgrade_1_1_90()
{
	// Remove trailing " (bad latency!)" string which was once saved with PulseAudio
	if(value("mixer", "audiodev").startsWith("PulseAudio ("))
	{
		setValue("mixer", "audiodev", "PulseAudio");
	}

	// MidiAlsaRaw used to store the device info as "Device" instead of "device"
	if (value("MidiAlsaRaw", "device").isNull())
	{
		// copy "device" = "Device" and then delete the old "Device" (further down)
		QString oldDevice = value("MidiAlsaRaw", "Device");
		setValue("MidiAlsaRaw", "device", oldDevice);
	}
	if (!value("MidiAlsaRaw", "device").isNull())
	{
		// delete the old "Device" in the case that we just copied it to "device"
		//   or if the user somehow set both the "Device" and "device" fields
		deleteValue("MidiAlsaRaw", "Device");
	}
}

void ConfigManager::upgrade_1_1_91()
{
	// rename displaydbv to displaydbfs
	if (!value("app", "displaydbv").isNull())
	{
		setValue("app", "displaydbfs", value("app", "displaydbv"));
		deleteValue("app", "displaydbv");
	}
}

void ConfigManager::upgrade_1_2_2()
{
	// Since mixer has been renamed to audioengine, we need to transfer the
	// attributes from the old element to the new one
	std::vector<QString> attrs = {
		"audiodev", "mididev", "framesperaudiobuffer", "hqaudio", "samplerate"
	};

	for (auto attr : attrs)
	{
		if (!value("mixer", attr).isNull())
		{
			setValue("audioengine", attr, value("mixer", attr));
			deleteValue("mixer", attr);
		}
	}

	m_settings.remove("mixer");
}

void ConfigManager::upgrade()
{
	// Skip the upgrade if versions match
	if (m_version == LMMS_VERSION)
	{
		return;
	}

	// Runs all necessary upgrade methods
	std::size_t max = std::min(static_cast<std::size_t>(m_configVersion), UPGRADE_METHODS.size());
	std::for_each( UPGRADE_METHODS.begin() + max, UPGRADE_METHODS.end(),
		[this](UpgradeMethod um)
		{
			(this->*um)();
		}
	);
	
	ProjectVersion createdWith = m_version;
	
	// Don't use old themes as they break the UI (i.e. 0.4 != 1.0, etc)
	if (createdWith.setCompareType(ProjectVersion::CompareType::Minor) != LMMS_VERSION)
	{
		m_themeDir = defaultThemeDir();
	}

	// Bump the version, now that we are upgraded
	m_version = LMMS_VERSION;
	m_configVersion = UPGRADE_METHODS.size();
}

QString ConfigManager::defaultVersion() const
{
	return LMMS_VERSION;
}

bool ConfigManager::enableBlockedPlugins()
{
	const char* envVar = getenv("LMMS_ENABLE_BLOCKED_PLUGINS");
	return (envVar && *envVar);
}

QStringList ConfigManager::availableVstEmbedMethods()
{
	QStringList methods;
	methods.append("none");
	methods.append("qt");
#ifdef LMMS_BUILD_WIN32
	methods.append("win32");
#endif
#if defined(LMMS_BUILD_LINUX) && (QT_VERSION < QT_VERSION_CHECK(6,0,0))
	if (static_cast<QGuiApplication*>(QApplication::instance())->
		platformName() == "xcb")
	{
		methods.append("xembed");
	}
#endif
	return methods;
}

QString ConfigManager::vstEmbedMethod() const
{
	QStringList methods = availableVstEmbedMethods();
	QString defaultMethod = *(methods.end() - 1);
	QString currentMethod = value( "ui", "vstembedmethod", defaultMethod );
	return methods.contains(currentMethod) ? currentMethod : defaultMethod;
}

bool ConfigManager::hasWorkingDir() const
{
	return QDir(m_workingDir).exists();
}


void ConfigManager::setWorkingDir(const QString & workingDir)
{
	m_workingDir = ensureTrailingSlash(QDir::cleanPath(workingDir));
}




void ConfigManager::setVSTDir(const QString & vstDir)
{
	m_vstDir = ensureTrailingSlash(vstDir);
}




void ConfigManager::setLADSPADir(const QString & ladspaDir)
{
	m_ladspaDir = ladspaDir;
}




void ConfigManager::setSTKDir(const QString & stkDir)
{
#ifdef LMMS_HAVE_STK
	m_stkDir = ensureTrailingSlash(stkDir);
#endif
}




void ConfigManager::setSF2Dir(const QString & sf2Dir)
{
	m_sf2Dir = sf2Dir;
}




void ConfigManager::setSF2File(const QString & sf2File)
{
#ifdef LMMS_HAVE_FLUIDSYNTH
	m_sf2File = sf2File;
#endif
}




void ConfigManager::setGIGDir(const QString & gigDir)
{
	m_gigDir = gigDir;
}




void ConfigManager::setThemeDir(const QString & themeDir)
{
	m_themeDir = ensureTrailingSlash(themeDir);
}




void ConfigManager::setBackgroundPicFile(const QString & backgroundPicFile)
{
	m_backgroundPicFile = backgroundPicFile;
}

void ConfigManager::createWorkingDir()
{
	QDir().mkpath(m_workingDir);

	QDir().mkpath(userProjectsDir());
	QDir().mkpath(userTemplateDir());
	QDir().mkpath(userSamplesDir());
	QDir().mkpath(userPresetsDir());
	QDir().mkpath(userGigDir());
	QDir().mkpath(userSf2Dir());
	QDir().mkpath(userVstDir());
	QDir().mkpath(userLadspaDir());
}



void ConfigManager::addRecentlyOpenedProject(const QString & file)
{
	QFileInfo recentFile(file);
	if(recentFile.suffix().toLower() == "mmp" ||
		recentFile.suffix().toLower() == "mmpz" ||
		recentFile.suffix().toLower() == "mpt")
	{
		m_recentlyOpenedProjects.removeAll(file);
		if(m_recentlyOpenedProjects.size() > 50)
		{
			m_recentlyOpenedProjects.removeLast();
		}
		m_recentlyOpenedProjects.push_front(file);
		ConfigManager::inst()->saveConfigFile();
	}
}

void ConfigManager::addFavoriteItem(const QString& item)
{
	m_favoriteItems.push_back(item);
	saveConfigFile();
	emit favoritesChanged();
}

void ConfigManager::removeFavoriteItem(const QString& item)
{
	m_favoriteItems.removeAll(item);
	saveConfigFile();
	emit favoritesChanged();
}

bool ConfigManager::isFavoriteItem(const QString& item)
{
	const auto& items = favoriteItems();
	const auto it = std::find_if(items.begin(), items.end(),
		[&](const auto& favoriteItem) { return QFileInfo{item} == QFileInfo{favoriteItem}; });
	return it != items.end();
}

QString ConfigManager::value(const QString& cls, const QString& attribute, const QString& defaultVal) const
{
	if (m_settings.find(cls) != m_settings.end())
	{
		for (const auto& setting : m_settings[cls])
		{
			if (setting.first == attribute)
			{
				return setting.second;
			}
		}
	}
	return defaultVal;
}




void ConfigManager::setValue(const QString & cls,
				const QString & attribute,
				const QString & value)
{
	if(m_settings.contains(cls))
	{
		for(QPair<QString, QString>& pair : m_settings[cls])
		{
			if(pair.first == attribute)
			{
				if (pair.second != value)
				{
					pair.second = value;
					emit valueChanged(cls, attribute, value);
				}
				return;
			}
		}
	}
	// not in map yet, so we have to add it...
	m_settings[cls].push_back(qMakePair(attribute, value));
}


void ConfigManager::deleteValue(const QString & cls, const QString & attribute)
{
	if(m_settings.contains(cls))
	{
		for(stringPairVector::iterator it = m_settings[cls].begin();
					it != m_settings[cls].end(); ++it)
		{
			if((*it).first == attribute)
			{
				m_settings[cls].erase(it);
				return;
			}
		}
	}
}


void ConfigManager::loadConfigFile(const QString & configFile)
{
	// read the XML file and create DOM tree
	// Allow configuration file override through --config commandline option
	if (!configFile.isEmpty())
	{
		m_configFile = configFile;
	}

	QFile cfg_file(m_configFile);
	QDomDocument dom_tree;

	if(cfg_file.open(QIODevice::ReadOnly))
	{
		QString errorString;
		int errorLine, errorCol;
		if (lmms::setContent(dom_tree, &cfg_file, false, &errorString, &errorLine, &errorCol))
		{
			// get the head information from the DOM
			QDomElement root = dom_tree.documentElement();

			QDomNode node = root.firstChild();

			// Cache LMMS version
			if (!root.attribute("version").isNull()) {
				m_version = root.attribute("version");
			}

			// Get the version of the configuration file (for upgrade purposes)
			if( root.attribute("configversion").isNull() )
			{
				m_configVersion = legacyConfigVersion(); // No configversion attribute found
			}
			else
			{
				bool success;
				m_configVersion = root.attribute("configversion").toUInt(&success);
				if( !success ) qWarning("Config Version conversion failure.");
			}

			// create the settings-map out of the DOM
			while(!node.isNull())
			{
				if(node.isElement() &&
					node.toElement().hasAttributes ())
				{
					stringPairVector attr;
					QDomNamedNodeMap node_attr =
						node.toElement().attributes();
					for(int i = 0; i < node_attr.count();
									++i)
					{
						QDomNode n = node_attr.item(i);
						if(n.isAttr())
						{
							attr.push_back(qMakePair(n.toAttr().name(),
											n.toAttr().value()));
						}
					}
					m_settings[node.nodeName()] = attr;
				}
				else if(node.nodeName() == "recentfiles")
				{
					m_recentlyOpenedProjects.clear();
					QDomNode n = node.firstChild();
					while(!n.isNull())
					{
						if(n.isElement() && n.toElement().hasAttributes())
						{
							m_recentlyOpenedProjects << n.toElement().attribute("path");
						}
						n = n.nextSibling();
					}
				}
				else if (node.nodeName() == "favoriteitems")
				{
					m_favoriteItems.clear();
					QDomNode n = node.firstChild();
					while (!n.isNull())
					{
						if (n.isElement() && n.toElement().hasAttributes())
						{
							m_favoriteItems << n.toElement().attribute("path");
						}
						n = n.nextSibling();
					}
				}
				node = node.nextSibling();
			}

			if(value("paths", "theme") != "")
			{
				m_themeDir = value("paths", "theme");
#ifdef LMMS_BUILD_WIN32
				// Detect a QDir/QFile hang on Windows
				// see issue #3417 on github
				bool badPath = (m_themeDir == "/" || m_themeDir == "\\");
#else
				bool badPath = false;
#endif

				if(badPath || !QDir(m_themeDir).exists() ||
						!QFile(m_themeDir + "/style.css").exists())
				{
					m_themeDir = defaultThemeDir();
				}
				m_themeDir = ensureTrailingSlash(m_themeDir);
			}
			setWorkingDir(value("paths", "workingdir"));

			setGIGDir(value("paths", "gigdir") == "" ? gigDir() : value("paths", "gigdir"));
			setSF2Dir(value("paths", "sf2dir") == "" ? sf2Dir() : value("paths", "sf2dir"));
			setVSTDir(value("paths", "vstdir"));
			setLADSPADir(value("paths", "ladspadir"));
		#ifdef LMMS_HAVE_STK
			setSTKDir(value("paths", "stkdir"));
		#endif
		#ifdef LMMS_HAVE_FLUIDSYNTH
			setSF2File(value("paths", "defaultsf2"));
		#endif
			setBackgroundPicFile(value("paths", "backgroundtheme"));
		}
		else if (gui::getGUI() != nullptr)
		{
			QMessageBox::warning(nullptr, gui::MainWindow::tr("Configuration file"),
									gui::MainWindow::tr("Error while parsing configuration file at line %1:%2: %3").
													arg(errorLine).
													arg(errorCol).
													arg(errorString));
		}
		cfg_file.close();
	}

	// Plugins are searched recursively, block problematic locations
	if( m_vstDir.isEmpty() || m_vstDir == QDir::separator() || m_vstDir == "/" ||
			m_vstDir == ensureTrailingSlash( QDir::homePath() ) ||
			!QDir( m_vstDir ).exists() )
	{
#ifdef LMMS_BUILD_WIN32
		QString programFiles = QString::fromLocal8Bit(getenv("ProgramFiles"));
		m_vstDir =  programFiles + "/VstPlugins/";
#else
		m_vstDir =  m_workingDir + "plugins/vst/";
#endif
	}

	if(m_ladspaDir.isEmpty() )
	{
		m_ladspaDir = userLadspaDir();
	}

#ifdef LMMS_HAVE_STK
	if(m_stkDir.isEmpty() || m_stkDir == QDir::separator() || m_stkDir == "/" ||
			!QDir(m_stkDir).exists())
	{
#if defined(LMMS_BUILD_WIN32)
		m_stkDir = m_dataDir + "stk/rawwaves/";
#else
		// Look for bundled raw waves first
		m_stkDir = qApp->applicationDirPath() + "/../share/stk/rawwaves/";
		// Try system installations if not exists
		if (!QDir(m_stkDir).exists())
		{
			m_stkDir = "/usr/local/share/stk/rawwaves/";
		}
		if (!QDir(m_stkDir).exists())
		{
			m_stkDir = "/usr/share/stk/rawwaves/";
		}
#endif
	}
#endif // LMMS_HAVE_STK

	upgrade();

	QStringList searchPaths;
	if (std::getenv("LMMS_THEME_PATH"))
		searchPaths << std::getenv("LMMS_THEME_PATH");
	searchPaths << themeDir() << defaultThemeDir();
	QDir::setSearchPaths("resources", searchPaths);

	// Create any missing subdirectories in the working dir, but only if the working dir exists
	if(hasWorkingDir())
	{
		createWorkingDir();
	}

	for (auto& file : m_recentlyOpenedProjects)
	{
		file = PathUtil::toAbsolute(file);
	}

	for (auto& file : m_favoriteItems)
	{
		file = PathUtil::toAbsolute(file);
	}
}




void ConfigManager::saveConfigFile()
{
	setValue("paths", "theme", m_themeDir);
	setValue("paths", "workingdir", m_workingDir);
	setValue("paths", "vstdir", m_vstDir);
	setValue("paths", "gigdir", m_gigDir);
	setValue("paths", "sf2dir", m_sf2Dir);
	setValue("paths", "ladspadir", m_ladspaDir);
#ifdef LMMS_HAVE_STK
	setValue("paths", "stkdir", m_stkDir);
#endif
#ifdef LMMS_HAVE_FLUIDSYNTH
	setValue("paths", "defaultsf2", m_sf2File);
#endif
	setValue("paths", "backgroundtheme", m_backgroundPicFile);

	QDomDocument doc("zene-config-file");

	QDomElement zene_config = doc.createElement("zene");
	zene_config.setAttribute("version", m_version);
	zene_config.setAttribute("configversion", m_configVersion);
	doc.appendChild(zene_config);

	for (auto it = m_settings.begin(); it != m_settings.end(); ++it)
	{
		QDomElement n = doc.createElement(it.key());
		for (const auto& [first, second] : *it)
		{
			n.setAttribute(first, second);
		}
		zene_config.appendChild(n);
	}

	QDomElement recent_files = doc.createElement("recentfiles");

	for (const auto& recentlyOpenedProject : m_recentlyOpenedProjects)
	{
		QDomElement n = doc.createElement("file");
		n.setAttribute("path", PathUtil::toShortestRelative(recentlyOpenedProject));
		recent_files.appendChild(n);
	}
	zene_config.appendChild(recent_files);

	QDomElement favorite_items = doc.createElement("favoriteitems");

	for (const auto& favoriteItem : m_favoriteItems)
	{
		QDomElement n = doc.createElement("item");
		n.setAttribute("path", PathUtil::toShortestRelative(favoriteItem));
		favorite_items.appendChild(n);
	}

	zene_config.appendChild(favorite_items);

	QString xml = "<?xml version=\"1.0\"?>\n" + doc.toString(2);

	QFile outfile(m_configFile);
	if(!outfile.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		using gui::MainWindow;

		QString title, message;
		title = MainWindow::tr("Could not open file");
		message = MainWindow::tr("Could not open file %1 "
					"for writing.\nPlease make "
					"sure you have write "
					"permission to the file and "
					"the directory containing the "
					"file and try again!"
						).arg(m_configFile);
		if (gui::getGUI() != nullptr)
		{
			QMessageBox::critical(nullptr, title, message,
						QMessageBox::Ok,
						QMessageBox::NoButton);
		}
		return;
	}

	outfile.write(xml.toUtf8());
	outfile.close();
}

void ConfigManager::initPortableWorkingDir()
{
	QString applicationPath = qApp->applicationDirPath();
	// A portable install (a `portable_mode.txt` marker beside the executable)
	// keeps its state in the application directory.  The pre-rename workspace
	// directory and config file are adopted here too, otherwise upgrading a
	// portable install on a memory stick would look like a fresh install.
	m_workingDir = applicationPath + "/zene-workspace/";
	m_workingDir = ConfigMigration::adoptWorkingDir( m_workingDir,
			applicationPath + "/lmms-workspace/" );
	m_configFile = ConfigMigration::adoptConfigFile(
			applicationPath + "/.zenestudio.xml",
			applicationPath + "/.lmmsrc.xml" );
}

void ConfigManager::initInstalledWorkingDir()
{
	const QString home = QDir::home().absolutePath();
	const QString docs =
		QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

	m_workingDir = docs + "/Zene Studio/";
	m_configFile = ConfigMigration::adoptConfigFile(
			home + "/.zenestudio.xml", home + "/.lmmsrc.xml" );

	// Precedence is unchanged from before the rename: the pre-1.2.0 layout
	// directly under $HOME wins when it holds projects, otherwise the Documents
	// location.  Either way the legacy directory is adopted -- renamed into the
	// new name where that is possible, and otherwise used where it already is,
	// so the user's projects stay found.
	if ( QFileInfo( home + "/lmms/projects/" ).exists() )
	{
		m_workingDir = ConfigMigration::adoptWorkingDir(
				home + "/zene/", home + "/lmms/" );
	}
	else
	{
		m_workingDir = ConfigMigration::adoptWorkingDir(
				docs + "/Zene Studio/", docs + "/lmms/" );
	}
}

void ConfigManager::initDevelopmentWorkingDir()
{
	// If we're in development (lmms is not installed) let's get the source and
	// binary directories by reading the CMake Cache
	QDir appPath = qApp->applicationDirPath();
	// If in tests, get parent directory
	if (appPath.dirName() == "tests") {
		appPath.cdUp();
	}
	QFile cmakeCache(appPath.absoluteFilePath("CMakeCache.txt"));
	if (cmakeCache.exists()) {
		cmakeCache.open(QFile::ReadOnly);
		QTextStream stream(&cmakeCache);

		// Find the lines containing something like zene_SOURCE_DIR:static=<dir>
		// and zene_BINARY_DIR:static=<dir>. The wave-R rename changed the CMake project
		// name and therefore these cache-entry names; the old lmms_ spelling is still
		// accepted so a build directory configured before the rename keeps working.
		int done = 0;
		while(! stream.atEnd())
		{
			QString line = stream.readLine();

			if (line.startsWith("zene_SOURCE_DIR:") || line.startsWith("lmms_SOURCE_DIR:")) {
				QString srcDir = line.section('=', -1).trimmed();
				QDir::addSearchPath("data", srcDir + "/data/");
				done++;
			}
			if (line.startsWith("zene_BINARY_DIR:") || line.startsWith("lmms_BINARY_DIR:")) {
				const QString buildDir = line.section('=', -1).trimmed();
				// A build tree configured before the rename keeps its settings.
				m_configFile = ConfigMigration::adoptConfigFile(
						buildDir + QDir::separator() + ".zenestudio.xml",
						buildDir + QDir::separator() + ".lmmsrc.xml" );
				done++;
			}
			if (done == 2)
			{
				break;
			}
		}

		cmakeCache.close();
	}
}


QString ConfigMigration::adoptConfigFile( const QString & newFile, const QString & legacyFile )
{
	if( newFile.isEmpty() || QFileInfo::exists( newFile ) )
	{
		// The new location either already holds the state or was not named.
		return newFile;
	}
	if( legacyFile.isEmpty() || !QFileInfo::exists( legacyFile ) )
	{
		// Nothing to adopt: a fresh install.
		return newFile;
	}

	// Rename, so the user keeps their settings AND the old name goes away.
	if( QFile::rename( legacyFile, newFile ) )
	{
		return newFile;
	}

	// Cross-device or permission failure: copy instead of rename, and read the
	// new file.  The legacy file is what the user actually had, so it is left
	// where it is rather than being deleted.
	if( QFile::copy( legacyFile, newFile ) )
	{
		return newFile;
	}

	// Neither possible: keep reading the file that holds the user's settings.
	// Orphaning them is the one outcome that is not acceptable.
	return legacyFile;
}


QString ConfigMigration::adoptWorkingDir( const QString & newDir, const QString & legacyDir )
{
	if( newDir.isEmpty() || QDir( newDir ).exists() )
	{
		// The new location wins whenever it exists; when both exist the user is
		// responsible for which one holds what, so the legacy directory is left
		// strictly alone.
		return newDir;
	}
	if( legacyDir.isEmpty() || !QDir( legacyDir ).exists() )
	{
		return newDir;
	}

	const QFileInfo newInfo( QDir::cleanPath( newDir ) );
	const QFileInfo legacyInfo( QDir::cleanPath( legacyDir ) );

	// Moving the directory is only attempted when both live under the same
	// parent -- which is the normal case and makes it a cheap atomic rename on
	// one filesystem instead of a copy that could fail halfway through.
	if( newInfo.absolutePath() == legacyInfo.absolutePath() )
	{
		QDir parent( newInfo.absolutePath() );
		if( parent.rename( legacyInfo.fileName(), newInfo.fileName() ) )
		{
			return newDir;
		}
	}

	// Could not move it.  The legacy directory holds the user's projects, so the
	// right failure mode is to keep using it where it is -- not to point the
	// product at an empty new folder and have every project appear to vanish.
	return legacyDir;
}

// If configversion is not present, we will convert the LMMS version to the appropriate
// configuration file version for backwards compatibility.
unsigned int ConfigManager::legacyConfigVersion()
{
	ProjectVersion createdWith = m_version;

	createdWith.setCompareType(ProjectVersion::CompareType::Build);

	if( createdWith < "1.1.90" )
	{
		return 0;
	}
	else if( createdWith < "1.1.91" )
	{
		return 1;
	}
	else
	{
		return 2;
	}
}


} // namespace lmms
