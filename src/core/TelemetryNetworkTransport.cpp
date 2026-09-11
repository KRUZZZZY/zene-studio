/*
 * TelemetryNetworkTransport.cpp - HTTPS delivery for opt-in telemetry
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

#include "TelemetryNetworkTransport.h"

#include "ConfigManager.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace lmms
{

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

bool TelemetryNetworkTransport::send(const QByteArray & payload)
{
	if(m_endpoint.isEmpty())
	{
		// Nothing is configured to receive it. Refusing here is what keeps a
		// build with no endpoint from opening a socket for a preview.
		return false;
	}

	QNetworkAccessManager manager;
	QNetworkRequest request{QUrl(m_endpoint)};
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QNetworkReply * reply = manager.post(request, payload);

	QEventLoop loop;
	QTimer timeout;
	timeout.setSingleShot(true);
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	timeout.start(10000);
	loop.exec();

	const bool ok = reply->error() == QNetworkReply::NoError;
	reply->deleteLater();
	return ok;
}

QString TelemetryNetworkTransport::describe() const
{
	return m_endpoint.isEmpty()
		? QStringLiteral("no endpoint configured")
		: QStringLiteral("https POST to ") + m_endpoint;
}

} // namespace lmms
