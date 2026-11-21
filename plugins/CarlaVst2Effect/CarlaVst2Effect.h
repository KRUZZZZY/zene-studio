/*
 * CarlaVst2Effect.h - a VST2 effect hosted through Carla (Q050-481.2).
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

#ifndef LMMS_CARLA_VST2_EFFECT_H
#define LMMS_CARLA_VST2_EFFECT_H

#include <memory>
#include <vector>

#include <QDomElement>

#include "AudioPlugin.h"
#include "Carla.h"
#include "CarlaVst2Rack.h"
#include "CarlaVst2Renderer.h"
#include "EffectControlDialog.h"
#include "EffectControls.h"
#include "PluginNativeEditor.h"

class QTimer;

namespace lmms
{

class CarlaVst2Effect;

/*!
 * The effect's parameters - one automatable CarlaParamFloatModel per parameter
 * slot of the Rack, the slots the plug-in does not use left unnamed (so
 * plugin.param_* and the device's parameter lists see only the plug-in's) - and
 * its state: Carla's project XML (binary, unique id, parameter values, the
 * plug-in's chunk) plus each model's settings (automation links), as
 * CarlaInstrument saves them.
 */
class CarlaVst2EffectControls : public EffectControls
{
	Q_OBJECT
public:
	explicit CarlaVst2EffectControls(CarlaVst2Effect* effect);
	~CarlaVst2EffectControls() override;

	void saveSettings(QDomDocument& doc, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;
	QString nodeName() const override { return QStringLiteral("carlavst2effectcontrols"); }
	int controlCount() override;
	gui::EffectControlDialog* createView() override;

	const std::vector<CarlaParamFloatModel*>& paramModels() const { return m_models; }

	//! GUI thread: re-reads the plug-in's parameters from Carla (@a init: and the saved model settings).
	void refreshParams(bool init = false);
	//! Render worker, before each period: hands every changed parameter value to Carla.
	void pushParameters();
	//! GUI thread: the value the plug-in's editor set, into its model (no echo back to the editor).
	void parameterChangedByPlugin(std::uint32_t index, float value);
	//! GUI thread: emits the change signals automation deferred on the render thread.
	void flushDeferredChanges();

private:
	void modelChanged(std::uint32_t index);

	CarlaVst2Effect* m_effect;
	std::vector<CarlaParamFloatModel*> m_models;
	//! The value last handed to Carla per parameter; render worker only.
	std::vector<float> m_sent;
	QDomElement m_settings;
	bool m_fromPlugin = false;
};

/*!
 * One VST2 effect inside one Carla Rack instance (carlavst2::Rack). Carla is
 * never called on the audio thread: carlavst2::Renderer's worker runs it two
 * periods behind the audio thread, which only exchanges frames with it through
 * two lock-free rings (latencyFrames() reports the delay, so the chain
 * compensates it). Wet/dry is mixed on the worker against the same delayed
 * input, so the dry signal stays aligned with the wet one.
 */
class CarlaVst2Effect : public DefaultEffect
{
public:
	CarlaVst2Effect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~CarlaVst2Effect() override;

	bool isLoaded() const { return m_loadError.isEmpty(); }
	QString loadError() const { return m_loadError; }
	QString unavailableReason() const override { return m_loadError; }

	EffectControls* controls() override { return m_controls; }
	CarlaVst2EffectControls* vst2Controls() { return m_controls; }
	carlavst2::Rack& rack() { return m_rack; }

	int latencyFrames() const override { return static_cast<int>(m_renderer.latencyFrames()); }
	//! The delayed output keeps sounding that long after the input stops: auto-quit must not cut it.
	int tailFrames() const override { return latencyFrames(); }

	bool openNativeEditor(QString* error) override;
	void closeNativeEditor() override;
	bool nativeEditorOpen() const override;

	//! Periods the audio thread found not ready (rendered as silence), and periods the worker processed.
	std::uint64_t renderUnderruns() const { return m_renderer.underruns(); }
	std::uint64_t renderedPeriods() const { return m_renderer.renderedPeriods(); }

	//! GUI thread: holds the render worker between two periods (nested calls count) while Carla's plug-in or
	//! parameter list changes.
	void pauseWorker();
	void resumeWorker();
	//! GUI thread: Carla could not restore the saved plug-in.
	void setLoadError(const QString& error) { m_loadError = error; }

private:
	ProcessStatus processImpl(InterleavedBufferView<float, 2> inOut) override;
	//! The worker's period: parameters, Carla's process in place, then wet/dry against the same input.
	void processPeriod(std::span<SampleFrame> frames);
	bool isBridged() const;
	void idle();

	QString m_loadError;
	carlavst2::Rack m_rack;
	CarlaVst2EffectControls* m_controls = nullptr;
	gui::PluginNativeEditor m_editor;
	bool m_bridgedEditorShown = false;
	QTimer* m_idleTimer = nullptr;
	int m_pauseDepth = 0;
	//! The input of the worker's current period, for the dry part of the mix; worker only.
	std::vector<SampleFrame> m_dry;
	carlavst2::Renderer m_renderer;
};

namespace gui
{

//! Knobs for the plug-in's parameters and the button that opens its own editor (plugin.editor_open).
class CarlaVst2EffectControlDialog : public EffectControlDialog
{
	Q_OBJECT
public:
	explicit CarlaVst2EffectControlDialog(CarlaVst2EffectControls* controls);
};

} // namespace gui

} // namespace lmms

#endif // LMMS_CARLA_VST2_EFFECT_H
