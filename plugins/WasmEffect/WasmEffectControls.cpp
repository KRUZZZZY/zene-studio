/*
 * WasmEffectControls.cpp - controls model for the WasmEffect plugin
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
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "WasmEffectControls.h"

#include "WasmEffect.h"

#include <QDomDocument>
#include <QDomElement>
#include <QFileInfo>

namespace lmms
{

WasmEffectControls::WasmEffectControls(WasmEffect* effect) :
	EffectControls(effect),
	m_effect(effect)
{
	for (int i = 0; i < paramCount; ++i)
	{
		// Neutral default: a fresh instance leaves every parameter at 0.5,
		// which loadSettings() relies on for elements that predate the plugin.
		m_params[i] = new FloatModel(0.5f, 0.0f, 1.0f, 0.001f, this,
			tr("Param %1").arg(i + 1));
		connect(m_params[i], &FloatModel::dataChanged, this, [this, i]() {
			m_effect->setModuleParam(static_cast<std::uint32_t>(i), m_params[i]->value());
		});
	}
}

FloatModel* WasmEffectControls::paramModel(int index)
{
	if (index < 0 || index >= paramCount)
	{
		return nullptr;
	}
	return m_params[index];
}

float WasmEffectControls::paramValue(int index) const
{
	if (index < 0 || index >= paramCount)
	{
		return 0.0f;
	}
	return m_params[index]->value();
}

bool WasmEffectControls::isModuleLoaded() const
{
	return m_effect->isModuleLoaded();
}

bool WasmEffectControls::loadModule(const QString& path)
{
	QString error;
	if (!m_effect->loadModule(path, &error))
	{
		m_error = error.isEmpty() ? tr("Failed to load %1").arg(path) : error;
		return false;
	}

	m_modulePath = path;
	m_error.clear();

	// Push the current parameter values into the freshly loaded module.
	for (int i = 0; i < paramCount; ++i)
	{
		m_effect->setModuleParam(static_cast<std::uint32_t>(i), m_params[i]->value());
	}
	return true;
}

QString WasmEffectControls::statusText() const
{
	const QString name = m_modulePath.isEmpty()
		? tr("(no module)")
		: QFileInfo(m_modulePath).fileName();

	QString status;
	if (m_effect->isModuleCorrupted())
	{
		status = tr("%1: quarantined after a trap - dry passthrough "
					"(%2 frames compensated)")
			.arg(name)
			.arg(m_effect->latencyFrames());
	}
	else
	{
		switch (m_effect->worker().state())
		{
			case wasm::WasmWorker::State::Idle:
				status = tr("No module loaded - dry passthrough");
				break;
			case wasm::WasmWorker::State::Loading:
				status = tr("%1: loading...").arg(name);
				break;
			case wasm::WasmWorker::State::Ready:
				status = tr("%1: ready - %2 ch, module latency %3 frames, "
							"effect latency %4 frames")
					.arg(name)
					.arg(m_effect->worker().declaredChannels())
					.arg(m_effect->moduleLatency())
					.arg(m_effect->latencyFrames());
				break;
			case wasm::WasmWorker::State::Failed:
				status = tr("%1: failed - %2").arg(name, m_effect->lastError());
				break;
			case wasm::WasmWorker::State::Corrupted:
				status = tr("%1: quarantined after a trap").arg(name);
				break;
			default:
				status = tr("unknown state");
				break;
		}
	}

	// A load that was refused keeps its reason in m_error. Without it here the
	// status line reports the running module ("ready") or "no module loaded",
	// and the user never learns why the module they picked was refused.
	if (!m_error.isEmpty() && !status.contains(m_error))
	{
		status = tr("%1 - load failed: %2").arg(status, m_error);
	}
	return status;
}

void WasmEffectControls::saveSettings(QDomDocument& doc, QDomElement& parent)
{
	if (!m_modulePath.isEmpty())
	{
		parent.setAttribute("module", m_modulePath);
	}
	for (int i = 0; i < paramCount; ++i)
	{
		QDomElement param = doc.createElement("param");
		param.setAttribute("index", i);
		param.setAttribute("value", QString::number(m_params[i]->value(), 'g', 8));
		parent.appendChild(param);
	}
}

void WasmEffectControls::loadSettings(const QDomElement& element)
{
	// Backward compatible by construction: projects saved before the plugin
	// existed have no <wasmeffectcontrols> element at all, and projects with
	// an element but no "module" attribute simply keep their defaults (no
	// module, dry passthrough). Parameters the element does not mention keep
	// their constructor defaults; unknown <param> indices are ignored.
	for (QDomElement param = element.firstChildElement("param");
			!param.isNull(); param = param.nextSiblingElement("param"))
	{
		bool ok = false;
		const int index = param.attribute("index").toInt(&ok);
		if (!ok || index < 0 || index >= paramCount)
		{
			continue;
		}
		m_params[index]->setValue(param.attribute("value").toFloat());
	}

	const QString path = element.attribute("module");
	if (path.isEmpty())
	{
		// 0.5.0 E22: an element without a module unloads the one running, so restoring a
		// device's earlier settings (the undo of wasm.device_load's first load) stops the
		// module instead of leaving it playing behind an empty path.
		m_effect->loadModule(QString());
		m_error.clear();
		return;
	}

	loadModule(path);
}

} // namespace lmms
