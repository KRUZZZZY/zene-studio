/*
 * StemModelStoreTest.cpp - model path, checksum and download policy
 *
 * Copyright (c) 2026 LMMS Developers
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

#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "StemSeparation/StemModelStore.h"

using namespace lmms;

namespace
{

// SHA-256 of the ASCII string "abc" (FIPS 180-2 test vector).
constexpr const char* Sha256OfAbc =
	"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

QString writeFile(const QString& path, const QByteArray& contents)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly))
	{
		qWarning("could not open %s for writing", qPrintable(path));
		return {};
	}
	file.write(contents);
	file.close();
	return path;
}

} // namespace

class StemModelStoreTest : public QObject
{
	Q_OBJECT
private slots:
	void testDefaultModelDirHonoursEnvironment()
	{
		const auto previous = qgetenv("LMMS_STEM_MODEL_DIR");
		qputenv("LMMS_STEM_MODEL_DIR", QByteArrayLiteral("/tmp/lmms-stem-model-dir-test"));
		QCOMPARE(StemModelStore::defaultModelDir(),
			QStringLiteral("/tmp/lmms-stem-model-dir-test"));
		if (previous.isEmpty())
		{
			qunsetenv("LMMS_STEM_MODEL_DIR");
		}
		else
		{
			qputenv("LMMS_STEM_MODEL_DIR", previous);
		}
	}

	void testModelPathIsInsideTheModelDir()
	{
		const auto path = StemModelStore::modelPath(QStringLiteral("htdemucs-fp16.onnx"));
		QVERIFY(path.endsWith(QStringLiteral("/htdemucs-fp16.onnx")));
		QVERIFY(path.startsWith(StemModelStore::defaultModelDir()));
	}

	void testSha256MatchesTheKnownTestVector()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto path = writeFile(dir.filePath(QStringLiteral("abc.bin")), QByteArrayLiteral("abc"));
		QCOMPARE(StemModelStore::sha256OfFile(path), QString::fromLatin1(Sha256OfAbc));
	}

	void testSha256OfMissingFileFails()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		QString error;
		QCOMPARE(StemModelStore::sha256OfFile(dir.filePath(QStringLiteral("nope.bin")), &error),
			QString());
		QVERIFY(!error.isEmpty());
	}

	void testVerifyAcceptsAMatchingFile()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto path = writeFile(dir.filePath(QStringLiteral("abc.bin")), QByteArrayLiteral("abc"));
		QString error;
		QVERIFY(StemModelStore::verify(path, QString::fromLatin1(Sha256OfAbc), 3, &error));
		QVERIFY(error.isEmpty());
		// checksum comparison is case-insensitive
		QVERIFY(StemModelStore::verify(path, QString::fromLatin1(Sha256OfAbc).toUpper(), 3, &error));
	}

	void testVerifyRejectsAChangedFile()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto path = writeFile(dir.filePath(QStringLiteral("tampered.bin")), QByteArrayLiteral("abcd"));
		QString error;
		QVERIFY(!StemModelStore::verify(path, QString::fromLatin1(Sha256OfAbc), 4, &error));
		QVERIFY(error.contains(QStringLiteral("checksum"), Qt::CaseInsensitive));
	}

	void testVerifyRejectsTheWrongSize()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto path = writeFile(dir.filePath(QStringLiteral("abc.bin")), QByteArrayLiteral("abc"));
		QString error;
		QVERIFY(!StemModelStore::verify(path, QString::fromLatin1(Sha256OfAbc), 999, &error));
		QVERIFY(error.contains(QStringLiteral("size"), Qt::CaseInsensitive));
	}

	void testVerifyRefusesAnUnpinnedChecksum()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto path = writeFile(dir.filePath(QStringLiteral("abc.bin")), QByteArrayLiteral("abc"));
		QString error;
		QVERIFY(!StemModelStore::verify(path, QString(), 3, &error));
		QVERIFY(error.contains(QStringLiteral("pinned"), Qt::CaseInsensitive));
	}

	void testVerifyRejectsAMissingFile()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		QString error;
		QVERIFY(!StemModelStore::verify(dir.filePath(QStringLiteral("missing.onnx")),
			QString::fromLatin1(Sha256OfAbc), 3, &error));
		QVERIFY(!error.isEmpty());
	}

	void testDownloadUrlPolicyIsHttpsOnly()
	{
		QVERIFY(StemModelStore::isDownloadUrlAllowed(
			QStringLiteral("https://huggingface.co/example/model.onnx")));
		QVERIFY(!StemModelStore::isDownloadUrlAllowed(
			QStringLiteral("http://huggingface.co/example/model.onnx")));
		QVERIFY(!StemModelStore::isDownloadUrlAllowed(
			QStringLiteral("file:///etc/passwd")));
		QVERIFY(!StemModelStore::isDownloadUrlAllowed(
			QStringLiteral("ftp://example.com/model.onnx")));
		QVERIFY(!StemModelStore::isDownloadUrlAllowed(QString()));
		QVERIFY(!StemModelStore::isDownloadUrlAllowed(QStringLiteral("model.onnx")));
	}

	void testDownloadRefusesAnUnpinnedSpec()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());

		StemModelSpec unpinned;
		unpinned.name = QStringLiteral("unpinned");
		unpinned.url = QStringLiteral("https://example.com/model.onnx");
		QString error;
		QVERIFY(!StemModelStore::download(unpinned, dir.path(), {}, &error));
		QVERIFY(error.contains(QStringLiteral("unpinned"), Qt::CaseInsensitive));
		QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("unpinned.onnx"))));
	}

	void testDownloadRefusesAPlainHttpUrl()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());

		StemModelSpec spec;
		spec.name = QStringLiteral("insecure");
		spec.url = QStringLiteral("http://example.com/model.onnx");
		spec.sha256 = QString::fromLatin1(Sha256OfAbc);
		spec.sizeBytes = 3;
		QString error;
		QVERIFY(!StemModelStore::download(spec, dir.path(), {}, &error));
		QVERIFY(error.contains(QStringLiteral("https"), Qt::CaseInsensitive));
	}

	void testDefaultSpecNeverPinsAGuessedUrlOrChecksum()
	{
		// Policy: the real model is an optional download whose URL and checksum
		// must come from the model card, never from a guess. This test pins the
		// current state (unpinned) so that pinning the model is a deliberate,
		// test-visible change.
		const auto spec = StemModelStore::defaultModelSpec();
		QVERIFY(!spec.name.isEmpty());
		QVERIFY(!spec.modelCardUrl.isEmpty());
		QVERIFY(spec.sha256.isEmpty());
		QCOMPARE(spec.sizeBytes, 0);
	}
};

QTEST_GUILESS_MAIN(StemModelStoreTest)
#include "StemModelStoreTest.moc"
