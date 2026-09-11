/*
 * TelemetryTest.cpp - proofs for opt-in telemetry v1
 *
 * Copyright (c) 2026 LMMS Developers
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

// Every test here works against a fake transport. No test opens a socket, and
// no test can: the client only ever hands bytes to whatever TelemetryTransport
// it was given, and every test gives it a recorder.

#include "Telemetry.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using lmms::Telemetry;
using lmms::TelemetryConsent;
using lmms::TelemetryHardware;
using lmms::TelemetryPayload;
using lmms::TelemetryTransport;

namespace
{

//! Records what would have been sent instead of sending it anywhere.
class RecordingTransport : public TelemetryTransport
{
public:
	bool send(const QByteArray & payload) override
	{
		++m_sendCount;
		m_lastPayload = payload;
		return m_accept;
	}

	QString describe() const override { return QStringLiteral("recording fake; never touches the network"); }

	int sendCount() const { return m_sendCount; }
	const QByteArray & lastPayload() const { return m_lastPayload; }

private:
	int m_sendCount = 0;
	QByteArray m_lastPayload;
	bool m_accept = true;
};

//! Inverted control for the consent gate: exactly the production class with
//! the gate answer forced to true, i.e. the code path a "gate removed" edit
//! would create.
class GateRemovedTelemetry : public Telemetry
{
public:
	using Telemetry::Telemetry;

protected:
	bool consentAllowsSend() const override { return true; }
};

//! Inverted control for the allowlist: the production payload with the
//! allowlist check widened to accept anything.
class UnrestrictedPayload : public TelemetryPayload
{
protected:
	bool allowsField(const QString &) const override { return true; }
};

TelemetryHardware fixedHardware()
{
	TelemetryHardware hw;
	hw.osFamily = QStringLiteral("linux");
	hw.osVersionBucket = QStringLiteral("kernel-7");
	hw.cpuArch = QStringLiteral("x86_64");
	hw.desktopSession = QStringLiteral("wayland");
	hw.cpuVendor = QStringLiteral("amd");
	hw.cpuClass = QStringLiteral("8-core");
	hw.ramBucket = QStringLiteral("16_32gb");
	hw.audioBackend = QStringLiteral("alsa");
	hw.vst3Count = 3;
	hw.clapCount = 1;
	hw.vst2Count = 2;
	hw.featureGroupsUsed = 4;
	hw.crashCount = 5;
	hw.crashStage = QStringLiteral("startup");
	return hw;
}

TelemetryConsent fullyConsented()
{
	TelemetryConsent consent;
	consent.enabled = true;
	consent.hardware = true;
	consent.featureUsage = true;
	consent.crashCounts = true;
	consent.consentVersion = Telemetry::ConsentVersion;
	return consent;
}

bool everyKeyAllowlisted(const QByteArray & json)
{
	const QJsonObject object = QJsonDocument::fromJson(json).object();
	for(auto it = object.constBegin(); it != object.constEnd(); ++it)
	{
		if(!TelemetryPayload::isAllowedField(it.key())) { return false; }
	}
	return true;
}

} // namespace

class TelemetryTest : public QObject
{
	Q_OBJECT

private slots:
	// --- 1. default-off consent --------------------------------------------
	void defaultConsentIsOff()
	{
		const TelemetryConsent fresh;
		QVERIFY(!fresh.enabled);
		QVERIFY(!fresh.anyGroup());
		QVERIFY(!fresh.maySend());

		// The persisted default is also off: with nothing stored, the loader
		// returns the same all-false record.
		const TelemetryConsent persisted = Telemetry::loadConsent();
		QVERIFY(!persisted.enabled);
		QVERIFY(!persisted.maySend());
	}

	// --- 2. consent off cannot send (the core privacy property) ------------
	void consentOffCannotSend()
	{
		RecordingTransport recording;
		Telemetry client(TelemetryConsent{}, &recording, fixedHardware());

		const Telemetry::SubmitResult result = client.submit();

		QVERIFY(result == Telemetry::SubmitResult::ConsentOff);
		QCOMPARE(recording.sendCount(), 0);

		// INVERTED CONTROL: the same call on the same class with the consent
		// gate answered "yes" does send. So the assertion above (0 sends) is
		// exactly what fails if the gate is removed from submit().
		RecordingTransport gateRemovedRecording;
		GateRemovedTelemetry gateRemoved(TelemetryConsent{}, &gateRemovedRecording, fixedHardware());
		QVERIFY(gateRemoved.submit() == Telemetry::SubmitResult::Sent);
		QCOMPARE(gateRemovedRecording.sendCount(), 1);
	}

	// --- 3. an empty group set is not consent either -----------------------
	void enabledWithoutAnyGroupStillCannotSend()
	{
		TelemetryConsent masterOnOnly;
		masterOnOnly.enabled = true;

		RecordingTransport recording;
		Telemetry client(masterOnOnly, &recording, fixedHardware());
		QVERIFY(client.submit() == Telemetry::SubmitResult::ConsentOff);
		QCOMPARE(recording.sendCount(), 0);
	}

	// --- 4. the allowlist --------------------------------------------------
	void nonAllowlistedFieldCannotEnterThePayload()
	{
		TelemetryPayload payload;
		const QStringList forbidden = {
			QStringLiteral("project_name"), QStringLiteral("file_path"),
			QStringLiteral("plugin_name"), QStringLiteral("email"),
			QStringLiteral("ip_address"), QStringLiteral("installation_id"),
		};
		for(const QString & key : forbidden)
		{
			QVERIFY2(!payload.setField(key, QStringLiteral("anything")), qPrintable(key));
			QVERIFY(payload.field(key).isEmpty());
			QVERIFY(!payload.presentFields().contains(key));
		}
		// An allowlisted key is accepted, so the refusal above is the
		// allowlist deciding, not setField() refusing everything.
		QVERIFY(payload.setField(QStringLiteral("os_family"), QStringLiteral("linux")));
		QCOMPARE(payload.field(QStringLiteral("os_family")), QStringLiteral("linux"));

		// INVERTED CONTROL: with the allowlist gate widened, the identical
		// call accepts the forbidden key - the assertion above is therefore
		// sensitive to the allowlist check being removed.
		UnrestrictedPayload unrestricted;
		QVERIFY(unrestricted.setField(QStringLiteral("project_name"), QStringLiteral("secret")));
		QCOMPARE(unrestricted.field(QStringLiteral("project_name")), QStringLiteral("secret"));

		// Nothing built through the production path can carry a key off the
		// list, even at full consent.
		RecordingTransport recording;
		Telemetry client(fullyConsented(), &recording, fixedHardware());
		QVERIFY(everyKeyAllowlisted(client.previewJson()));
	}

	// --- 5. the preview is the wire payload --------------------------------
	void previewIsExactlyWhatIsSent()
	{
		RecordingTransport recording;
		Telemetry client(fullyConsented(), &recording, fixedHardware());

		const QByteArray preview = client.previewJson();
		QVERIFY(client.submit() == Telemetry::SubmitResult::Sent);

		QCOMPARE(recording.sendCount(), 1);
		QCOMPARE(recording.lastPayload(), preview);
		QVERIFY(everyKeyAllowlisted(recording.lastPayload()));
	}

	// --- 6. the consent groups are granular --------------------------------
	void granularConsentSelectsFields()
	{
		TelemetryConsent hardwareOnly;
		hardwareOnly.enabled = true;
		hardwareOnly.hardware = true;

		Telemetry client(hardwareOnly, nullptr, fixedHardware());
		const QJsonObject object = QJsonDocument::fromJson(client.previewJson()).object();

		QVERIFY(object.contains(QStringLiteral("os_family")));
		QVERIFY(!object.contains(QStringLiteral("crash_count")));
		QVERIFY(!object.contains(QStringLiteral("plugin_vst3_count")));
	}

	// --- 7. consent persists through ConfigManager -------------------------
	void consentRoundTripsThroughConfigManager()
	{
		const TelemetryConsent saved = fullyConsented();
		Telemetry::saveConsent(saved);

		const TelemetryConsent loaded = Telemetry::loadConsent();
		QVERIFY(loaded.enabled);
		QVERIFY(loaded.hardware);
		QVERIFY(loaded.featureUsage);
		QVERIFY(loaded.crashCounts);
		QCOMPARE(loaded.consentVersion, saved.consentVersion);

		// Leave the store as we found it: off.
		Telemetry::saveConsent(TelemetryConsent{});
		QVERIFY(!Telemetry::loadConsent().enabled);
	}

	// --- 8. the packager kill switch --------------------------------------
	void killSwitchStateMatchesTheBuild()
	{
#ifdef ZENE_TELEMETRY_ENABLED
		QVERIFY(Telemetry::isCompiledIn());
		RecordingTransport recording;
		Telemetry client(fullyConsented(), &recording, fixedHardware());
		QVERIFY(client.submit() == Telemetry::SubmitResult::Sent);
#else
		QVERIFY(!Telemetry::isCompiledIn());
		RecordingTransport recording;
		Telemetry client(fullyConsented(), &recording, fixedHardware());
		QVERIFY(client.submit() == Telemetry::SubmitResult::CompiledOut);
		QCOMPARE(recording.sendCount(), 0);
#endif
	}
};

QTEST_GUILESS_MAIN(TelemetryTest)
#include "TelemetryTest.moc"
