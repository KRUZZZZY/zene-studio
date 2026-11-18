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

#include "Appearance.h"
#include "UiConfigStore.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

#include <QFile>
#include <QFontDatabase>
#include <QRegularExpression>
#include <QPainter>
#include <QRectF>

namespace lmms::gui::tokens
{

namespace
{

//! 0.5.0 L14 (WF-130): the high-contrast palette - the dark one with black surfaces, white text
//! (7:1 and more on every surface), brighter lines and a yellow focus ring.
Palette makeHighContrast(Palette p)
{
	p.base = QColor(0x00, 0x00, 0x00);
	p.panel = QColor(0x08, 0x08, 0x0a);
	p.raised = QColor(0x12, 0x12, 0x16);
	p.control = QColor(0x1c, 0x1c, 0x22);
	p.controlHover = QColor(0x2a, 0x2a, 0x33);
	p.selected = QColor(0x2f, 0x34, 0x4a);
	p.separator = QColor(0x8a, 0x8a, 0x95);
	p.text = QColor(0xff, 0xff, 0xff);
	p.textMuted = QColor(0xe0, 0xe0, 0xe6);
	p.textDisabled = QColor(0xa6, 0xa6, 0xb0);
	p.accent = QColor(0x3f, 0xf0, 0xd8);
	p.accentHover = QColor(0x8a, 0xff, 0xee);
	p.focusRing = QColor(0xff, 0xd4, 0x00);
	p.gridBar = QColor(0xa0, 0xa0, 0xaa);
	p.gridBeat = QColor(0x60, 0x60, 0x6a);
	p.gridStep = QColor(0x34, 0x34, 0x3c);
	p.playhead = QColor(0xff, 0xff, 0xff);
	return p;
}

//! 0.5.0 W3 (WF-118): the light palette - dark text on light surfaces, a deep teal accent with white text
//! on it, and every declared pair at AA (DesignTokensTest measures it).
Palette makeLight(Palette p)
{
	p.base = QColor(0xd9, 0xdd, 0xe2);
	p.panel = QColor(0xf3, 0xf4, 0xf6);
	p.raised = QColor(0xe7, 0xe9, 0xec);
	p.control = QColor(0xff, 0xff, 0xff);
	p.controlHover = QColor(0xec, 0xef, 0xf3);
	p.selected = QColor(0xd3, 0xe2, 0xf4);
	p.separator = QColor(0xc3, 0xc8, 0xcf);
	p.text = QColor(0x15, 0x17, 0x1b);
	p.textMuted = QColor(0x4a, 0x52, 0x5d);
	p.textDisabled = QColor(0x8f, 0x96, 0x9f);
	p.textOnAccent = QColor(0xff, 0xff, 0xff);
	p.accent = QColor(0x0b, 0x70, 0x64);
	p.accentHover = QColor(0x09, 0x5c, 0x52);
	p.focusRing = QColor(0x1d, 0x5c, 0xcc);
	p.play = QColor(0x16, 0x79, 0x3f);
	p.record = QColor(0xc0, 0x22, 0x36);
	p.warning = QColor(0x8a, 0x5a, 0x00);
	p.meterLow = QColor(0x2f, 0xa8, 0x64);
	p.meterMid = QColor(0xd9, 0xa2, 0x1b);
	p.meterHigh = QColor(0xd9, 0x3a, 0x4f);
	p.gridBar = QColor(0x8f, 0x97, 0xa2);
	p.gridBeat = QColor(0xc3, 0xc8, 0xce);
	p.gridStep = QColor(0xdd, 0xe0, 0xe4);
	p.keyWhiteRow = QColor(0xfb, 0xfb, 0xfc);
	p.keyBlackRow = QColor(0xec, 0xee, 0xf1);
	p.scaleRow = QColor(0xdc, 0xef, 0xeb);
	p.playhead = QColor(0x1a, 0x1c, 0x20);
	p.clipMidi = QColor(0x0b, 0x70, 0x64);
	p.clipSample = QColor(0x2a, 0x5f, 0xb4);
	p.clipAutomation = QColor(0x64, 0x46, 0xbd);
	p.clipPattern = QColor(0x8a, 0x5a, 0x00);
	return p;
}

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
	p.clipMidi = QColor(0x2f, 0xc9, 0xb5);
	p.clipSample = QColor(0x4a, 0x90, 0xe2);
	p.clipAutomation = QColor(0x9a, 0x7a, 0xea);
	p.clipPattern = QColor(0xe6, 0xb3, 0x4a);
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

//! Every colour role by the name the stylesheet template and theme.set_token use.
const std::map<QString, QColor Palette::*>& roles()
{
	static const std::map<QString, QColor Palette::*> table{
		{"base", &Palette::base}, {"panel", &Palette::panel}, {"raised", &Palette::raised},
		{"control", &Palette::control}, {"controlHover", &Palette::controlHover}, {"selected", &Palette::selected},
		{"separator", &Palette::separator}, {"text", &Palette::text}, {"textMuted", &Palette::textMuted},
		{"textDisabled", &Palette::textDisabled}, {"textOnAccent", &Palette::textOnAccent}, {"accent", &Palette::accent},
		{"accentHover", &Palette::accentHover}, {"focusRing", &Palette::focusRing}, {"play", &Palette::play},
		{"record", &Palette::record}, {"warning", &Palette::warning}, {"meterLow", &Palette::meterLow},
		{"meterMid", &Palette::meterMid}, {"meterHigh", &Palette::meterHigh}, {"gridBar", &Palette::gridBar},
		{"gridBeat", &Palette::gridBeat}, {"gridStep", &Palette::gridStep}, {"keyWhiteRow", &Palette::keyWhiteRow},
		{"keyBlackRow", &Palette::keyBlackRow}, {"scaleRow", &Palette::scaleRow}, {"playhead", &Palette::playhead},
		{"clipMidi", &Palette::clipMidi}, {"clipSample", &Palette::clipSample},
		{"clipAutomation", &Palette::clipAutomation}, {"clipPattern", &Palette::clipPattern}};
	return table;
}

//! Every "<n>px" of @a value times @a factor.
QString scaleLengths(const QString& value, double factor)
{
	static const QRegularExpression length(QStringLiteral("(\\d+(?:\\.\\d+)?)px"));  // a whole length, "1.5px" too
	QString scaled;
	int at = 0;
	for (auto px = length.globalMatch(value); px.hasNext();)
	{
		const QRegularExpressionMatch n = px.next();
		scaled += value.mid(at, n.capturedStart() - at) + QStringLiteral("%1px").arg(std::lround(n.captured(1).toDouble() * factor));
		at = n.capturedEnd();
	}
	return scaled + value.mid(at);
}

} // namespace

Palette paletteNamed(const QString& name)
{
	if (name == QLatin1String("high_contrast")) { return makeHighContrast(makePalette()); }
	return name == QLatin1String("light") ? makeLight(makePalette()) : makePalette();
}

const Palette& palette()
{
	// Read once, at the first draw: the chosen palette with the user's overrides on it. The options are the ones this
	// run started with (Appearance.h), which is what every reply's restart_pending is measured against.
	static const Palette p = withOverrides(paletteNamed(appearance::effectivePalette(appearance::startedWith())), themeOverrides());
	return p;
}

QStringList tokenNames()
{
	QStringList names;
	for (const auto& role : roles()) { names.append(role.first); }
	return names;
}

QColor roleColor(const Palette& p, const QString& name, bool* known)
{
	const auto it = roles().find(name);
	if (known != nullptr) { *known = it != roles().end(); }
	return it != roles().end() ? p.*(it->second) : QColor();
}

Palette withOverrides(Palette base, const QJsonObject& colors, QStringList* rejected)
{
	for (auto it = colors.begin(); it != colors.end(); ++it)
	{
		const auto role = roles().find(it.key());
		const QColor colour(it.value().toString());
		if (role == roles().end() || !colour.isValid())
		{
			if (rejected != nullptr) { rejected->append(it.key()); }
			continue;
		}
		base.*(role->second) = colour;
	}
	return base;
}

QJsonObject themeOverrides()
{
	return uistore::read(QStringLiteral("theme")).value(QStringLiteral("colors")).toObject();
}

bool accessibleMode()
{
	static const bool on = appearance::startedWith().accessible;  // read once
	return on;
}

bool reducedMotion() { return accessibleMode(); }

qreal focusRingWidth() { return accessibleMode() ? 3.0 : 2.0; }

double targetFactor() { return accessibleMode() ? appearance::accessibleTargetFactor : 1.0; }

int targetSize()
{
	return std::max(size::target, static_cast<int>(std::lround(size::target * targetFactor())));
}

bool touchWorkspace()
{
	static const bool on = appearance::workspaceIsTouch(appearance::startedWith().workspace);  // read once
	return on;
}

int tapTargetSize()
{
	return touchWorkspace() ? std::max(targetSize(), appearance::touchTargetSide) : targetSize();
}

int targetSpacing(int standard)
{
	return accessibleMode() ? std::max(standard, appearance::accessibleTargetSpacing) : standard;
}

QString accessibleModeRules()
{
	// The base rules colour a focused control's border with @focusRing; three pixels make that ring strong, and
	// every bordered control gets them focused or not, so focus never shifts a control's content.
	return QStringLiteral("\n/* accessible mode (DesignTokens.cpp) */\n"
		"QPushButton, QToolButton, QLineEdit, QSpinBox, QDoubleSpinBox, QTextEdit, QPlainTextEdit, QComboBox, QDateTimeEdit "
		"{ border-width: 3px; }\n"
		"QCheckBox::indicator:focus, QRadioButton::indicator:focus { border: 3px solid @focusRing; }\n"
		"QTreeView:focus, QListView:focus, QTableView:focus { border: 3px solid @focusRing; }\n");
}

QString scaleMetrics(const QString& css, double factor)
{
	if (factor == 1.0) { return css; }
	static const QRegularExpression declaration(QStringLiteral("((?:padding(?:-left|-right|-top|-bottom)?|min-height|min-width)\\s*:)([^;\\x7d]*)"));  // \x7d: the closing brace, spelled out so brace checkers do not count it
	QString out;
	out.reserve(css.size());
	int last = 0;
	for (auto it = declaration.globalMatch(css); it.hasNext();)
	{
		const QRegularExpressionMatch m = it.next();
		out += css.mid(last, m.capturedStart(2) - last) + scaleLengths(m.captured(2), factor);
		last = m.capturedEnd(2);
	}
	out += css.mid(last);
	return out;
}

QString family()
{
	// Registered once, on first use, from the binary's own resources (src/CMakeLists.txt,
	// zenefonts.qrc): the same faces on every platform, in every package and test binary.
	static const QString name = [] {
		QString found;
		for (const char* face : {":/fonts/Inter-Regular.ttf", ":/fonts/Inter-Medium.ttf", ":/fonts/Inter-SemiBold.ttf"})
		{
			const int id = QFontDatabase::addApplicationFont(QString::fromLatin1(face));
			const QStringList families = id >= 0 ? QFontDatabase::applicationFontFamilies(id) : QStringList();
			if (found.isEmpty() && !families.isEmpty()) { found = families.first(); }
		}
		return found.isEmpty() ? QStringLiteral("sans-serif") : found;
	}();
	return name;
}

QFont font(TextRole role)
{
	static constexpr std::array<int, 5> pixels{10, 11, 12, 14, 20};
	if (static_cast<std::size_t>(role) >= pixels.size()) { role = TextRole::Body; }
	QFont f(family());
	f.setPixelSize(pixels[static_cast<std::size_t>(role)]);  // the one global zoom scales it (Appearance.h)
	f.setHintingPreference(QFont::PreferNoHinting);
	f.setStyleStrategy(QFont::PreferAntialias);
	if (role == TextRole::Title) { f.setWeight(QFont::DemiBold); }
	if (role == TextRole::Display) { f.setWeight(QFont::Medium); }
	if (role == TextRole::Caption) { f.setWeight(QFont::Medium); }
	return f;
}

std::vector<ContrastPair> contrastPairs()
{
	return contrastPairs(palette());
}

std::vector<ContrastPair> contrastPairs(const Palette& p)
{
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
		// 0.5.0 theme 20: the surfaces a custom colour could otherwise slip past unchecked.
		{"muted text on base", p.textMuted, p.base, 4.5},
		{"muted text on hovered control", p.textMuted, p.controlHover, 4.5},
		{"warning on panel", p.warning, p.panel, 3.0},
		{"warning on raised", p.warning, p.raised, 3.0},
		{"accent toggle on control", p.accent, p.control, 3.0},
		{"text on accent", p.textOnAccent, p.accent, 4.5},
		{"text on hovered accent", p.textOnAccent, p.accentHover, 4.5},
		{"accent text on panel", p.accent, p.panel, 4.5},
		{"accent text on raised", p.accent, p.raised, 4.5},
		{"focus ring on panel", p.focusRing, p.panel, 3.0},
		{"focus ring on control", p.focusRing, p.control, 3.0},
		{"focus ring on selected", p.focusRing, p.selected, 3.0},
		{"focus ring on raised", p.focusRing, p.raised, 3.0},
		{"focus ring on hovered control", p.focusRing, p.controlHover, 3.0},
		{"record on raised", p.record, p.raised, 3.0},
		{"play on raised", p.play, p.raised, 3.0},
		{"playhead on white-key row", p.playhead, p.keyWhiteRow, 3.0},
		{"bar line on white-key row", p.gridBar, p.keyWhiteRow, 1.5},
		{"clip name on a MIDI clip", p.textOnAccent, p.clipMidi, 4.5},
		{"clip name on a sample clip", p.textOnAccent, p.clipSample, 4.5},
		{"clip name on an automation clip", p.textOnAccent, p.clipAutomation, 4.5},
		{"clip name on a pattern clip", p.textOnAccent, p.clipPattern, 4.5},
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

QColor tokenColor(const QString& name, bool* known)
{
	return roleColor(palette(), name, known);
}

QString resolveStyleSheet(const QString& templ, QStringList* unknown)
{
	// @name -> #rrggbb, @name/alpha -> rgba(r, g, b, alpha). An unknown name is left as written
	// (Qt then ignores the declaration) and reported, so a typo fails DesignTokensTest.
	static const QRegularExpression token(QStringLiteral("@([A-Za-z]+)(?:/(\\d{1,3}))?"));
	QString out;
	out.reserve(templ.size());
	int last = 0;
	for (auto it = token.globalMatch(templ); it.hasNext();)
	{
		const QRegularExpressionMatch m = it.next();
		out += templ.mid(last, m.capturedStart() - last);
		last = m.capturedEnd();
		bool known = false;
		const QColor c = tokenColor(m.captured(1), &known);
		if (!known)
		{
			if (unknown != nullptr) { unknown->append(m.captured(1)); }
			out += m.captured(0);
			continue;
		}
		out += m.captured(2).isEmpty() ? hex(c)
			: QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(m.captured(2));
	}
	out += templ.mid(last);
	return out;
}

QString styleSheetTemplate()
{
	QFile file(QStringLiteral(":/zene/zene.qss"));
	return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

QString applicationStyleSheet()
{
	const QString templ = accessibleMode() ? styleSheetTemplate() + accessibleModeRules() : styleSheetTemplate();
	return scaleMetrics(resolveStyleSheet(templ, nullptr), targetFactor());
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
