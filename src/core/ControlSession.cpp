
/*
 * ControlSession.cpp - the control surface's readiness report and shutdown
 *                      intent (SPEC A12, task #626).
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

// Split out of ControlRegistry.cpp: the registry is the command table, this file
// is the session state a client reads before it may use that table (why the
// engine is not addressable yet) and the shutdown intent it sets to leave.

#include "ControlRegistry.h"

#include <cstdio>
#include <cstdlib>

#include <QCoreApplication>
#include <QTimer>

#include "AudioEngine.h"
#include "Engine.h"
#include "Mixer.h"
#include "Song.h"

namespace lmms
{

ControlRegistry::QuitPromptAnswer ControlRegistry::s_quitAnswer = ControlRegistry::QuitPromptAnswer::Ask;
bool ControlRegistry::s_quitPending = false;
QTimer* ControlRegistry::s_quitGuard = nullptr;

ControlRegistry::ReadinessReport ControlRegistry::readinessReport()
{
	ReadinessReport report;

	AudioEngine* audio = Engine::audioEngine();
	if (audio != nullptr)
	{
		report.audioStarted = !audio->audioDevStartFailed();
		report.actualDevice = audio->audioDevName();
		report.requestedDevice = audio->audioDevRequestName();
	}

	report.ready = isEngineReady();
	if (report.ready) { return report; }

	// Not addressable: say why, in a closed-set code plus something a client can
	// act on. "false" on its own is what made task #626 expensive.
	if (!s_ready)
	{
		report.code = QStringLiteral("engine_starting");
		report.message = QStringLiteral(
			"the engine is still starting up; poll control.ping until engine_ready is true "
			"before issuing engine commands");
		return report;
	}
	if (Engine::getSong() == nullptr || Engine::mixer() == nullptr)
	{
		report.code = QStringLiteral("engine_missing");
		report.message = QStringLiteral(
			"startup finished without a usable engine; this instance cannot run engine commands");
		return report;
	}
	report.code = QStringLiteral("audio_device_failed");
	report.message = QStringLiteral(
		"the configured audio device '%1' could not be opened, so the engine fell back to '%2' "
		"and is not usable for playback (%3)")
		.arg(report.requestedDevice, report.actualDevice,
			audio != nullptr ? audio->audioDevStartReason() : QString());
	return report;
}

void ControlRegistry::requestQuit(QuitPromptAnswer answer)
{
	s_quitAnswer = answer;
	s_quitPending = true;
	// Ask the application to quit at once *and* leave the request pending: if the
	// instance is still starting up, the loop the quit lands in is not the main
	// one and the request would be lost, so main() re-applies it once the engine
	// is ready (applyPendingQuit). Doing both is what makes "connect and quit
	// immediately" work no matter where startup has got to.
	scheduleQuit();
}

bool ControlRegistry::quitPending()
{
	return s_quitPending;
}

ControlRegistry::QuitPromptAnswer ControlRegistry::quitPromptAnswer()
{
	return s_quitAnswer;
}

void ControlRegistry::setQuitPromptAnswer(QuitPromptAnswer answer)
{
	s_quitAnswer = answer;
}

bool ControlRegistry::applyPendingQuit()
{
	if (!s_quitPending) { return false; }
	s_quitPending = false;
	scheduleQuit();
	return true;
}

void ControlRegistry::scheduleQuit()
{
	QCoreApplication* app = QCoreApplication::instance();
	if (app == nullptr) { return; }

	// The NORMAL application quit: QGuiApplication's termination closes the main
	// window, MainWindow::closeEvent accepts (its prompt is answered from
	// quitPromptAnswer(), not by a dialog), app->exec() returns and main()
	// destroys the engine and unlinks the control socket. This is the whole fix
	// for the two #626 reproductions; both of their stacks are in the lane
	// report.
	QTimer::singleShot(0, app, &QCoreApplication::quit);

	// Absolute last resort. The shutdown path walks Qt widget teardown, plugin
	// destructors, the audio thread and the autosaver - code this process does
	// not own and cannot enumerate. If one of those ever blocks in a loop a human
	// would have to answer, an agent would otherwise wait forever on a socket
	// nobody will close. Bounded, loud, and FAILURE: a normal shutdown never
	// reaches this, and a forced one must not look like a clean one. main()
	// cancels it as soon as the event loop returns, so a slow but healthy
	// teardown is never punished.
	if (s_quitGuard != nullptr)
	{
		s_quitGuard->stop();
		s_quitGuard->deleteLater();
	}
	s_quitGuard = new QTimer(app);
	s_quitGuard->setSingleShot(true);
	QObject::connect(s_quitGuard, &QTimer::timeout, app, []() {
		std::fprintf(stderr,
			"control.quit: FATAL: the normal shutdown did not finish within %d ms; a dialog or "
			"teardown path is blocking it (this is a bug in the shutdown path, not in the client). "
			"Forcing an exit with the failure status.\n", ShutdownGuardMs);
		std::fflush(stderr);
		ControlRegistry::instance()->runShutdownHooks();
		std::_Exit(EXIT_FAILURE);
	});
	s_quitGuard->start(ShutdownGuardMs);
}

void ControlRegistry::cancelShutdownGuard()
{
	if (s_quitGuard == nullptr) { return; }
	s_quitGuard->stop();
	s_quitGuard->deleteLater();
	s_quitGuard = nullptr;
}

} // namespace lmms
