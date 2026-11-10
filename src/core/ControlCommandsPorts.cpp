/*
 * ControlCommandsPorts.cpp - the port.* command group (SPEC A11-A16).
 *
 * Feature row 29 of docs/FEATURE-LIST-0.3.0.md ("Audio ports / AudioBus"), the
 * pin-matrix half. The engine side exists and is proven by registered tests
 * (tests/src/core/AudioPortsTest.cpp, AudioPortsModelTest.cpp,
 * PluginAudioPortsTest.cpp): include/AudioPortsModel.h:67 is the model,
 * AudioPortsModel::Matrix is the per-direction pin matrix (Matrix::enabled /
 * Matrix::setPin, include/AudioPortsModel.h:94-111) and
 * Effect::audioPortsModel() / Instrument::audioPortsModel() are the accessors
 * the PinConnector view is built from. The audit's row 29 says "pin and bus
 * topology are reachable only from C++". This file registers the pin writes.
 *
 * THE ONE THING A READER SHOULD CHECK (and why it is not a hack): the model is
 * reached through a const accessor, and the write goes through a const_cast -
 * exactly what the engine does itself when it hands the model to its editor,
 * AudioPortsModel::instantiateView() (src/core/AudioPortsModel.cpp:339-345:
 * "This method does not modify AudioPortsModel, but it needs PinConnector to
 * store a mutable pointer to the AudioPortsModel, hence the const_cast"). The
 * object is mutable by construction; the const in the accessor describes the
 * accessor, not the object. Control handlers run on the UI thread, the same
 * thread the PinConnector writes from, so this is the GUI's own write path
 * reached from the socket instead of from a mouse.
 *
 * REVERSIBILITY: a pin is one bool in the processor's own <pins> element. It is
 * a bounded recorded state and the inverse is the SAME COMMAND with the value
 * captured before the write (`applies: command`, dispatched by control.undo),
 * which is why port.set_pin is a `snapshot` row and not `true_inverse`: there is
 * no JournallingObject behind an AudioPortsModel (Model is a QObject; the class
 * is a SerializingObject, not a JournallingObject), so no checkpoint exists.
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

#include <algorithm>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>

#include "ControlDeviceSupport.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"

#include "AudioPortsModel.h"
#include "ControlMixerSupport.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "Mixer.h"
#include "MixerSurround.h"
#include "Song.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The tap points of a sidechain send, as the schema's enum.
QString directionListText()
{
	return QStringLiteral("\"in\" or \"out\"");
}

//! The device's audio-ports model, or nullptr - with the reason in *error when
//! there is none, so every caller reports the same bound.
const AudioPortsModel* resolvePortsModel(const ControlTarget& target, const QString& deviceId,
	ControlResult* error)
{
	ControlDeviceHandle handle;
	if (!resolveControlDevice(target, deviceId, &handle, error)) { return nullptr; }

	const AudioPortsModel* model = nullptr;
	if (handle.effect != nullptr) { model = handle.effect->audioPortsModel(); }
	else if (handle.track != nullptr && handle.track->instrument() != nullptr)
	{
		model = handle.track->instrument()->audioPortsModel();
	}
	if (model == nullptr)
	{
		// Not an error in the engine: most devices have no audio ports at all
		// (Effect::audioPortsModel() defaults to nullptr). Naming the fact is
		// the honest answer - the pin matrix belongs to a device that HAS one.
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("%1 on %2 exposes no audio-ports model, so it has no pin matrix to read "
				"or write (an audio-ports model comes from the audio-plugin base class; built-in "
				"effects have none)")
				.arg(deviceId, target.id));
		return nullptr;
	}
	return model;
}

//! One direction's matrix: its shape, the enabled pins, and the engine's own
//! used-channel caches.
QJsonObject matrixJson(const AudioPortsModel::Matrix& matrix)
{
	QJsonArray pins;
	for (ch_cnt_t trackChannel = 0; trackChannel < matrix.trackChannelCount(); ++trackChannel)
	{
		for (ch_cnt_t channel = 0; channel < matrix.channelCount(); ++channel)
		{
			if (!matrix.enabled(trackChannel, channel)) { continue; }
			pins.append(QJsonObject{
				{QStringLiteral("track_channel"), static_cast<int>(trackChannel)},
				{QStringLiteral("processor_channel"), static_cast<int>(channel)}});
		}
	}

	QJsonArray usedTrackChannels;
	for (std::size_t i = 0; i < MaxTrackChannels; ++i)
	{
		if (matrix.usedTrackChannels().test(i)) { usedTrackChannels.append(static_cast<int>(i)); }
	}
	QJsonArray usedChannels;
	for (std::size_t i = 0; i < matrix.usedChannels().size(); ++i)
	{
		if (matrix.usedChannels()[i]) { usedChannels.append(static_cast<int>(i)); }
	}
	QJsonArray names;
	for (ch_cnt_t channel = 0; channel < matrix.channelCount(); ++channel)
	{
		names.append(matrix.channelName(channel));
	}

	QJsonObject out;
	out.insert(QStringLiteral("channel_count"), static_cast<int>(matrix.channelCount()));
	out.insert(QStringLiteral("track_channel_count"),
		static_cast<int>(matrix.trackChannelCount()));
	out.insert(QStringLiteral("is_output"), matrix.isOutput());
	out.insert(QStringLiteral("pins"), pins);
	out.insert(QStringLiteral("pin_count"), pins.size());
	out.insert(QStringLiteral("used_track_channels"), usedTrackChannels);
	out.insert(QStringLiteral("used_channels"), usedChannels);
	out.insert(QStringLiteral("channel_names"), names);
	return out;
}

QJsonObject portsModelJson(const AudioPortsModel* model)
{
	QJsonObject out;
	out.insert(QStringLiteral("initialized"), model->initialized());
	out.insert(QStringLiteral("is_instrument"), model->isInstrument());
	out.insert(QStringLiteral("track_channel_count"),
		static_cast<int>(model->trackChannelCount()));
	out.insert(QStringLiteral("track_channels_upper_bound"),
		static_cast<int>(model->trackChannelsUpperBound()));
	// null when the direct-routing optimisation is not available (a non-default
	// pin layout); otherwise the track channel pair routed to/from the
	// processor. Reported as the engine reports it, null included.
	const std::optional<ch_cnt_t> direct = model->directRouting();
	out.insert(QStringLiteral("direct_routing"),
		direct.has_value() ? QJsonValue(static_cast<int>(*direct)) : QJsonValue(QJsonValue::Null));
	out.insert(QStringLiteral("in"), matrixJson(model->in()));
	out.insert(QStringLiteral("out"), matrixJson(model->out()));
	return out;
}

//! Q050-155.2: the bus the device's track channels address (controlHostBusChannels), as port.get_state reports it.
QJsonObject hostBusJson(const ControlTarget& target, const AudioPortsModel* model)
{
	const int channels = controlHostBusChannels(target.id);
	const QString format = channels > 2 ? surround::formatName(surround::discreteFormat(channels))
		: QStringLiteral("stereo");
	const int reached = std::min(channels, static_cast<int>(model->trackChannelCount()));
	return QJsonObject{{QStringLiteral("format"), format}, {QStringLiteral("channels"), channels},
		{QStringLiteral("reached_track_channels"), reached}};
}

//! The highest track channel a pin may name: the model's own, or its host bus's when that is wider.
int trackChannelBound(const ControlTarget& target, const AudioPortsModel::Matrix& matrix)
{
	return std::max(static_cast<int>(matrix.trackChannelCount()), controlHostBusChannels(target.id));
}

//! The matrix a direction name selects, or nullptr (typed) when the name is not
//! one of the two.
AudioPortsModel::Matrix* matrixForDirection(AudioPortsModel* model, const QString& direction,
	ControlResult* error)
{
	if (direction == QLatin1String("in")) { return &model->in(); }
	if (direction == QLatin1String("out")) { return &model->out(); }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("'%1' is not a direction; use %2")
			.arg(direction, directionListText()));
	return nullptr;
}

//! The processor's ports model, validated and writable, with a typed refusal for
//! every way the request can be wrong - all of them BEFORE a single pin moves.
AudioPortsModel* writableModel(const ControlTarget& target, const QString& deviceId,
	const QString& direction, ControlResult* error)
{
	const AudioPortsModel* model = resolvePortsModel(target, deviceId, error);
	if (model == nullptr) { return nullptr; }
	if (!model->initialized())
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 on %2 has an audio-ports model whose channel counts are not known "
				"yet (AudioPortsModel::initialized() is false), so it has no pin to set")
				.arg(deviceId, target.id));
		return nullptr;
	}
	ControlResult ignored;
	if (matrixForDirection(const_cast<AudioPortsModel*>(model), direction, &ignored) == nullptr)
	{
		return nullptr;
	}
	// See the file header: the engine hands its editor the same mutable pointer
	// (AudioPortsModel::instantiateView, src/core/AudioPortsModel.cpp:339).
	return const_cast<AudioPortsModel*>(model);
}

ControlResult handlePortGetState(const QJsonObject& args)
{
	ControlResult error;
	ControlTarget target;
	const QString targetId = args.value(QStringLiteral("target")).toString();
	if (!resolveControlTarget(targetId, &target, &error)) { return error; }
	const QString deviceId = args.value(QStringLiteral("device")).toString();
	const AudioPortsModel* model = resolvePortsModel(target, deviceId, &error);
	if (model == nullptr) { return error; }

	QJsonObject result;
	result.insert(QStringLiteral("target"), target.id);
	result.insert(QStringLiteral("device"), deviceId);
	result.insert(QStringLiteral("ports"), portsModelJson(model));
	result.insert(QStringLiteral("bus"), hostBusJson(target, model));
	result.insert(QStringLiteral("note"),
		QStringLiteral("the pin matrix is the processor's own serialized <pins> element; a pin is "
			"set with port.set_pin. track_channel indexes the song's track channels, "
			"processor_channel indexes the device's own ports"));
	return ControlResult::success(result);
}

ControlResult handlePortSetPin(const QJsonObject& args)
{
	ControlResult error;
	ControlTarget target;
	const QString targetId = args.value(QStringLiteral("target")).toString();
	if (!resolveControlTarget(targetId, &target, &error)) { return error; }
	const QString deviceId = args.value(QStringLiteral("device")).toString();
	const QString direction = args.value(QStringLiteral("direction")).toString();
	const int trackChannel = args.value(QStringLiteral("track_channel")).toInt(-1);
	const int processorChannel = args.value(QStringLiteral("processor_channel")).toInt(-1);
	const QJsonValue wanted = args.value(QStringLiteral("enabled"));

	if (!wanted.isBool())
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'enabled' must be a boolean"));
	}

	AudioPortsModel* model = writableModel(target, deviceId, direction, &error);
	if (model == nullptr) { return error; }
	AudioPortsModel::Matrix* matrix = matrixForDirection(model, direction, &error);
	if (matrix == nullptr) { return error; }

	// Validated before the write, so a refusal changes nothing.
	if (trackChannel < 0 || trackChannel >= trackChannelBound(target, *matrix))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("track_channel %1 is out of range: this device routes %2 track channels")
				.arg(trackChannel).arg(trackChannelBound(target, *matrix)));
	}
	if (processorChannel < 0 || processorChannel >= static_cast<int>(matrix->channelCount()))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("processor_channel %1 is out of range: the %2 matrix has %3 channels")
				.arg(processorChannel).arg(direction)
				.arg(static_cast<int>(matrix->channelCount())));
	}

	fitPinsToHostBus(target.id, model);  // Q050-155.2: a discrete bus's channels, before the pin is read
	const bool enabled = wanted.toBool();
	const bool previous = matrix->enabled(static_cast<ch_cnt_t>(trackChannel),
		static_cast<ch_cnt_t>(processorChannel));
	// The engine's own write path: updates the caches and emits dataChanged, so
	// a live PinConnector view and the audio router both see the change.
	matrix->setPin(static_cast<ch_cnt_t>(trackChannel),
		static_cast<ch_cnt_t>(processorChannel), enabled);

	QJsonObject result;
	result.insert(QStringLiteral("target"), target.id);
	result.insert(QStringLiteral("device"), deviceId);
	result.insert(QStringLiteral("direction"), direction);
	result.insert(QStringLiteral("track_channel"), trackChannel);
	result.insert(QStringLiteral("processor_channel"), processorChannel);
	result.insert(QStringLiteral("enabled"), enabled);
	result.insert(QStringLiteral("previous"), previous);
	result.insert(QStringLiteral("ports"), portsModelJson(model));

	QJsonObject beforeState;
	beforeState.insert(QStringLiteral("enabled"), previous);
	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("target"), target.id);
	inverseArgs.insert(QStringLiteral("device"), deviceId);
	inverseArgs.insert(QStringLiteral("direction"), direction);
	inverseArgs.insert(QStringLiteral("track_channel"), trackChannel);
	inverseArgs.insert(QStringLiteral("processor_channel"), processorChannel);
	inverseArgs.insert(QStringLiteral("enabled"), previous);
	QJsonObject transaction = transactionPayload(beforeState,
		QStringLiteral("port.set_pin"), inverseArgs, true,
		QStringLiteral("snapshot: one pin is one bool in the processor's own <pins> element, and "
			"the recorded inverse is port.set_pin with the value captured before the write; there "
			"is no JournallingObject behind an AudioPortsModel, so no live checkpoint exists"));
	// `applies: command` is what makes control.undo dispatch the recorded
	// inverse through the registry instead of the journal.
	QJsonObject inverse = transaction.value(QStringLiteral("inverse")).toObject();
	inverse.insert(QStringLiteral("applies"), QStringLiteral("command"));
	transaction.insert(QStringLiteral("inverse"), inverse);
	result.insert(QStringLiteral("__transaction"), transaction);
	return ControlResult::success(result);
}

//! One entry of port.set_pins, validated against the matrix it names; false with *error on the first bad field.
bool parsePinEdit(const ControlTarget& target, AudioPortsModel* model, const QJsonValue& value,
	AudioPortsModel::PinEdit* edit, ControlResult* error)
{
	const QJsonObject entry = value.toObject();
	const QString direction = entry.value(QStringLiteral("direction")).toString();
	AudioPortsModel::Matrix* matrix = matrixForDirection(model, direction, error);
	if (matrix == nullptr) { return false; }
	const int trackChannel = entry.value(QStringLiteral("track_channel")).toInt(-1);
	const int processorChannel = entry.value(QStringLiteral("processor_channel")).toInt(-1);
	const QJsonValue enabled = entry.value(QStringLiteral("enabled"));
	const int tracks = trackChannelBound(target, *matrix);
	if (!enabled.isBool() || trackChannel < 0 || trackChannel >= tracks
		|| processorChannel < 0 || processorChannel >= static_cast<int>(matrix->channelCount()))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("each pin needs a boolean 'enabled', a track_channel in 0..%1 and a processor_channel "
				"in 0..%2 of the %3 matrix").arg(tracks - 1)
				.arg(static_cast<int>(matrix->channelCount()) - 1).arg(direction));
		return false;
	}
	*edit = {matrix->isOutput(), static_cast<ch_cnt_t>(trackChannel), static_cast<ch_cnt_t>(processorChannel),
		enabled.toBool()};
	return true;
}

/*! The undo and redo of port.set_pins: @a pins on the device @a deviceId of @a targetId, re-resolved each time (a
 *  journal step outlives the pointer it was recorded with). A pin the matrix no longer has is skipped. */
