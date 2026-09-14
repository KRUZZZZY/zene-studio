/*
 * ControlCommandsPluginScanEdit.cpp - the EDIT half of the plugin.* scan-cache
 *                                     surface (SPEC A11-A16).
 *
 * Feature row 46 of docs/FEATURE-LIST-0.3.0.md, the half that retires the
 * hand-edit route the audit named ("the documented quarantine route is
 * hand-editing a JSON file"): plugin.scan_cache_quarantine_add,
 * plugin.scan_cache_quarantine_remove and plugin.rescan. The read half is
 * ControlCommandsPluginScan.cpp; the shared vocabulary is
 * ControlCommandsPluginScanShared.h.
 *
 * WHICH HANDLERS ARE HERE AND WHAT EACH IS FOR
 *   plugin.scan_cache_quarantine_add     hide a plugin file from discovery, with a reason.
 *   plugin.scan_cache_quarantine_remove  stop hiding it.
 *   plugin.rescan                        run the scan the factory already has
 *                                        (PluginFactory::discoverPlugins, a public slot the
 *                                        GUI never re-invokes) - which is what APPLIES a
 *                                        quarantine edit and what fills the cache.
 *
 * A16, and why the class is `snapshot` rather than `true_inverse`. PluginScanCache
 * is NOT a JournallingObject: it holds a JSON file outside the project, so no
 * ProjectJournal checkpoint can capture it and there is nothing to
 * addJournalCheckPoint() on. The inverse is therefore a recorded PAIR of
 * commands: each verb names the other with the state THIS call displaced, and
 * control.undo dispatches it through the registry (`applies: command`). That is
 * the same class and mechanism browser.tag.add / browser.tag.remove already
 * carry for the same reason (src/core/ControlReversibilityTableSnapshot.cpp).
 *
 * THE TRAP, and where this group has its own version of it. A checkpoint
 * captures state BEFORE a write, so a feature the engine serialises only when it
 * is non-default cannot be taken back by restoring the pre-first-edit state.
 * Here the reason IS that feature: PluginScanCache stores one REASON per
 * quarantined path, and the reason is not derivable from anything else. An
 * inverse that recorded only the PATH would restore the entry with an EMPTY
 * reason and the file would come back hidden for an unknown cause - the state
 * would look restored and would not be. So quarantine_remove reads the reason
 * BEFORE the write and carries it in the inverse's args, and
 * tests/control-plugin-scan-commands.py measures the difference directly: it
 * drives the path-only add a naive inverse would make and asserts the reason is
 * LOST, then drives the recorded inverse and asserts it is restored exactly.
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

#include <QJsonArray>
#include <QJsonObject>

#include "ControlCommandsPluginScanShared.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"

#include "ControlVocabulary.h"
#include "PluginFactory.h"
#include "PluginScanCache.h"

namespace lmms
{

namespace
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

const QString QuarantineAddName = QStringLiteral("plugin.scan_cache_quarantine_add");
const QString QuarantineRemoveName = QStringLiteral("plugin.scan_cache_quarantine_remove");
const QString RescanName = QStringLiteral("plugin.rescan");

/*! The cache an EDIT may write to, or the typed refusal that says why not.
 *
 * The order matters: the cache must be addressable AND persistent BEFORE
 * anything is written, because the whole point of the group is that the file is
 * the durable record. With no file (`ConfigManager` has no working directory
 * yet, or the process was started with an empty LMMS_PLUGIN_SCAN_CACHE) an edit
 * could only live in memory, and the next scan calls PluginScanCache::load(),
 * which clears the in-memory list and re-reads the file - so an edit accepted
 * there would be forgotten at the next scan boundary. refused, and nothing
 * written.
 */
bool writableScanCache(PluginScanCache** cache, ControlResult* error, const QString& command)
{
	PluginScanCache* found = scanCacheOrNull();
	if (found == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1: this instance has no plugin factory").arg(command));
		return false;
	}
	if (!found->isPersistent())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1: this instance's scan cache has no file (LMMS_PLUGIN_SCAN_CACHE is unset "
				"and there is no working directory), so a quarantine entry could not be stored - and the "
				"next scan re-reads the file, which would drop it. Nothing was written").arg(command));
		return false;
	}
	*cache = found;
	return true;
}

/*! A transaction whose inverse is the PAIRED COMMAND (SPEC A16): the same shape
 *  ControlBrowserSupport.cpp builds for browser.tag.add / tag.remove, and for
 *  the same reason - the state lives in a file outside the project, so nothing
 *  on the engine's own stack describes it.
 */
