/*
 * ControlResultCheck.h - holding a command's reply to the result schema it declares
 *
 * Every registered command publishes a resultSchema in control.commands_list, and
 * agents (and the MCP bridge's generated tools) read it as the contract. Until
 * 2026-09-28 nothing checked a reply against it: argsSchema was validated before
 * every handler, resultSchema only ever published. This is the other half - the
 * same schema subset (ControlSchema.cpp), applied to the reply.
 *
 * The check is OPT-IN, by environment: ZENE_CONTROL_CHECK_RESULTS=1. The test
 * suite's socket drivers run with it on, so every command they drive also proves
 * its reply matches what it advertises; a shipped instance never pays for it and
 * never changes a reply because of it.
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

#ifndef LMMS_CONTROL_RESULT_CHECK_H
#define LMMS_CONTROL_RESULT_CHECK_H

#include <QJsonObject>
#include <QString>

#include "lmms_export.h"

namespace lmms
{

struct ControlResult;

namespace control
{

//! Why @a result does not satisfy @a schema; empty when it does (or @a schema
//! is empty). The same subset as argument validation.
LMMS_EXPORT QString resultSchemaViolation(const QJsonObject& schema, const QJsonObject& result);

//! Whether ZENE_CONTROL_CHECK_RESULTS is set (read once per process).
LMMS_EXPORT bool resultChecksEnabled();

/*! The last step of every dispatch: drops the internal `__transaction` side
 *  channel and, when checks are enabled, turns a successful reply that breaks
 *  its own @a resultSchema into a typed failure naming the violation. */
LMMS_EXPORT void finishResult(const QString& commandId, const QJsonObject& resultSchema,
	ControlResult* result);

} // namespace control
} // namespace lmms

#endif // LMMS_CONTROL_RESULT_CHECK_H