void reapplyPins(const QString& targetId, const QString& deviceId, std::vector<AudioPortsModel::PinEdit> pins)
{
	ControlTarget target;
	ControlResult ignored;
	if (!resolveControlTarget(targetId, &target, &ignored)) { return; }
	AudioPortsModel* model = writableModel(target, deviceId, QStringLiteral("in"), &ignored);
	if (model == nullptr) { return; }
	fitPinsToHostBus(targetId, model);
	const auto inside = [model](const AudioPortsModel::PinEdit& pin) {
		const AudioPortsModel::Matrix& matrix = pin.output ? model->out() : model->in();
		return pin.trackChannel < matrix.trackChannelCount() && pin.processorChannel < matrix.channelCount();
	};
	pins.erase(std::remove_if(pins.begin(), pins.end(), [&inside](const auto& pin) { return !inside(pin); }), pins.end());
	model->setPins(pins);
}

//! The most pins one port.set_pins call takes: every pin of two 64 x 64 matrices.
constexpr int MaxPinsPerBatch = 2 * 64 * 64;

ControlResult handlePortSetPins(const QJsonObject& args)
{
	ControlResult error;
	ControlTarget target;
	if (!resolveControlTarget(args.value(QStringLiteral("target")).toString(), &target, &error)) { return error; }
	const QString deviceId = args.value(QStringLiteral("device")).toString();
	AudioPortsModel* model = writableModel(target, deviceId, QStringLiteral("in"), &error);
	if (model == nullptr) { return error; }
	const QJsonArray pins = args.value(QStringLiteral("pins")).toArray();
	if (pins.isEmpty() || pins.size() > MaxPinsPerBatch)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'pins' needs 1 .. %1 entries").arg(MaxPinsPerBatch));
	}
	// Every entry is validated before a single pin moves, so a refusal changes nothing.
	std::vector<AudioPortsModel::PinEdit> edits(static_cast<std::size_t>(pins.size()));
	for (int i = 0; i < pins.size(); ++i)
	{
		if (!parsePinEdit(target, model, pins.at(i), &edits[static_cast<std::size_t>(i)], &error)) { return error; }
	}
	fitPinsToHostBus(target.id, model);  // Q050-155.2: a discrete bus's channels, before any pin is read
	// The undo step sets the same pins back to the values read here, before any write.
	std::vector<AudioPortsModel::PinEdit> before = edits;
	for (AudioPortsModel::PinEdit& edit : before)
	{
		edit.enabled = (edit.output ? model->out() : model->in()).enabled(edit.trackChannel, edit.processorChannel);
	}
	model->setPins(edits);
	const QString targetId = target.id;
	addUndoStep([targetId, deviceId, before]() { reapplyPins(targetId, deviceId, before); },
		[targetId, deviceId, edits]() { reapplyPins(targetId, deviceId, edits); });
	Engine::getSong()->setModified();

	QJsonObject result{{QStringLiteral("target"), target.id}, {QStringLiteral("device"), deviceId},
		{QStringLiteral("set"), pins.size()}, {QStringLiteral("ports"), portsModelJson(model)},
		{QStringLiteral("bus"), hostBusJson(target, model)}};
	return ControlResult::success(result);
}

} // namespace