QJsonObject pairedCommandTransaction(const QJsonObject& before, const QString& inverseOp,
	const QJsonObject& inverseArgs, const QString& mechanism)
{
	QJsonObject transaction = transactionPayload(before, inverseOp, inverseArgs, true, mechanism);
	QJsonObject inverse = transaction.value(QStringLiteral("inverse")).toObject();
	// `applies: command` is what makes control.undo dispatch the inverse through
	// the registry instead of unwinding the project journal, which holds nothing
	// for a plugin scan cache.
	inverse.insert(QStringLiteral("applies"), QStringLiteral("command"));
	transaction.insert(QStringLiteral("inverse"), inverse);
	return transaction;
}

//! The result body both quarantine verbs return: the list as it now stands.
QJsonObject quarantineState(const PluginScanCache& cache)
{
	QJsonObject out;
	out.insert(QStringLiteral("cache_file"), cache.filePath());
	out.insert(QStringLiteral("quarantine_count"), cache.quarantineCount());
	out.insert(QStringLiteral("quarantine"), quarantineReport(cache));
	out.insert(QStringLiteral("note"),
		QStringLiteral("a quarantine entry takes effect on the NEXT scan: the list is read at the top of "
			"PluginFactory::discoverPlugins, so run plugin.rescan to apply it now - until then the plugin "
			"browser still shows the plugin the entry hides"));
	return out;
}

//! The reason a quarantine edit is a snapshot: the class, in the row's words.
QString quarantineMechanism(const QString& inverseOp)
{
	return QStringLiteral("snapshot: the scan cache is a JSON file outside the project and is not a "
		"JournallingObject, so no ProjectJournal checkpoint can hold it and there is nothing to "
		"addJournalCheckPoint() on. The inverse is the paired COMMAND %1 (applies=command), dispatched by "
		"control.undo through the registry, with the state this call displaced in the transaction's "
		"before-state").arg(inverseOp);
}

ControlResult handleQuarantineAdd(const QJsonObject& args)
{
	const QString path = args.value(QStringLiteral("path")).toString();
	const QString reason = args.value(QStringLiteral("reason")).toString();
	if (path.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("%1 needs 'path': the plugin file to hide, as an absolute path - the quarantine "
				"list stores paths, and a scan matches a candidate file's absolute path against it")
				.arg(QuarantineAddName));
	}

	PluginScanCache* cache = nullptr;
	ControlResult error;
	if (!writableScanCache(&cache, &error, QuarantineAddName)) { return error; }

	// Validated BEFORE anything is written: PluginScanCache::addToQuarantine()
	// returns without writing when the path is already listed (one entry per
	// path, one reason), so accepting this call would report a change that did
	// not happen and would record an inverse for a write that never occurred.
	if (cache->isQuarantined(path))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1: %2 is already quarantined (reason: '%3'). The engine keeps ONE reason per "
				"path and does not overwrite it, so this call would change nothing; remove the entry first "
				"(%4) to change the reason").arg(QuarantineAddName, path,
				cache->quarantineReason(path), QuarantineRemoveName));
	}

	cache->addToQuarantine(path, reason);
	if (!cache->save())
	{
		// Rolled back: see writableScanCache() - an edit that is only in memory
		// is dropped at the next scan boundary, and reporting success for it
		// would be a lie about a boundary the caller cannot see.
		cache->removeFromQuarantine(path);
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("%1: the scan cache %2 could not be written, so the entry was rolled back and "
				"the quarantine list is unchanged").arg(QuarantineAddName, cache->filePath()));
	}

	QJsonObject result = quarantineState(*cache);
	result.insert(QStringLiteral("path"), path);
	result.insert(QStringLiteral("reason"), reason);
	result.insert(QStringLiteral("quarantined"), true);
	result.insert(QStringLiteral("action"), QStringLiteral("added"));

	QJsonObject before;
	before.insert(QStringLiteral("path"), path);
	before.insert(QStringLiteral("quarantined"), false);
	before.insert(QStringLiteral("quarantine_count"), cache->quarantineCount() - 1);
	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("path"), path);
	result.insert(QStringLiteral("__transaction"), pairedCommandTransaction(before,
		QuarantineRemoveName, inverseArgs,
		quarantineMechanism(QuarantineRemoveName)));
	return ControlResult::success(result);
}

