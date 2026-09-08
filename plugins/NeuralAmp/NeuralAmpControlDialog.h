/*
 * NeuralAmpControlDialog.h - control dialog for the neural amp effect
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

#ifndef LMMS_GUI_NEURAL_AMP_CONTROL_DIALOG_H
#define LMMS_GUI_NEURAL_AMP_CONTROL_DIALOG_H

#include "EffectControlDialog.h"

class QLabel;
class QLineEdit;

namespace lmms
{

class NeuralAmpControls;

namespace gui
{

class NeuralAmpControlDialog : public EffectControlDialog
{
	Q_OBJECT
public:
	NeuralAmpControlDialog(NeuralAmpControls* controls);
	~NeuralAmpControlDialog() override = default;

private slots:
	void browseForModel();
	void loadModel();

private:
	void refreshStatus();

	NeuralAmpControls* m_controls;
	QLineEdit* m_pathEdit;
	QLabel* m_statusLabel;
};

}  // namespace gui

}  // namespace lmms

#endif  // LMMS_GUI_NEURAL_AMP_CONTROL_DIALOG_H
