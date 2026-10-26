/*
 * ControlCommandsSampleStereo.cpp - Q050-106: split a stereo source into two mono files
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

/*! 0.5.0 L05 (Q050-106, R-editing-11): `sample.split_stereo` writes a sample clip's left and
 *  right channels as two mono WAV files. File export only: the clip and the project are not
 *  touched, so the command is not_mutating (the render.render class). The outputs are float32
 *  WAV at the source rate, one sample per source frame - no resampling, trimming or mixing. */

#include <vector>

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QSaveFile>
#include <QString>

#include <sndfile.h>

#include "Clip.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "SampleClip.h"
#include "SampleOperators.h"

namespace lmms
{

using namespace control;

namespace
{

bool isWavPath(const QString& path)
{
	return path.endsWith(QLatin1String(".wav"), Qt::CaseInsensitive);
}

//! Write @a samples as a mono float32 WAV into @a file's temporary path. The caller commits
//! (or cancels) the QSaveFile only after BOTH channels have been written, so a failure on the
//! second file leaves neither destination half-replaced by a truncated one.
bool writeMonoWav(QSaveFile& file, const std::vector<float>& samples, int sampleRate, QString* error)
{
	if (!QDir().mkpath(QFileInfo(file.fileName()).absolutePath()))
	{
		*error = QStringLiteral("cannot create the directory for %1").arg(file.fileName());
		return false;
	}
	if (!file.open(QIODevice::WriteOnly))
	{
		*error = QStringLiteral("cannot write %1: %2").arg(file.fileName(), file.errorString());
		return false;
	}
	SF_INFO info{};
	info.samplerate = sampleRate;
	info.channels = 1;
	info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
	SNDFILE* handle = sf_open_fd(file.handle(), SFM_WRITE, &info, SF_FALSE);
	if (handle == nullptr)
	{
		file.cancelWriting();
		*error = QStringLiteral("cannot write %1: %2").arg(file.fileName(),
			QString::fromLocal8Bit(sf_strerror(nullptr)));
		return false;
	}
	const auto frames = static_cast<sf_count_t>(samples.size());
	const bool written = samples.empty() || sf_writef_float(handle, samples.data(), frames) == frames;
	const bool closed = sf_close(handle) == 0;
	if (!written || !closed)
	{
		file.cancelWriting();
		*error = QStringLiteral("writing %1 failed").arg(file.fileName());
		return false;
	}
	return true;
}

bool resolveStereoClip(const QString& id, ClipRef* ref, SampleClip** clip, ControlResult* error)
{
	if (!resolveClip(id, ref, error)) { return false; }
	auto* sample = dynamic_cast<SampleClip*>(ref->clip);
	if (sample == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is not a sample clip: only sample audio has channels to split").arg(id));
		return false;
	}
	if (sample->sample().sampleSize() == 0)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 plays no source, so there is nothing to split").arg(id));
		return false;
	}
	if (sample->sample().channels() != DEFAULT_CHANNELS)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 is mono already: splitting it would write the same samples twice").arg(id));
		return false;
	}
	*clip = sample;
	return true;
}

//! The destination as the filesystem sees it: the deepest part that already exists is resolved
//! through symlinks, and the part that does not exist yet is kept as written. Two spellings of one
//! output (a symlinked directory beside its target) therefore compare equal.
QString resolvedDestination(const QString& path)
{
	const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
	QString suffix;
	QString existing = absolute;
	while (!QFileInfo::exists(existing))
	{
		const int slash = existing.lastIndexOf(QLatin1Char('/'));
		if (slash <= 0) { return absolute; }
		suffix = existing.mid(slash) + suffix;
		existing.truncate(slash);
	}
	const QString canonical = QFileInfo(existing).canonicalFilePath();
	return canonical.isEmpty() ? absolute : QDir::cleanPath(canonical + suffix);
}

