/*
 * AccessibleAnnouncements.cpp - which state transitions a screen reader is told about, and how often
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

// 0.5.0 UX decision, theme 17: the announcement policy. The research's adjustment - define the debouncing, the error
// handling and the user's verbosity before choosing what is spoken - is this file:
// - verbosity: config ui/announcements {level}: "all" (the default), "errors" (assertive ones only) or "off";
// - debouncing: the same message twice within RepeatMs is spoken once (a status float and a refusal that say the
//   same thing, a transport toggled twice);
// - what is spoken by default: errors and refused edits (assertive), recording starting and stopping, a project saved
//   or opened, a long job finished (polite). Values are spoken only for the focused control, by AccessibleWidgets'
//   announcer; meters, the playhead and anything automation moves are never spoken.

#include <QApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include "AccessibleWidgets.h"
#include "DirectChildOfType.h"
#include "Engine.h"
#include "Song.h"
#include "UiConfigStore.h"

namespace lmms::a11y
{

namespace
{

constexpr int RepeatMs = 1000;

struct Spoken
{
	QString text;
	QElapsedTimer at;
};

int& spokenCounter()
{
	static int count = 0;
	return count;
}

Spoken& lastSpoken()
{
	static Spoken spoken;
	return spoken;
}

//! The object an announcement not tied to a control is raised on: the active window, else the application.
QObject* applicationSource()
{
	if (QWidget* active = QApplication::activeWindow()) { return active; }
	return QCoreApplication::instance();
}

QString transitionText(Transition kind, const QString& detail)
{
	switch (kind)
	{
		case Transition::RecordStarted: return QObject::tr("Recording");
		case Transition::RecordStopped: return QObject::tr("Recording stopped");
		case Transition::Saved: return QObject::tr("Saved %1").arg(detail).trimmed();
		case Transition::Opened: return QObject::tr("Opened %1").arg(detail).trimmed();
		case Transition::Refused: return QObject::tr("Not done: %1").arg(detail);
		default: return detail;
	}
}

Politeness politenessOf(Transition kind)
{
	return kind == Transition::Error || kind == Transition::Refused ? Politeness::Assertive : Politeness::Polite;
}

//! Follows one song for the transitions worth saying: recording starting and stopping, a save, an open. Parented to
//! the song, so it goes with it.
class SongTransitions : public QObject
{
public:
	explicit SongTransitions(Song* song)
		: QObject(song)
		, m_song(song)
		, m_wasModified(song->isModified())  // a song with unsaved changes already: its next save clears the flag
	{
		setObjectName(QStringLiteral("zeneSongTransitions"));
		connect(song, &Song::playbackStateChanged, this, [this] { playbackChanged(); });
		connect(song, &Song::modified, this, [this] { modifiedChanged(); });
		connect(song, &Song::projectLoaded, this, [this] { loaded(); });
	}

private:
	void playbackChanged()
	{
		// The song keeps its recording flag after it stops: recording is playing with the flag set.
		const bool recording = m_song->isPlaying() && m_song->isRecording();
		if (recording == m_recording) { return; }
		m_recording = recording;
		announceTransition(applicationSource(), recording ? Transition::RecordStarted : Transition::RecordStopped);
	}

	void modifiedChanged()
	{
		// A save clears the modified flag; so do a load (said as an open instead) and a new project (no file name).
		// The saved file's name is set just after the flag is cleared, so it is read on the next turn of the loop.
		const bool modified = m_song->isModified();
		const bool cleared = m_wasModified && !modified && !m_loading;
		m_wasModified = modified;
		if (!cleared) { return; }
		QTimer::singleShot(0, this, [this] {
			if (m_song->isModified() || m_song->projectFileName().isEmpty()) { return; }
			announceTransition(applicationSource(), Transition::Saved, QFileInfo(m_song->projectFileName()).fileName());
		});
	}

	void loaded()
	{
		// The load clears the modified flag after this signal, in the same call: not a save.
		m_loading = true;
		QTimer::singleShot(0, this, [this] { m_loading = false; m_wasModified = m_song->isModified(); });
		announceTransition(applicationSource(), Transition::Opened, QFileInfo(m_song->projectFileName()).fileName());
	}

	Song* m_song;
	bool m_recording = false;
	bool m_wasModified;
	bool m_loading = false;
};

} // namespace

AnnounceLevel announceLevel()
{
	const QString level = gui::uistore::read(QStringLiteral("announcements")).value(QStringLiteral("level")).toString();
	if (level == QLatin1String("off")) { return AnnounceLevel::Off; }
	return level == QLatin1String("errors") ? AnnounceLevel::Errors : AnnounceLevel::All;
}

const QStringList& announceLevelNames()
{
	static const QStringList names{QStringLiteral("all"), QStringLiteral("errors"), QStringLiteral("off")};
	return names;
}

bool shouldAnnounce(const QString& message, Politeness politeness, Repeat repeat)
{
	const AnnounceLevel level = announceLevel();
	if (message.isEmpty() || level == AnnounceLevel::Off) { return false; }
	if (level == AnnounceLevel::Errors && politeness != Politeness::Assertive) { return false; }
	Spoken& last = lastSpoken();
	if (repeat == Repeat::Suppress && last.text == message && last.at.isValid() && last.at.elapsed() < RepeatMs)
	{
		return false;
	}
	last.text = message;
	last.at.start();
	++spokenCounter();
	return true;
}

int announcementCount()
{
	return spokenCounter();
}

QString lastAnnouncement()
{
	return lastSpoken().text;
}

void announceTransition(QObject* source, Transition kind, const QString& detail)
{
	announceStatus(source != nullptr ? source : applicationSource(), transitionText(kind, detail), politenessOf(kind));
}

void watchSongTransitions()
{
	Song* song = Engine::getSong();
	if (song == nullptr || directChildOfType<SongTransitions>(song, QStringLiteral("zeneSongTransitions")) != nullptr)
	{
		return;
	}
	new SongTransitions(song);
}

} // namespace lmms::a11y
