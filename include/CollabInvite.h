/*
 * CollabInvite.h - the one copyable string a collaboration host hands out
 *                  (0.5.0 F6, WF-050/052)
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

#ifndef LMMS_COLLAB_INVITE_H
#define LMMS_COLLAB_INVITE_H

#include <QByteArray>
#include <QHostAddress>
#include <QString>

#include "CollabPolicy.h"
#include "lmms_export.h"

namespace lmms::collab
{

//! Bytes in a role's pre-shared key: 128 random bits.
constexpr int InviteKeyBytes = 16;

/*! What an invite carries: where to connect, as which role, and that role's pre-shared key.
 *
 *  Wire form: "ZENE1-" then RFC 4648 base32 (no padding) of
 *  version(1) | flags(1: bits 0-1 role, bit 7 IPv6) | address(4 or 16) | port(2, big-endian) | key(16) |
 *  checksum(4: the first bytes of SHA-256 over everything before it).
 *  The checksum catches a mistyped or truncated paste before any connection is tried. The key IS the
 *  secret: an invite must be shared like a password, and nothing in this program logs one.
 */
struct LMMS_EXPORT Invite
{
	QHostAddress address;
	quint16 port = 0;
	CollabRole role = CollabRole::Editor;
	QByteArray key;  //!< InviteKeyBytes bytes

	QString encode() const;
	//! False with a human-readable \a error for anything that is not a valid invite.
	static bool decode(const QString& text, Invite* out, QString* error);
};

//! Base32 (RFC 4648 alphabet, upper case, no padding) - exposed for the codec test.
LMMS_EXPORT QString base32Encode(const QByteArray& bytes);
//! Accepts upper or lower case and ignores '-' and whitespace; false on any other character.
LMMS_EXPORT bool base32Decode(const QString& text, QByteArray* out);
//! A fresh random key (QRandomGenerator::system()).
LMMS_EXPORT QByteArray randomInviteKey();

} // namespace lmms::collab

#endif // LMMS_COLLAB_INVITE_H
