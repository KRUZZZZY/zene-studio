/*
 * StemModelStore.cpp - model discovery, checksum validation and optional download
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

#include "StemSeparation/StemModelStore.h"

#include <QDir>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace lmms
{

namespace
{

void setError(QString* error, const QString& text)
{
	if (error != nullptr)
	{
		*error = text;
	}
}

/*! Is the default model there AND usable? A file is only "present" when it
 *  verifies against the pinned spec: a nonempty file with the wrong bytes is a
 *  broken fetch, and treating it as present leaves nothing able to repair it.
 *  An explicit LMMS_STEM_MODEL is the operator's own file, outside the pinned
 *  spec's contract, so it is taken on its nonempty-ness alone. */
bool defaultModelUsable()
{
	const QString path = StemModelStore::defaultModelPath();
	if (!qEnvironmentVariableIsEmpty("LMMS_STEM_MODEL"))
	{
		return StemModelStore::isModelPresent(path);
	}
	const StemModelSpec spec = StemModelStore::defaultModelSpec();
	if (spec.sha256.trimmed().isEmpty() || spec.sizeBytes <= 0)
	{
		// Nothing pinned to check against: nonempty is all that can be asked.
		return StemModelStore::isModelPresent(path);
	}
	return StemModelStore::verify(path, spec.sha256, spec.sizeBytes);
}

} // namespace




QString StemModelStore::defaultModelDir()
{
	const auto override = qEnvironmentVariable("LMMS_STEM_MODEL_DIR");
	if (!override.isEmpty())
	{
		return override;
	}
	return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
		+ QStringLiteral("/models/stems");
}




QString StemModelStore::modelPath(const QString& fileName)
{
	return QDir(defaultModelDir()).filePath(fileName);
}




StemModelSpec StemModelStore::defaultModelSpec()
{
	StemModelSpec spec;
	spec.name = QStringLiteral("htdemucs-fp16");
	// Owner decision 14 (2026-09-29): pinned from the model card, to one COMMIT
	// of the repository rather than to a branch, so the bytes behind the URL
	// cannot move under the checksum. Verified by download and sha256sum, and
	// the SHA-256 is also the file's Hugging Face LFS oid. The file's one output
	// is the stems stacked as [1, 4, 2, 343980], which both backends accept.
	spec.url = QStringLiteral("https://huggingface.co/StemSplitio/htdemucs-onnx/resolve/"
		"d54ed9eb60e258ea82131c6ee14578628816456a/htdemucs_fp16weights.onnx");
	spec.sha256 = QStringLiteral("d05c269d0178d2a72ad484b10b11dd370193fc923201c3b27a99f848745db70a");
	spec.sizeBytes = 165612636;
	spec.license = QStringLiteral("MIT");
	spec.licenseUrl = QStringLiteral("https://github.com/facebookresearch/demucs/blob/main/LICENSE");
	spec.modelCardUrl = QStringLiteral("https://huggingface.co/StemSplitio/htdemucs-onnx");
	return spec;
}




QString StemModelStore::defaultModelPath()
{
	const auto override = qEnvironmentVariable("LMMS_STEM_MODEL");
	if (!override.isEmpty())
	{
		return override;
	}
	return modelPath(defaultModelSpec().name + QStringLiteral(".onnx"));
}




bool StemModelStore::isModelPresent(const QString& filePath, QString* error)
{
	if (filePath.isEmpty())
	{
		setError(error, QStringLiteral("Model path is empty"));
		return false;
	}
	const QFileInfo info(filePath);
	if (!info.exists() || !info.isFile())
	{
		setError(error, QStringLiteral("Model file not found: %1").arg(filePath));
		return false;
	}
	if (info.size() <= 0)
	{
		setError(error, QStringLiteral("Model file is empty: %1").arg(filePath));
		return false;
	}
	return true;
}




QString StemModelStore::sha256OfFile(const QString& filePath, QString* error)
{
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly))
	{
		setError(error, QStringLiteral("Cannot read %1: %2").arg(filePath, file.errorString()));
		return QString();
	}
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (!hash.addData(&file))
	{
		setError(error, QStringLiteral("Checksum computation failed for %1").arg(filePath));
		return QString();
	}
	return QString::fromLatin1(hash.result().toHex());
}




