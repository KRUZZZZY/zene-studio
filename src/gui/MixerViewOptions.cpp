/*
 * MixerViewOptions.cpp - the mixer view's options: sections, strip width, channel filter, meter mode and scale
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
#include "MixerViewOptions.h"

#include <algorithm>
#include <cmath>

#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QSet>
#include <QStackedWidget>

#include "CommandForm.h"
#include "ConfigManager.h"
#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "Effect.h"
#include "Fader.h"
#include "Knob.h"
#include "LcdWidget.h"
#include "MacroKnobs.h"
#include "Mixer.h"
#include "MixerChannelView.h"
#include "ConsoleStripSection.h"
#include "ControlMeterSupport.h"
#include "Song.h"
#include "StripMeters.h"
#include "Track.h"
#include "lmms_math.h"

namespace lmms::gui::mixerviewoptions
{

namespace
{

Options s_options;
bool s_loaded = false;
int s_generation = 0;
QSet<int> s_selectedIds;  //!< MX-005: MixerChannel::id()s

const QString kSection = QStringLiteral("ui");
const QString kKey = QStringLiteral("mixerview");

QString pick(const QJsonObject& args, const char* key, const QStringList& names, const QString& current, QString* error)
{
	const QString name = QLatin1String(key);
	if (!args.contains(name)) { return current; }
	const QString value = args.value(name).toString();
	if (!names.contains(value)) { *error = QStringLiteral("'%1' is one of %2").arg(name, names.join(QStringLiteral(" / "))); }
	return value;
}

//! The channels the filter is about: those instrument tracks play into, those sample tracks play into.
void trackChannels(QSet<int>* instruments, QSet<int>* audio)
{
	for (Track* track : Engine::getSong()->tracks())
	{
		IntModel* channel = track->mixerChannelModel();
		if (channel == nullptr) { continue; }
		if (track->type() == Track::Type::Instrument) { instruments->insert(channel->value()); }
		if (track->type() == Track::Type::Sample) { audio->insert(channel->value()); }
	}
}

int nameWidth(const QString& width)
{
	return width == QLatin1String("narrow") ? 40 : width == QLatin1String("wide") ? 88 : 56;
}

float minPeakOf(const QString& scale)
{
	return dbfsToAmp(scale == QLatin1String("db60") ? -60.f : scale == QLatin1String("db24") ? -24.f : -42.f);
}

//! Q050-108: the strip's own mode's reading, in words - the VU needle, or the short-term loudness and true peak.
QString modeReading(const MixerChannel* channel)
{
	const auto number = [](float v, int decimals) { return std::isfinite(v) ? QString::number(static_cast<double>(v), 'f', decimals) : QStringLiteral("-inf"); };
	const int mode = stripmeters::mode(*channel);
	if (mode == static_cast<int>(stripmeters::Mode::Vu))
	{
		const double reference = control::meterReferenceDbfs();
		const float vu = std::max(stripmeters::vu(*channel, 0, reference), stripmeters::vu(*channel, 1, reference));
		return QObject::tr("VU %1").arg(number(vu, 1));
	}
	const StripLoudness* loudness = stripmeters::loudness(*channel);
	if (mode != static_cast<int>(stripmeters::Mode::Lufs) || loudness == nullptr) { return QString(); }
	return QObject::tr("S %1 LUFS TP %2").arg(number(loudness->shortTermLufs(), 1), number(loudness->truePeakDbtp(), 1));
}

QString meterText(MixerChannel* channel)
{
	QStringList parts;
	if (const QString reading = modeReading(channel); !reading.isEmpty()) { parts << reading; }  // Q050-108
	const float reduction = channel->m_gainReduction.get();
	if (reduction >= 0.1f) { parts << QStringLiteral("GR %1").arg(static_cast<double>(reduction), 0, 'f', 1); }
	if (channel->m_clipped.load(std::memory_order_relaxed))
	{
		// The palette's record red (4.9:1 or more on a strip in every palette), not a fixed #e04040 (3.8:1 on light).
		parts << QStringLiteral("<a href=\"clear\" style=\"color:%1;\">CLIP</a>").arg(tokens::palette().record.name());
	}
	return parts.isEmpty() ? QStringLiteral("&nbsp;") : parts.join(QStringLiteral(" "));
}

//! The pre-fader peak since the last read, or the RMS; the read takes the pre-fader peak (as the
//! inherited feed takes m_peakLeft), and the post-fader peak is let go so it does not pile up.
void meterSource(MixerChannel* channel, bool rms, float* left, float* right)
{
	if (rms)
	{
		*left = channel->m_rmsLeft.get();
		*right = channel->m_rmsRight.get();
	}
	else
	{
		*left = channel->m_preMeterLeft.get();
		*right = channel->m_preMeterRight.get();
		channel->m_preMeterLeft.set(0.f);
		channel->m_preMeterRight.set(0.f);
	}
	channel->m_peakLeft = channel->m_peakRight = -1.f;
}

//! L10: the inserts by name (a disabled one in brackets) and the macro knobs, each when it has something to show.
void applyRackSections(MixerChannelView* view, MixerChannel* channel, const Options& o)
{
	QStringList names;
	for (const Effect* effect : channel->m_fxChain.effects())
	{
		const QString name = effect->displayName();
		names << (effect->isEnabled() ? name : QLatin1Char('[') + name + QLatin1Char(']'));
	}
	view->insertList()->setVisible(o.inserts && !names.isEmpty());
	if (o.inserts) { view->insertList()->setText(names.join(QLatin1Char('\n'))); }
	view->macroKnobs()->refresh();
	view->macroKnobs()->setVisible(o.macros && view->macroKnobs()->knobCount() > 0);
}

} // namespace

const QStringList& widthNames()
{
	static const QStringList names{QStringLiteral("narrow"), QStringLiteral("normal"), QStringLiteral("wide")};
	return names;
}

const QStringList& filterNames()
{
	static const QStringList names{QStringLiteral("all"), QStringLiteral("instruments"), QStringLiteral("audio"),
		QStringLiteral("returns"), QStringLiteral("used"), QStringLiteral("selected")};
	return names;
}

const QStringList& meterModeNames()
{
	static const QStringList names{QStringLiteral("peak"), QStringLiteral("rms"), QStringLiteral("pre_fader")};
	return names;
}

const QStringList& meterScaleNames()
{
	static const QStringList names{QStringLiteral("db42"), QStringLiteral("db60"), QStringLiteral("db24"), QStringLiteral("linear")};
	return names;
}

const QStringList& meterFallOffNames()
{
	static const QStringList names{QStringLiteral("fast"), QStringLiteral("medium"), QStringLiteral("slow")};
	return names;
}

const QStringList& peakHoldNames()
{
	static const QStringList names{QStringLiteral("off"), QStringLiteral("1.5s"), QStringLiteral("3s"), QStringLiteral("6s")};
	return names;
}

const QStringList& viewNames()
{
	static const QStringList names{QStringLiteral("mix"), QStringLiteral("routing")};
	return names;
}

const QStringList& densityNames()
{
	static const QStringList names{QStringLiteral("compact"), QStringLiteral("expanded")};
	return names;
}

const Options& options()
{
	if (!s_loaded)
	{
		s_loaded = true;
		const QJsonObject saved = QJsonDocument::fromJson(ConfigManager::inst()->value(kSection, kKey).toUtf8()).object();
		Options loaded;
		QString error;
		if (merge(saved, &loaded, &error)) { s_options = loaded; }
	}
	return s_options;
}

void setOptions(const Options& next)
{
	s_options = next;
	s_loaded = true;
	++s_generation;
	ConfigManager::inst()->setValue(kSection, kKey, QString::fromUtf8(QJsonDocument(toJson(next)).toJson(QJsonDocument::Compact)));
}

int generation() { return s_generation; }

Options effective(const Options& o)
{
	Options shown = o;
	const bool routing = o.view == QLatin1String("routing");
	if (routing) { shown.sends = true; }
	if (routing || o.density == QLatin1String("compact"))
	{
		shown.inserts = false;
		shown.macros = false;
		shown.console = false;  // Q050-088: a processing section, like the inserts
	}
	if (!routing && o.density == QLatin1String("compact")) { shown.sends = false; }
	return shown;
}

bool routingView() { return options().view == QLatin1String("routing"); }

bool compactDensity() { return options().density == QLatin1String("compact"); }

float meterFallOff()
{
	// Per 60 Hz tick: fast is the inherited 1.25 (about 116 dB/s), medium about 26 dB/s, slow about 12 dB/s -
	// the fall of a peak programme meter (20 dB in 1.7 s).
	const QString& name = options().meterFallOff;
	return name == QLatin1String("slow") ? 1.0233f : name == QLatin1String("medium") ? 1.0512f : 1.25f;
}

int peakHoldMs(const QString& name)
{
	return name == QLatin1String("off") ? 0 : name == QLatin1String("3s") ? 3000 : name == QLatin1String("6s") ? 6000 : 1500;
}

QJsonObject toJson(const Options& o)
{
	return {{QStringLiteral("show_sends"), o.sends}, {QStringLiteral("show_pan"), o.pan}, {QStringLiteral("show_number"), o.number},
		{QStringLiteral("show_meter_info"), o.meterInfo}, {QStringLiteral("show_inserts"), o.inserts},
		{QStringLiteral("show_macros"), o.macros}, {QStringLiteral("show_console"), o.console}, {QStringLiteral("strip_width"), o.width}, {QStringLiteral("filter"), o.filter},
		{QStringLiteral("meter_mode"), o.meterMode}, {QStringLiteral("meter_scale"), o.meterScale},
		{QStringLiteral("meter_falloff"), o.meterFallOff}, {QStringLiteral("peak_hold"), o.peakHold},
		{QStringLiteral("view"), o.view}, {QStringLiteral("density"), o.density}};
}

bool merge(const QJsonObject& args, Options* base, QString* error)
{
	Options next = *base;
	next.sends = args.value(QStringLiteral("show_sends")).toBool(next.sends);
	next.pan = args.value(QStringLiteral("show_pan")).toBool(next.pan);
	next.number = args.value(QStringLiteral("show_number")).toBool(next.number);
	next.meterInfo = args.value(QStringLiteral("show_meter_info")).toBool(next.meterInfo);
	next.inserts = args.value(QStringLiteral("show_inserts")).toBool(next.inserts);
	next.macros = args.value(QStringLiteral("show_macros")).toBool(next.macros);
	next.console = args.value(QStringLiteral("show_console")).toBool(next.console);  // Q050-088
	next.width = pick(args, "strip_width", widthNames(), next.width, error);
	next.filter = pick(args, "filter", filterNames(), next.filter, error);
	next.meterMode = pick(args, "meter_mode", meterModeNames(), next.meterMode, error);
	next.meterScale = pick(args, "meter_scale", meterScaleNames(), next.meterScale, error);
	next.meterFallOff = pick(args, "meter_falloff", meterFallOffNames(), next.meterFallOff, error);
	next.peakHold = pick(args, "peak_hold", peakHoldNames(), next.peakHold, error);
	next.view = pick(args, "view", viewNames(), next.view, error);
	next.density = pick(args, "density", densityNames(), next.density, error);
	if (!error->isEmpty()) { return false; }
	*base = next;
	return true;
}

QList<int> selectedChannels()
{
	QList<int> indices;
	for (int i = 1; i < Engine::mixer()->numChannels(); ++i)
	{
		if (s_selectedIds.contains(Engine::mixer()->mixerChannel(i)->id())) { indices.append(i); }
	}
	return indices;
}

bool isSelected(int index)
{
	MixerChannel* channel = index > 0 ? Engine::mixer()->mixerChannel(index) : nullptr;
	return channel != nullptr && s_selectedIds.contains(channel->id());
}

void setSelectedChannels(const QList<int>& indices)
{
	s_selectedIds.clear();
	for (const int index : indices)
	{
		MixerChannel* channel = index > 0 ? Engine::mixer()->mixerChannel(index) : nullptr;
		if (channel != nullptr) { s_selectedIds.insert(channel->id()); }
	}
	++s_generation;
}

void clearSelection()
{
	s_selectedIds.clear();
	++s_generation;
}

QJsonArray selectionJson()
{
	QJsonArray ids;
	for (const int index : selectedChannels()) { ids.append(control::channelIdOf(Engine::mixer()->mixerChannel(index))); }
	return ids;
}

void toggleSelected(QWidget* origin, int index)
{
	if (index <= 0) { return; }  // the master always shows: it is never in the selection
	QStringList ids;
	for (const QJsonValue& id : selectionJson()) { ids.append(id.toString()); }
	const QString id = control::channelIdOf(Engine::mixer()->mixerChannel(index));
	if (!ids.removeOne(id)) { ids.append(id); }
	runCommand(origin, CommandCall{QStringLiteral("window.mixer_view"),
		{{QStringLiteral("selected_channels"), QJsonArray::fromStringList(ids)}}, {}, QString(), false});
}

bool shows(int index)
{
	const QString& filter = options().filter;
	if (index == 0 || filter == QLatin1String("all")) { return true; }
	if (filter == QLatin1String("selected")) { return isSelected(index); }
	MixerChannel* channel = Engine::mixer()->mixerChannel(index);
	if (filter == QLatin1String("returns")) { return channel->isBus(); }
	QSet<int> instruments;
	QSet<int> audio;
	trackChannels(&instruments, &audio);
	if (filter == QLatin1String("instruments")) { return instruments.contains(index); }
	if (filter == QLatin1String("audio")) { return audio.contains(index); }
	return instruments.contains(index) || audio.contains(index) || !channel->m_receives.empty();  // used
}

void applyToStrip(MixerChannelView* view, MixerChannel* channel)
{
	const Options o = effective(options());  // theme 16: the view and the density decide the sections
	view->setSendsShown(o.sends);
	view->panKnob()->setVisible(o.pan);
	view->numberLcd()->setVisible(o.number);
	view->nameEdit()->setFixedWidth(nameWidth(o.width));
	view->meterInfo()->setVisible(o.meterInfo);
	if (o.meterInfo) { view->meterInfo()->setText(meterText(channel)); }
	applyRackSections(view, channel, o);
	view->consoleSection()->setVisible(o.console);  // Q050-088
	if (o.console) { view->consoleSection()->refresh(); }
	Fader* fader = view->fader();
	fader->setMinPeak(minPeakOf(o.meterScale));
	fader->setProperty("levelsDisplayedInDBFS", o.meterScale != QLatin1String("linear"));
	fader->setPeakHoldMs(peakHoldMs(o.peakHold));  // MX-128
}

//! MX-133: the strip's readings as a line of text - the pre-fader peak (whichever point the bars show), RMS,
//! gain reduction, correlation.
QString meterReadout(const MixerChannel* channel)
{
	const auto db = [](float v) { return v > 0.f ? QString::number(20.0 * std::log10(v), 'f', 1) : QStringLiteral("-inf"); };
	return QObject::tr("Pre-fader peak %1 / %2 dB   RMS %3 / %4 dB   GR %5 dB   Correlation %6")
		.arg(db(channel->m_prePeakLeft.get()), db(channel->m_prePeakRight.get()), db(channel->m_rmsLeft.get()), db(channel->m_rmsRight.get()))
		.arg(channel->m_gainReduction.get(), 0, 'f', 1).arg(channel->m_correlation.get(), 0, 'f', 2);
}

QString meterModeOf(const MixerChannel* channel)
{
	const int own = stripmeters::mode(*channel);
	return own == static_cast<int>(stripmeters::Mode::Default) ? options().meterMode : stripmeters::modeNames().value(own);
}

bool feedMeter(Fader* fader, MixerChannel* channel)
{
	if (fader->underMouse()) { fader->setToolTip(meterReadout(channel)); }  // only while someone is looking (MX-133)
	const QString mode = meterModeOf(channel);  // Q050-108: the strip's own mode, else the view's
	if (mode == QLatin1String("peak")) { return false; }
	float left = 0.f;
	float right = 0.f;
	if (mode == QLatin1String("vu"))
	{
		// The needle as the bars: a steady sine shows its peak level, so 0 VU sits at the reference's dBFS.
		left = channel->m_vuLeft.get();
		right = channel->m_vuRight.get();
		channel->m_peakLeft = channel->m_peakRight = -1.f;
	}
	else if (mode == QLatin1String("lufs"))
	{
		// The momentary loudness as a level (-23 LUFS draws at -23 dB); the meter line reads the short-term and the true peak.
		const StripLoudness* loudness = stripmeters::loudness(*channel);
		const float lufs = loudness != nullptr ? loudness->momentaryLufs() : LufsMeter::MinusInfinity;
		left = right = std::isfinite(lufs) ? dbfsToAmp(lufs) : 0.f;
		channel->m_peakLeft = channel->m_peakRight = -1.f;
	}
	else { meterSource(channel, mode == QLatin1String("rms"), &left, &right); }
	const float fallOff = meterFallOff();  // MX-128
	fader->setPeak_L(std::max(left, fader->getPeak_L() / fallOff));
	fader->setPeak_R(std::max(right, fader->getPeak_R() / fallOff));
	return true;
}

} // namespace lmms::gui::mixerviewoptions
