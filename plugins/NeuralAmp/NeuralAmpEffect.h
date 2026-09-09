/*
 * NeuralAmpEffect.h - real-time neural amplifier (.nam) effect
 *
 * Copyright (c) 2026 AI-KOS Team
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

#ifndef LMMS_NEURAL_AMP_EFFECT_H
#define LMMS_NEURAL_AMP_EFFECT_H

#include "Effect.h"
#include "NeuralAmpControls.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lmms
{

namespace nam
{
class NamModel;
}

/// Real-time effect that runs a .nam neural amplifier model.
///
/// Threading model:
///  * setModelPath() starts a worker thread that parses the model file and
///    publishes it. The audio thread only ever loads an already-built model
///    pointer, so processImpl() does no allocation, locking or I/O.
///  * The worker waits for the audio thread to leave processImpl() before it
///    frees a replaced model (safe reclamation; the audio thread never waits).
class NeuralAmpEffect : public Effect
{
	Q_OBJECT
public:
	NeuralAmpEffect(Model* parent, const Descriptor::SubPluginFeatures::Key* key);
	~NeuralAmpEffect() override;

	ProcessStatus processImpl(SampleFrame* buf, const f_cnt_t frames) override;

	EffectControls* controls() override
	{
		return &m_controls;
	}

	/// Load a .nam model on a worker thread. Safe to call from the GUI thread.
	void setModelPath(const QString& path);
	QString modelPath() const;

	/// Human-readable load status, shown in the control dialog.
	QString modelStatus() const;

private:
	void loadWorker(QString path);
	void publishModel(std::unique_ptr<nam::NamModel> model);
	void setStatus(const std::string& status);

	NeuralAmpControls m_controls;

	/// Published inference engine; owned by this effect.
	std::atomic<nam::NamModel*> m_model;
	/// True while the audio thread is inside processImpl().
	std::atomic<bool> m_audioActive;
	/// Worker thread for the most recent load request.
	std::thread m_loadThread;

	mutable std::mutex m_statusMutex;
	std::string m_status;

	/// Scratch buffers, sized once in the constructor (never resized in processImpl()).
	std::vector<float> m_scratchIn;
	std::vector<float> m_scratchOut;

	friend class NeuralAmpControls;
	friend class gui::NeuralAmpControlDialog;
};

}  // namespace lmms

#endif  // LMMS_NEURAL_AMP_EFFECT_H
