/*
 * Telemetry.cpp - opt-in, default-off anonymous platform statistics
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

#include "Telemetry.h"

#include "ConfigManager.h"
#include "lmmsversion.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>

namespace lmms
{

#ifdef ZENE_TELEMETRY_ENABLED

// --- the allowlist ---------------------------------------------------------
//
// Grouped exactly as the design groups them. A key that is not in one of
// these three lists is not a payload field, and cannot become one by calling
// setField() with it.

static const QStringList & hardwareFields()
{
	static const QStringList s_fields = {
		QStringLiteral("payload_schema_version"),
		QStringLiteral("client_version"),
		QStringLiteral("os_family"),
		QStringLiteral("os_version_bucket"),
		QStringLiteral("cpu_arch"),
		QStringLiteral("desktop_session"),
		QStringLiteral("cpu_vendor"),
		QStringLiteral("cpu_class"),
		QStringLiteral("gpu_vendor"),
		QStringLiteral("gpu_class"),
		QStringLiteral("vram_bucket"),
		QStringLiteral("ram_bucket"),
		QStringLiteral("display_resolution_bucket"),
		QStringLiteral("display_scale"),
		QStringLiteral("audio_backend"),
		QStringLiteral("audio_buffer_size_bucket"),
		QStringLiteral("audio_sample_rate"),
	};
	return s_fields;
}

static const QStringList & featureFields()
{
	static const QStringList s_fields = {
		QStringLiteral("plugin_vst3_count"),
		QStringLiteral("plugin_clap_count"),
		QStringLiteral("plugin_lv2_count"),
		QStringLiteral("plugin_vst2_count"),
		QStringLiteral("feature_groups_used"),
	};
	return s_fields;
}

static const QStringList & crashFields()
{
	static const QStringList s_fields = {
		QStringLiteral("crash_count"),
		QStringLiteral("crash_stage"),
	};
	return s_fields;
}

QStringList TelemetryPayload::allowedFields()
{
	return hardwareFields() + featureFields() + crashFields();
}

bool TelemetryPayload::isAllowedField(const QString & key)
{
	return allowedFields().contains(key);
}

bool TelemetryPayload::allowsField(const QString & key) const
{
	return isAllowedField(key);
}

TelemetryPayload::TelemetryPayload() = default;

bool TelemetryPayload::setField(const QString & key, const QString & value)
{
	if(!allowsField(key))
	{
		// Not allowlisted: the value is dropped, never stored, never sent.
		return false;
	}
	m_fields.insert(key, value);
	return true;
}

QString TelemetryPayload::field(const QString & key) const
{
	return m_fields.value(key);
}

QStringList TelemetryPayload::presentFields() const
{
	QStringList present;
	for(const QString & key : allowedFields())
	{
		if(m_fields.contains(key)) { present.append(key); }
	}
	return present;
}

bool TelemetryPayload::isEmpty() const
{
	return m_fields.isEmpty();
}

QByteArray TelemetryPayload::toJsonBytes() const
{
	QJsonObject object;
	for(auto it = m_fields.constBegin(); it != m_fields.constEnd(); ++it)
	{
		object.insert(it.key(), it.value());
	}
	// QJsonObject stores keys sorted, so the bytes are deterministic.
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

// --- consent persistence ---------------------------------------------------

static const char * const kConsentClass = "telemetry";

TelemetryConsent Telemetry::loadConsent()
{
	ConfigManager * cfg = ConfigManager::inst();
	TelemetryConsent consent;
	consent.enabled = cfg->value(kConsentClass, QStringLiteral("enabled"), QStringLiteral("0")) == QStringLiteral("1");
	consent.hardware = cfg->value(kConsentClass, QStringLiteral("hardware"), QStringLiteral("0")) == QStringLiteral("1");
	consent.featureUsage = cfg->value(kConsentClass, QStringLiteral("feature_usage"), QStringLiteral("0")) == QStringLiteral("1");
	consent.crashCounts = cfg->value(kConsentClass, QStringLiteral("crash_counts"), QStringLiteral("0")) == QStringLiteral("1");
	consent.consentVersion = cfg->value(kConsentClass, QStringLiteral("consent_version"), QStringLiteral("0")).toInt();
	return consent;
}

void Telemetry::saveConsent(const TelemetryConsent & consent)
{
	ConfigManager * cfg = ConfigManager::inst();
	cfg->setValue(kConsentClass, QStringLiteral("enabled"), consent.enabled ? QStringLiteral("1") : QStringLiteral("0"));
	cfg->setValue(kConsentClass, QStringLiteral("hardware"), consent.hardware ? QStringLiteral("1") : QStringLiteral("0"));
	cfg->setValue(kConsentClass, QStringLiteral("feature_usage"), consent.featureUsage ? QStringLiteral("1") : QStringLiteral("0"));
	cfg->setValue(kConsentClass, QStringLiteral("crash_counts"), consent.crashCounts ? QStringLiteral("1") : QStringLiteral("0"));
	// The minimal consent log: what revision was agreed to, against which
	// payload schema, and when. The toggles above are the rest of the record.
	cfg->setValue(kConsentClass, QStringLiteral("consent_version"), QString::number(consent.consentVersion));
	cfg->setValue(kConsentClass, QStringLiteral("consent_notice_version"), QString::number(ConsentVersion));
	cfg->setValue(kConsentClass, QStringLiteral("consent_schema_version"), QString::number(PayloadSchemaVersion));
	cfg->setValue(kConsentClass, QStringLiteral("consent_timestamp"),
			QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
}

// --- the client ------------------------------------------------------------

Telemetry::Telemetry() = default;

Telemetry::Telemetry(const TelemetryConsent & consent,
		TelemetryTransport * transport,
		const TelemetryHardware & hardware) :
	m_consent(consent),
	m_transport(transport),
	m_hardware(hardware)
{
}

bool Telemetry::isCompiledIn()
{
#ifdef ZENE_TELEMETRY_ENABLED
	return true;
#else
	return false;
#endif
}

bool Telemetry::consentAllowsSend() const
{
	return m_consent.maySend();
}

TelemetryPayload Telemetry::buildPayload() const
{
	TelemetryPayload payload;
	payload.setField(QStringLiteral("payload_schema_version"), QString::number(PayloadSchemaVersion));
	payload.setField(QStringLiteral("client_version"), QStringLiteral(LMMS_VERSION));

	if(m_consent.hardware)
	{
		payload.setField(QStringLiteral("os_family"), m_hardware.osFamily);
		payload.setField(QStringLiteral("os_version_bucket"), m_hardware.osVersionBucket);
		payload.setField(QStringLiteral("cpu_arch"), m_hardware.cpuArch);
		payload.setField(QStringLiteral("desktop_session"), m_hardware.desktopSession);
		payload.setField(QStringLiteral("cpu_vendor"), m_hardware.cpuVendor);
		payload.setField(QStringLiteral("cpu_class"), m_hardware.cpuClass);
		payload.setField(QStringLiteral("gpu_vendor"), m_hardware.gpuVendor);
		payload.setField(QStringLiteral("gpu_class"), m_hardware.gpuClass);
		payload.setField(QStringLiteral("vram_bucket"), m_hardware.vramBucket);
		payload.setField(QStringLiteral("ram_bucket"), m_hardware.ramBucket);
		payload.setField(QStringLiteral("display_resolution_bucket"), m_hardware.displayResolutionBucket);
		payload.setField(QStringLiteral("display_scale"), m_hardware.displayScale);
		payload.setField(QStringLiteral("audio_backend"), m_hardware.audioBackend);
		payload.setField(QStringLiteral("audio_buffer_size_bucket"), m_hardware.audioBufferSizeBucket);
		payload.setField(QStringLiteral("audio_sample_rate"), m_hardware.audioSampleRate);
	}

	if(m_consent.featureUsage)
	{
		payload.setField(QStringLiteral("plugin_vst3_count"), QString::number(m_hardware.vst3Count));
		payload.setField(QStringLiteral("plugin_clap_count"), QString::number(m_hardware.clapCount));
		payload.setField(QStringLiteral("plugin_lv2_count"), QString::number(m_hardware.lv2Count));
		payload.setField(QStringLiteral("plugin_vst2_count"), QString::number(m_hardware.vst2Count));
		payload.setField(QStringLiteral("feature_groups_used"), QString::number(m_hardware.featureGroupsUsed));
	}

	if(m_consent.crashCounts)
	{
		payload.setField(QStringLiteral("crash_count"), QString::number(m_hardware.crashCount));
		payload.setField(QStringLiteral("crash_stage"), m_hardware.crashStage);
	}

	return payload;
}

QByteArray Telemetry::previewJson() const
{
	// The preview IS the wire payload: same builder, same serialisation.
	return buildPayload().toJsonBytes();
}

Telemetry::SubmitResult Telemetry::deliver()
{
	if(!isCompiledIn()) { return SubmitResult::CompiledOut; }
	if(m_transport == nullptr) { return SubmitResult::NoTransport; }
	return m_transport->send(previewJson()) ? SubmitResult::Sent : SubmitResult::TransportRejected;
}

Telemetry::SubmitResult Telemetry::submit()
{
	if(!consentAllowsSend()) { return SubmitResult::ConsentOff; }
	return deliver();
}

// --- coarse environment ----------------------------------------------------

static QString memoryBucket()
{
	QFile meminfo(QStringLiteral("/proc/meminfo"));
	if(!meminfo.open(QIODevice::ReadOnly | QIODevice::Text)) { return QStringLiteral("unknown"); }
	while(!meminfo.atEnd())
	{
		const QByteArray line = meminfo.readLine();
		if(!line.startsWith("MemTotal:")) { continue; }
		bool ok = false;
		const long kb = line.mid(9).trimmed().split(' ').value(0).toLong(&ok);
		if(!ok || kb <= 0) { return QStringLiteral("unknown"); }
		const long gb = kb / (1024 * 1024);
		if(gb < 4) { return QStringLiteral("lt_4gb"); }
		if(gb < 8) { return QStringLiteral("4_8gb"); }
		if(gb < 16) { return QStringLiteral("8_16gb"); }
		if(gb < 32) { return QStringLiteral("16_32gb"); }
		return QStringLiteral("ge_32gb");
	}
	return QStringLiteral("unknown");
}

TelemetryHardware TelemetryHardware::collect()
{
	TelemetryHardware hw;
	hw.osFamily = QSysInfo::kernelType();
	const QString kernel = QSysInfo::kernelVersion();
	hw.osVersionBucket = kernel.isEmpty()
		? QStringLiteral("unknown")
		: QStringLiteral("kernel-") + kernel.section('.', 0, 0);
	hw.cpuArch = QSysInfo::currentCpuArchitecture();
	hw.desktopSession = qEnvironmentVariable("XDG_SESSION_TYPE", QStringLiteral("unknown"));
	hw.ramBucket = memoryBucket();
	return hw;
}

#else // !ZENE_TELEMETRY_ENABLED

// The packager kill switch. With -DZENE_TELEMETRY=OFF this translation unit
// defines exactly one symbol: the query callers use to ask whether the feature
// is present. There is no payload builder, no consent store, no submit() and
// no send path in the object file (checked with nm in docs/TELEMETRY-V1.md).
bool Telemetry::isCompiledIn()
{
	return false;
}

#endif // ZENE_TELEMETRY_ENABLED

} // namespace lmms
