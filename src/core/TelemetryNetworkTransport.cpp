/*
 * TelemetryNetworkTransport.cpp - HTTPS delivery for opt-in telemetry
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
 * CODE-7. Two properties this file exists to enforce, and neither is a matter
 * of configuration:
 *
 *   https only  - the scheme is checked on EVERY send (isAllowedEndpoint),
 *                 before a socket exists. The previous revision posted to
 *                 QUrl(m_endpoint) whatever scheme it had, so an endpoint
 *                 configured as http:// would have put the payload on the wire
 *                 in clear - and the consent notice says "over TLS", not
 *                 "whatever the config file says".
 *
 *   never block - the POST is handed to Qt and the call returns. The previous
 *                 revision ran QEventLoop::exec() with a 10 s single-shot
 *                 timer and waited for reply->finished(): a slow endpoint
 *                 parked the calling thread for ten seconds. A telemetry
 *                 attempt must never be able to do that to a session.
 */

#include "TelemetryNetworkTransport.h"

#include "ConfigManager.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace lmms
{

bool TelemetryNetworkTransport::isAllowedEndpoint(const QUrl & endpoint, QString * reason)
{
	if (endpoint.isEmpty() || endpoint.host().isEmpty())
	{
		// The shipped default: no ingest service exists yet, so an enabled
		// build still refuses to open a socket. That refusal is a POLICY one,
		// and it reads the same way an https refusal does.
		if (reason != nullptr)
		{
			*reason = QStringLiteral("no endpoint is configured (the telemetry/endpoint config "
				"key is empty)");
		}
		return false;
	}

	const QString scheme = endpoint.scheme().toLower();
	if (scheme != QLatin1String("https"))
	{
		if (reason != nullptr)
		{
			*reason = QStringLiteral("the endpoint's scheme is '%1' and this transport posts over "
				"https only: a payload sent in clear is not something the consent notice covers, "
				"so it is refused rather than downgraded")
				.arg(scheme.isEmpty() ? QStringLiteral("none") : scheme);
		}
		return false;
	}
	return true;
}


QString TelemetryNetworkTransport::endpointPolicy()
{
	return QStringLiteral("https-only, non-blocking: an endpoint that is not https is refused "
		"before a socket exists, and a send hands the POST to Qt without waiting for the reply");
}


QString TelemetryNetworkTransport::configuredEndpoint()
{
	// The endpoint is deployment configuration, never a hard-coded host, and
	// it ships empty in v1: there is no ingest service yet, so an enabled
	// build still refuses to open a socket. A packager/owner sets this key
	// when the Cloudflare ingest function exists.
	return ConfigManager::inst()->value(QStringLiteral("telemetry"),
			QStringLiteral("endpoint"), QString());
}


TelemetryNetworkTransport::TelemetryNetworkTransport(const QString & endpointUrl) :
	m_endpoint(endpointUrl)
{
}


TelemetryNetworkTransport::~TelemetryNetworkTransport() = default;


void TelemetryNetworkTransport::setDeliverer(Deliverer deliverer)
{
	m_deliverer = std::move(deliverer);
}


bool TelemetryNetworkTransport::send(const QByteArray & payload)
{
	m_lastRefusal.clear();

	const QUrl endpoint(m_endpoint);
	if (!isAllowedEndpoint(endpoint, &m_lastRefusal))
	{
		// Refused BEFORE the deliverer and before any socket: this is the
		// branch a plain-http endpoint takes, and it takes it instantly.
		return false;
	}

	if (m_deliverer)
	{
		return m_deliverer(endpoint, payload);
	}

	if (m_manager == nullptr)
	{
		m_manager = std::make_unique<QNetworkAccessManager>();
	}
	QNetworkRequest request{endpoint};
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QNetworkReply * reply = m_manager->post(request, payload);
	// No wait, no nested event loop, no timeout timer: the reply deletes itself
	// when it finally finishes, and until then the caller has already returned.
	// A reply that never finishes is an attempt that never becomes a send, which
	// is the honest reading of a non-blocking transport.
	QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
	return true;
}


QString TelemetryNetworkTransport::describe() const
{
	if (m_endpoint.isEmpty())
	{
		return QStringLiteral("no endpoint configured");
	}

	QString reason;
	if (!isAllowedEndpoint(QUrl(m_endpoint), &reason))
	{
		// The endpoint is set but this transport will not post to it. Say so
		// where a human reads it, not only in lastRefusal() after a send.
		return QStringLiteral("endpoint refused: ") + reason;
	}
	return QStringLiteral("https POST to ") + m_endpoint;
}

} // namespace lmms
