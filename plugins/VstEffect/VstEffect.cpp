/*
 * VstEffect.cpp - class for handling VST effect plugins
 *
 * Copyright (c) 2006-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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


#include "VstEffect.h"

#include <algorithm>

#include "Engine.h"
#include "GuiApplication.h"
#include "Song.h"
#include "TextFloat.h"
#include "VstPlugin.h"
#include "VstSubPluginFeatures.h"

#include "embed.h"
#include "plugin_export.h"

namespace lmms
{


extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT vsteffect_plugin_descriptor =
{
	LMMS_STRINGIFY( PLUGIN_NAME ),
	"VST",
	QT_TRANSLATE_NOOP( "PluginBrowser",
				"plugin for using arbitrary VST effects inside Zene Studio." ),
	"Tobias Doerffel <tobydox/at/users.sf.net>",
	0x0200,
	Plugin::Type::Effect,
	new PluginPixmapLoader("logo"),
	nullptr,
	new VstSubPluginFeatures( Plugin::Type::Effect )
} ;

}


VstEffect::VstEffect( Model * _parent,
			const Descriptor::SubPluginFeatures::Key * _key ) :
	AudioPlugin( &vsteffect_plugin_descriptor, _parent, _key ),
	m_pluginMutex(),
	m_key( *_key ),
	m_vstControls( this )
{
	bool loaded = false;
	if( !m_key.attributes["file"].isEmpty() )
	{
		loaded = openPlugin(m_key.attributes["file"]);
	}
	setDisplayName( m_key.attributes["file"].section( ".dll", 0, 0 ).isEmpty()
		? m_key.name : m_key.attributes["file"].section( ".dll", 0, 0 ) );

	setDontRun(!loaded);
}




auto VstEffect::processImpl(PlanarBufferView<const float> in, PlanarBufferView<float> out)
	-> ProcessStatus
{
	assert(m_plugin != nullptr);

	// The remote client processes the shared audio block in place: the input
	// planes it reads are the dry signal and the output planes it writes are
	// the wet signal (#589).
	if (!m_plugin->process())
	{
		return ProcessStatus::Sleep;
	}

	if (in.channels() == 0)
	{
		// Do not process wet/dry for an instrument loaded as an effect
		// TODO: Prevent instruments from loading as effects?
		return ProcessStatus::ContinueIfNotQuiet;
	}

	// Wet/dry mixing applies to the first 2 channels; additional channels
	// remain as-is.
	const float w = wetLevel();
	const float d = dryLevel();
	const auto mixableOutputs = std::min<ch_cnt_t>(out.channels(), 2);
	for (ch_cnt_t channel = 0; channel < mixableOutputs; ++channel)
	{
		auto wetBuffer = out.buffer(channel);
		auto dryBuffer = in.buffer(std::min(channel, static_cast<ch_cnt_t>(in.channels() - 1)));
		for (f_cnt_t f = 0; f < out.frames(); ++f)
		{
			wetBuffer[f] = w * wetBuffer[f] + d * dryBuffer[f];
		}
	}

	return ProcessStatus::ContinueIfNotQuiet;
}




auto VstEffect::processLock() -> bool
{
	return m_pluginMutex.tryLock(Engine::getSong()->isExporting() ? -1 : 0);
}




void VstEffect::processUnlock()
{
	m_pluginMutex.unlock();
}




bool VstEffect::openPlugin(const QString& plugin)
{
	gui::TextFloat* tf = nullptr;
	if( gui::getGUI() != nullptr )
	{
		tf = gui::TextFloat::displayMessage(
			VstPlugin::tr( "Loading plugin" ),
			VstPlugin::tr( "Please wait while loading VST plugin..." ),
				PLUGIN_NAME::getIconPixmap( "logo", 24, 24 ), 0 );
	}

	QMutexLocker ml( &m_pluginMutex ); Q_UNUSED( ml );
	m_plugin = QSharedPointer<VstPlugin>(new VstPlugin(plugin, audioPorts().controller()));
	if( m_plugin->failed() )
	{
		m_plugin.clear();
		delete tf;
		collectErrorForUI(VstPlugin::tr("The VST plugin %1 could not be loaded.").arg(plugin));
		return false;
	}

	delete tf;

	m_key.attributes["file"] = plugin;
	return true;
}




extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin * lmms_plugin_main( Model * _parent, void * _data )
{
	return new VstEffect( _parent,
		static_cast<const Plugin::Descriptor::SubPluginFeatures::Key *>(
								_data ) );
}

}


} // namespace lmms