int controlHostBusChannels(const QString& targetId)
{
	ControlResult ignored;
	if (!targetId.startsWith(QLatin1String("ch-"))) { return 2; }
	const MixerChannel* channel = resolveMixerChannel(targetId, &ignored);
	if (channel == nullptr || !surround::isDiscrete(channel->m_surround.format())) { return 2; }
	return surround::channelCount(channel->m_surround.format());
}

void fitPinsToHostBus(const QString& targetId, AudioPortsModel* model)
{
	if (model != nullptr) { model->growTrackChannels(controlHostBusChannels(targetId)); }
}

void registerPortCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("port.get_state");
		cmd.group = QStringLiteral("port");
		cmd.verb = QStringLiteral("get_state");
		cmd.description = QStringLiteral("A device's audio-ports model: the input and output pin "
			"matrices, their channel counts and names, the engine's own used-channel caches, and the bus "
			"its track channels address (a discrete-N mixer channel: N channels by index; any other host: "
			"the stereo pair). Read-only.");
		cmd.argsSchema = objectSchema(
			{{QStringLiteral("target"), stringProperty()},
				{QStringLiteral("device"), stringProperty()}},
			{QStringLiteral("target"), QStringLiteral("device")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("target"), stringProperty()},
			{QStringLiteral("device"), stringProperty()},
			{QStringLiteral("ports"), objectProperty()},
			{QStringLiteral("bus"), objectProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.handler = [](const QJsonObject& args) { return handlePortGetState(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("port.set_pin");
		cmd.group = QStringLiteral("port");
		cmd.verb = QStringLiteral("set_pin");
		cmd.description = QStringLiteral("Set one pin of a device's audio-ports matrix (the pin "
			"connector's own write, AudioPortsModel::Matrix::setPin). Reversible: control.undo "
			"re-dispatches this command with the value the pin held before the write.");
		cmd.argsSchema = objectSchema(
			{{QStringLiteral("target"), stringProperty()},
				{QStringLiteral("device"), stringProperty()},
				{QStringLiteral("direction"), enumProperty(
					{QStringLiteral("in"), QStringLiteral("out")})},
				{QStringLiteral("track_channel"), integerProperty(0, MaxTrackChannels - 1)},
				{QStringLiteral("processor_channel"), integerProperty(0, 255)},
				{QStringLiteral("enabled"), booleanProperty()}},
			{QStringLiteral("target"), QStringLiteral("device"),
				QStringLiteral("direction"), QStringLiteral("track_channel"),
				QStringLiteral("processor_channel"), QStringLiteral("enabled")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("target"), stringProperty()},
			{QStringLiteral("device"), stringProperty()},
			{QStringLiteral("direction"), stringProperty()},
			{QStringLiteral("track_channel"), integerProperty()},
			{QStringLiteral("processor_channel"), integerProperty()},
			{QStringLiteral("enabled"), booleanProperty()},
			{QStringLiteral("previous"), booleanProperty()},
			{QStringLiteral("ports"), objectProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return handlePortSetPin(args); };
		registry.registerCommand(cmd);
	}

	{
		// Q050-155.2: a whole routing - up to every pin of a 64-channel device - as one edit and one undo step.
		ControlCommand cmd;
		cmd.id = QStringLiteral("port.set_pins");
		cmd.group = QStringLiteral("port");
		cmd.verb = QStringLiteral("set_pins");
		cmd.description = QStringLiteral("Set many pins of a device's audio-ports matrices as one routing edit "
			"(the render sees all of them or none). 'pins' lists {direction, track_channel, processor_channel, "
			"enabled}, up to 8192. A device in a discrete-N mixer channel reaches channels 0..N-1 by index "
			"(port.get_state's 'bus'). One undo step on the engine's own history (control.undo, Edit > Undo) "
			"puts back the values the pins held before; redo applies them again.");
		QJsonObject pinsProperty = arrayProperty();
		pinsProperty.insert(QStringLiteral("items"), objectSchema(
			{{QStringLiteral("direction"), enumProperty({QStringLiteral("in"), QStringLiteral("out")})},
				{QStringLiteral("track_channel"), integerProperty(0, MaxTrackChannels - 1)},
				{QStringLiteral("processor_channel"), integerProperty(0, 255)},
				{QStringLiteral("enabled"), booleanProperty()}},
			{QStringLiteral("direction"), QStringLiteral("track_channel"),
				QStringLiteral("processor_channel"), QStringLiteral("enabled")}));
		cmd.argsSchema = objectSchema(
			{{QStringLiteral("target"), stringProperty()},
				{QStringLiteral("device"), stringProperty()},
				{QStringLiteral("pins"), pinsProperty}},
			{QStringLiteral("target"), QStringLiteral("device"), QStringLiteral("pins")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("target"), stringProperty()},
			{QStringLiteral("device"), stringProperty()},
			{QStringLiteral("set"), integerProperty()},
			{QStringLiteral("ports"), objectProperty()},
			{QStringLiteral("bus"), objectProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return handlePortSetPins(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
