/*
 * ControlBrowserSupport.cpp - the browser.* group's shared wire form and its
 *                             argument readers.
 *
 * Every reader answers a typed refusal rather than an empty success, which is
 * what lets the whole registry be swept headlessly with junk arguments: an
 * empty `path`, a path that is not absolute, a tag longer than a label, an
 * unknown kind and an unknown root id each get their own message naming the
 * value that was refused.
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

#include "ControlBrowserSupport.h"

#include <algorithm>

#include <QFileInfo>
#include <QJsonArray>

#include "BrowserCatalog.h"
#include "ControlEdit.h" // transactionPayload (SPEC A16)
#include "ControlRegistry.h"

namespace lmms
{

namespace control
{

namespace
{

//! A tag must be a printable label: control characters would be invisible in a
//! list and unquotable in the JSON a client stores them in.
bool tagIsPrintable(const QString& tag)
{
	for (const QChar& character : tag)
	{
		if (!character.isPrint()) { return false; }
	}
	return true;
}

bool readTagValue(const QString& raw, QString* tag, const QString& where, ControlResult* error)
{
	const QString trimmed = raw.trimmed();
	if (trimmed.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("%1 is empty: a tag is a label, and an empty one cannot be "
				"found again").arg(where));
		return false;
	}
	if (trimmed.size() > MaxTagLength)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("%1 is %2 characters; the limit is %3")
				.arg(where).arg(trimmed.size()).arg(MaxTagLength));
		return false;
	}
	if (!tagIsPrintable(trimmed))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("%1 carries a non-printable character").arg(where));
		return false;
	}
	*tag = trimmed;
	return true;
}

bool readTagList(const QJsonValue& value, QStringList* tags, ControlResult* error)
{
	if (!value.isArray())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'tags' is not an array of tag strings"));
		return false;
	}
	for (const QJsonValue& entry : value.toArray())
	{
		if (!entry.isString())
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'tags' holds a value that is not a string"));
			return false;
		}
		QString tag;
		if (!readTagValue(entry.toString(), &tag, QStringLiteral("a 'tags' entry"), error))
		{
			return false;
		}
		if (!tags->contains(tag, Qt::CaseInsensitive)) { tags->append(tag); }
	}
	return true;
}

//! The optional directory a query walks instead of the roots.
bool readQueryPath(const QJsonObject& args, BrowserQuery* query, ControlResult* error)
{
	if (!args.contains(QStringLiteral("path"))) { return true; }
	const QString raw = args.value(QStringLiteral("path")).toString().trimmed();
	if (raw.isEmpty()) { return true; }
	const QFileInfo info(raw);
	if (!info.isAbsolute())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'path' must be absolute: '%1' would resolve against the "
				"process's working directory, which is not the browser's").arg(raw));
		return false;
	}
	if (!info.isDir())
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1 is not a directory").arg(raw));
		return false;
	}
	query->path = info.absoluteFilePath();
	return true;
}

bool readQueryRoot(const QJsonObject& args, BrowserQuery* query, ControlResult* error)
{
	const QString id = args.value(QStringLiteral("root")).toString().trimmed();
	if (id.isEmpty()) { return true; }
	if (browserRootById(id).id != id)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("'%1' names no browser root; browser.roots lists the ids").arg(id));
		return false;
	}
	query->root = id;
	return true;
}

} // namespace

QJsonObject browserMetadataJson(const BrowserMetadata& metadata)
{
	QJsonObject out;
	out.insert(QStringLiteral("valid"), metadata.valid);
	if (!metadata.valid)
	{
		out.insert(QStringLiteral("error"), metadata.error);
		return out;
	}
	out.insert(QStringLiteral("sample_rate"), metadata.sampleRate);
	out.insert(QStringLiteral("channels"), metadata.channels);
	out.insert(QStringLiteral("frames"), static_cast<double>(metadata.frames));
	out.insert(QStringLiteral("duration_seconds"), metadata.durationSeconds);
	out.insert(QStringLiteral("format"), metadata.format);
	out.insert(QStringLiteral("encoding"), metadata.encoding);
	// The embedded tags. Only the ones the file actually carries: a field that
	// is absent is a fact about the file, and writing "" for it would be a claim
	// that the file carries an empty tag.
	const QJsonObject tags{
		{QStringLiteral("title"), metadata.title},
		{QStringLiteral("artist"), metadata.artist},
		{QStringLiteral("album"), metadata.album},
		{QStringLiteral("comment"), metadata.comment},
		{QStringLiteral("genre"), metadata.genre},
		{QStringLiteral("date"), metadata.date},
		{QStringLiteral("software"), metadata.software},
		{QStringLiteral("copyright"), metadata.copyright},
	};
	QJsonObject present;
	for (auto it = tags.begin(); it != tags.end(); ++it)
	{
		if (!it.value().toString().isEmpty()) { present.insert(it.key(), it.value()); }
	}
	out.insert(QStringLiteral("tags"), present);
	return out;
}

QJsonObject browserItemJson(const BrowserItem& item, bool withMetadata)
{
	QJsonObject out;
	out.insert(QStringLiteral("path"), item.path);
	out.insert(QStringLiteral("name"), item.name);
	out.insert(QStringLiteral("kind"), browserKindName(item.kind));
	out.insert(QStringLiteral("size_bytes"), static_cast<double>(item.sizeBytes));
	out.insert(QStringLiteral("modified_ms"), static_cast<double>(item.modifiedMs));
	out.insert(QStringLiteral("tags"), QJsonArray::fromStringList(item.tags));
	if (withMetadata && item.hasMetadata)
	{
		out.insert(QStringLiteral("metadata"), browserMetadataJson(item.metadata));
	}
	return out;
}

QJsonObject browserRootJson(const BrowserRoot& root)
{
	QJsonObject out;
	out.insert(QStringLiteral("id"), root.id);
	out.insert(QStringLiteral("path"), root.path);
	out.insert(QStringLiteral("exists"), root.exists);
	return out;
}

QJsonObject browserPeakJson(const BrowserPeak& peak)
{
	QJsonObject out;
	out.insert(QStringLiteral("min"), static_cast<double>(peak.min));
	out.insert(QStringLiteral("max"), static_cast<double>(peak.max));
	return out;
}

QJsonObject browserPeakCacheJson(const BrowserPeakCache& cache)
{
	QJsonObject out;
	out.insert(QStringLiteral("entries"), cache.entryCount());
	out.insert(QStringLiteral("capacity"), BrowserPeakCache::Capacity);
	out.insert(QStringLiteral("base_buckets"), BrowserPeakCache::BaseBuckets);
	out.insert(QStringLiteral("hits"), cache.hitCount());
	out.insert(QStringLiteral("misses"), cache.missCount());
	return out;
}

QJsonObject browserMetadataSchema()
{
	return objectSchema({
		{QStringLiteral("valid"), booleanProperty()},
		{QStringLiteral("error"), stringProperty()},
		{QStringLiteral("sample_rate"), integerProperty(0, 4294967)},
		{QStringLiteral("channels"), integerProperty(0, 1024)},
		{QStringLiteral("frames"), numberProperty()},
		{QStringLiteral("duration_seconds"), numberProperty()},
		{QStringLiteral("format"), stringProperty()},
		{QStringLiteral("encoding"), stringProperty()},
		{QStringLiteral("tags"), objectProperty()},
	});
}

QJsonObject browserItemSchema(bool withMetadata)
{
	QJsonObject properties{
		{QStringLiteral("path"), stringProperty()},
		{QStringLiteral("name"), stringProperty()},
		{QStringLiteral("kind"), stringProperty()},
		{QStringLiteral("size_bytes"), numberProperty()},
		{QStringLiteral("modified_ms"), numberProperty()},
		{QStringLiteral("tags"), arrayProperty()},
	};
	if (withMetadata) { properties.insert(QStringLiteral("metadata"), browserMetadataSchema()); }
	return objectSchema(properties);
}

QJsonObject browserItemsProperty(bool withMetadata)
{
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
		{QStringLiteral("items"), browserItemSchema(withMetadata)}};
}

QJsonObject browserStringListProperty()
{
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
		{QStringLiteral("items"), stringProperty()}};
}

bool readBrowserFileArgument(const QJsonObject& args, QString* key, ControlResult* error)
{
	const QString raw = args.value(QStringLiteral("path")).toString().trimmed();
	if (raw.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'path' is required and is empty: the browser addresses a file by "
				"its absolute path"));
		return false;
	}
	if (!QFileInfo(raw).isAbsolute())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'path' must be absolute; '%1' would resolve against the process's "
				"working directory").arg(raw));
		return false;
	}
	const QString canonical = browserCanonicalKey(raw);
	if (!QFileInfo(canonical).isFile())
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1 is not a file (the browser addresses files, not directories)")
				.arg(canonical));
		return false;
	}
	*key = canonical;
	return true;
}

bool readBrowserTagArguments(const QJsonObject& args, QString* key, QString* tag,
	ControlResult* error)
{
	if (!readBrowserFileArgument(args, key, error)) { return false; }
	return readTagValue(args.value(QStringLiteral("tag")).toString(), tag,
		QStringLiteral("'tag'"), error);
}

bool readBrowserQueryArguments(const QJsonObject& args, BrowserQuery* query, ControlResult* error)
{
	query->text = args.value(QStringLiteral("text")).toString();
	query->probe = args.value(QStringLiteral("probe")).toBool(false);
	query->kind = args.value(QStringLiteral("kind")).toString().trimmed();
	if (args.contains(QStringLiteral("tags"))
		&& !readTagList(args.value(QStringLiteral("tags")), &query->tags, error))
	{
		return false;
	}
	if (!query->kind.isEmpty())
	{
		BrowserKind kind = BrowserKind::Other;
		if (!browserKindFromName(query->kind, &kind))
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'kind' is '%1'; it is one of audio, preset, project, midi, other")
					.arg(query->kind));
			return false;
		}
	}
	if (!readQueryRoot(args, query, error)) { return false; }
	if (!readQueryPath(args, query, error)) { return false; }

	// Clamped rather than refused: the schemas bound both, and a client that
	// sends a nonsense limit wants the prefix of a page, not a lesson.
	query->limit = qBound(1, args.value(QStringLiteral("limit")).toInt(25), 500);
	query->offset = std::max(0, args.value(QStringLiteral("offset")).toInt(0));
	return true;
}

QJsonObject browserTagState(const QString& key, const QStringList& tags)
{
	QJsonObject out;
	out.insert(QStringLiteral("path"), key);
	out.insert(QStringLiteral("tags"), QJsonArray::fromStringList(tags));
	out.insert(QStringLiteral("store_path"), BrowserTagStore::instance().storePath());
	return out;
}

QJsonObject browserTagTransaction(const QString& key, const QString& tag,
	const QStringList& before, const QString& inverseOp, const QString& mechanism)
{
	QJsonObject beforeState;
	beforeState.insert(QStringLiteral("path"), key);
	beforeState.insert(QStringLiteral("tags"), QJsonArray::fromStringList(before));
	beforeState.insert(QStringLiteral("tag"), tag);

	QJsonObject args;
	args.insert(QStringLiteral("path"), key);
	args.insert(QStringLiteral("tag"), tag);

	QJsonObject transaction = transactionPayload(beforeState, inverseOp, args, true, mechanism);
	// `applies: command` is what makes control.undo dispatch the inverse through
	// the registry instead of unwinding the project journal, which holds nothing
	// for a file in the config directory. It is set here, not by
	// transactionPayload, whose default is the journal (SPEC A16).
	QJsonObject inverse = transaction.value(QStringLiteral("inverse")).toObject();
	inverse.insert(QStringLiteral("applies"), QStringLiteral("command"));
	transaction.insert(QStringLiteral("inverse"), inverse);
	return transaction;
}

} // namespace control

} // namespace lmms
