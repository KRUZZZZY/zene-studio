/*
 * AuEffectControls.h - an Audio Unit effect's parameters and saved state (Q050-491, macOS only)
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
 */

#ifndef LMMS_AU_EFFECT_CONTROLS_H
#define LMMS_AU_EFFECT_CONTROLS_H

#include <QDomDocument>
#include <QDomElement>

#include "AuParameter.h"
#include "EffectControls.h"

class QTimer;

namespace lmms
{

class AuEffect;

namespace gui
{
class EffectControlDialog;
}

/*! Every parameter of the unit as a model, and its kAudioUnitProperty_ClassInfo saved in the project
 *  (<au><state>base64 binary plist</state><parameters .../></au>), restored before the parameters. */
class AuEffectControls : public EffectControls
{
	Q_OBJECT
public:
	explicit AuEffectControls(AuEffect* effect);

	void saveSettings(QDomDocument& doc, QDomElement& element) override;
	void loadSettings(const QDomElement& element) override;
	auto nodeName() const -> QString override { return QStringLiteral("au"); }
	auto controlCount() -> int override { return static_cast<int>(m_params.models().size()); }
	auto createView() -> gui::EffectControlDialog* override;

	auto auEffect() -> AuEffect* { return m_effect; }
	auto paramModels() const -> const std::vector<AuParamModel*>& { return m_params.models(); }

private slots:
	//! GUI thread: the unit's own changes (its editor) into the models, and a re-prepare the audio thread asked for.
	void poll();

private:
	AuEffect* m_effect;
	AuParamModelList m_params;
	QTimer* m_pollTimer = nullptr;
};

} // namespace lmms

#endif // LMMS_AU_EFFECT_CONTROLS_H