bool StemModelStore::verify(const QString& filePath,
	const QString& expectedSha256,
	qint64 expectedSize,
	QString* error)
{
	if (!isModelPresent(filePath, error))
	{
		return false;
	}
	const qint64 actualSize = QFileInfo(filePath).size();
	if (expectedSize > 0 && actualSize != expectedSize)
	{
		setError(error, QStringLiteral("Model size mismatch: expected %1 bytes, got %2")
			.arg(expectedSize).arg(actualSize));
		return false;
	}
	if (expectedSha256.trimmed().isEmpty())
	{
		setError(error, QStringLiteral("Model has no pinned SHA-256 checksum"));
		return false;
	}
	const auto actual = sha256OfFile(filePath, error);
	if (actual.isEmpty())
	{
		return false;
	}
	if (actual.compare(expectedSha256.trimmed(), Qt::CaseInsensitive) != 0)
	{
		setError(error, QStringLiteral("Model checksum (SHA-256) mismatch: expected %1, got %2")
			.arg(expectedSha256.trimmed(), actual));
		return false;
	}
	return true;
}




bool StemModelStore::isDownloadUrlAllowed(const QString& url)
{
	const QUrl parsed(url);
	return parsed.isValid()
		&& parsed.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0
		&& !parsed.host().isEmpty();
}




bool StemModelStore::isOffline()
{
	const QByteArray value = qgetenv("LMMS_STEM_OFFLINE");
	return !value.isEmpty() && value != "0";
}




bool StemModelStore::canFetchDefaultModel()
{
	// Present means verified (see defaultModelUsable), so a corrupt file in the
	// managed directory still leaves a fetch available to repair it.
	if (defaultModelUsable() || isOffline()
		|| !qEnvironmentVariableIsEmpty("LMMS_STEM_MODEL"))
	{
		return false;
	}
	const StemModelSpec spec = defaultModelSpec();
	return !spec.sha256.trimmed().isEmpty() && spec.sizeBytes > 0 && isDownloadUrlAllowed(spec.url);
}




bool StemModelStore::fetchDefaultModelIfMissing(const std::atomic<bool>& cancel,
	const std::function<void(float)>& progress, QString* error)
{
	// Verified, not merely nonempty: a corrupt model is reported missing here so
	// that download() replaces it. download() verifies its .part and only then
	// removes the old file, so a repair never leaves the project with nothing.
	if (defaultModelUsable()) { return true; }
	const StemModelSpec spec = defaultModelSpec();
	return download(spec, defaultModelDir(),
		[&progress, &spec](qint64 received, qint64 total)
		{
			const qint64 whole = total > 0 ? total : spec.sizeBytes;
			if (progress && whole > 0) { progress(static_cast<float>(received) / static_cast<float>(whole)); }
		},
		error, &cancel);
}




