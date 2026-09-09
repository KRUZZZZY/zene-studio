/*
 * NeuralAmpControlDialog.cpp - control dialog for the neural amp effect
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

#include "NeuralAmpControlDialog.h"

#include "NeuralAmpControls.h"
#include "NeuralAmpEffect.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>


namespace lmms
{

namespace gui
{

NeuralAmpControlDialog::NeuralAmpControlDialog(NeuralAmpControls* controls) :
	EffectControlDialog(controls),
	m_controls(controls),
	m_pathEdit(new QLineEdit(controls->modelPath(), this)),
	m_statusLabel(new QLabel(this))
{
	auto* layout = new QVBoxLayout(this);
	auto* row = new QHBoxLayout();

	m_pathEdit->setPlaceholderText(tr("Path to a .nam model file"));
	m_pathEdit->setMinimumWidth(260);
	row->addWidget(m_pathEdit, 1);

	auto* browseButton = new QPushButton(tr("Browse..."), this);
	row->addWidget(browseButton);

	auto* loadButton = new QPushButton(tr("Load"), this);
	row->addWidget(loadButton);

	layout->addLayout(row);

	m_statusLabel->setWordWrap(true);
	m_statusLabel->setMinimumWidth(300);
	layout->addWidget(m_statusLabel);

	connect(browseButton, &QPushButton::clicked,
		this, &NeuralAmpControlDialog::browseForModel);
	connect(loadButton, &QPushButton::clicked,
		this, &NeuralAmpControlDialog::loadModel);

	// Model loading is asynchronous, so poll the effect for the latest status.
	auto* timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &NeuralAmpControlDialog::refreshStatus);
	timer->start(500);

	refreshStatus();
}


void NeuralAmpControlDialog::browseForModel()
{
	const QString startDir = QFileInfo(m_pathEdit->text()).absolutePath();
	const QString path = QFileDialog::getOpenFileName(this,
		tr("Open neural amp model"), startDir,
		tr("NAM models (*.nam);;All files (*)"));

	if (!path.isEmpty())
	{
		m_pathEdit->setText(path);
		loadModel();
	}
}


void NeuralAmpControlDialog::loadModel()
{
	const QString path = m_pathEdit->text().trimmed();
	if (path.isEmpty())
	{
		return;
	}
	m_statusLabel->setText(tr("loading..."));
	m_controls->setModelPath(path);
}


void NeuralAmpControlDialog::refreshStatus()
{
	m_statusLabel->setText(m_controls->m_effect->modelStatus());
}

}  // namespace gui

}  // namespace lmms
