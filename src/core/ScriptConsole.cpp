/*
 * ScriptConsole.cpp - script console output for the LMMS/Zene Lua API
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

#include "ScriptConsole.h"

#include <QDebug>
#include <QMutexLocker>

#include <atomic>

#include "ScriptEngine.h"

namespace lmms
{
namespace ScriptConsole
{

namespace
{

//! Console routing state. A relaxed atomic, not a lock: the flag is a
//! diagnostic switch, never a synchronisation point, and a reader on any thread
//! (including a future one on the audio side) must not be able to block.
std::atomic<bool> s_enabled{true};
constexpr qsizetype CaptureCharacterLimit = 1024 * 1024;
constexpr qsizetype CaptureLineLimit = 4096;
const QString s_captureLimitNotice =
	QStringLiteral("[script output truncated: further lines dropped]");

} // namespace

void CaptureBuffer::reset()
{
	m_characters = 0;
	m_lines = 0;
	m_limitReached = false;
}

bool CaptureBuffer::append(QStringList& messages, const QString& line)
{
	if (m_limitReached)
	{
		return false;
	}
	const qsizetype availableCharacters = CaptureCharacterLimit - m_characters
		- s_captureLimitNotice.size();
	const qsizetype availableLines = CaptureLineLimit - m_lines - 1;
	if (availableLines > 0 && line.size() <= availableCharacters)
	{
		messages.append(line);
		m_characters += line.size();
		++m_lines;
		return false;
	}
	m_limitReached = true;
	messages.append(s_captureLimitNotice);
	m_characters += s_captureLimitNotice.size();
	++m_lines;
	return true;
}

const QString& CaptureBuffer::truncationNotice()
{
	return s_captureLimitNotice;
}

void setEnabled(bool enabled)
{
	s_enabled.store(enabled, std::memory_order_relaxed);
}

bool enabled()
{
	return s_enabled.load(std::memory_order_relaxed);
}

void streamLine(const QString& line)
{
	if (line.isEmpty() || !enabled())
	{
		return;
	}
	// Same shape as the loop MainWindow::runScript used to print, so the output
	// a user sees does not change when the printing moves in here.
	qInfo().noquote() << QStringLiteral("lua:") << line;
}

} // namespace ScriptConsole

void ScriptEngine::logMessage(const QString& message)
{
	bool reportCaptureLimit = false;
	{
		QMutexLocker locker(&m_stateMutex);
		reportCaptureLimit = m_logCapture.append(m_logMessages, message);
	}
	emit logged(message);
	// The console is the part a script author sees while debugging: without it
	// a print() only lands in the capture buffer above, which nothing renders.
	ScriptConsole::streamLine(message);
	if (reportCaptureLimit)
	{
		const QString& notice = ScriptConsole::CaptureBuffer::truncationNotice();
		emit logged(notice);
		ScriptConsole::streamLine(notice);
	}
}

} // namespace lmms
