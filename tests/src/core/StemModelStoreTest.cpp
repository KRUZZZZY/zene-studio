/*
 * StemModelStoreTest.cpp - model path, checksum and download policy
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

#include <QCoreApplication>
#include <QtTest>

#include <atomic>
#include <chrono>
#include <thread>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QNetworkProxy>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslKey>
#include <QSslSocket>
#include <QTimer>
#include <QTcpServer>
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

//! The partial files download() leaves beside `dirPath>/<name>.onnx` while a
//! transfer is in flight - its per-attempt staging files, whatever they are named.
QStringList partialFiles(const QString& dirPath, const QString& name)
{
	return QDir(dirPath).entryList({name + QStringLiteral(".onnx.part*")}, QDir::Files);
}

class SslServer : public QTcpServer
{
public:
	SslServer(const QByteArray& certPem, const QByteArray& keyPem, QObject* parent = nullptr)
		: QTcpServer(parent), m_certPem(certPem), m_keyPem(keyPem) {}
protected:
	void incomingConnection(qintptr handle) override
	{
		QSslSocket* socket = new QSslSocket(this);
		socket->setSocketDescriptor(handle);
		socket->setLocalCertificate(QSslCertificate(m_certPem));
		socket->setPrivateKey(QSslKey(m_keyPem, QSsl::Rsa));
		addPendingConnection(socket);
		connect(socket, qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors), socket, [socket](const QList<QSslError>&){ socket->ignoreSslErrors(); });
		socket->startServerEncryption();
	}
private:
	QByteArray m_certPem;
	QByteArray m_keyPem;
};

} // namespace

class StemModelStoreTest : public QObject
{
	Q_OBJECT
private:
	// The in-flight-download fixture. A failed QVERIFY returns from the slot
	// while the download threads still run, so everything they touch lives
	// here and cleanup() cancels and joins them.
	StemModelSpec m_spec;
	QTemporaryDir m_dir;
	std::atomic<bool> m_cancelA{false};
	std::atomic<bool> m_cancelB{false};
	std::thread m_threadA;
	std::thread m_threadB;
	bool m_okA = true;
	bool m_okB = true;
	QString m_errorA;
	QString m_errorB;

private slots:
	void cleanup()
	{
		m_cancelA.store(true);
		m_cancelB.store(true);
		if (m_threadA.joinable()) { m_threadA.join(); }
		if (m_threadB.joinable()) { m_threadB.join(); }
	}

	//! The one partial file the first download has in flight, or empty if none appeared.
	QString waitForTheFirstPartial()
	{
		QElapsedTimer timer;
		timer.start();
		QStringList seen;
		while (seen.isEmpty() && timer.elapsed() < 5000)
		{
			seen = partialFiles(m_dir.path(), m_spec.name);
			if (seen.isEmpty()) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
		}
		return seen.size() == 1 ? m_dir.filePath(seen.front()) : QString();
	}

	//! Returns 1 the moment the in-flight partial's bytes are gone: the second
	//! download clobbered them. Returns 0 once it has a staging file of its own
	//! (it can no longer clobber). Returns -1 if the watch runs out without
	//! observing either outcome, which is a test failure, not a pass.
	int clobbersThePartialInFlight(const QString& partialA, const QByteArray& streamed)
	{
		QElapsedTimer timer;
		timer.start();
		while (timer.elapsed() < 5000)
		{
			{
				QFile file(partialA);
				if (!file.open(QIODevice::ReadOnly) || file.readAll() != streamed) { return 1; }
			}
			if (partialFiles(m_dir.path(), m_spec.name).size() >= 2) { return 0; }
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		return -1;
	}

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

	void testDefaultSpecIsPinnedToOneCommitOfTheModelCard()
	{
		// Owner decision 14 pinned the model - the deliberate, test-visible change
		// this slot's predecessor (which asserted the unpinned state) asked for.
		// The values are the ones measured from the file (sha256sum, byte count)
		// and the URL names a COMMIT, not a branch, so the bytes cannot move.
		const auto spec = StemModelStore::defaultModelSpec();
		QCOMPARE(spec.name, QStringLiteral("htdemucs-fp16"));
		QVERIFY(StemModelStore::isDownloadUrlAllowed(spec.url));
		QVERIFY(spec.url.startsWith(spec.modelCardUrl + QStringLiteral("/resolve/")));
		QVERIFY2(spec.url.contains(QStringLiteral("/d54ed9eb60e258ea82131c6ee14578628816456a/")),
			qPrintable(QStringLiteral("not pinned to the verified commit: ") + spec.url));
		QCOMPARE(spec.sha256, QStringLiteral("d05c269d0178d2a72ad484b10b11dd370193fc923201c3b27a99f848745db70a"));
		QCOMPARE(spec.sizeBytes, qint64{165612636});
		QCOMPARE(spec.license, QStringLiteral("MIT"));
	}

	//! Offline mode refuses the pinned default BEFORE any network, and says why;
	//! the policy refusals still come first (an unpinned spec is still named).
	void testOfflineModeRefusesEveryDownload()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		qputenv("LMMS_STEM_OFFLINE", QByteArrayLiteral("1"));
		QVERIFY(StemModelStore::isOffline());
		QString error;
		QVERIFY(!StemModelStore::download(StemModelStore::defaultModelSpec(), dir.path(), {}, &error));
		QVERIFY2(error.contains(QStringLiteral("offline"), Qt::CaseInsensitive), qPrintable(error));
		QVERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty());
		StemModelSpec unpinned;
		unpinned.name = QStringLiteral("unpinned");
		unpinned.url = QStringLiteral("https://example.com/model.onnx");
		QVERIFY(!StemModelStore::download(unpinned, dir.path(), {}, &error));
		QVERIFY2(error.contains(QStringLiteral("unpinned"), Qt::CaseInsensitive), qPrintable(error));
		qputenv("LMMS_STEM_OFFLINE", QByteArrayLiteral("0"));
		QVERIFY(!StemModelStore::isOffline());
		qunsetenv("LMMS_STEM_OFFLINE");
		QVERIFY(!StemModelStore::isOffline());
	}

	//! A file already in place that verifies IS the download: success with no transfer,
	//! offline included. A file that does not verify is still refused, and left alone.
	void testAVerifiedFileInPlaceIsSuccessEvenOffline()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		StemModelSpec spec;
		spec.name = QStringLiteral("inplace");
		spec.url = QStringLiteral("https://example.invalid/never-contacted.onnx");
		spec.sha256 = QString::fromLatin1(Sha256OfAbc);
		spec.sizeBytes = 3;
		const QString path = QDir(dir.path()).filePath(QStringLiteral("inplace.onnx"));
		{
			QFile file(path);
			QVERIFY(file.open(QIODevice::WriteOnly));
			file.write("abc");
		}
		qputenv("LMMS_STEM_OFFLINE", QByteArrayLiteral("1"));
		QString error;
		QVERIFY2(StemModelStore::download(spec, dir.path(), {}, &error), qPrintable(error));

		{
			QFile file(path);
			QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
			file.write("abd");
		}
		QVERIFY(!StemModelStore::download(spec, dir.path(), {}, &error));
		QVERIFY2(error.contains(QStringLiteral("offline"), Qt::CaseInsensitive), qPrintable(error));
		QFile file(path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		QCOMPARE(file.readAll(), QByteArrayLiteral("abd"));
		qunsetenv("LMMS_STEM_OFFLINE");
	}

	/*! Luna cluster 4495: a nonempty file with the wrong bytes is a broken
	 *  fetch, not a model. It must not read as "present", or nothing can
	 *  repair it: the GUI's download action and the first-use fetch both ask
	 *  whether the model is already there. */
	void testACorruptDefaultModelIsNotPresentAndStaysFetchable()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto previous = qgetenv("LMMS_STEM_MODEL_DIR");
		qputenv("LMMS_STEM_MODEL_DIR", dir.path().toUtf8());
		QVERIFY(qEnvironmentVariableIsEmpty("LMMS_STEM_MODEL"));
		QVERIFY(!StemModelStore::isOffline());

		const auto spec = StemModelStore::defaultModelSpec();
		const auto path = StemModelStore::modelPath(spec.name + QStringLiteral(".onnx"));
		writeFile(path, QByteArrayLiteral("not the pinned model bytes at all"));

		// A corrupt file still leaves a fetch available to repair it.
		QVERIFY2(StemModelStore::canFetchDefaultModel(),
			"a corrupt model is treated as present, so it can never be refreshed");

		// Offline, that repair is refused - and refused honestly: the fetch does
		// not report success for a file it never verified.
		qputenv("LMMS_STEM_OFFLINE", QByteArrayLiteral("1"));
		QString error;
		std::atomic<bool> cancel{false};
		QVERIFY(!StemModelStore::fetchDefaultModelIfMissing(cancel, {}, &error));
		QVERIFY2(error.contains(QStringLiteral("offline"), Qt::CaseInsensitive), qPrintable(error));
		qunsetenv("LMMS_STEM_OFFLINE");

		if (previous.isEmpty()) { qunsetenv("LMMS_STEM_MODEL_DIR"); }
		else { qputenv("LMMS_STEM_MODEL_DIR", previous); }
	}

	//! The operator's own file (LMMS_STEM_MODEL) is outside the pinned spec's
	//! contract, so a nonempty one is still "present" and never overwritten.
	void testAnExplicitModelPathIsTakenAtFaceValue()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const auto previous = qgetenv("LMMS_STEM_MODEL");
		const auto path = QDir(dir.path()).filePath(QStringLiteral("operator.onnx"));
		qputenv("LMMS_STEM_MODEL", path.toUtf8());
		writeFile(path, QByteArrayLiteral("whatever the operator put there"));

		std::atomic<bool> cancel{false};
		QVERIFY(StemModelStore::fetchDefaultModelIfMissing(cancel, {}, nullptr));
		// ...and it is never a candidate for a download over the top of it.
		QVERIFY(!StemModelStore::canFetchDefaultModel());

		if (previous.isEmpty()) { qunsetenv("LMMS_STEM_MODEL"); }
		else { qputenv("LMMS_STEM_MODEL", previous); }
	}

	/*! Luna cluster new-p12-255: two downloads of one model into one directory
	 *  must not share one partial file. The second caller used to open
	 *  `<name>.onnx.part` with truncation while the first was still streaming
	 *  into it: the bytes already on disk vanished and both attempts interleaved
	 *  writes into one file, so a clobbered install could pass or fail
	 *  verification by luck. The socket here accepts and never answers, so a
	 *  download stays in flight until cancelled; the bytes a transfer has
	 *  already streamed are stood in for by writing them into the partial file. */
	void testConcurrentDownloadsDoNotClobberAPartialFileInFlight()
	{
		QVERIFY(m_dir.isValid());
		// Nothing may reroute the loopback URL below away from this socket.
		QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
		QTcpServer server;
		QVERIFY(server.listen(QHostAddress::LocalHost));

		m_spec.name = QStringLiteral("concurrent");
		m_spec.url = QStringLiteral("https://127.0.0.1:%1/concurrent.onnx").arg(server.serverPort());
		m_spec.sha256 = QString::fromLatin1(Sha256OfAbc);
		m_spec.sizeBytes = 3;
		m_cancelA.store(false);
		m_cancelB.store(false);
		m_okA = m_okB = true;
		m_errorA.clear();
		m_errorB.clear();

		const auto download = [this](bool first)
			{
				bool* ok = first ? &m_okA : &m_okB;
				QString* error = first ? &m_errorA : &m_errorB;
				const std::atomic<bool>* cancel = first ? &m_cancelA : &m_cancelB;
				*ok = StemModelStore::download(m_spec, m_dir.path(), {}, error, cancel);
			};
		m_threadA = std::thread(download, true);

		// The first download hangs in its transfer with its partial file open.
		const QString partialA = waitForTheFirstPartial();
		QVERIFY2(!partialA.isEmpty(), "the first download never created its partial file");

		// Stand in for the bytes the first transfer has already streamed...
		const QByteArray streamed("already-streamed-bytes");
		{
			QFile file(partialA);
			QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append));
			QCOMPARE(file.write(streamed), qint64(streamed.size()));
		}

		// ...and let a second download of the same model run across it.
		m_threadB = std::thread(download, false);
		const int outcome = clobbersThePartialInFlight(partialA, streamed);

		m_cancelA.store(true);
		m_cancelB.store(true);
		// Ending the fake server ends both transfers at once, whatever the
		// cancel flags do: the pending handshakes die with the listening socket.
		server.close();
		m_threadA.join();
		m_threadB.join();

		QVERIFY2(outcome != -1,
			"timed out without observing either truncation or a second staging file");
		QVERIFY2(outcome != 1,
			"a concurrent download truncated the partial file of a download still in flight");
		QVERIFY2(!m_okA && !m_okB, "a download reported success without ever fetching anything");
		// Neither attempt leaves its staging file behind once it has failed.
		QVERIFY2(partialFiles(m_dir.path(), m_spec.name).isEmpty(),
			"a failed download left its staging file behind");
	}

	//! A download that loses the final rename to a concurrent installer still
	//! reports success (the pinned bytes are in place), and must not leave its
	//! own staging file behind as a full-size duplicate model.
	void testConcurrentSuccessfulDownloadsLeaveNoStagingFile()
	{
		QTemporaryDir certDir;
		QVERIFY(certDir.isValid());

		QProcess proc;
		proc.start(QStringLiteral("openssl"),
			{QStringLiteral("req"), QStringLiteral("-x509"), QStringLiteral("-newkey"),
			 QStringLiteral("rsa:2048"), QStringLiteral("-keyout"),
			 certDir.filePath(QStringLiteral("key.pem")), QStringLiteral("-out"),
			 certDir.filePath(QStringLiteral("cert.pem")), QStringLiteral("-days"),
			 QStringLiteral("1"), QStringLiteral("-nodes"), QStringLiteral("-subj"),
			 QStringLiteral("/CN=127.0.0.1")});
		QVERIFY(proc.waitForFinished(10000));
		QCOMPARE(proc.exitCode(), 0);

		QFile certFile(certDir.filePath(QStringLiteral("cert.pem")));
		QVERIFY(certFile.open(QIODevice::ReadOnly));
		const QByteArray certPem = certFile.readAll();
		certFile.close();
		QFile keyFile(certDir.filePath(QStringLiteral("key.pem")));
		QVERIFY(keyFile.open(QIODevice::ReadOnly));
		const QByteArray keyPem = keyFile.readAll();
		keyFile.close();

		const QSslConfiguration previous = QSslConfiguration::defaultConfiguration();
		QSslConfiguration cfg = previous;
		cfg.setPeerVerifyMode(QSslSocket::VerifyNone);
		QSslConfiguration::setDefaultConfiguration(cfg);

		QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));

		for (int iter = 0; iter < 10; ++iter)
		{
			QTemporaryDir dir;
			QVERIFY(dir.isValid());

			SslServer server(certPem, keyPem);
			QVERIFY(server.listen(QHostAddress::LocalHost));

			QObject::connect(&server, &QTcpServer::newConnection, [&]() {
				while (server.hasPendingConnections())
				{
					QSslSocket* sock = qobject_cast<QSslSocket*>(server.nextPendingConnection());
					connect(sock, qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors), sock, [sock](const QList<QSslError>&){ sock->ignoreSslErrors(); });
					auto respond = [sock]() {
						std::this_thread::sleep_for(std::chrono::milliseconds(200));
						sock->readAll();
						const QByteArray body("abc");
						QByteArray resp = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Length: 3\r\nConnection: close\r\n\r\n");
						resp.append(body);
						sock->write(resp);
						sock->flush();
						QTimer::singleShot(100, sock, [sock](){ sock->disconnectFromHost(); });
					};
					connect(sock, &QSslSocket::encrypted, sock, respond);
					if (sock->isEncrypted()) { respond(); }
				}
			});

			StemModelSpec spec;
			spec.name = QStringLiteral("concurrent-ok");
			spec.url = QStringLiteral("https://127.0.0.1:%1/x").arg(server.serverPort());
			spec.sha256 = QString::fromLatin1(Sha256OfAbc);
			spec.sizeBytes = 3;

			std::atomic<bool> cancelA{false};
			std::atomic<bool> cancelB{false};
			std::atomic<bool> doneA{false};
			std::atomic<bool> doneB{false};
			std::atomic<bool> startFlag{false};
			bool okA = true;
			bool okB = true;
			QString errA;
			QString errB;
			std::thread tA([&]() { while (!startFlag.load()) { std::this_thread::yield(); } okA = StemModelStore::download(spec, dir.path(), {}, &errA, &cancelA); doneA.store(true); });
			std::thread tB([&]() { while (!startFlag.load()) { std::this_thread::yield(); } okB = StemModelStore::download(spec, dir.path(), {}, &errB, &cancelB); doneB.store(true); });
			startFlag.store(true);

			QElapsedTimer guard;
			guard.start();
			while ((!doneA.load() || !doneB.load()) && guard.elapsed() < 30000)
			{
				QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
			if (tA.joinable()) { tA.join(); }
			if (tB.joinable()) { tB.join(); }
			server.close();

			QVERIFY2(okA, qPrintable(errA));
			QVERIFY2(okB, qPrintable(errB));
			QVERIFY(StemModelStore::verify(dir.filePath(spec.name + QStringLiteral(".onnx")),
				spec.sha256, spec.sizeBytes));


			QVERIFY2(partialFiles(dir.path(), spec.name).isEmpty(),
				qPrintable(QStringLiteral("leftover staging files: ") + partialFiles(dir.path(), spec.name).join(QStringLiteral(", "))));
		}

		QSslConfiguration::setDefaultConfiguration(previous);
	}

	//! Deterministic rename-loser path: a concurrent installer moves the pinned
	//! bytes into place between this attempt's remove(finalPath) and its rename.
	//! The rename then fails, but the file verifies, so download() reports
	//! success and must remove its own staging file (no duplicate model left).
	void testDownloadRenameLoserReportsSuccessAndRemovesStaging()
	{
		QTemporaryDir certDir;
		QVERIFY(certDir.isValid());

		QProcess proc;
		proc.start(QStringLiteral("openssl"),
			{QStringLiteral("req"), QStringLiteral("-x509"), QStringLiteral("-newkey"),
			 QStringLiteral("rsa:2048"), QStringLiteral("-keyout"),
			 certDir.filePath(QStringLiteral("key.pem")), QStringLiteral("-out"),
			 certDir.filePath(QStringLiteral("cert.pem")), QStringLiteral("-days"),
			 QStringLiteral("1"), QStringLiteral("-nodes"), QStringLiteral("-subj"),
			 QStringLiteral("/CN=127.0.0.1")});
		QVERIFY(proc.waitForFinished(10000));
		QCOMPARE(proc.exitCode(), 0);

		QFile certFile(certDir.filePath(QStringLiteral("cert.pem")));
		QVERIFY(certFile.open(QIODevice::ReadOnly));
		const QByteArray certPem = certFile.readAll();
		certFile.close();
		QFile keyFile(certDir.filePath(QStringLiteral("key.pem")));
		QVERIFY(keyFile.open(QIODevice::ReadOnly));
		const QByteArray keyPem = keyFile.readAll();
		keyFile.close();

		const QSslConfiguration previous = QSslConfiguration::defaultConfiguration();
		QSslConfiguration cfg = previous;
		cfg.setPeerVerifyMode(QSslSocket::VerifyNone);
		QSslConfiguration::setDefaultConfiguration(cfg);

		QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));

		QTemporaryDir dir;
		QVERIFY(dir.isValid());

		SslServer server(certPem, keyPem);
		QVERIFY(server.listen(QHostAddress::LocalHost));

		QObject::connect(&server, &QTcpServer::newConnection, [&]() {
			while (server.hasPendingConnections())
			{
				QSslSocket* sock = qobject_cast<QSslSocket*>(server.nextPendingConnection());
				connect(sock, qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors), sock, [sock](const QList<QSslError>&){ sock->ignoreSslErrors(); });
				auto respond = [sock]() {
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
					sock->readAll();
					const QByteArray body("abc");
					QByteArray resp = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Length: 3\r\nConnection: close\r\n\r\n");
					resp.append(body);
					sock->write(resp);
					sock->flush();
					QTimer::singleShot(100, sock, [sock](){ sock->disconnectFromHost(); });
				};
				connect(sock, &QSslSocket::encrypted, sock, respond);
				if (sock->isEncrypted()) { respond(); }
			}
		});

		StemModelSpec spec;
		spec.name = QStringLiteral("rename-loser");
		spec.url = QStringLiteral("https://127.0.0.1:%1/x").arg(server.serverPort());
		spec.sha256 = QString::fromLatin1(Sha256OfAbc);
		spec.sizeBytes = 3;

		// The hook stands in for the concurrent installer. Every step of the
		// injection is recorded and asserted below: if the hook did not run,
		// could not open the destination, did not write every byte or left a
		// destination that does not verify against the pinned spec, the
		// ordinary rename would have succeeded and the rename-loser path would
		// never have been exercised - the test would pass without proving
		// anything about it.
		bool hookRan = false;
		bool hookOpenedDestination = false;
		bool hookWroteAllBytes = false;
		bool hookDestinationVerified = false;
		StemModelStore::setDownloadRenameHook([&](const QString& finalPath) {
			hookRan = true;
			QFile f(finalPath);
			hookOpenedDestination = f.open(QIODevice::WriteOnly | QIODevice::Truncate);
			if (!hookOpenedDestination) { return; }
			hookWroteAllBytes = (f.write("abc") == 3);
			f.close();
			// The competing installer must have the pinned bytes in place
			// BEFORE this attempt's rename, or that rename would succeed.
			hookDestinationVerified = StemModelStore::verify(finalPath, spec.sha256, spec.sizeBytes);
		});

		QString error;
		std::atomic<bool> cancel{false};
		const bool ok = StemModelStore::download(spec, dir.path(), {}, &error, &cancel);

		StemModelStore::setDownloadRenameHook({});

		QVERIFY2(hookRan, "the rename hook did not run");
		QVERIFY2(hookOpenedDestination, "the rename hook could not open the destination file");
		QVERIFY2(hookWroteAllBytes, "the rename hook did not write all expected bytes");
		QVERIFY2(hookDestinationVerified, "the rename hook's destination does not verify against the pinned spec");
		QVERIFY2(ok, qPrintable(error));
		QVERIFY(StemModelStore::verify(dir.filePath(spec.name + QStringLiteral(".onnx")),
			spec.sha256, spec.sizeBytes));
		QVERIFY2(partialFiles(dir.path(), spec.name).isEmpty(),
			qPrintable(QStringLiteral("leftover staging files: ") + partialFiles(dir.path(), spec.name).join(QStringLiteral(", "))));

		QSslConfiguration::setDefaultConfiguration(previous);
		server.close();
	}

};

QTEST_GUILESS_MAIN(StemModelStoreTest)
#include "StemModelStoreTest.moc"
