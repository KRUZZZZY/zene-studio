/*
 * PluginNativeEditor.cpp - R4.4: the window a plug-in's own editor opens in
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

#include "PluginNativeEditor.h"

#include "CommandForm.h"
#include "CommandLists.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectDeviceMenu.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "AudioBusHandle.h"
#include "Mixer.h"
#include "SampleTrack.h"
#include "Song.h"

#include <utility>

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace lmms::gui
{

namespace
{

//! The device a header's commands name: the effect's chain owner and fx-<n>, or a track's instrument.
QJsonObject ownerAddress(const PluginNativeEditor::Owner& owner)
{
	if (owner.effect != nullptr)
	{
		QJsonObject address = chainAddress(owner.effect->effectChain());
		address.insert(QStringLiteral("plugin"), control::effectIdOf(owner.effect));
		return address;
	}
	if (owner.instrument != nullptr && owner.instrument->instrumentTrack() != nullptr)
	{
		return {{QStringLiteral("target"), control::trackIdOf(owner.instrument->instrumentTrack())}, {QStringLiteral("plugin"), QStringLiteral("inst")}};
	}
	return {};
}

//! A header button running @a id with the device's address (read when clicked) and @a extra; a form when @a ask.
QToolButton* headerButton(QWidget* parent, const QString& text, const QString& tip, const char* id,
	const PluginNativeEditor::Owner& owner, const QJsonObject& extra = {}, bool ask = false)
{
	auto* button = new QToolButton(parent);
	button->setText(text);
	button->setToolTip(tip);
	button->setProperty("controlCommand", QLatin1String(id));
	QObject::connect(button, &QToolButton::clicked, parent, [parent, id, owner, extra, ask] {
		QJsonObject args = ownerAddress(owner);
		for (auto it = extra.begin(); it != extra.end(); ++it) { args.insert(it.key(), it.value()); }
		runCommand(parent, CommandCall{QLatin1String(id), args, {}, QString(), ask});
	});
	return button;
}

//! The effect's Bypass toggle (plugin.bypass). A refusal is shown and puts the button back, and the
//! button follows the effect's own switch when something else (the rack's LED, an agent) flips it.
QToolButton* bypassButton(QWidget* header, const PluginNativeEditor::Owner& owner)
{
	auto* bypass = new QToolButton(header);
	bypass->setText(QObject::tr("Bypass"));
	bypass->setToolTip(QObject::tr("Switch this effect off: its input passes through unprocessed"));
	bypass->setCheckable(true);
	bypass->setChecked(!owner.effect->isEnabled());
	bypass->setProperty("controlCommand", QStringLiteral("plugin.bypass"));
	const QPointer<Effect> effect = owner.effect;
	const auto follow = [bypass, effect] {
		if (effect.isNull() || bypass->isChecked() == !effect->isEnabled()) { return; }
		const QSignalBlocker block(bypass);
		bypass->setChecked(!effect->isEnabled());
	};
	QObject::connect(bypass, &QToolButton::toggled, header, [header, owner, follow](bool on) {
		QJsonObject args = ownerAddress(owner);
		args.insert(QStringLiteral("bypass"), on);
		if (!runCommand(header, CommandCall{QStringLiteral("plugin.bypass"), args, {}, QString(), false}).ok) { follow(); }
	});
	auto* poll = new QTimer(bypass);
	QObject::connect(poll, &QTimer::timeout, bypass, follow);
	poll->start(500);
	return bypass;
}

//! The host's row above a plug-in's own view: bypass and A/B for an effect, presets for both.
QWidget* hostHeader(QWidget* parent, const PluginNativeEditor::Owner& owner)
{
	if (owner.effect == nullptr && owner.instrument == nullptr) { return nullptr; }
	auto* header = new QWidget(parent);
	header->setObjectName(QStringLiteral("pluginHostHeader"));
	auto* row = new QHBoxLayout(header);
	row->setContentsMargins(6, 3, 6, 3);
	if (owner.effect != nullptr)
	{
		row->addWidget(bypassButton(header, owner));
		row->addWidget(headerButton(header, QObject::tr("Store A"), QObject::tr("Keep the current settings in slot A"), "plugin.ab",
			owner, {{QStringLiteral("action"), QStringLiteral("store")}, {QStringLiteral("slot"), QStringLiteral("a")}}));
		row->addWidget(headerButton(header, QObject::tr("Store B"), QObject::tr("Keep the current settings in slot B"), "plugin.ab",
			owner, {{QStringLiteral("action"), QStringLiteral("store")}, {QStringLiteral("slot"), QStringLiteral("b")}}));
		row->addWidget(headerButton(header, QObject::tr("A/B"),
			QObject::tr("Compare: keep the current settings in the slot you hear and switch to the other one"), "plugin.ab",
			owner, {{QStringLiteral("action"), QStringLiteral("toggle")}}));
	}
	row->addStretch(1);
	row->addWidget(headerButton(header, QObject::tr("Save Preset..."), QObject::tr("Save the current settings as a preset"),
		"plugin.preset_save", owner, {}, true));
	row->addWidget(headerButton(header, QObject::tr("Load Preset..."), QObject::tr("Load a preset onto this device"),
		"plugin.preset_load", owner, {}, true));
	// 0.5.0 L11 (WF-025): every preset of the device - load, rename, remove, save.
	auto* presets = new QToolButton(header);
	presets->setText(QObject::tr("Presets..."));
	presets->setToolTip(QObject::tr("Every preset of this device: load, rename, remove or save one"));
	presets->setProperty("controlCommand", QStringLiteral("plugin.preset_list"));
	QObject::connect(presets, &QToolButton::clicked, header, [header, owner] { showCommandList(header, CommandLists::devicePresets(ownerAddress(owner))); });
	row->addWidget(presets);
	return header;
}

//! The window sized to the plug-in's view plus the host header above it.
void sizeToView(QWidget* window, QWidget* view, const QWidget* header, const PluginNativeEditor::Hooks& hooks)
{
	int width = 0;
	int height = 0;
	if (hooks.size) { hooks.size(&width, &height); }
	if (width <= 0 || height <= 0) { return; }
	view->setMinimumSize(width, height);
	window->resize(width, height + (header != nullptr ? header->sizeHint().height() : 0));
}

//! The window: a close from the window manager detaches the editor first.
class EditorWindow : public QWidget
{
public:
	explicit EditorWindow(std::function<void()> onClose) : m_onClose(std::move(onClose)) {}

protected:
	void closeEvent(QCloseEvent* event) override
	{
		// Once: the window is on its way out, and the editor that owns the callback may go before it does.
		if (std::function<void()> onClose = std::exchange(m_onClose, {})) { onClose(); }
		QWidget::closeEvent(event);
	}

private:
	std::function<void()> m_onClose;
};

} // namespace


PluginNativeEditor::~PluginNativeEditor()
{
	close();
}


bool PluginNativeEditor::open(Hooks hooks, const QString& title, QString* error, Owner owner)
{
	if (isOpen())
	{
		m_window->raise();
		m_window->activateWindow();
		return true;
	}
	auto* window = new EditorWindow([this] {
		if (m_hooks.detach) { m_hooks.detach(); }
		m_hooks = Hooks{};
		// Closed now, deleted once control returns to the event loop: isOpen() is false at once, so an open()
		// in between builds a new window instead of raising this one.
		if (m_window != nullptr) { m_window->deleteLater(); }
		m_window = nullptr;
	});
	window->setWindowTitle(title);
	// 0.5.0 L10 (MX-091): the host's header above the plug-in's own view, which attaches to a native
	// child window below it.
	auto* column = new QVBoxLayout(window);
	column->setContentsMargins(0, 0, 0, 0);
	column->setSpacing(0);
	QWidget* header = hostHeader(window, owner);
	if (header != nullptr) { column->addWidget(header); }
	auto* view = new QWidget(window);
	view->setAttribute(Qt::WA_NativeWindow);
	column->addWidget(view, 1);
	const WId id = view->winId();
	if (!hooks.attach || !hooks.attach(reinterpret_cast<void*>(id), error))
	{
		delete window;
		return false;
	}
	sizeToView(window, view, header, hooks);
	m_hooks = std::move(hooks);
	m_window = window;
	window->show();
	return true;
}


void PluginNativeEditor::close()
{
	if (m_hooks.detach) { m_hooks.detach(); }
	m_hooks = Hooks{};
	if (!m_window.isNull()) { delete m_window.data(); }
}

namespace
{

using DeviceArgs = std::function<QJsonObject()>;

//! The button and its refusal line; @a args names the device when the button is clicked.
QWidget* editorButton(DeviceArgs args, QWidget* parent)
{
	auto* box = new QWidget(parent);
	auto* layout = new QVBoxLayout(box);
	layout->setContentsMargins(0, 0, 0, 0);
	auto* button = new QToolButton(box);
	button->setText(QObject::tr("Show plugin editor"));
	button->setAccessibleName(QObject::tr("Show the plug-in's own editor"));
	button->setProperty("controlCommand", QStringLiteral("plugin.editor_open"));
	auto* reason = new QLabel(box);
	reason->setWordWrap(true);
	reason->hide();
	layout->addWidget(button);
	layout->addWidget(reason);
	QObject::connect(button, &QToolButton::clicked, box, [args = std::move(args), reason] {
		const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("plugin.editor_open"), args());
		reason->setText(result.ok ? QString() : result.errorMessage);
		reason->setVisible(!result.ok);
	});
	return box;
}


} // namespace

QString targetIdOfChain(const EffectChain* chain)
{
	for (Track* track : Engine::getSong()->tracks())
	{
		AudioBusHandle* bus = nullptr;
		if (auto* instrumentTrack = dynamic_cast<InstrumentTrack*>(track)) { bus = instrumentTrack->audioBusHandle(); }
		if (auto* sampleTrack = dynamic_cast<SampleTrack*>(track)) { bus = sampleTrack->audioBusHandle(); }
		if (bus != nullptr && bus->effects() == chain) { return control::trackIdOf(track); }
	}
	Mixer* mixer = Engine::mixer();
	for (int i = 0; i < static_cast<int>(mixer->numChannels()); ++i)
	{
		if (&mixer->mixerChannel(i)->m_fxChain == chain) { return control::channelIdOf(mixer->mixerChannel(i)); }
	}
	return QString();
}

QWidget* makeNativeEditorButton(Instrument* instrument, QWidget* parent)
{
	return editorButton([instrument] {
		const QString track = instrument->instrumentTrack() != nullptr
			? control::trackIdOf(instrument->instrumentTrack()) : QString();
		return QJsonObject{{QStringLiteral("target"), track}, {QStringLiteral("plugin"), QStringLiteral("inst")}};
	}, parent);
}

QWidget* makeNativeEditorButton(Effect* effect, QWidget* parent)
{
	return editorButton([effect] {
		return QJsonObject{{QStringLiteral("target"), targetIdOfChain(effect->effectChain())},
			{QStringLiteral("plugin"), control::effectIdOf(effect)}};
	}, parent);
}

} // namespace lmms::gui
