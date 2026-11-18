/*
 * ConsoleStrip.cpp - the mixer strip's Console section: trim, polarity, EQ, and the channel's Console drive and glue
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

#include "ConsoleStrip.h"

#include <cmath>

#include "AutomatableModel.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Mixer.h"
#include "ParameterControls.h"

namespace lmms::consolestrip
{

namespace
{

//! One number the section sets: its field name, its place in Settings, its range, and whether it is the Console's.
struct NumberField
{
	const char* name;
	float Settings::*member;
	float low;
	float high;
	bool console;
};

const NumberField kNumbers[] = {
	{"trim_db", &Settings::trim, -24.f, 24.f, false},
	{"eq_low_db", &Settings::eqLow, -18.f, 18.f, false},
	{"eq_mid_db", &Settings::eqMid, -18.f, 18.f, false},
	{"eq_mid_freq", &Settings::eqMidFreq, 100.f, 10000.f, false},
	{"eq_high_db", &Settings::eqHigh, -18.f, 18.f, false},
	{"drive", &Settings::drive, 0.f, 100.f, true},
	{"glue_threshold_db", &Settings::glueThreshold, -40.f, 0.f, true},
	{"glue_ratio", &Settings::glueRatio, 1.f, 20.f, true},
	{"glue_makeup_db", &Settings::glueMakeup, 0.f, 24.f, true},
};

struct FlagField
{
	const char* name;
	bool Settings::*member;
	bool console;
};

const FlagField kFlags[] = {
	{"polarity_left", &Settings::polarityLeft, false},
	{"polarity_right", &Settings::polarityRight, false},
	{"glue", &Settings::glue, true},
};

float valueOf(Effect* console, const char* key, float fallback)
{
	const AutomatableModel* model = parameterByKey(console, QLatin1String(key));
	return model != nullptr ? model->value<float>() : fallback;
}

void write(Effect* console, const char* key, float value)
{
	if (AutomatableModel* model = parameterByKey(console, QLatin1String(key)); model != nullptr && model->value<float>() != value)
	{
		model->setValue(value);
	}
}

} // namespace

Effect* consoleOf(const MixerChannel& channel)
{
	for (Effect* effect : channel.m_fxChain.effects())
	{
		if (QString::fromUtf8(effect->descriptor()->name) == QLatin1String("console")) { return effect; }
	}
	return nullptr;
}

AutomatableModel* parameterByKey(Effect* effect, const QString& key)
{
	auto* controls = effect != nullptr ? dynamic_cast<ParameterControls*>(effect->controls()) : nullptr;
	if (controls == nullptr) { return nullptr; }
	for (const auto& [model, saved] : controls->parameters()) { if (saved == key) { return model; } }
	return nullptr;
}

Settings read(const MixerChannel& c)
{
	Settings s;
	s.trim = c.m_trimModel.value();
	s.polarityLeft = c.m_polarityLeftModel.value();
	s.polarityRight = c.m_polarityRightModel.value();
	s.eqLow = c.m_eqLowModel.value();
	s.eqMid = c.m_eqMidModel.value();
	s.eqMidFreq = c.m_eqMidFreqModel.value();
	s.eqHigh = c.m_eqHighModel.value();
	if (Effect* console = consoleOf(c))
	{
		s.hasConsole = true;
		s.drive = valueOf(console, "drive", s.drive);
		s.glue = valueOf(console, "glue", 0.f) != 0.f;
		s.glueThreshold = valueOf(console, "gluethreshold", s.glueThreshold);
		s.glueRatio = valueOf(console, "glueratio", s.glueRatio);
		s.glueMakeup = valueOf(console, "gluemakeup", s.glueMakeup);
	}
	return s;
}

void apply(MixerChannel& c, const Settings& s)
{
	c.m_trimModel.setValue(s.trim);
	c.m_polarityLeftModel.setValue(s.polarityLeft);
	c.m_polarityRightModel.setValue(s.polarityRight);
	c.m_eqLowModel.setValue(s.eqLow);
	c.m_eqMidModel.setValue(s.eqMid);
	c.m_eqMidFreqModel.setValue(s.eqMidFreq);
	c.m_eqHighModel.setValue(s.eqHigh);
	Effect* console = consoleOf(c);
	if (console == nullptr || !s.hasConsole) { return; }
	write(console, "drive", s.drive);
	write(console, "glue", s.glue ? 1.f : 0.f);
	write(console, "gluethreshold", s.glueThreshold);
	write(console, "glueratio", s.glueRatio);
	write(console, "gluemakeup", s.glueMakeup);
}

QJsonObject toJson(const Settings& s)
{
	QJsonObject json{{QStringLiteral("console"), s.hasConsole}};
	for (const NumberField& field : kNumbers)
	{
		const bool shown = !field.console || s.hasConsole;
		json.insert(QLatin1String(field.name), shown ? QJsonValue(static_cast<double>(s.*field.member)) : QJsonValue());
	}
	for (const FlagField& field : kFlags)
	{
		const bool shown = !field.console || s.hasConsole;
		json.insert(QLatin1String(field.name), shown ? QJsonValue(s.*field.member) : QJsonValue());
	}
	return json;
}

namespace
{

bool consoleMissing(const char* name, QString* error, bool* refused)
{
	*error = QStringLiteral("'%1' is a Console setting and this channel has no Console - mixer.console_apply puts one on "
		"every channel").arg(QLatin1String(name));
	*refused = true;
	return false;
}

bool mergeNumbers(const QJsonObject& args, Settings* next, QString* error, bool* refused)
{
	for (const NumberField& field : kNumbers)
	{
		const QJsonValue value = args.value(QLatin1String(field.name));
		if (value.isUndefined()) { continue; }
		if (field.console && !next->hasConsole) { return consoleMissing(field.name, error, refused); }
		const double number = value.toDouble(std::nan(""));
		if (!value.isDouble() || !std::isfinite(number) || number < field.low || number > field.high)
		{
			*error = QStringLiteral("'%1' is a number from %2 to %3").arg(QLatin1String(field.name)).arg(static_cast<double>(field.low))
				.arg(static_cast<double>(field.high));
			return false;
		}
		next->*field.member = static_cast<float>(number);
	}
	return true;
}

bool mergeFlags(const QJsonObject& args, Settings* next, QString* error, bool* refused)
{
	for (const FlagField& field : kFlags)
	{
		const QJsonValue value = args.value(QLatin1String(field.name));
		if (value.isUndefined()) { continue; }
		if (field.console && !next->hasConsole) { return consoleMissing(field.name, error, refused); }
		if (!value.isBool())
		{
			*error = QStringLiteral("'%1' is true or false").arg(QLatin1String(field.name));
			return false;
		}
		next->*field.member = value.toBool();
	}
	return true;
}

} // namespace

bool merge(const QJsonObject& args, Settings* settings, QString* error, bool* refused)
{
	Settings next = *settings;
	*refused = false;
	if (!mergeNumbers(args, &next, error, refused) || !mergeFlags(args, &next, error, refused)) { return false; }
	*settings = next;
	return true;
}

} // namespace lmms::consolestrip
