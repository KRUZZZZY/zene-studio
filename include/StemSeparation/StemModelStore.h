/*
 * StemModelStore.h - model discovery, checksum validation and optional download
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

#ifndef LMMS_STEM_MODEL_STORE_H
#define LMMS_STEM_MODEL_STORE_H

#include <functional>

#include <QString>
#include <QtGlobal>

#include "lmms_export.h"

namespace lmms
{

// Description of a downloadable stem-separation model. Models are NEVER
// bundled with LMMS: they are optional downloads, HTTPS-only, with a pinned
// SHA-256 checksum (see doc/STEM-SPLIT.md, "Model handling").
struct StemModelSpec
{
	QString name;
	QString url;
	QString sha256;				// lowercase hex; empty = unpinned, download refused
	qint64 sizeBytes = 0;		// 0 = not pinned
	QString license;
	QString licenseUrl;
	QString modelCardUrl;		// human-facing page for manual download
};

class LMMS_EXPORT StemModelStore
{
public:
	using DownloadProgressFn = std::function<void(qint64 received, qint64 total)>;

	// <AppDataLocation>/models/stems, overridable with LMMS_STEM_MODEL_DIR.
	static QString defaultModelDir();

	// Absolute path of a model file inside the default model dir.
	static QString modelPath(const QString& fileName);

	// Path from LMMS_STEM_MODEL, else modelPath(spec.name + ".onnx").
	static QString defaultModelPath();

	// HTDemucs fp16 (166 MB), MIT. The URL and checksum are intentionally
	// unpinned in v1: they must be taken from the model card at G3 rather than
	// guessed. download() refuses unpinned specs, which is what makes the
	// "never bundled, always verified" policy enforceable.
	static StemModelSpec defaultModelSpec();

	static bool isModelPresent(const QString& filePath, QString* error = nullptr);

	// Lowercase hex SHA-256 of a file; empty string on error.
	static QString sha256OfFile(const QString& filePath, QString* error = nullptr);

	// Checksum + size validation. `expectedSha256` is case-insensitive.
	static bool verify(const QString& filePath,
		const QString& expectedSha256,
		qint64 expectedSize,
		QString* error = nullptr);

	// HTTPS only. http://, file:// and relative URLs are rejected.
	static bool isDownloadUrlAllowed(const QString& url);

	// Downloads to `<destDir>/<spec.name>.onnx.part`, verifies size and
	// SHA-256, then atomically renames into place. Returns false and sets
	// `error` on any failure; a partial file never replaces a good one.
	static bool download(const StemModelSpec& spec,
		const QString& destDir,
		const DownloadProgressFn& progress,
		QString* error = nullptr);
};

} // namespace lmms

#endif // LMMS_STEM_MODEL_STORE_H
