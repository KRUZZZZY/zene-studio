/*
 * CarlaVst2Effect.cpp - VST2 effects hosted through Carla (Q050-481.2).
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

/*
 * Stage 2 of replacing the product's own VST2 host (VstBase/Vestige/VstEffect,
 * research/reviews/hotspots-2026-10-19) with Carla: VST2 EFFECTS, native or
 * Windows-through-Wine, each one a Carla Rack holding exactly that plug-in.
 * Stage 1 (plugins/CarlaVst2/CarlaVst2.cpp, module carlavst2) hosts the
 * instruments; both list what the shared discovery in carlabase found.
 */

#include "CarlaVst2Effect.h"

#include <algorithm>
#include <limits>

#include <QCoreApplication>
#include <QGridLayout>
#include <QLabel>
#include <QThread>
#include <QTimer>

#include "AudioEngine.h"
#include "CarlaVst2SubPluginFeatures.h"
#include "Engine.h"
#include "Knob.h"
#include "Song.h"
#include "embed.h"
#include "plugin_export.h"

namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT carlavst2effect_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"VST2 effect (Carla)",
	QT_TRANSLATE_NOOP("PluginBrowser", "VST2 effects, hosted through Carla"),
	"Zene Studio contributors",
	0x0100,
	Plugin::Type::Effect,
	new PluginPixmapLoader("logo"),
	nullptr,
	new carlavst2::SubPluginFeatures(Plugin::Type::Effect),
};

PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	// No Carla runtime (the weak stand-in answers nullptr), no key naming a plug-in, or one Carla cannot
	// load: Effect::instantiate turns nullptr into the placeholder that keeps a project's XML (Q050-018).
	const auto* key = static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data);
	if (carla_get_native_rack_plugin() == nullptr || key == nullptr
		|| key->attributes.value(carlavst2::fileAttribute()).isEmpty())
	{
		return nullptr;
	}
	auto* effect = new CarlaVst2Effect(parent, key);
	if (!effect->isLoaded())
	{
		qWarning("carlavst2effect: %s", qPrintable(effect->loadError()));
		delete effect;
		return nullptr;
	}
	return effect;
}

} // extern "C"

namespace
{

//! Runs @a work on @a context's thread: now when already there, else queued (Carla may call back from elsewhere).
template<typename Work>
void onThreadOf(QObject* context, Work work)
{
	if (QThread::currentThread() == context->thread()) { work(); }
	else { QMetaObject::invokeMethod(context, work, Qt::QueuedConnection); }
}

} // namespace

CarlaVst2Effect::CarlaVst2Effect(Model* parent, const Descriptor::SubPluginFeatures::Key* key) :
	DefaultEffect(&carlavst2effect_plugin_descriptor, parent, key),
	m_rack(carlavst2::Rack::Callbacks{
		[this](std::uint32_t index, float value) {
			onThreadOf(this, [this, index, value] { if (m_controls) { m_controls->parameterChangedByPlugin(index, value); } });
		},
		[this] { onThreadOf(this, [this] { if (m_controls) { m_controls->refreshParams(); } }); }}),
	m_dry(carlavst2::Renderer::MaxPeriodFrames),
	m_renderer([this](std::span<SampleFrame> frames) { processPeriod(frames); }, carlavst2::Renderer::Mode::Process)
{
	const auto& attributes = this->key().attributes;
	QString error;
	if (!m_rack.load(attributes.value(carlavst2::fileAttribute()),
			carlavst2::binaryFromName(attributes.value(carlavst2::binaryAttribute())),
			attributes.value(carlavst2::uniqueIdAttribute()).toLongLong(), &error))
	{
		m_loadError = error;
	}
	m_controls = new CarlaVst2EffectControls(this);
	m_controls->refreshParams();
	// Carla runs on the render worker, two periods behind the audio thread; the chain compensates for that.
	m_renderer.start(2 * carlavst2::Renderer::periodFrames());
	connect(Engine::audioEngine(), &AudioEngine::sampleRateChanged, this, [this] {
		pauseWorker();
		m_rack.sampleRateChanged();
		resumeWorker();
	});
	// Carla's non-realtime idle and the deferred parameter signals, on this (the GUI) thread.
	m_idleTimer = new QTimer(this);
	m_idleTimer->setInterval(30);
	connect(m_idleTimer, &QTimer::timeout, this, [this] { idle(); });
	m_idleTimer->start();
}

CarlaVst2Effect::~CarlaVst2Effect()
{
	// The chain no longer processes this effect; the worker goes before the rack and the models it reads.
	m_renderer.stop();
	m_editor.close();
	delete m_controls;
	m_controls = nullptr;
}

void CarlaVst2Effect::pauseWorker()
{
	if (m_pauseDepth++ == 0) { m_renderer.pause(); }
}

void CarlaVst2Effect::resumeWorker()
{
	if (m_pauseDepth > 0 && --m_pauseDepth == 0) { m_renderer.resume(); }
}