namespace
{

//! The download policy, in order: HTTPS, pinned, then offline - so an offline
//! caller still learns that a spec is unpinned or insecure. Empty: allowed.
QString downloadRefusal(const StemModelSpec& spec, const QString& destDir)
{
	if (!StemModelStore::isDownloadUrlAllowed(spec.url))
	{
		return QStringLiteral("Refusing non-HTTPS or empty download URL for model '%1'").arg(spec.name);
	}
	if (spec.sha256.trimmed().isEmpty() || spec.sizeBytes <= 0)
	{
		return QStringLiteral("Refusing to download unpinned model '%1' "
			"(no SHA-256/size); pin it from the model card first").arg(spec.name);
	}
	if (StemModelStore::isOffline())
	{
		return QStringLiteral("Refusing to download model '%1': offline mode "
			"(LMMS_STEM_OFFLINE is set); place the file at %2 by hand")
			.arg(spec.name, QDir(destDir).filePath(spec.name + QStringLiteral(".onnx")));
	}
	return QString();
}

//! The pinned spec's file is already at <destDir>/<name>.onnx and verifies against it.
bool verifiedInPlace(const StemModelSpec& spec, const QString& destDir)
{
	return StemModelStore::isDownloadUrlAllowed(spec.url) && !spec.sha256.trimmed().isEmpty()
		&& spec.sizeBytes > 0
		&& StemModelStore::verify(QDir(destDir).filePath(spec.name + QStringLiteral(".onnx")),
			spec.sha256, spec.sizeBytes);
}

//! How often a transfer looks at its cancel flag, independently of data arriving.
constexpr int CancelPollMs = 250;
//! A transfer that moves no data for this long is aborted (a server that accepted and stalled).
constexpr int StallTimeoutMs = 30000;

//! Appends what \a reply has to \a partFile; false, writing nothing, when that would take the
//! file past \a maxBytes - the pinned size, which a longer body could only fail to verify against
//! after an endless one had filled the disk.
bool appendCapped(QNetworkReply* reply, QFile& partFile, qint64 maxBytes)
{
	const QByteArray chunk = reply->readAll();
	if (partFile.pos() + chunk.size() > maxBytes) { return false; }
	partFile.write(chunk);
	return true;
}

//! Streams \a reply into \a partFile (capped, see appendCapped()), and polls \a cancel on a
//! timer, so a server that stops sending is still cancellable: downloadProgress, the other place
//! the flag is read, only fires when data arrives.
void streamInto(QNetworkReply* reply, QFile& partFile, qint64 maxBytes, const std::atomic<bool>* cancel,
	bool* oversize)
{
	QObject::connect(reply, &QNetworkReply::readyRead, [reply, &partFile, maxBytes, oversize]()
		{
			if (!appendCapped(reply, partFile, maxBytes)) { *oversize = true; reply->abort(); }
		});
	if (cancel == nullptr) { return; }
	auto* poll = new QTimer(reply);
	QObject::connect(poll, &QTimer::timeout, [reply, cancel]() { if (cancel->load()) { reply->abort(); } });
	poll->start(CancelPollMs);
}

//! Why a transfer failed, in the order a reader needs it: cancelled, oversize, then the network's own.
QString transferFailure(bool cancelled, bool oversize, qint64 maxBytes, const QNetworkReply& reply)
{
	if (cancelled) { return QStringLiteral("cancelled"); }
	if (oversize) { return QStringLiteral("the server sent more than the pinned %1 bytes").arg(maxBytes); }
	return reply.errorString();
}

/*! The network half: GET \a url into \a partFile. Streamed as it arrives, so a
 *  166 MB model is never held in memory whole, and never past \a maxBytes; a set
 *  \a cancel aborts within CancelPollMs. Blocks on a local event loop, so it runs
 *  on any QThread. */
bool transfer(const QString& url, QFile& partFile, qint64 maxBytes, const StemModelStore::DownloadProgressFn& progress,
	const std::atomic<bool>* cancel, QString* networkError)
{
	QNetworkAccessManager manager;
	QNetworkRequest request{QUrl(url)};
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Zene-Studio-stem-split/1.0"));
	request.setTransferTimeout(StallTimeoutMs);

	QNetworkReply* reply = manager.get(request);
	bool oversize = false;
	streamInto(reply, partFile, maxBytes, cancel, &oversize);
	QObject::connect(reply, &QNetworkReply::downloadProgress,
		[&progress, reply, cancel](qint64 received, qint64 total)
		{
			if (cancel != nullptr && cancel->load()) { reply->abort(); return; }
			if (progress) { progress(received, total); }
		});

	QEventLoop loop;
	QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	loop.exec();

	oversize = oversize || !appendCapped(reply, partFile, maxBytes);
	const bool cancelled = cancel != nullptr && cancel->load();
	const bool ok = !cancelled && !oversize && reply->error() == QNetworkReply::NoError;
	if (!ok) { *networkError = transferFailure(cancelled, oversize, maxBytes, *reply); }
	partFile.close();
	reply->deleteLater();
	return ok;
}

} // namespace




bool StemModelStore::download(const StemModelSpec& spec,
	const QString& destDir,
	const DownloadProgressFn& progress,
	QString* error,
	const std::atomic<bool>* cancel)
{
	// A file already in place that verifies against the pinned spec IS the download:
	// nothing is fetched, so neither offline mode nor a dead network refuses it. The
	// policy refusals (HTTPS, pinned) still come first - an unpinned spec has nothing
	// to verify a file against.
	if (verifiedInPlace(spec, destDir)) { return true; }
	const QString refusal = downloadRefusal(spec, destDir);
	if (!refusal.isEmpty())
	{
		setError(error, refusal);
		return false;
	}

	QDir dir(destDir);
	if (!dir.mkpath(QStringLiteral(".")))
	{
		setError(error, QStringLiteral("Cannot create model directory %1").arg(destDir));
		return false;
	}
	const auto finalPath = dir.filePath(spec.name + QStringLiteral(".onnx"));
	const auto partPath = finalPath + QStringLiteral(".part");

	QFile partFile(partPath);
	if (!partFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		setError(error, QStringLiteral("Cannot write %1: %2").arg(partPath, partFile.errorString()));
		return false;
	}

	QString networkError;
	if (!transfer(spec.url, partFile, spec.sizeBytes, progress, cancel, &networkError))
	{
		QFile::remove(partPath);
		setError(error, QStringLiteral("Download failed: %1").arg(networkError));
		return false;
	}

	if (!verify(partPath, spec.sha256, spec.sizeBytes, error))
	{
		// Never leave a corrupt/partial file behind, never overwrite a good one.
		QFile::remove(partPath);
		return false;
	}
	QFile::remove(finalPath);
	if (!QFile::rename(partPath, finalPath))
	{
		setError(error, QStringLiteral("Cannot move %1 into place").arg(finalPath));
		QFile::remove(partPath);
		return false;
	}
	return true;
}

} // namespace lmms
