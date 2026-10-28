/*
 * OscBridge.cpp - OSC 1.0 messages to and from registered commands (Q050-205)
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

#include "OscBridge.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include <QJsonDocument>
#include <QSet>
#include <QtEndian>

#include "CollabPolicy.h"
#include "ControlRegistry.h"

namespace lmms::osc
{

namespace
{

//! Bytes needed to pad @a size up to the next multiple of four (OSC's alignment).
int padded(int size)
{
	return (size + 3) & ~3;
}

//! Reads the NUL-terminated, 4-aligned string at *offset; false when it is unterminated or runs off the end.
bool readString(const QByteArray& packet, int* offset, QString* out)
{
	const int start = *offset;
	const int end = packet.indexOf('\0', start);
	if (start >= packet.size() || end < 0) { return false; }
	*out = QString::fromUtf8(packet.constData() + start, end - start);
	*offset = padded(end + 1);
	return *offset <= packet.size();
}

bool readUint32(const QByteArray& packet, int* offset, std::uint32_t* out)
{
	if (*offset + 4 > packet.size()) { return false; }
	*out = qFromBigEndian<std::uint32_t>(reinterpret_cast<const uchar*>(packet.constData() + *offset));
	*offset += 4;
	return true;
}

bool readUint64(const QByteArray& packet, int* offset, std::uint64_t* out)
{
	if (*offset + 8 > packet.size()) { return false; }
	*out = qFromBigEndian<std::uint64_t>(reinterpret_cast<const uchar*>(packet.constData() + *offset));
	*offset += 8;
	return true;
}

//! A 32-bit argument (i or f) at *offset, appended to @a args.
bool readWord(const QByteArray& packet, bool isFloat, int* offset, QJsonArray* args)
{
	std::uint32_t word = 0;
	if (!readUint32(packet, offset, &word)) { return false; }
	if (!isFloat)
	{
		args->append(static_cast<double>(static_cast<std::int32_t>(word)));
		return true;
	}
	float value = 0.0f;
	std::memcpy(&value, &word, sizeof(value));
	args->append(static_cast<double>(value));
	return true;
}

//! A 64-bit argument (h or d) at *offset, appended to @a args.
bool readWide(const QByteArray& packet, bool isDouble, int* offset, QJsonArray* args)
{
	std::uint64_t wide = 0;
	if (!readUint64(packet, offset, &wide)) { return false; }
	if (!isDouble)
	{
		args->append(static_cast<double>(static_cast<std::int64_t>(wide)));
		return true;
	}
	double value = 0.0;
	std::memcpy(&value, &wide, sizeof(value));
	args->append(value);
	return true;
}

//! One argument of tag @a tag at *offset, appended to @a args; false with *error when it cannot be read.
bool readArgument(const QByteArray& packet, QChar tag, int* offset, QJsonArray* args, QString* error)
{
	static const QString valueless = QStringLiteral("TFN");
	static const QString strings = QStringLiteral("sS");
	static const QString words = QStringLiteral("if");
	static const QString wides = QStringLiteral("hd");
	const int constant = valueless.indexOf(tag);
	if (constant >= 0)
	{
		args->append(constant == 2 ? QJsonValue(QJsonValue::Null) : QJsonValue(constant == 0));
		return true;
	}
	bool read = false;
	QString text;
	if (strings.contains(tag))
	{
		read = readString(packet, offset, &text);
		if (read) { args->append(text); }
	}
	else if (words.contains(tag)) { read = readWord(packet, tag == QLatin1Char('f'), offset, args); }
	else if (wides.contains(tag)) { read = readWide(packet, tag == QLatin1Char('d'), offset, args); }
	else
	{
		*error = QStringLiteral("unsupported OSC argument type '%1' (supported: i f h d s T F N)").arg(tag);
		return false;
	}
	if (!read) { *error = QStringLiteral("malformed OSC packet: argument '%1' runs past the end").arg(tag); }
	return read;
}

void appendString(QByteArray* out, const QString& text)
{
	const QByteArray bytes = text.toUtf8();
	out->append(bytes);
	out->append('\0');
	while (out->size() % 4 != 0) { out->append('\0'); }
}

void appendUint32(QByteArray* out, std::uint32_t value)
{
	uchar bytes[4];
	qToBigEndian<std::uint32_t>(value, bytes);
	out->append(reinterpret_cast<const char*>(bytes), 4);
}

//! @a number as i when it is a whole 32-bit value, else as f.
void appendNumber(double number, QString* tags, QByteArray* data)
{
	if (std::floor(number) == number && number >= INT32_MIN && number <= INT32_MAX)
	{
		*tags += QLatin1Char('i');
		appendUint32(data, static_cast<std::uint32_t>(static_cast<std::int32_t>(number)));
		return;
	}
	*tags += QLatin1Char('f');
	const float single = static_cast<float>(number);
	std::uint32_t word = 0;
	std::memcpy(&word, &single, sizeof(word));
	appendUint32(data, word);
}

//! One JSON value as an OSC argument: its tag onto @a tags, its bytes onto @a data.
void appendValue(const QJsonValue& value, QString* tags, QByteArray* data)
{
	if (value.isBool()) { *tags += value.toBool() ? QLatin1Char('T') : QLatin1Char('F'); }
	else if (value.isNull() || value.isUndefined()) { *tags += QLatin1Char('N'); }
	else if (value.isDouble()) { appendNumber(value.toDouble(), tags, data); }
	else
	{
		*tags += QLatin1Char('s');
		appendString(data, value.isString() ? value.toString()
			: QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact)));
	}
}

//! The address and the type tags (without the comma) of the message in @a packet; *offset after them.
bool readHeader(const QByteArray& packet, int* offset, QString* address, QString* tags, QString* error)
{
	if (!readString(packet, offset, address))
	{
		*error = QStringLiteral("malformed OSC packet: the address is not terminated");
		return false;
	}
	if (*offset < packet.size())
	{
		if (!readString(packet, offset, tags) || !tags->startsWith(QLatin1Char(',')))
		{
			*error = QStringLiteral("malformed OSC packet: the type tag string is missing or does not start with ','");
			return false;
		}
		tags->remove(0, 1);
	}
	if (tags->size() > MaxArguments)
	{
		*error = QStringLiteral("too many OSC arguments (%1; the limit is %2)").arg(tags->size()).arg(MaxArguments);
		return false;
	}
	return true;
}

QString capped(const QString& text)
{
	return text.size() > MaxMessageChars ? text.left(MaxMessageChars - 3) + QStringLiteral("...") : text;
}

Exchange errorExchange(const QString& subject, const QString& kind, const QString& message, const QString& command = {})
{
	Exchange out;
	out.reply = encode(addressPrefix() + QStringLiteral("error"),
		QJsonArray{capped(subject), kind, capped(message)});
	out.command = command;
	out.errorKind = kind;
	return out;
}

//! The groups and ids no OSC request runs, whatever the scope: code, sessions, crash and telemetry uploads,
//! the server itself, and quitting the application.
bool alwaysRefused(const QString& id)
{
	static const QSet<QString> groups{QStringLiteral("script"), QStringLiteral("wasm"), QStringLiteral("livecode"),
		QStringLiteral("osc"), QStringLiteral("collab"), QStringLiteral("crash"), QStringLiteral("telemetry"),
		QStringLiteral("feedback")};
	return groups.contains(id.section(QLatin1Char('.'), 0, 0)) || id == QLatin1String("control.quit")
		|| id.startsWith(QLatin1String("controller.script_"));
}

} // namespace

QString addressPrefix()
{
	return QStringLiteral("/zene/");
}

QString scopeName(Scope scope)
{
	return scope == Scope::Full ? QStringLiteral("full") : QStringLiteral("remote");
}

bool scopeFromName(const QString& name, Scope* scope)
{
	if (name == QLatin1String("remote")) { *scope = Scope::Remote; return true; }
	if (name == QLatin1String("full")) { *scope = Scope::Full; return true; }
	return false;
}

bool decode(const QByteArray& packet, Message* out, QString* error)
{
	if (packet.size() > MaxPacketBytes)
	{
		*error = QStringLiteral("the packet is %1 bytes; the limit is %2").arg(packet.size()).arg(MaxPacketBytes);
		return false;
	}
	if (packet.startsWith("#bundle"))
	{
		*error = QStringLiteral("OSC bundles are not supported: send one message per datagram");
		return false;
	}
	if (packet.isEmpty() || packet.at(0) != '/' || packet.size() % 4 != 0)
	{
		*error = QStringLiteral("malformed OSC packet: not a message (an address starting with '/', 4-byte aligned)");
		return false;
	}
	int offset = 0;
	Message message;
	QString tags;
	if (!readHeader(packet, &offset, &message.address, &tags, error)) { return false; }
	for (const QChar tag : tags)
	{
		if (!readArgument(packet, tag, &offset, &message.args, error)) { return false; }
	}
	message.typeTags = tags;
	*out = message;
	return true;
}

QByteArray encode(const QString& address, const QJsonArray& args)
{
	QByteArray out;
	appendString(&out, address);
	QString tags = QStringLiteral(",");
	QByteArray data;
	for (const QJsonValue& value : args) { appendValue(value, &tags, &data); }
	appendString(&out, tags);
	out.append(data);
	return out;
}

bool argumentsToJson(const QJsonArray& args, QJsonObject* out, QString* error)
{
	*out = QJsonObject();
	if (args.isEmpty()) { return true; }
	if (args.size() == 1 && args.at(0).isString())
	{
		QJsonParseError parse;
		const QJsonDocument document = QJsonDocument::fromJson(args.at(0).toString().toUtf8(), &parse);
		if (parse.error != QJsonParseError::NoError || !document.isObject())
		{
			*error = QStringLiteral("a single string argument must be the JSON object of the command's arguments");
			return false;
		}
		*out = document.object();
		return true;
	}
	if (args.size() % 2 != 0)
	{
		*error = QStringLiteral("arguments must be key/value pairs (a string key, then its value)");
		return false;
	}
	for (int i = 0; i < args.size(); i += 2)
	{
		if (!args.at(i).isString() || args.at(i).toString().isEmpty())
		{
			*error = QStringLiteral("argument %1 must be a string key").arg(i);
			return false;
		}
		out->insert(args.at(i).toString(), args.at(i + 1));
	}
	return true;
}

QString scopeRefusal(const QString& id, Scope scope)
{
	if (alwaysRefused(id)) { return QStringLiteral("'%1' is not available over OSC").arg(id); }
	if (scope == Scope::Remote && !collab::remoteAllowList().contains(id))
	{
		return QStringLiteral("'%1' is not on the remote scope's allow-list (transport, markers, mixer faders and state); "
			"start the server with scope 'full' on a loopback address for every command").arg(id);
	}
	return QString();
}

Exchange handleRequest(const QByteArray& packet, Scope scope)
{
	Message message;
	QString error;
	if (!decode(packet, &message, &error)) { return errorExchange(QStringLiteral("?"), QStringLiteral("malformed"), error); }
	if (!message.address.startsWith(addressPrefix()))
	{
		return errorExchange(message.address, QStringLiteral("unsupported"),
			QStringLiteral("unsupported address: requests are %1<command-id>").arg(addressPrefix()));
	}
	const QString id = message.address.mid(addressPrefix().size());
	ControlRegistry* registry = ControlRegistry::instance();
	if (registry == nullptr || !registry->hasCommand(id))
	{
		return errorExchange(message.address, controlErrorKindName(ControlErrorKind::NotFound),
			QStringLiteral("no registered command '%1'").arg(id));
	}
	const QString refusal = scopeRefusal(id, scope);
	if (!refusal.isEmpty()) { return errorExchange(id, controlErrorKindName(ControlErrorKind::Refused), refusal, id); }
	QJsonObject args;
	if (!argumentsToJson(message.args, &args, &error))
	{
		return errorExchange(id, controlErrorKindName(ControlErrorKind::InvalidArgs), error, id);
	}
	if (collab::hasPathLikeArgument(args))
	{
		return errorExchange(id, controlErrorKindName(ControlErrorKind::Refused),
			QStringLiteral("an OSC request may not name a file on this machine"), id);
	}
	ControlResult result = registry->invoke(id, args);
	if (!result.ok)
	{
		return errorExchange(id, controlErrorKindName(result.errorKind), result.errorMessage, id);
	}
	result.result.remove(QStringLiteral("__transaction"));
	Exchange out;
	out.reply = encode(addressPrefix() + QStringLiteral("reply"),
		QJsonArray{id, QString::fromUtf8(QJsonDocument(result.result).toJson(QJsonDocument::Compact))});
	if (out.reply.size() > MaxReplyBytes)
	{
		return errorExchange(id, QStringLiteral("too_large"), QStringLiteral("the result is %1 bytes; an OSC reply is "
			"at most %2 - read it over the control socket").arg(out.reply.size()).arg(MaxReplyBytes), id);
	}
	out.ok = true;
	out.command = id;
	return out;
}

} // namespace lmms::osc
