/*
 * Vst3Parameter.h - an AutomatableModel mapped onto one normalized VST3 parameter
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

#ifndef LMMS_VST3_PARAMETER_H
#define LMMS_VST3_PARAMETER_H

#include <atomic>
#include <cstdint>
#include <functional>

#include <QString>

#include "AutomatableModel.h"
#include "Vst3ParamDescriptor.h"

namespace lmms
{

/**
 * An LMMS model for a single VST3 parameter.
 *
 * VST3 parameters are always normalized to [0, 1], so the model range is
 * [0, 1] as well and `displayValue()` delegates to the plug-in's own
 * `IEditController::getParamStringByValue()` via a formatter callback.
 *
 * `audioValue()` is an atomic snapshot of the current value. It is written by
 * `Model::dataChanged` (GUI thread or automation thread) and read by the
 * audio thread, so no locking or allocation is needed on the audio path.
 */
class Vst3ParamModel : public FloatModel
{
	Q_OBJECT
public:
	using DisplayFormatter = std::function<QString(float)>;

	Vst3ParamModel(const Vst3ParamDescriptor& descriptor, Model* parent = nullptr);

	auto paramId() const -> std::uint32_t { return m_descriptor.id; }
	auto descriptor() const -> const Vst3ParamDescriptor& { return m_descriptor; }

	//! Sets the callback used to render a normalized value as a string
	void setDisplayFormatter(DisplayFormatter formatter) { m_formatter = std::move(formatter); }

	QString displayValue(const float value) const override;

private:
	Vst3ParamDescriptor m_descriptor;
	DisplayFormatter m_formatter;
};

} // namespace lmms

#endif // LMMS_VST3_PARAMETER_H
