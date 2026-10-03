/*
 * InsertRecorder.cpp - an insert that records what passes it
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

#include "InsertRecorder.h"

#include <QDomElement>

#include "embed.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT insertrecorder_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Recorder",
	QT_TRANSLATE_NOOP("PluginBrowser", "Records the signal passing it to a WAV take"),
	"Zene Studio contributors",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("zene-plugin-logo"),
	nullptr,
	nullptr,
};

}

InsertRecorderControls::InsertRecorderControls(InsertRecorderEffect* effect) :
	ParameterControls(effect, QStringLiteral("InsertRecorderControls")),
	m_effect(effect)
{
}

void InsertRecorderControls::saveSettings(QDomDocument& doc, QDomElement& parent)
{
	ParameterControls::saveSettings(doc, parent);
	const QString& take = m_effect->recorder().lastTake();
	if (!take.isEmpty()) { parent.setAttribute(QStringLiteral("last-take"), take); }
}

void InsertRecorderControls::loadSettings(const QDomElement& parent)
{
	ParameterControls::loadSettings(parent);
	m_effect->recorder().setLastTake(parent.attribute(QStringLiteral("last-take")));
}

InsertRecorderEffect::InsertRecorderEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	AudioPlugin(&insertrecorder_plugin_descriptor, parent, key),
	m_controls(this)
{
	m_recorder.attach(this);
}

InsertRecorderEffect::~InsertRecorderEffect()
{
	m_recorder.stop();
	m_recorder.detach();
}

ProcessStatus InsertRecorderEffect::processImpl(InterleavedBufferView<float, 2> inOut)
{
	m_recorder.push(inOut.asSampleFrames().data(), inOut.frames());
	return m_recorder.recording() ? ProcessStatus::Continue : ProcessStatus::ContinueIfNotQuiet;
}

extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new InsertRecorderEffect(parent, static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}

} // namespace lmms
