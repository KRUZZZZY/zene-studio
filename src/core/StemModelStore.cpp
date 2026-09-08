/*
 * StemModelStore.cpp - model discovery, checksum validation and optional download
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
	// Intentionally unset until G3 pins the exact file URL and SHA-256 from the
	// model card. download() refuses to fetch anything that is not pinned, so
	// a guessed URL can never turn into an unverified download.
	spec.url = QString();
	spec.sha256 = QString();
	spec.sizeBytes = 0;
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
		setError(error, QStringLiteral("Model SHA-256 mismatch: expected %1, got %2")
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




bool StemModelStore::download(const StemModelSpec& spec,
	const QString& destDir,
	const DownloadProgressFn& progress,
	QString* error)
{
	if (!isDownloadUrlAllowed(spec.url))
	{
		setError(error, QStringLiteral("Refusing non-HTTPS or empty download URL for model '%1'")
			.arg(spec.name));
		return false;
	}
	if (spec.sha256.trimmed().isEmpty() || spec.sizeBytes <= 0)
	{
		setError(error, QStringLiteral("Refusing to download unpinned model '%1' "
			"(no SHA-256/size); pin it from the model card first").arg(spec.name));
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

	QNetworkAccessManager manager;
	QNetworkRequest request(QUrl(spec.url));
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LMMS-stem-split/1.0"));

	QNetworkReply* reply = manager.get(request);
	QObject::connect(reply, &QNetworkReply::downloadProgress,
		[&progress](qint64 received, qint64 total) { if (progress) { progress(received, total); } });

	QEventLoop loop;
	QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	loop.exec();

	bool ok = (reply->error() == QNetworkReply::NoError);
	QString networkError;
	if (!ok)
	{
		networkError = reply->errorString();
	}
	partFile.write(reply->readAll());
	partFile.close();
	reply->deleteLater();

	if (!ok)
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
