/*
 * ParameterReadOnly.h - the mark a hosted plug-in's read-only parameter model carries
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

#ifndef LMMS_PARAMETER_READ_ONLY_H
#define LMMS_PARAMETER_READ_ONLY_H

#include <QObject>
#include <QVariant>

namespace lmms
{

/*! A parameter the plug-in itself owns - a meter, an output level, a gain reduction, DPF's "Buffer Size" and
 *  "Sample Rate" - declared with VST3 ParameterInfo::kIsReadOnly or CLAP_PARAM_IS_READONLY (stress-4). The host
 *  still gives it a model, so it can be shown and read, but a write is not the host's to make: the CLAP host drops
 *  it (ClapHostParams.cpp, setParamPlain) and the plug-in overwrites it. The model carries the flag as a dynamic
 *  property, so the control surface - which sees only AutomatableModel - can report it (`read_only`) and refuse a
 *  plugin.param_set on it. GUI thread: the property is set when the model is made or its descriptor changes. */
inline constexpr const char* ReadOnlyParameterProperty = "zeneReadOnlyParameter";

inline void markParameterReadOnly(QObject* model, bool readOnly)
{
	model->setProperty(ReadOnlyParameterProperty, readOnly);
}

inline bool isParameterReadOnly(const QObject* model)
{
	return model != nullptr && model->property(ReadOnlyParameterProperty).toBool();
}

} // namespace lmms

#endif // LMMS_PARAMETER_READ_ONLY_H
