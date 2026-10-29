/*
 * ClapParameter.h - an AutomatableModel mapped onto one CLAP parameter, and the list of them
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

#ifndef LMMS_CLAP_PARAMETER_H
#define LMMS_CLAP_PARAMETER_H

#include <cstdint>
#include <functional>
#include <vector>

#include <QDomDocument>
#include <QDomElement>
#include <QString>

#include "AutomatableModel.h"
#include "ClapParamDescriptor.h"

namespace lmms
{

namespace clap
{
class HostedPlugin;
}

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
	//! W6: the plug-in rescanned this parameter - its name, range and flags follow; the model, and so every
	//! automation, controller, macro and modulator connected to it, stays.
	void setDescriptor(const ClapParamDescriptor& descriptor);

	//! Sets the callback used to render a plain value as a string
	void setDisplayFormatter(DisplayFormatter formatter) { m_formatter = std::move(formatter); }

	QString displayValue(const float value) const override;

private:
	ClapParamDescriptor m_descriptor;
	DisplayFormatter m_formatter;
};

/**
 * The models of one hosted CLAP plug-in's parameters, kept in step with its list (W6) - shared by the
 * effect's controls and the instrument.
 *
 * sync() builds them, and after the plug-in rescanned its parameters (HostedPlugin::paramsRevision
 * moved) brings them in step: a parameter that is still there keeps its model, with its name, range
 * and flags refreshed, so automation, controllers, macros and modulators stay connected to it; a new
 * parameter gets a model; a removed one's model is deleted, under the engine's change guard, since the
 * audio thread may be automating it. A model's value is the parameter's plain value: a change is
 * written to the host's lock-free snapshot, except the host's own (pullValues). GUI thread, except the
 * write of a changed value, which runs on the thread that changed the model.
 */
class ClapParamModelList
{
public:
	//! The models of @a plugin's parameters, children of @a parent. Builds nothing until sync().
	ClapParamModelList(clap::HostedPlugin* plugin, Model* parent);

	//! Brings the models in step with the plug-in's list; true when it built or rebuilt them.
	auto sync() -> bool;
	auto models() const -> const std::vector<ClapParamModel*>& { return m_models; }
	auto modelFor(std::uint32_t id) const -> ClapParamModel*;
	//! The plug-in's values into the models, not echoed back: each further from the model's than
	//! @a tolerance of its range (0: every value that differs).
	void pullValues(float tolerance);
	//! stress-4 (b): the host's value of every writable parameter, as `<parameters>` under @a element - saved
	//! beside the plug-in's own state, which does not always carry what the host set.
	void saveValues(QDomDocument& doc, QDomElement& element) const;
	//! After the plug-in restored its state: each writable parameter saved by saveValues() whose value the
	//! plug-in did not bring back is set to the saved one (sent to the plug-in on its next block). A
	//! read-only or unknown one, or one the plug-in already restored, is left alone. True when it set any.
	auto restoreValues(const QDomElement& element) -> bool;

private:
	auto create(const ClapParamDescriptor& descriptor) -> ClapParamModel*;
	//! Deletes the models not in @a kept.
	void removeAllBut(const std::vector<ClapParamModel*>& kept);

	clap::HostedPlugin* m_plugin;
	Model* m_parent;
	std::vector<ClapParamModel*> m_models;
	std::uint32_t m_revision = 0;
	bool m_built = false;
	//! True while the host writes into the models: their change is not sent back to the plug-in.
	bool m_syncing = false;
};

} // namespace lmms

#endif // LMMS_CLAP_PARAMETER_H