ProcessStatus CarlaVst2Effect::processImpl(InterleavedBufferView<float, 2> inOut)
{
	// Audio thread: two rings and a copy, never Carla, never a wait - not even in an export, where the exporter
	// waits for the worker between periods instead (OfflineRenderBarrier.h).
	m_renderer.process(inOut.asSampleFrames());
	return ProcessStatus::ContinueIfNotQuiet;
}

void CarlaVst2Effect::processPeriod(std::span<SampleFrame> frames)
{
	m_controls->pushParameters();
	std::copy(frames.begin(), frames.end(), m_dry.begin());
	m_rack.process(frames);
	const float wet = wetLevel();
	const float dry = dryLevel();
	for (std::size_t f = 0; f < frames.size(); ++f) { frames[f] = frames[f] * wet + m_dry[f] * dry; }
}

void CarlaVst2Effect::idle()
{
	if (m_controls != nullptr) { m_controls->flushDeferredChanges(); }
	m_rack.idle();
}

bool CarlaVst2Effect::isBridged() const
{
	return carlavst2::binaryFromName(key().attributes.value(carlavst2::binaryAttribute())) != carlavst2::Binary::Native;
}

bool CarlaVst2Effect::openNativeEditor(QString* error)
{
	if (!m_rack.hasPlugin())
	{
		*error = QStringLiteral("no VST2 plug-in is loaded");
		return false;
	}
	if (isBridged())
	{
		// A Windows plug-in runs in Wine, in another process: Carla shows its editor in a window of its own.
		m_rack.showEditor(true);
		m_bridgedEditorShown = true;
		return true;
	}
	return m_editor.open(gui::PluginNativeEditor::Hooks{
		[this](void* parent, QString*) {
			// A VST2 editor does not report failure reliably: the window is the editor's from here on.
			m_rack.embedEditor(parent);
			return true;
		},
		[this] { m_rack.showEditor(false); },
		[this](int* width, int* height) {
			*width = m_rack.editorWidth();
			*height = m_rack.editorHeight();
		}}, displayName(), error, gui::PluginNativeEditor::Owner{this, nullptr});
}

void CarlaVst2Effect::closeNativeEditor()
{
	m_editor.close();
	if (m_bridgedEditorShown) { m_rack.showEditor(false); }
	m_bridgedEditorShown = false;
}

bool CarlaVst2Effect::nativeEditorOpen() const
{
	return m_editor.isOpen() || m_bridgedEditorShown;
}

// ---------------------------------------------------------------------------

CarlaVst2EffectControls::CarlaVst2EffectControls(CarlaVst2Effect* effect) :
	EffectControls(effect),
	m_effect(effect)
{
	const std::uint32_t count = effect->rack().parameterCount();
	m_models.reserve(count);
	m_sent.assign(count, std::numeric_limits<float>::quiet_NaN());
	for (std::uint32_t i = 0; i < count; ++i)
	{
		auto* model = new CarlaParamFloatModel(this);
		// Automation writes these models on the render thread: there they only mark a change, which the
		// effect's GUI timer emits, so no parameter signal - nor Carla's UI call behind it - runs there.
		model->setDeferChangeSignalOnRenderThread(true);
		connect(model, &Model::dataChanged, this, [this, i] { modelChanged(i); }, Qt::DirectConnection);
		m_models.push_back(model);
	}
}

CarlaVst2EffectControls::~CarlaVst2EffectControls()
{
	for (CarlaParamFloatModel* model : m_models) { delete model; }
	m_models.clear();
}

int CarlaVst2EffectControls::controlCount()
{
	// At least one, so the device always offers its dialog (and the plug-in's editor button in it).
	const auto enabled = std::count_if(m_models.begin(), m_models.end(),
		[](CarlaParamFloatModel* model) { return model->enabled() && !model->isOutput(); });
	return std::max(1, static_cast<int>(enabled));
}

gui::EffectControlDialog* CarlaVst2EffectControls::createView()
{
	return new gui::CarlaVst2EffectControlDialog(this);
}

void CarlaVst2EffectControls::refreshParams(bool init)
{
	m_effect->pauseWorker();
	carlavst2::Rack& rack = m_effect->rack();
	const std::uint32_t count = std::min<std::uint32_t>(rack.parameterCount(), static_cast<std::uint32_t>(m_models.size()));
	for (std::uint32_t i = 0; i < count; ++i)
	{
		const NativeParameter* info = rack.parameterInfo(i);
		if (info == nullptr) { continue; }
		CarlaParamFloatModel* model = m_models[i];
		model->setOutput((info->hints & NATIVE_PARAMETER_IS_OUTPUT) != 0);
		model->setEnabled((info->hints & NATIVE_PARAMETER_IS_ENABLED) != 0);
		// Only the plug-in's own parameters carry a name: an unnamed model is not a device parameter.
		model->setDisplayName(model->enabled() && info->name != nullptr ? QString::fromUtf8(info->name) : QString());
		model->setRange(info->ranges.min, info->ranges.max, info->ranges.step);
		m_fromPlugin = true;  // Carla's own value: nothing to tell the plug-in's editor
		model->setValue(rack.parameterValue(i));
		if (init) { model->loadSettings(m_settings, QStringLiteral(CARLA_SETTING_PREFIX) + QString::number(i)); }
		m_fromPlugin = false;
	}
	std::fill(m_sent.begin(), m_sent.end(), std::numeric_limits<float>::quiet_NaN());
	m_effect->resumeWorker();
}

