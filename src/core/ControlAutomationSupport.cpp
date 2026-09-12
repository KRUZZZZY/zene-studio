/*
 * ControlAutomationSupport.cpp - shared helpers for the automation.* command
 *                                group (SPEC A11-A16).
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

#include "ControlAutomationSupport.h"

#include "ControlVocabulary.h"

#include <utility>

#include "AutomatableModel.h"
#include "AutomationClip.h"
#include "AutomationTrack.h"
#include "ControlDeviceSupport.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "PatternStore.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace control
{

QString AutomationParameter::id() const
{
	return pluginId + QLatin1Char('/') + QString::number(index);
}

namespace
{

bool parseParameterId(const QString& id, QString* pluginId, int* index, ControlResult* error)
{
	const int slash = id.indexOf(QLatin1Char('/'));
	bool ok = false;
	const int parsed = slash < 0 ? -1 : id.mid(slash + 1).toInt(&ok);
	if (slash <= 0 || !ok || parsed < 0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is not a parameter id of the form <plugin>/<index> "
				"(automation.get_state returns them)").arg(id));
		return false;
	}
	*pluginId = id.left(slash);
	*index = parsed;
	return true;
}

QString progressionName(AutomationClip::ProgressionType type)
{
	switch (type)
	{
		case AutomationClip::ProgressionType::Discrete: return QStringLiteral("discrete");
		case AutomationClip::ProgressionType::Linear: return QStringLiteral("linear");
		case AutomationClip::ProgressionType::CubicHermite: return QStringLiteral("cubic_hermite");
	}
	return QStringLiteral("unknown");
}

//! The index of \a clip's track in the song container, or -1 (a clip on the
//! hidden global automation track, for instance, is not addressable as trk-<n>).
int songIndexOfTrack(const Track* track)
{
	const TrackContainer::TrackList& tracks = Engine::getSong()->tracks();
	for (int i = 0; i < static_cast<int>(tracks.size()); ++i)
	{
		if (tracks[i] == track) { return i; }
	}
	return -1;
}

int clipIndexInTrack(const AutomationClip* clip)
{
	const Track* track = clip->getTrack();
	if (track == nullptr) { return -1; }
	const Track::clipVector& clips = track->getClips();
	for (std::size_t i = 0; i < clips.size(); ++i)
	{
		if (clips[i] == clip) { return static_cast<int>(i); }
	}
	return -1;
}

QJsonObject pointJson(const AutomationClip::TimemapIterator& it, const AutomatableModel* model)
{
	QJsonObject point;
	point.insert(QStringLiteral("ticks"), POS(it));
	point.insert(QStringLiteral("value"), static_cast<double>(model->scaledValue(INVAL(it))));
	point.insert(QStringLiteral("raw_value"), static_cast<double>(INVAL(it)));
	point.insert(QStringLiteral("out_value"), static_cast<double>(model->scaledValue(OUTVAL(it))));
	point.insert(QStringLiteral("in_tangent"), static_cast<double>(INTAN(it)));
	point.insert(QStringLiteral("out_tangent"), static_cast<double>(OUTTAN(it)));
	point.insert(QStringLiteral("locked_tangents"), LOCKEDTAN(it));
	return point;
}

} // namespace

QList<AutomationParameter> automationParameters(const ControlTarget& target)
{
	QList<AutomationParameter> out;
	if (target.instrumentTrack != nullptr && target.instrumentTrack->instrument() != nullptr)
	{
		Instrument* instrument = target.instrumentTrack->instrument();
		const QList<AutomatableModel*> models = controlInstrumentParameters(instrument);
		const QString name = QString::fromUtf8(instrument->descriptor()->name);
		for (int i = 0; i < models.size(); ++i)
		{
			out.append({QStringLiteral("inst"), name, i, models[i]});
		}
	}
	if (target.chain == nullptr) { return out; }

	const std::vector<Effect*>& effects = target.chain->effects();
	for (std::size_t e = 0; e < effects.size(); ++e)
	{
		const QList<AutomatableModel*> models = controlEffectParameters(effects[e]);
		const QString name = QString::fromUtf8(effects[e]->descriptor()->name);
		for (int i = 0; i < models.size(); ++i)
		{
			out.append({effectId(static_cast<int>(e)), name, i, models[i]});
		}
	}
	return out;
}

bool findAutomationParameter(const ControlTarget& target, const QString& id,
	AutomationParameter* out, ControlResult* error)
{
	QString pluginId;
	int index = 0;
	if (!parseParameterId(id, &pluginId, &index, error)) { return false; }

	const QList<AutomationParameter> parameters = automationParameters(target);
	for (const AutomationParameter& parameter : parameters)
	{
		if (parameter.pluginId == pluginId && parameter.index == index)
		{
			*out = parameter;
			return true;
		}
	}
	*error = ControlResult::failure(ControlErrorKind::NotFound,
		QStringLiteral("no parameter %1 on %2 (it exposes %3)")
			.arg(id, target.id).arg(parameters.size()));
	return false;
}

ControlResult automatableRangeRefusal(const AutomatableModel* model, double value)
{
	return ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("value %1 is outside the range %2..%3 of parameter '%4'")
			.arg(value)
			.arg(static_cast<double>(model->minValue<float>()))
			.arg(static_cast<double>(model->maxValue<float>()))
			.arg(model->displayName()));
}

namespace
{

//! Every automation track the engine plays automation from: the song's own
//! tracks, the pattern store's, and the song's hidden global automation track.
QList<Track*> automationTracks()
{
	QList<Track*> out;
	TrackContainer* containers[] = {Engine::getSong(), Engine::patternStore()};
	for (TrackContainer* container : containers)
	{
		if (container == nullptr) { continue; }
		for (Track* track : container->tracks())
		{
			if (track->type() == Track::Type::Automation ||
				track->type() == Track::Type::HiddenAutomation)
			{
				out.append(track);
			}
		}
	}
	if (Engine::getSong() != nullptr && Engine::getSong()->globalAutomationTrack() != nullptr)
	{
		out.append(Engine::getSong()->globalAutomationTrack());
	}
	return out;
}

} // namespace

AutomationClip* existingAutomationClip(AutomatableModel* model)
{
	for (Track* track : automationTracks())
	{
		for (Clip* clip : track->getClips())
		{
			AutomationClip* automation = dynamic_cast<AutomationClip*>(clip);
			if (automation == nullptr) { continue; }
			for (const QPointer<AutomatableModel>& object : automation->objects())
			{
				if (object == model) { return automation; }
			}
		}
	}
	return nullptr;
}

AutomationClip* automationClipForModel(AutomatableModel* model, bool* created, ControlResult* error)
{
	*created = false;
	if (AutomationClip* existing = existingAutomationClip(model)) { return existing; }

	Song* song = Engine::getSong();
	Track* track = song == nullptr ? nullptr : Track::create(Track::Type::Automation, song);
	AutomationClip* clip = track == nullptr
		? nullptr
		: dynamic_cast<AutomationClip*>(track->createClip(TimePos(0)));
	if (clip == nullptr)
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the engine could not create an automation track for this parameter"));
		return nullptr;
	}
	// Binding the model seeds the clip with one node at tick 0 holding the
	// model's current value: the engine's own convention (AutomationClip::addObject).
	clip->addObject(model, false);
	*created = true;
	return clip;
}

QJsonArray automationPointsJson(AutomationClip* clip, const AutomatableModel* model)
{
	QJsonArray points;
	const AutomationClip::timeMap& map = clip->getTimeMap();
	for (AutomationClip::TimemapIterator it = map.constBegin(); it != map.constEnd(); ++it)
	{
		points.append(pointJson(it, model));
	}
	return points;
}

QJsonObject automationJson(AutomationClip* clip, const AutomatableModel* model)
{
	const int index = songIndexOfTrack(clip->getTrack());
	QJsonArray points = automationPointsJson(clip, model);

	QJsonObject out;
	out.insert(QStringLiteral("track"),
		index >= 0 ? trackIdOf(clip->getTrack()) : QString());
	out.insert(QStringLiteral("clip_index"), clipIndexInTrack(clip));
	out.insert(QStringLiteral("clip_type"), clip->nodeName());
	out.insert(QStringLiteral("progression"), progressionName(clip->progressionType()));
	out.insert(QStringLiteral("point_count"), points.size());
	out.insert(QStringLiteral("points"), points);
	return out;
}

QJsonObject automationParameterJson(const AutomationParameter& parameter)
{
	QJsonObject entry;
	entry.insert(QStringLiteral("id"), parameter.id());
	entry.insert(QStringLiteral("plugin"), parameter.pluginId);
	entry.insert(QStringLiteral("device_plugin"), parameter.devicePlugin);
	entry.insert(QStringLiteral("index"), parameter.index);
	entry.insert(QStringLiteral("name"), parameter.model->displayName());
	entry.insert(QStringLiteral("value"), static_cast<double>(parameter.model->value<float>()));
	entry.insert(QStringLiteral("min"), static_cast<double>(parameter.model->minValue<float>()));
	entry.insert(QStringLiteral("max"), static_cast<double>(parameter.model->maxValue<float>()));

	AutomationClip* clip = existingAutomationClip(parameter.model);
	const bool automated = clip != nullptr && clip->hasAutomation();
	entry.insert(QStringLiteral("automated"), automated);
	if (automated) { entry.insert(QStringLiteral("automation"), automationJson(clip, parameter.model)); }
	return entry;
}

QJsonObject automationTransaction(const AutomationParameter& parameter, const QJsonArray& before,
	int pointCountBefore, bool created)
{
	QJsonObject inverse;
	inverse.insert(QStringLiteral("op"), created
		? QStringLiteral("remove the automation track this command created")
		: QStringLiteral("reapply the recorded points"));
	inverse.insert(QStringLiteral("points_before"), before);

	QJsonObject beforeState;
	beforeState.insert(QStringLiteral("parameter"), parameter.id());
	beforeState.insert(QStringLiteral("point_count"), pointCountBefore);
	beforeState.insert(QStringLiteral("points"), before);

	QJsonObject transaction;
	transaction.insert(QStringLiteral("before"), beforeState);
	transaction.insert(QStringLiteral("inverse"), inverse);
	// SPEC A16 deliverable 5: even the creating call is one undoable step, so
	// both halves are reversible:true and the difference is the MECHANISM, not
	// the verdict.
	transaction.insert(QStringLiteral("reversible"), true);
	transaction.insert(QStringLiteral("mechanism"), created
		? QStringLiteral("action checkpoint: the AutomationTrack this call created is removed by "
			"the recorded undo step, so the first point is one undoable step like every later "
			"one. LIMIT: redo is not offered for the creating call - re-issue "
			"automation.add_point")
		: QStringLiteral("ProjectJournal (AutomationClip checkpoint)"));
	return transaction;
}

} // namespace control

} // namespace lmms
