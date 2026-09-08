/*
 * ClapParameter.h - an AutomatableModel mapped onto one CLAP parameter
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

#ifndef LMMS_CLAP_PARAMETER_H
#define LMMS_CLAP_PARAMETER_H

#include <cstdint>
#include <functional>

#include <QString>

#include "AutomatableModel.h"
#include "ClapParamDescriptor.h"

namespace lmms
{

/**
 * An LMMS model for a single CLAP parameter.
 *
 * CLAP parameters are plain doubles over a plug-in defined [min, max] range,
 * so the model uses that range directly and `displayValue()` delegates to the
 * plug-in's own `clap_plugin_params::value_to_text()` via a formatter callback.
 *
 * The model value is pushed into the host's lock-free parameter snapshot by
 * `ClapEffectControls`; the audio thread reads only that snapshot.
 */
class ClapParamModel : public FloatModel
{
	Q_OBJECT
public:
	using DisplayFormatter = std::function<QString(double)>;

	ClapParamModel(const ClapParamDescriptor& descriptor, Model* parent = nullptr);

	auto paramId() const -> std::uint32_t { return m_descriptor.id; }
	auto descriptor() const -> const ClapParamDescriptor& { return m_descriptor; }

	//! Sets the callback used to render a plain value as a string
	void setDisplayFormatter(DisplayFormatter formatter) { m_formatter = std::move(formatter); }

	QString displayValue(const float value) const override;

private:
	ClapParamDescriptor m_descriptor;
	DisplayFormatter m_formatter;
};

} // namespace lmms

#endif // LMMS_CLAP_PARAMETER_H
