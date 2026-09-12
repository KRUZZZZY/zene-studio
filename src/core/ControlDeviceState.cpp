/*
 * ControlDeviceState.cpp - device state files and the preset directory layout
 *                          behind plugin.state_* / plugin.preset_* (SPEC A14).
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

#include "ControlDeviceSupport.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

#include "ConfigManager.h"
#include "DataFile.h"
#include "Effect.h"
#include "Instrument.h"
#include "InstrumentTrack.h"

namespace lmms
{

bool resolveControlDevice(const ControlTarget& target, const QString& pluginId,
	ControlDeviceHandle* handle, ControlResult* error)
{
	if (pluginId == QLatin1String("inst"))
	{
		if (target.instrumentTrack == nullptr || target.instrumentTrack->instrument() == nullptr)
		{
			*error = ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("target %1 carries no instrument").arg(target.id));
			return false;
		}
		handle->track = target.instrumentTrack;
		const Instrument* instrument = handle->track->instrument();
		handle->pluginName = QString::fromUtf8(instrument->descriptor()->name);
		// The product's own preset folder name for an instrument track.
		handle->folder = handle->track->instrumentName();
		if (handle->folder.isEmpty()) { handle->folder = handle->pluginName; }
		return true;
	}

	handle->effect = resolveControlEffect(target, pluginId, error);
	if (handle->effect == nullptr) { return false; }
	handle->pluginName = QString::fromUtf8(handle->effect->descriptor()->name);
	// A hosted plugin (LADSPA) is identified by its own label, not by the host
	// module's name, so its presets do not collide with every other LADSPA
	// device's.
	const Plugin::Descriptor::SubPluginFeatures::Key& key = handle->effect->key();
	handle->folder = key.isValid() ? key.attributes.value(QStringLiteral("plugin"))
								   : handle->pluginName;
	if (handle->folder.isEmpty()) { handle->folder = handle->pluginName; }
	return true;
}

QByteArray controlDeviceStateBytes(const ControlDeviceHandle& handle)
{
	if (handle.effect != nullptr)
	{
		return controlEffectStateXml(handle.effect).toUtf8();
	}
	// The product's instrument preset: the same DataFile + Track::savePreset
	// path the instrument window's "save preset" uses, so the file it writes
	// is one the browser can also load.
	DataFile dataFile(DataFile::Type::InstrumentTrackSettings);
	QDomElement& content = dataFile.content();
	handle.track->savePreset(dataFile, content);
	content.setAttribute(QStringLiteral("muted"), 0);
	content.setAttribute(QStringLiteral("solo"), 0);
	content.setAttribute(QStringLiteral("mutedBeforeSolo"), 0);

	QString xml;
	QTextStream stream(&xml);
	dataFile.write(stream);
	return xml.toUtf8();
}

ControlResult controlRestoreDeviceState(const ControlDeviceHandle& handle, const QByteArray& bytes)
{
	if (handle.effect != nullptr)
	{
		return controlRestoreEffectState(handle.effect, bytes);
	}
	DataFile dataFile(bytes);
	if (dataFile.content().isNull())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("the file is not an instrument-track preset document"));
	}
	handle.track->loadPreset(dataFile.content());

	QJsonObject result;
	result.insert(QStringLiteral("restored"), true);
	result.insert(QStringLiteral("plugin"), handle.pluginName);
	return ControlResult::success(result);
}

QString controlUserPresetDir(const ControlDeviceHandle& handle)
{
	return ConfigManager::inst()->userPresetsDir() + handle.folder + QLatin1Char('/');
}

QString controlFactoryPresetDir(const ControlDeviceHandle& handle)
{
	return ConfigManager::inst()->factoryPresetsDir() + handle.folder + QLatin1Char('/');
}

bool controlReadFileBytes(const QString& path, QByteArray* bytes, ControlResult* error)
{
	QFile file(path);
	if (!file.exists())
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no such file: %1").arg(path));
		return false;
	}
	if (!file.open(QIODevice::ReadOnly))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("cannot read %1 (%2)").arg(path, file.errorString()));
		return false;
	}
	*bytes = file.readAll();
	return true;
}

bool controlWriteFileBytes(const QString& path, const QByteArray& bytes, bool overwrite,
	ControlResult* error)
{
	if (QFileInfo::exists(path) && !overwrite)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 already exists; pass \"overwrite\":true to replace it").arg(path));
		return false;
	}
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("cannot write %1 (%2)").arg(path, file.errorString()));
		return false;
	}
	if (file.write(bytes) != bytes.size() || !file.commit())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("could not write %1").arg(path));
		return false;
	}
	return true;
}

QString controlSha256OfBytes(const QByteArray& bytes)
{
	return QString::fromLatin1(
		QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QJsonObject controlFileSnapshot(const QString& path)
{
	QJsonObject snapshot;
	snapshot.insert(QStringLiteral("path"), path);
	const QFileInfo info(path);
	snapshot.insert(QStringLiteral("existed"), info.exists());
	if (!info.exists()) { return snapshot; }

	QByteArray bytes;
	ControlResult ignored;
	if (!controlReadFileBytes(path, &bytes, &ignored)) { return snapshot; }
	snapshot.insert(QStringLiteral("previous_bytes"), bytes.size());
	snapshot.insert(QStringLiteral("previous_sha256"), controlSha256OfBytes(bytes));
	const bool truncated = bytes.size() > ControlSnapshotLimit;
	snapshot.insert(QStringLiteral("previous_truncated"), truncated);
	if (truncated) { bytes.truncate(ControlSnapshotLimit); }
	snapshot.insert(QStringLiteral("previous_content"), QString::fromUtf8(bytes));
	return snapshot;
}

bool controlSafePresetName(const QString& name, ControlResult* error)
{
	const bool unsafe = name.isEmpty() || name.contains(QLatin1Char('/')) ||
		name.contains(QLatin1Char('\\')) || name.contains(QLatin1String("..")) ||
		name.startsWith(QLatin1Char('.'));
	if (unsafe)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is not a preset name: a bare file name without path separators "
				"is required").arg(name));
		return false;
	}
	return true;
}

QString controlPresetFileName(const QString& name)
{
	return name.endsWith(QLatin1String(".xpf")) ? name : name + QLatin1String(".xpf");
}

} // namespace lmms
