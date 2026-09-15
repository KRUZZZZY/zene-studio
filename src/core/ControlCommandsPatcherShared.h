/*
 * ControlCommandsPatcherShared.h - what the two halves of the patcher.* group
 *                                   share: the patch-shaped view of a chain's
 *                                   graph and the validation a wiring goes
 *                                   through before anything is written.
 *
 * The group is two translation units because Gate 7 measures A FILE (500 lines,
 * zero tolerance): src/core/ControlCommandsPatcher.cpp is the read half
 * (patcher.get_state) and src/core/ControlCommandsPatcherEdit.cpp is the edit
 * half (patcher.set_wiring), the seam ControlCommandsAutomation.cpp /
 * ...AutomationEdit.cpp and ControlCommandsPluginScan.cpp / ...ScanEdit.cpp
 * established. This header is the third file of that seam, the shape
 * ControlCommandsPluginScanShared.h uses.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef LMMS_CONTROL_COMMANDS_PATCHER_SHARED_H
#define LMMS_CONTROL_COMMANDS_PATCHER_SHARED_H

#include <QJsonObject>
#include <QString>

namespace lmms
{

class EffectChain;
class PatchWiring;
struct ControlResult;
struct ControlTarget;

namespace control
{

/*! The patcher state BOTH verbs answer with: the target, whether its graph is
 *  on the signal path, the wiring in force (derived or authored), the graph
 *  with the role each node is addressed by, and whether an edit can land at
 *  all. Defined in src/core/ControlCommandsPatcher.cpp, the read half.
 */
QJsonObject patcherStateJson(const EffectChain& chain, const ControlTarget& target);

//! The wiring a chain renders through right now: the authored one when a patch
//! is set, the derived linear one otherwise.
PatchWiring effectiveWiring(const EffectChain& chain);

//! A wiring as the wire shape both verbs report it in.
QJsonObject wiringJson(const PatchWiring& wiring);

/*! Everything a patch can get wrong, checked against the CURRENT node set
 *  BEFORE the chain is touched, so a refusal writes nothing: an unknown
 *  reference, a repeated or self edge, a port outside a node's arity, a cycle,
 *  and an output node the input cannot reach. @returns false with @a reason set.
 */
bool validateWiring(const EffectChain& chain, const PatchWiring& wiring, QString* reason);

/*! The wiring a command's arguments ask for: its edges (`edges`, empty or absent
 *  meaning the derived wiring) and its output node (`output`, the last effect by
 *  default). @returns false with the typed error in @a error.
 */
bool wiringFromArgs(const QJsonObject& args, const EffectChain& chain, PatchWiring* wanted,
	ControlResult* error);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_COMMANDS_PATCHER_SHARED_H
