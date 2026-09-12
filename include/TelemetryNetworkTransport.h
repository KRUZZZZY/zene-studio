/*
 * TelemetryNetworkTransport.h - HTTPS delivery for opt-in telemetry
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

#ifndef LMMS_TELEMETRY_NETWORK_TRANSPORT_H
#define LMMS_TELEMETRY_NETWORK_TRANSPORT_H

#include "lmmsconfig.h"
#include "Telemetry.h"
#include "lmms_export.h"

#include <QString>

namespace lmms
{

//! The production transport: one batched HTTPS POST of the payload JSON.
//!
//! This whole translation unit is compiled only when the build has telemetry
//! enabled. With -DZENE_TELEMETRY=OFF it does not exist, so there is no send
//! path in the binary at all (prove it with nm: no NetworkTransport symbol,
//! no QNetworkAccessManager reference from this code).
class LMMS_EXPORT TelemetryNetworkTransport : public TelemetryTransport
{
public:
	//! An empty endpoint (the default) means "not configured": send() refuses
	//! without opening a socket.
	static QString configuredEndpoint();

	explicit TelemetryNetworkTransport(const QString & endpointUrl = configuredEndpoint());

	bool send(const QByteArray & payload) override;
	QString describe() const override;

private:
	QString m_endpoint;
};

} // namespace lmms

#endif // LMMS_TELEMETRY_NETWORK_TRANSPORT_H