ControlResult handleQuarantineRemove(const QJsonObject& args)
{
	const QString path = args.value(QStringLiteral("path")).toString();
	if (path.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("%1 needs 'path': the plugin file to un-hide, as an absolute path")
				.arg(QuarantineRemoveName));
	}

	PluginScanCache* cache = nullptr;
	ControlResult error;
	if (!writableScanCache(&cache, &error, QuarantineRemoveName)) { return error; }
	if (!cache->isQuarantined(path))
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1: %2 is not in the quarantine list, so there was nothing to remove and "
				"nothing was written").arg(QuarantineRemoveName, path));
	}

	// THE TRAP'S OWN LINE: the reason is read BEFORE the write, because it is
	// the one part of the entry that no other state can reconstruct. An inverse
	// carrying only the path would restore the entry with an empty reason.
	const QString reason = cache->quarantineReason(path);
	const int entriesBefore = cache->quarantineCount();

	cache->removeFromQuarantine(path);
	if (!cache->save())
	{
		cache->addToQuarantine(path, reason);
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("%1: the scan cache %2 could not be written, so the removal was rolled back and "
				"the entry is still quarantined").arg(QuarantineRemoveName, cache->filePath()));
	}

	QJsonObject result = quarantineState(*cache);
	result.insert(QStringLiteral("path"), path);
	result.insert(QStringLiteral("reason"), reason);
	result.insert(QStringLiteral("quarantined"), false);
	result.insert(QStringLiteral("action"), QStringLiteral("removed"));

	QJsonObject before;
	before.insert(QStringLiteral("path"), path);
	before.insert(QStringLiteral("quarantined"), true);
	before.insert(QStringLiteral("reason"), reason);
	before.insert(QStringLiteral("quarantine_count"), entriesBefore);
	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("path"), path);
	inverseArgs.insert(QStringLiteral("reason"), reason);
	result.insert(QStringLiteral("__transaction"), pairedCommandTransaction(before,
		QuarantineAddName, inverseArgs,
		QStringLiteral("%1. The REASON is carried in the inverse's args because the engine stores one per "
			"path and no other state reconstructs it: a path-only re-add would bring the entry back with an "
			"EMPTY reason, which looks restored and is not (tests/control-plugin-scan-commands.py measures "
			"that loss directly)").arg(quarantineMechanism(QuarantineAddName))));
	return ControlResult::success(result);
}

//! The scan's own numbers, after a run, plus what the run did to the cache file.
QJsonObject rescanReport(const PluginFactory& factory)
{
	QJsonObject result = scanStatsJson(factory);
	result.insert(QStringLiteral("cache_file"), factory.scanCache().filePath());
	result.insert(QStringLiteral("cache_entry_count"), factory.scanCache().fileCount());
	result.insert(QStringLiteral("cache_quarantine_count"), factory.scanCache().quarantineCount());
	result.insert(QStringLiteral("cache_dirty"), factory.scanCache().isDirty());
	result.insert(QStringLiteral("quarantined_entries"), quarantineReport(factory.scanCache()));
	return result;
}

/*! plugin.rescan - run PluginFactory::discoverPlugins(), the scan the engine
 *  already has (a public slot nothing in the shipped GUI re-invokes: the
 *  discovery happens once, in the factory's constructor).
 *
 * A16 is `irreversible`, and this is the honest reading rather than a
 * pessimistic one: a scan re-MEASURES the files on disk and refreshes the cache,
 * and no command puts a file's PREVIOUS fingerprint record back once a scan has
 * replaced it. The fallback is the one the cache's own design leaves (restore
 * the library and scan again). Nothing in the project is touched, so there is
 * nothing there to put back.
 */
ControlResult handleRescan()
{
	PluginFactory* factory = pluginFactoryOrNull();
	if (factory == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1: this instance has no plugin factory").arg(RescanName));
	}

	const QJsonObject before = rescanReport(*factory);
	// The scan itself. discoverPlugins() resets its own stats, re-reads the
	// cache file (so an edited quarantine list applies without a restart) and
	// only writes the file when something actually changed.
	factory->discoverPlugins();

	QJsonObject result = rescanReport(*factory);
	result.insert(QStringLiteral("previous"), before);
	result.insert(QStringLiteral("__transaction"),
		QJsonObject{{QStringLiteral("before"), before},
			{QStringLiteral("reversible"), false},
			{QStringLiteral("mechanism"),
				QStringLiteral("none: a scan re-measures the files on disk and refreshes the cache, and no "
					"command restores a file's PREVIOUS fingerprint record once a scan has replaced it. The "
					"cache is derived state - PluginScanCache's own contract is that discovery never depends on "
					"it, only on how much work a scan repeats - so the fallback restores nothing that was lost: "
					"put the library file back and scan again, and its fingerprint is recorded anew. The project "
					"is not touched by any of this")}});
	return ControlResult::success(result);
}

} // namespace

void registerPluginScanEditCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("plugin.scan_cache_quarantine_add");
		cmd.group = QStringLiteral("plugin");
		cmd.verb = QStringLiteral("scan_cache_quarantine_add");
		cmd.description = QStringLiteral("Quarantine a plugin file: add it to the scan cache's skip list, "
			"with a reason, and write the cache file. A quarantined file is not loaded and not offered to the "
			"device catalogue on the next scan (plugin.rescan applies it). ONE entry per path: adding a path "
			"that is already listed is refused rather than silently ignored. Reversible - control.undo "
			"dispatches the recorded plugin.scan_cache_quarantine_remove.");
		cmd.mutating = true;
		cmd.argsSchema = objectSchema({
			{QStringLiteral("path"), stringProperty()},
			{QStringLiteral("reason"), stringProperty()},
		}, {QStringLiteral("path")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("path"), stringProperty()},
			{QStringLiteral("reason"), stringProperty()},
			{QStringLiteral("quarantined"), booleanProperty()},
			{QStringLiteral("action"), stringProperty()},
			{QStringLiteral("cache_file"), stringProperty()},
			{QStringLiteral("quarantine_count"), integerProperty()},
			{QStringLiteral("quarantine"), arrayProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject& args) { return handleQuarantineAdd(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("plugin.scan_cache_quarantine_remove");
		cmd.group = QStringLiteral("plugin");
		cmd.verb = QStringLiteral("scan_cache_quarantine_remove");
		cmd.description = QStringLiteral("Un-quarantine a plugin file: drop it from the scan cache's skip "
			"list, write the cache file, and report the reason the entry carried. The next scan loads the "
			"file again. Reversible - control.undo dispatches the recorded plugin.scan_cache_quarantine_add "
			"WITH the reason captured before the removal (a path-only re-add would lose it).");
		cmd.mutating = true;
		cmd.argsSchema = objectSchema({
			{QStringLiteral("path"), stringProperty()},
		}, {QStringLiteral("path")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("path"), stringProperty()},
			{QStringLiteral("reason"), stringProperty()},
			{QStringLiteral("quarantined"), booleanProperty()},
			{QStringLiteral("action"), stringProperty()},
			{QStringLiteral("cache_file"), stringProperty()},
			{QStringLiteral("quarantine_count"), integerProperty()},
			{QStringLiteral("quarantine"), arrayProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject& args) { return handleQuarantineRemove(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("plugin.rescan");
		cmd.group = QStringLiteral("plugin");
		cmd.verb = QStringLiteral("rescan");
		cmd.description = QStringLiteral("Run the plugin scan the factory already has "
			"(PluginFactory::discoverPlugins, the slot the GUI never re-invokes): re-read the cache, drop "
			"quarantined files, serve what the cache can still be trusted for, load what changed, and rewrite "
			"the cache. This is what APPLIES a quarantine edit. Reports the scan's own numbers before and "
			"after. Not reversible: the previous fingerprint record of a file the scan replaced is not "
			"restorable by any command.");
		cmd.mutating = true;
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("candidate_files"), integerProperty()},
			{QStringLiteral("quarantined"), integerProperty()},
			{QStringLiteral("served_from_cache"), integerProperty()},
			{QStringLiteral("negative_from_cache"), integerProperty()},
			{QStringLiteral("scanned"), integerProperty()},
			{QStringLiteral("descriptors"), integerProperty()},
			{QStringLiteral("quarantined_paths"), arrayProperty()},
			{QStringLiteral("quarantined_reasons"), arrayProperty()},
			{QStringLiteral("report"), stringProperty()},
			{QStringLiteral("cache_file"), stringProperty()},
			{QStringLiteral("cache_entry_count"), integerProperty()},
			{QStringLiteral("cache_quarantine_count"), integerProperty()},
			{QStringLiteral("cache_dirty"), booleanProperty()},
			{QStringLiteral("quarantined_entries"), arrayProperty()},
			// The same field set again, as it stood before this run.
			{QStringLiteral("previous"), objectProperty()},
		});
		cmd.handler = [](const QJsonObject&) { return handleRescan(); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
