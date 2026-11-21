/*
 * UpdateCheck.h - asking, with the user's consent, whether a newer release exists
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

#ifndef LMMS_UPDATE_CHECK_H
#define LMMS_UPDATE_CHECK_H

#include <functional>
#include <optional>

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTimer>

#include "lmms_export.h"

class QNetworkAccessManager;
class QNetworkReply;
class QUrl;

namespace lmms::updatecheck
{

/*! Q050-501. Everything here runs on the main thread, never on the audio thread. Nothing is downloaded or installed:
 *  a check is one bodyless GET of the feed, and its answer is a version, its notes and a page for a person to open.
 *  The file carries no Q_OBJECT so that -DZENE_UPDATE_CHECK=OFF cannot leave a moc behind (as TelemetryConsentDialog.h). */

struct Preferences
{
	bool enabled = false;      //!< true only once a person (or agent) said so; an unanswered question is off
	bool answered = false;     //!< the question has been answered, either way
	bool prereleases = false;  //!< pre-releases count too
};

LMMS_EXPORT Preferences preferences();
LMMS_EXPORT void savePreferences(const Preferences& preferences);
//! The first-run question has been shown; it is not asked again whatever came of it.
LMMS_EXPORT bool consentAsked();
LMMS_EXPORT void markConsentAsked();
//! The last release an automatic check showed a notice for: each is announced once, not daily.
LMMS_EXPORT QString noticedVersion();
LMMS_EXPORT void markNoticed(const QString& version);
//! When a request was last made; invalid when none was.
LMMS_EXPORT QDateTime lastCheck();

//! The configured feed: the updatecheck/feed config key, or the product's GitHub releases.
LMMS_EXPORT QString feedUrl();
//! https, or plain http to a loopback address only (what the tests use). Nothing else is ever requested.
LMMS_EXPORT bool isAllowedUrl(const QUrl& url);
//! "ZeneStudio/<version> (<operating system>; <cpu>)": all a request says about the program.
LMMS_EXPORT QByteArray userAgent();
//! What updates this copy instead ("flatpak", "snap"), or empty. When set the feature does not exist at run time.
LMMS_EXPORT QString managedBy();

//! Whether @a version is SemVer 2.0 (a leading 'v', as release tags have, is allowed).
LMMS_EXPORT bool isValidVersion(const QString& version);
//! SemVer precedence of two valid versions: negative, 0 or positive. Build metadata does not count.
LMMS_EXPORT int compareVersions(const QString& a, const QString& b);

struct Release
{
	QString version;  //!< the tag without its 'v'
	QString title;
	QString notes;    //!< plain text as the feed gave it, bounded; never interpreted as markup
	QString url;      //!< the download page
};

/*! The highest eligible release of a feed answer, in nothing when it lists none. Takes a GitHub release list or one
 *  release. Drafts never count, pre-releases (by flag or by version) only with @a prereleases, and an entry whose tag
 *  is not a version or whose page is not https is passed over. False, with @a error, when the answer is not a feed. */
LMMS_EXPORT bool parseFeed(const QByteArray& json, bool prereleases, std::optional<Release>* best, QString* error);

struct Outcome
{
	enum class State { None, Newer, UpToDate, Failed };
	State state = State::None;
	Release release;  //!< the newer release, when state is Newer
	QString reason;   //!< why it failed, when state is Failed
	bool manual = false;  //!< a person asked for this check and is waiting to hear
};

class LMMS_EXPORT UpdateChecker : public QObject
{
public:
	enum class Start { Started, AlreadyRunning, Off };

	static constexpr int DefaultTimeoutMs = 15000;
	static constexpr int MaxBodyBytes = 4 * 1024 * 1024;
	static constexpr qint64 CheckIntervalSeconds = 24 * 60 * 60;

	//! Lives as long as the application object.
	static UpdateChecker* instance();

	//! Starts a background check and returns at once; Off, without touching the network, unless the setting is on.
	Start check(bool manual);
	//! check(false) when at least a day has passed since the last request. True when a check started.
	bool checkIfDue(const QDateTime& now = QDateTime::currentDateTimeUtc());

	bool inProgress() const { return m_reply != nullptr; }
	const Outcome& lastOutcome() const { return m_last; }

	//! Called on the main thread with each finished check's outcome (the interface shows its notice from here).
	void setListener(std::function<void(const Outcome&)> listener) { m_listener = std::move(listener); }

	//! Test seams: this version stands in for the build's, and a shorter bound stands in for DefaultTimeoutMs.
	void setCurrentVersion(const QString& version) { m_version = version; }
	QString currentVersion() const;
	void setTimeoutMs(int ms) { m_timeoutMs = ms; }

private:
	explicit UpdateChecker(QObject* parent);
	void finish(QNetworkReply* reply);
	void conclude(Outcome outcome);

	QNetworkAccessManager* m_network = nullptr;  //!< made by the first request: an off setting never makes one
	QNetworkReply* m_reply = nullptr;  //!< the running request; owned by m_network
	QTimer m_deadline;
	QByteArray m_body;
	bool m_timedOut = false;
	bool m_tooLarge = false;
	bool m_manual = false;
	int m_timeoutMs = DefaultTimeoutMs;
	QString m_version;
	Outcome m_last;
	std::function<void(const Outcome&)> m_listener;
};

} // namespace lmms::updatecheck

#endif // LMMS_UPDATE_CHECK_H
