/*
 * MixerOptions.h - the project's pan law
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
#ifndef LMMS_MIXER_OPTIONS_H
#define LMMS_MIXER_OPTIONS_H

#include <atomic>

#include <QStringList>

#include "ProjectSection.h"
#include "lmms_export.h"

namespace lmms
{

//! How a centred mono signal is panned: balance (no change at the centre, the product's own law and
//! the default), or a centre cut of 3, 4.5 or 6 dB (constant power, its compromise, linear).
enum class PanLaw
{
	Balance,
	ConstantPower,  //!< -3 dB at the centre
	Compromise,     //!< -4.5 dB at the centre
	Linear          //!< -6 dB at the centre
};

/*! 0.5.0 L08 (MX-025): the project's pan law, which every channel's balance pan uses. Saved as
 *  <mixer-options> only when it is not balance, so a project that never chose one re-saves byte for
 *  byte and renders exactly as before. Written on the UI thread, read by the mixer's workers. */
class LMMS_EXPORT MixerOptions : public ProjectSection
{
public:
	static MixerOptions& instance();
	static const QStringList& panLawNames();

	PanLaw panLaw() const { return static_cast<PanLaw>(m_panLaw.load(std::memory_order_relaxed)); }
	void setPanLaw(PanLaw law) { m_panLaw.store(static_cast<int>(law), std::memory_order_relaxed); }
	//! The left and right gains of @a pan (-1..1) under @a law; balance is the caller's own stage.
	static void gains(PanLaw law, float pan, float* left, float* right);

	QString elementName() const override { return QStringLiteral("mixer-options"); }
	bool shouldPersist() const override { return panLaw() != PanLaw::Balance; }
	void save(QDomDocument& doc, QDomElement& parent) const override;
	void load(const QDomElement& element) override;
	void clear() override { setPanLaw(PanLaw::Balance); }

private:
	MixerOptions() = default;
	std::atomic<int> m_panLaw{0};
};

} // namespace lmms

#endif // LMMS_MIXER_OPTIONS_H
