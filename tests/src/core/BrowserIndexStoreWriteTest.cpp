/*
 * BrowserIndexStoreWriteTest.cpp - an index store write the kernel cuts short is refused, not published
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
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */
/*! cluster new-p15-286: BrowserIndex::save writes the whole index document or nothing. A write the
 *  kernel cuts short (RLIMIT_FSIZE, so it is deterministic: no disk, no timing, no race) is refused -
 *  the store keeps the index it had, the scan reports the failure, and the next load reads that index.
 *  Before this, save() ignored QSaveFile::write's result and committed the partial document, so the
 *  store became truncated JSON and the next load read an empty index. */

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>

#if defined(Q_OS_UNIX)
#include <csignal>
#include <sys/resource.h>
#endif

#include "BrowserIndex.h"

using namespace lmms;

namespace
{

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
#endif

//! A one-byte file under @a directory named @a name: the index admits it by its extension, and this
//! scan measures no features, so it never opens the file.
void writeFile(const QString& directory, const QString& name)
{
	QFile file(QDir(directory).filePath(name));
	QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.fileName()));
	QCOMPARE(file.write("x"), qint64(1));
	file.close();
}

//! The bytes of the store file, or empty when it cannot be read.
QByteArray readStore(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

class BrowserIndexStoreWriteTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY2(m_store.isValid(), "no temporary directory for the index store");
		QVERIFY2(m_tree.isValid(), "no temporary directory for the scanned tree");
		// The store of its own, outside the scanned tree, as the sibling browser tests do.
		qputenv("ZENE_BROWSER_INDEX", m_store.filePath(QStringLiteral("index.json")).toLocal8Bit());
	}

	void cleanupTestCase()
	{
		qunsetenv("ZENE_BROWSER_INDEX");
	}

	//! A save whose write the kernel cuts short is a failed save: the index the store still holds is
	//! kept, the store's bytes do not change, and a restart reads that same index.
	void aShortIndexStoreWriteIsRefusedNotPublished()
	{
#if !defined(Q_OS_UNIX)
		QSKIP("RLIMIT_FSIZE is POSIX-only, so an index write cannot be cut short here");
#else
		const QString tree = m_tree.filePath(QStringLiteral("library"));
		QVERIFY(QDir().mkpath(tree));
		// Enough long-named files that one index document is several times QSaveFile's write buffer
		// (16384 bytes): a smaller document is buffered, so a cut lands in commit() and fails on its
		// own - that path says nothing about the byte count save() used to drop.
		for (int i = 0; i < 256; ++i)
		{
			writeFile(tree, QStringLiteral("probe_%1_%2.wav")
				.arg(i, 4, 10, QLatin1Char('0')).arg(QString(200, QLatin1Char('a'))));
		}
		writeFile(tree, QStringLiteral("first.wav"));

		BrowserIndex& index = BrowserIndex::instance();
		const auto names = [&index] {
			QStringList out;
			for (const BrowserIndexEntry& entry : index.entries()) { out << entry.name; }
			out.sort();
			return out;
		};
		index.reload();
		QVERIFY(index.scanNow(QStringList{tree}, false, 0));
		QVERIFY2(index.status().error.isEmpty(), qPrintable(index.status().error));
		const QStringList published = names();
		QCOMPARE(published.size(), 257);
		QVERIFY2(published.contains(QStringLiteral("first.wav")), "the first scan did not publish its index");
		const QByteArray stored = readStore(index.storePath());
		QVERIFY2(stored.size() > 32768, qPrintable(QStringLiteral("the index document is %1 bytes, not past "
			"QSaveFile's 16384-byte write buffer, so the cut would not reach the write").arg(stored.size())));

		// The scan below walks a second file, so its document differs from the one stored: a save that
		// published it would change the store, whether or not the write was cut short.
		writeFile(tree, QStringLiteral("second.wav"));
		QString savedError;
		bool stillRunning = true;
		{
			ScopedFileSizeLimit limit(8);
			QVERIFY2(limit.active(), "RLIMIT_FSIZE could not be lowered, so no write can be cut short");
			QVERIFY(index.scanNow(QStringList{tree}, false, 0));
			savedError = index.status().error;
			stillRunning = index.status().running;
		}
		// Asserted outside the cap: while it is in force every file write stops at 8 bytes, and QTest's
		// own diagnostics go to whatever stdout is - a log file included.
		QVERIFY2(savedError.contains(QStringLiteral("could not be written")),
			qPrintable(QStringLiteral("a save whose write was cut short was not reported as a failed save: '%1'")
				.arg(savedError)));
		QVERIFY(!stillRunning);

		// The index the store still holds is the one the index still answers with.
		QCOMPARE(names(), published);
		QCOMPARE(readStore(index.storePath()), stored);

		// And a restart reads that same index: the store is not a truncated document.
		index.reload();
		QCOMPARE(names(), published);
		QVERIFY2(index.status().error.isEmpty(), qPrintable(index.status().error));
#endif
	}

private:
	QTemporaryDir m_store;
	QTemporaryDir m_tree;
};

QTEST_GUILESS_MAIN(BrowserIndexStoreWriteTest)
#include "BrowserIndexStoreWriteTest.moc"
