/*
 * WasmEffectControlDialog.cpp - control dialog for the WasmEffect plugin
 *
 * Copyright (c) 2026 LMMS WASM DSP sandbox contributors
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
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "WasmEffectControlDialog.h"

#include "Knob.h"
#include "WasmEffectControls.h"

#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace lmms::gui
{

WasmEffectControlDialog::WasmEffectControlDialog(WasmEffectControls* controls) :
	EffectControlDialog(controls),
	m_wasmControls(controls)
{
	auto layout = new QVBoxLayout(this);
	layout->setSizeConstraint(QLayout::SetFixedSize);

	auto loadButton = new QPushButton(tr("Load module..."), this);
	connect(loadButton, &QPushButton::clicked,
		this, &WasmEffectControlDialog::chooseModule);

	m_pathLabel = new QLabel(this);
	m_pathLabel->setMinimumWidth(260);

	auto topRow = new QHBoxLayout;
	topRow->addWidget(loadButton);
	topRow->addWidget(m_pathLabel, 1);
	layout->addLayout(topRow);

	m_statusLabel = new QLabel(this);
	m_statusLabel->setWordWrap(true);
	m_statusLabel->setMinimumWidth(320);
	layout->addWidget(m_statusLabel);

	auto grid = new QGridLayout;
	for (int i = 0; i < WasmEffectControls::paramCount; ++i)
	{
		auto knob = new Knob(KnobType::Bright26, tr("P%1").arg(i + 1), this);
		knob->setModel(controls->paramModel(i));
		grid->addWidget(knob, i / 4, i % 4);
	}
	layout->addLayout(grid);

	// The module is loaded/instantiated by the worker thread and can be
	// quarantined at any time by a trap, so poll the status instead of trying
	// to track it through signals from the audio thread.
	m_statusTimer = new QTimer(this);
	m_statusTimer->setInterval(500);
	connect(m_statusTimer, &QTimer::timeout,
		this, &WasmEffectControlDialog::refreshStatus);
	m_statusTimer->start();

	refreshStatus();
}

void WasmEffectControlDialog::chooseModule()
{
	const QString path = QFileDialog::getOpenFileName(this, tr("Load WASM module"),
		QString(), tr("WebAssembly modules (*.wasm);;All files (*)"));
	if (path.isEmpty())
	{
		return;
	}
	m_wasmControls->loadModule(path);
	refreshStatus();
}

void WasmEffectControlDialog::refreshStatus()
{
	const QString path = m_wasmControls->modulePath();
	m_pathLabel->setText(path.isEmpty()
		? tr("(no module loaded)")
		: m_pathLabel->fontMetrics().elidedText(path, Qt::ElideMiddle, 320));
	m_statusLabel->setText(m_wasmControls->statusText());
}

} // namespace lmms::gui
