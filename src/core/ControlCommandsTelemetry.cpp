/*
 * ControlCommandsTelemetry.cpp - the telemetry.* command group (SPEC A15).
 *
 * Why these two commands exist, and why they are shaped this way
 * -------------------------------------------------------------
 * The Help menu has carried a "Telemetry - what we send..." action since #617,
 * and the agent-surface gate is right to fail on it: to an agent, a feature that
 * only exists in a menu is a feature that does not exist (AGENT-TOOLING.md
 * section 1). The fix is not to grandfather the action - it is to give it a
 * command to declare and a command an agent can actually use.
 *
 * The two commands split the feature along the one line that matters:
 *
 *   telemetry.consent  opens the consent screen. It declares `requires:
 *                      display, human` because it is a MODAL SCREEN: an agent
 *                      must never be able to consent on the user's behalf, and
 *                      include/UnattendedRun.h exists precisely because a modal
 *                      dialog in an unattended run is a wait on a click that
 *                      never comes (task #625). An agent that calls it gets a
 *                      typed `requires` refusal, and the registry refuses
 *                      before the handler runs - so no unattended caller can
 *                      ever open the screen.
 *
 *   telemetry.status   is READ-ONLY: it reports whether telemetry is compiled
 *                      in, whether consent is on, which groups are on, and the
 *                      exact payload the client would send (the same
 *                      Telemetry::buildPayload() bytes the consent screen
 *                      previews and submit() sends - there is no second list
 *                      here that could drift). It needs no display, no device
 *                      and no human, so the headless sweep exercises it.
 *
 * The product point of the split: an agent can SEE the telemetry state without
 * being able to change it. Consent stays a human act; visibility does not
 * require one (docs/TELEMETRY-V1.md).
 *
 * Both commands are registered whatever -DZENE_TELEMETRY says. With the kill
 * switch off the handlers answer a typed, honest "not in this build" instead of
 * vanishing from the registry: a client should be able to ask the question, and
 * "the registry has no telemetry.* group" or "the registry has it but nothing
 * answers" would both be worse than an answer.
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

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Telemetry.h"
#include "UnattendedRun.h"

#ifdef ZENE_TELEMETRY_ENABLED
#include "GuiApplication.h"
#include "MainWindow.h"
#include "TelemetryConsentDialog.h"
#endif

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The consent record as a client reads it. Only inline members of the struct,
//! so this compiles in a build with the kill switch off too.
QJsonObject consentState(const TelemetryConsent& consent)
{
	QJsonObject out;
	out.insert(QStringLiteral("enabled"), consent.enabled);
	out.insert(QStringLiteral("hardware"), consent.hardware);
	out.insert(QStringLiteral("feature_usage"), consent.featureUsage);
	out.insert(QStringLiteral("crash_counts"), consent.crashCounts);
	out.insert(QStringLiteral("consent_version"), consent.consentVersion);
	// The gate, not a second opinion: sending needs the master switch AND at
	// least one group, and this is the same predicate submit() consults.
	out.insert(QStringLiteral("may_send"), consent.maySend());
	return out;
}

//! The six consent keys above, as a result-schema property set.
QJsonObject consentProperties()
{
	QJsonObject properties;
	properties.insert(QStringLiteral("enabled"), booleanProperty());
	properties.insert(QStringLiteral("hardware"), booleanProperty());
	properties.insert(QStringLiteral("feature_usage"), booleanProperty());
	properties.insert(QStringLiteral("crash_counts"), booleanProperty());
	properties.insert(QStringLiteral("consent_version"), integerProperty());
	properties.insert(QStringLiteral("may_send"), booleanProperty());
	return properties;
}

//! consentProperties() plus the one key telemetry.consent adds.
QJsonObject consentScreenProperties()
{
	QJsonObject properties = consentProperties();
	properties.insert(QStringLiteral("saved"), booleanProperty());
	return objectSchema(properties);
}

//! consentProperties() plus what only the read-only report answers.
QJsonObject statusSchema()
{
	QJsonObject properties = consentProperties();
	properties.insert(QStringLiteral("compiled_in"), booleanProperty());
	properties.insert(QStringLiteral("payload_json"), stringProperty());
	properties.insert(QStringLiteral("payload_fields"), arrayProperty());
	properties.insert(QStringLiteral("payload_schema_version"), integerProperty());
	properties.insert(QStringLiteral("notice_version"), integerProperty());
	// Present only in a build with the kill switch off, where it says so. A
	// property is optional unless it is listed in "required", and none is.
	properties.insert(QStringLiteral("note"), stringProperty());
	return objectSchema(properties);
}

ControlResult telemetryStatus()
{
	QJsonObject result;
	result.insert(QStringLiteral("compiled_in"), Telemetry::isCompiledIn());

#ifdef ZENE_TELEMETRY_ENABLED
	const TelemetryConsent consent = Telemetry::loadConsent();
	const QJsonObject state = consentState(consent);
	for (auto it = state.constBegin(); it != state.constEnd(); ++it)
	{
		result.insert(it.key(), it.value());
	}

	// The exact bytes submit() would hand to the transport, built by the same
	// builder the consent screen previews. With consent off (the default) this
	// is the two non-consented fields, which is itself the answer to "what
	// would you send right now?".
	Telemetry client;
	client.setConsent(consent);
	client.setHardware(TelemetryHardware::collect());
	const TelemetryPayload payload = client.buildPayload();
	result.insert(QStringLiteral("payload_json"), QString::fromUtf8(payload.toJsonBytes()));
	result.insert(QStringLiteral("payload_fields"), QJsonArray::fromStringList(payload.presentFields()));
	result.insert(QStringLiteral("payload_schema_version"), Telemetry::PayloadSchemaVersion);
	result.insert(QStringLiteral("notice_version"), Telemetry::ConsentVersion);
#else
	// The kill switch removes the consent store and the payload builder with the
	// rest of the client, so there is no state to read. Every other key is still
	// answered, with the values that are true of this build, so the result does
	// not change shape with how the package was configured.
	result.insert(QStringLiteral("enabled"), false);
	result.insert(QStringLiteral("hardware"), false);
	result.insert(QStringLiteral("feature_usage"), false);
	result.insert(QStringLiteral("crash_counts"), false);
	result.insert(QStringLiteral("consent_version"), 0);
	result.insert(QStringLiteral("may_send"), false);
	result.insert(QStringLiteral("payload_json"), QString());
	result.insert(QStringLiteral("payload_fields"), QJsonArray());
	result.insert(QStringLiteral("payload_schema_version"), Telemetry::PayloadSchemaVersion);
	result.insert(QStringLiteral("notice_version"), Telemetry::ConsentVersion);
	result.insert(QStringLiteral("note"),
		QStringLiteral("this package was compiled with -DZENE_TELEMETRY=OFF: the consent store "
			"and the payload builder are not in this binary, so there is no telemetry state to "
			"report and nothing that could ever be sent."));
#endif
	return ControlResult::success(result);
}

} // namespace

ControlResult openTelemetryConsentScreen()
{
	// ONE implementation of the consent screen, shared by the Help menu action
	// (which declares telemetry.consent through the dynamic property
	// "controlCommand") and by the registry handler of that command - SPEC A11's
	// "one action, one implementation", so the menu item and the agent surface
	// cannot drift into two screens.
	if (isUnattendedRun())
	{
		// Nobody can answer this screen: --control-socket, or a platform with no
		// display. QDialog::exec() here would block the instance on a click that
		// never comes (task #625), so refuse, typed, and name the read-only
		// alternative the agent does have.
		return ControlResult::failure(ControlErrorKind::Requires,
			QStringLiteral("telemetry.consent opens a dialog and requires a human: this instance "
				"was started with --control-socket or on a platform with no display, so the "
				"consent screen would never be answered. Consent is a human decision; read the "
				"state and the exact payload with telemetry.status instead."));
	}

#ifdef ZENE_TELEMETRY_ENABLED
	gui::GuiApplication* application = gui::getGUI();
	gui::MainWindow* window = application != nullptr ? application->mainWindow() : nullptr;
	if (window == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("telemetry.consent parents the consent screen on the main window, "
				"and this instance has no GUI."));
	}
	gui::TelemetryConsentDialog dialog(window);
	dialog.exec();
	// What the screen left behind, read back from the one consent store, so the
	// result reports the state rather than assuming the human clicked Save.
	const TelemetryConsent consent = Telemetry::loadConsent();
	QJsonObject result = consentState(consent);
	result.insert(QStringLiteral("saved"), true);
	return ControlResult::success(result);
#else
	return ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("this package was compiled with -DZENE_TELEMETRY=OFF: the consent screen "
			"and the rest of the telemetry client are not in this binary."));
#endif
}

void registerTelemetryCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("telemetry.consent");
		cmd.group = QStringLiteral("telemetry");
		cmd.verb = QStringLiteral("consent");
		cmd.description = QStringLiteral("Open the telemetry consent screen - the Help menu's "
			"\"Telemetry - what we send...\" action, with the live preview of the exact payload. "
			"A13/UnattendedRun: this one is a modal screen, so it declares `requires: display, "
			"human` and every unattended caller is refused, typed, before the handler runs - an "
			"agent can never consent on the user's behalf. Read the state with telemetry.status.");
		// SPEC A13's closed vocabulary. `human` is the load-bearing one: it makes
		// this command unreachable for any automated caller, which is exactly the
		// property "consent is a human act" needs. `display` is declared too
		// because the implementation opens a dialog, so a headless run is told
		// about the screen rather than about the human first.
		cmd.requiresDecl = {QStringLiteral("display"), QStringLiteral("human")};
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = consentScreenProperties();
		cmd.handler = [](const QJsonObject&) { return openTelemetryConsentScreen(); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("telemetry.status");
		cmd.group = QStringLiteral("telemetry");
		cmd.verb = QStringLiteral("status");
		cmd.description = QStringLiteral("Read-only: whether telemetry is compiled in, whether "
			"consent is on, which groups are on, and the exact payload submit() would send "
			"(the same bytes the consent screen previews). This is the agent's way to see the "
			"telemetry state without being able to change it - consent stays a human act.");
		// No `requires`: reading the configuration and the environment needs no
		// display, no device and no human, so the headless sweep exercises it
		// instead of the allowlist excusing it (SPEC A15 reverse completeness).
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = statusSchema();
		cmd.handler = [](const QJsonObject&) { return telemetryStatus(); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
