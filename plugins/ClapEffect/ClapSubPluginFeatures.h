/*
 * ClapSubPluginFeatures.h - CLAP module discovery for the plugin browser
 *
 * Copyright (c) 2026 LMMS contributors
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

#ifndef LMMS_CLAP_SUBPLUGIN_FEATURES_H
#define LMMS_CLAP_SUBPLUGIN_FEATURES_H

#include <QStringList>

#include "Plugin.h"

namespace lmms
{

//! Discovers *.clap modules below the configured VST directory and lists every
//! audio class inside them as a sub-plugin key. The key attributes carry the
//! module path ("file"), the stable CLAP plug-in id ("id") and the display name
//! ("name"), which is all the host needs to instantiate the plug-in again on
//! project load.
class ClapSubPluginFeatures : public Plugin::Descriptor::SubPluginFeatures
{
public:
	explicit ClapSubPluginFeatures(Plugin::Type type);

	void listSubPluginKeys(const Plugin::Descriptor* descriptor,
		KeyList& result) const override;
	void fillDescriptionWidget(QWidget* parent, const Key* key) const override;

protected:
	auto displayName(const Key& key) const -> QString override;
	auto description(const Key& key) const -> QString override;
	auto additionalFileExtensions(const Key& key) const -> QString override;

private:
	static void collectModules(const QString& dir, QStringList& result, int depth);
};

} // namespace lmms

#endif // LMMS_CLAP_SUBPLUGIN_FEATURES_H