bool resolveDestinations(const QJsonObject& args, QString* leftPath, QString* rightPath, ControlResult* error)
{
	*leftPath = args.value(QStringLiteral("left_path")).toString();
	*rightPath = args.value(QStringLiteral("right_path")).toString();
	if (leftPath->isEmpty() || rightPath->isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'left_path' and 'right_path' are the two mono files to write"));
		return false;
	}
	if (!isWavPath(*leftPath) || !isWavPath(*rightPath))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'left_path' and 'right_path' are .wav files"));
		return false;
	}
	const QString absoluteLeft = resolvedDestination(*leftPath);
	const QString absoluteRight = resolvedDestination(*rightPath);
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
	const bool sameDestination = absoluteLeft.compare(absoluteRight, Qt::CaseInsensitive) == 0;
#else
	const bool sameDestination = absoluteLeft == absoluteRight;
#endif
	if (sameDestination)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'left_path' and 'right_path' must differ: two channels, two files"));
		return false;
	}
	return true;
}

ControlResult writeDestinations(const QString& leftPath, const QString& rightPath,
	const sampleops::StereoChannels& channels, int rate)
{
	QSaveFile left(leftPath);
	QSaveFile right(rightPath);
	QString writeError;
	if (!writeMonoWav(left, channels.left, rate, &writeError)
		|| !writeMonoWav(right, channels.right, rate, &writeError))
	{
		return ControlResult::failure(ControlErrorKind::Refused, writeError);
	}
	if (!left.commit())
	{
		right.cancelWriting();
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("cannot replace %1: %2").arg(leftPath, left.errorString()));
	}
	if (!right.commit())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("cannot replace %1: %2").arg(rightPath, right.errorString()));
	}
	return ControlResult::success();
}

ControlResult splitStereo(const QJsonObject& args)
{
	const QString id = args.value(QStringLiteral("clip")).toString();
	ClipRef ref;
	SampleClip* clip = nullptr;
	ControlResult error;
	if (!resolveStereoClip(id, &ref, &clip, &error)) { return error; }
	QString leftPath;
	QString rightPath;
	if (!resolveDestinations(args, &leftPath, &rightPath, &error)) { return error; }

	const auto frames = static_cast<f_cnt_t>(clip->sample().sampleSize());
	const int rate = static_cast<int>(clip->sample().sampleRate());
	const sampleops::StereoChannels channels = sampleops::splitStereo(clip->sample().data(), frames);
	const ControlResult written = writeDestinations(leftPath, rightPath, channels, rate);
	if (!written.ok) { return written; }
	QJsonObject result;
	result.insert(QStringLiteral("clip"), clipId(ref.id));
	result.insert(QStringLiteral("left_path"), leftPath);
	result.insert(QStringLiteral("right_path"), rightPath);
	result.insert(QStringLiteral("frames"), static_cast<double>(frames));
	result.insert(QStringLiteral("sample_rate"), rate);
	result.insert(QStringLiteral("channels"), 1);
	result.insert(QStringLiteral("layout"), QStringLiteral("mono"));
	return ControlResult::success(result);
}

} // namespace

void registerSampleStereoCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("sample.split_stereo");
	cmd.group = QStringLiteral("sample");
	cmd.verb = QStringLiteral("split_stereo");
	cmd.description = QStringLiteral("Write a sample clip's whole stereo source as two mono WAV files "
		"(float32, at the source rate): 'left_path' receives the left channel and 'right_path' the right "
		"channel, sample for sample, with no resampling, trimming or mixing. The clip and the project are "
		"not changed. Both paths are .wav files and must differ; a mono clip is refused.");
	cmd.argsSchema = objectSchema(
		{
			{QStringLiteral("clip"), stringProperty()},
			{QStringLiteral("left_path"), stringProperty()},
			{QStringLiteral("right_path"), stringProperty()},
		},
		QJsonArray{QStringLiteral("clip"), QStringLiteral("left_path"), QStringLiteral("right_path")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("left_path"), stringProperty()},
		{QStringLiteral("right_path"), stringProperty()},
		{QStringLiteral("frames"), integerProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("channels"), integerProperty()},
		{QStringLiteral("layout"), stringProperty()},
	});
	cmd.mutating = false;
	cmd.handler = splitStereo;
	registry.registerCommand(cmd);
}

} // namespace lmms
