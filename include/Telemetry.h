/*
 * Telemetry.h - opt-in, default-off anonymous platform statistics
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

#ifndef LMMS_TELEMETRY_H
#define LMMS_TELEMETRY_H

#include "lmmsconfig.h"
#include "lmms_export.h"

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>

namespace lmms
{

//! The payload is a closed set of named, coarse, non-identifying fields.
//!
//! Values enter only through setField(), which refuses any key that is not on
//! the fixed allowlist (see TelemetryPayload::allowedFields()). There is no
//! generic "add anything" path, so a project name, a file path, an email or a
//! free-text string cannot be expressed in a payload at all.
class LMMS_EXPORT TelemetryPayload
{
public:
	//! Every key a payload may ever carry. This is the whole contract.
	static QStringList allowedFields();
	//! True when \p key is on the allowlist.
	static bool isAllowedField(const QString & key);

	virtual ~TelemetryPayload() = default;

	TelemetryPayload();

	//! Stores \p value under \p key. Returns false (and stores nothing) when
	//! \p key is not allowlisted.
	bool setField(const QString & key, const QString & value);

	QString field(const QString & key) const;
	//! The allowlisted keys actually present, in allowlist order.
	QStringList presentFields() const;
	bool isEmpty() const;

	//! The exact bytes that are shown to the user and, if consent is on, sent.
	QByteArray toJsonBytes() const;

protected:
	//! The allowlist gate. setField() calls this; a subclass may widen it.
	virtual bool allowsField(const QString & key) const;

private:
	QMap<QString, QString> m_fields;
};

//! Consent is granular and default-off. Nothing here is ever true by default.
struct LMMS_EXPORT TelemetryConsent
{
	bool enabled = false;      //!< master switch, default off
	bool hardware = false;     //!< OS/CPU/GPU/RAM/display/audio group
	bool featureUsage = false; //!< coarse plugin-format and feature counters
	bool crashCounts = false;  //!< crash count + coarse stage label
	int consentVersion = 0;    //!< the notice revision the user agreed to

	//! Sending needs the master switch and at least one group.
	bool anyGroup() const { return hardware || featureUsage || crashCounts; }
	bool maySend() const { return enabled && anyGroup(); }
};

//! The seam the send path sits behind. Tests substitute a fake; production
//! uses TelemetryNetworkTransport (compiled only when telemetry is enabled).
class LMMS_EXPORT TelemetryTransport
{
public:
	virtual ~TelemetryTransport() = default;
	//! Delivers one already-serialised payload. True on success.
	virtual bool send(const QByteArray & payload) = 0;
	virtual QString describe() const = 0;
};

//! Coarse environment values. Every value is a bucket or a vendor/class label
//! - no model numbers, no serials, no paths, no hostnames, no user names.
struct LMMS_EXPORT TelemetryHardware
{
	QString osFamily = QStringLiteral("unknown");
	QString osVersionBucket = QStringLiteral("unknown");
	QString cpuArch = QStringLiteral("unknown");
	QString desktopSession = QStringLiteral("unknown");
	QString cpuVendor = QStringLiteral("unknown");
	QString cpuClass = QStringLiteral("unknown");
	QString gpuVendor = QStringLiteral("unknown");
	QString gpuClass = QStringLiteral("unknown");
	QString vramBucket = QStringLiteral("unknown");
	QString ramBucket = QStringLiteral("unknown");
	QString displayResolutionBucket = QStringLiteral("unknown");
	QString displayScale = QStringLiteral("unknown");
	QString audioBackend = QStringLiteral("unknown");
	QString audioBufferSizeBucket = QStringLiteral("unknown");
	QString audioSampleRate = QStringLiteral("unknown");
	int vst3Count = 0;
	int clapCount = 0;
	int lv2Count = 0;
	int vst2Count = 0;
	int featureGroupsUsed = 0;
	int crashCount = 0;
	QString crashStage = QStringLiteral("none");

	//! Reads the coarse values this machine can answer without touching any
	//! user data (Qt system info + the session type).
	static TelemetryHardware collect();
};

//! The opt-in telemetry client.
//!
//! Three properties are structural, not configuration:
//!   * the default consent is off, so a fresh install never sends;
//!   * submit() refuses unless the consent gate allows the send, and the only
//!     bytes it can ever hand to a transport are previewJson()'s bytes - the
//!     same object the UI shows;
//!   * when the package is built with -DZENE_TELEMETRY=OFF the whole client
//!     collapses to isCompiledIn() == false and submit() == CompiledOut, and
//!     no networking code is compiled at all.
class LMMS_EXPORT Telemetry
{
public:
	static constexpr int PayloadSchemaVersion = 1;
	//! Revision of the consent notice text; stored with every consent record.
	static constexpr int ConsentVersion = 1;

	enum class SubmitResult
	{
		Sent,               //!< handed to the transport and accepted
		ConsentOff,         //!< the consent gate refused
		CompiledOut,        //!< built with -DZENE_TELEMETRY=OFF
		NoTransport,        //!< no transport configured (the default)
		TransportRejected   //!< the transport refused the payload
	};

	Telemetry();
	Telemetry(const TelemetryConsent & consent,
			TelemetryTransport * transport,
			const TelemetryHardware & hardware);

	//! False when the packager built the feature out.
	static bool isCompiledIn();

	// --- consent persistence (the ConfigManager idiom) ---------------------
	static TelemetryConsent loadConsent();
	static void saveConsent(const TelemetryConsent & consent);

	const TelemetryConsent & consent() const { return m_consent; }
	void setConsent(const TelemetryConsent & consent) { m_consent = consent; }
	void setTransport(TelemetryTransport * transport) { m_transport = transport; }
	void setHardware(const TelemetryHardware & hardware) { m_hardware = hardware; }

	//! The payload for the current consent and environment. The UI preview
	//! renders exactly this; submit() sends exactly this.
	TelemetryPayload buildPayload() const;
	//! The preview bytes. submit() sends byte-for-byte these bytes.
	QByteArray previewJson() const;

	//! Builds the payload and, only if the gate allows it, sends it.
	SubmitResult submit();

protected:
	//! The consent gate. submit() refuses to send unless this returns true.
	virtual bool consentAllowsSend() const;

private:
	SubmitResult deliver();

	TelemetryConsent m_consent;
	TelemetryTransport * m_transport = nullptr;
	TelemetryHardware m_hardware;
};

} // namespace lmms

#endif // LMMS_TELEMETRY_H
