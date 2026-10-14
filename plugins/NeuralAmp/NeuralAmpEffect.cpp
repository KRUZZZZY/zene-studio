/*
 * NeuralAmpEffect.cpp - real-time neural amplifier (.nam) effect
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

#include "NeuralAmpEffect.h"

#include "nam/NamModel.h"

#include "AudioEngine.h"
#include "Engine.h"
#include "embed.h"
#include "plugin_export.h"

#include <algorithm>
#include <sstream>
#include <utility>


namespace lmms
{

extern "C"
{

Plugin::Descriptor PLUGIN_EXPORT neuralamp_plugin_descriptor =
{
	LMMS_STRINGIFY(PLUGIN_NAME),
	"Neural Amp Modeler",
	QT_TRANSLATE_NOOP("PluginBrowser",
		"Runs .nam WaveNet neural amplifier models in real time"),
	"Zene Studio contributors",
	0x0100,
	Plugin::Type::Effect,
	new PixmapLoader("zene-plugin-logo"),
	nullptr,
	nullptr,
} ;

}


NeuralAmpEffect::NeuralAmpEffect(Model* parent,
	const Descriptor::SubPluginFeatures::Key* key) :
	Effect(&neuralamp_plugin_descriptor, parent, key),
	m_controls(this),
	m_model(nullptr),
	m_audioActive(false),
	m_sampleRateCompatible(false),
	m_scratchIn(nam::NamModel::kMaxBlock, 0.0f),
	m_scratchOut(nam::NamModel::kMaxBlock, 0.0f)
{
	if (auto* engine = Engine::audioEngine())
	{
		m_engineSampleRate = engine->outputSampleRate();
		connect(engine, &AudioEngine::sampleRateChanged, this,
			[this] { refreshSampleRateCompatibility(); });
	}

	// Optional default model (used by the headless harness and for convenience):
	//   LMMS_NAM_MODEL=/path/to/model.nam
	const QByteArray envPath = qgetenv("LMMS_NAM_MODEL");
	if (!envPath.isEmpty())
	{
		setModelPath(QString::fromLocal8Bit(envPath));
	}
	else
	{
		setStatus("no model loaded");
	}
}


NeuralAmpEffect::~NeuralAmpEffect()
{
	// Drop any request still waiting and let the worker leave; a parse already under way cannot be
	// interrupted, so this waits for that one file at most. Nothing is published after this point.
	{
		std::lock_guard<std::mutex> lock(m_loadMutex);
		m_shuttingDown = true;
		m_hasPending = false;
	}
	if (m_loadThread.joinable())
	{
		m_loadThread.join();
	}
	// No audio thread can be running at this point.
	delete m_model.load(std::memory_order_acquire);
	m_model.store(nullptr, std::memory_order_release);
}


QString NeuralAmpEffect::modelPath() const
{
	return m_controls.modelPath();
}


void NeuralAmpEffect::setModelPath(const QString& path)
{
	// Record the request and return: the caller (the GUI thread) never waits for a load. A request made
	// while the worker is busy replaces the one still waiting, so the last request is the one that wins.
	std::lock_guard<std::mutex> lock(m_loadMutex);
	if (m_shuttingDown) { return; }
	m_pendingPath = path;
	m_hasPending = true;
	if (m_workerRunning) { return; }
	// The previous worker has cleared m_workerRunning as its last locked act, so this join is only
	// the thread's final return.
	if (m_loadThread.joinable())
	{
		m_loadThread.join();
	}
	m_workerRunning = true;
	m_loadThread = std::thread(&NeuralAmpEffect::workerLoop, this);
}


void NeuralAmpEffect::workerLoop()
{
	for (;;)
	{
		QString path;
		{
			std::lock_guard<std::mutex> lock(m_loadMutex);
			if (!m_hasPending || m_shuttingDown)
			{
				m_hasPending = false;
				m_workerRunning = false;
				return;
			}
			path = std::move(m_pendingPath);
			m_hasPending = false;
		}
		loadOne(path);
	}
}


//! The status line for a model that parsed: its name, architecture and size.
static std::string describeLoadedModel(const nam::ModelSpec& spec, const std::string& nativePath)
{
	std::ostringstream status;
	status << "loaded " << (spec.name.empty() ? nativePath : spec.name)
	       << " [" << spec.architecture;
	if (!spec.version.empty())
	{
		status << " " << spec.version;
	}
	status << "] arrays=" << spec.arrays.size()
	       << " weights=" << spec.weightCount
	       << " receptive=" << spec.receptiveField << " samples";
	if (spec.sampleRate > 0.0)
	{
		status << " trained@" << static_cast<long>(spec.sampleRate) << " Hz";
	}
	return status.str();
}


void NeuralAmpEffect::loadOne(const QString& path)
{
	setStatus("loading " + path.toStdString());
#ifdef ZENE_NAM_TEST_SEAM
	if (s_testBeforeLoad != nullptr) { s_testBeforeLoad(path); }
#endif
	const std::string nativePath = path.toStdString();
	std::string error;
	std::unique_ptr<nam::NamModel> model = nam::NamModel::loadFromFile(nativePath, &error);

	{
		// A newer request, or the destructor, made this result stale: never publish it.
		std::lock_guard<std::mutex> lock(m_loadMutex);
		if (m_hasPending || m_shuttingDown) { return; }
	}

	if (model == nullptr)
	{
		setStatus("load failed: " + error);
		return;
	}

	const nam::ModelSpec& spec = model->spec();
	const std::string loadedStatus = describeLoadedModel(spec, nativePath);
	{
		std::lock_guard<std::mutex> lock(m_rateMutex);
		if (auto* engine = Engine::audioEngine())
		{
			m_engineSampleRate = engine->outputSampleRate();
		}
		if (!nam::sampleRateMatchesEngine(spec.sampleRate, m_engineSampleRate))
		{
			std::ostringstream refusal;
			refusal << "load refused: model trained at " << static_cast<long>(spec.sampleRate)
				<< " Hz, engine runs at " << static_cast<long>(m_engineSampleRate)
				<< " Hz; rate adaptation is not implemented";
			setStatus(refusal.str());
			return;
		}
		m_modelSampleRate = spec.sampleRate;
		m_loadedStatus = loadedStatus;
		m_modelPublicationInProgress = true;
		m_sampleRateCompatible.store(false, std::memory_order_release);
	}

	publishModel(std::move(model));
	{
		std::lock_guard<std::mutex> lock(m_rateMutex);
		if (auto* engine = Engine::audioEngine())
		{
			m_engineSampleRate = engine->outputSampleRate();
		}
		m_modelPublicationInProgress = false;
		m_sampleRateCompatible.store(
			nam::sampleRateMatchesEngine(m_modelSampleRate, m_engineSampleRate),
			std::memory_order_release);
	}
	refreshSampleRateCompatibility();
}


void NeuralAmpEffect::refreshSampleRateCompatibility()
{
	std::string status;
	bool hasModel = false;
	{
		std::lock_guard<std::mutex> lock(m_rateMutex);
		if (auto* engine = Engine::audioEngine())
		{
			m_engineSampleRate = engine->outputSampleRate();
		}
		const bool compatible = !m_modelPublicationInProgress
			&& nam::sampleRateMatchesEngine(m_modelSampleRate, m_engineSampleRate);
		m_sampleRateCompatible.store(compatible, std::memory_order_release);
		hasModel = m_model.load(std::memory_order_acquire) != nullptr;
		if (hasModel)
		{
			if (compatible) { status = m_loadedStatus; }
			else
			{
				std::ostringstream message;
				message << "bypassed: model trained at " << static_cast<long>(m_modelSampleRate)
					<< " Hz, engine runs at " << static_cast<long>(m_engineSampleRate)
					<< " Hz; rate adaptation is not implemented";
				status = message.str();
			}
		}
	}
	if (hasModel) { setStatus(status); }
}


void NeuralAmpEffect::publishModel(std::unique_ptr<nam::NamModel> model)
{
	nam::NamModel* incoming = model.release();
	// seq_cst here and on the audio thread's flag store and pointer load (round 6): with acquire/release
	// alone the audio thread's store could be ordered after its load (store-load reordering), so it could
	// read `previous` while this thread still saw the flag clear and freed it.
	nam::NamModel* previous = m_model.exchange(incoming, std::memory_order_seq_cst);

	if (previous != nullptr)
	{
		// The audio thread may still be inside processImpl() using `previous`.
		// Wait for it to leave before freeing; the audio thread never waits.
		while (m_audioActive.load(std::memory_order_seq_cst))
		{
			std::this_thread::yield();
		}
		delete previous;
	}
}


void NeuralAmpEffect::setStatus(const std::string& status)
{
	std::lock_guard<std::mutex> lock(m_statusMutex);
	m_status = status;
}


QString NeuralAmpEffect::modelStatus() const
{
	std::lock_guard<std::mutex> lock(m_statusMutex);
	return QString::fromStdString(m_status);
}


Effect::ProcessStatus NeuralAmpEffect::processImpl(
	SampleFrame* buf, const f_cnt_t frames)
{
	// Announce that the audio thread is using the published model, then take a
	// snapshot of the pointer. No allocation, no locks, no I/O below.
	m_audioActive.store(true, std::memory_order_seq_cst);
	nam::NamModel* model = m_model.load(std::memory_order_seq_cst);

	if (model == nullptr || !model->ready()
		|| !m_sampleRateCompatible.load(std::memory_order_acquire))
	{
		m_audioActive.store(false, std::memory_order_release);
		return ProcessStatus::ContinueIfNotQuiet;
	}

	const float dry = dryLevel();
	const float wet = wetLevel();
	const int maxBlock = nam::NamModel::kMaxBlock;

	f_cnt_t offset = 0;
	while (offset < frames)
	{
		const int chunk = static_cast<int>(
			std::min<f_cnt_t>(frames - offset, static_cast<f_cnt_t>(maxBlock)));

		for (int i = 0; i < chunk; ++i)
		{
			m_scratchIn[i] = (buf[offset + i][0] + buf[offset + i][1]) * 0.5f;
		}

		model->process(m_scratchIn.data(), m_scratchOut.data(), chunk);

		for (int i = 0; i < chunk; ++i)
		{
			const float wetSample = m_scratchOut[i];
			buf[offset + i][0] = buf[offset + i][0] * dry + wetSample * wet;
			buf[offset + i][1] = buf[offset + i][1] * dry + wetSample * wet;
		}

		offset += chunk;
	}

	m_audioActive.store(false, std::memory_order_release);
	return ProcessStatus::ContinueIfNotQuiet;
}


extern "C"
{

// necessary for getting instance out of shared lib
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* parent, void* data)
{
	return new NeuralAmpEffect(parent,
		static_cast<const Plugin::Descriptor::SubPluginFeatures::Key*>(data));
}

}

}  // namespace lmms
