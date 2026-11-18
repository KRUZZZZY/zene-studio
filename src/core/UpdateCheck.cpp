/*
 * UpdateCheck.cpp - asking, with the user's consent, whether a newer release exists
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

#include "UpdateCheck.h"

#include <algorithm>
#include <utility>

#include <QCoreApplication>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QSysInfo>
#include <QUrl>

#include "ConfigManager.h"
#include "ProjectVersion.h"
#include "lmmsversion.h"

namespace lmms::updatecheck
{

namespace
{

constexpr int MaxNotesChars = 64 * 1024;
constexpr int MaxRedirects = 3;

const QString& section()
{
	static const QString name = QStringLiteral("updatecheck");
	return name;
}

bool flag(const QString& attribute)
{
	return ConfigManager::inst()->value(section(), attribute) == QLatin1String("1");
}

void store(const QString& attribute, const QString& value)
{
	ConfigManager::inst()->setValue(section(), attribute, value);
	ConfigManager::inst()->saveConfigFile();
}

QString withoutTagPrefix(const QString& version)
{
	return version.startsWith(QLatin1Char('v')) ? version.mid(1) : version;
}

bool isPrerelease(const QString& version)
{
	return !ProjectVersion(withoutTagPrefix(version)).getLabels().isEmpty();
}

int compareNumericIdentifiers(const QString& left, const QString& right)
{
	return left.size() == right.size() ? QString::compare(left, right) : left.size() < right.size() ? -1 : 1;
}

int comparePrerelease(const QStringList& left, const QStringList& right)
{
	if (left.isEmpty() || right.isEmpty()) { return static_cast<int>(left.isEmpty()) - static_cast<int>(right.isEmpty()); }
	const auto numeric = [](const QString& identifier) {
		return std::all_of(identifier.cbegin(), identifier.cend(), [](QChar c) { return c >= QLatin1Char('0') && c <= QLatin1Char('9'); });
	};
	for (int i = 0; i < std::min(left.size(), right.size()); ++i)
	{
		const bool leftNumeric = numeric(left.at(i)), rightNumeric = numeric(right.at(i));
		if (leftNumeric != rightNumeric) { return leftNumeric ? -1 : 1; }
		const int order = leftNumeric ? compareNumericIdentifiers(left.at(i), right.at(i))
			: QString::compare(left.at(i), right.at(i), Qt::CaseSensitive);
		if (order != 0) { return order; }
	}
	return (left.size() > right.size()) - (left.size() < right.size());
}

//! One feed entry as a release, or nothing when it does not count.
std::optional<Release> eligibleRelease(const QJsonObject& entry, bool prereleases)
{
	const QString tag = entry.value(QStringLiteral("tag_name")).toString();
	const QUrl page(entry.value(QStringLiteral("html_url")).toString());
	if (entry.value(QStringLiteral("draft")).toBool() || !isValidVersion(tag) || !page.isValid() || !isAllowedUrl(page))
	{
		return std::nullopt;
	}
	if (!prereleases && (entry.value(QStringLiteral("prerelease")).toBool() || isPrerelease(tag))) { return std::nullopt; }
	return Release{withoutTagPrefix(tag), entry.value(QStringLiteral("name")).toString(),
		entry.value(QStringLiteral("body")).toString().left(MaxNotesChars), page.toString()};
}

//! A feed answer's releases: a list of them, or one.
bool feedEntries(const QByteArray& json, QJsonArray* entries, QString* error)
{
	QJsonParseError parse;
	const QJsonDocument document = QJsonDocument::fromJson(json, &parse);
	if (document.isArray())
	{
		*entries = document.array();
		return true;
	}
	if (document.isObject() && document.object().contains(QStringLiteral("tag_name")))
	{
		entries->append(document.object());
		return true;
	}
	*error = parse.error != QJsonParseError::NoError ? QStringLiteral("the answer is not JSON") : QStringLiteral("the answer lists no releases");
	return false;
}

//! The one quiet line a failed check leaves. Nothing a failed check could do should reach the user unasked.
void logQuietly(const QString& reason)
{
	qInfo("Update check: %s", qUtf8Printable(reason));
}

} // namespace

Preferences preferences()
{
	Preferences out;
	out.answered = flag(QStringLiteral("answered"));
	out.enabled = out.answered && flag(QStringLiteral("enabled"));
	out.prereleases = flag(QStringLiteral("prereleases"));
	return out;
}

void savePreferences(const Preferences& wanted)
{
	ConfigManager* config = ConfigManager::inst();
	config->setValue(section(), QStringLiteral("answered"), wanted.answered ? QStringLiteral("1") : QStringLiteral("0"));
	config->setValue(section(), QStringLiteral("enabled"), wanted.enabled ? QStringLiteral("1") : QStringLiteral("0"));
	config->setValue(section(), QStringLiteral("prereleases"), wanted.prereleases ? QStringLiteral("1") : QStringLiteral("0"));
	config->saveConfigFile();
}

bool consentAsked()
{
	return flag(QStringLiteral("asked"));
}

void markConsentAsked()
{
	store(QStringLiteral("asked"), QStringLiteral("1"));
}

QString noticedVersion()
{
	return ConfigManager::inst()->value(section(), QStringLiteral("noticed"));
}

void markNoticed(const QString& version)
{
	store(QStringLiteral("noticed"), version);
}

QDateTime lastCheck()
{
	return QDateTime::fromString(ConfigManager::inst()->value(section(), QStringLiteral("lastcheck")), Qt::ISODate);
}

QString feedUrl()
{
	return ConfigManager::inst()->value(section(), QStringLiteral("feed"),
		QStringLiteral("https://api.github.com/repos/KRUZZZZY/zene-studio/releases?per_page=10"));
}

bool isAllowedUrl(const QUrl& url)
{
	if (url.host().isEmpty()) { return false; }
	if (url.scheme() == QLatin1String("https")) { return true; }
	if (url.scheme() != QLatin1String("http")) { return false; }
	return url.host() == QLatin1String("localhost") || QHostAddress(url.host()).isLoopback();
}

QByteArray userAgent()
{
	return QStringLiteral("ZeneStudio/%1 (%2; %3)").arg(QString::fromUtf8(LMMS_VERSION), QSysInfo::kernelType(),
		QSysInfo::currentCpuArchitecture()).toUtf8();
}

QString managedBy()
{
	if (qEnvironmentVariableIsSet("FLATPAK_ID") || QFile::exists(QStringLiteral("/.flatpak-info"))) { return QStringLiteral("flatpak"); }
	if (qEnvironmentVariableIsSet("SNAP")) { return QStringLiteral("snap"); }
	return {};
}

bool isValidVersion(const QString& version)
{
	static const QRegularExpression semver(QStringLiteral(
		R"(^v?(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*))"
		R"((-(0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*)(\.(0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*))*)?)"
		R"((\+[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$)"));
	return semver.match(version).hasMatch();
}

int compareVersions(const QString& a, const QString& b)
{
	const auto splitVersion = [](QString version) {
		version = withoutTagPrefix(version).section(QLatin1Char('+'), 0, 0);
		const int dash = version.indexOf(QLatin1Char('-'));
		return std::pair{version.left(dash < 0 ? version.size() : dash).split(QLatin1Char('.')),
			dash < 0 ? QStringList() : version.mid(dash + 1).split(QLatin1Char('.'))};
	};
	const auto first = splitVersion(a);
	const auto second = splitVersion(b);
	for (int i = 0; i < 3; ++i)
	{
		const int order = compareNumericIdentifiers(first.first.at(i), second.first.at(i));
		if (order != 0) { return order; }
	}
	return comparePrerelease(first.second, second.second);
}

bool parseFeed(const QByteArray& json, bool prereleases, std::optional<Release>* best, QString* error)
{
	best->reset();
	QJsonArray entries;
	if (!feedEntries(json, &entries, error)) { return false; }
	for (const QJsonValue& value : entries)
	{
		if (!value.isObject() || !value.toObject().value(QStringLiteral("tag_name")).isString())
		{
			*error = QStringLiteral("the answer lists something that is not a release");
			return false;
		}
		const std::optional<Release> release = eligibleRelease(value.toObject(), prereleases);
		if (release && (!*best || compareVersions(release->version, (*best)->version) > 0)) { *best = release; }
	}
	return true;
}

UpdateChecker::UpdateChecker(QObject* parent) :
	QObject(parent)
{
	m_deadline.setSingleShot(true);
	connect(&m_deadline, &QTimer::timeout, this, [this] {
		m_timedOut = true;
		if (m_reply) { m_reply->abort(); }
	});
}

UpdateChecker* UpdateChecker::instance()
{
	static QPointer<UpdateChecker> checker;
	if (checker.isNull()) { checker = new UpdateChecker(QCoreApplication::instance()); }
	return checker;
}

QString UpdateChecker::currentVersion() const
{
	return m_version.isEmpty() ? QString::fromUtf8(LMMS_VERSION) : m_version;
}

UpdateChecker::Start UpdateChecker::check(bool manual)
{
	if (!managedBy().isEmpty() || !preferences().enabled) { return Start::Off; }
	if (inProgress())
	{
		m_manual = m_manual || manual;  // whoever asked second is waiting for the same answer
		return Start::AlreadyRunning;
	}
	m_manual = manual;
	m_timedOut = false;
	m_tooLarge = false;
	m_body.clear();
	const QUrl feed(feedUrl());
	store(QStringLiteral("lastcheck"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
	if (!feed.isValid() || !isAllowedUrl(feed))
	{
		// Reported later, like every other failure, so a caller sees one way an answer arrives.
		QTimer::singleShot(0, this, [this] { conclude({Outcome::State::Failed, {}, QStringLiteral("the configured feed is not https"), m_manual}); });
		return Start::Started;
	}
	if (m_network == nullptr)
	{
		m_network = new QNetworkAccessManager(this);
	}
	QNetworkRequest request(feed);
	request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
	request.setRawHeader("Accept", "application/json");
	request.setRawHeader("Accept-Language", "en");  // Qt would otherwise fill in the system's locale
	request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
	request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setMaximumRedirectsAllowed(MaxRedirects);
	QNetworkReply* reply = m_network->get(request);
	m_reply = reply;
	connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
		m_body += reply->read(MaxBodyBytes + 1 - m_body.size());
		if (m_body.size() > MaxBodyBytes && !m_tooLarge)
		{
			m_tooLarge = true;
			reply->abort();
		}
	});
	connect(reply, &QNetworkReply::finished, this, [this, reply] { finish(reply); });
	m_deadline.start(m_timeoutMs);
	return Start::Started;
}

bool UpdateChecker::checkIfDue(const QDateTime& now)
{
	const QDateTime last = lastCheck();
	// A clock put back since the last check (a last check in the future) does not silence checks for good.
	if (last.isValid() && last <= now && last.secsTo(now) < CheckIntervalSeconds) { return false; }
	return check(false) == Start::Started;
}

void UpdateChecker::finish(QNetworkReply* reply)
{
	m_deadline.stop();
	m_reply = nullptr;
	reply->deleteLater();
	Outcome outcome;
	outcome.manual = m_manual;
	outcome.state = Outcome::State::Failed;
	const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	std::optional<Release> best;
	if (m_timedOut) { outcome.reason = QStringLiteral("no answer in %1 ms").arg(m_timeoutMs); }
	else if (m_tooLarge) { outcome.reason = QStringLiteral("the answer is too large"); }
	else if (status != 0 && status != 200) { outcome.reason = QStringLiteral("the feed answered HTTP %1").arg(status); }
	else if (reply->error() != QNetworkReply::NoError) { outcome.reason = reply->errorString(); }
	else if (parseFeed(m_body, preferences().prereleases, &best, &outcome.reason))
	{
		outcome.state = best && compareVersions(best->version, currentVersion()) > 0 ? Outcome::State::Newer : Outcome::State::UpToDate;
		if (outcome.state == Outcome::State::Newer) { outcome.release = *best; }
	}
	m_body.clear();
	conclude(outcome);
}

void UpdateChecker::conclude(Outcome outcome)
{
	if (outcome.state == Outcome::State::Failed) { logQuietly(outcome.reason); }
	m_last = std::move(outcome);
	if (m_listener) { m_listener(m_last); }
}

} // namespace lmms::updatecheck
