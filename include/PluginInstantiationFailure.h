/*
 * PluginInstantiationFailure.h - why a plug-in module's entry point produced no plug-in
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

#ifndef LMMS_PLUGIN_INSTANTIATION_FAILURE_H
#define LMMS_PLUGIN_INSTANTIATION_FAILURE_H

#include <QString>

#include "lmms_export.h"

namespace lmms::pluginload
{

/*! Cluster stress-3: a module's lmms_plugin_main() can only answer nullptr, and the reason it had went to
 *  qWarning() - which Qt sends to the journal, not stderr, when stderr is not a terminal - so plugin.load
 *  refused a third-party plug-in with "the engine could not instantiate 'vst3effect'" and nothing else.
 *  A host notes its reason here, on the thread that instantiates; the caller takes it after the call. */
LMMS_EXPORT void noteInstantiationFailure(const QString& reason);

//! The reason noted on this thread since the last take (empty when none was), cleared by the call.
LMMS_EXPORT auto takeInstantiationFailure() -> QString;

} // namespace lmms::pluginload

#endif // LMMS_PLUGIN_INSTANTIATION_FAILURE_H
