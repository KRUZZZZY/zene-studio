/*
 * PluginCompatEditorProbe.cpp - Q050-071: the in-process half of the third-party plug-in compatibility matrix
 *
 * `plugin.editor_open` needs a display and a socket-driven instance is always "headless" (UnattendedRun.h), so the
 * editor column of the matrix cannot be measured over the control socket. This probe runs the real application
 * (GuiApplication over a throwaway home) on whatever display the caller gives it, loads ONE third-party plug-in by
 * format and name through the same registered commands the socket takes (plugin.paths_set, plugin.rescan, plugin.list,
 * track.add, plugin.load), asks it for its own editor (plugin.editor_open / plugin.editor_close) and prints the outcome
 * as one line, `COMPAT_PROBE_RESULT {json}`, for tests/plugin_compat_matrix.py to collect.
 *
 * It is registered as a test so the build covers it, but it only does anything when ZENE_COMPAT_PROBE names a subject
 * (a JSON object: format, name, kind, vst3_dirs, clap_dirs); without it the case skips. It is NOT part of the fixture
 * suite and proves nothing about a plug-in until the matrix driver runs it against a pinned third-party binary.
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdio>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"

using namespace lmms;

namespace
{

QJsonObject outcome(const QString& state, const QString& detail)
{
	return {{QStringLiteral("outcome"), state}, {QStringLiteral("detail"), detail}};
}

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! The dev-<n> of the device called \a name in \a format, empty when the host catalogues none.
QString findDevice(const QString& format, const QString& name)
{
	const QJsonArray devices = run(QStringLiteral("plugin.list"), {{QStringLiteral("format"), format}}).result.value(QStringLiteral("devices")).toArray();
	for (const QJsonValue& value : devices)
	{
		if (value.toObject().value(QStringLiteral("name")).toString() == name) { return value.toObject().value(QStringLiteral("id")).toString(); }
	}
	return {};
}

//! The rescan of a real instance runs as a background task, so the catalogue fills in a moment later: look for up to a minute.
QString waitForDevice(const QString& format, const QString& name)
{
	for (int i = 0; i < 120; ++i)
	{
		const QString device = findDevice(format, name);
		if (!device.isEmpty()) { return device; }
		QTest::qWait(500);
	}
	return {};
}

//! What a refused editor open means: the host does not support this plug-in's editor (unsupported), or it failed (fail).
QJsonObject classifyRefusal(const ControlResult& opened)
{
	const QString message = opened.errorMessage;
	const bool declined = opened.errorKind == ControlErrorKind::Refused || opened.errorKind == ControlErrorKind::Requires;
	return outcome(declined ? QStringLiteral("unsupported") : QStringLiteral("fail"), message);
}

QJsonObject openEditor(const QString& track, const QString& plugin)
{
	const ControlResult opened = run(QStringLiteral("plugin.editor_open"), {{QStringLiteral("target"), track}, {QStringLiteral("plugin"), plugin}});
	if (!opened.ok) { return classifyRefusal(opened); }
	for (int i = 0; i < 20; ++i) { QTest::qWait(50); }  // let the editor map its window and draw
	const ControlResult closed = run(QStringLiteral("plugin.editor_close"), {{QStringLiteral("target"), track}, {QStringLiteral("plugin"), plugin}});
	const bool wasOpen = closed.ok && closed.result.value(QStringLiteral("was_open")).toBool();
	return wasOpen ? outcome(QStringLiteral("pass"), QStringLiteral("opened, drew for one second, closed"))
		: outcome(QStringLiteral("fail"), QStringLiteral("plugin.editor_open answered ok but the editor was not open afterwards"));
}

} // namespace

class PluginCompatEditorProbe : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_home;

	//! The real application over a throwaway home, like guitest::startGui, with the first-run setup already answered:
	//! on a real display the main window asks it as a modal dialog, which nobody is there to close.
	void startGui()
	{
		for (const char* name : {"HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"}) { qputenv(name, m_home.path().toLocal8Bit()); }
		ConfigManager* config = ConfigManager::inst();
		config->loadConfigFile(m_home.filePath(QStringLiteral("zene.xml")));
		config->setValue(QStringLiteral("app"), QStringLiteral("configured"), QStringLiteral("1"));
		config->setValue(QStringLiteral("audioengine"), QStringLiteral("audiodev"), QStringLiteral("Dummy (no sound output)"));
		config->setWorkingDir(m_home.filePath(QStringLiteral("work")));
		config->createWorkingDir();
		new gui::GuiApplication();
	}

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		if (qEnvironmentVariableIsEmpty("ZENE_COMPAT_PROBE")) { return; }
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);  // the build's own hosting modules (vst3effect, clapeffect, lv2effect...)
#endif
		startGui();
		QVERIFY(gui::getGUI() != nullptr);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		if (qEnvironmentVariableIsEmpty("ZENE_COMPAT_PROBE")) { return; }
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void probe()
	{
		if (qEnvironmentVariableIsEmpty("ZENE_COMPAT_PROBE")) { QSKIP("ZENE_COMPAT_PROBE names no subject: nothing to probe"); }
		const QJsonObject subject = QJsonDocument::fromJson(qgetenv("ZENE_COMPAT_PROBE")).object();
		const QString format = subject.value(QStringLiteral("format")).toString();
		QJsonObject pathsSet;
		for (const QString& key : {QStringLiteral("vst3"), QStringLiteral("clap")})
		{
			const QJsonArray dirs = subject.value(key + QStringLiteral("_dirs")).toArray();
			if (dirs.isEmpty()) { continue; }
			const ControlResult set = run(QStringLiteral("plugin.paths_set"), {{QStringLiteral("format"), key}, {QStringLiteral("dirs"), dirs}});
			pathsSet.insert(key, set.ok ? QJsonValue(set.result) : QJsonValue(set.errorMessage));
		}
		const ControlResult rescanned = run(QStringLiteral("plugin.rescan"));

		QJsonObject result{{QStringLiteral("subject"), subject.value(QStringLiteral("id"))}};
		const QString device = waitForDevice(format, subject.value(QStringLiteral("name")).toString());
		result.insert(QStringLiteral("listed"), !device.isEmpty());
		result.insert(QStringLiteral("paths_set"), pathsSet);
		result.insert(QStringLiteral("rescan"), rescanned.ok ? QJsonValue(rescanned.result) : QJsonValue(rescanned.errorMessage));
		if (device.isEmpty())
		{
			result.insert(QStringLiteral("editor"), outcome(QStringLiteral("not_run"), QStringLiteral("the host does not list this plug-in")));
		}
		else
		{
			const QString track = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")}}).result.value(QStringLiteral("track")).toString();
			const ControlResult loaded = run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), track}, {QStringLiteral("device"), device}});
			result.insert(QStringLiteral("editor"), loaded.ok ? openEditor(track, loaded.result.value(QStringLiteral("id")).toString())
				: outcome(QStringLiteral("not_run"), QStringLiteral("plugin.load refused: ") + loaded.errorMessage));
		}
		std::printf("COMPAT_PROBE_RESULT %s\n", QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
		std::fflush(stdout);
	}
};

QTEST_MAIN(PluginCompatEditorProbe)
#include "PluginCompatEditorProbe.moc"
