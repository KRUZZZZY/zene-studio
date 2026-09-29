/*
 * StemModelStore.h - model discovery, checksum validation and optional download
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

#ifndef LMMS_STEM_MODEL_STORE_H
#define LMMS_STEM_MODEL_STORE_H

#include <atomic>
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

	// HTDemucs fp16 (165612636 bytes), MIT, pinned to one commit of the model
	// card's repository (owner decision 14). download() still refuses any spec
	// that is not pinned, which is what keeps "never bundled, always verified"
	// enforceable for a caller-supplied spec.
	static StemModelSpec defaultModelSpec();

	// True when LMMS_STEM_OFFLINE is set to anything but "0": download() then
	// refuses, and a job never fetches a missing model on first use.
	static bool isOffline();

	// Owner decision 14, the first-use fetch both front ends share (the agent
	// surface and the GUI action). canFetchDefaultModel(): the default model is
	// absent, its spec is pinned, LMMS_STEM_MODEL does not name its own file
	// (an explicit path is the operator's, never overwritten) and offline mode
	// is off. fetchDefaultModelIfMissing(): true at once when the model is
	// present, otherwise download()s the default spec into defaultModelDir(),
	// reporting progress in [0, 1]. Blocking: call it from a worker thread
	// (StemJobManager::setModelProvisioner is the intended caller).
	static bool canFetchDefaultModel();
	static bool fetchDefaultModelIfMissing(const std::atomic<bool>& cancel,
		const std::function<void(float)>& progress, QString* error);

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
	// A non-null `cancel` that becomes true aborts the transfer (reported as
	// "cancelled"); the partial file is removed as on any other failure.
	// A file already at `<destDir>/<spec.name>.onnx` that verifies against the
	// pinned spec is success with nothing fetched - offline mode included.
	static bool download(const StemModelSpec& spec,
		const QString& destDir,
		const DownloadProgressFn& progress,
		QString* error = nullptr,
		const std::atomic<bool>* cancel = nullptr);
};

} // namespace lmms

#endif // LMMS_STEM_MODEL_STORE_H
