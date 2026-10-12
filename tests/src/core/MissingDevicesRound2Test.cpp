/*
 * MissingDevicesRound2Test.cpp - Q050-018 review follow-up for every mixer chain and the GUI project-open notice
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

#include <QCoreApplication>

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QTextStream>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "MissingDevices.h"
#include "Mixer.h"
#include "Plugin.h"
#include "PluginFactory.h"
#include "PluginLoadPolicy.h"
#include "PluginScanCache.h"
#include "Song.h"
#include "../gui/GuiTestApplication.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QString readAll(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

QString canon(const QDomNode& node)
{
	QString out;
	QTextStream stream(&out);
	node.save(stream, 0);
	stream.flush();
	return out;
}

//! The same pretty-printed form canon() gives a parsed element, so a literal XML string compares with saved XML.
QString canonText(const QString& xml)
{
	QDomDocument doc;
	doc.setContent(xml);
	return canon(doc.documentElement());
}

QString stripIds(QString text)
{
	static const QRegularExpression idPattern(" id=\"[0-9]+\"");
	return text.remove(idPattern);
}

QDomDocument parse(const QString& path)
{
	QDomDocument doc;
	doc.setContent(readAll(path));
	return doc;
}

QDomElement effectOf(const QDomDocument& doc)
{
	const QDomNodeList effects = doc.elementsByTagName(QStringLiteral("effect"));
	return effects.isEmpty() ? QDomElement() : effects.item(0).toElement();
}

QDomElement masterMixerChannel(const QDomDocument& doc)
{
	const QDomNodeList channels = doc.elementsByTagName(QStringLiteral("mixerchannel"));
	for (int i = 0; i < channels.size(); ++i)
	{
		const QDomElement channel = channels.item(i).toElement();
		if (channel.attribute(QStringLiteral("num")) == QLatin1String("0")) { return channel; }
	}
	return {};
}

QDomElement rackEffect(const QDomDocument& doc)
{
	const QDomElement channel = masterMixerChannel(doc);
	const QDomElement chain = channel.firstChildElement(QStringLiteral("rack")).firstChildElement(QStringLiteral("chain"));
	return chain.firstChildElement(QStringLiteral("fxchain")).firstChildElement(QStringLiteral("effect"));
}

QDomElement postInsertEffect(const QDomDocument& doc)
{
	const QDomElement channel = masterMixerChannel(doc);
	return channel.firstChildElement(QStringLiteral("postinserts")).firstChildElement(QStringLiteral("fxchain"))
		.firstChildElement(QStringLiteral("effect"));
}

QDomElement makeFxChain(QDomDocument& doc, const QDomElement& effect)
{
	QDomElement chain = doc.createElement(QStringLiteral("fxchain"));
	chain.setAttribute(QStringLiteral("numofeffects"), 1);
	chain.setAttribute(QStringLiteral("enabled"), 1);
	chain.appendChild(doc.importNode(effect, true));
	return chain;
}

} // namespace

class MissingDevicesRound2Test : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_home;
	QTemporaryDir m_dir;
	QTemporaryDir m_full;
	QTemporaryDir m_missingEntry;
	QString m_original;
	QString m_amplifierXml;
	QString m_missingEffectXml;

	void pin(const QTemporaryDir& dir)
	{
		QDir::setSearchPaths(QStringLiteral("plugins"), QStringList{dir.path()});
		getPluginFactory()->discoverPlugins();
	}

	void clearNotices()
	{
		if (getGUI() == nullptr || getGUI()->mainWindow() == nullptr) { return; }
		MainWindow* window = getGUI()->mainWindow();
		for (QMessageBox* box : window->findChildren<QMessageBox*>()) { delete box; }
	}

	static QDomElement masterEffectOf(const QDomDocument& doc)
	{
		return masterMixerChannel(doc).firstChildElement(QStringLiteral("fxchain")).firstChildElement(QStringLiteral("effect"));
	}

	//! The saved project with the master channel's effect replaced by a module that is found but has no entry point,
	//! and the same amplifier placed on a parallel rack chain and in the post-fader inserts.
	void writeReviewProject(const QString& path)
	{
		QDomDocument doc = parse(m_original);
		QDomElement baseEffect = effectOf(doc);
		QVERIFY(!baseEffect.isNull());
		QDomElement mixerChannel = masterMixerChannel(doc);
		QVERIFY(!mixerChannel.isNull());

		QDomElement rack = doc.createElement(QStringLiteral("rack"));
		rack.setAttribute(QStringLiteral("version"), 2);
		rack.setAttribute(QStringLiteral("selected"), -1);
		QDomElement rackChain = doc.createElement(QStringLiteral("chain"));
		rackChain.setAttribute(QStringLiteral("index"), 1);
		rackChain.appendChild(makeFxChain(doc, baseEffect));
		rack.appendChild(rackChain);
		mixerChannel.appendChild(rack);

		QDomElement postInserts = doc.createElement(QStringLiteral("postinserts"));
		postInserts.appendChild(makeFxChain(doc, baseEffect));
		mixerChannel.appendChild(postInserts);

		QDomDocument fragment;
		QVERIFY(fragment.setContent(m_missingEffectXml));
		baseEffect.parentNode().replaceChild(doc.importNode(fragment.documentElement(), true), baseEffect);

		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
		const QByteArray bytes = doc.toString().toUtf8();
		QCOMPARE(file.write(bytes), qint64(bytes.size()));
	}

	void addNoEntryScanDescriptor()
	{
		const QString modulePath = QStringLiteral(LMMS_TEST_NO_ENTRY_MODULE);
		const QFileInfo source(modulePath);
		const QString copyPath = m_missingEntry.filePath(source.fileName());
		QVERIFY(QFile::copy(source.absoluteFilePath(), copyPath));
		pin(m_missingEntry);  // The module is loadable, but the scan correctly sees no LMMS descriptor.

		const QFileInfo copied(copyPath);
		PluginScanRecord record;
		record.filePath = copied.absoluteFilePath();
		record.size = copied.size();
		record.mtimeMs = copied.lastModified().toMSecsSinceEpoch();
		record.status = PluginScanRecord::Status::HasDescriptor;
		record.name = QStringLiteral("missingentry");
		record.displayName = QStringLiteral("Missing Entry Test Module");
		record.description = QStringLiteral("Test module that has no lmms_plugin_main");
		record.author = QStringLiteral("Zene Studio test");
		record.type = static_cast<int>(Plugin::Type::Instrument);
		record.hasSubPluginFeatures = false;
		getPluginFactory()->scanCache().store(record);
		QVERIFY(getPluginFactory()->scanCache().save());
		getPluginFactory()->discoverPlugins();  // Serves the descriptor without loading the module yet.
		QVERIFY2(!getPluginFactory()->pluginInfo("missingentry").isNull(),
			"the test cache must make this discovered module available by its saved descriptor");
	}

private slots:
	//! A discovered-but-broken module follows the same no-per-device-dialog rule while the project is loading.
	void projectLoadFailureUsesTheSingleNoticePolicy()
	{
		QVERIFY(pluginload::shouldShowFailureDialog(true, false, false));
		QVERIFY(!pluginload::shouldShowFailureDialog(true, false, true));
		QVERIFY(!pluginload::shouldShowFailureDialog(true, true, false));
	}

	void initTestCase()
	{
		QVERIFY(m_home.isValid() && m_dir.isValid() && m_full.isValid() && m_missingEntry.isValid());
		qputenv("LMMS_PLUGIN_SCAN_CACHE", m_home.filePath(QStringLiteral("plugin-scan-cache.json")).toLocal8Bit());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr && getGUI()->mainWindow() != nullptr);
		getGUI()->mainWindow()->show();
		ControlRegistry::setReady(true);

		const QString source = QStringLiteral(LMMS_TEST_PLUGIN_DIR);
		const QString amplifier = QStringLiteral(LMMS_TEST_MODULE_AMPLIFIER);
		QVERIFY2(QFile::copy(source + QLatin1Char('/') + amplifier, m_full.filePath(amplifier)), qPrintable(amplifier));
		pin(m_full);

		// The master channel's own chain holds the real amplifier, so the saved project has a real <effect> to copy.
		MixerChannel* master = Engine::mixer()->mixerChannel(0);
		Effect* amplifierEffect = Effect::instantiate(QStringLiteral("amplifier"), &master->m_fxChain, nullptr);
		QVERIFY(amplifierEffect != nullptr);
		master->m_fxChain.appendEffect(amplifierEffect);

		m_original = m_dir.filePath(QStringLiteral("original.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(m_original));
		m_amplifierXml = canon(effectOf(parse(m_original)));
		QVERIFY(!m_amplifierXml.isEmpty());
		m_missingEffectXml = QStringLiteral(
			"<effect name=\"missingentry\"><missingentry><state>QUJD</state></missingentry></effect>");
		addNoEntryScanDescriptor();
	}

	void cleanupTestCase()
	{
		clearNotices();
		ControlRegistry::setReady(false);
		QDir::setSearchPaths(QStringLiteral("plugins"), QStringList{});
		// Deferred view deletions run while the song still exists, as in ArrangerKeysNavigatorTest.
		QCoreApplication::processEvents();
		if (Engine::getSong() != nullptr) { Engine::getSong()->clearProject(); }
		QCoreApplication::processEvents();
		Engine::destroy();
	}

	//! Criterion 2: collect rack and post-fader placeholders, keep their XML through save and reopen, and notify once
	//! through MainWindow's project-loaded connection with no modal for the found-but-entry-less module.
	void everyMixerChainRoundTripsAndOneProjectOpenNoticeListsAllPlaceholders()
	{
		const QString target = m_dir.filePath(QStringLiteral("round2.mmp"));
		writeReviewProject(target);
		pin(m_missingEntry);
		clearNotices();

		const ControlResult opened = ControlRegistry::instance()->invoke(QStringLiteral("project.open"),
			{{QStringLiteral("path"), target}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		QCOMPARE(opened.result.value(QStringLiteral("missing_device_count")).toInt(), 3);
		const QJsonArray devices = opened.result.value(QStringLiteral("missing_devices")).toArray();
		QCOMPARE(devices.size(), 3);

		QStringList locations;
		for (const QJsonValue& item : devices) { locations << item.toObject().value(QStringLiteral("track")).toString(); }
		QVERIFY(locations.contains(QStringLiteral("mixer channel 0")));
		QVERIFY(locations.contains(QStringLiteral("mixer channel 0 rack chain 1")));
		QVERIFY(locations.contains(QStringLiteral("mixer channel 0 post-fader inserts")));
		QCOMPARE(devices.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("missingentry"));

		MainWindow* window = getGUI()->mainWindow();
		QCOMPARE(window->findChildren<QMessageBox*>().size(), 1);
		QMessageBox* notice = window->findChildren<QMessageBox*>().first();
		QVERIFY(!notice->isModal());
		QVERIFY(notice->text().contains(QStringLiteral("missingentry")));
		QVERIFY(notice->text().contains(QStringLiteral("rack chain 1")));
		QVERIFY(notice->text().contains(QStringLiteral("post-fader inserts")));

		const QString firstSave = m_dir.filePath(QStringLiteral("first-save.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(firstSave));
		const QDomDocument first = parse(firstSave);
		QCOMPARE(stripIds(canon(masterEffectOf(first))), stripIds(canonText(m_missingEffectXml)));
		QCOMPARE(stripIds(canon(rackEffect(first))), stripIds(m_amplifierXml));
		QCOMPARE(stripIds(canon(postInsertEffect(first))), stripIds(m_amplifierXml));

		clearNotices();
		const ControlResult reopened = ControlRegistry::instance()->invoke(QStringLiteral("project.open"),
			{{QStringLiteral("path"), firstSave}});
		QVERIFY2(reopened.ok, qPrintable(reopened.errorMessage));
		QCOMPARE(reopened.result.value(QStringLiteral("missing_device_count")).toInt(), 3);
		QCOMPARE(window->findChildren<QMessageBox*>().size(), 1);

		const QString secondSave = m_dir.filePath(QStringLiteral("second-save.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(secondSave));
		const QDomDocument second = parse(secondSave);
		QCOMPARE(stripIds(canon(masterEffectOf(second))), stripIds(canonText(m_missingEffectXml)));
		QCOMPARE(stripIds(canon(rackEffect(second))), stripIds(m_amplifierXml));
		QCOMPARE(stripIds(canon(postInsertEffect(second))), stripIds(m_amplifierXml));
	}
};

QTEST_MAIN(MissingDevicesRound2Test)
#include "MissingDevicesRound2Test.moc"
