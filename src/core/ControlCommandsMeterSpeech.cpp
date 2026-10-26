/*
 * ControlCommandsMeterSpeech.cpp - Q050-100: the spoken meter and the peak-watch preference
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <algorithm>
#include <cmath>

#include <QCoreApplication>
#include <QJsonObject>
#include <QString>

#include "AccessibleWidgets.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlEdit.h"
#include "ControlMeterSupport.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "LufsMeter.h"
#include "MasterLoudnessTap.h"
#include "Mixer.h"

namespace lmms
{

using namespace control;

namespace
{

MasterLoudnessTap* engineTap()
{
	AudioEngine* engine = Engine::audioEngine();
	return engine != nullptr ? engine->masterLoudness() : nullptr;
}

double peakDb(float left, float right)
{
	const float peak = std::max(left, right);
	return peak > 0.f ? 20.0 * std::log10(static_cast<double>(peak)) : -120.0;
}

ControlResult handleSpeak(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
	if (channel == nullptr) { return error; }
	const double channelDb = peakDb(channel->m_speechPeakLeft.get(), channel->m_speechPeakRight.get());

	const MasterLoudnessTap* tap = engineTap();
	const bool tapArmed = tap != nullptr && tap->enabled();
	const MasterLoudnessTap::Snapshot live = tap != nullptr ? tap->snapshot() : MasterLoudnessTap::Snapshot{};
	const bool lufsAvailable = tapArmed && std::isfinite(live.shortTermLufs);
	const double lufs = lufsAvailable ? static_cast<double>(live.shortTermLufs) : 0.0;

	QString spoken = QStringLiteral("Channel %1 peak %2 dBFS.").arg(channel->m_name, QString::number(channelDb, 'f', 1));
	if (lufsAvailable)
	{
		spoken += QStringLiteral(" Master short-term loudness %1 LUFS.").arg(QString::number(lufs, 'f', 1));
	}
	else if (tap == nullptr)
	{
		spoken += QStringLiteral(" Master short-term loudness unavailable: this instance has no audio engine.");
	}
	else if (!tapArmed)
	{
		spoken += QStringLiteral(" Master short-term loudness unavailable: the master loudness tap is not armed.");
	}
	else
	{
		spoken += QStringLiteral(" Master short-term loudness unavailable: its three-second window is not ready.");
	}
	a11y::announceStatus(QCoreApplication::instance(), spoken, a11y::Politeness::Polite, a11y::Repeat::Allow);

	return ControlResult::success({
		{QStringLiteral("channel"), channelIdOf(channel)},
		{QStringLiteral("name"), channel->m_name},
		{QStringLiteral("peak_db"), channelDb},
		{QStringLiteral("master_lufs_available"), lufsAvailable},
		{QStringLiteral("master_lufs"), lufsAvailable ? QJsonValue(lufs) : QJsonValue(QJsonValue::Null)},
		{QStringLiteral("tap_armed"), tapArmed},
		{QStringLiteral("spoken"), spoken},
	});
}

ControlResult handlePeakWatch(const QJsonObject& args)
{
	const bool beforeEnabled = meterPeakWatchEnabled();
	const double beforeThreshold = meterPeakWatchThresholdDbfs();
	const bool enabled = args.contains(QStringLiteral("enabled")) ? args.value(QStringLiteral("enabled")).toBool() : beforeEnabled;
	const double threshold = args.contains(QStringLiteral("threshold_dbfs"))
		? args.value(QStringLiteral("threshold_dbfs")).toDouble() : beforeThreshold;
	if (!std::isfinite(threshold) || threshold < -60.0 || threshold > 0.0)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'threshold_dbfs' is -60..0"));
	}
	const bool changed = enabled != beforeEnabled || threshold != beforeThreshold;
	if (changed)
	{
		setMeterPeakWatchEnabled(enabled);
		setMeterPeakWatchThresholdDbfs(threshold);
		addUndoStep(
			[beforeEnabled, beforeThreshold]() {
				setMeterPeakWatchEnabled(beforeEnabled);
				setMeterPeakWatchThresholdDbfs(beforeThreshold);
			},
			[enabled, threshold]() {
				setMeterPeakWatchEnabled(enabled);
				setMeterPeakWatchThresholdDbfs(threshold);
			});
	}
	QJsonObject result{{QStringLiteral("enabled"), enabled}, {QStringLiteral("threshold_dbfs"), threshold},
		{QStringLiteral("previous_enabled"), beforeEnabled}, {QStringLiteral("previous_threshold_dbfs"), beforeThreshold}};
	if (changed)
	{
		result.insert(QStringLiteral("__transaction"),
			transactionPayload({{QStringLiteral("enabled"), beforeEnabled}, {QStringLiteral("threshold_dbfs"), beforeThreshold}},
				QStringLiteral("control.undo"), {}, true,
				QStringLiteral("action checkpoint: the recorded undo step restores the peak-watch preference "
					"(enabled and threshold in the config file)")));
	}
	return ControlResult::success(result);
}

} // namespace

void registerMeterSpeechCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("meter.speak");
		cmd.group = QStringLiteral("meter");
		cmd.verb = QStringLiteral("speak");
		cmd.description = QStringLiteral("Speak, through the screen reader, the focused mixer channel's post-fader stereo "
			"sample peak in dBFS and the master's short-term loudness (LUFS-S). The master tap is read, never armed or "
			"reset; when it is disarmed or its three-second window is not ready, the reply says the loudness is "
			"unavailable and the speech says why instead of speaking a zero. Read only.");
		cmd.argsSchema = objectSchema({{QStringLiteral("channel"), stringProperty()}}, {QStringLiteral("channel")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("channel"), stringProperty()},
			{QStringLiteral("name"), stringProperty()},
			{QStringLiteral("peak_db"), numberProperty()},
			{QStringLiteral("master_lufs_available"), booleanProperty()},
			{QStringLiteral("master_lufs"), nullable(numberProperty())},
			{QStringLiteral("tap_armed"), booleanProperty()},
			{QStringLiteral("spoken"), stringProperty()},
		});
		cmd.mutating = false;
		cmd.handler = [](const QJsonObject& args) { return handleSpeak(args); };
		registry.registerCommand(cmd);
	}
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("meter.set_peak_watch");
		cmd.group = QStringLiteral("meter");
		cmd.verb = QStringLiteral("set_peak_watch");
		cmd.description = QStringLiteral("Set the peak watch: 'enabled' and 'threshold_dbfs' (-60..0, -6 by default). "
			"When on, the input stereo bus and the master output each speak one announcement when their peak rises "
			"from below the threshold to at or above it, stay quiet while it remains high, and arm again after it "
			"falls below. The threshold is a preference in the config file; one undo step restores both values. With "
			"no arguments it reports them.");
		cmd.argsSchema = objectSchema({{QStringLiteral("enabled"), booleanProperty()},
			{QStringLiteral("threshold_dbfs"), numberProperty()}});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("enabled"), booleanProperty()},
			{QStringLiteral("threshold_dbfs"), numberProperty()},
			{QStringLiteral("previous_enabled"), booleanProperty()},
			{QStringLiteral("previous_threshold_dbfs"), numberProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return handlePeakWatch(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
