/*
 * HelpTasks.cpp - help by task: the manual's sections as the tasks commands, shortcuts and controls belong to
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

#include "HelpTasks.h"

#include <array>
#include <utility>

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QMainWindow>
#include <QMap>
#include <QTextDocumentFragment>
#include <QWidget>

#include "CommandMetadata.h"
#include "ContextHelp.h"
#include "ControlRegistry.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "ShortcutsPage.h"

namespace lmms::gui::help
{

namespace
{

//! A manual section: its anchor, its heading (the manual's own words), more words it is looked up by, and the
//! command groups whose commands it covers.
struct Task
{
	const char* topic;
	const char* title;
	const char* words;
	const char* groups;
};

const std::array<Task, 13>& tasks()
{
	static const std::array<Task, 13> list{{
		// Q050-323: the task-indexed quick start leads the manual; it owns no command group.
		{"quickstart", QT_TRANSLATE_NOOP("HelpTasks", "Quick start"), "quick start first beat task how to begin "
			"getting started", ""},
		{"window", QT_TRANSLATE_NOOP("HelpTasks", "The window"), "header browser inspector panel view zoom", "window view"},
		{"arrange", QT_TRANSLATE_NOOP("HelpTasks", "Arranging"), "arranger playlist timeline clip track automation marker "
			"loop tempo transport", "arrangement clip track automation range section selection marker location warp video "
			"bounce freeze transport sample"},
		{"notes", QT_TRANSLATE_NOOP("HelpTasks", "Editing notes"), "piano roll midi notes velocity chord scale quantize "
			"groove tuning", "note chord scale roll groove mts key_names detect"},
		{"mix", QT_TRANSLATE_NOOP("HelpTasks", "Mixing"), "mixer level volume pan send fader bus group meter loudness "
			"master", "mixer bus vca routing port meter mastering dsp pdc monitor reference stem"},
		{"devices", QT_TRANSLATE_NOOP("HelpTasks", "Devices"), "instrument effect plug-in plugin vst clap preset rack "
			"macro modulator lfo", "plugin chain rack modulator device patcher oop"},
		{"record", QT_TRANSLATE_NOOP("HelpTasks", "Recording"), "record arm input take comp punch audio interface midi "
			"keyboard", "record comp audio midi tuner"},
		{"launch", QT_TRANSLATE_NOOP("HelpTasks", "The launcher"), "session clip launch scene live performance link sync",
			"session livecode link clock"},
		{"workflows", QT_TRANSLATE_NOOP("HelpTasks", "Patterns, clips and cells"), "pattern patternstore patterntrack "
			"patternclip midi clip launcher cell scene session mixer channel routing arrange", ""},
		{"projects", QT_TRANSLATE_NOOP("HelpTasks", "Projects"), "save open file export render bounce import revisions "
			"collaborate", "project revisions dawproject interchange render export browser collab"},
		{"agents", QT_TRANSLATE_NOOP("HelpTasks", "Scripts, agents and controllers"), "script lua agent socket mcp "
			"controller surface midi learn remote", "script control controller surface remote"},
		{"customise", QT_TRANSLATE_NOOP("HelpTasks", "Customising and the Window menu"), "settings preferences theme "
			"colours shortcuts keys layout workflow macro record a workflow", "settings shortcuts theme workflow crash telemetry "
			"feedback safestart"},
		{"access", QT_TRANSLATE_NOOP("HelpTasks", "Access"), "accessibility screen reader keyboard zoom contrast help ask "
			"tour", "app"},
	}};
	return list;
}

const QHash<QString, QString>& groupTopics()
{
	static const QHash<QString, QString> map = [] {
		QHash<QString, QString> out;
		for (const Task& task : tasks())
		{
			for (const QString& group : QString::fromLatin1(task.groups).split(QLatin1Char(' ')))
			{
				// A section that introduces a model but owns no commands has no
				// command group; it must not claim the empty one.
				if (!group.isEmpty()) { out.insert(group, QLatin1String(task.topic)); }
			}
		}
		return out;
	}();
	return map;
}

const Task* taskOf(const QString& topic)
{
	for (const Task& task : tasks())
	{
		if (topic == QLatin1String(task.topic)) { return &task; }
	}
	return nullptr;
}

//! The task a kind of panel is about, by its class name (the first match wins: an instrument track's window is a
//! device's, not the arranger's).
QString classTopic(const char* className)
{
	static const std::array<std::pair<const char*, const char*>, 18> kinds{{{"Mixer", "mix"}, {"PianoRoll", "notes"},
		{"NoteEditor", "notes"}, {"Instrument", "devices"}, {"Effect", "devices"}, {"Plugin", "devices"}, {"Rack", "devices"},
		{"Device", "devices"}, {"Session", "launch"}, {"Launch", "launch"}, {"Record", "record"}, {"Take", "record"},
		{"Automation", "arrange"}, {"SongEditor", "arrange"}, {"Arranger", "arrange"}, {"Track", "arrange"},
		{"Browser", "window"}, {"Inspector", "window"}}};
	const QString name = QLatin1String(className);
	for (const auto& [needle, topic] : kinds)
	{
		if (name.contains(QLatin1String(needle))) { return QLatin1String(topic); }
	}
	return QString();
}

//! What @a widget itself says about its task: a declared topic, its command's, or its kind's.
QString ownTopic(const QWidget* widget)
{
	const QString declared = widget->property("helpTopic").toString();
	if (topics().contains(declared)) { return declared; }
	const QString byCommand = commandTopic(widget->property("controlCommand").toString());
	return byCommand.isEmpty() ? classTopic(widget->metaObject()->className()) : byCommand;
}

QString controlName(const QWidget* widget)
{
	if (!widget->accessibleName().isEmpty()) { return widget->accessibleName(); }
	const QString text = widget->property("text").toString().remove(QLatin1Char('&'));
	return text.isEmpty() ? QString::fromLatin1(widget->metaObject()->className()) : text;
}

//! What the control says of itself - its What's This, its tooltip or its description - as plain text.
QString controlHelp(const QWidget* widget)
{
	QString html = widget->whatsThis();
	if (html.isEmpty()) { html = widget->toolTip(); }
	if (html.isEmpty()) { html = widget->accessibleDescription(); }
	return QTextDocumentFragment::fromHtml(html).toPlainText().trimmed();
}

//! The key a menu gives command @a id in @a widget's main window, or empty.
QString shortcutFor(const QWidget* widget, const QString& id)
{
	auto* main = qobject_cast<QMainWindow*>(widget->window());
	if (main == nullptr && getGUI() != nullptr) { main = getGUI()->mainWindow(); }
	return main != nullptr ? commandShortcuts(main->menuBar()).value(id) : QString();
}

void addCommand(QJsonObject* out, const QWidget* widget)
{
	const QString id = ContextHelp::commandFor(widget);  // its own, or its two nearest parents', as hover help reads it
	if (id.isEmpty() || !ControlRegistry::instance()->hasCommand(id)) { return; }
	out->insert(QStringLiteral("command"), id);
	out->insert(QStringLiteral("command_name"), commandName(id));
	out->insert(QStringLiteral("command_help"), ContextHelp::helpFor(widget, false));
	const QString shortcut = shortcutFor(widget, id);
	if (!shortcut.isEmpty()) { out->insert(QStringLiteral("shortcut"), shortcut); }
}

QString tr(const char* text)
{
	return QCoreApplication::translate("HelpTasks", text);
}

//! One command as the reference shows it: its friendly name, its id and shortcut, its description and arguments
//! (the required ones in bold).
QString commandEntry(const ControlCommand& command, const QString& shortcut)
{
	QStringList args;
	const QJsonArray required = command.argsSchema.value(QStringLiteral("required")).toArray();
	const QJsonObject properties = command.argsSchema.value(QStringLiteral("properties")).toObject();
	for (auto it = properties.begin(); it != properties.end(); ++it)
	{
		args << (required.contains(it.key()) ? QStringLiteral("<b>%1</b>").arg(it.key()) : it.key());
	}
	const QString keys = shortcut.isEmpty() ? QString() : QStringLiteral(" &nbsp; %1 %2").arg(tr("Shortcut:"), shortcut.toHtmlEscaped());
	return QStringLiteral("<h3>%1</h3><p><code>%2</code>%3</p><p>%4</p><p><i>%5</i> %6</p>").arg(commandName(command.id).toHtmlEscaped(),
		command.id, keys, command.description.toHtmlEscaped(), tr("Arguments:"),
		args.isEmpty() ? tr("none") : args.join(QStringLiteral(", ")));
}

bool referenceMatches(const ControlCommand& command, const QString& needle, const QString& shortcut)
{
	if (needle.isEmpty()) { return true; }
	const QString topic = commandTopic(command.id);
	const QString words = QStringList{command.id, commandName(command.id), command.description, shortcut, topicTitle(topic),
		topicWords(topic)}.join(QLatin1Char(' '));
	return words.contains(needle, Qt::CaseInsensitive);
}

} // namespace

const QStringList& topics()
{
	static const QStringList list = [] {
		QStringList out;
		for (const Task& task : tasks()) { out.append(QLatin1String(task.topic)); }
		return out;
	}();
	return list;
}

QString topicTitle(const QString& topic)
{
	const Task* task = taskOf(topic);
	return task != nullptr ? tr(task->title) : QString();
}

QString topicWords(const QString& topic)
{
	const Task* task = taskOf(topic);
	return task != nullptr ? QString::fromLatin1(task->words) : QString();
}

QString commandTopic(const QString& id)
{
	return id.isEmpty() ? QString() : groupTopics().value(id.section(QLatin1Char('.'), 0, 0));
}

QString menuTopic(const QString& command, const QString& menuPath)
{
	const QString byCommand = commandTopic(command);
	if (!byCommand.isEmpty()) { return byCommand; }
	static const QHash<QString, QString> menus{{QStringLiteral("file"), QStringLiteral("projects")},
		{QStringLiteral("edit"), QStringLiteral("arrange")}, {QStringLiteral("view"), QStringLiteral("window")},
		{QStringLiteral("window"), QStringLiteral("customise")}, {QStringLiteral("workflows"), QStringLiteral("customise")},
		{QStringLiteral("project tools"), QStringLiteral("customise")}, {QStringLiteral("help"), QStringLiteral("access")}};
	const QString top = menuPath.section(QChar(0x25B8), 0, 0).remove(QLatin1Char('&')).trimmed().toLower();
	return menus.value(top, QStringLiteral("window"));
}

QString widgetTopic(const QWidget* widget)
{
	for (const QWidget* at = widget; at != nullptr; at = at->parentWidget())
	{
		const QString topic = ownTopic(at);
		if (!topic.isEmpty()) { return topic; }
	}
	return QStringLiteral("window");
}

QJsonObject contextHelp(const QWidget* widget)
{
	const QString topic = widget != nullptr ? widgetTopic(widget) : QStringLiteral("window");
	QJsonObject out{{QStringLiteral("topic"), topic}, {QStringLiteral("topic_title"), topicTitle(topic)}};
	if (widget == nullptr) { return out; }
	out.insert(QStringLiteral("name"), controlName(widget));
	out.insert(QStringLiteral("help"), controlHelp(widget));
	addCommand(&out, widget);
	return out;
}

QString contextHtml(const QJsonObject& context)
{
	const auto text = [&context](const char* key) { return context.value(QLatin1String(key)).toString().toHtmlEscaped(); };
	const QString more = tr("More in the manual: %1").arg(QStringLiteral("<a href=\"topic:%1\">%2</a>").arg(text("topic"), text("topic_title")));
	if (!context.contains(QStringLiteral("name")))
	{
		return QStringLiteral("<p>%1 %2</p>").arg(tr("Nothing has the keyboard focus."), more);
	}
	QString html = QStringLiteral("<h3>%1</h3>").arg(text("name"));
	if (!text("help").isEmpty()) { html += QStringLiteral("<p>%1</p>").arg(text("help")); }
	if (context.contains(QStringLiteral("command")))
	{
		html += QStringLiteral("<p><b>%1</b> %2 <small>(%3)</small> - %4</p>").arg(tr("Runs:"), text("command_name"),
			text("command"), text("command_help"));
	}
	if (context.contains(QStringLiteral("shortcut")))
	{
		html += QStringLiteral("<p><b>%1</b> %2</p>").arg(tr("Shortcut:"), text("shortcut"));
	}
	return html + QStringLiteral("<p>%1</p>").arg(more);
}

QHash<QString, QString> commandShortcuts(QMenuBar* bar)
{
	QHash<QString, QString> out;
	if (bar == nullptr) { return out; }
	for (const PaletteEntry& entry : shortcutEntries(bar))
	{
		if (!entry.command.isEmpty() && !out.contains(entry.command)) { out.insert(entry.command, entry.shortcut); }
	}
	return out;
}

QString referenceHtml(const QString& filter, const QHash<QString, QString>& shortcuts)
{
	const QString needle = filter.trimmed();
	QMap<int, QMap<QString, QString>> byTask;  // task (the manual's order; "other" last) -> friendly name -> entry
	for (const QString& id : ControlRegistry::instance()->commandIds())
	{
		const ControlCommand* command = ControlRegistry::instance()->command(id);
		if (command == nullptr || !referenceMatches(*command, needle, shortcuts.value(id))) { continue; }
		const auto task = topics().indexOf(commandTopic(id));
		byTask[task < 0 ? static_cast<int>(topics().size()) : static_cast<int>(task)].insert(commandName(id) + id,
			commandEntry(*command, shortcuts.value(id)));
	}
	QString html;
	for (auto task = byTask.begin(); task != byTask.end(); ++task)
	{
		const bool known = task.key() < topics().size();
		html += known ? QStringLiteral("<h2>%1 <small><a href=\"topic:%2\">%3</a></small></h2>").arg(topicTitle(topics().at(task.key())),
			topics().at(task.key()), tr("read the manual")) : QStringLiteral("<h2>%1</h2>").arg(tr("Other commands"));
		for (const QString& entry : task.value()) { html += entry; }
	}
	return html;
}

} // namespace lmms::gui::help
