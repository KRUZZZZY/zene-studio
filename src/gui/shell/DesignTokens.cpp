/*
 * DesignTokens.cpp - the 0.5.0 interface's one source of colour, spacing, type and size
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

#include "DesignTokens.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <QPainter>
#include <QRectF>

namespace lmms::gui::tokens
{

namespace
{

Palette makePalette()
{
	Palette p;
	p.base = QColor(0x10, 0x11, 0x14);
	p.panel = QColor(0x17, 0x19, 0x1d);
	p.raised = QColor(0x1f, 0x22, 0x27);
	p.control = QColor(0x29, 0x2d, 0x33);
	p.controlHover = QColor(0x33, 0x38, 0x3f);
	p.selected = QColor(0x3b, 0x41, 0x49);
	p.separator = QColor(0x2a, 0x2e, 0x34);

	p.text = QColor(0xec, 0xee, 0xf1);
	p.textMuted = QColor(0xa8, 0xaf, 0xb9);
	p.textDisabled = QColor(0x6a, 0x70, 0x7a);
	p.textOnAccent = QColor(0x0a, 0x0b, 0x0d);

	p.accent = QColor(0x2f, 0xc9, 0xb5);
	p.accentHover = QColor(0x5a, 0xd8, 0xc7);
	p.focusRing = QColor(0x7a, 0xb4, 0xff);

	p.play = QColor(0x4f, 0xd9, 0x8a);
	p.record = QColor(0xff, 0x54, 0x68);
	p.warning = QColor(0xff, 0xc2, 0x4d);
	p.meterLow = QColor(0x4f, 0xd9, 0x8a);
	p.meterMid = QColor(0xff, 0xc2, 0x4d);
	p.meterHigh = QColor(0xff, 0x54, 0x68);

	p.gridBar = QColor(0x4a, 0x50, 0x59);
	p.gridBeat = QColor(0x31, 0x35, 0x3c);
	p.gridStep = QColor(0x23, 0x26, 0x2b);
	p.keyWhiteRow = QColor(0x1c, 0x1f, 0x23);
	p.keyBlackRow = QColor(0x16, 0x18, 0x1b);
	p.scaleRow = QColor(0x1d, 0x2b, 0x2a);
	p.playhead = QColor(0xf2, 0xf4, 0xf7);
	return p;
}

QColor mix(const QColor& a, const QColor& b, double t)
{
	const double u = std::clamp(t, 0.0, 1.0);
	return QColor::fromRgbF(
		static_cast<float>(a.redF() + (b.redF() - a.redF()) * u),
		static_cast<float>(a.greenF() + (b.greenF() - a.greenF()) * u),
		static_cast<float>(a.blueF() + (b.blueF() - a.blueF()) * u));
}

QString hex(const QColor& c) { return c.name(QColor::HexRgb); }

} // namespace

const Palette& palette()
{
	static const Palette p = makePalette();
	return p;
}

QFont font(TextRole role)
{
	static constexpr std::array<int, 5> pixels{10, 11, 12, 14, 22};
	QFont f;
	f.setPixelSize(pixels[static_cast<std::size_t>(role)]);
	if (role == TextRole::Title) { f.setWeight(QFont::DemiBold); }
	if (role == TextRole::Display)
	{
		f.setWeight(QFont::Medium);
		f.setStyleHint(QFont::Monospace);
		f.setFamily(QStringLiteral("monospace"));
	}
	return f;
}

std::vector<ContrastPair> contrastPairs()
{
	const Palette& p = palette();
	return {
		{"text on base", p.text, p.base, 4.5},
		{"text on panel", p.text, p.panel, 4.5},
		{"text on raised", p.text, p.raised, 4.5},
		{"text on control", p.text, p.control, 4.5},
		{"text on hovered control", p.text, p.controlHover, 4.5},
		{"text on selected", p.text, p.selected, 4.5},
		{"muted text on panel", p.textMuted, p.panel, 4.5},
		{"muted text on raised", p.textMuted, p.raised, 4.5},
		{"muted text on control", p.textMuted, p.control, 4.5},
		{"muted text on selected", p.textMuted, p.selected, 4.5},
		{"text on accent", p.textOnAccent, p.accent, 4.5},
		{"text on hovered accent", p.textOnAccent, p.accentHover, 4.5},
		{"accent text on panel", p.accent, p.panel, 4.5},
		{"accent text on raised", p.accent, p.raised, 4.5},
		{"focus ring on panel", p.focusRing, p.panel, 3.0},
		{"focus ring on control", p.focusRing, p.control, 3.0},
		{"focus ring on selected", p.focusRing, p.selected, 3.0},
		{"record on raised", p.record, p.raised, 3.0},
		{"play on raised", p.play, p.raised, 3.0},
		{"playhead on white-key row", p.playhead, p.keyWhiteRow, 3.0},
		{"bar line on white-key row", p.gridBar, p.keyWhiteRow, 1.5},
	};
}

QColor velocityColor(int velocity)
{
	// The ramp is read once per note per frame, so it is computed once: a 201-entry table.
	static const std::array<QColor, 201> ramp = [] {
		const QColor quiet(0x2b, 0x4f, 0xa8);
		const QColor middle(0x2f, 0xc9, 0xb5);
		const QColor loud(0xf5, 0xd5, 0x47);
		std::array<QColor, 201> table;
		for (int v = 0; v <= 200; ++v)
		{
			const double t = v / 200.0;
			table[static_cast<std::size_t>(v)] = t < 0.5 ? mix(quiet, middle, t * 2.0) : mix(middle, loud, (t - 0.5) * 2.0);
		}
		return table;
	}();
	return ramp[static_cast<std::size_t>(std::clamp(velocity, 0, 200))];
}

int trackColorCount() { return 16; }

QColor trackColor(int index)
{
	static const std::array<QRgb, 16> colours{
		0xe0605a, 0xe8894a, 0xe6b34a, 0xc9cf4f, 0x8fcf57, 0x52c776, 0x3fc4a8, 0x42b6d6,
		0x4a90e2, 0x6b78e6, 0x8f6ce0, 0xb563d6, 0xd65fb3, 0xdf6189, 0x9aa3ad, 0xc4a77d};
	const int n = trackColorCount();
	return QColor::fromRgb(colours[static_cast<std::size_t>(((index % n) + n) % n)]);
}

QString applicationStyleSheet()
{
	const Palette& p = palette();
	QString css;
	css += QStringLiteral(
		"QMenuBar { background-color: %1; color: %2; }\n"
		"QMenuBar::item:selected { background-color: %3; color: %2; }\n"
		"QMenu { background-color: %4; color: %2; border: 1px solid %5; }\n"
		"QMenu::item:selected { background-color: %6; color: %7; }\n"
		"QMenu::item:disabled { color: %8; }\n"
		"QMenu::separator { height: 1px; background-color: %5; }\n")
		.arg(hex(p.raised), hex(p.text), hex(p.controlHover), hex(p.panel), hex(p.separator),
			hex(p.accent), hex(p.textOnAccent), hex(p.textDisabled));
	css += QStringLiteral(
		"QToolTip { background-color: %1; color: %2; border: 1px solid %3; padding: 4px; }\n"
		"QSplitter::handle { background-color: %4; }\n"
		"lmms--gui--ShellPanel { background-color: %5; }\n"
		"lmms--gui--ShellFooter, lmms--gui--ShellHeader { background-color: %6; color: %2; }\n")
		.arg(hex(p.control), hex(p.text), hex(p.separator), hex(p.base), hex(p.panel), hex(p.raised));
	return css;
}

void strokeCrisp(QPainter& painter, const QRectF& rect, const QColor& colour)
{
	const qreal dpr = painter.device() != nullptr ? painter.device()->devicePixelRatioF() : 1.0;
	const qreal half = 0.5 / dpr;
	QPen pen(colour);
	pen.setWidthF(1.0 / dpr);
	pen.setCosmetic(false);
	painter.save();
	painter.setPen(pen);
	painter.setBrush(Qt::NoBrush);
	painter.drawRect(rect.adjusted(half, half, -half, -half));
	painter.restore();
}

} // namespace lmms::gui::tokens