void CarlaVst2EffectControls::pushParameters()
{
	for (std::size_t i = 0; i < m_models.size() && i < m_sent.size(); ++i)
	{
		CarlaParamFloatModel* model = m_models[i];
		if (!model->enabled() || model->isOutput()) { continue; }
		const float value = model->value();
		if (value == m_sent[i]) { continue; }
		m_sent[i] = value;
		m_effect->rack().setParameterValue(static_cast<std::uint32_t>(i), value);
	}
}

void CarlaVst2EffectControls::parameterChangedByPlugin(std::uint32_t index, float value)
{
	if (index >= m_models.size()) { return; }
	m_fromPlugin = true;
	m_models[index]->setValue(value);
	m_fromPlugin = false;
}

void CarlaVst2EffectControls::flushDeferredChanges()
{
	for (CarlaParamFloatModel* model : m_models) { model->flushDeferredChange(); }
}

void CarlaVst2EffectControls::modelChanged(std::uint32_t index)
{
	// GUI thread only (render-thread changes are deferred): the worker hands the value to the plug-in before its
	// next period; here only the plug-in's editor hears of it.
	if (m_fromPlugin || index >= m_models.size() || m_models[index]->isOutput()) { return; }
	m_effect->rack().uiSetParameterValue(index, m_models[index]->value());
}

void CarlaVst2EffectControls::saveSettings(QDomDocument& doc, QDomElement& element)
{
	m_effect->pauseWorker();  // the plug-in's chunk is read between two periods
	const QString state = m_effect->rack().state();
	m_effect->resumeWorker();
	QDomDocument carlaDoc(QStringLiteral("carla"));
	if (!state.isEmpty() && carlaDoc.setContent(state))
	{
		element.appendChild(doc.importNode(carlaDoc.documentElement(), true));
	}
	for (std::size_t i = 0; i < m_models.size(); ++i)
	{
		m_models[i]->saveSettings(doc, element, QStringLiteral(CARLA_SETTING_PREFIX) + QString::number(i));
	}
}

void CarlaVst2EffectControls::loadSettings(const QDomElement& element)
{
	QDomElement project = element.firstChildElement(QStringLiteral("CARLA-PROJECT"));
	if (project.isNull()) { return; }  // no Carla state: keep what the key loaded
	QDomDocument carlaDoc(QStringLiteral("carla"));
	carlaDoc.appendChild(carlaDoc.importNode(project, true));
	// The worker holds still while Carla replaces its plug-in and no parameter goes to one going away.
	m_effect->pauseWorker();
	m_effect->rack().setState(carlaDoc.toString(0));
	m_settings = element;
	refreshParams(true);
	m_effect->resumeWorker();
	m_effect->setLoadError(m_effect->rack().hasPlugin() ? QString()
		: QStringLiteral("Carla could not restore the saved VST2 plug-in: %1").arg(m_effect->rack().lastError()));
}

// ---------------------------------------------------------------------------

namespace gui
{

namespace
{
constexpr int KnobsPerRow = 4;
}

CarlaVst2EffectControlDialog::CarlaVst2EffectControlDialog(CarlaVst2EffectControls* controls) :
	EffectControlDialog(controls)
{
	auto* layout = new QGridLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(6);
	int shown = 0;
	for (CarlaParamFloatModel* model : controls->paramModels())
	{
		if (!model->enabled() || model->isOutput()) { continue; }
		auto* knob = new Knob(KnobType::Bright26, model->displayName(), this);
		knob->setModel(model);
		knob->setHintText(model->displayName(), QString());
		layout->addWidget(knob, shown / KnobsPerRow, shown % KnobsPerRow, Qt::AlignCenter);
		++shown;
	}
	if (shown == 0) { layout->addWidget(new QLabel(tr("This plug-in has no parameters."), this), 0, 0); }
	// The plug-in's own editor, through the same plugin.editor_open an agent sends.
	const int rows = shown == 0 ? 1 : (shown + KnobsPerRow - 1) / KnobsPerRow;
	layout->addWidget(makeNativeEditorButton(controls->effect(), this), rows, 0, 1, KnobsPerRow);
	setLayout(layout);
	adjustSize();
	setFixedSize(sizeHint());
}

} // namespace gui

} // namespace lmms
