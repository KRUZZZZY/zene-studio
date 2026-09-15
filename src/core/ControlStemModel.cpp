/*
 * ControlStemModel.cpp - the model-store half of the stem.* surface.
 *
 * Split out of ControlStemSupport.cpp because the file-length ratchet reads a
 * file as a unit: what lives here is the model store's own policy exposed to a
 * client - where the model file is, whether it is present, what the store WOULD
 * download (and whether that spec is pinned enough to be worth attempting), and
 * the one download entry point, which refuses an unpinned spec rather than
 * guessing a URL.
 *
 * The store's own rules are not repeated here: include/StemSeparation/
 * StemModelStore.h holds them ("Models are NEVER bundled with LMMS: they are
 * optional downloads, HTTPS-only, with a pinned SHA-256 checksum"), and this
 * file only reports them and drives them. doc/STEM-SPLIT.md, "Model handling",
 * is the engine-side document.
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

#include "ControlStemSupport.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QString>

#include "StemSeparation/StemModelStore.h"

namespace lmms
{
namespace control
{

QJsonObject stemModelState(bool withHash)
{
	const StemModelSpec spec = StemModelStore::defaultModelSpec();
	const QString path = StemModelStore::defaultModelPath();
	const QFileInfo info(path);

	QJsonObject specJson;
	specJson.insert(QStringLiteral("name"), spec.name);
	specJson.insert(QStringLiteral("url"), spec.url);
	specJson.insert(QStringLiteral("sha256"), spec.sha256);
	specJson.insert(QStringLiteral("size_bytes"), static_cast<double>(spec.sizeBytes));
	specJson.insert(QStringLiteral("license"), spec.license);
	specJson.insert(QStringLiteral("license_url"), spec.licenseUrl);
	specJson.insert(QStringLiteral("model_card_url"), spec.modelCardUrl);
	const bool pinned = !spec.sha256.trimmed().isEmpty() && spec.sizeBytes > 0
		&& StemModelStore::isDownloadUrlAllowed(spec.url);
	specJson.insert(QStringLiteral("pinned"), pinned);

	QJsonObject out;
	out.insert(QStringLiteral("dir"), StemModelStore::defaultModelDir());
	out.insert(QStringLiteral("path"), path);
	out.insert(QStringLiteral("present"), StemModelStore::isModelPresent(path));
	out.insert(QStringLiteral("bytes"), static_cast<double>(info.size()));
	out.insert(QStringLiteral("spec"), specJson);
	// The policy, in the store's own terms: an unpinned spec is never fetched,
	// and this is what makes the "never bundled, always verified" rule
	// enforceable rather than aspirational.
	out.insert(QStringLiteral("download_allowed"), pinned);
	out.insert(QStringLiteral("download_reason"), pinned
		? QStringLiteral("the spec is pinned (HTTPS URL, SHA-256 and size present)")
		: QStringLiteral("the default spec is deliberately unpinned in v1: take the URL and the "
			"SHA-256 from the model card (%1) and pass them to stem.model_download, or place the "
			"file at 'path' by hand").arg(spec.modelCardUrl));
	QJsonObject env;
	env.insert(QStringLiteral("LMMS_STEM_MODEL"), qEnvironmentVariable("LMMS_STEM_MODEL"));
	env.insert(QStringLiteral("LMMS_STEM_MODEL_DIR"), qEnvironmentVariable("LMMS_STEM_MODEL_DIR"));
	out.insert(QStringLiteral("env"), env);

	if (withHash)
	{
		QString error;
		const QString hash = StemModelStore::sha256OfFile(path, &error);
		out.insert(QStringLiteral("sha256"), hash);
		out.insert(QStringLiteral("hash_error"), hash.isEmpty() ? error : QString());
		// null when there is nothing to compare against - never a bare "true"
		// that a caller could read as "this file is the model".
		out.insert(QStringLiteral("matches_spec"), pinned
			? QJsonValue(hash.compare(spec.sha256.trimmed(), Qt::CaseInsensitive) == 0)
			: QJsonValue(QJsonValue::Null));
	}
	return out;
}

bool stemModelDownload(const QString& url,
	const QString& sha256,
	qint64 sizeBytes,
	const QString& name,
	const QString& destDir,
	QJsonObject* result,
	QString* error)
{
	StemModelSpec spec = StemModelStore::defaultModelSpec();
	if (!url.isEmpty()) { spec.url = url; }
	if (sha256.isEmpty() && sizeBytes <= 0 && url.isEmpty())
	{
		// The default path: the store's own spec, which is unpinned by policy.
		*error = QStringLiteral("refusing to download the default model spec: it is deliberately "
			"unpinned in v1 (no URL, no SHA-256, no size). Take the URL and checksum from the "
			"model card (%1) and pass url/sha256/size_bytes, or place the file at '%2' by hand")
			.arg(spec.modelCardUrl).arg(StemModelStore::defaultModelPath());
		return false;
	}
	spec.sha256 = sha256;
	spec.sizeBytes = sizeBytes;
	if (!name.isEmpty()) { spec.name = name; }
	if (!StemModelStore::isDownloadUrlAllowed(spec.url))
	{
		*error = QStringLiteral("refusing '%1': the model store downloads over HTTPS only, from a "
			"URL with a host (http://, file:// and relative paths are rejected)")
			.arg(spec.url);
		return false;
	}
	if (spec.sha256.trimmed().isEmpty() || spec.sizeBytes <= 0)
	{
		*error = QStringLiteral("refusing '%1': a download must be pinned with both 'sha256' and "
			"'size_bytes' - the store verifies the file before it is moved into place")
			.arg(spec.name);
		return false;
	}

	const QString directory = destDir.isEmpty() ? StemModelStore::defaultModelDir() : destDir;
	QString storeError;
	if (!StemModelStore::download(spec, directory, StemModelStore::DownloadProgressFn(), &storeError))
	{
		*error = storeError;
		return false;
	}
	const QString path = QDir(directory).filePath(spec.name + QStringLiteral(".onnx"));

	QJsonObject out;
	out.insert(QStringLiteral("name"), spec.name);
	out.insert(QStringLiteral("path"), path);
	out.insert(QStringLiteral("bytes"), static_cast<double>(QFileInfo(path).size()));
	out.insert(QStringLiteral("sha256"), spec.sha256);
	out.insert(QStringLiteral("verified"), true);
	out.insert(QStringLiteral("model_card_url"), spec.modelCardUrl);
	*result = out;
	return true;
}

} // namespace control
} // namespace lmms
