/*
 * ControlBrowserSupport.h - what the browser.* group's two translation units
 *                           share: the wire form of an item and its metadata,
 *                           the schemas both halves build their results from,
 *                           and the argument readers that turn a junk argument
 *                           into a typed refusal.
 *
 * The group is split read/mutating the way automation.* and warp.* are
 * (ControlCommandsAutomation.cpp / ...AutomationEdit.cpp), because this fork's
 * file-length ratchet measures a file as a unit and the whole group in one file
 * would run past it.
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

#ifndef LMMS_CONTROL_BROWSER_SUPPORT_H
#define LMMS_CONTROL_BROWSER_SUPPORT_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "BrowserPeakCache.h"
#include "ControlVocabulary.h" // the shared schema + id vocabulary

namespace lmms
{

struct BrowserItem;
struct BrowserMetadata;
struct BrowserQuery;
struct BrowserQueryResult;
struct BrowserRoot;
struct ControlResult;

namespace control
{

//! The tag a file may carry is a short label, not a document: this is the bound
//! the arguments reader enforces, and it is the same number the schema states.
constexpr int MaxTagLength = 64;
//! The distinct tags one library may hold. Past this the answer is a refusal
//! with the reason, not a silent truncation of somebody's library.
constexpr int MaxDistinctTags = 4096;

//! One probed audio file as the wire reports it.
QJsonObject browserMetadataJson(const BrowserMetadata& metadata);
//! One browsed item; \a withMetadata is false for a query that did not probe.
QJsonObject browserItemJson(const BrowserItem& item, bool withMetadata);
//! One root, as browser.roots reports it.
QJsonObject browserRootJson(const BrowserRoot& root);
//! One peak bucket.
QJsonObject browserPeakJson(const BrowserPeak& peak);
//! The peak cache's own state: what a client reads to see the cache working.
QJsonObject browserPeakCacheJson(const BrowserPeakCache& cache);

//! The metadata object schema.
QJsonObject browserMetadataSchema();
//! The item object schema, with or without the metadata property.
QJsonObject browserItemSchema(bool withMetadata);
//! `{"type":"array","items":<item>}` - the items property of a query result.
QJsonObject browserItemsProperty(bool withMetadata);
//! The `{"type":"array"}` property a tag list uses.
QJsonObject browserStringListProperty();

/*! Reads the required `path` argument and canonicalises it.
 *
 * Every refusal here is typed and nothing is read from the filesystem: an empty
 * or relative argument is `invalid_args` (the browser addresses files by
 * absolute path, and a relative one would resolve against whatever the process
 * happens to have as its working directory), and a path that names no file is
 * `not_found`.
 */
bool readBrowserFileArgument(const QJsonObject& args, QString* key, ControlResult* error);

/*! Reads `path` and `tag` for the two mutating verbs, enforcing the tag bound.
 *  The file must exist: the store keys on the canonical path, so a tag on a
 *  path that is not there would be filed under a name no query can ever list.
 */
bool readBrowserTagArguments(const QJsonObject& args, QString* key, QString* tag,
	ControlResult* error);

//! Reads the optional query arguments into \a query; typed on a bad kind.
bool readBrowserQueryArguments(const QJsonObject& args, BrowserQuery* query, ControlResult* error);

//! The tags of one file plus the store's location: the read-back both tag
//! verbs return, so a caller never has to guess what the store became.
QJsonObject browserTagState(const QString& key, const QStringList& tags);

/*! The SPEC A16 transaction for a tag edit.
 *
 * The tag store is a user-config file and not a JournallingObject, so no
 * ProjectJournal checkpoint can hold it: the inverse is the PAIRED COMMAND
 * (`applies: "command"`), which control.undo dispatches through the registry.
 * The before-state carries the tag list the edit started from, so the record
 * says what the file looked like and a reader can restore it by hand.
 */
QJsonObject browserTagTransaction(const QString& key, const QString& tag,
	const QStringList& before, const QString& inverseOp, const QString& mechanism);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_BROWSER_SUPPORT_H
