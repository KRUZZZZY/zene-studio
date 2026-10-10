/*
 * CollabInvite.cpp - the one copyable string a collaboration host hands out
 *                    (0.5.0 F6, WF-050/052)
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

#include "CollabInvite.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

namespace lmms::collab
{

namespace
{

constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
constexpr char Prefix[] = "ZENE1-";
constexpr quint8 Version = 1;
constexpr quint8 Ipv6Flag = 0x80;
constexpr int ChecksumBytes = 4;

QByteArray checksumOf(const QByteArray& body)
{
	return QCryptographicHash::hash(body, QCryptographicHash::Sha256).left(ChecksumBytes);
}

int alphabetIndex(QChar c)
{
	const char upper = static_cast<char>(c.toUpper().toLatin1());
	for (int i = 0; i < 32; ++i)
	{
		if (Alphabet[i] == upper) { return i; }
	}
	return -1;
}

bool skippable(QChar c)
{
	return c == QLatin1Char('-') || c.isSpace();
}

QByteArray addressBytes(const QHostAddress& address, bool* v6)
{
	QByteArray bytes;
	bool isV4 = false;
	const quint32 v4 = address.toIPv4Address(&isV4);
	*v6 = !isV4;
	if (isV4)
	{
		for (int shift = 24; shift >= 0; shift -= 8) { bytes.append(static_cast<char>((v4 >> shift) & 0xff)); }
		return bytes;
	}
	const Q_IPV6ADDR v6addr = address.toIPv6Address();
	return QByteArray(reinterpret_cast<const char*>(v6addr.c), 16);
}

QHostAddress addressFrom(const QByteArray& bytes)
{
	if (bytes.size() == 4)
	{
		const auto* b = reinterpret_cast<const uchar*>(bytes.constData());
		return QHostAddress((quint32(b[0]) << 24) | (quint32(b[1]) << 16) | (quint32(b[2]) << 8) | quint32(b[3]));
	}
	return QHostAddress(reinterpret_cast<const quint8*>(bytes.constData()));
}

//! Splits a checked body into its fields; false when its length does not match its flags.
bool readBody(const QByteArray& body, Invite* out, QString* error)
{
	const auto flags = static_cast<quint8>(body.at(1));
	const int addressLength = (flags & Ipv6Flag) ? 16 : 4;
	if (body.size() != 2 + addressLength + 2 + InviteKeyBytes)
	{
		*error = QStringLiteral("this invite has the wrong length; copy it again, whole");
		return false;
	}
	if (!roleFromName(roleName(static_cast<CollabRole>(flags & 0x03)), &out->role))
	{
		*error = QStringLiteral("this invite names a role this version does not know");
		return false;
	}
	const auto* port = reinterpret_cast<const uchar*>(body.constData() + 2 + addressLength);
	const auto decodedPort = static_cast<quint16>((port[0] << 8) | port[1]);
	if (decodedPort == 0)
	{
		*error = QStringLiteral("this invite has an invalid port");
		return false;
	}
	out->address = addressFrom(body.mid(2, addressLength));
	out->port = decodedPort;
	out->key = body.right(InviteKeyBytes);
	return true;
}

} // namespace

QString base32Encode(const QByteArray& bytes)
{
	QString out;
	quint32 buffer = 0;
	int bits = 0;
	for (const char byte : bytes)
	{
		buffer = (buffer << 8) | static_cast<quint8>(byte);
		bits += 8;
		while (bits >= 5)
		{
			out.append(QLatin1Char(Alphabet[(buffer >> (bits - 5)) & 0x1f]));
			bits -= 5;
		}
	}
	if (bits > 0) { out.append(QLatin1Char(Alphabet[(buffer << (5 - bits)) & 0x1f])); }
	return out;
}

bool base32Decode(const QString& text, QByteArray* out)
{
	out->clear();
	quint32 buffer = 0;
	int bits = 0;
	for (const QChar c : text)
	{
		if (skippable(c)) { continue; }
		const int value = alphabetIndex(c);
		if (value < 0) { return false; }
		buffer = (buffer << 5) | static_cast<quint32>(value);
		bits += 5;
		if (bits >= 8)
		{
			out->append(static_cast<char>((buffer >> (bits - 8)) & 0xff));
			bits -= 8;
		}
	}
	return true;
}

QByteArray randomInviteKey()
{
	QByteArray key(InviteKeyBytes, Qt::Uninitialized);
	QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(key.data()), InviteKeyBytes / 4);
	return key;
}

QString Invite::encode() const
{
	bool v6 = false;
	const QByteArray addr = addressBytes(address, &v6);
	QByteArray body;
	body.append(static_cast<char>(Version));
	body.append(static_cast<char>(static_cast<quint8>(role) | (v6 ? Ipv6Flag : 0)));
	body.append(addr);
	body.append(static_cast<char>(port >> 8));
	body.append(static_cast<char>(port & 0xff));
	body.append(key.left(InviteKeyBytes));
	return QLatin1String(Prefix) + base32Encode(body + checksumOf(body));
}

bool Invite::decode(const QString& text, Invite* out, QString* error)
{
	const QString trimmed = text.trimmed();
	if (!trimmed.startsWith(QLatin1String(Prefix), Qt::CaseInsensitive))
	{
		*error = QStringLiteral("not a Zene invite: it starts with %1").arg(QLatin1String(Prefix));
		return false;
	}
	QByteArray raw;
	if (!base32Decode(trimmed.mid(int(sizeof(Prefix)) - 1), &raw) || raw.size() < 2 + ChecksumBytes)
	{
		*error = QStringLiteral("this invite has a character that is not in it; copy it again");
		return false;
	}
	const QByteArray body = raw.left(raw.size() - ChecksumBytes);
	if (checksumOf(body) != raw.right(ChecksumBytes))
	{
		*error = QStringLiteral("this invite's checksum does not match: a character is wrong or missing");
		return false;
	}
	if (static_cast<quint8>(body.at(0)) != Version)
	{
		*error = QStringLiteral("this invite was made by a different version of Zene Studio");
		return false;
	}
	return readBody(body, out, error);
}

} // namespace lmms::collab
