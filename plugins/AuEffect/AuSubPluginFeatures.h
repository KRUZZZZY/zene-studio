/*
 * AuSubPluginFeatures.h - the Audio Unit components a host module offers (Q050-491, macOS only)
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

#ifndef LMMS_AU_SUBPLUGIN_FEATURES_H
#define LMMS_AU_SUBPLUGIN_FEATURES_H

#include "Plugin.h"

namespace lmms
{

/*! Lists the effect (aufx, aumf) or instrument (aumu) components of the system component registry as
 *  sub-plugin keys. Every component is recorded in the plug-in scan cache, and one on its quarantine
 *  list is left out (au::recordScan, the rules a plug-in file is scanned by). A key carries the
 *  component ("component" = type:subtype:manufacturer) and its name, which is all a project needs to
 *  instantiate it again. */
class AuSubPluginFeatures : public Plugin::Descriptor::SubPluginFeatures
{
public:
	explicit AuSubPluginFeatures(Plugin::Type type);

	void listSubPluginKeys(const Plugin::Descriptor* descriptor, KeyList& result) const override;
	void fillDescriptionWidget(QWidget* parent, const Key* key) const override;

protected:
	auto displayName(const Key& key) const -> QString override;
	auto description(const Key& key) const -> QString override;
};

} // namespace lmms

#endif // LMMS_AU_SUBPLUGIN_FEATURES_H
