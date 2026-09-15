/*
 * TelemetryNetworkTransport.h - HTTPS delivery for opt-in telemetry
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

#ifndef LMMS_TELEMETRY_NETWORK_TRANSPORT_H
#define LMMS_TELEMETRY_NETWORK_TRANSPORT_H

#include "lmmsconfig.h"
#include "Telemetry.h"
#include "lmms_export.h"

#include <QString>
#include <QUrl>

#include <functional>
#include <memory>

class QNetworkAccessManager;

namespace lmms
{

/*! \brief The production transport: one batched HTTPS POST of the payload JSON.
 *
 * Two properties are structural, and CODE-7 is where they are enforced rather
 * than assumed:
 *
 *   * **HTTPS only.** The endpoint is deployment configuration, and
 *     configuration is a thing that can be wrong, so the scheme is checked on
 *     every send and not just when the key is written: a plain-http endpoint -
 *     or ftp://, file://, or a scheme-less string - is REFUSED before any
 *     socket exists, and the refusal is in words (lastRefusal(), describe()).
 *     A payload that leaves this class leaves over TLS, or it does not leave.
 *
 *   * **It never blocks the caller.** send() hands the request to Qt's
 *     networking and returns; there is no nested QEventLoop and no wait for the
 *     reply. The previous implementation ran QEventLoop::exec() behind a 10 s
 *     single-shot timer, so a slow, blackholed or unreachable endpoint parked
 *     the calling thread - the GUI thread - for the whole ten seconds. `true`
 *     therefore means "accepted and queued", NOT "delivered": the delivery
 *     outcome is not something the caller waits for, and the consequence is
 *     stated rather than hidden - the transport wants an event loop on the
 *     thread that owns it, and a reply that never arrives is an attempt that is
 *     never counted as a send.
 *
 * This whole translation unit is compiled only when the build has telemetry
 * enabled. With -DZENE_TELEMETRY=OFF it does not exist, so there is no send
 * path in the binary at all (prove it with nm: no NetworkTransport symbol and
 * no QNetworkAccessManager reference).
 */
class LMMS_EXPORT TelemetryNetworkTransport : public TelemetryTransport
{
public:
	/*! \brief The ONE definition of "may this endpoint be posted to?".
	 *
	 * https and a host, nothing else. \a reason receives the refusal in words
	 * when the answer is false, so a caller reports a reason instead of a bare
	 * false (telemetry.status reports both).
	 */
	static bool isAllowedEndpoint(const QUrl & endpoint, QString * reason = nullptr);
	//! The policy in one line, as telemetry.status and the docs quote it.
	static QString endpointPolicy();

	//! An empty endpoint (the default) means "not configured": send() refuses
	//! without opening a socket.
	static QString configuredEndpoint();

	explicit TelemetryNetworkTransport(const QString & endpointUrl = configuredEndpoint());
	~TelemetryNetworkTransport() override;

	/*! Hands \p payload to the endpoint and returns at once.
	 *
	 * \return true when the POST was queued - the caller must not read that as
	 *         "delivered". False, with lastRefusal() set, when the endpoint is
	 *         empty or is not https: nothing was opened and nothing was sent.
	 */
	bool send(const QByteArray & payload) override;
	QString describe() const override;

	//! Why the last send() returned false; empty after a queued send.
	QString lastRefusal() const { return m_lastRefusal; }

	/*! The delivery seam, for tests.
	 *
	 * It exists so that "the https rule refused this" can be proven WITHOUT a
	 * socket: a test attaches a deliverer that records the call, and "the
	 * deliverer was never called" is a stronger statement than "the request
	 * failed". It is also how a test hands the transport a delivery that never
	 * completes, which is the only way to time "the caller did not wait".
	 *
	 * Production never sets one; the default delivers through
	 * QNetworkAccessManager.
	 */
	using Deliverer = std::function<bool(const QUrl &, const QByteArray &)>;
	void setDeliverer(Deliverer deliverer);

private:
	QString m_endpoint;
	QString m_lastRefusal;
	Deliverer m_deliverer;
	//! Created on the first accepted send. Held rather than constructed per
	//! call: a manager that dies while a reply is in flight takes the reply
	//! with it, and the whole point of CODE-7 is that the reply is not waited
	//! for here.
	std::unique_ptr<QNetworkAccessManager> m_manager;
};

} // namespace lmms

#endif // LMMS_TELEMETRY_NETWORK_TRANSPORT_H
