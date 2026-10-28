/*
 * BrowserCollectionStoreWriteTest.cpp - a collection store write the kernel cuts short is refused, not published
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
/*! cluster new-p17-271: browser.collection_save and browser.collection_remove write the whole collections store
 *  (browser-collections.json) or nothing. A write the kernel cuts short (RLIMIT_FSIZE, so it is deterministic) is
 *  refused, and the store on disk keeps what it had. */

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#if defined(Q_OS_UNIX)
#include <csignal>
#include <sys/resource.h>
#endif

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"

using namespace lmms;

namespace
{

#if defined(Q_OS_UNIX) // only the POSIX write-limit cases call it
ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}
#endif

#if defined(Q_OS_UNIX)
//! Caps the size any file may grow to for the scope, so a write past it is cut short by the kernel
//! (write returns fewer bytes than asked). The old limit and SIGXFSZ come back when it leaves scope.
class ScopedFileSizeLimit
{
	struct rlimit m_original{};
	void (*m_originalHandler)(int) = SIG_DFL;
	bool m_active = false;

public:
	explicit ScopedFileSizeLimit(rlim_t bytes)
	{
		if (getrlimit(RLIMIT_FSIZE, &m_original) != 0) { return; }
		m_originalHandler = std::signal(SIGXFSZ, SIG_IGN);
		if (m_originalHandler == SIG_ERR) { return; }
		rlimit limited = m_original;
		limited.rlim_cur = bytes;
		m_active = setrlimit(RLIMIT_FSIZE, &limited) == 0;
		if (!m_active) { std::signal(SIGXFSZ, m_originalHandler); }
	}

	~ScopedFileSizeLimit()
	{
		if (m_active)
		{
			setrlimit(RLIMIT_FSIZE, &m_original);
			std::signal(SIGXFSZ, m_originalHandler);
		}
	}

	bool active() const { return m_active; }
};

//! The bytes of the store file, or empty when it cannot be read.
QByteArray readStore(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
#endif

} // namespace

class BrowserCollectionStoreWriteTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		qputenv("ZENE_BROWSER_COLLECTIONS", m_dir.filePath(QStringLiteral("collections.json")).toLocal8Bit());
		Engine::init(true);
		ConfigManager::inst()->setWorkingDir(m_dir.filePath(QStringLiteral("work")));
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! A store write the kernel cuts short is a failed save or removal, and the store keeps what it had.
	//! Before this, writeAll() ignored QSaveFile::write's result and committed the partial JSON, so the
	//! command answered success and the store on disk was 8 bytes of JSON.
	void aShortCollectionStoreWriteIsRefusedNotPublished()
	{
#if defined(Q_OS_UNIX)
		const QString store = m_dir.filePath(QStringLiteral("collections.json"));
		QVERIFY(run(QStringLiteral("browser.collection_save"), {{QStringLiteral("name"), QStringLiteral("Snares")},
			{QStringLiteral("text"), QStringLiteral("name:snare")}}).ok);
		// Large enough that every store written below is bigger than Qt's write buffer: such a write goes
		// straight to the file, and the kernel cuts it short after 8 bytes without an error the write reports.
		const QString bigText = QString(20000, QLatin1Char('h'));
		QVERIFY(run(QStringLiteral("browser.collection_save"), {{QStringLiteral("name"), QStringLiteral("Hats")},
			{QStringLiteral("text"), bigText}}).ok);
		QFile before(store);
		QVERIFY(before.open(QIODevice::ReadOnly));
		const QByteArray original = before.readAll();
		before.close();
		QVERIFY(original.size() > 20000);

		// The save below writes the whole store, and the removal leaves Hats in it: both are cut short.
		{
			ScopedFileSizeLimit limit(8);
			QVERIFY2(limit.active(), "RLIMIT_FSIZE could not be lowered, so no write can be cut short");
			const ControlResult saved = run(QStringLiteral("browser.collection_save"), {{QStringLiteral("name"), QStringLiteral("Drums")},
				{QStringLiteral("text"), QStringLiteral("name:kick")}});
			QVERIFY2(!saved.ok, "a save whose store write was cut short was reported as success");
			QCOMPARE(saved.errorKind, ControlErrorKind::Refused);
			const ControlResult removed = run(QStringLiteral("browser.collection_remove"), {{QStringLiteral("name"), QStringLiteral("Snares")}});
			QVERIFY2(!removed.ok, "a removal whose store write was cut short was reported as success");
			QCOMPARE(removed.errorKind, ControlErrorKind::Refused);
		}

		QFile after(store);
		QVERIFY(after.open(QIODevice::ReadOnly));
		QCOMPARE(after.readAll(), original);
		after.close();
		const ControlResult listed = run(QStringLiteral("browser.collections"));
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		QCOMPARE(listed.result.value(QStringLiteral("collections")).toArray().size(), 2);
#else
		QSKIP("RLIMIT_FSIZE is POSIX-only, so a store write cannot be cut short here");
#endif
	}

	//! An undo or redo whose store write the kernel cuts short is refused, and the store keeps the bytes it
	//! had. Before this, the undo and redo callbacks dropped writeAll's result, so control.undo and
	//! control.redo answered success over a store that did not change.
	void aShortUndoOrRedoOfTheStoreIsRefused()
	{
#if defined(Q_OS_UNIX)
		const QString store = m_dir.filePath(QStringLiteral("collections.json"));
		const QString bigText = QString(20000, QLatin1Char('h'));
		QVERIFY(run(QStringLiteral("browser.collection_save"), {{QStringLiteral("name"), QStringLiteral("Snares")},
			{QStringLiteral("text"), QStringLiteral("name:snare")}}).ok);
		QVERIFY(run(QStringLiteral("browser.collection_save"), {{QStringLiteral("name"), QStringLiteral("Hats")},
			{QStringLiteral("text"), bigText}}).ok);

		// Undo: removing Snares leaves the big Hats store. Undoing the removal rewrites the bigger store, cut short.
		QVERIFY(run(QStringLiteral("browser.collection_remove"), {{QStringLiteral("name"), QStringLiteral("Snares")}}).ok);
		const QByteArray withoutSnares = readStore(store);
		QVERIFY(!withoutSnares.isEmpty());
		{
			ScopedFileSizeLimit limit(8);
			QVERIFY2(limit.active(), "RLIMIT_FSIZE could not be lowered, so no write can be cut short");
			const ControlResult undone = run(QStringLiteral("control.undo"));
			QVERIFY2(!undone.ok, "an undo whose store write was cut short was reported as success");
			QCOMPARE(undone.errorKind, ControlErrorKind::Refused);
			QVERIFY2(undone.errorMessage.contains(QStringLiteral("NOT restored")), qPrintable(undone.errorMessage));
		}
		QCOMPARE(readStore(store), withoutSnares);

		// Redo: saving Snares and undoing that save (no limit) leaves the store as it was; the redo writes the bigger store back, cut short.
		QVERIFY(run(QStringLiteral("browser.collection_save"), {{QStringLiteral("name"), QStringLiteral("Snares")},
			{QStringLiteral("text"), QStringLiteral("name:snare")}}).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		const QByteArray beforeRedo = readStore(store);
		QCOMPARE(beforeRedo, withoutSnares);
		{
			ScopedFileSizeLimit limit(8);
			QVERIFY2(limit.active(), "RLIMIT_FSIZE could not be lowered, so no write can be cut short");
			const ControlResult redone = run(QStringLiteral("control.redo"));
			QVERIFY2(!redone.ok, "a redo whose store write was cut short was reported as success");
			QCOMPARE(redone.errorKind, ControlErrorKind::Refused);
			QVERIFY2(redone.errorMessage.contains(QStringLiteral("NOT re-applied")), qPrintable(redone.errorMessage));
		}
		QCOMPARE(readStore(store), beforeRedo);
		const ControlResult listed = run(QStringLiteral("browser.collections"));
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		QCOMPARE(listed.result.value(QStringLiteral("collections")).toArray().size(), 1);
#else
		QSKIP("RLIMIT_FSIZE is POSIX-only, so a store write cannot be cut short here");
#endif
	}

private:
	QTemporaryDir m_dir;
};

QTEST_MAIN(BrowserCollectionStoreWriteTest)
#include "BrowserCollectionStoreWriteTest.moc"
