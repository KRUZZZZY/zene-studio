/*
 * EngineServe.cpp - the engine as a process of its own (`zene --engine`)
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

#include "EngineServe.h"

#include <array>
#include <cstdio>

#include <QCoreApplication>
#include <QSocketNotifier>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "CrashReporter.h"
#include "Engine.h"
#include "ImportFilter.h"
#include "ProjectRecovery.h"
#include "SafeStart.h"
#include "Song.h"
#include "lmmsconfig.h"

#ifndef LMMS_BUILD_WIN32
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace lmms::engineserve
{

namespace
{

//! The prompt the application asks ("create the working directory?") has nobody to answer it here, so
//! it takes the prompt's own default - yes - and says so, exactly as an unattended application start
//! does (GuiApplication's constructor, task #625).
void ensureWorkingDir()
{
	ConfigManager* config = ConfigManager::inst();
	if (config->hasWorkingDir()) { return; }
	const QString workingDir = config->workingDir();
	config->createWorkingDir();
	if (config->hasWorkingDir())
	{
		std::fprintf(stderr, "engine: working-directory path=%s state=created\n", qUtf8Printable(workingDir));
	}
	else
	{
		std::fprintf(stderr, "engine: working-directory-error path=%s reason=create-failed\n",
			qUtf8Printable(workingDir));
	}
	std::fflush(stderr);
}

void followProjectPath()
{
	const std::string path = Engine::getSong()->projectFileName().toStdString();
	crashreporter::setProjectPath(path);
	safestart::setProjectPath(path);
}

//! A recovery file that belongs to this launch is recovered without a prompt - the answer an
//! unattended application start gives (main(), task #625) - so the two ways of starting the same
//! engine cannot disagree about what a crash left behind. Returns the file to load, or empty.
QString recoveryToLoad(const QString& fileToLoad)
{
	const QString recoveryFile = ConfigManager::inst()->recoveryFile();
	const ProjectRecovery::RecoveryInfo info = ProjectRecovery::readRecoveryInfo(recoveryFile);
	const ProjectRecovery::RecoveryDecision decision = ProjectRecovery::decideRecovery(info, fileToLoad);
	if (decision.offered())
	{
		std::fprintf(stderr, "engine: recovering %s without a prompt (the prompt's default answer)\n",
			qUtf8Printable(recoveryFile));
		return recoveryFile;
	}
	if (info.fileExists)
	{
		std::fprintf(stderr, "engine: not offering recovery file %s: %s\n", qUtf8Printable(recoveryFile),
			qUtf8Printable(decision.detail));
	}
	return QString();
}

void loadInitialProject(const StartOptions& options)
{
	Song* song = Engine::getSong();
	const QString recovery = recoveryToLoad(options.fileToLoad);
	if (!recovery.isEmpty()) { song->createNewProjectFromTemplate(recovery); }
	else if (!options.fileToLoad.isEmpty()) { song->loadProject(options.fileToLoad); }
	else if (!options.fileToImport.isEmpty()) { ImportFilter::import(options.fileToImport, song); }
	else { song->createNewProject(); }

	followProjectPath();
	QObject::connect(song, &Song::projectFileNameChanged, song, [] { followProjectPath(); });
}

//! control.quit {save:true} is answered by MainWindow::closeEvent in the application. There is no
//! window here, so the save it promised is done as the event loop winds down. A failed save is said
//! on stderr: the reply to control.quit has already gone, and a silent loss is the worst outcome.
void saveOnQuit()
{
	Song* song = Engine::getSong();
	if (song == nullptr) { return; }
	const bool saveAnswer = ControlRegistry::quitPromptAnswer() == ControlRegistry::QuitPromptAnswer::Save;
	if (!quitShouldSave(ControlRegistry::quitPending(), saveAnswer, !song->projectFileName().isEmpty())) { return; }
	if (!song->guiSaveProject())
	{
		std::fprintf(stderr, "engine: control.quit save:true could NOT save %s\n",
			qUtf8Printable(song->projectFileName()));
		std::fflush(stderr);
	}
}

#ifndef LMMS_BUILD_WIN32
std::array<int, 2> s_signalFd = {-1, -1};

//! Async-signal-safe: one byte down a socket pair, read on the main thread by a QSocketNotifier.
void onTerminationSignal(int)
{
	const char byte = 1;
	const auto ignored = ::write(s_signalFd[0], &byte, sizeof(byte));
	static_cast<void>(ignored);
}

//! SIGTERM and SIGINT end a served engine the way control.quit {save:false} does: the normal quit,
//! with its bounded guard. A supervisor (a front end, systemd, a terminal's Ctrl+C) needs no socket
//! client to stop the process cleanly.
void installTerminationSignals(QCoreApplication* app)
{
	if (::socketpair(AF_UNIX, SOCK_STREAM, 0, s_signalFd.data()) != 0)
	{
		std::fprintf(stderr, "engine: could not create the signal socket pair; SIGTERM will not be clean\n");
		return;
	}
	auto* notifier = new QSocketNotifier(s_signalFd[1], QSocketNotifier::Read, app);
	QObject::connect(notifier, &QSocketNotifier::activated, app, [notifier] {
		notifier->setEnabled(false);
		std::fprintf(stderr, "engine: termination signal, quitting\n");
		std::fflush(stderr);
		ControlRegistry::requestQuit(ControlRegistry::QuitPromptAnswer::Discard);
	});
	std::signal(SIGTERM, onTerminationSignal);
	std::signal(SIGINT, onTerminationSignal);
}
#else
void installTerminationSignals(QCoreApplication*) {}
#endif

} // namespace

QString startRefusal(const StartOptions& options)
{
	if (options.controlSocket.isEmpty())
	{
		return QStringLiteral("--engine needs --control-socket <path>: with no interface, the control "
			"surface is the only way to reach or stop the engine");
	}
	if (!options.fileToLoad.isEmpty() && !options.fileToImport.isEmpty())
	{
		return QStringLiteral("--engine takes a project or --import, not both");
	}
	return QString();
}

bool quitShouldSave(bool quitPending, bool saveAnswer, bool hasProjectFile)
{
	return quitPending && saveAnswer && hasProjectFile;
}

bool start(const StartOptions& options, QString* error)
{
	const QString refusal = startRefusal(options);
	if (!refusal.isEmpty())
	{
		if (error != nullptr) { *error = refusal; }
		return false;
	}
	QCoreApplication* app = QCoreApplication::instance();

	ensureWorkingDir();
	// The directory may have just been created: give the crash reporter and the safe-start marker the
	// same second chance to attach that the application start gives them.
	crashreporter::install(ConfigManager::inst()->workingDir().toStdString());
	crashreporter::beginSession();
	safestart::install(ConfigManager::inst()->workingDir().toStdString());
	safestart::beginSession();

	// Not render-only: a served engine opens the configured audio and MIDI devices, as the application does.
	Engine::init(false);
	loadInitialProject(options);

	QObject::connect(app, &QCoreApplication::aboutToQuit, app, [] { saveOnQuit(); });
	installTerminationSignals(app);

	std::fprintf(stderr, "engine: serving on %s with no interface (pid %lld)\n",
		qUtf8Printable(options.controlSocket), static_cast<long long>(QCoreApplication::applicationPid()));
	std::fflush(stderr);
	return true;
}

} // namespace lmms::engineserve
