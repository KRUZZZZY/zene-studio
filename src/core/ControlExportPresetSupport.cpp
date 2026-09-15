/*
 * ControlExportPresetSupport.cpp - the saved render/export preset store and the
 *                                   settings the NEXT render asks for.
 *
 * The design (a private, cross-project store of one JSON document per preset;
 * the applied preset becoming the render child's own command line) is argued in
 * the header. What matters here is that each of the three settings has exactly
 * ONE name and ONE parser: the wire name a document carries and the flag the
 * child is started with are derived from the same pair of functions, so a preset
 * cannot store a bit depth the render path then reads as something else.
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

#include "ControlExportPresetSupport.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

#include "ConfigManager.h"

namespace lmms
{

namespace
{

//! The file suffix of a stored render preset, and the marker the document's own
//! "kind" field carries. The marker is what makes a refusal readable: a JSON
//! file that is not one of ours is named as such instead of being read as a
//! half-empty preset.
QString exportPresetSuffix() { return QStringLiteral(".zrp"); }
QString exportPresetKind() { return QStringLiteral("zenerenderpreset"); }
constexpr int kExportPresetVersion = 1;

//! The document's own field names, once each.
QString kindKey() { return QStringLiteral("kind"); }
QString versionKey() { return QStringLiteral("version"); }
QString nameKey() { return QStringLiteral("name"); }
QString sampleRateKey() { return QStringLiteral("sample_rate"); }
QString bitDepthKey() { return QStringLiteral("bit_depth"); }
QString stereoModeKey() { return QStringLiteral("stereo_mode"); }

ControlResult invalidArgs(const QString& message)
{
	return ControlResult::failure(ControlErrorKind::InvalidArgs, message);
}

//! A required field of the document, or the typed refusal naming the field and
//! what a document must carry. Absent and null are the same thing here: a
//! document that names a field without a value states nothing.
bool requiredField(const QJsonObject& document, const QString& key, QJsonValue* out,
	ControlResult* error)
{
	if (!document.contains(key) || document.value(key).isNull() || document.value(key).isUndefined())
	{
		*error = invalidArgs(QStringLiteral("the preset document has no \"%1\"; a render preset "
			"states all three of \"%2\", \"%3\" and \"%4\" or it is refused rather than applied "
			"with a default nobody asked for").arg(key, sampleRateKey(), bitDepthKey(),
				stereoModeKey()));
		return false;
	}
	*out = document.value(key);
	return true;
}

} // namespace

QString controlExportPresetDir()
{
	return ConfigManager::inst()->userPresetsDir() + QStringLiteral("renderpresets/");
}

QString controlExportPresetSuffix()
{
	return exportPresetSuffix();
}

bool controlExportPresetPath(const QString& name, QString* path, ControlResult* error)
{
	QString bare = name;
	if (bare.endsWith(exportPresetSuffix(), Qt::CaseInsensitive))
	{
		bare.chop(exportPresetSuffix().size());
	}
	// ONE name rule for the whole preset tree: plugin.preset_save's own check,
	// reused rather than restated.
	if (!controlSafePresetName(bare, error)) { return false; }
	*path = controlExportPresetDir() + bare + exportPresetSuffix();
	return true;
}

QString controlExportPresetName(const QString& path)
{
	return QFileInfo(path).completeBaseName();
}

QStringList controlExportPresetFiles()
{
	QStringList files;
	const QDir dir(controlExportPresetDir());
	for (const QFileInfo& entry : dir.entryInfoList({QStringLiteral("*") + exportPresetSuffix()},
			QDir::Files, QDir::Name))
	{
		files.append(entry.absoluteFilePath());
	}
	return files;
}

const char* controlExportPresetBitDepthName(OutputSettings::BitDepth depth)
{
	switch (depth)
	{
		case OutputSettings::BitDepth::Depth16Bit: return "16";
		case OutputSettings::BitDepth::Depth24Bit: return "24";
		case OutputSettings::BitDepth::Depth32Bit: return "32";
		case OutputSettings::BitDepth::Count: break;
	}
	return "";
}

bool controlExportPresetBitDepthFromName(const QString& name, OutputSettings::BitDepth* out)
{
	for (int i = 0; i < static_cast<int>(OutputSettings::BitDepth::Count); ++i)
	{
		const auto depth = static_cast<OutputSettings::BitDepth>(i);
		if (name == QLatin1String(controlExportPresetBitDepthName(depth)))
		{
			*out = depth;
			return true;
		}
	}
	return false;
}

const char* controlExportPresetStereoModeName(OutputSettings::StereoMode mode)
{
	switch (mode)
	{
		case OutputSettings::StereoMode::Mono: return "mono";
		case OutputSettings::StereoMode::Stereo: return "stereo";
		case OutputSettings::StereoMode::JointStereo: return "jointstereo";
		case OutputSettings::StereoMode::Count: break;
	}
	return "";
}

bool controlExportPresetStereoModeFromName(const QString& name, OutputSettings::StereoMode* out)
{
	for (int i = 0; i < static_cast<int>(OutputSettings::StereoMode::Count); ++i)
	{
		const auto mode = static_cast<OutputSettings::StereoMode>(i);
		if (name.compare(QLatin1String(controlExportPresetStereoModeName(mode)),
				Qt::CaseInsensitive) == 0)
		{
			*out = mode;
			return true;
		}
	}
	return false;
}

sample_rate_t controlExportPresetMinSampleRate() { return 44100; }
sample_rate_t controlExportPresetMaxSampleRate() { return 192000; }

QString controlExportPresetValidate(const ControlExportPreset& preset)
{
	if (preset.sampleRate < controlExportPresetMinSampleRate()
		|| preset.sampleRate > controlExportPresetMaxSampleRate())
	{
		return QStringLiteral("sample_rate %1 is outside the window the render path accepts "
			"(%2 to %3 Hz): a preset outside it would name a render this build cannot perform")
			.arg(preset.sampleRate)
			.arg(controlExportPresetMinSampleRate())
			.arg(controlExportPresetMaxSampleRate());
	}
	if (controlExportPresetBitDepthName(preset.bitDepth).isEmpty())
	{
		return QStringLiteral("bit_depth is not one of 16, 24 or 32");
	}
	if (controlExportPresetStereoModeName(preset.stereoMode).isEmpty())
	{
		return QStringLiteral("stereo_mode is not one of mono, stereo or jointstereo");
	}
	return QString();
}

QByteArray controlExportPresetDocument(const ControlExportPreset& preset)
{
	QJsonObject document;
	document.insert(kindKey(), exportPresetKind());
	document.insert(versionKey(), kExportPresetVersion);
	document.insert(nameKey(), preset.name);
	document.insert(sampleRateKey(), static_cast<int>(preset.sampleRate));
	document.insert(bitDepthKey(),
		QString::fromLatin1(controlExportPresetBitDepthName(preset.bitDepth)));
	document.insert(stereoModeKey(),
		QString::fromLatin1(controlExportPresetStereoModeName(preset.stereoMode)));
	return QJsonDocument(document).toJson(QJsonDocument::Indented);
}

bool controlExportPresetFromBytes(const QByteArray& bytes, const QString& path,
	ControlExportPreset* out, ControlResult* error)
{
	QJsonParseError parseError;
	const QJsonDocument parsed = QJsonDocument::fromJson(bytes, &parseError);
	if (parsed.isNull() || !parsed.isObject())
	{
		*error = invalidArgs(QStringLiteral("%1 is not a render preset document: %2 (offset %3)")
			.arg(path, parseError.errorString()).arg(parseError.offset));
		return false;
	}
	const QJsonObject document = parsed.object();
	if (document.value(kindKey()).toString() != exportPresetKind())
	{
		*error = invalidArgs(QStringLiteral("%1 is not a render preset document (its \"%2\" is "
			"\"%3\", not \"%4\")").arg(path, kindKey(),
				document.value(kindKey()).toString(), exportPresetKind()));
		return false;
	}
	if (document.value(versionKey()).toInt() != kExportPresetVersion)
	{
		*error = invalidArgs(QStringLiteral("%1 carries render preset version %2; this build "
			"reads version %3 only").arg(path).arg(document.value(versionKey()).toInt())
			.arg(kExportPresetVersion));
		return false;
	}

	ControlExportPreset preset;
	preset.path = path;
	// The FILE NAME is the store's key, exactly as it is for a chain preset: the
	// document's own "name" is only the name it was captured under.
	preset.name = controlExportPresetName(path);
	if (preset.name.isEmpty()) { preset.name = document.value(nameKey()).toString(); }

	QJsonValue field;
	if (!requiredField(document, sampleRateKey(), &field, error)) { return false; }
	if (!field.isDouble())
	{
		*error = invalidArgs(QStringLiteral("%1: \"%2\" is not a number").arg(path, sampleRateKey()));
		return false;
	}
	preset.sampleRate = static_cast<sample_rate_t>(field.toInt());

	if (!requiredField(document, bitDepthKey(), &field, error)) { return false; }
	const QString depthName = field.isString() ? field.toString()
		: QString::number(field.toInt());
	if (!controlExportPresetBitDepthFromName(depthName, &preset.bitDepth))
	{
		*error = invalidArgs(QStringLiteral("%1: \"%2\" is \"%3\"; this build writes 16, 24 or 32")
			.arg(path, bitDepthKey(), field.toVariant().toString()));
		return false;
	}

	if (!requiredField(document, stereoModeKey(), &field, error)) { return false; }
	if (!controlExportPresetStereoModeFromName(field.toString(), &preset.stereoMode))
	{
		*error = invalidArgs(QStringLiteral("%1: \"%2\" is \"%3\"; this build writes mono, stereo "
			"or jointstereo").arg(path, stereoModeKey(), field.toString()));
		return false;
	}

	// The stored record is checked against the SAME rule a fresh one is, so a
	// document that was edited by hand cannot reach a render the store's own
	// writer would have refused.
	const QString invalid = controlExportPresetValidate(preset);
	if (!invalid.isEmpty())
	{
		*error = invalidArgs(QStringLiteral("%1: %2").arg(path, invalid));
		return false;
	}

	*out = preset;
	return true;
}

QJsonObject controlExportPresetJson(const ControlExportPreset& preset, bool detailed)
{
	QJsonObject out;
	out.insert(QStringLiteral("name"), preset.name);
	out.insert(QStringLiteral("path"), preset.path);
	out.insert(QStringLiteral("sample_rate"), static_cast<int>(preset.sampleRate));
	out.insert(QStringLiteral("bit_depth"),
		QString::fromLatin1(controlExportPresetBitDepthName(preset.bitDepth)));
	out.insert(QStringLiteral("stereo_mode"),
		QString::fromLatin1(controlExportPresetStereoModeName(preset.stereoMode)));
	if (detailed)
	{
		const QFileInfo info(preset.path);
		out.insert(QStringLiteral("bytes"), static_cast<qint64>(info.size()));
	}
	return out;
}

QStringList controlExportPresetRenderArgs(const ControlExportPreset& settings)
{
	QStringList args;
	args << QStringLiteral("-s") << QString::number(settings.sampleRate);

	// The two flags are passed only when the value is NOT what the CLI already
	// defaults to (BitDepth::Depth16Bit, StereoMode::JointStereo): a render with
	// no preset applied then carries the single argument it always did, which is
	// what makes "an apply is the only thing that changes a render's options" a
	// property of the argument list rather than a claim in a comment.
	if (settings.bitDepth != OutputSettings::BitDepth::Depth16Bit)
	{
		args << QStringLiteral("--bit-depth")
			<< QString::fromLatin1(controlExportPresetBitDepthName(settings.bitDepth));
	}
	if (settings.stereoMode != OutputSettings::StereoMode::JointStereo)
	{
		const char* mode = settings.stereoMode == OutputSettings::StereoMode::Mono ? "m" : "s";
		args << QStringLiteral("-m") << QString::fromLatin1(mode);
	}
	return args;
}

bool ControlExportPresetSettings::s_active = false;
ControlExportPreset ControlExportPresetSettings::s_preset;

bool ControlExportPresetSettings::active(ControlExportPreset* out)
{
	if (!s_active) { return false; }
	if (out != nullptr) { *out = s_preset; }
	return true;
}

QString ControlExportPresetSettings::activeName()
{
	return s_active ? s_preset.name : QString();
}

ControlExportPreset ControlExportPresetSettings::effective()
{
	return s_active ? s_preset : ControlExportPreset();
}

void ControlExportPresetSettings::setActive(const ControlExportPreset& preset)
{
	s_preset = preset;
	s_active = true;
}

void ControlExportPresetSettings::clear()
{
	s_preset = ControlExportPreset();
	s_active = false;
}

} // namespace lmms
