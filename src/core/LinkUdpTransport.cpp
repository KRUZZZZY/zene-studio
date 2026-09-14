/*
 * LinkUdpTransport.cpp - how a session announcement travels: UDP multicast on
 *                        the group and port Ableton Link itself uses
 *                        (224.76.78.75:20808).
 *
 * WHY UDP MULTICAST, AND WHY THIS FILE IS THE ONLY PLATFORM-AWARE ONE.
 *
 * A session is "whoever is on this network segment", with no address list to
 * configure: multicast is how Link does discovery and it is the only send shape
 * that reaches every instance on one host at once (a unicast datagram to a port
 * two sockets are bound to is delivered to one of them, not both). Two Zene
 * instances on one box are the release's stated use case, so they BOTH bind the
 * group port with SO_REUSEADDR and the kernel loops each send back to all of
 * them (IP_MULTICAST_LOOP). The same code reaches a LAN with no change: the
 * membership is joined on every interface (INADDR_ANY) and the group send
 * leaves by the system's default multicast interface.
 *
 * A platform without the POSIX datagram API gets the stub below, which reports
 * itself unavailable with a reason instead of pretending. The MODEL in
 * include/LinkSync.h does not know the difference - that is the point of
 * LinkPeerTransport - so `link.get_state` still answers there, and the model
 * still has tests. Windows is the platform in question: winsock2 would need a
 * second socket implementation that this lane cannot run a test for, and
 * shipping untested socket code is worse than reporting the gap.
 *
 * EVERYTHING HERE RUNS ON THE UI THREAD. The socket is non-blocking and read
 * through a QSocketNotifier, the same pattern ControlServerSocket.cpp uses for
 * the control socket. There is no receive thread, so there is no lock and
 * nothing for the audio thread to synchronise with.
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

#include "LinkPeerTransport.h"

#include <cstddef>
#include <cstring>
#include <utility>

#include <QObject>
#include <QSocketNotifier>
#include <QString>

#include "LinkSync.h"

#if !defined(Q_OS_WIN)
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <QElapsedTimer>

namespace lmms
{

namespace
{

//! The largest announcement this transport reads. A packet is ~250 bytes of
//! JSON; anything larger is not one of ours and is read into the same bounded
//! buffer and rejected by the model's decoder.
constexpr int DatagramBufferBytes = 2048;

/*! How long the loopback probe may take to come back before this host is
 *  declared unable to RECEIVE an announcement. A host that can deliver one
 *  delivers it in well under a millisecond (measured: 0.1 ms on loopback, see
 *  docs/LINK-SYNC.md section 3), so a healthy host pays ~nothing here and only
 *  a host that is about to be reported as unable to carry a session pays the
 *  whole bound - once, at link.set_enabled.
 */
constexpr int LoopbackProbeBoundMs = 250;

/*! The probe's payload. Deliberately NOT an announcement: the model's decoder
 *  refuses anything whose JSON it cannot read, so this can never be mistaken
 *  for a peer.
 */
constexpr const char* LoopbackProbePayload = "zene-link-loopback-probe";

QString endpointString()
{
	return QStringLiteral("%1:%2").arg(QString::fromLatin1(link::DefaultGroup))
		.arg(link::DefaultPort);
}

#if !defined(Q_OS_WIN)

//! "what failed: why", so link.get_state's transport_reason is actionable.
QString socketFailure(const QString& what)
{
	return QStringLiteral("%1: %2").arg(what, QString::fromLocal8Bit(std::strerror(errno)));
}

/*! The multicast transport: bind the group port, join the group, read datagrams
 *  through a notifier. */
class LinkUdpTransport : public QObject, public LinkPeerTransport
{
public:
	explicit LinkUdpTransport(QObject* parent = nullptr) : QObject(parent) {}
	~LinkUdpTransport() override { stop(); }

	LinkUdpTransport(const LinkUdpTransport&) = delete;
	LinkUdpTransport& operator=(const LinkUdpTransport&) = delete;

	bool start() override
	{
		if (m_fd >= 0)
		{
			return true;
		}
		const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
		if (fd < 0)
		{
			m_reason = socketFailure(QStringLiteral("socket()"));
			return false;
		}
		if (!configure(fd))
		{
			::close(fd);
			return false;
		}
		m_fd = fd;
		m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
		// No arguments taken: QSocketNotifier::activated passes different
		// things in Qt5 and Qt6, and this callback needs neither.
		connect(m_notifier, &QSocketNotifier::activated, this, [this]() { drain(); });
		m_reason.clear();
		return true;
	}

