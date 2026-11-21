/*
 * LoopImportWarp.cpp - an imported loop's tempo and key detected, and followed or kept (see LoopImportWarp.h)
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
#include "LoopImportWarp.h"

#include <cmath>

#include <QDomDocument>
#include <QDomElement>

#include "ConfigManager.h"
#include "ImportDetection.h"
#include "ProjectKey.h"
#include "RubberBandStretch.h"
#include "SampleClip.h"

namespace lmms::loopwarp
{

namespace
{

const QString FollowName = QStringLiteral("follow_project");
const QString KeepName = QStringLiteral("keep_unstretched");
const QString ConfigClass = QStringLiteral("import");
const QString ConfigAttribute = QStringLiteral("loopwarp");

QStringList applyTempo(SampleClip* clip, LoopSource* source)
{
	if (!source->tempoFound)
	{
		return {QStringLiteral("no tempo was detected, so the clip keeps its source duration")};
	}
	// A whole BPM, as detect.apply writes one: loops are cut at whole tempos, and the exact estimate is kept beside it.
	const auto bpm = static_cast<float>(std::lround(source->bpm));
	if (bpm <= 0.0f) { return {QStringLiteral("the detected tempo is not usable, so the clip keeps its source duration")}; }
	clip->setSourceTempo(bpm);
	clip->setWarpTempoMode(WarpTempoMode::SourceTempo);
	clip->setWarpStretchMode(WarpStretchMode::PreservePitch);
	clip->updateLength();
	source->tempoFollowed = true;
	return {};
}

QStringList applyKey(SampleClip* clip, LoopSource* source, const ProjectKey& projectKey)
{
	if (!source->keyFound) { return {QStringLiteral("no key was detected, so the clip is not transposed")}; }
	if (projectKey.empty() || projectKey.tonicPitchClass() < 0)
	{
		return {QStringLiteral("the project has no key, so the clip is not transposed")};
	}
	const int semitones = semitonesToward(source->tonicPitchClass, projectKey.tonicPitchClass());
	if (semitones != 0 && !RubberBandPool::available())
	{
		// clip.set_transpose's own refusal: a sample transpose keeps its timing only through Rubber Band.
		return {QStringLiteral("transposing a sample clip keeps its timing through Rubber Band, which this build "
			"does not have, so the clip is not transposed")};
	}
	if (semitones != 0)
	{
		ClipEdits edits = clip->clipEdits();
		edits.transpose = semitones;
		clip->setClipEdits(edits);
		clip->setWarpStretchMode(WarpStretchMode::Complex);  // what clip.set_transpose sets for a sample transpose
	}
	source->keyFollowed = true;
	source->transposeApplied = semitones;
	return {};
}

QString tempoModeName(WarpTempoMode mode)
{
	return mode == WarpTempoMode::SourceTempo ? QStringLiteral("source") : QStringLiteral("follow");
}

} // namespace

QString modeName(Mode mode) { return mode == Mode::FollowProject ? FollowName : KeepName; }

bool modeFromName(const QString& name, Mode* mode)
{
	if (name == FollowName) { *mode = Mode::FollowProject; return true; }
	if (name == KeepName) { *mode = Mode::KeepUnstretched; return true; }
	return false;
}

QString preferenceKey() { return ConfigClass + QLatin1Char('/') + ConfigAttribute; }

Mode preferredMode()
{
	Mode mode = Mode::KeepUnstretched;
	if (ConfigManager::inst() == nullptr) { return mode; }
	modeFromName(ConfigManager::inst()->value(ConfigClass, ConfigAttribute), &mode);
	return mode;
}

int semitonesToward(int from, int to)
{
	int delta = ((to - from) % 12 + 12) % 12;
	if (delta > 6) { delta -= 12; }
	return delta;
}

LoopSource analyse(const QString& path, QString* error)
{
	LoopSource source;
	const ImportDetectionResult analysis = analyseAudioFile(path);
	if (!analysis.ok)
	{
		if (error != nullptr) { *error = analysis.error; }
		return source;
	}
	source.analysed = true;
	source.tempoFound = analysis.tempo.found && std::isfinite(analysis.tempo.bpm) && analysis.tempo.bpm > 0.0;
	source.bpm = source.tempoFound ? analysis.tempo.bpm : 0.0;
	source.keyFound = analysis.key.found && analysis.tonicPitchClass >= 0;
	source.tonicPitchClass = source.keyFound ? analysis.tonicPitchClass : -1;
	source.tonicName = source.keyFound ? analysis.tonicName : QString();
	source.scaleName = source.keyFound ? analysis.scaleName : QString();
	return source;
}

QStringList apply(SampleClip* clip, LoopSource source, Mode mode, const ProjectKey& projectKey)
{
	source.followProject = mode == Mode::FollowProject;
	source.tempoFollowed = false;
	source.keyFollowed = false;
	source.transposeApplied = 0;
	QStringList notes;
	if (source.analysed && source.followProject)
	{
		notes += applyTempo(clip, &source);
		notes += applyKey(clip, &source, projectKey);
	}
	clip->setLoopSource(source);
	return notes;
}

QJsonObject sourceJson(const LoopSource& source)
{
	QJsonObject tempo{{QStringLiteral("found"), source.tempoFound}, {QStringLiteral("bpm"), source.bpm}};
	QJsonObject key{{QStringLiteral("found"), source.keyFound}, {QStringLiteral("pitch_class"), source.tonicPitchClass},
		{QStringLiteral("tonic"), source.tonicName}, {QStringLiteral("scale"), source.scaleName}};
	return QJsonObject{
		{QStringLiteral("analysed"), source.analysed},
		{QStringLiteral("source_tempo"), tempo},
		{QStringLiteral("source_key"), key},
		{QStringLiteral("loop_mode"), source.analysed ? modeName(source.followProject ? Mode::FollowProject : Mode::KeepUnstretched)
			: QString()},
		{QStringLiteral("tempo_followed"), source.tempoFollowed},
		{QStringLiteral("key_followed"), source.keyFollowed},
		{QStringLiteral("transpose_applied"), source.transposeApplied},
	};
}

QJsonObject clipStateJson(const SampleClip& clip)
{
	QJsonObject state = sourceJson(clip.loopSource());
	state.insert(QStringLiteral("tempo_mode"), tempoModeName(clip.warpTempoMode()));
	state.insert(QStringLiteral("warp_source_tempo"), static_cast<double>(clip.sourceTempo()));
	state.insert(QStringLiteral("stretch"), warpStretchModeName(clip.warpStretchMode()));
	state.insert(QStringLiteral("transpose"), clip.clipEdits().transpose);
	state.insert(QStringLiteral("length"), clip.length().getTicks());
	return state;
}

void saveLoopSource(QDomDocument& doc, QDomElement& clipElement, const LoopSource& source)
{
	QDomElement element = doc.createElement(QStringLiteral("loopsource"));
	if (source.tempoFound) { element.setAttribute(QStringLiteral("bpm"), QString::number(source.bpm, 'g', 17)); }
	if (source.keyFound)
	{
		element.setAttribute(QStringLiteral("tonic"), source.tonicPitchClass);
		element.setAttribute(QStringLiteral("tonicname"), source.tonicName);
		element.setAttribute(QStringLiteral("scale"), source.scaleName);
	}
	element.setAttribute(QStringLiteral("mode"), modeName(source.followProject ? Mode::FollowProject : Mode::KeepUnstretched));
	element.setAttribute(QStringLiteral("tempofollowed"), source.tempoFollowed ? 1 : 0);
	element.setAttribute(QStringLiteral("keyfollowed"), source.keyFollowed ? 1 : 0);
	element.setAttribute(QStringLiteral("transpose"), source.transposeApplied);
	clipElement.appendChild(element);
}

LoopSource loadLoopSource(const QDomElement& clipElement)
{
	LoopSource source;
	const QDomElement element = clipElement.firstChildElement(QStringLiteral("loopsource"));
	if (element.isNull()) { return source; }
	source.analysed = true;
	bool ok = false;
	const double bpm = element.attribute(QStringLiteral("bpm")).toDouble(&ok);
	source.tempoFound = ok && std::isfinite(bpm) && bpm > 0.0;
	source.bpm = source.tempoFound ? bpm : 0.0;
	const int tonic = element.attribute(QStringLiteral("tonic")).toInt(&ok);
	source.keyFound = ok && tonic >= 0 && tonic < 12;
	source.tonicPitchClass = source.keyFound ? tonic : -1;
	source.tonicName = source.keyFound ? element.attribute(QStringLiteral("tonicname")) : QString();
	source.scaleName = source.keyFound ? element.attribute(QStringLiteral("scale")) : QString();
	source.followProject = element.attribute(QStringLiteral("mode")) == FollowName;
	source.tempoFollowed = element.attribute(QStringLiteral("tempofollowed")).toInt() == 1;
	source.keyFollowed = element.attribute(QStringLiteral("keyfollowed")).toInt() == 1;
	source.transposeApplied = element.attribute(QStringLiteral("transpose")).toInt();
	return source;
}

} // namespace lmms::loopwarp
