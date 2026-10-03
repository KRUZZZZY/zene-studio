/*
 * PreviewOutput.cpp - the mixer channel the browser's previews play into
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
#include "PreviewOutput.h"

#include "AudioBusHandle.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "Mixer.h"
#include "PlayHandle.h"

namespace lmms::previewoutput
{

int channel()
{
	const int index = ConfigManager::inst()->value(QStringLiteral("mixer"), QStringLiteral("previewchannel")).toInt();
	return index > 0 && index < Engine::mixer()->numChannels() ? index : 0;
}

void setChannel(int index)
{
	ConfigManager::inst()->setValue(QStringLiteral("mixer"), QStringLiteral("previewchannel"), QString::number(index));
}

void route(PlayHandle* handle)
{
	if (handle != nullptr && handle->audioBusHandle() != nullptr) { handle->audioBusHandle()->setNextMixerChannel(channel()); }
}

} // namespace lmms::previewoutput