	void stop() override
	{
		delete m_notifier;
		m_notifier = nullptr;
		if (m_fd >= 0)
		{
			::close(m_fd);
			m_fd = -1;
		}
		m_reason = QStringLiteral("not started (link.set_enabled starts it)");
	}

	bool available() const override { return m_fd >= 0; }
	QString reason() const override { return m_fd >= 0 ? QString() : m_reason; }
	QString endpoint() const override { return endpointString(); }
	LoopbackProbe loopbackProbe() const override { return m_probe; }

	void send(const QByteArray& payload) override
	{
		if (m_fd < 0)
		{
			return;
		}
		// Best-effort by design: a dropped datagram is a missed announcement,
		// and the next one is 100 ms away. A send error is NOT a state change.
		(void)::sendto(m_fd, payload.constData(), static_cast<size_t>(payload.size()), 0,
			reinterpret_cast<const sockaddr*>(&m_group), sizeof(m_group));
	}

	void setReceiver(Receiver receiver) override { m_receiver = std::move(receiver); }

private:
	/*! Bind the group port and join the group on \a fd. \a why receives the
	 *  failing step's reason when it is not null. ONE definition: the real
	 *  socket and the probe socket below must be configured identically, or the
	 *  measurement would be of a different socket than the one that travels. */
	static bool joinGroup(int fd, QString* why)
	{
		int on = 1;
		/* Two instances on ONE host both bind this port, so SO_REUSEADDR is
		 * required rather than tidy. macOS and the BSDs want SO_REUSEPORT for
		 * the same thing; its failure is ignored on purpose, because Linux is
		 * happy without it. */
		(void)::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#ifdef SO_REUSEPORT
		(void)::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
#endif
		sockaddr_in local{};
		local.sin_family = AF_INET;
		local.sin_addr.s_addr = htonl(INADDR_ANY);
		local.sin_port = htons(link::DefaultPort);
		if (::bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0)
		{
			if (why != nullptr)
			{
				*why = socketFailure(QStringLiteral("bind(%1)").arg(endpointString()));
			}
			return false;
		}
		ip_mreq membership{};
		membership.imr_multiaddr.s_addr = ::inet_addr(link::DefaultGroup);
		// INADDR_ANY: join on every interface, so the same build works for two
		// instances on one box and for peers on a LAN.
		membership.imr_interface.s_addr = htonl(INADDR_ANY);
		if (::setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership,
				sizeof(membership)) != 0)
		{
			if (why != nullptr)
			{
				*why = socketFailure(QStringLiteral("IP_ADD_MEMBERSHIP(%1)")
					.arg(QString::fromLatin1(link::DefaultGroup)));
			}
			return false;
		}
		return true;
	}

	/*! MEASURE that announcements can be RECEIVED on this host: open a SECOND
	 *  socket, configured exactly like this one, and require a datagram sent
	 *  from \a fd to the group to arrive on it inside the bound.
	 *
	 *  Why the second socket and not a send-to-self. A socket reading its own
	 *  looped datagram proves the kernel loops a packet back to its SENDER;
	 *  the session needs it delivered to ANOTHER socket, which is the case two
	 *  instances on one box are and the case a platform that cannot receive
	 *  multicast fails. Measuring the weak property would let exactly the host
	 *  this check exists for pass it.
	 *
	 *  bind() and IP_ADD_MEMBERSHIP() succeeding says nothing about this: a
	 *  host can accept both and still deliver nothing, which is why the probe
	 *  exists and why `available()` is derived from its verdict. */
	bool measureLoopback(int fd)
	{
		m_probe = LoopbackProbe();
		m_probe.attempted = true;
		m_probe.boundMs = LoopbackProbeBoundMs;

		const int probeFd = ::socket(AF_INET, SOCK_DGRAM, 0);
		if (probeFd < 0)
		{
			m_probe.boundMs = 0;
			return false;
		}
		if (!joinGroup(probeFd, nullptr))
		{
			::close(probeFd);
			return false;
		}
		QElapsedTimer clock;
		clock.start();
		(void)::sendto(fd, LoopbackProbePayload, std::strlen(LoopbackProbePayload), 0,
			reinterpret_cast<const sockaddr*>(&m_group), sizeof(m_group));

		char buffer[DatagramBufferBytes];
		bool delivered = false;
		while (!delivered)
		{
			pollfd waiting{probeFd, POLLIN, 0};
			const int remaining = LoopbackProbeBoundMs - static_cast<int>(clock.elapsed());
			if (remaining <= 0 || ::poll(&waiting, 1, remaining) <= 0) { break; }
			const ssize_t read = ::recv(probeFd, buffer, sizeof(buffer), 0);
			if (read <= 0) { break; }
			// A datagram that is not the probe is some other instance's
			// announcement, which is a peer: keep waiting for ours.
			delivered = QByteArray(buffer, static_cast<int>(read)) == LoopbackProbePayload;
		}
		m_probe.delivered = delivered;
		m_probe.elapsedMs = delivered ? static_cast<int>(clock.elapsed()) : -1;
		::close(probeFd);
		return delivered;
	}

	//! Socket options + bind + join + the receive measurement. Split out of
	//! start() so neither function carries the whole recipe (the complexity
	//! ratchet measures both).
	bool configure(int fd)
	{
		QString why;
		if (!joinGroup(fd, &why))
		{
			m_reason = why;
			return false;
		}
		const unsigned char ttl = 1;    // one network segment, like a Link session
		const unsigned char loop = 1;   // required: the peers may be on THIS host
		(void)::setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
		(void)::setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
		const int flags = ::fcntl(fd, F_GETFL, 0);
		if (flags >= 0)
		{
			(void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
		}
		sockaddr_in group{};
		group.sin_family = AF_INET;
		group.sin_addr.s_addr = ::inet_addr(link::DefaultGroup);
		group.sin_port = htons(link::DefaultPort);
		m_group = group;

		if (!measureLoopback(fd))
		{
			m_reason = QStringLiteral("%1 cannot be received on this host: a datagram sent to %2 "
				"was not read by a SECOND socket here that bound the port and joined the group "
				"within %3 ms, so a session cannot carry announcements here even though the socket "
				"was configured. The sync model and its commands are still present "
				"(docs/LINK-SYNC.md section 3)")
				.arg(QString::fromLatin1(link::DefaultGroup), endpointString())
				.arg(LoopbackProbeBoundMs);
			return false;
		}
		return true;
	}

	//! Read every datagram waiting, in arrival order, each stamped with this
	//! process's monotone clock. A truncated or foreign payload is still
	//! delivered: the MODEL is what decides whether it is one of ours.
	void drain()
	{
		char buffer[DatagramBufferBytes];
		while (m_fd >= 0)
		{
			const ssize_t read = ::recv(m_fd, buffer, sizeof(buffer), 0);
			if (read <= 0)
			{
				// EAGAIN/EWOULDBLOCK is the normal end of a drain; a real error
				// and a zero-length datagram both end it too.
				return;
			}
			if (m_receiver)
			{
				m_receiver(QByteArray(buffer, static_cast<int>(read)),
					LinkSyncEngine::monotonicMicros());
			}
		}
	}

	int m_fd = -1;
	QSocketNotifier* m_notifier = nullptr;
	Receiver m_receiver;
	QString m_reason{QStringLiteral("not started (link.set_enabled starts it)")};
	sockaddr_in m_group{};
	//! The receive measurement `available()` is derived from (start()).
	LoopbackProbe m_probe;
};

#else // Q_OS_WIN and anything else without the POSIX datagram API

/*! The unavailable transport. It reports WHY rather than being absent, so that
 *  a Windows reader of `link.get_state` sees a stated gap instead of an empty
 *  session that looks like nobody is there. */
class LinkUdpTransport : public QObject, public LinkPeerTransport
{
public:
	explicit LinkUdpTransport(QObject* parent = nullptr) : QObject(parent) {}

	bool start() override { return false; }
	void stop() override {}
	bool available() const override { return false; }
	QString reason() const override
	{
		return QStringLiteral("this platform has no POSIX datagram sockets in this build, so "
			"announcements cannot travel; the sync model and its commands are present "
			"(docs/LINK-SYNC.md section 5)");
	}
	QString endpoint() const override { return endpointString(); }
	void send(const QByteArray&) override {}
	void setReceiver(Receiver) override {}
};

#endif

} // namespace

LinkPeerTransport* createLinkUdpTransport(QObject* parent)
{
	return new LinkUdpTransport(parent);
}

} // namespace lmms
