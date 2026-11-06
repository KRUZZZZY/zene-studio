/*
 * ControlCommandsUpdateCheck.cpp - app.update_check_get / _set / _run
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

/*! Q050-501: the update-check preference and the check itself, for agents and for the interface (Settings and
 *  Help > Check for Updates run these same commands). Compiled only with -DZENE_UPDATE_CHECK=ON; in a Flatpak or Snap
 *  sandbox nothing registers, so the ids do not exist where something else updates the program. */

#include "ControlCommandDef.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "UpdateCheck.h"

namespace lmms
{

using namespace control;
using updatecheck::Outcome;
using updatecheck::Preferences;
using updatecheck::UpdateChecker;

namespace
{

QString stateName(Outcome::State state)
{
	switch (state)
	{
	case Outcome::State::Newer: return QStringLiteral("newer");
	case Outcome::State::UpToDate: return QStringLiteral("up_to_date");
	case Outcome::State::Failed: return QStringLiteral("failed");
	case Outcome::State::None: break;
	}
	return QStringLiteral("none");
}

QJsonObject preferencesJson(const Preferences& preferences)
{
	return {{QStringLiteral("enabled"), preferences.enabled}, {QStringLiteral("answered"), preferences.answered},
		{QStringLiteral("prereleases"), preferences.prereleases}};
}

ControlResult getState(const QJsonObject&)
{
	UpdateChecker* checker = UpdateChecker::instance();
	QJsonObject out = preferencesJson(updatecheck::preferences());
	const Outcome& last = checker->lastOutcome();
	const QDateTime lastCheck = updatecheck::lastCheck();
	out.insert(QStringLiteral("feed"), updatecheck::feedUrl());
	out.insert(QStringLiteral("user_agent"), QString::fromUtf8(updatecheck::userAgent()));
	out.insert(QStringLiteral("current_version"), checker->currentVersion());
	out.insert(QStringLiteral("last_check"), lastCheck.isValid() ? lastCheck.toString(Qt::ISODate) : QString());
	out.insert(QStringLiteral("in_progress"), checker->inProgress());
	out.insert(QStringLiteral("result"), QJsonObject{{QStringLiteral("state"), stateName(last.state)},
		{QStringLiteral("version"), last.release.version}, {QStringLiteral("title"), last.release.title},
		{QStringLiteral("notes"), last.release.notes}, {QStringLiteral("url"), last.release.url},
		{QStringLiteral("reason"), last.reason}});
	return ControlResult::success(out);
}

ControlResult setState(const QJsonObject& args)
{
	const QString enabledKey = QStringLiteral("enabled");
	const QString prereleasesKey = QStringLiteral("prereleases");
	if (!args.contains(enabledKey) && !args.contains(prereleasesKey))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("say 'enabled', 'prereleases' or both"));
	}
	const Preferences before = updatecheck::preferences();
	Preferences after = before;
	if (args.contains(enabledKey))
	{
		after.enabled = args.value(enabledKey).toBool();
		after.answered = true;
	}
	after.prereleases = args.value(prereleasesKey).toBool(before.prereleases);
	addUndoStep([before] { updatecheck::savePreferences(before); }, [after] { updatecheck::savePreferences(after); });
	updatecheck::savePreferences(after);

	QJsonObject out = preferencesJson(after);
	// With the question still unanswered no set can state "unanswered" again, so only the action checkpoint reverses it.
	out.insert(QStringLiteral("__transaction"), QJsonObject{{QStringLiteral("before"), preferencesJson(before)},
		{QStringLiteral("inverse"), before.answered
			? QJsonObject{{QStringLiteral("op"), QStringLiteral("app.update_check_set")},
				{QStringLiteral("args"), QJsonObject{{enabledKey, before.enabled}, {prereleasesKey, before.prereleases}}}}
			: QJsonObject{{QStringLiteral("op"), QStringLiteral("control.undo")}, {QStringLiteral("args"), QJsonObject()}}},
		{QStringLiteral("reversible"), true},
		{QStringLiteral("mechanism"), QStringLiteral("action checkpoint: the recorded undo step writes the previous "
			"preference back and saves the config file, exactly as this command does")}});
	return ControlResult::success(out);
}

ControlResult runCheck(const QJsonObject&)
{
	switch (UpdateChecker::instance()->check(false))
	{
	case UpdateChecker::Start::Off:
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("update checks are off, so no request is "
			"made: turn them on with app.update_check_set first"));
	case UpdateChecker::Start::AlreadyRunning:
		return ControlResult::success({{QStringLiteral("started"), false}, {QStringLiteral("in_progress"), true}});
	case UpdateChecker::Start::Started: break;
	}
	return ControlResult::success({{QStringLiteral("started"), true}, {QStringLiteral("in_progress"), true}});
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args, bool mutating,
	ControlResult (*handler)(const QJsonObject&))
{
	defineCommandWithSchema(registry, QStringLiteral("app.") + QLatin1String(verb), description, args, mutating, handler, false);
}

} // namespace

void registerUpdateCheckCommands(ControlRegistry& registry)
{
	if (!updatecheck::managedBy().isEmpty()) { return; }
	add(registry, "update_check_get", QStringLiteral("Whether Zene may check for a newer release: 'enabled', 'answered' (the "
		"question was answered; unanswered is off), 'prereleases'; the 'feed', the 'user_agent' a request sends (all it "
		"sends about the program), 'last_check', 'in_progress' and 'result' of the last check this session ('state' "
		"none|newer|up_to_date|failed, 'version', 'title', 'notes', 'url' of the download page, 'reason'). Nothing is sent."),
		objectSchema({}), false, getState);
	add(registry, "update_check_set", QStringLiteral("Allow or forbid update checks ('enabled') and whether pre-releases "
		"count ('prereleases'; the other stays as it was). Saved in the user's configuration, as Settings does; one undo step. "
		"This only changes the preference: app.update_check_run or the daily check makes the request."),
		objectSchema({{QStringLiteral("enabled"), booleanProperty()}, {QStringLiteral("prereleases"), booleanProperty()}}),
		true, setState);
	add(registry, "update_check_run", QStringLiteral("Start a check of the feed in the background and return at once; "
		"app.update_check_get's 'result' has the answer. Refused while update checks are off: no request is ever made then. "
		"One GET naming the version and platform in its User-Agent; nothing is downloaded or installed."),
		objectSchema({}), false, runCheck);
}

} // namespace lmms
