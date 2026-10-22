/*
 * LinkAudioSocket.cpp - the datagram socket of the link audio stream
 *                       (include/LinkAudioStream.h, docs/LINK-AUDIO.md).
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

// The socket half of the link audio stream: the OS calls live here, the stream half (the
// rings, the rates and the epochs) lives in LinkAudioStream.cpp.

#include "LinkAudioStream.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // windows.h must not define min/max over std::min/std::clamp
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace lmms::link
{

// ---- the socket layer: POSIX sockets, or winsock2 on Windows (unverified on Windows: not built here) ----

constexpr int IoPollMs = 2;
//! Datagrams read per wake: under a sustained flood the loop must come back to check the stop flag.
constexpr int MaxPacketsPerWake = 256;

namespace
{

#ifdef _WIN32
using Handle = SOCKET;
using SockLen = int;
using IoSize = int;
using IoLen = int;
constexpr Handle NoSocket = INVALID_SOCKET;
std::string lastErrorText() { return "WSA error " + std::to_string(WSAGetLastError()); }

//! Every successful networkReady() is paired with one networkRelease() (WSAStartup / WSACleanup are counted by winsock).
bool networkReady(std::string* error)
{
	WSADATA data;
	const int started = WSAStartup(MAKEWORD(2, 2), &data);
	if (started != 0 && error != nullptr) { *error = "WSAStartup failed: " + std::to_string(started); }
	return started == 0;
}
void networkRelease() { WSACleanup(); }
bool setNonBlocking(Handle h) { u_long on = 1; return ::ioctlsocket(h, FIONBIO, &on) == 0; }
void closeSocket(Handle h) { ::closesocket(h); }
void waitReadable(Handle h, int ms)
{
	fd_set set;
	FD_ZERO(&set);
	FD_SET(h, &set);
	timeval timeout{0, ms * 1000};
	::select(0, &set, nullptr, nullptr, &timeout);
}
#else
using Handle = int;
using SockLen = socklen_t;
using IoSize = ssize_t;
using IoLen = std::size_t;
constexpr Handle NoSocket = -1;
std::string lastErrorText() { return std::strerror(errno); }

bool networkReady(std::string*) { return true; }
void networkRelease() {}
bool setNonBlocking(Handle h)
{
	const int flags = ::fcntl(h, F_GETFL, 0);
	return flags >= 0 && ::fcntl(h, F_SETFL, flags | O_NONBLOCK) == 0;
}
void closeSocket(Handle h) { ::close(h); }
void waitReadable(Handle h, int ms)
{
	pollfd wait{h, POLLIN, 0};
	::poll(&wait, 1, ms);
}
#endif

constexpr std::uintptr_t NoStoredSocket = ~std::uintptr_t{0};   // INVALID_SOCKET on Windows, (uintptr_t)-1 for a POSIX fd

Handle handleOf(std::uintptr_t stored) { return static_cast<Handle>(stored); }

bool failWith(std::string* error, const std::string& what)
{
	if (error != nullptr) { *error = what + ": " + lastErrorText(); }
	return false;
}

} // namespace

bool LinkAudioStream::openSocket(const AudioStreamConfig& cfg, std::string* error)
{
	if (!networkReady(error)) { return false; }
	std::uint32_t bindAddress = 0;
	parseIpv4(cfg.bindAddress, bindAddress);   // configure() validated both literals
	if (!cfg.peerAddress.empty()) { parseIpv4(cfg.peerAddress, m_peerAddress); }
	const Handle fd = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (fd == NoSocket)
	{
		const bool ok = failWith(error, "socket()");
		networkRelease();
		return ok;
	}
	sockaddr_in local{};
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(bindAddress);
	local.sin_port = htons(static_cast<std::uint16_t>(cfg.receive ? cfg.listenPort : 0));
	const bool nonBlocking = setNonBlocking(fd);
	if (!nonBlocking || ::bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0)
	{
		const std::string what = !nonBlocking ? "non-blocking mode" : "bind " + cfg.bindAddress + ":" + std::to_string(cfg.listenPort);
		const std::string reason = lastErrorText();
		closeSocket(fd);
		networkRelease();
		m_peerAddress = 0;
		if (error != nullptr) { *error = what + ": " + reason; }
		return false;
	}
	SockLen length = sizeof(local);
	::getsockname(fd, reinterpret_cast<sockaddr*>(&local), &length);
	m_boundAddress = cfg.bindAddress;
	m_boundPort = ntohs(local.sin_port);
	m_socket = static_cast<std::uintptr_t>(fd);
	m_peerPort = cfg.peerPort;
	return true;
}

void LinkAudioStream::receiveDatagrams()
{
	for (int packets = 0; packets < MaxPacketsPerWake && !m_stop.load(std::memory_order_acquire); ++packets)
	{
		sockaddr_in from{};
		SockLen fromLength = sizeof(from);
		const IoSize got = ::recvfrom(handleOf(m_socket), reinterpret_cast<char*>(m_packet.data()),
			static_cast<IoLen>(m_packet.size()), 0, reinterpret_cast<sockaddr*>(&from), &fromLength);
		if (got < 0) { return; }  // would block: drained (or a transient error: wait again)
		if (m_peerAddress != 0 && ntohl(from.sin_addr.s_addr) != m_peerAddress)
		{
			m_malformed.fetch_add(1, std::memory_order_relaxed);  // from a stranger
			continue;
		}
		AudioPacketHeader header;
		if (!decodeAudioPacket(m_packet.data(), static_cast<std::size_t>(got), header,
				m_samples.data(), m_samples.size()))
		{
			m_malformed.fetch_add(1, std::memory_order_relaxed);
			continue;
		}
		acceptPacket(header, m_samples.data());
	}
}

void LinkAudioStream::sendPending()
{
	// The window in which the frames of the TX ring stay queued: a test can hold the I/O
	// thread here, so that a sample rate change happens while old-rate frames are waiting.
	LinkAudioStreamSeam::waitAtSendHold();
	sockaddr_in to{};
	to.sin_family = AF_INET;
	to.sin_addr.s_addr = htonl(m_peerAddress);
	to.sin_port = htons(static_cast<std::uint16_t>(m_peerPort));
	for (int packets = 0; packets < MaxPacketsPerWake && !m_stop.load(std::memory_order_acquire); ++packets)
	{
		const std::uint32_t rate = txRateForNextFrames();
		if (rate == 0) { return; }   // the engine's rate is not known yet: the frames wait for it
		const std::size_t available = m_tx.fill();
		if (available == 0) { return; }
		const std::size_t n = m_tx.read(m_samples.data(), std::min(available, AudioMaxPacketFrames));
		if (m_peerAddress == 0 || n == 0) { continue; }  // nowhere to send: drained and dropped
		AudioPacketHeader header;
		header.sequence = m_nextSequence++;
		header.sampleRate = rate;
		header.frames = static_cast<std::uint16_t>(n);
		const std::size_t bytes = encodeAudioPacket(header, m_samples.data(), m_packet.data(), m_packet.size());
		const IoSize sent = bytes == 0 ? -1
			: ::sendto(handleOf(m_socket), reinterpret_cast<const char*>(m_packet.data()), static_cast<IoLen>(bytes), 0,
				reinterpret_cast<sockaddr*>(&to), sizeof(to));
		if (sent < 0)
		{
			m_sendErrors.fetch_add(1, std::memory_order_relaxed);
			continue;
		}
		m_packetsSent.fetch_add(1, std::memory_order_relaxed);
		m_framesSent.fetch_add(n, std::memory_order_relaxed);
	}
}

//! The I/O thread's side of a sample rate change: it publishes the epoch it has drained, so
//! the audio thread knows that everything it publishes from now on carries the current rate.
//! The acknowledgement comes after the writes of the previous loop iteration, so the frames
//! it published before belong to the old rate - the audio thread drops them.

void LinkAudioStream::ioLoop()
{
	while (!m_stop.load(std::memory_order_acquire))
	{
		waitReadable(handleOf(m_socket), IoPollMs);
		acknowledgeRxRate();
		receiveDatagrams();
		sendPending();
	}
}

void LinkAudioStream::stopLocked()
{
	// Order matters: the audio thread stops reaching us first, then the I/O thread ends.
	m_running.store(false, std::memory_order_relaxed);
	if (s_active.load(std::memory_order_relaxed) == this) { s_active.store(nullptr, std::memory_order_release); }
	m_sendOn.store(false, std::memory_order_relaxed);
	m_receiveOn.store(false, std::memory_order_relaxed);
	m_stop.store(true, std::memory_order_release);
	if (m_thread.joinable()) { m_thread.join(); }
	if (m_socket != NoStoredSocket)
	{
		closeSocket(handleOf(m_socket));
		networkRelease();
	}
	m_socket = NoStoredSocket;
	m_peerAddress = 0;
}

} // namespace lmms::link
